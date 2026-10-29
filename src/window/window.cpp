#include "window/window.hpp"

#include <windowsx.h>

#include <algorithm>
#include <memory>
#include <new>
#include <utility>

#include "catalog.hpp"
#include "channel.hpp"
#include "gesture.hpp"
#include "log.hpp"
#include "render.hpp"
#include "shell.hpp"
#include "window/chrome.hpp"
#include "window/paint.hpp"
#include "window/session.hpp"
#include "window/state.hpp"
#include "window/view.hpp"

namespace window {
    namespace {
        using namespace detail;

        constexpr wchar_t kClassName[] = L"ViewerMainWindow";

        // 中键的单击与按住以 GetDoubleClickTime 为界：定时器到点即判为按住。
        constexpr UINT_PTR kHoldTimer = 1;

        // 中键按住搬窗口。记下起点后每次都按「当前光标与起点之差」算绝对位置，
        // 而不是累加增量——搬动窗口本身会改变客户区坐标系，累加必然漂。
        void begin_move(HWND hwnd, State& st) {
            ::GetCursorPos(&st.drag_cursor);
            RECT wr{};
            ::GetWindowRect(hwnd, &wr);
            st.drag_origin = {wr.left, wr.top};
        }

        void step_move(HWND hwnd, const State& st) {
            POINT now{};
            if (!::GetCursorPos(&now))
                return;
            ::SetWindowPos(hwnd, nullptr,
                           st.drag_origin.x + (now.x - st.drag_cursor.x),
                           st.drag_origin.y + (now.y - st.drag_cursor.y),
                           0, 0,
                           SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
        }

        // 有键按住就抓捕获，全松开就还回去——否则松在窗口外的那一下会丢。
        void sync_capture(HWND hwnd, const State& st) {
            const bool want = st.input.left || st.input.right || st.input.middle;
            if (want && ::GetCapture() != hwnd)
                ::SetCapture(hwnd);
            else if (!want && ::GetCapture() == hwnd)
                ::ReleaseCapture();
        }

        void apply(HWND hwnd, State& st, gesture::Action action, render::Point delta) {
            switch (action) {
            case gesture::Action::None:
                break;

            case gesture::Action::Next:
                step(hwnd, st, +1);
                break;
            case gesture::Action::Prev:
                step(hwnd, st, -1);
                break;

            case gesture::Action::RotateCW:
                reorient(hwnd, st, render::kRotateCW);
                break;
            case gesture::Action::RotateCCW:
                reorient(hwnd, st, render::kRotateCCW);
                break;
            case gesture::Action::FlipH:
                reorient(hwnd, st, render::kFlipH);
                break;
            case gesture::Action::FlipV:
                reorient(hwnd, st, render::kFlipV);
                break;

            case gesture::Action::Exit:
                ::DestroyWindow(hwnd);
                break;

            case gesture::Action::ToggleFullscreen:
                set_fullscreen(hwnd, st, !st.fullscreen);
                break;

            case gesture::Action::DragWindow:
                // 全屏的窗口没有「位置」可言，拖走只会留下一个几何错乱的全屏态——
                // MoveStep 早有这道闸，这条漏了。
                if (st.fullscreen)
                    break;
                // 判定成立后把控制权整个交给系统的拖动循环，Aero Snap 因此一并生效。
                ::ReleaseCapture();
                ::SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
                break;

            // 三条都要先看有没有图：没有图就没有什么可平移的，重画一次都不该发生。
            // 而失败提示那条路径是整块刷背景再写字（有图时只补图片四周），按住左键
            // 划一下就是一串整屏重刷——白字于是闪成一片。
            case gesture::Action::PanBegin:
                begin_pan(hwnd, st, {st.input.cursor.x, st.input.cursor.y});
                if (st.frame)
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                break;
            case gesture::Action::PanMove:
                if (!st.frame || (delta.x == 0 && delta.y == 0))
                    break; // 回中那一下，没动就别重画
                st.pan.x += delta.x;
                st.pan.y += delta.y;
                ::InvalidateRect(hwnd, nullptr, FALSE);
                break;
            case gesture::Action::PanEnd:
                if (st.frame)
                    ::InvalidateRect(hwnd, nullptr, FALSE); // 回到适应窗口
                break;

            case gesture::Action::MoveBegin:
                ::KillTimer(hwnd, kHoldTimer); // 已经开拖了，单击/按住的判定不用了
                break;
            case gesture::Action::MoveStep:
                // 全屏的窗口没有「位置」可言，拖它只会得到一个尺寸错乱的窗口。
                if (!st.fullscreen)
                    step_move(hwnd, st);
                break;
            case gesture::Action::MoveEnd:
                break;

            case gesture::Action::HoldStart:
                // 锚点在按下那一刻就记下。等判定成立再记的话，指针已经挪过阈值了，
                // 窗口会先跳那么一下才跟上。
                begin_move(hwnd, st);
                ::SetTimer(hwnd, kHoldTimer, ::GetDoubleClickTime(), nullptr);
                break;

            case gesture::Action::CopyImage:
                copy_image(hwnd, st);
                break;
            case gesture::Action::Delete:
                remove_current(hwnd, st, true);
                break;
            case gesture::Action::DeletePermanent:
                remove_current(hwnd, st, false);
                break;

            case gesture::Action::OpenStore:
                // Enter 只在当前这张因缺扩展打不开时响应，平时不占用。
                // 这个条件是解码结果，手势层看不见，所以判在这里。
                if (!st.frame && st.shown) {
                    if (const wchar_t* store = notice_for(st.status).store)
                        shell::open_uri(store);
                }
                break;
            }
        }

        bool mouse_event(UINT msg, gesture::Event& out) {
            switch (msg) {
            case WM_LBUTTONDOWN: out = gesture::Event::LeftDown;
                return true;
            case WM_LBUTTONUP: out = gesture::Event::LeftUp;
                return true;
            case WM_RBUTTONDOWN: out = gesture::Event::RightDown;
                return true;
            case WM_RBUTTONUP: out = gesture::Event::RightUp;
                return true;
            case WM_MBUTTONDOWN: out = gesture::Event::MiddleDown;
                return true;
            case WM_MBUTTONUP: out = gesture::Event::MiddleUp;
                return true;
            case WM_MOUSEMOVE: out = gesture::Event::Move;
                return true;
            default: return false;
            }
        }

        LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
            if (msg == WM_NCCREATE) {
                // 状态在这里诞生、在 WM_DESTROY 里销毁，所有权自始至终只由窗口持有。
                // 创建中途失败时系统会补发 WM_DESTROY，于是既不会漏、也不会双释放——
                // 在 create() 里先 new 后转交才是那条会双释放的路。
                const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
                auto* st = new(std::nothrow) State{};
                if (!st)
                    return FALSE;
                const auto* cfg = static_cast<const config::Settings*>(cs->lpCreateParams);
                st->background = cfg->background;
                st->input.drag_threshold = (std::max)(::GetSystemMetrics(SM_CXDRAG),
                                                      ::GetSystemMetrics(SM_CYDRAG));
                ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
                return ::DefWindowProcW(hwnd, msg, wParam, lParam);
            }

            State* st = state_of(hwnd);
            if (!st)
                return ::DefWindowProcW(hwnd, msg, wParam, lParam);

#ifdef VIEWER_LOG
            // 每一次几何变化都记下来：查这类问题时，「到底跳了几次、分别是谁发的」
            // 是唯一能一锤定音的信息，凭症状猜只会来回打转。
            switch (msg) {
            case WM_WINDOWPOSCHANGING:
            case WM_WINDOWPOSCHANGED:
                {
                    const auto* wp = reinterpret_cast<const WINDOWPOS*>(lParam);
                    VLOG(L"%s (%d,%d %dx%d) flags=%08X",
                         msg == WM_WINDOWPOSCHANGING ? L"POSCHANGING" : L"POSCHANGED ",
                         wp->x, wp->y, wp->cx, wp->cy, static_cast<unsigned>(wp->flags));
                    break;
                }
            case WM_SIZE:
                VLOG(L"SIZE        %dx%d wParam=%d",
                     static_cast<int>(LOWORD(lParam)), static_cast<int>(HIWORD(lParam)),
                     static_cast<int>(wParam));
                break;
            case WM_MOVE:
                VLOG(L"MOVE        (%d,%d)", GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                break;
            default:
                break;
            }
#endif

            // 鼠标全部交给纯状态机裁决，这里只负责翻译消息和执行裁决结果。
            if (gesture::Event ev{}; mouse_event(msg, ev)) {
                const gesture::Point at{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                const gesture::Point prev = st->input.cursor;
                const bool alt = (::GetKeyState(VK_MENU) & 0x8000) != 0;

                const gesture::Action action = gesture::on_mouse(st->input, ev, at, alt);
                if (ev == gesture::Event::MiddleUp)
                    ::KillTimer(hwnd, kHoldTimer); // 单击还是按住，到这儿已经定了

                apply(hwnd, *st, action, {at.x - prev.x, at.y - prev.y});
                // apply 可能走到 Action::Exit：DestroyWindow 是同步的，WM_DESTROY 在它返回
                // 之前就把 st 删了。所以这里必须重新取一次——窗口都没了，也就
                // 没有捕获需要同步。其余四处 apply 调用点在它之后都不再碰 st。
                if (State* alive = state_of(hwnd))
                    sync_capture(hwnd, *alive);
                return 0;
            }

            switch (msg) {
            case WM_CREATE:
                extend_frame(hwnd);
                disable_transitions(hwnd);
                // 通道在这里起、在 WM_DESTROY 里停：它要 HWND 才能回投，而 hwnd 到
                // WM_NCCREATE 才成立。建不起来时下面每处都有空检查，程序退化成没有图。
                for (auto& lane : st->lanes)
                    lane.reset(new(std::nothrow) channel::Decode(hwnd, st->background));
                break;

            case WM_DWMCOMPOSITIONCHANGED:
                extend_frame(hwnd); // 合成开关切换后要重调一次
                return 0;

            case WM_DPICHANGED:
                {
                    // 系统给的建议矩形已经把新缩放率算进去了，照搬即可。边缘命中区
                    // 每次 WM_NCHITTEST 现算 DPI，不用在这里同步。
                    //
                    // 全屏态不插手：窗口矩形由 set_fullscreen 钉在 rcMonitor 上，而 PerMonitorV2
                    // 进程拿到的显示器矩形是物理像素、不随缩放率变——照建议矩形改反而会缩错。
                    if (st->fullscreen)
                        return 0;
                    const auto* suggested = reinterpret_cast<const RECT*>(lParam);
                    ::SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                                   suggested->right - suggested->left,
                                   suggested->bottom - suggested->top,
                                   SWP_NOZORDER | SWP_NOACTIVATE);
                    return 0;
                }

            case WM_NCCALCSIZE:
                if (!wParam)
                    break; // 仅处理 wParam == TRUE
                return calc_size(*st, lParam);

            case WM_NCHITTEST:
                return hit_test(hwnd, *st, lParam);

            case WM_SYSCOMMAND:
                // 只有窗口化与全屏两种状态：系统要最大化（Win+↑ 之类），就给它全屏。
                // 低四位是系统自用的，比较前要掩掉。
                if ((wParam & 0xFFF0) == SC_MAXIMIZE) {
                    set_fullscreen(hwnd, *st, true);
                    return 0;
                }
                break;

            case WM_NCACTIVATE:
                // 激活状态一变，DefWindowProc 就要去重画非客户区——而那块已经被
                // NCCALCSIZE 算成零了，它便退化成一条经典主题的标题栏糊在顶上。
                // lParam 传 -1 是文档给的「状态照改，但别重画」。
                return ::DefWindowProcW(hwnd, msg, wParam, -1);

            case WM_MOUSEWHEEL:
                {
                    // 滚轮带的是屏幕坐标，而手势只关心是哪一档，光标位置维持原样。
                    const gesture::Event ev = GET_WHEEL_DELTA_WPARAM(wParam) > 0
                                                  ? gesture::Event::WheelUp
                                                  : gesture::Event::WheelDown;
                    apply(hwnd, *st, gesture::on_mouse(st->input, ev, st->input.cursor, false), {});
                    return 0;
                }

            case WM_CAPTURECHANGED:
                apply(hwnd, *st,
                      gesture::on_mouse(st->input, gesture::Event::Cancel, st->input.cursor, false),
                      {});
                return 0;

            case WM_TIMER:
                if (wParam == kHoldTimer) {
                    ::KillTimer(hwnd, kHoldTimer);
                    apply(hwnd, *st,
                          gesture::on_mouse(st->input, gesture::Event::HoldElapsed,
                                            st->input.cursor, false),
                          {});
                    return 0;
                }
                break;

            case WM_PAINT:
                {
                    PAINTSTRUCT ps{};
                    // BeginPaint 只为清掉更新区。窗口没有重定向表面，往它给的 DC 上画不会上屏。
                    ::BeginPaint(hwnd, &ps);
                    VLOG(L"BEGINPAINT  rcPaint=(%d,%d %dx%d) erase=%d",
                         ps.rcPaint.left, ps.rcPaint.top,
                         ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top,
                         ps.fErase ? 1 : 0);
                    RECT rc{};
                    ::GetClientRect(hwnd, &rc);
                    paint_frame(hwnd, *st, {rc.right, rc.bottom});
                    ::EndPaint(hwnd, &ps);
                    return 0;
                }

            case WM_SIZE:
                // 尺寸一变就当场画完。WM_SIZE 是 SetWindowPos 内部同步派发的，
                // 比等 WM_PAINT 排到下一轮消息循环早一整步。
                ::RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
                return 0;

            case WM_ERASEBKGND:
                return 1; // WM_PAINT 里连背景一起画

            case channel::WM_FRAME_READY:
                {
                    // unique_ptr 负责释放，无论这一帧要不要——投递方已经把所有权交出来了。
                    const std::unique_ptr<channel::Decoded> boxed{
                        reinterpret_cast<channel::Decoded*>(lParam)
                    };                    accept_decoded(hwnd, *st, std::move(*boxed));
                    return 0;
                }

            case channel::WM_CATALOG_READY:
                {
                    const std::unique_ptr<catalog::List> boxed{reinterpret_cast<catalog::List*>(lParam)};
                    accept_list(hwnd, *st, std::move(*boxed));
                    return 0;
                }

            // 全部键在 KeyUp 上触发，不是 KeyDown。理由是手感，不是技术必要性。
            case WM_KEYUP:
                {
                    // Win + 方向键归系统（Snap、最小化），松开的那个方向键照样会发到这里，
                    // 不拦的话 Win+↑ 就成了翻页、Win+← 成了旋转。
                    if ((::GetKeyState(VK_LWIN) & 0x8000) || (::GetKeyState(VK_RWIN) & 0x8000))
                        return 0;
                    const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    const bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    const gesture::Action action =
                        gesture::on_key(st->input, static_cast<unsigned>(wParam), shift, ctrl);
                    VLOG(L"KEYUP       vk=0x%02X shift=%d ctrl=%d -> action=%d",
                         static_cast<unsigned>(wParam), shift ? 1 : 0, ctrl ? 1 : 0,
                         static_cast<int>(action));
                    apply(hwnd, *st, action, {});
                    return 0;
                }

            case WM_DESTROY:
                {
                    ::KillTimer(hwnd, kHoldTimer);

                    // 先停通道：join 回来之后才能保证不会再有新的帧投进来。这是全程唯一
                    // 一次允许 UI 线程等后台线程的地方——窗口都在拆了，手感无从谈起。
                    for (auto& lane : st->lanes)
                        lane.reset();
                    st->scanner = {}; // 移动赋值即 request_stop + join

                    // 队列里可能还压着未取走的帧和列表，逐条排空，否则泄漏。
                    MSG queued;
                    while (::PeekMessageW(&queued, hwnd, channel::WM_FRAME_READY,
                                          channel::WM_FRAME_READY, PM_REMOVE))
                        delete reinterpret_cast<channel::Decoded*>(queued.lParam);
                    while (::PeekMessageW(&queued, hwnd, channel::WM_CATALOG_READY,
                                          channel::WM_CATALOG_READY, PM_REMOVE))
                        delete reinterpret_cast<catalog::List*>(queued.lParam);

                    ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                    delete st;
                    ::PostQuitMessage(0);
                    return 0;
                }

            default:
                break;
            }

            return ::DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    } // namespace

    HWND create(HINSTANCE instance, const config::Settings& cfg) {
        const WNDCLASSEXW wc{
            .cbSize = sizeof(WNDCLASSEXW),
            .style = CS_HREDRAW | CS_VREDRAW,
            .lpfnWndProc = wnd_proc,
            .hInstance = instance,
            .hCursor = ::LoadCursorW(nullptr, IDC_ARROW),
            .lpszClassName = kClassName,
        };
        if (!::RegisterClassExW(&wc))
            return nullptr;

        // 样式一个都不摘：WS_THICKFRAME 换来边缘 resize，WS_MAXIMIZEBOX 是 Aero Snap
        // 的前提——摘掉它 Snap 整个失效。最大化本身在 WM_SYSCOMMAND 里改道成全屏。
        // 无边框只靠 WM_NCCALCSIZE 把非客户区算成零。
        // lpCreateParams 只在 CreateWindowExW 调用期间被 WM_NCCREATE 读走，取址足够。
        const detail::StartupGeometry g = detail::startup_geometry(cfg);
        // WS_EX_NOREDIRECTIONBITMAP：画面走 DXGI 交换链（见 present.hpp），不要 GDI 重定向表面。
        HWND hwnd = ::CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, kClassName, L"Viewer",
                                      WS_OVERLAPPEDWINDOW,
                                      g.x, g.y, g.w, g.h,
                                      nullptr, nullptr, instance,
                                      const_cast<config::Settings*>(&cfg));
        if (!hwnd)
            return nullptr;

        // 创建过程中那次 WM_NCCALCSIZE 的结果不会自己贴到窗口上，得显式要一次
        // frame 变更。少了这一下，标题栏会一直挂到用户第一次拖动边缘为止。
        ::SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                       SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

        ::ShowWindow(hwnd, SW_SHOWNORMAL);
        ::UpdateWindow(hwnd);
        return hwnd;
    }

    void open(HWND hwnd, std::filesystem::path initial) {
        detail::State* st = detail::state_of(hwnd);
        if (!st)
            return;
        // 解码线程要按窗口尺寸缩，而首帧的请求早于第一次 WM_PAINT。
        RECT rc{};
        ::GetClientRect(hwnd, &rc);
        st->client = {rc.right, rc.bottom};

        // 先投解码再起扫描：首帧不依赖任何可以延后的东西
        st->list.files.push_back(initial);
        detail::load_current(hwnd, *st);
        st->scanner = channel::scan(hwnd, std::move(initial));
    }
} // namespace window

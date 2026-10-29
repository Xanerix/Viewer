#include "window/chrome.hpp"

#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>

#include "log.hpp"
#include "window/paint.hpp"

namespace window::detail {
    namespace {
        // 边缘 resize 命中区。原作是 const int edge = 8，这里按 DPI 缩放。
        constexpr int kResizeEdge = 8;

        // 只有两种状态：窗口化与全屏。最大化在建窗时就摘掉了 WS_MAXIMIZEBOX。
        void calc_size_impl(const State& st, RECT& rc) {
            // 全屏时客户区钉在显示器矩形上。
            if (st.fullscreen) {
                rc = st.monitor;
                return;
            }
            // 窗口化：整块归客户区，Snap 与边缘 resize 依然生效
        }

        // 新帧与新几何没有原子提交的 API，只能争取让它们落进同一次合成：
        // 贵的缩放先做完，DwmFlush 等到一次合成刚结束，再 Present 并立刻改几何，
        // 这样两者之间有将近一整帧的余量。不等的话落点随机——实测有时帧先到
        // （全屏帧被压进小窗口），有时几何先到（旧帧被拉大）。
        //
        // 用硬件设备时，进全屏仍会有一帧旧内容被拉大（白字提示上看得最清楚；适应窗口的图
        // 等比缩放，看不出来）。当时日志显示新帧的 Present 比 SetWindowPos 早约 0.02 ms，
        // 且都落在 DwmFlush 之后约 3.5 ms 内，是同一个合成周期里的事——那一帧出在 DWM 内部，
        // 疑似窗口铺满显示器时切进 independent flip 的过渡。WARP 的缓冲走不了那条路。
        void present_before_reframe(HWND hwnd, State& st, render::Size size) {
            prescale(st, size);
            ::DwmFlush();
            paint_frame(hwnd, st, size);
        }
    } // namespace

    void extend_frame(HWND hwnd) {
        constexpr MARGINS margins{0, 0, 1, 0};
        ::DwmExtendFrameIntoClientArea(hwnd, &margins);
    }

    void disable_transitions(HWND hwnd) {
        constexpr BOOL disabled = TRUE;
        ::DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED,
                                &disabled, sizeof(disabled));
    }

    LRESULT calc_size(const State& st, LPARAM lParam) {
        auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
        RECT& rc = p->rgrc[0];

#ifdef VIEWER_LOG
        const RECT in = rc;
#endif
        calc_size_impl(st, rc);
        VLOG(L"NCCALCSIZE  fs=%d  in=(%d,%d %dx%d)  out=(%d,%d %dx%d)",
             st.fullscreen ? 1 : 0,
             in.left, in.top, in.right - in.left, in.bottom - in.top,
             rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
        return 0;
    }

    LRESULT hit_test(HWND hwnd, const State& st, LPARAM lParam) {
        if (st.fullscreen)
            return HTCLIENT; // 全屏态下禁用边缘 resize

        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ::ScreenToClient(hwnd, &pt);
        RECT rc{};
        ::GetClientRect(hwnd, &rc);

        const int edge = ::MulDiv(kResizeEdge, static_cast<int>(::GetDpiForWindow(hwnd)), 96);
        const bool left = pt.x < edge;
        const bool right = pt.x >= rc.right - edge;
        const bool top = pt.y < edge;
        const bool bottom = pt.y >= rc.bottom - edge;

        if (top) return left ? HTTOPLEFT : right ? HTTOPRIGHT : HTTOP;
        if (bottom) return left ? HTBOTTOMLEFT : right ? HTBOTTOMRIGHT : HTBOTTOM;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        return HTCLIENT;
    }

    void set_fullscreen(HWND hwnd, State& st, bool on) {
#ifdef VIEWER_LOG
        RECT wr0{};
        ::GetWindowRect(hwnd, &wr0);
        VLOG(L"set_fullscreen(%d)  cur_fs=%d style=%08X  wnd=(%d,%d %dx%d)",
             on ? 1 : 0, st.fullscreen ? 1 : 0,
             static_cast<unsigned>(::GetWindowLongPtrW(hwnd, GWL_STYLE)),
             wr0.left, wr0.top, wr0.right - wr0.left, wr0.bottom - wr0.top);
#endif

        if (on == st.fullscreen)
            return;

        if (on) {
            st.placement.length = sizeof(st.placement);
            if (!::GetWindowPlacement(hwnd, &st.placement))
                return;

            MONITORINFO mi{};
            mi.cbSize = sizeof(mi);
            if (!::GetMonitorInfoW(::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi))
                return;

            st.monitor = mi.rcMonitor;
            st.fullscreen = true; // 必须在 SetWindowPos 之前——它会触发 NCCALCSIZE

            // 先按全屏尺寸画好并 Present，再动窗口几何。反过来做的话，DWM 先拿到新几何，
            // 新帧要晚一个合成周期才到，中间那帧只能把旧缓冲拉伸着顶上（实测如此）。
            const render::Size full{
                mi.rcMonitor.right - mi.rcMonitor.left,
                mi.rcMonitor.bottom - mi.rcMonitor.top
            };
            present_before_reframe(hwnd, st, full);

            // 窗口矩形必须*精确*等于显示器矩形。早先的做法是把它撑得大一圈，
            // 好让 DWM 画在窗口矩形之内的那圈外框线落到屏幕外；代价是 shell 不再认它
            // 是全屏窗口，任务栏一直压在上面。精确铺满之后任务栏的去留全归 shell 管：
            // 它只给*前台*的全屏窗口让路——Viewer 在前台时自动隐藏的任务栏也唤不出，
            // 一失焦任务栏就回来。这是 shell 的规矩，不归我们处理。
            // 撑大本来也没必要：客户区由 NCCALCSIZE 钉在 rcMonitor 上，此时非客户区
            // 宽度为零，压根没有框线可露。
            VLOG(L"  enter: monitor=(%d,%d %dx%d) dpi=%u  -> SetWindowPos 精确铺满",
                 mi.rcMonitor.left, mi.rcMonitor.top,
                 mi.rcMonitor.right - mi.rcMonitor.left,
                 mi.rcMonitor.bottom - mi.rcMonitor.top, ::GetDpiForWindow(hwnd));
            ::SetWindowPos(hwnd, HWND_TOP,
                           mi.rcMonitor.left, mi.rcMonitor.top,
                           mi.rcMonitor.right - mi.rcMonitor.left,
                           mi.rcMonitor.bottom - mi.rcMonitor.top,
                           SWP_NOOWNERZORDER | SWP_FRAMECHANGED);        }
        else {
            st.fullscreen = false;

            // 退出方向同理：还原后的客户区就是 rcNormalPosition，先按它画好交出去。
            const RECT& n = st.placement.rcNormalPosition;
            present_before_reframe(hwnd, st, {n.right - n.left, n.bottom - n.top});

            ::SetWindowPlacement(hwnd, &st.placement);
            ::SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                           SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                           SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        }

        // 走 GDI 重定向表面的年代，这里有一帧旧图贴在角上：表面在窗口改尺寸时先拿旧内容
        // 顶一帧，同步重画赶不上。SWP_NOCOPYBITS、NCCALCSIZE 返回 WVR_REDRAW 都无效；
        // DWMWA_CLOAK 同样无效，还让 shell 错过全屏判定。所以换成了交换链（present.hpp）。
        // 几何改完再同步画一次，兜住 placement 还原出来的实际尺寸与预期不符的情形。
        ::RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
    }

    // 建窗那一刻还没有 hwnd，所以定位显示器只能靠光标——项目别处用的
    // MonitorFromWindow 在这里还不成立。双击图片启动时，光标就在用户正在用的那块屏上，
    // 窗口该出现在那儿，而不是跑回主屏。
    //
    // 尺寸按 rcMonitor（分辨率）而不是 rcWork（工作区）算：用户说的是「分辨率的四分之一」。
    // 半宽半高的窗口居中后下边缘只到屏幕 3/4 处，压不到任务栏，两者取哪个都不影响观感。
    StartupGeometry startup_geometry(const config::Settings& cfg) {
        POINT cursor{};
        HMONITOR mon = ::GetCursorPos(&cursor)
                                 ? ::MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST)
                                 : ::MonitorFromPoint(POINT{}, MONITOR_DEFAULTTOPRIMARY);

        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (!::GetMonitorInfoW(mon, &mi)) {
            // 问不出显示器就别自作主张，把位置交回给系统。
            const int w = cfg.width > 0 ? cfg.width : 1024;
            const int h = cfg.height > 0 ? cfg.height : 768;
            return {CW_USEDEFAULT, CW_USEDEFAULT, w, h};
        }

        const int sw = mi.rcMonitor.right - mi.rcMonitor.left;
        const int sh = mi.rcMonitor.bottom - mi.rcMonitor.top;

        // 配置没写尺寸就取分辨率的一半见方——横竖各半，面积四分之一。
        // 写了就以分辨率为上限：这个窗口无边框、只能 Alt + 拖来搬，比屏幕大就很难够回来。
        const int w = cfg.width > 0 ? (std::min)(cfg.width, sw) : (std::max)(sw / 2, 1);
        const int h = cfg.height > 0 ? (std::min)(cfg.height, sh) : (std::max)(sh / 2, 1);

        return {mi.rcMonitor.left + (sw - w) / 2, mi.rcMonitor.top + (sh - h) / 2, w, h};
    }
} // namespace window::detail

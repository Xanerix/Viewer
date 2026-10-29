#include "window/paint.hpp"

#include <string>

#include "log.hpp"

namespace window::detail {
    namespace {
        void fill(HDC hdc, const RECT& rc, HBRUSH brush) {
            if (rc.right > rc.left && rc.bottom > rc.top)
                ::FillRect(hdc, &rc, brush);
        }

        void draw_notice(HWND hwnd, HDC hdc, const RECT& client, const Notice& n) {
            // 字号跟着窗口所在显示器的 DPI 走。清单已经声明 PerMonitorV2，
            // 这里用不带 ForDpi 的那个版本就会在高缩放率的屏上小得看不清。
            NONCLIENTMETRICSW ncm{sizeof(ncm)};
            HFONT font = nullptr;
            if (::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0,
                                             ::GetDpiForWindow(hwnd)))
                font = ::CreateFontIndirectW(&ncm.lfMessageFont);
            HGDIOBJ prev = font ? ::SelectObject(hdc, font) : nullptr;

            std::wstring text = n.line;
            if (n.hint) {
                text += L"\n\n";
                text += n.hint;
            }

            ::SetBkMode(hdc, TRANSPARENT);
            ::SetTextColor(hdc, RGB(255, 255, 255));

            // DT_VCENTER 只对单行有效，垂直居中得自己算：先量高度，再把矩形顶边挪上去。
            constexpr UINT format = DT_CENTER | DT_NOPREFIX | DT_WORDBREAK;
            RECT box = client;
            ::DrawTextW(hdc, text.c_str(), -1, &box, format | DT_CALCRECT);
            RECT at{
                client.left, (client.bottom - (box.bottom - box.top)) / 2,
                client.right, client.bottom
            };
            ::DrawTextW(hdc, text.c_str(), -1, &at, format);

            if (prev)
                ::SelectObject(hdc, prev);
            if (font)
                ::DeleteObject(font);
        }

        // 自上而下的 DIB 用负高度表示。GDI 这一步永远是 1:1 拷贝，缩放已经自己算完。
        BITMAPINFO top_down_info(render::Size size) {
            return BITMAPINFO{
                .bmiHeader = {
                    .biSize = sizeof(BITMAPINFOHEADER),
                    .biWidth = size.width,
                    .biHeight = -size.height,
                    .biPlanes = 1,
                    .biBitCount = 32,
                    .biCompression = BI_RGB,
                },
            };
        }
    } // namespace

    Notice notice_for(decode::Status status) {
        switch (status) {
        case decode::Status::NoHeif:
            return {
                L"未安装 HEIF 图像扩展",
                L"按 Enter 前往 Microsoft Store 安装（免费）",
                L"ms-windows-store://pdp/?ProductId=9PMMSR1CGPWG"
            };
        case decode::Status::NoHevc:
            // 这个扩展要付费，提示里必须写明，不能一声不吭把用户送到付款页。
            return {
                L"未安装 HEVC 视频扩展",
                L"HEIC 的像素是 HEVC 编码的，按 Enter 前往 Microsoft Store（该扩展需付费）",
                L"ms-windows-store://pdp/?ProductId=9NMZLZ57R3T7"
            };
        default:
            return {L"无法解码", nullptr, nullptr};
        }
    }

    render::Size prescale(State& st, render::Size client) {
        if (!st.frame || client.width <= 0 || client.height <= 0)
            return {};

        const render::Size target = render::fit(st.shown_size(), client);
        if (target.width <= 0 || (st.surface_scaled && target == st.surface_size))
            return target;

        // 缩放结果所在的坐标系还没施加变换，旋转过 90° 时它的宽高与 target 互换。
        const render::Size size = render::transposes(st.orient)
                                      ? render::Size{target.height, target.width}
                                      : target;

        // 解码线程缩好的那一份还对得上就直接用：命中时这里一个像素都不用算。
        // 对不上只有两种情形——窗口 resize 过，或用户在 EXIF 方向之外又转过。
        const std::vector<uint32_t>* src = nullptr;
        if (st.scaled && st.scaled->size == size) {
            src = &st.scaled->pixels;
        }
        else if (size == render::Size{st.frame->width, st.frame->height}) {
            src = &st.frame->pixels; // 不比窗口大，无需缩
        }
        else if (const size_t slot = render::transposes(st.orient) ? 1 : 0;
            st.scratch_size[slot] == size) {
            // 自己缩过一次就留着。窗口 resize 或全屏切换之后，当前图的 scaled 是按旧
            // 尺寸缩的、再也对不上（图没换就不会重新请求），横向这一路全靠这里兜住。
            src = &st.scratch[slot];
        }
        else {
            std::vector<uint32_t>& dst = st.scratch[slot];
            dst.resize(static_cast<size_t>(size.width) * size.height);
            render::box_downsample(st.frame->pixels, st.frame->width, st.frame->height,
                                   dst, size.width, size.height);
            st.scratch_size[slot] = size;
            src = &dst;
        }

        if (st.orient == render::Orient{}) {
            st.bits = src->data(); // 恒等变换：缩放结果就是要 blit 的东西
        }
        else {
            // 先缩后转：变换那一步只搬缩放结果，代价跟着窗口走而不是跟着原图走。
            // 反过来先转整张原图，连按四次 → 就要搬四遍全图。
            st.surface.resize(static_cast<size_t>(target.width) * target.height);
            render::transform(*src, size.width, size, st.orient, {}, target, st.surface);
            st.bits = st.surface.data();
        }
        st.surface_scaled = true;
        st.surface_size = target;
        return target;
    }

    void paint(HWND hwnd, State& st, HDC hdc, render::Size size) {
        const RECT client{0, 0, size.width, size.height};
        const int cw = client.right;
        const int ch = client.bottom;
        st.client = {cw, ch}; // 往后投的解码请求按这个尺寸缩

        HBRUSH brush = ::CreateSolidBrush(RGB((st.background >> 16) & 0xFF,
                                              (st.background >> 8) & 0xFF,
                                              (st.background >> 0) & 0xFF));

        render::Point at{}; // 图片落在客户区的哪里
        render::Size blit{}; // 实际搬运的尺寸

        if (st.frame && cw > 0 && ch > 0) {
            const render::Size image = st.shown_size();

            if (st.one_to_one()) {
                // 原始比例：钳住平移量，只搬看得见的那一块。v.src 是*呈现*坐标系里的
                // 位置，二面体重映射由 transform 在取像素时一并做掉。
                st.pan = render::clamp_pan(st.pan, image, {cw, ch});
                const render::View v = render::visible(image, {cw, ch}, st.pan);
                if (v.size.width > 0 && v.size.height > 0) {
                    st.surface.resize(static_cast<size_t>(v.size.width) * v.size.height);
                    render::transform(st.frame->pixels, st.frame->width,
                                      {st.frame->width, st.frame->height},
                                      st.orient, v.src, v.size, st.surface);
                    st.surface_scaled = false; // 缩放缓存作废
                    st.surface_size = v.size;
                    st.bits = st.surface.data();
                    at = v.dst;
                    blit = v.size;
                }
            }
            else if (const render::Size target = prescale(st, {cw, ch}); target.width > 0) {
                at = {(cw - target.width) / 2, (ch - target.height) / 2};
                blit = target;
            }
        }

        VLOG(L"PAINT       client=%dx%d blit=%dx%d at=(%d,%d) one2one=%d",
             cw, ch, blit.width, blit.height, at.x, at.y, st.one_to_one() ? 1 : 0);

        if (blit.width <= 0) {
            // 无图或尺寸为零：只填背景色，不画 loading 指示器——
            // 一个转圈动画比空背景更像「卡住了」。
            // 整块刷背景再写字也不会闪：画的是交换链的后台缓冲，Present 之前谁都看不见。
            fill(hdc, client, brush);
            // 已经交货、但没有帧：这一张是解码失败，得说一声。
            // 还在解的那段空窗期 shown 为 false，那时什么都不写。
            if (st.shown && !st.frame)
                draw_notice(hwnd, hdc, client, notice_for(st.status));
            ::DeleteObject(brush);
            return;
        }

        const BITMAPINFO bi = top_down_info(blit);
        const int lines = ::SetDIBitsToDevice(hdc, at.x, at.y, static_cast<DWORD>(blit.width),
                                              static_cast<DWORD>(blit.height),
                                              0, 0, 0, static_cast<UINT>(blit.height),
                                              st.bits, &bi, DIB_RGB_COLORS);
        (void)lines;
#ifdef VIEWER_LOG
        RECT clip{};
        const int clip_kind = ::GetClipBox(hdc, &clip);
        VLOG(L"  blit lines=%d/%d  clip[%d]=(%d,%d %dx%d)", lines, blit.height,
             clip_kind, clip.left, clip.top, clip.right - clip.left, clip.bottom - clip.top);
#endif

        // 只补图片四周，不整块刷——整块刷会在缩放窗口时闪。
        const RECT margins[] = {
            {0, 0, cw, at.y},
            {0, at.y + blit.height, cw, ch},
            {0, at.y, at.x, at.y + blit.height},
            {at.x + blit.width, at.y, cw, at.y + blit.height},
        };
        for (const RECT& m : margins)
            fill(hdc, m, brush);

        ::DeleteObject(brush);
    }

    void paint_frame(HWND hwnd, State& st, render::Size size) {
        if (HDC hdc = st.present.begin(hwnd, size)) {
            paint(hwnd, st, hdc, size);
            st.present.end(hwnd);
        }
    }
} // namespace window::detail

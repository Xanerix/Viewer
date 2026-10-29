#include "window/view.hpp"

#include <cstdint>
#include <vector>

#include "log.hpp"
#include "shell.hpp"

namespace window::detail {
    void center_pan(HWND hwnd, State& st) {
        st.pan = {};
        if (!st.frame)
            return;
        RECT client{};
        ::GetClientRect(hwnd, &client);
        const render::Size image = st.shown_size();
        st.pan = render::clamp_pan({
                                       (client.right - image.width) / 2,
                                       (client.bottom - image.height) / 2
                                   },
                                   image, {client.right, client.bottom});
    }

    void reorient(HWND hwnd, State& st, render::Orient by) {
        if (!st.frame)
            return;
        st.orient = render::compose(by, st.orient);
        st.surface_scaled = false; // 呈现尺寸可能已经宽高互换，缓存作废
        center_pan(hwnd, st);
        ::InvalidateRect(hwnd, nullptr, FALSE);
    }

    void begin_pan(HWND hwnd, State& st, render::Point cursor) {
        center_pan(hwnd, st);
        if (!st.frame)
            return;

        RECT client{};
        ::GetClientRect(hwnd, &client);
        const render::Size image = st.shown_size();
        const render::Size shown = render::fit(image, {client.right, client.bottom});
        if (shown.width <= 0)
            return;

        // 光标落在缩放图上的位置，换算回原图坐标，再倒推出图片左上角该在哪
        const render::Point origin{
            (client.right - shown.width) / 2,
            (client.bottom - shown.height) / 2
        };
        const auto ix = static_cast<int>(
            static_cast<int64_t>(cursor.x - origin.x) * image.width / shown.width);
        const auto iy = static_cast<int>(
            static_cast<int64_t>(cursor.y - origin.y) * image.height / shown.height);

        st.pan = render::clamp_pan({cursor.x - ix, cursor.y - iy},
                                   image, {client.right, client.bottom});
    }

    void copy_image(HWND hwnd, const State& st) {
        if (!st.frame)
            return;
        const render::Size size = st.shown_size();
        if (size.width <= 0)
            return;

        std::vector<uint32_t> oriented(static_cast<size_t>(size.width) * size.height);
        render::transform(st.frame->pixels, st.frame->width,
                          {st.frame->width, st.frame->height},
                          st.orient, {}, size, oriented);
        const bool ok = shell::copy_image(hwnd, oriented.data(), size);
        VLOG(L"COPY        %dx%d -> %s", size.width, size.height, ok ? L"ok" : L"failed");
        (void)ok; // VIEWER_LOG 关掉时它就只是个没人用的局部量
    }
} // namespace window::detail

#include "render.hpp"

#include <algorithm>
#include <cassert>

namespace render {
    Size fit(Size image, Size window) {
        if (image.width <= 0 || image.height <= 0 || window.width <= 0 || window.height <= 0)
            return {};

        // 原图已经装得下：原样呈现，不放大（§6.4）
        if (image.width <= window.width && image.height <= window.height)
            return image;

        const int64_t iw = image.width;
        const int64_t ih = image.height;
        const int64_t ww = window.width;
        const int64_t wh = window.height;

        Size out{};
        if (iw * wh >= ih * ww) {
            // 图比窗口更「宽」：宽度先顶到窗口
            out.width = window.width;
            out.height = static_cast<int>((ih * ww + iw / 2) / iw);
        }
        else {
            out.height = window.height;
            out.width = static_cast<int>((iw * wh + ih / 2) / ih);
        }

        // 极端长宽比下四舍五入可能算出 0，钳到 1 像素
        out.width = std::max(1, out.width);
        out.height = std::max(1, out.height);
        return out;
    }

    void box_downsample(std::span<const uint32_t> src, int sw, int sh,
                        std::span<uint32_t> dst, int dw, int dh) {
        assert(sw > 0 && sh > 0 && dw > 0 && dh > 0);
        assert(dw <= sw && dh <= sh);
        assert(src.size() >= static_cast<size_t>(sw) * static_cast<size_t>(sh));
        assert(dst.size() >= static_cast<size_t>(dw) * static_cast<size_t>(dh));

        for (int y = 0; y < dh; ++y) {
            const int y0 = static_cast<int>(static_cast<int64_t>(y) * sh / dh);
            const int y1 = std::max(y0 + 1,
                                    static_cast<int>(static_cast<int64_t>(y + 1) * sh / dh));

            for (int x = 0; x < dw; ++x) {
                const int x0 = static_cast<int>(static_cast<int64_t>(x) * sw / dw);
                const int x1 = std::max(x0 + 1,
                                        static_cast<int>(static_cast<int64_t>(x + 1) * sw / dw));

                // 极限情况 8000x6000 缩到 1x1，样本数 4.8e7 × 255 会溢出 32 位
                uint64_t b = 0, g = 0, r = 0;
                for (int sy = y0; sy < y1; ++sy) {
                    const uint32_t* row = src.data() + static_cast<size_t>(sy) * sw;
                    for (int sx = x0; sx < x1; ++sx) {
                        const uint32_t p = row[sx];
                        b += (p >> 0) & 0xFFu;
                        g += (p >> 8) & 0xFFu;
                        r += (p >> 16) & 0xFFu;
                    }
                }

                const uint64_t n = static_cast<uint64_t>(x1 - x0) * static_cast<uint64_t>(y1 - y0);
                dst[static_cast<size_t>(y) * dw + x] =
                    0xFF000000u
                    | (static_cast<uint32_t>(r / n) << 16)
                    | (static_cast<uint32_t>(g / n) << 8)
                    | static_cast<uint32_t>(b / n);
            }
        }
    }

    Point clamp_pan(Point pan, Size image, Size window) {
        Point out{};
        out.x = (image.width <= window.width)
                    ? (window.width - image.width) / 2
                    : std::clamp(pan.x, window.width - image.width, 0);
        out.y = (image.height <= window.height)
                    ? (window.height - image.height) / 2
                    : std::clamp(pan.y, window.height - image.height, 0);
        return out;
    }

    View visible(Size image, Size window, Point pan) {
        View v{};
        // pan 为负表示图片左上角在客户区外——那部分正是要跳过的源像素。
        v.src = {std::max(0, -pan.x), std::max(0, -pan.y)};
        v.dst = {std::max(0, pan.x), std::max(0, pan.y)};
        v.size = {
            std::max(0, std::min(image.width - v.src.x, window.width - v.dst.x)),
            std::max(0, std::min(image.height - v.src.y, window.height - v.dst.y))
        };
        return v;
    }

    Orient compose(Orient a, Orient b) {
        // D4 里 f·r = r⁻¹·f，所以把 b 的旋转量挪到左边时，a 带翻转就要取反——
        // 「顺时针」在翻转过的图上朝哪边转，整个就取决于这个负号（§6.2）。
        const int quarter = a.quarter + (a.flip ? -b.quarter : b.quarter);
        return {((quarter % 4) + 4) % 4, a.flip != b.flip};
    }

    bool transposes(Orient o) {
        return (o.quarter & 1) != 0;
    }

    Size oriented(Size src, Orient o) {
        return transposes(o) ? Size{src.height, src.width} : src;
    }

    Orient from_exif(unsigned tag) {
        switch (tag) {
        case 2: return {0, true};
        case 3: return {2, false};
        case 4: return {2, true}; // 180° + 水平翻转 == 垂直翻转
        case 5: return {3, true};
        case 6: return {1, false};
        case 7: return {1, true};
        case 8: return {3, false};
        default: return {}; // 含 1、缺失、以及任何越界值（§4.4）
        }
    }

    void transform(std::span<const uint32_t> src, int src_stride, Size src_size,
                   Orient o, Point at, Size size, std::span<uint32_t> dst) {
        const Size out = oriented(src_size, o);
        assert(src_stride >= src_size.width && src_size.width > 0 && src_size.height > 0);
        assert(o.quarter >= 0 && o.quarter < 4);
        assert(at.x >= 0 && at.y >= 0 && size.width > 0 && size.height > 0);
        assert(at.x + size.width <= out.width && at.y + size.height <= out.height);
        assert(src.size() >= static_cast<size_t>(src_size.height - 1) * src_stride + src_size.width);
        assert(dst.size() >= static_cast<size_t>(size.width) * size.height);
        (void)out;

        const int w = src_size.width;
        const int h = src_size.height;

        // 输出坐标 (x, y) 对应的源坐标是仿射的：src = origin + x * u + y * v。
        // 先按旋转量反解，再把翻转叠在 x 分量上——推导见 §6.2，改这里务必跑单测。
        int sx0 = 0, sxu = 0, sxv = 0;
        int sy0 = 0, syu = 0, syv = 0;
        switch (o.quarter) {
        case 0: sx0 = 0;
            sxu = 1;
            sxv = 0;
            sy0 = 0;
            syu = 0;
            syv = 1;
            break;
        case 1: sx0 = 0;
            sxu = 0;
            sxv = 1;
            sy0 = h - 1;
            syu = -1;
            syv = 0;
            break;
        case 2: sx0 = w - 1;
            sxu = -1;
            sxv = 0;
            sy0 = h - 1;
            syu = 0;
            syv = -1;
            break;
        default: sx0 = w - 1;
            sxu = 0;
            sxv = -1;
            sy0 = 0;
            syu = 1;
            syv = 0;
            break;
        }
        if (o.flip) {
            sx0 = w - 1 - sx0;
            sxu = -sxu;
            sxv = -sxv;
        }

        for (int y = 0; y < size.height; ++y) {
            const int oy = at.y + y;
            const int row_x = sx0 + at.x * sxu + oy * sxv;
            const int row_y = sy0 + at.x * syu + oy * syv;
            uint32_t* out_row = dst.data() + static_cast<size_t>(y) * size.width;

            if (sxu == 1) { // 恒等：整行连续，交给 memmove 而不是逐像素爬
                std::copy_n(src.data() + static_cast<size_t>(row_y) * src_stride + row_x,
                            size.width, out_row);
                continue;
            }

            int sx = row_x;
            int sy = row_y;
            for (int x = 0; x < size.width; ++x) {
                out_row[x] = src[static_cast<size_t>(sy) * src_stride + sx];
                sx += sxu;
                sy += syu;
            }
        }
    }
} // namespace render

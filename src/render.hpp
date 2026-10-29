#pragma once

#include <cstdint>
#include <span>

// 纯函数，不含 windows.h：全项目最容易「算错但没越界」的地方，
// 因此单独成模块以便单元测试
namespace render {
    struct Size {
        int width = 0;
        int height = 0;

        friend bool operator==(const Size&, const Size&) = default;
    };

    struct Point {
        int x = 0;
        int y = 0;

        friend bool operator==(const Point&, const Point&) = default;
    };

    // 1:1 视图下该拷源图的哪一块、画到客户区的哪里。size 任一边为 0 表示没有可见部分。
    struct View {
        Point src;
        Point dst;
        Size size;
    };

    // 二面体群 D4 的一个元素，用 3 位表示：2 位旋转 + 1 位翻转。
    // 语义固定为「先水平翻转（若 flip），再顺时针旋转 quarter 个 90°」——
    // 群里 r 与 f 不交换，这个顺序一旦含糊，合成方向就会反。
    struct Orient {
        int quarter = 0; // 0..3，顺时针 90° 的次数
        bool flip = false; // 水平翻转（沿垂直轴镜像）

        friend bool operator==(const Orient&, const Orient&) = default;
    };

    // 用户视角的四个动作。屏幕空间语义：一律 compose(动作, 当前)，即左乘。
    inline constexpr Orient kRotateCW{1, false};
    inline constexpr Orient kRotateCCW{3, false};
    inline constexpr Orient kFlipH{0, true};
    inline constexpr Orient kFlipV{2, true};

    // 群运算：返回「先施 b、再施 a」的那一个元素。不搬像素。
    Orient compose(Orient a, Orient b);

    // 旋转量为奇数时宽高互换。
    bool transposes(Orient o);

    // 源尺寸经变换后呈现出来的尺寸。
    Size oriented(Size src, Orient o);

    // EXIF tag 274 的 8 个取值。越界或缺失一律按 1。
    Orient from_exif(unsigned tag);

    // 按 o 把源图重映射到输出坐标系，但只写输出坐标系中以 at 起、size 大的那一块，
    // 紧密排布进 dst。at + size 必须落在 oriented(src_size, o) 之内。
    // 这是全项目唯一真正搬运二面体变换的地方——旋转本身只改 Orient。
    void transform(std::span<const uint32_t> src, int src_stride, Size src_size, Orient o, Point at, Size size, std::span<uint32_t> dst);

    // 等比缩放到不超出窗口的最大尺寸。不放大——Viewer 的最大缩放就是 100%。
    // 任一边为非正时返回 {0, 0}。
    Size fit(Size image, Size window);

    // 面积平均的 box 下采样：的唯一缩小路径，单遍线性扫描
    // 要求 0 < dst.width <= sw、0 < dst.height <= sh，dst 至少容纳 dst.width * dst.height。
    // dst 与 src 尺寸相同时退化为拷贝。输出 alpha 恒为 255。
    void box_downsample(std::span<const uint32_t> src, int sw, int sh, std::span<uint32_t> dst, int dw, int dh);

    // 把平移量钳进「图片不露白」的范围。pan 是图片左上角在客户区坐标系里的位置；
    // 某一轴上图片比窗口小时该轴强制居中——那个方向本来就无处可平移。
    Point clamp_pan(Point pan, Size image, Size window);

    // 已钳位的 pan 下，1:1 呈现时实际要搬的那一块。
    View visible(Size image, Size window, Point pan);
} // namespace render

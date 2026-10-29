#pragma once

#include <cstdint>
#include <vector>

#include "render.hpp"

// 跨线程传递的唯一单位：构造完成即不可变
//
// 像素为 BGRA 字节序、自上而下、无行填充（stride 恒为 width * 4）
// alpha 通道恒为 255——带透明度的源在 decode 阶段就已合成到背景色上
// 因为 SetDIBitsToDevice 不做 alpha 混合，渲染层不处理透明
// orient 是 EXIF tag 274 定下的方向，尚未施加到像素上
// 二面体变换只在最终 blit 时做一次索引重映射，解码阶段搬一遍全图是白搬
// 窗口层拿它当旋转状态的初值，用户的每一次旋转再左乘上去
struct Frame {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels; // 小端下即 0xAARRGGBB
    render::Orient orient;
};

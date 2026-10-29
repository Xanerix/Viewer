#pragma once

#include <windows.h>

#include "render.hpp"
#include "window/state.hpp"

// 当前这一张图在窗口里怎么摆：方向、平移，以及照这个摆法复制出去。
namespace window::detail {
    // 1:1 视图的默认落点是居中。图片比窗口小时 clamp_pan 自己会居中，
    // 大的时候得先算出居中值再交给它——直接给 {0,0} 会钉在左上角。
    void center_pan(HWND hwnd, State& st);

    // 旋转与翻转一律左乘：用户按「顺时针」，画面就朝顺时针转，不管此前翻转过没有。
    // 像素一个都不搬，只有下一次 blit 会多做一遍索引重映射。
    void reorient(HWND hwnd, State& st, render::Orient by);

    // 按下左键切到原始比例时，让光标底下那个像素留在原地——否则画面会跳一下。
    void begin_pan(HWND hwnd, State& st, render::Point cursor);

    // Ctrl + C：按屏幕上看到的朝向复制全分辨率位图。缩放和平移不带进去——
    // 剪贴板里该是这张图，不是这个窗口。
    void copy_image(HWND hwnd, const State& st);
} // namespace window::detail

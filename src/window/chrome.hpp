#pragma once

#include <windows.h>

#include "config.hpp"
#include "window/state.hpp"

// 窗口这个「框」本身：无边框、命中测试、全屏、DPI、建窗时的初始几何。
namespace window::detail {
    // NCCALCSIZE 把非客户区算成零之后，DWM 画在那上面的投影和外框线一并消失，
    // 窗口会变成一块没有边界的浮块。把 1 px 的 frame 延进客户区就能取回。
    //
    // 这个状态自始至终不许切换。关掉再开，DWM 要下一个合成周期才重新接管窗口框架，
    // 这中间 USER32 会用旧路径画一遍——就是那条闪一下的经典主题边框。
    void extend_frame(HWND hwnd);

    // DWM 会给窗口的几何变化套一段过渡动画。日志里明明只有一次 SetWindowPos、
    // 客户区一步到位、1.1 ms 后就画完了，眼睛看到的却是「先贴左上角再放大」——
    // 那段放大是合成器自己叠上去的，调消息时序调不掉。
    void disable_transitions(HWND hwnd);

    // 无边框的核心：不摘样式，只改计算。
    LRESULT calc_size(const State& st, LPARAM lParam);

    // 客户区一律 HTCLIENT。返回 HTCAPTION 是无边框窗口的常见偷懒写法，
    // 但 Viewer 的左键是「原始比例 + 平移」，拖窗走 Alt + 左键。
    LRESULT hit_test(HWND hwnd, const State& st, LPARAM lParam);

    void set_fullscreen(HWND hwnd, State& st, bool on);

    struct StartupGeometry {
        int x, y, w, h;
    };

    StartupGeometry startup_geometry(const config::Settings& cfg);
} // namespace window::detail

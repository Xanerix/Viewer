#pragma once

#include <windows.h>

#include "decode.hpp"
#include "render.hpp"
#include "window/state.hpp"

namespace window::detail {
    // 解码失败时画面中心那段白字。无边框窗口的标题只在任务栏悬停或
    // Alt+Tab 时可见，光改标题等于没提示——屏幕上就只剩一片背景色。
    struct Notice {
        const wchar_t* line;
        const wchar_t* hint; // 空 = 没有 Enter 这条路
        const wchar_t* store; // 空 = 同上
    };

    Notice notice_for(decode::Status status);

    // 备好 client 尺寸下的缩放结果，返回实际呈现尺寸。缓存还有效就直接返回。
    render::Size prescale(State& st, render::Size client);

    // 按 size 把整个客户区画到 hdc 上。size 通常就是客户区，但全屏切换时是*即将*
    // 变成的客户区——那一帧要赶在窗口几何之前交给 DWM，见 set_fullscreen。
    void paint(HWND hwnd, State& st, HDC hdc, render::Size size);

    // 在交换链上按 size 画一帧并 Present。
    void paint_frame(HWND hwnd, State& st, render::Size size);
} // namespace window::detail

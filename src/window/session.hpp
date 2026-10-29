#pragma once

#include <windows.h>

#include "catalog.hpp"
#include "channel.hpp"
#include "window/state.hpp"

// 哪一张该显示、谁去解、解完了往哪放：通道调度、翻页与删除都在这里。
namespace window::detail {
    // 只投请求，不等结果（UI 线程永不阻塞在后台任务上）。
    // 画面保留上一张直到新帧到达——切图时先闪一下背景色，比晚几十毫秒更刺眼；
    // 标题却立刻改，否则连切时用户不知道自己翻到哪了。
    void load_current(HWND hwnd, State& st);

    // 收货。过期与否只看路径还在不在 wanted 与相邻 ±1 之内：用户已经翻走了的，
    // 跑完丢弃；预取的那张若恰好是用户刚翻到的，直接上屏。
    void accept_decoded(HWND hwnd, State& st, channel::Decoded d);

    // 列表就绪：按累计的净位移一次性跳转。滚了五格就跳五张，而不是逐张解码五次。
    void accept_list(HWND hwnd, State& st, catalog::List list);

    // delta 只取 ±1。列表两端环绕。
    void step(HWND hwnd, State& st, int delta);

    // Delete / Shift + Delete。删完停在同一索引，越界回 0，列表空则退出。
    void remove_current(HWND hwnd, State& st, bool to_recycle);
} // namespace window::detail

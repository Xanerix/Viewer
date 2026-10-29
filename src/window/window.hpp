#pragma once

#include <filesystem>

#include <windows.h>

#include "config.hpp"

// unsafe 重灾区：Win32 消息处理全部关在 src/window/ 里，其余模块不得直接调用。
// 这个头是整个模块唯一的对外接口，内部各文件之间走 window/*.hpp。
namespace window {
    // 注册窗口类并创建主窗口，立刻显示。失败返回 nullptr。
    // cfg 只在这次调用期间被读走，窗口自己留副本，调用方不必让它活下去。
    // 此时还没有任何图——先建窗口再解码。
    HWND create(HINSTANCE instance, const config::Settings& cfg);

    // 把 initial 投给后台解码通道，同时起后台目录扫描——两样都不等结果就返回。
    // 首帧到达之前窗口画的是背景色，这是「先建窗口再解码」的应有之义。
    void open(HWND hwnd, std::filesystem::path initial);
} // namespace window

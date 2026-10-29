#pragma once

#include <cstdint>
#include <filesystem>

#include <windows.h>

#include "render.hpp"

// 与 shell 打交道的口子（§3）。
namespace shell {
    // 把 pixels（size 大、紧密排布、不透明的 BGRA）作为 CF_DIB 放进剪贴板（§4.2 Ctrl+C）。
    bool copy_image(HWND owner, const uint32_t* pixels, render::Size size);

    // 删掉 path。to_recycle 为真是进回收站，静默；为假是永久删除，此时**故意**不加
    // FOF_NOCONFIRMATION，由 shell 自己弹那句「确实要永久性地删除此文件吗？」。
    // 返回是否真的删掉了——用户在确认框上点取消也走这个 false。
    bool remove_file(HWND owner, const std::filesystem::path& path, bool to_recycle);

    // 交给系统去开一个 URI。这里只用来跳 ms-windows-store://（§11 P4 修订 C）。
    void open_uri(const wchar_t* uri);
} // namespace shell

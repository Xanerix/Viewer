#pragma once

#include <windows.h>

namespace settings {
    // 无参数启动时弹出的设置对话框，模态，关掉就返回
    // 改动写回 viewer.toml，下次打开图片时生效
    void run(HINSTANCE instance);
} // namespace settings

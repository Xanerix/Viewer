#pragma once

#include <cstdint>
#include <string_view>

namespace config {
    struct Settings {
        uint32_t background = 0xFF101010; // 0xFFRRGGBB，alpha 恒为不透明

        // 初始窗口尺寸。0 表示取光标所在显示器分辨率面积四分之一；超出分辨率时按分辨率截断
        int width = 0;
        int height = 0;
    };

    // 读 exe 同目录的 viewer.toml
    // 文件不存在是常态，全用默认值，不报错也不提示
    // 整个文件语法错：弹一次 MessageBox，然后全用默认值照常启动
    // 单个字段类型或取值不合法：只有那一项回落默认值
    Settings load();

    // 整个重写 viewer.toml，原有注释与未知键都不保留；width 为 0 时不写 window_size
    // 写不进去（典型是 per-machine 装在 Program Files）返回 false
    bool save(const Settings& s);

    // 试探 viewer.toml 能否写入，不留痕迹：原本没有的文件探测完即删
    bool writable();

    // 只认 "#RRGGBB"，out 为 0xFFRRGGBB
    bool parse_color(std::string_view s, uint32_t& out);
} // namespace config

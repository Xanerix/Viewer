#include "config.hpp"

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>

#include "log.hpp"
#include "toml.hpp"

namespace {
    std::filesystem::path config_path() {
        wchar_t buf[MAX_PATH]{};
        const DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
        if (n == 0 || n >= MAX_PATH)
            return {};
        return std::filesystem::path(buf).replace_filename(L"viewer.toml");
    }

    std::wstring widen(std::string_view utf8) {
        if (utf8.empty())
            return {};
        const int n = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                            static_cast<int>(utf8.size()), nullptr, 0);
        if (n <= 0)
            return {};
        std::wstring out(static_cast<size_t>(n), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                              out.data(), n);
        return out;
    }

    void read_background(const toml::table& tbl, config::Settings& out) {
        const auto node = tbl["background"];
        if (!node)
            return;
        const auto* s = node.as_string();
        if (!s) {
            VLOG(L"CONFIG      background 不是字符串，回落默认值");
            return;
        }
        uint32_t v = 0;
        if (!config::parse_color(s->get(), v)) {
            VLOG(L"CONFIG      background 不是 #RRGGBB，回落默认值");
            return;
        }
        out.background = v;
    }

    void read_window_size(const toml::table& tbl, config::Settings& out) {
        const auto node = tbl["window_size"];
        if (!node)
            return;
        const auto* arr = node.as_array();
        if (!arr || arr->size() != 2) {
            VLOG(L"CONFIG      window_size 不是 [宽, 高] 两元数组，回落自动尺寸");
            return;
        }
        const auto w = (*arr)[0].value<int64_t>();
        const auto h = (*arr)[1].value<int64_t>();
        // 上限取 SHRT_MAX：再大 CreateWindowExW 也接不住，而 0 或负数会直接建不出窗口。
        if (!w || !h || *w < 1 || *h < 1 || *w > 32767 || *h > 32767) {
            VLOG(L"CONFIG      window_size 取值越界（须为 1..32767 的整数），回落自动尺寸");
            return;
        }
        out.width = static_cast<int>(*w);
        out.height = static_cast<int>(*h);
    }
} // namespace

namespace config {
    Settings load() {
        Settings out; // 任何一条提前返回都等于「这一项用默认值」

        const std::filesystem::path path = config_path();
        if (path.empty())
            return out;

        // 自己读进内存再交给 toml++
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return out;

        std::ostringstream buf;
        buf << in.rdbuf();
        const std::string text = buf.str();

        toml::table tbl;
        try {
            tbl = toml::parse(text);
        }
        catch (const toml::parse_error& e) {
            // 语法错是用户明摆着编坏了文件，静默回落会让他抓瞎——弹一次，然后照常启动。
            std::wostringstream msg;
            msg << L"viewer.toml 第 " << e.source().begin.line << L" 行解析失败：\n"
                << widen(e.description()) << L"\n\n本次启动全部使用默认值。";
            ::MessageBoxW(nullptr, msg.str().c_str(), L"Viewer", MB_ICONWARNING | MB_OK);
            return Settings{};
        }

        read_background(tbl, out);
        read_window_size(tbl, out);
        return out;
    }

    bool save(const Settings& s) {
        const std::filesystem::path path = config_path();
        if (path.empty())
            return false;

        char color[8]{};
        std::snprintf(color, sizeof color, "#%06X", s.background & 0xFFFFFFu);

        std::ostringstream text;
        text << "background = \"" << color << "\"\n";
        if (s.width > 0 && s.height > 0)
            text << "window_size = [" << s.width << ", " << s.height << "]\n";

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out << text.str();
        return static_cast<bool>(out.flush());
    }

    bool writable() {
        const std::filesystem::path path = config_path();
        if (path.empty())
            return false;

        // OPEN_ALWAYS：已有文件就试它本身（可能被设成只读），没有就建一个再删掉
        const HANDLE h = ::CreateFileW(path.c_str(), GENERIC_WRITE,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        const bool created = ::GetLastError() != ERROR_ALREADY_EXISTS;
        ::CloseHandle(h);
        if (created)
            ::DeleteFileW(path.c_str());
        return true;
    }

    bool parse_color(std::string_view s, uint32_t& out) {
        if (s.size() != 7 || s.front() != '#')
            return false;
        s.remove_prefix(1);

        uint32_t v = 0;
        for (const char c : s) {
            uint32_t d = 0;
            if (c >= '0' && c <= '9') d = static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') d = static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') d = static_cast<uint32_t>(c - 'A' + 10);
            else return false;
            v = (v << 4) | d;
        }
        out = 0xFF000000u | v;
        return true;
    }
} // namespace config

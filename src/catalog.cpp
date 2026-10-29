#include "catalog.hpp"

#include <shlwapi.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <string>
#include <string_view>

namespace {
    // §4.5 的枚举白名单：去点、全小写。webp / heic 靠系统 codec，开不了的在打开时
    // 降级为提示。注意这只管枚举；文件关联注册要按 codec 探测，是另一回事（§11 P5）。
    constexpr std::array<std::wstring_view, 12> extensions{
        L"jpg", L"jpeg", L"tif", L"tiff", L"png", L"pns",
        L"bmp", L"rle", L"dib", L"gif", L"webp", L"heic",
    };

    bool is_supported(const std::filesystem::path& path) {
        std::wstring ext = path.extension().wstring();
        if (ext.size() < 2)
            return false;
        ext.erase(0, 1); // 去掉点
        for (wchar_t& c : ext)
            c = static_cast<wchar_t>(std::towlower(c));
        return std::ranges::find(extensions, ext) != extensions.end();
    }
} // namespace

namespace catalog {
    List scan(const std::filesystem::path& initial, std::stop_token stop) {
        List list;

        std::filesystem::path dir = initial.parent_path();
        if (dir.empty())
            dir = L".";

        // 目录打不开时迭代器直接等于 end()，循环不执行
        for (std::error_code ec; const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            // 逐条查停止请求：几千张的目录、尤其网络盘上，别让退出程序等它扫完。
            if (stop.stop_requested())
                return list;
            if (!entry.is_regular_file(ec) || !is_supported(entry.path()))
                continue;
            list.files.push_back(entry.path());
        }

        // 资源管理器自己用的比较函数：IMG_2.jpg 排在 IMG_10.jpg 前面
        std::ranges::sort(list.files,
                          [](const std::filesystem::path& a, const std::filesystem::path& b){
                              return ::StrCmpLogicalW(a.filename().c_str(), b.filename().c_str()) < 0;
                          });

        const std::wstring wanted = initial.filename().wstring();
        const auto it = std::ranges::find_if(list.files,
                                             [&](const std::filesystem::path& p){
                                                 return ::_wcsicmp(p.filename().c_str(), wanted.c_str()) == 0;
                                             });

        if (it != list.files.end()) {
            // 换成调用方给的原样：解码结果按路径认领，".\a.jpg" 与 "a.jpg" 是同一张却不相等
            *it = initial;
            list.index = static_cast<size_t>(it - list.files.begin());
        }
        else {
            // 扩展名不在白名单、或目录读不到，仍然要能打开
            list.files.insert(list.files.begin(), initial);
            list.index = 0;
        }
        return list;
    }
} // namespace catalog

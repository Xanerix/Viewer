#pragma once

#include <cstddef>
#include <filesystem>
#include <stop_token>
#include <vector>

namespace catalog {
    struct List {
        std::vector<std::filesystem::path> files;
        size_t index = 0; // 当前图在 files 中的位置

        [[nodiscard]] bool empty() const {
            return files.empty();
        }

        [[nodiscard]] const std::filesystem::path& current() const {
            return files[index];
        }
    };

    // 扫描 initial 所在目录，按 StrCmpLogicalW 排序（§4.5）。
    // initial 本身一定在结果里、且是原样那个 path 对象，index 指向它——哪怕它的扩展名不在白名单上。
    // 目录不可读时退化为只含 initial 的单元素列表。
    //
    // 跑在后台线程上（§7 规则 3）。stop 被请求时提前返回，结果不完整——调用方丢弃它。
    List scan(const std::filesystem::path& initial,
              std::stop_token stop = {});

    // index 在长度为 n 的环上走 delta 步，两端环绕。n 必须非零，index 必须小于 n。
    // 列表就绪前累计的净位移可以是任意整数（§7 修订 B），负数取模最容易写错，所以单测。
    inline size_t wrap(size_t index, long delta, size_t n) {
        const auto m = static_cast<long long>(n);
        long long r = (static_cast<long long>(index) + delta % m) % m;
        if (r < 0)
            r += m;
        return static_cast<size_t>(r);
    }
} // namespace catalog

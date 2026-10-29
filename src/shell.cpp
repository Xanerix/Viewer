#include "shell.hpp"

#include <shellapi.h>

#include <cstring>
#include <string>

namespace shell {
    bool copy_image(HWND owner, const uint32_t* pixels, render::Size size) {
        if (!pixels || size.width <= 0 || size.height <= 0)
            return false;

        const size_t row = static_cast<size_t>(size.width) * 4;
        const size_t bytes = row * static_cast<size_t>(size.height);

        // 剪贴板拿走的必须是 GlobalAlloc 出来的块，不能是 new 的。
        HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + bytes);
        if (!mem)
            return false;

        auto* head = static_cast<BITMAPINFOHEADER*>(::GlobalLock(mem));
        if (!head) {
            ::GlobalFree(mem);
            return false;
        }

        *head = BITMAPINFOHEADER{
            .biSize = sizeof(BITMAPINFOHEADER),
            .biWidth = size.width,
            .biHeight = size.height, // 正数 = 自下而上
            .biPlanes = 1,
            .biBitCount = 32,
            .biCompression = BI_RGB,
            .biSizeImage = static_cast<DWORD>(bytes),
        };

        // CF_DIB 的传统行序是自下而上。负高度虽然合法，但粘贴方（尤其老程序）
        // 经常读成上下颠倒——这一步只在用户按 Ctrl+C 时跑一次，不值得赌那点拷贝开销。
        auto* dst = reinterpret_cast<BYTE*>(head + 1);
        for (int y = 0; y < size.height; ++y) {
            const uint32_t* src = pixels + static_cast<size_t>(size.width) * (size.height - 1 - y);
            std::memcpy(dst + row * static_cast<size_t>(y), src, row);
        }
        ::GlobalUnlock(mem);

        if (!::OpenClipboard(owner)) {
            ::GlobalFree(mem);
            return false;
        }
        ::EmptyClipboard();
        const bool ok = ::SetClipboardData(CF_DIB, mem) != nullptr;
        ::CloseClipboard();
        if (!ok)
            ::GlobalFree(mem); // 没被接手，所有权还在我们这儿
        return ok;
    }

    bool remove_file(HWND owner, const std::filesystem::path& path, bool to_recycle) {
        // pFrom 要的是双 \0 结尾的完整路径：相对路径 SHFileOperation 按当前工作目录
        // 解释，而那个目录不一定是图片所在的目录。
        std::error_code ec;
        const std::filesystem::path full = std::filesystem::absolute(path, ec);
        std::wstring from = (ec ? path : full).wstring();
        from.push_back(L'\0'); // c_str() 自带一个，加上这个正好两个

        SHFILEOPSTRUCTW op{};
        op.hwnd = owner;
        op.wFunc = FO_DELETE;
        op.pFrom = from.c_str();
        // 回收站那条是可撤销的，不打扰；永久删除故意留着 shell 自己的确认框（§4.2）。
        op.fFlags = FOF_SILENT | (to_recycle ? (FOF_ALLOWUNDO | FOF_NOCONFIRMATION) : 0);

        // fAnyOperationsAborted：用户在确认框上点了取消，返回值同样是 0。
        return ::SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
    }

    void open_uri(const wchar_t* uri) {
        ::ShellExecuteW(nullptr, L"open", uri, nullptr, nullptr, SW_SHOWNORMAL);
    }
} // namespace shell

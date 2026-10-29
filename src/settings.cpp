#include "settings.hpp"

#include <commdlg.h>
#include <shellapi.h>

#include <cwchar>
#include <string>

#include "config.hpp"
#include "res/resource.h"

namespace {
    struct State {
        HINSTANCE instance = nullptr;
        config::Settings cfg;
        HBRUSH swatch = nullptr;
    };

    COLORREF to_colorref(uint32_t argb) {
        return RGB((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF);
    }

    uint32_t from_colorref(COLORREF c) {
        return 0xFF000000u | (static_cast<uint32_t>(GetRValue(c)) << 16)
               | (static_cast<uint32_t>(GetGValue(c)) << 8) | GetBValue(c);
    }

    // 合法的输入只可能是 ASCII，逐字收窄后交给 config 那份解析
    bool read_color(HWND dlg, uint32_t& out) {
        wchar_t buf[16]{};
        ::GetDlgItemTextW(dlg, IDC_BG_EDIT, buf, 16);
        std::string s;
        for (const wchar_t* p = buf; *p; ++p) {
            if (*p > 0x7F)
                return false;
            s.push_back(static_cast<char>(*p));
        }
        return config::parse_color(s, out);
    }

    void set_color_text(HWND dlg, uint32_t argb) {
        wchar_t buf[8]{};
        std::swprintf(buf, 8, L"#%06X", argb & 0xFFFFFFu);
        ::SetDlgItemTextW(dlg, IDC_BG_EDIT, buf);
    }

    void update_swatch(HWND dlg, State& st, uint32_t argb) {
        if (st.swatch)
            ::DeleteObject(st.swatch);
        st.swatch = ::CreateSolidBrush(to_colorref(argb));
        ::InvalidateRect(::GetDlgItem(dlg, IDC_BG_SWATCH), nullptr, TRUE);
    }

    void sync_size_enabled(HWND dlg) {
        const BOOL manual = ::IsDlgButtonChecked(dlg, IDC_SIZE_AUTO) != BST_CHECKED;
        ::EnableWindow(::GetDlgItem(dlg, IDC_WIDTH), manual);
        ::EnableWindow(::GetDlgItem(dlg, IDC_HEIGHT), manual);
    }

    void pick_color(HWND dlg, const State& st) {
        static COLORREF custom[16]{};
        uint32_t cur = st.cfg.background;
        read_color(dlg, cur); // 编辑框写坏了就从文件里的颜色起步

        CHOOSECOLORW cc{};
        cc.lStructSize = sizeof cc;
        cc.hwndOwner = dlg;
        cc.rgbResult = to_colorref(cur);
        cc.lpCustColors = custom;
        cc.Flags = CC_RGBINIT | CC_FULLOPEN;
        if (::ChooseColorW(&cc))
            set_color_text(dlg, from_colorref(cc.rgbResult)); // EN_CHANGE 顺带刷新色块
    }

    // 范围与 config.cpp 的 read_window_size 一致
    bool read_size(HWND dlg, int id, int& out) {
        BOOL ok = FALSE;
        const UINT v = ::GetDlgItemInt(dlg, id, &ok, FALSE);
        if (!ok || v < 1 || v > 32767)
            return false;
        out = static_cast<int>(v);
        return true;
    }

    bool commit(HWND dlg) {
        config::Settings s;
        if (!read_color(dlg, s.background)) {
            ::MessageBoxW(dlg, L"背景色须为 #RRGGBB。", L"Viewer", MB_ICONWARNING);
            ::SetFocus(::GetDlgItem(dlg, IDC_BG_EDIT));
            return false;
        }
        if (::IsDlgButtonChecked(dlg, IDC_SIZE_AUTO) != BST_CHECKED
            && (!read_size(dlg, IDC_WIDTH, s.width) || !read_size(dlg, IDC_HEIGHT, s.height))) {
            ::MessageBoxW(dlg, L"宽、高须为 1 到 32767 的整数。", L"Viewer", MB_ICONWARNING);
            ::SetFocus(::GetDlgItem(dlg, IDC_WIDTH));
            return false;
        }
        if (!config::save(s)) {
            ::MessageBoxW(dlg, L"无法写入 viewer.toml。", L"Viewer", MB_ICONERROR);
            return false;
        }
        return true;
    }

    void init(HWND dlg, State& st) {
        const HICON big = ::LoadIconW(st.instance, MAKEINTRESOURCEW(1));
        const auto little = static_cast<HICON>(::LoadImageW(
            st.instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
            ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), LR_SHARED));
        ::SendMessageW(dlg, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big));
        ::SendMessageW(dlg, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(little));

        ::SendDlgItemMessageW(dlg, IDC_BG_EDIT, EM_LIMITTEXT, 7, 0);
        ::SendDlgItemMessageW(dlg, IDC_WIDTH, EM_LIMITTEXT, 5, 0);
        ::SendDlgItemMessageW(dlg, IDC_HEIGHT, EM_LIMITTEXT, 5, 0);

        update_swatch(dlg, st, st.cfg.background);
        set_color_text(dlg, st.cfg.background);

        if (st.cfg.width > 0) {
            ::SetDlgItemInt(dlg, IDC_WIDTH, static_cast<UINT>(st.cfg.width), FALSE);
            ::SetDlgItemInt(dlg, IDC_HEIGHT, static_cast<UINT>(st.cfg.height), FALSE);
        } else {
            ::CheckDlgButton(dlg, IDC_SIZE_AUTO, BST_CHECKED);
        }
        sync_size_enabled(dlg);
    }

    INT_PTR CALLBACK proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
        auto* st = reinterpret_cast<State*>(::GetWindowLongPtrW(dlg, DWLP_USER));

        switch (msg) {
        case WM_INITDIALOG:
            ::SetWindowLongPtrW(dlg, DWLP_USER, lp);
            init(dlg, *reinterpret_cast<State*>(lp));
            return TRUE;

        case WM_CTLCOLORSTATIC:
            if (reinterpret_cast<HWND>(lp) == ::GetDlgItem(dlg, IDC_BG_SWATCH))
                return reinterpret_cast<INT_PTR>(st->swatch);
            return FALSE;

        case WM_COMMAND:
            switch (LOWORD(wp)) {
            case IDC_BG_EDIT:
                if (HIWORD(wp) == EN_CHANGE) {
                    uint32_t v = 0;
                    if (read_color(dlg, v)) // 输到一半不刷，色块保持上一个有效颜色
                        update_swatch(dlg, *st, v);
                }
                return TRUE;
            case IDC_BG_PICK:
                pick_color(dlg, *st);
                return TRUE;
            case IDC_SIZE_AUTO:
                sync_size_enabled(dlg);
                return TRUE;
            case IDOK:
                if (commit(dlg))
                    ::EndDialog(dlg, IDOK);
                return TRUE;
            case IDCANCEL:
                ::EndDialog(dlg, IDCANCEL);
                return TRUE;
            default:
                return FALSE;
            }

        case WM_DESTROY:
            if (st && st->swatch)
                ::DeleteObject(st->swatch);
            return FALSE;

        default:
            return FALSE;
        }
    }
    bool elevated() {
        HANDLE token = nullptr;
        if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token))
            return false;
        TOKEN_ELEVATION e{};
        DWORD n = 0;
        const BOOL ok = ::GetTokenInformation(token, TokenElevation, &e, sizeof e, &n);
        ::CloseHandle(token);
        return ok && e.TokenIsElevated;
    }

    // 无参数重启自己，新进程提权后照样进设置对话框；用户在 UAC 里点「否」返回 false
    bool relaunch_elevated() {
        wchar_t exe[MAX_PATH]{};
        const DWORD n = ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
        if (n == 0 || n >= MAX_PATH)
            return false;

        SHELLEXECUTEINFOW sei{};
        sei.cbSize = sizeof sei;
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = L"runas";
        sei.lpFile = exe;
        sei.nShow = SW_SHOWNORMAL;
        if (!::ShellExecuteExW(&sei))
            return false;
        // 新进程是 AppInfo 服务拉起来的，拿不到前台权；把本进程的那份转给它，
        // 对话框的 DS_SETFOREGROUND 才生效，不然会被别的窗口压住
        if (sei.hProcess) {
            ::AllowSetForegroundWindow(::GetProcessId(sei.hProcess));
            ::CloseHandle(sei.hProcess);
        }
        return true;
    }
} // namespace

namespace settings {
    void run(HINSTANCE instance) {
        // 为所有用户安装时 viewer.toml 在 Program Files 里，得提权才能写。
        // 已经提权还写不了就不再重启，否则 runas 不弹 UAC，会无限自我重启。
        if (!config::writable() && !elevated()) {
            // UAC 安全桌面切回来后本进程已没有前台权，MB_SETFOREGROUND 会被前台锁拦下；
            // MB_TOPMOST 至少保证提示不被别的窗口盖住
            if (!relaunch_elevated())
                ::MessageBoxW(nullptr, L"未获得管理员权限，无法修改设置。", L"Viewer",
                              MB_ICONINFORMATION | MB_SETFOREGROUND | MB_TOPMOST);
            return;
        }

        State st;
        st.instance = instance;
        st.cfg = config::load();
        ::DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), nullptr, proc,
                          reinterpret_cast<LPARAM>(&st));
    }
} // namespace settings

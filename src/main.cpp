#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <filesystem>

#include "config.hpp"
#include "settings.hpp"
#include "window/window.hpp"

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    // UI 线程用 STA；三条通道的 worker 各自 MTA 初始化
    if (FAILED(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
        return 1;

    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    // 无参数（开始菜单快捷方式、直接双击 exe）进设置对话框
    if (!argv || argc < 2) {
        if (argv)
            ::LocalFree(argv);
        settings::run(instance);
        ::CoUninitialize();
        return 0;
    }
    const std::filesystem::path initial = argv[1];
    ::LocalFree(argv);

    // 配置
    const config::Settings cfg = config::load();

    // 窗口先出来，哪怕内容是空的
    HWND hwnd = window::create(instance, cfg);
    if (!hwnd) {
        ::CoUninitialize();
        return 1;
    }

    // 扫描扔后台
    window::open(hwnd, initial);

    MSG msg{};
    BOOL got = FALSE;
    while ((got = ::GetMessageW(&msg, nullptr, 0, 0)) != 0) {
        if (got == -1)
            break;
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    ::CoUninitialize();
    return static_cast<int>(msg.wParam);
}

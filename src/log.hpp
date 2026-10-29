#pragma once

// 诊断日志。不属于 README 的模块划分，是排查用的设施。
//
// 只在定义了 VIEWER_LOG 时才编进去：CMake 里 Debug 默认开、Release 默认关，
// 需要时 -DVIEWER_LOG=ON 可以在 Release 上单独打开——时序类问题（比如
// 「先建窗后解码」的首帧耗时）在 Debug 上量不准，只能这么查。
//
// 关掉时 VLOG 整条展开成 ((void)0)，连实参都不会被求值，所以参数里放
// GetWindowRect 之类的调用也不会有代价。只在日志里用到的局部变量记得一并
// 用 #ifdef VIEWER_LOG 圈起来，否则 /W4 会报「定义了但未引用」。
#ifdef VIEWER_LOG

#include <windows.h>

#include <share.h>

#include <cstdarg>
#include <cstdio>

namespace viewer_log {
    inline void write(const wchar_t* fmt, ...) {
        // _SH_DENYWR：允许别人一边跑一边读。独占打开的话就只有进程退出后才看得到，
        // 而要查的往往恰恰是运行中的时序。
        static std::FILE* f = []{
            wchar_t path[MAX_PATH]{};
            ::GetModuleFileNameW(nullptr, path, MAX_PATH);
            if (wchar_t* slash = ::wcsrchr(path, L'\\'))
                ::wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"viewer.log");
            return ::_wfsopen(path, L"w, ccs=UTF-8", _SH_DENYWR);
        }();
        if (!f)
            return;

        static const LARGE_INTEGER freq = []{
            LARGE_INTEGER v{};
            ::QueryPerformanceFrequency(&v);
            return v;
        }();
        static const LARGE_INTEGER t0 = []{
            LARGE_INTEGER v{};
            ::QueryPerformanceCounter(&v);
            return v;
        }();

        LARGE_INTEGER now{};
        ::QueryPerformanceCounter(&now);
        std::fwprintf(f, L"[%9.3f] ",
                      static_cast<double>(now.QuadPart - t0.QuadPart) * 1000.0
                      / static_cast<double>(freq.QuadPart));

        va_list args;
        va_start(args, fmt);
        std::vfwprintf(f, fmt, args);
        va_end(args);

        std::fputwc(L'\n', f);
        std::fflush(f); // 崩溃前的最后几行才是关键，不缓冲
    }
} // namespace viewer_log

#define VLOG(...) ::viewer_log::write(__VA_ARGS__)

#else
#define VLOG(...) ((void)0)
#endif

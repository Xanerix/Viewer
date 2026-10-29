#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#include <winrt/base.h>     // com_ptr：随 Windows SDK 提供，不增加第三方依赖

#include "render.hpp"

// 把画好的一帧交给 DWM。窗口建时带 WS_EX_NOREDIRECTIONBITMAP，没有 GDI 重定向表面——
// 那张表面在窗口改尺寸时会先拿旧内容顶一帧（进全屏贴左上、退全屏贴右下），
// 在它上面怎么同步重画都消不掉。换成 flip 交换链，新尺寸的内容由我们直接 Present。
//
// 缓冲带 GDI_COMPATIBLE：paint 照旧往 HDC 上画，一行不用改。
namespace window::detail {
    class Present {
    public:
        // 拿到后台缓冲的 DC，缓冲尺寸已对齐到 size。失败或 size 为零时返回 nullptr，
        // 此时不要调 end。
        HDC begin(HWND hwnd, render::Size size);
        void end(HWND hwnd);

    private:
        bool attach(HWND hwnd);

        winrt::com_ptr<ID3D11Device> device_;
        winrt::com_ptr<IDXGISwapChain1> chain_;
        winrt::com_ptr<IDXGISurface1> surface_; // 只在 begin 与 end 之间持有
    };
} // namespace window::detail

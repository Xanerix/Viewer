#include "window/present.hpp"

#include "log.hpp"

namespace window::detail {
    namespace {
        constexpr UINT kChainFlags = DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE;
    } // namespace

    bool Present::attach(HWND hwnd) {
        // 只用 WARP，不碰硬件设备。画面全是 GDI 在 CPU 上画的，D3D 只负责把缓冲交给 DWM，
        // 用不上 GPU；而硬件设备要加载显卡驱动，实测 180–260 ms，首帧整个被它拖住。
        // WARP 建设备约 12 ms，4K 一帧从画完到 Present 也不比硬件慢（都在 2 ms 内）。
        // BGRA_SUPPORT 是 GDI 互操作的前提。
        constexpr UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        HRESULT hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                                         nullptr, 0, D3D11_SDK_VERSION,
                                         device_.put(), nullptr, nullptr);
        if (FAILED(hr)) {
            VLOG(L"D3D11CreateDevice failed hr=%08X", static_cast<unsigned>(hr));
            return false;
        }

        // 工厂得从设备身上顺藤摸瓜拿，自己 CreateDXGIFactory 出来的那个未必是同一块适配器的。
        // try_as 而不是 as：后者失败时抛 winrt 异常，会把 WinRT 运行时拖进链接。
        const auto dxgi_device = device_.try_as<IDXGIDevice>();
        winrt::com_ptr<IDXGIAdapter> adapter;
        dxgi_device->GetAdapter(adapter.put());
        winrt::com_ptr<IDXGIFactory2> factory;
        adapter->GetParent(IID_PPV_ARGS(factory.put()));

        // 宽高给 0：取窗口当前客户区。STRETCH 是用户选的——窗口尺寸与缓冲对不上的那一瞬，
        // 旧帧被拉伸铺满，而不是贴在角上。
        const DXGI_SWAP_CHAIN_DESC1 desc{
            .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
            .SampleDesc = {1, 0},
            .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
            .BufferCount = 2,
            .Scaling = DXGI_SCALING_STRETCH,
            .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
            .AlphaMode = DXGI_ALPHA_MODE_IGNORE,
            .Flags = kChainFlags,
        };
        hr = factory->CreateSwapChainForHwnd(device_.get(), hwnd, &desc, nullptr, nullptr,
                                             chain_.put());
        if (FAILED(hr)) {
            VLOG(L"CreateSwapChainForHwnd failed hr=%08X", static_cast<unsigned>(hr));
            device_ = nullptr;
            return false;
        }
        // Alt+Enter 是 DXGI 自带的独占全屏切换，跟我们自己的无边框全屏打架。
        factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        return true;
    }

    HDC Present::begin(HWND hwnd, render::Size size) {
        if (size.width <= 0 || size.height <= 0)
            return nullptr; // 最小化

        if (!chain_ && !attach(hwnd))
            return nullptr;

        // 尺寸在这里对齐，而不是等 WM_SIZE：带 SWP_NOSIZE 的 SWP_FRAMECHANGED 只改客户区、
        // 不发 WM_SIZE，缓冲会一直停在旧尺寸上。
        const auto w = static_cast<UINT>(size.width);
        const auto h = static_cast<UINT>(size.height);
        DXGI_SWAP_CHAIN_DESC1 desc{};
        chain_->GetDesc1(&desc);
        if (desc.Width != w || desc.Height != h) {
            const HRESULT hr = chain_->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, kChainFlags);
            if (FAILED(hr)) {
                VLOG(L"ResizeBuffers failed hr=%08X", static_cast<unsigned>(hr));
                return nullptr;
            }
        }

        // flip 模型下 0 号缓冲永远是当前的后台缓冲。
        if (FAILED(chain_->GetBuffer(0, IID_PPV_ARGS(surface_.put()))))
            return nullptr;
        // DISCARD 模型每帧内容都不保留，paint 本来就整块画满，旧内容不要也罢。
        HDC hdc = nullptr;
        if (FAILED(surface_->GetDC(TRUE, &hdc))) {
            surface_ = nullptr;
            return nullptr;
        }
        return hdc;
    }

    void Present::end(HWND hwnd) {
        surface_->ReleaseDC(nullptr);
        surface_ = nullptr;

        // 不等垂直同步：窗口模式下 DWM 本来就不会撕裂，等了只会卡住 UI 线程。
        const HRESULT hr = chain_->Present(0, 0);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            // 驱动更新、显卡被重置。整套扔掉，下一次 begin 重建；主动要一次重画，
            // 否则屏幕上会停在最后那帧，直到用户碰点什么。
            VLOG(L"Present: device lost hr=%08X", static_cast<unsigned>(hr));
            chain_ = nullptr;
            device_ = nullptr;
            ::InvalidateRect(hwnd, nullptr, FALSE);
        }
    }
} // namespace window::detail

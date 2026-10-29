#include "decode.hpp"

#include <winrt/base.h>     // com_ptr：随 Windows SDK 提供，不增加第三方依赖

#include <windows.h>
#include <wincodec.h>
#include <mferror.h>       // MF_E_TOPO_CODEC_NOT_FOUND：纯宏定义头，不需要链接 MF 库

#include <cwctype>
#include <string>

#include "render.hpp"

namespace {
    // 对尺寸做上限检查，拒绝分配超过预算的位图，而不是让它抛出。
    // 512 MiB 的 BGRA，约 1.34 亿像素——远超任何消费级相机。
    constexpr uint64_t kMaxPixels = 512ull * 1024 * 1024 / 4;

    // 源是预乘 BGRA，over 合成即 dst = src + bg * (255 - a) / 255。
    // 结果不会溢出：src 分量 <= a，加数 <= 255 - a。
    void composite_over(std::vector<uint32_t>& pixels, uint32_t background) {
        const uint32_t bb = (background >> 0) & 0xFFu;
        const uint32_t bg = (background >> 8) & 0xFFu;
        const uint32_t br = (background >> 16) & 0xFFu;

        for (uint32_t& p : pixels) {
            const uint32_t a = p >> 24;
            if (a == 0xFFu)
                continue;

            const uint32_t inv = 255u - a;
            const uint32_t b = ((p >> 0) & 0xFFu) + bb * inv / 255u;
            const uint32_t g = ((p >> 8) & 0xFFu) + bg * inv / 255u;
            const uint32_t r = ((p >> 16) & 0xFFu) + br * inv / 255u;
            p = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }

    // EXIF tag 274 WIC 的查询语言直接给得到，不必自己啃 TIFF 头。
    // 读不到、类型不对、值越界——一律按 1 处理，绝不能因此让打开失败。
    render::Orient read_orient(IWICBitmapFrameDecode* source) {
        winrt::com_ptr<IWICMetadataQueryReader> reader;
        if (FAILED(source->GetMetadataQueryReader(reader.put())))
            return {};

        // JPEG 把 IFD 挂在 APP1 段下，TIFF 则是顶层。两条都试一次就够——
        // 剩下那些容器要么没有方向标签，要么 WIC 本来就不给读。
        for (const wchar_t* path : {L"/app1/ifd/{ushort=274}", L"/ifd/{ushort=274}"}) {
            PROPVARIANT value{};
            ::PropVariantInit(&value);

            unsigned tag = 0;
            if (SUCCEEDED(reader->GetMetadataByName(path, &value))) {
                if (value.vt == VT_UI2)
                    tag = value.uiVal;
                else if (value.vt == VT_UI4)
                    tag = value.ulVal;
            }
            ::PropVariantClear(&value);

            if (tag != 0)
                return render::from_exif(tag);
        }
        return {};
    }

    // 解码器自己肯吐 32bppPBGRA 时绕过 FormatConverter。JPEG 解码器正是如此，实测比走转换器快 15–20%。肯吐的是别的格式就返回 false，交给转换器兜底
    bool copy_direct(IWICBitmapFrameDecode* source, const UINT w, const UINT h, const UINT bytes, BYTE* dst) {
        winrt::com_ptr<IWICBitmapSourceTransform> transform;
        if (FAILED(source->QueryInterface(IID_PPV_ARGS(transform.put()))))
            return false;

        WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
        if (FAILED(transform->GetClosestPixelFormat(&format)) ||
            format != GUID_WICPixelFormat32bppPBGRA)
            return false;

        // 方向一律 Rotate0：EXIF 只记进 Frame::orient，不在解码时搬像素
        return SUCCEEDED(transform->CopyPixels(nullptr, w, h, &format, WICBitmapTransformRotate0,w * 4, bytes, dst));
    }

    // 兜底路径：PNG、GIF、BMP 吐的都不是 32bppPBGRA，交给 WIC 转
    // 返回 HRESULT 而不是 bool：失败原因要能区分「没装解码器」和「文件坏了」
    HRESULT copy_converted(IWICImagingFactory* factory, IWICBitmapSource* source, const UINT w, const UINT bytes, BYTE* dst) {
        winrt::com_ptr<IWICFormatConverter> converter;
        HRESULT hr = factory->CreateFormatConverter(converter.put());
        if (FAILED(hr))
            return hr;
        hr = converter->Initialize(source, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeMedianCut);
        if (FAILED(hr))
            return hr;
        return converter->CopyPixels(nullptr, w * 4, bytes, dst);
    }

    // WINCODEC_ERR_COMPONENTINITIALIZEFAILURE：卸载「HEIF 图像扩展」后打开 .heic 拿到不是 COMPONENTNOTFOUND，扩展包卸掉了，但 .heic 的解码器注册项还留在系统里，WIC 找得到但实例化不起来
    // COMPONENTNOTFOUND：从没装过 HEIF 支持的机器
    // MF_E_TOPO_CODEC_NOT_FOUND：装了 HEIF、缺 HEVC，Converter::CopyPixels 返回 0xC00D5212。HEIF 扩展是转手交给 Media Foundation 去解 HEVC 帧的，MF 找不到 transform 报这个。
    bool missing_codec(const HRESULT hr) {
        return hr == WINCODEC_ERR_COMPONENTINITIALIZEFAILURE ||
            hr == WINCODEC_ERR_COMPONENTNOTFOUND ||
            hr == MF_E_TOPO_CODEC_NOT_FOUND;
    }

    // 缺扩展只对 HEIC / HEIF 成立，COMPONENTNOTFOUND 的含义仅仅是 没有能开这个格式的解码器
    bool is_heif(const std::filesystem::path& path) {
        std::wstring ext = path.extension().wstring();
        for (wchar_t& c : ext)
            c = static_cast<wchar_t>(std::towlower(c));
        return ext == L".heic" || ext == L".heif";
    }
} // namespace

namespace decode {
    Session::Session() {
        IWICImagingFactory* factory = nullptr;
        if (SUCCEEDED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))))
            factory_ = factory;
    }

    Session::~Session() {
        if (factory_) static_cast<IWICImagingFactory*>(factory_)->Release();
    }

    std::shared_ptr<const Frame> Session::load(const std::filesystem::path& path, const uint32_t background, Status& status) const {
        status = Status::Failed;
        if (!factory_) return nullptr;

        const auto factory = static_cast<IWICImagingFactory*>(factory_);

        winrt::com_ptr<IWICBitmapDecoder> decoder;
        HRESULT hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.put());
        if (FAILED(hr)) {
            // 容器没打开：缺「HEIF 图像扩展」
            if (missing_codec(hr) && is_heif(path))
                status = Status::NoHeif;
            return nullptr;
        }

        winrt::com_ptr<IWICBitmapFrameDecode> source;
        hr = decoder->GetFrame(0, source.put());
        if (FAILED(hr)) {
            if (missing_codec(hr) && is_heif(path))
                status = Status::NoHevc;
            return nullptr;
        }

        UINT w = 0, h = 0;
        if (FAILED(source->GetSize(&w, &h)) || w == 0 || h == 0)
            return nullptr;
        if (static_cast<uint64_t>(w) * h > kMaxPixels)
            return nullptr;

        auto frame = std::make_shared<Frame>();
        frame->width = static_cast<int>(w);
        frame->height = static_cast<int>(h);
        frame->orient = read_orient(source.get());
        frame->pixels.resize(static_cast<size_t>(w) * h);

        const UINT bytes = static_cast<UINT>(frame->pixels.size() * sizeof(uint32_t));
        auto* dst = reinterpret_cast<BYTE*>(frame->pixels.data());
        if (!copy_direct(source.get(), w, h, bytes, dst)) {
            hr = copy_converted(factory, source.get(), w, bytes, dst);
            if (FAILED(hr)) {
                // 容器开得了、像素解不出来：HEIC 上这就是装了 HEIF 扩展、缺 HEVC 扩展。
                // 直出快路径同样会在这一步失败，它失败后本来就落到这条兜底上。
                if (missing_codec(hr) && is_heif(path))
                    status = Status::NoHevc;
                return nullptr;
            }
        }

        composite_over(frame->pixels, background);
        status = Status::Ok;
        return frame;
    }
} // namespace decode

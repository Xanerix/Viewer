#pragma once

#include <filesystem>
#include <memory>

#include "frame.hpp"

namespace decode {
    enum class Status {
        Ok,
        NoHeif, // 容器没打开：缺「HEIF 图像扩展」
        NoHevc, // 容器开了、像素解不出来：缺「HEVC 视频扩展」
        Failed, // 文件损坏、格式不支持、尺寸超限
    };

    // WIC 工厂随常驻 worker 一起常驻，省掉每次解码重建工厂的开销。必须在已 CoInitializeEx 的线程上构造和析构，且不得跨线程共享
    class Session {
    private:
        void* factory_ = nullptr; // IWICImagingFactory*；COM 不出这一层
    public:
        Session();
        ~Session();
        Session(const Session&) = delete;
        Session& operator=(const Session&) = delete;

        // 同步解全图。失败（工厂没建起来、格式不支持、文件损坏、尺寸超限）返回 nullptr。失败时 status 说明是哪一种，
        // 窗口层据它决定在画面中心写什么。
        //
        // background 为 0xAARRGGBB；带 alpha 的源在此合成到它上面。
        std::shared_ptr<const Frame> load(const std::filesystem::path& path, uint32_t background, Status& status) const;
    };
} // namespace decode

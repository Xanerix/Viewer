#include "channel.hpp"

#include <objbase.h>

#include <memory>
#include <utility>

#include "catalog.hpp"
#include "decode.hpp"
#include "frame.hpp"

namespace channel {
    std::jthread scan(HWND ui, std::filesystem::path initial) {
        return std::jthread([ui, initial = std::move(initial)](const std::stop_token& st){
                // 被叫停的结果是残缺的，不许投出去
                if (auto* boxed = new catalog::List{catalog::scan(initial, st)};
                    st.stop_requested() ||
                    !::PostMessageW(ui, WM_CATALOG_READY, 0, reinterpret_cast<LPARAM>(boxed)))
                    delete boxed;
            }
        );
    }

    Decode::Decode(HWND ui, uint32_t background)
        : ui_(ui),
          background_(background),
          worker_([this](const std::stop_token& st){
              run(st);
          }) {}

    Decode::~Decode() = default; // jthread 自己 request_stop + join（仅退出时）

    void Decode::request(std::filesystem::path path, const render::Size client) {
        {
            std::lock_guard g{m_};
            pending_ = std::move(path);
            pending_client_ = client;
        }
        cv_.notify_one();
    }

    namespace {
        // 按 client 把 frame 缩好，缩不小就返回空。与 window.cpp 的 prescale 同一口径：先按 EXIF 方向算呈现尺寸，fit 出目标，再折回未变换的坐标系。
        std::shared_ptr<const Scaled> prescale(const Frame& frame, const render::Size client) {
            if (client.width <= 0 || client.height <= 0)
                return nullptr;

            const render::Size src{frame.width, frame.height};
            const render::Size target = render::fit(render::oriented(src, frame.orient), client);
            if (target.width <= 0)
                return nullptr;

            const render::Size size = render::transposes(frame.orient)
                                          ? render::Size{target.height, target.width}
                                          : target;
            if (size == src)
                return nullptr; // 不比窗口大：UI 直接拿原像素，别白占一份内存

            auto scaled = std::make_shared<Scaled>();
            scaled->size = size;
            scaled->pixels.resize(static_cast<size_t>(size.width) * size.height);
            render::box_downsample(frame.pixels, frame.width, frame.height,
                                   scaled->pixels, size.width, size.height);
            return scaled;
        }
    } // namespace

    void Decode::run(const std::stop_token& st) {
        // UI 线程是 STA，通道一律 MTA。工厂跟着线程走，不跨线程共享。
        if (FAILED(::CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
            return;
        {
            const decode::Session session{};

            while (!st.stop_requested()) {
                std::filesystem::path path;
                render::Size client;
                {
                    std::unique_lock lk{m_};
                    if (!cv_.wait(lk, st, [this]{
                        return !pending_.empty();
                    }))
                        break; // 停止请求：连锁一起退干净
                    path = std::exchange(pending_, {});
                    client = pending_client_;
                }

                decode::Status status{};
                auto frame = session.load(path, background_, status);

                // 缩放也在这条线程上做：1080p 全屏下它是 17 ms，压在 UI 线程的 WM_PAINT 里就是每次换图掉一帧。预取通道本来就闲着。
                auto scaled = frame ? prescale(*frame, client) : nullptr;

                // 解出来的也可能已经过期——解码期间用户又翻了页。照样交货， 由 UI 按路径裁决（见头文件）；单槽保证过期的每条通道最多一张。 解码失败同样要回投：UI 那边等着据此把标题改成「无法解码」，不回投就成了静默卡住——空指针本身就是消息内容的一部分。
                auto boxed = new Decoded{
                    std::move(path), std::move(frame), std::move(scaled),
                    status
                };
                if (!::PostMessageW(ui_, WM_FRAME_READY, 0, reinterpret_cast<LPARAM>(boxed)))
                    delete boxed; // 窗口已死或队列满：立即回收
            }
        } // session 必须在 CoUninitialize 之前析构：COM 对象不能活过它的公寓
        ::CoUninitialize();
    }
} // namespace channel

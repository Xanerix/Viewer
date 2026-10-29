#pragma once

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>

#include <windows.h>

#include "decode.hpp"
#include "render.hpp"

struct Frame;

// 后台解码通道（§8）。四条规则里这里担两条：UI 线程永不阻塞在后台任务上，
// 过期结果丢弃而不靠取消。
namespace channel {
    // 解码线程顺手缩好的那一份（§6.3）。尺寸是**未施加二面体变换**的坐标系里的，
    // 与 prescale 的中间结果同一口径：旋转只搬缩放结果，那一步留给 UI 线程，便宜。
    //
    // 缩放按请求时的窗口尺寸和 EXIF 方向算。UI 收货时窗口已 resize、或用户已转过，
    // 尺寸就对不上——那时作废，退回 UI 线程自己缩。
    struct Scaled {
        render::Size size;
        std::vector<uint32_t> pixels;
    };

    // 一次解码的结果。frame 为空表示解码失败——那同样是个结果，不是「还没好」。
    // scaled 为空表示没缩（失败、或图本来就不比窗口大），不是「还没缩」。
    // frame 为空时 status 说明失败的种类，窗口层据它决定画面中心写什么（§11 P4 修订 C）。
    struct Decoded {
        std::filesystem::path path;
        std::shared_ptr<const Frame> frame;
        std::shared_ptr<const Scaled> scaled;
        decode::Status status = decode::Status::Failed;
    };

    // wParam 不用；lParam = new Decoded，收货方负责 delete，无论它要不要这一帧。
    // 窗口死了或消息队列满，投递方自己回收（§10.4）。
    inline constexpr UINT WM_FRAME_READY = WM_APP + 1;

    // lParam = new catalog::List，所有权规矩同上。
    inline constexpr UINT WM_CATALOG_READY = WM_APP + 2;

    // 目录扫描（§7 规则 3）。整个进程只扫一次，所以不是常驻单槽，扫完线程就结束。
    // 析构返回的 jthread 会 request_stop + join——只许在窗口销毁时做，而且之后要
    // 排空 WM_CATALOG_READY：停止请求与投递之间有竞争，排空才是兜底。
    std::jthread scan(HWND ui, std::filesystem::path initial);

    // 单槽通道：新请求直接覆盖尚未开工的旧请求。用户连切十张时，中间那八张
    // 根本不需要被解码；已经开工的那一张跑完、照常交货——要不要由 UI 按路径裁决。
    //
    // 与 §8 的偏离：通道内部不再按世代号丢弃。预取出来的那张在用户翻过去的一刻
    // 就成了当前图，而那时通道早已被改派去解下一张；按世代号它是「过期」的，
    // 丢掉就得重解一遍，HEIC 上是白等 190 ms。结果按路径认领，过期与否看它是不是
    // 还在当前与相邻 ±1 之内。
    class Decode {
    public:
        Decode(HWND ui, uint32_t background);
        ~Decode();
        Decode(const Decode&) = delete;
        Decode& operator=(const Decode&) = delete;

        // UI 线程调用。只加锁改两个字段再 notify——微秒级，不会阻塞。
        // client 是投请求那一刻的客户区尺寸，解码线程据它把缩放一并做掉（§6.3）。
        void request(std::filesystem::path path, render::Size client);
    private:
        void run(const std::stop_token& st);

        HWND ui_;
        uint32_t background_;

        std::mutex m_;
        std::condition_variable_any cv_;
        std::filesystem::path pending_; // 单槽
        render::Size pending_client_; // 跟着 pending_ 一起被覆盖

        // 必须最后声明：线程一启动就会碰上面每一个字段，声明早了就是构造期竞争。
        std::jthread worker_;
    };
} // namespace channel

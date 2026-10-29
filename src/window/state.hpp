#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <thread>
#include <vector>

#include <windows.h>

#include "catalog.hpp"
#include "channel.hpp"
#include "decode.hpp"
#include "frame.hpp"
#include "gesture.hpp"
#include "render.hpp"
#include "window/present.hpp"

// 窗口模块的内部共享状态。对外不可见——window.hpp 才是这个模块的接口，
// 这个头只给 src/window/ 下的实现文件互相引用。
namespace window::detail {
    struct State {
        catalog::List list;
        std::shared_ptr<const Frame> frame;

        // 备好可直接 blit 的像素：适应窗口时是缩放结果，1:1 时是裁剪结果。
        // 只有缩放那一路值得缓存，1:1 的平移每帧都在变。
        std::vector<uint32_t> surface;
        // 自己缩出来的缩放结果，留着复用。呈现取向只有横竖两种，两个槽就够，按
        // transposes 索引——共用一个槽的话来回转时横竖会互相覆盖，每一下都得重缩。
        std::array<std::vector<uint32_t>, 2> scratch;
        std::array<render::Size, 2> scratch_size;
        render::Size surface_size;
        bool surface_scaled = false;
        const uint32_t* bits = nullptr; // blit 的取数处：surface，或解码线程缩好的那份

        // 解码线程按请求时的窗口尺寸缩好的当前图。尺寸对不上就作废。
        std::shared_ptr<const channel::Scaled> scaled;

        Present present; // paint 画在它的后台缓冲上，不画在窗口 DC 上

        // 投解码请求时随附的客户区尺寸。请求可能早于第一次 WM_PAINT，所以建窗后就得有值。
        render::Size client;

        gesture::State input;
        render::Orient orient; // 当前二面体状态，初值来自 EXIF
        render::Point pan; // 1:1 下图片左上角在客户区中的位置

        bool fullscreen = false;
        WINDOWPLACEMENT placement{}; // 进全屏之前的窗口位置
        RECT monitor{}; // 全屏占的那块显示器矩形

        POINT drag_cursor{}; // 中键搬窗口的起点，屏幕坐标
        POINT drag_origin{}; // 同上，窗口左上角

        uint32_t background = 0; // 0xAARRGGBB，补图片四周与解码通道合成透明像素都用它

        // 三条解码通道：当前图与预取 ±1。不固定分工，而是一个池——
        // 前翻一步时，原先的 +1 成了当前、原先的当前成了 −1，固定分工就得来回倒手。
        // in_flight 是每条通道最后一次请求、尚未交货的路径；空表示闲着。
        std::array<std::unique_ptr<channel::Decode>, 3> lanes;
        std::array<std::filesystem::path, 3> in_flight;

        // 已解好的帧，只留 wanted 与相邻 ±1。结果按路径认领：帧在消息队列里排队期间
        // 用户完全可能又翻了两页，不在这三张之内的一律丢弃。
        std::vector<channel::Decoded> cache;
        std::filesystem::path wanted; // 此刻该显示哪个文件
        bool shown = false; // wanted 的结果是否已上屏——失败也算上屏
        decode::Status status = decode::Status::Ok; // frame 为空时才有意义

        // 后台目录扫描。listed 之前 list 里只有用户双击的那一张，
        // 期间的翻页只记净位移，不解码。
        std::jthread scanner;
        bool listed = false;
        long pending_steps = 0;

        // 只有左键按住才切到原始比例
        bool one_to_one() const {
            return input.left;
        }

        // 呈现尺寸：旋转了 90° 的图，宽高是互换的。所有几何计算都该用这个，
        // 而不是 frame 的原始宽高——混用就是画面斜切的来源。
        render::Size shown_size() const {
            return frame
                       ? render::oriented({frame->width, frame->height}, orient)
                       : render::Size{};
        }
    };

    inline State* state_of(HWND hwnd) {
        return reinterpret_cast<State*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
} // namespace window::detail

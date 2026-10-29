#include "window/session.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include "shell.hpp"
#include "window/view.hpp"

namespace window::detail {
    namespace {
        // 上屏。到这儿的就是 wanted 那一张；frame 为空表示解码失败。
        void accept_frame(HWND hwnd, State& st, std::shared_ptr<const Frame> frame,
                          std::shared_ptr<const channel::Scaled> scaled, decode::Status status) {
            st.frame = std::move(frame);
            st.scaled = std::move(scaled);
            st.shown = true;
            st.status = status;
            st.surface_scaled = false; // 换图即作废缓存
            st.scratch_size = {}; // 自己缩的那两份是上一张的，别认错
            // 方向从 EXIF 起算，上一张转过多少不带到下一张
            st.orient = st.frame ? st.frame->orient : render::Orient{};
            center_pan(hwnd, st);

            if (!st.frame) {
                std::wstring title = st.list.current().filename().wstring();
                title += L"（无法解码） — Viewer"; // 降级为提示，不是崩溃
                ::SetWindowTextW(hwnd, title.c_str());
            }

            ::InvalidateRect(hwnd, nullptr, FALSE);
        }

        // 这个文件还值不值得留：是 wanted，或者是它在列表里的相邻 ±1。
        bool needed(const State& st, const std::filesystem::path& path) {
            if (path == st.wanted)
                return true;
            const size_t n = st.list.files.size();
            if (!st.listed || n < 2)
                return false;
            return path == st.list.files[catalog::wrap(st.list.index, +1, n)] ||
                path == st.list.files[catalog::wrap(st.list.index, -1, n)];
        }

        const channel::Decoded* cached(const State& st, const std::filesystem::path& path) {
            const auto it = std::ranges::find_if(st.cache,
                                                 [&](const channel::Decoded& d){
                                                     return d.path == path;
                                                 });
            return it != st.cache.end() ? &*it : nullptr;
        }

        // 让 path 有着落：已解好、或已在某条通道上跑，就什么也不做；否则找条闲通道投出去。
        // 「闲」包括手上那张已经不需要了的——覆盖它正是单槽的本意。
        void ensure(State& st, const std::filesystem::path& path) {
            if (cached(st, path) ||
                std::ranges::find(st.in_flight, path) != st.in_flight.end())
                return;

            // 需要的至多三张、通道恰好三条。in_flight 里的路径两两不同且都未缓存（上面那道
            // 检查挡掉了重复请求，交货时又会清掉），所以一张没着落时必有一条通道手上的不再需要。
            // 找不到只可能是通道没建起来。
            //
            // 先挑真闲着的（已交货），再挑手上那张不再需要的：后者多半还在跑，
            // 排在它后面要多等一整张解码——连翻时正是这种通道最多。
            for (const bool idle_only : {true, false}) {
                for (size_t i = 0; i < st.lanes.size(); ++i) {
                    const bool free = st.in_flight[i].empty() ||
                        (!idle_only && !needed(st, st.in_flight[i]));
                    if (st.lanes[i] && free) {
                        st.lanes[i]->request(path, st.client);
                        st.in_flight[i] = path;
                        return;
                    }
                }
            }
        }

        // 预取相邻 ±1。列表没就绪时没有邻居可言。
        void prefetch(State& st) {
            const size_t n = st.list.files.size();
            if (!st.listed || n < 2)
                return;
            ensure(st, st.list.files[catalog::wrap(st.list.index, +1, n)]);
            ensure(st, st.list.files[catalog::wrap(st.list.index, -1, n)]);
        }
    } // namespace

    void load_current(HWND hwnd, State& st) {
        std::wstring title = st.list.current().filename().wstring();
        title += L" — Viewer";
        ::SetWindowTextW(hwnd, title.c_str());

        st.wanted = st.list.current();
        st.shown = false;
        std::erase_if(st.cache, [&](const channel::Decoded& d){
            return !needed(st, d.path);
        });

        // 预取命中：当场上屏，一次解码都不等
        if (const channel::Decoded* hit = cached(st, st.wanted))
            accept_frame(hwnd, st, hit->frame, hit->scaled, hit->status);

        ensure(st, st.wanted); // 当前图先占通道，邻居其次
        prefetch(st);
    }

    void accept_decoded(HWND hwnd, State& st, channel::Decoded d) {
        for (std::filesystem::path& p : st.in_flight) {
            if (p == d.path)
                p.clear();
        }
        if (!needed(st, d.path) || cached(st, d.path))
            return;
        if (!st.shown && d.path == st.wanted)
            accept_frame(hwnd, st, d.frame, d.scaled, d.status);
        st.cache.push_back(std::move(d));
    }

    void step(HWND hwnd, State& st, int delta) {
        if (!st.listed) {
            // 列表还在后台扫：入队，不阻塞、不丢弃。界面此时无反馈，
            // 这个窗口期只有几十到几百毫秒，加提示反而显得慢。
            st.pending_steps += delta;
            return;
        }
        const size_t n = st.list.files.size();
        if (n < 2)
            return;
        st.list.index = catalog::wrap(st.list.index, delta, n);
        load_current(hwnd, st);
    }

    void accept_list(HWND hwnd, State& st, catalog::List list) {
        st.list = std::move(list);
        st.listed = true;

        const long steps = std::exchange(st.pending_steps, 0);
        const size_t n = st.list.files.size();
        const size_t next = n < 2 ? st.list.index : catalog::wrap(st.list.index, steps, n);
        // 净位移为零或恰好绕回原位：还是这一张，别重新上屏——那会把用户期间转过的方向冲掉。
        // 邻居这时才第一次有定义，预取从这里起步。
        if (next == st.list.index) {
            prefetch(st);
            return;
        }
        st.list.index = next;
        load_current(hwnd, st);
    }

    void remove_current(HWND hwnd, State& st, bool to_recycle) {
        // 列表还在后台扫的那几十毫秒里，手上这一张就是「整个列表」——照规格字面走
        // 会因为「列表空」把程序关掉，而目录里明明还有几千张。这段窗口期忽略掉。
        if (!st.listed || st.list.empty())
            return;

        const std::filesystem::path victim = st.list.current();
        if (!shell::remove_file(hwnd, victim, to_recycle))
            return; // 删不掉，或用户在确认框上点了取消

        st.list.files.erase(st.list.files.begin() + static_cast<ptrdiff_t>(st.list.index));
        std::erase_if(st.cache, [&](const channel::Decoded& d){
            return d.path == victim;
        });
        // in_flight 里可能还挂着它：不用管，它已不在列表里，needed() 一律判否，
        // ensure() 会把那条通道当成闲着的覆盖掉，交货时也会被 accept_decoded 丢弃。

        if (st.list.files.empty()) {
            ::DestroyWindow(hwnd);
            return;
        }
        if (st.list.index >= st.list.files.size())
            st.list.index = 0; // 删的是最后一张

        ::MessageBeep(MB_OK); // 交互规格要求的提示音
        load_current(hwnd, st);
    }
} // namespace window::detail

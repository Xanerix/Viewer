#include "gesture.hpp"

#include <cstdlib>

namespace gesture {
    Action on_mouse(State& st, Event ev, Point at, bool alt) {
        st.cursor = at;

        switch (ev) {
        case Event::LeftDown:
            if (st.left)
                return Action::None;
            st.right_alone = false; // 右键按住期间又按了左键 → 松开不退出
            if (alt) {
                // 拖窗立刻进系统的模态拖动循环，LeftUp 永远回不到这里，
                // 所以绝不能在这条路上置位 left——那会留下一个再也清不掉的平移态。
                return Action::DragWindow;
            }
            st.left = true;
            return Action::PanBegin;

        case Event::LeftUp:
            if (!st.left)
                return Action::None;
            st.left = false;
            return Action::PanEnd;

        case Event::Move:
            if (st.moving)
                return Action::MoveStep;
            // 定时器只是兜底（按住不动也该判成拖）。真正让手感跟手的是这个阈值：
            // 光等 GetDoubleClickTime 的话，每次拖窗都先白等半秒。
            if (st.middle && (std::abs(at.x - st.press.x) > st.drag_threshold ||
                std::abs(at.y - st.press.y) > st.drag_threshold)) {
                st.moving = true;
                return Action::MoveBegin;
            }
            return st.left ? Action::PanMove : Action::None;

        case Event::RightDown:
            if (st.right)
                return Action::None;
            st.right = true;
            st.right_alone = !st.left && !st.middle; // 「单独按下才生效」（§4.1）
            return Action::None;

        case Event::RightUp:
            {
                if (!st.right)
                    return Action::None;
                const bool alone = st.right_alone;
                st.right = false;
                st.right_alone = false;
                return alone ? Action::Exit : Action::None;
            }

        case Event::MiddleDown:
            if (st.middle)
                return Action::None;
            st.middle = true;
            st.press = at;
            st.right_alone = false;
            return Action::HoldStart;

        case Event::HoldElapsed:
            if (!st.middle || st.moving)
                return Action::None;
            st.moving = true;
            return Action::MoveBegin;

        case Event::MiddleUp:
            if (!st.middle)
                return Action::None;
            st.middle = false;
            if (st.moving) {
                st.moving = false;
                return Action::MoveEnd;
            }
            return Action::ToggleFullscreen; // 定时器没到，算单击

        case Event::WheelUp:
        case Event::WheelDown:
            if (st.right) {
                // 右键按住时滚轮改判为旋转（§4.1）。right_alone 必须就地清掉：
                // 留着的话，转完松开右键会顺带退出程序。
                st.right_alone = false;
                if (st.left)
                    return Action::None; // 「左键未按下」是成立条件之一
                return ev == Event::WheelUp ? Action::RotateCCW : Action::RotateCW;
            }
            if (st.suppressed())
                return Action::None;
            return ev == Event::WheelUp ? Action::Prev : Action::Next;

        case Event::Cancel:
            {
                // 捕获丢了就当所有键都松了。搬窗口和平移不可能同时成立，收一个就够。
                const bool was_moving = st.moving;
                const bool was_left = st.left;
                st = State{};
                st.cursor = at;
                if (was_moving)
                    return Action::MoveEnd;
                return was_left ? Action::PanEnd : Action::None;
            }
        }

        return Action::None;
    }

    Action on_key(const State& st, unsigned vk, bool shift, bool ctrl) {
        // Ctrl 组合不受锁定状态限制（§4.2）。这条判定必须在最前面：Ctrl + Delete
        // 不是删除，落进下面的分支就成了。
        if (ctrl)
            return vk == kC ? Action::CopyImage : Action::None;

        switch (vk) {
        case kEscape:
            return Action::Exit; // 无条件（§4.2）
        case kDelete:
            if (st.suppressed())
                return Action::None;
            return shift ? Action::DeletePermanent : Action::Delete;
        case kEnter:
            return st.suppressed() ? Action::None : Action::OpenStore;
        case kUp:
            return st.suppressed() ? Action::None : Action::Prev;
        case kDown:
            return st.suppressed() ? Action::None : Action::Next;
        case kLeft:
            return st.suppressed() ? Action::None : Action::RotateCCW;
        case kRight:
            return st.suppressed() ? Action::None : Action::RotateCW;
        case kH:
            return st.suppressed() ? Action::None : Action::FlipH;
        case kV:
            return st.suppressed() ? Action::None : Action::FlipV;
        case kSpace:
            return st.suppressed() ? Action::None : Action::ToggleFullscreen;
        default:
            return Action::None;
        }
    }
} // namespace gesture

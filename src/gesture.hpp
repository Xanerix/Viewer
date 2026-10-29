#pragma once

// 纯状态机，不含 windows.h：§4.1 那张表的抑制条件互相纠缠，是全项目最容易
// *误读* 的地方，因此与系统 API 完全解耦以便单测（§3）。
//
// 与 §4.1 的一处偏离：中键按住原作是「锁定原始比例」，实测下来和左键按住平移
// 无从区分，于是改判为移动窗口。代价是这条路拿不到 Aero Snap——系统的拖动
// 循环认左键松开才收尾——所以 Alt + 左键那条得留着。
namespace gesture {
    struct Point {
        int x = 0;
        int y = 0;
    };

    // VK_* 来自 WinUser.h，这里手抄成常量——包含 windows.h 就破了上面那条规矩。
    enum Key : unsigned {
        kEnter = 0x0D,
        kEscape = 0x1B,
        kSpace = 0x20,
        kLeft = 0x25,
        kUp = 0x26,
        kRight = 0x27,
        kDown = 0x28,
        kDelete = 0x2E,
        kC = 0x43,
        kH = 0x48,
        kV = 0x56,
    };

    enum class Event {
        LeftDown, LeftUp,
        RightDown, RightUp,
        MiddleDown, MiddleUp,
        HoldElapsed, // 中键按住已超过 GetDoubleClickTime，判定为「按住」
        Move,
        WheelUp, WheelDown,
        Cancel, // 丢失鼠标捕获：一切按下状态作废
    };

    // 每个事件至多产生一个动作，窗口层据此决定调哪个 Win32 API。
    // 「至多一个」不是简化，是 §4.1 逐条核对后的事实——真出现第二个动作时
    // 该改的是这里的枚举，不是在窗口层偷偷补一刀。
    enum class Action {
        None,
        Next, Prev,
        RotateCW, RotateCCW, // 屏幕空间方向，含义见 README §6.2
        FlipH, FlipV,
        ToggleFullscreen,
        Exit,
        DragWindow, // 转交系统拖动循环（§4.1 Alt + 左键）
        PanBegin, PanMove, PanEnd,
        MoveBegin, MoveStep, MoveEnd, // 中键按住搬窗口，见下
        HoldStart, // 请窗口层起一个 GetDoubleClickTime 的定时器
        CopyImage, // Ctrl + C（§4.2）
        Delete, DeletePermanent, // Delete / Shift + Delete（§4.2）
        OpenStore, // Enter：只在当前这张因缺解码扩展打不开时有意义，
        // 「打不开」是解码结果、不是输入状态，由窗口层裁决（§11 P4）
    };

    struct State {
        bool left = false;
        bool right = false;
        bool middle = false;
        bool moving = false; // 中键按住已进入搬窗口
        bool right_alone = false; // 右键按下后没被滚轮或别的键打断
        Point cursor; // 最近一次光标位置，客户区坐标
        Point press; // 中键按下的位置，用来判拖拽

        // 挪过这个距离就算拖，不必等满 GetDoubleClickTime。
        // 窗口层启动时填 SM_CXDRAG / SM_CYDRAG，这里给个不至于误判的默认值。
        int drag_threshold = 4;

        // ↑/↓、Space 在搬窗口或左键按住时被忽略（§4.2）
        [[nodiscard]] bool suppressed() const {
            return moving || left;
        }
    };

    // alt 只在 LeftDown 时有意义：按住 Alt 的左键是拖窗，不进平移态。
    Action on_mouse(State& st, Event ev, Point at, bool alt);

    // 按键在 KeyUp 上触发，理由是手感而非技术必要性（§4.2）。
    // shift 只对 Delete 有意义（永久删除）；ctrl 组合不受锁定状态限制。
    Action on_key(const State& st, unsigned vk, bool shift = false, bool ctrl = false);
} // namespace gesture

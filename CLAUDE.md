# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Viewer：Windows 原生轻量图片查看器，C++20 + Win32 + WIC，冷启动速度是第一指标。手势、按键与 `viewer.toml` 配置见 README.md。

## 构建

- 只用项目根的 `pwsh ./build.ps1`，不要直接调 `cmake`。它会在找不到 `cl.exe` 时自己导入 vcvars64，`cmake`/`ninja` 不在 PATH 上时借 CLion 自带的那份，然后配置 `ninja-release` preset 并构建 `installer` 目标（产物在 `cmake-build-release/`，含 `viewer.exe` 与 Inno Setup 安装包）。
- 链接报 `LNK1104`（打不开 viewer.exe）是因为用户那边还开着 Viewer，请用户关掉即可。
- 只支持 MSVC / clang-cl：`winrt/base.h`、`/W4 /permissive- /utf-8` 都依赖它，MinGW 会在 CMake 阶段直接 `FATAL_ERROR`。
- 其他 preset：`ninja-debug`（`cmake-build-debug/`）、`ninja-asan`（Debug + `/fsanitize=address`，自动剥掉互斥的 `/RTC1`）。Debug 用动态 CRT，所以不生成 `installer` 目标；Release 用静态 CRT 并开 `/GS /guard:cf`。
- 编译选项是 `/W4`，构建后要检查有没有新增警告。
- 唯一的第三方依赖是 `third_party/` 下 vendored 的 toml++ 单头文件（以 SYSTEM 方式引入）。
- 发版：版本号有两处，`installer/viewer.iss` 的 `AppVersion` 和 `src/res/viewer.rc`，要一起改；然后推送 `v<AppVersion>` tag，`.github/workflows/release.yml` 会在 Windows runner 上跑 `build.ps1`，把安装版（`Viewer-<ver>-x64-setup.exe`）和便携版（`Viewer-<ver>-x64-portable.zip`）发布到同名 Release。

## 测试与验证

- 仓库里没有测试目标。`render`、`gesture`、`catalog::wrap` 被刻意写成不含 `windows.h` 的纯函数/纯状态机，就是为了能单测，改这几处时保持这个约束。
- `.cache/` 放的是手工测试用的样图（各种尺寸、格式、EXIF 方向、动图、alpha、HEIC）。
- GUI 的视觉效果由用户检查：构建完成后列出具体要点的场景交给用户，不要自己起窗口截图。用户明确要求自己跑流程时，在 scratchpad 里另开一个 `-DVIEWER_LOG=ON` 的 Release 构建目录（别动 `cmake-build-release/`），用 `PostMessage WM_KEYUP` 驱动窗口（按键都在 KeyUp 上触发），再读 exe 同目录的 `viewer.log`。
- 日志：`VLOG(...)`（`src/log.hpp`）只在定义了 `VIEWER_LOG` 时编入（Debug 恒开，Release 需 `-DVIEWER_LOG=ON`），关闭时连实参都不求值。只给日志用的局部变量要用 `#ifdef VIEWER_LOG` 包起来，否则 `/W4` 会报未引用。时序类问题只认 Release + 日志的测量结果。

## 架构

进程入口 `src/main.cpp`：不带参数启动 → `settings::run` 设置对话框（写回 `viewer.toml`，写不进去时用 `runas` 提权重启）；带一个路径参数 → `config::load` → `window::create`（先把窗口建出来）→ `window::open`（投解码请求并启动目录扫描，都不等结果）→ 消息循环。

### 线程模型（核心约束）

- UI 线程是 STA，永远不阻塞等后台任务。唯一的例外是 `WM_DESTROY`：先 reset 解码通道、停止扫描线程，再用 `PeekMessage` 排空队列里残留的 `WM_FRAME_READY` / `WM_CATALOG_READY` 并 delete 它们的 payload。
- 后台结果通过 `PostMessage` 投回，`lParam` 是 `new` 出来的对象，所有权交给接收方（`channel::Decoded` / `catalog::List`）。投递失败时由投递方自己回收。
- `channel::Decode`（`src/channel.*`）是单槽通道：新请求直接覆盖还没开工的旧请求，已开工的那张照常交货。不靠取消或世代号丢弃过期结果，**按路径认领**：只保留 `wanted` 与相邻 ±1 的结果，其余丢掉。`State::lanes` 中三条通道组成一个池（当前图 + 预取 ±1），不固定分工。
- 解码线程用 `decode::Session`（常驻 WIC 工厂，不跨线程共享）解出全图，并按请求时的客户区尺寸顺手缩小一份（`channel::Scaled`）。UI 收到时如果尺寸或方向已经变了，就丢弃这份，回退到 UI 线程自己 `prescale`。
- `channel::scan` 在后台扫一次目录（`StrCmpLogicalW` 排序）。列表就绪之前的翻页只累计净位移 `pending_steps`，就绪后用 `catalog::wrap` 一次跳到位。

### 数据与渲染

- `Frame`（`src/frame.hpp`）是跨线程传递的唯一单位，构造完即不可变：BGRA、无行填充，alpha 在解码阶段已合成到背景色上（恒为 255）。`orient` 是 EXIF 方向，**还没有施加到像素上**。
- 旋转/翻转只改 `render::Orient`（D4 群元素，语义固定为「先水平翻转再顺时针旋转」，用户动作一律 `compose(动作, 当前)` 左乘）。像素只在最终 blit 时由 `render::transform` 做一次索引重映射。几何计算一律用 `State::shown_size()`（旋转后的尺寸），不要用 frame 的原始宽高。
- 缩放只缩不放（最大 100%），只有 `render::box_downsample` 这一条路径。左键按住时进入 1:1 视图，配合 `clamp_pan` / `visible` 平移。
- 上屏走 `window::detail::Present`：窗口带 `WS_EX_NOREDIRECTIONBITMAP`，使用 D3D11 flip 交换链 + GDI_COMPATIBLE 后台缓冲，`paint` 照常往 HDC 上画。这是为了消除改尺寸/切全屏时 DWM 用旧内容先顶一帧的问题，不要改回 GDI 重定向表面。`WM_SIZE` 里会同步 `RedrawWindow`。

### 模块边界

- `src/window/` 是唯一处理 Win32 消息的地方。`window.hpp` 是这个模块唯一的对外接口；`state.hpp`（共享 `State`，通过 `GWLP_USERDATA` 取得）和其他 `window/*.hpp` 只供模块内部使用。拆分大致是：`window.cpp` 负责 WndProc 与把输入翻译成 gesture 事件，`session` 负责翻页/调度/收货/删除，`view` 负责方向/平移/复制，`paint` 负责 prescale 与绘制，`chrome` 负责无边框/命中测试/全屏/DPI/初始几何，`present` 负责交换链。
- 输入：WndProc 把消息翻译成 `gesture::Event`，交给纯状态机 `gesture::on_mouse` / `on_key`，得到**至多一个** `gesture::Action`，再由窗口层的 `apply` 执行。需要新增行为时扩展 `Action` 枚举，不要在窗口层绕过状态机直接处理。`gesture` 里手抄了 VK 常量，就是为了不引入 `windows.h`。
- 无边框窗口：`WM_NCCALCSIZE` 把非客户区算成 0，再用 `DwmExtendFrameIntoClientArea` 延伸 1px 取回投影（这个状态自始至终不切换）；客户区命中测试一律 `HTCLIENT`（拖窗口走 Alt + 左键）。
- `shell`：剪贴板 CF_DIB、回收站/永久删除（永久删除故意保留 shell 自带的确认框）、打开 `ms-windows-store://`。缺 HEIF/HEVC 扩展时由 `decode::Status` 区分，画面中央显示提示，按 Enter 跳转商店。
- `installer/viewer.iss`：Inno Setup，默认 per-user 安装（`PrivilegesRequired=lowest`），可选为所有用户安装；负责注册「打开方式」的文件关联。

## 约定

- 注释和面向用户的字符串都是简体中文，注释重在解释「为什么」。注释里的 `§x.y` / `§11 P4` 引用的是一份不在仓库里的原始设计文档，不是现在的 README.md，不要试图在 README 里找对应章节。
- 编译定义带 `UNICODE`：Win32 一律调用 `W` 版本 API，并显式写 `::` 前缀。

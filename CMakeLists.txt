cmake_minimum_required(VERSION 3.21)
project(Viewer LANGUAGES CXX)

if (NOT MSVC)
    # winrt/base.h 是 MSVC / clang-cl 专有，/W4 /permissive- /utf-8 同理。
    # MinGW 不是「差一点」，是根本编不过（§2 §10.3）。
    message(FATAL_ERROR
            "Viewer 需要 MSVC 工具链（README §2），当前检测到 "
            "${CMAKE_CXX_COMPILER_ID}：${CMAKE_CXX_COMPILER}\n"
            "CLion 修法：Settings | Build, Execution, Deployment | Toolchains "
            "新建 Visual Studio 工具链（Architecture = amd64），"
            "再到 CMake 页把它选给当前 Profile。")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VIEWER_ASAN "调试配置启用 /fsanitize=address（README §9.2）" OFF)
option(VIEWER_LOG  "把诊断日志编进 Release（Debug 本来就有）" OFF)

add_executable(viewer WIN32
        src/main.cpp
        src/window/window.cpp
        src/window/paint.cpp
        src/window/view.cpp
        src/window/session.cpp
        src/window/chrome.cpp
        src/window/present.cpp
        src/channel.cpp
        src/decode.cpp
        src/render.cpp
        src/gesture.cpp
        src/catalog.cpp
        src/shell.cpp
        src/config.cpp
        src/settings.cpp
        # 清单里声明 Per-Monitor V2（§5）。MSVC 下 CMake 会把 .manifest 交给
        # mt.exe 与链接器自动生成的那份合并，不需要额外的 /MANIFESTINPUT。
        src/res/viewer.manifest
        # 图标与版本信息。MSVC 下 CMake 自动把 .rc 交给 rc.exe。
        src/res/viewer.rc
)

target_include_directories(viewer PRIVATE src)

# toml++ v3.4.0（MIT），唯一的第三方依赖。单头 vendored 进版本控制：
# 无 FetchContent、无子模块，离线也能构建。SYSTEM 是为了别让 /W4 去数别人家的警告。
target_include_directories(viewer SYSTEM PRIVATE third_party)

# UNICODE：漏了会在 StrCmpLogicalW 之类的地方悄悄退化成 A 版
target_compile_definitions(viewer PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

# 诊断日志：Debug 无条件带上，Release 默认剔除。
# 首帧耗时之类的时序问题在 Debug 上量不准，那时用 -DVIEWER_LOG=ON 单独打开。
target_compile_definitions(viewer PRIVATE
        $<$<OR:$<CONFIG:Debug>,$<BOOL:${VIEWER_LOG}>>:VIEWER_LOG=1>)

target_compile_options(viewer PRIVATE
        /W4 /permissive- /utf-8 /EHsc)

# 解码器处理不可信输入，发布配置的最低要求
target_compile_options(viewer PRIVATE $<$<NOT:$<CONFIG:Debug>>:/GS /guard:cf>)
target_link_options(viewer PRIVATE $<$<NOT:$<CONFIG:Debug>>:/GUARD:CF>)

# 静态 CRT：安装目录里就一个 exe，目标机器不需要 VC++ Redistributable（§11 P6）。
# 这不是洁癖——per-user 安装全程不提权，而装 Redist 要管理员权限，两者不能并存。
# Debug 维持动态 CRT：ASan 的全量回归是在那上面跑的，不动（§10.1）。
set_property(TARGET viewer PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:DebugDLL>")

if (VIEWER_ASAN)
    # ASan 与 /RTC1 互斥，而 MSVC 的 Debug 默认就开着它；
    # /INCREMENTAL 同样会被强制关闭，这里显式拆掉，否则第一次构建就报错。
    string(REGEX REPLACE "/RTC[1csu]+" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    target_compile_options(viewer PRIVATE $<$<CONFIG:Debug>:/fsanitize=address>)
    target_link_options(viewer PRIVATE $<$<CONFIG:Debug>:/INCREMENTAL:NO>)
endif()

# dwmapi：取回无边框窗口的投影；shell32：问自动隐藏任务栏在哪条边（§5）
# comdlg32：设置对话框里的 ChooseColor；d3d11 + dxgi：画面走 flip 交换链上屏
target_link_libraries(viewer PRIVATE windowscodecs shlwapi dwmapi shell32 comdlg32 d3d11 dxgi)

# Inno Setup 不是构建依赖：找不到 ISCC 就只是没有 installer 目标，其余照常。
# winget 装的 Inno Setup 落在 %LocalAppData%（无提权），手动装的在 Program Files。
find_program(VIEWER_ISCC NAMES ISCC
        PATHS "$ENV{LOCALAPPDATA}/Programs/Inno Setup 6"
              "$ENV{ProgramFiles}/Inno Setup 6"
              "$ENV{ProgramFiles\(x86\)}/Inno Setup 6")

# Debug 配置不给打包：它链的是动态 CRT（见上面的 MSVC_RUNTIME_LIBRARY），
# 打出来的包在没装 VC++ Redistributable 的机器上双击就是「找不到 MSVCP140.dll」。
if (VIEWER_ISCC AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_custom_target(installer
            COMMAND "${VIEWER_ISCC}"
                    "/DSourceExe=$<TARGET_FILE:viewer>"
                    "/O$<TARGET_FILE_DIR:viewer>"
                    "${CMAKE_CURRENT_SOURCE_DIR}/installer/viewer.iss"
            DEPENDS viewer
            COMMENT "ISCC：打包安装器"
            VERBATIM)
elseif (NOT VIEWER_ISCC)
    message(STATUS "未找到 ISCC.exe，不生成 installer 目标")
endif()

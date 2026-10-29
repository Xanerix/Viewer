; Viewer 安装器（README §11 P6）
;
; 编译：ISCC.exe /DSourceExe=<viewer.exe 全路径> /O<输出目录> viewer.iss
; 通常不必手敲，根 CMakeLists.txt 的 installer 目标会代劳。
;
; 这里刻意不做 WIC 解码器探测——修订 F 把「按 codec 探测」换成了「让用户自己勾」，
; 理由见 README §11 P6。安装器只把 Viewer 登记为**候选**打开方式：Win10 之后
; 系统不允许安装器静默设置默认程序，真正的「默认打开方式」得用户自己去设置里点。

#define AppName      "Viewer"
#define AppVersion   "1.0.0"
#define AppPublisher "Xanerix"
#define AppExeName  "viewer.exe"
#define ProgId      "Viewer.Image"
#define AssocGroup  "将 Viewer 加入这些文件类型的「打开方式」："

; 默认值只为手动编译方便；CMake 走的是命令行 /DSourceExe= 与 /O。
#ifndef SourceExe
  #define SourceExe "..\cmake-build-release\viewer.exe"
#endif

[Setup]
AppId={{E8F7D76F-3D54-463E-9111-586AE464048D}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
; setup.exe 自己的版本信息资源，跟 src/res/viewer.rc 里那份对齐。
VersionInfoVersion={#AppVersion}
VersionInfoCompany={#AppPublisher}
VersionInfoCopyright=Copyright (C) 2026 {#AppPublisher}
DefaultDirName={autopf}\{#AppName}
UninstallDisplayIcon={app}\{#AppExeName}
; 安装包自己的图标；「添加/删除程序」里的那个走上面的 UninstallDisplayIcon，
; 由 viewer.exe 的图标资源提供。
SetupIconFile=..\src\res\viewer.ico
OutputDir=..\cmake-build-release
OutputBaseFilename={#AppName}-{#AppVersion}-x64-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

; x64 专用：项目只在 MSVC amd64 上构建（README §2）。
; ArchitecturesInstallIn64BitMode 这条不能省——缺了它 per-machine 安装会把
; 注册表写进 HKLM\Software\WOW6432Node\Classes，Explorer 在那儿找不到关联。
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

; 默认 per-user、全程不提权；想装给全机的用户在第一页自己选，那时才弹 UAC。
; 两条路径下 {autopf} 与 HKA 会跟着解析：
;   per-user    → %LocalAppData%\Programs\Viewer + HKCU\Software\Classes
;   per-machine → %ProgramFiles%\Viewer          + HKLM\Software\Classes
; commandline 那一半是给 /ALLUSERS 与 /CURRENTUSER 用的——没有它，静默安装
; 只能走 per-user，per-machine 这条路径就没法在无人值守下验收。
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline

; 装完与卸完各通知一次 Explorer 刷新关联，省掉一段 [Code] 里的 SHChangeNotify。
ChangesAssociations=yes

; 只有一个开始菜单快捷方式，直接放在「程序」根下，不要程序组文件夹，也就不要选组那一页。
; viewer.exe 无参数启动是设置对话框（src/settings.cpp），快捷方式就是设置入口。
DisableProgramGroupPage=yes

[Languages]
; 简中翻译不随 Inno 官方分发，vendored 在同目录（MIT，kira-96/Inno-Setup-
; Chinese-Simplified-Translation），跟 third_party/toml.hpp 一个做法。
Name: "chs"; MessagesFile: "ChineseSimplified.isl"

[Files]
Source: "{#SourceExe}"; DestDir: "{app}"; Flags: ignoreversion

[Tasks]
Name: "startmenuicon"; Description: "在开始菜单创建快捷方式"

; 常见格式默认勾上，冷门的留给用户自己挑。
Name: "assoc_jpg";  Description: ".jpg";  GroupDescription: "{#AssocGroup}"
Name: "assoc_jpeg"; Description: ".jpeg"; GroupDescription: "{#AssocGroup}"
Name: "assoc_png";  Description: ".png";  GroupDescription: "{#AssocGroup}"
Name: "assoc_gif";  Description: ".gif";  GroupDescription: "{#AssocGroup}"
Name: "assoc_bmp";  Description: ".bmp";  GroupDescription: "{#AssocGroup}"
Name: "assoc_webp"; Description: ".webp"; GroupDescription: "{#AssocGroup}"
Name: "assoc_heic"; Description: ".heic（需要 HEVC 视频扩展和 HEIF 图像扩展）"; GroupDescription: "{#AssocGroup}"; Flags: unchecked
Name: "assoc_tif";  Description: ".tif";  GroupDescription: "{#AssocGroup}"; Flags: unchecked
Name: "assoc_tiff"; Description: ".tiff"; GroupDescription: "{#AssocGroup}"; Flags: unchecked
Name: "assoc_pns";  Description: ".pns";  GroupDescription: "{#AssocGroup}"; Flags: unchecked
Name: "assoc_rle";  Description: ".rle";  GroupDescription: "{#AssocGroup}"; Flags: unchecked
Name: "assoc_dib";  Description: ".dib";  GroupDescription: "{#AssocGroup}"; Flags: unchecked

[Icons]
; {autoprograms} 跟 {autopf} 一样随安装模式解析：
;   per-user    → %AppData%\Microsoft\Windows\Start Menu\Programs
;   per-machine → %ProgramData%\Microsoft\Windows\Start Menu\Programs
; 卸载时 Inno 会自动删掉 [Icons] 建的快捷方式。
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: startmenuicon

[Registry]
; ——ProgID 本体————————————————————————————————————————————
; DefaultIcon 写 "%1"：图标从图片自己提取，关联后缩略图照常显示。
; viewer.exe 里没有图标资源，指向它只会让所有图片变成空白 exe 图标。
Root: HKA; Subkey: "Software\Classes\{#ProgId}"; ValueType: string; ValueName: ""; ValueData: "图像文件"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\{#ProgId}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "%1"
Root: HKA; Subkey: "Software\Classes\{#ProgId}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeName}"" ""%1"""

; ——「打开方式 → 浏览」选到 exe 时的身份————————————————————————
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExeName}"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "{#AppName}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExeName}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeName}"" ""%1"""

; ——逐扩展名登记为候选————————————————————————————————————
; 每个扩展名三行，形状一致：
;   1. 扩展名键本身，只为卸载时「空了就删」留个记号；
;   2. 挂进 OpenWithProgIds——只删自己那个值，绝不能用 uninsdeletekey，
;      那会连别的程序登记在同一个键下的值一起端掉；
;   3. 登进 SupportedTypes，父键带 uninsdeletekey，卸载时整枝清掉。
; 卸载按书写逆序执行，所以「删值」总排在「空则删键」前面，顺序不能调。
Root: HKA; Subkey: "Software\Classes\.jpg"; Flags: uninsdeletekeyifempty; Tasks: assoc_jpg
Root: HKA; Subkey: "Software\Classes\.jpg\OpenWithProgIds"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: assoc_jpg
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExeName}\SupportedTypes"; ValueType: string; ValueName: ".jpg"; ValueData: ""; Tasks: assoc_jpg

Root: HKA; Subkey: "Software\Classes\.jpeg"; Flags: uninsdeletekeyifempty; Tasks: assoc_jpeg
Root: HKA; Subkey: "Software\Classes\.jpeg\OpenWithProgIds"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: assoc_jpeg
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExeName}\SupportedTypes"; ValueType: string; ValueName: ".jpeg"; ValueData: ""; Tasks: assoc_jpeg

Root: HKA; Subkey: "Software\Classes\.png"; Flags: uninsdeletekeyifempty; Tasks: assoc_png
Root: HKA; Subkey: "Software\Classes\.png\OpenWithProgIds"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: assoc_png
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExeName}\SupportedTypes"; ValueType: string; ValueName: ".png"; ValueData: ""; Tasks: assoc_png

Root: HKA; Subkey: "Software\Classes\.gif"; Flags: uninsdeletekeyifempty; Tasks: assoc_gif
Root: HKA; Subkey: "Software\Classes\.gif\OpenWithProgIds"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: assoc_gif
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExeName}\SupportedTypes"; ValueType: string; ValueName: ".gif"; ValueData: ""; Tasks: assoc_gif

Root: HKA; Subkey: "Software\Classes\.bmp"; Flags: uninsdeletekeyifempty; Tasks: assoc_bmp
Root: HKA; Subkey: "Software\Classes\.bmp\OpenWithProgIds"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: assoc_bmp
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExeName}\SupportedTypes"; ValueType: string; ValueName: ".bmp"; ValueData: ""; Tasks: assoc_bmp

Root: HKA; Subkey: "Software\Classes\.webp"; Flags: uninsdeletekeyifempty; Tasks: assoc_webp
Root: HKA; Subkey: "Software\Classes\.webp\OpenWithProgIds"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: assoc_webp
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExeName}\SupportedTypes"; ValueType: string; ValueName: ".webp"; ValueData: ""; Tasks: assoc_webp

Root: HKA; Subkey: "Software\Classes\.heic"; Flags: uninsdeletekeyifempty; Tasks: assoc_heic
Root: HKA; Subkey: "Software\Classes\.heic\OpenWithProgIds"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: assoc_heic
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExeName}\SupportedTypes"; ValueType: string; ValueName: ".heic"; ValueData: ""; Tasks: assoc_heic

Root: HKA; Subkey: "Software\Classes\.tif"; Flags: uninsdeletekeyifempty; Tasks: assoc_tif
Root: HKA; Subkey: "Software\Classes\.tif\OpenWithProgIds"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: assoc_tif
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExeName}\SupportedTypes"; ValueType: string; ValueName: ".tif"; ValueData: ""; Tasks: assoc_tif

Root: HKA; Subkey: "Software\Classes\.tiff"; Flags: uninsdeletekeyifempty; Tasks: assoc_tiff
Root: HKA; Subkey: "Software\Classes\.tiff\OpenWithProgIds"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: assoc_tiff
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExeName}\SupportedTypes"; ValueType: string; ValueName: ".tiff"; ValueData: ""; Tasks: assoc_tiff

Root: HKA; Subkey: "Software\Classes\.pns"; Flags: uninsdeletekeyifempty; Tasks: assoc_pns
Root: HKA; Subkey: "Software\Classes\.pns\OpenWithProgIds"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: assoc_pns
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExeName}\SupportedTypes"; ValueType: string; ValueName: ".pns"; ValueData: ""; Tasks: assoc_pns

Root: HKA; Subkey: "Software\Classes\.rle"; Flags: uninsdeletekeyifempty; Tasks: assoc_rle
Root: HKA; Subkey: "Software\Classes\.rle\OpenWithProgIds"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: assoc_rle
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExeName}\SupportedTypes"; ValueType: string; ValueName: ".rle"; ValueData: ""; Tasks: assoc_rle

Root: HKA; Subkey: "Software\Classes\.dib"; Flags: uninsdeletekeyifempty; Tasks: assoc_dib
Root: HKA; Subkey: "Software\Classes\.dib\OpenWithProgIds"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: assoc_dib
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExeName}\SupportedTypes"; ValueType: string; ValueName: ".dib"; ValueData: ""; Tasks: assoc_dib

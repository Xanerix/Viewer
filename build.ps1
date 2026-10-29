#Requires -Version 7.3
# 构建 Release 并打包安装器，产物在 cmake-build-release/。
# 不需要事先进 Developer PowerShell：找不到 cl.exe 时自己导入 vcvars64 的环境。

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
Set-Location $PSScriptRoot

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vcvars = Get-Item 'C:\Program Files*\Microsoft Visual Studio\*\*\VC\Auxiliary\Build\vcvars64.bat' -ErrorAction SilentlyContinue |
              Select-Object -First 1
    if (-not $vcvars) { throw '找不到 vcvars64.bat，需要装 MSVC 工具链' }
    cmd /c "`"$($vcvars.FullName)`" >nul && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] }
    }
}

# cmake / ninja 不在 PATH 上时借 CLion 自带的那份
foreach ($tool in @{ cmake = 'bin\cmake\win\x64\bin'; ninja = 'bin\ninja\win\x64' }.GetEnumerator()) {
    if (Get-Command "$($tool.Key).exe" -ErrorAction SilentlyContinue) { continue }
    $dir = Get-Item "C:\Program Files\JetBrains\CLion*\$($tool.Value)" -ErrorAction SilentlyContinue |
           Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $dir) { throw "找不到 $($tool.Key)" }
    $env:PATH = "$($dir.FullName);$env:PATH"
}

cmake --preset ninja-release
cmake --build --preset ninja-release --target installer

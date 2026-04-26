@echo off
REM ============================================
REM T3000汉化 + AgentBridge 一键编译脚本
REM 在 Windows 上运行，需要 Visual Studio 2019
REM ============================================

echo ================================================
echo    T3000 建筑自动化系统 - 一键编译
echo    汉化 + AgentBridge 智能体对接
echo ================================================
echo.

REM 检查 VS2019 是否安装
where cl.exe >nul 2>nul
if %errorlevel% neq 0 (
    echo [错误] 未找到 Visual Studio 编译器
    echo 请运行 VS2019 的开发者命令提示符，或设置环境变量
    echo.
    echo 方法 1: 开始菜单 -> Visual Studio 2019 -> x86 Native Tools Command Prompt
    echo 方法 2: 运行 "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars32.bat"
    pause
    exit /b 1
)

echo [1/6] 检查项目文件...
if not exist "T3000 - VS2019.sln" (
    echo [错误] 找不到解决方案文件 T3000 - VS2019.sln
    pause
    exit /b 1
)
echo [√] 解决方案文件存在

echo.
echo [2/6] 检查新增源文件...
set MISSING_FILES=0
for %%f in (
    "T3000\AgentBridge.h"
    "T3000\AgentBridge.cpp"
    "T3000\AgentHttpServer.h"
    "T3000\AgentHttpServer.cpp"
    "T3000\AgentWebSocket.h"
    "T3000\AgentWebSocket.cpp"
    "T3000\AgentMcpServer.h"
    "T3000\AgentMcpServer.cpp"
    "T3000\AgentBridgeDlg.h"
    "T3000\LanguageLocale_zh.ini"
) do (
    if not exist %%f (
        echo [!] 缺少文件: %%f
        set MISSING_FILES=1
    )
)
if %MISSING_FILES%==0 (
    echo [√] 所有新增文件存在
) else (
    echo [!] 部分文件缺失，请检查
)

echo.
echo [3/6] 将 AgentBridge 源文件添加到项目...
REM 使用 PowerShell 修改 .vcxproj 文件
powershell -Command "$proj = 'T3000\T3000_VS2019.vcxproj'; $content = Get-Content $proj -Raw -Encoding UTF8; $files = @('AgentBridge.h','AgentBridge.cpp','AgentHttpServer.h','AgentHttpServer.cpp','AgentWebSocket.h','AgentWebSocket.cpp','AgentMcpServer.h','AgentMcpServer.cpp','AgentBridgeDlg.h'); foreach ($f in $files) { if ($content -notmatch $f) { $ext = [System.IO.Path]::GetExtension($f); if ($ext -eq '.h') { $content = $content -replace '(</ItemGroup>)', \"`t<ClInclude Include=`\"$f`\" />`r`n`$1\" } else { $content = $content -replace '(</ItemGroup>)', \"`t<ClCompile Include=`\"$f`\" />`r`n`$1\" } } }; Set-Content $proj $content -Encoding UTF8; Write-Host '[√] 项目文件已更新'"

echo.
echo [4/6] 编译主程序 (Release / Win32)...
echo 使用 MSBuild 编译...
msbuild "T3000 - VS2019.sln" /p:Configuration=Release /p:Platform=Win32 /t:Rebuild /m /v:minimal

if %errorlevel% neq 0 (
    echo.
    echo [错误] 编译失败，请检查错误信息
    echo 常见原因:
    echo   1. 未安装 C++ MFC 组件
    echo   2. 缺少依赖库
    echo   3. 代码有编译错误
    pause
    exit /b 1
)

echo.
echo [5/6] 编译 C# 控件库...
if exist "T3000Controls\T3000Controls.sln" (
    msbuild "T3000Controls\T3000Controls.sln" /p:Configuration=Release /t:Rebuild /m /v:minimal
    if %errorlevel% neq 0 (
        echo [!] C# 控件库编译失败（非致命错误）
    ) else (
        echo [√] C# 控件库编译成功
    )
) else (
    echo [!] 未找到 C# 控件库解决方案
)

echo.
echo [6/6] 复制输出文件到安装目录...
if not exist "T3000InstallShield" mkdir "T3000InstallShield"

REM 复制主程序
if exist "Release\T3000.exe" (
    copy "Release\T3000.exe" "T3000InstallShield\" >nul
    echo [√] 已复制 T3000.exe
)

REM 复制 DLL
for %%f in (T3000Controls.dll ModbusDllforVc.dll FlexSlideBar.dll sqlite3.dll) do (
    if exist "Release\%%f" (
        copy "Release\%%f" "T3000InstallShield\" >nul
        echo [√] 已复制 %%f
    )
)

REM 复制中文语言包
if exist "T3000\LanguageLocale_zh.ini" (
    copy "T3000\LanguageLocale_zh.ini" "T3000InstallShield\" >nul
    echo [√] 已复制中文语言包
)

echo.
echo ================================================
echo    编译完成！
echo ================================================
echo.
echo 输出文件位置:
echo   主程序: Release\T3000.exe
echo   安装包: T3000InstallShield\
echo.
echo 下一步:
echo   1. 运行 T3000.exe 测试
echo   2. 编译 Inno Setup 安装包 (T3000_zh.iss)
echo.
pause

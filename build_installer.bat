@echo off
REM ============================================
REM T3000 Inno Setup 安装包编译脚本
REM 需要安装 Inno Setup 6.x
REM ============================================

echo ================================================
echo    T3000 建筑自动化系统 - 编译安装包
echo ================================================
echo.

REM 检查 Inno Setup 是否安装
set ISSCC=""
if exist "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" (
    set ISSCC="C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
) else if exist "C:\Program Files\Inno Setup 6\ISCC.exe" (
    set ISSCC="C:\Program Files\Inno Setup 6\ISCC.exe"
)

if %ISSCC%=="" (
    echo [错误] 未找到 Inno Setup 编译器
    echo 请安装 Inno Setup 6.x: https://jrsoftware.org/isdl.php
    pause
    exit /b 1
)

echo [√] Inno Setup 编译器: %ISSCC%
echo.

REM 检查脚本文件
if not exist "T3000_zh.iss" (
    echo [错误] 找不到安装包脚本 T3000_zh.iss
    pause
    exit /b 1
)

echo [1/2] 检查安装文件...
set MISSING=0
for %%f in (
    "T3000InstallShield\T3000.exe"
    "T3000InstallShield\T3000Controls.dll"
    "T3000InstallShield\LanguageLocale_zh.ini"
) do (
    if not exist %%f (
        echo [!] 缺少文件: %%f
        echo    请先运行 build_zh.bat 编译主程序
        set MISSING=1
    )
)
if %MISSING%==1 (
    pause
    exit /b 1
)
echo [√] 所有安装文件存在

echo.
echo [2/2] 编译安装包...
%ISSCC% "T3000_zh.iss"

if %errorlevel% neq 0 (
    echo.
    echo [错误] 编译安装包失败
    pause
    exit /b 1
)

echo.
echo ================================================
echo    安装包编译完成！
echo ================================================
echo.
if exist "Output\T3000_建筑自动化系统_Setup.exe" (
    echo 安装包位置: Output\T3000_建筑自动化系统_Setup.exe
    for %%A in ("Output\T3000_建筑自动化系统_Setup.exe") do echo 文件大小: %%~zA 字节
) else (
    echo 安装包位置: Output\ 目录
)
echo.
pause

@echo off
chcp 65001 >nul 2>&1
echo ========================================
echo   ArbExpress Generator - 编译中...
echo ========================================

set GXX=C:\msys64\ucrt64\bin\g++.exe
set WINDRES=C:\msys64\ucrt64\bin\windres.exe
set OUT=ArbExpressGenerator.exe
set FLAGS=-municode -mwindows -O2 -static -Wall

echo [1/2] 编译资源 (manifest)...
"%WINDRES%" src/app.rc -O coff -o src/app_res.o

echo [2/2] 编译 C++ 代码...
"%GXX%" %FLAGS% src\main.cpp src\waveform.cpp src\app_res.o -o %OUT% -lgdi32 -lgdiplus -lshell32 -luser32 -lcomctl32 -lole32

if %errorlevel% equ 0 (
    echo.
    echo   编译成功! 输出: %OUT%
    echo   运行: %OUT%
    echo.
    start "" %OUT%
) else (
    echo.
    echo   *** 编译失败! ***
)
pause

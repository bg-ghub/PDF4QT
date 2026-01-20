@echo off
setlocal enabledelayedexpansion

echo === PDF4QT-Opus Local Build ===

REM Use VS2022 BuildTools - installed via winget
set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
if not exist "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" (
    echo ERROR: VS2022 BuildTools not found at %VS_PATH%
    exit /b 1
)

echo Using VS2022 BuildTools at: %VS_PATH%

REM Set up VS2022 environment
call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat"
if %errorlevel% neq 0 (
    echo ERROR: Failed to set up VS2022 environment
    exit /b 1
)

echo Compiler version: %VCToolsVersion%

REM Set up paths
set Path=C:\Program Files\CMake\bin;C:\Qt\6.9.0\msvc2022_64\bin;%Path%
set VCPKG_ROOT=C:\vcpkg
set Qt6_DIR=C:\Qt\6.9.0\msvc2022_64
set CMAKE_PREFIX_PATH=C:\Qt\6.9.0\msvc2022_64

echo === Cleaning previous build ===
if exist build rmdir /s /q build

echo === Running CMake Configure ===
cmake -G "Visual Studio 17 2022" -A x64 -B build -S . ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake ^
    -DPDF4QT_QT_ROOT=C:\Qt\6.9.0\msvc2022_64 ^
    -DPDF4QT_INSTALL_QT_DEPENDENCIES=ON ^
    -DPDF4QT_INSTALL_DEPENDENCIES=ON ^
    -DPDF4QT_INSTALL_MSVC_REDISTRIBUTABLE=ON
if %errorlevel% neq 0 (
    echo ERROR: CMake configure failed
    exit /b %errorlevel%
)

echo === Building ===
cmake --build build --config Release -j%NUMBER_OF_PROCESSORS%
if %errorlevel% neq 0 (
    echo ERROR: Build failed
    exit /b %errorlevel%
)

echo === Installing (deploys all dependencies) ===
cmake --install build --config Release
if %errorlevel% neq 0 (
    echo ERROR: Install failed
    exit /b %errorlevel%
)

echo === Build Complete! ===
echo.
echo Output is in: build\install\usr\bin\
echo Run: build\install\usr\bin\Pdf4QtEditor.exe

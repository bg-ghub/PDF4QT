@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
set Path=C:\Program Files\CMake\bin;C:\Qt\6.9.0\msvc2022_64\bin;%Path%
set VCPKG_ROOT=C:\vcpkg
set Qt6_DIR=C:\Qt\6.9.0\msvc2022_64
set CMAKE_PREFIX_PATH=C:\Qt\6.9.0\msvc2022_64

echo === Running CMake Configure ===
cmake -G "Visual Studio 17 2022" -A x64 -B build -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake -DPDF4QT_QT_ROOT=C:\Qt\6.9.0\msvc2022_64
if %errorlevel% neq 0 exit /b %errorlevel%

echo === Building ===
cmake --build build --config Release -j6
if %errorlevel% neq 0 exit /b %errorlevel%

echo === Build Complete! ===

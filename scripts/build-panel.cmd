@echo off
setlocal
if not defined W11WIDGET_QT_PREFIX set "W11WIDGET_QT_PREFIX=C:\Qt\6.12.0\msvc2022_64"
where cmake.exe >nul 2>nul
if errorlevel 1 (
    echo CMake not found. Run this script from an x64 Native Tools Command Prompt for VS 2022 with CMake on PATH.
    exit /b 1
)
where ninja.exe >nul 2>nul
if errorlevel 1 (
    echo Ninja not found. Install Ninja and add it to PATH.
    exit /b 1
)
if not exist "%W11WIDGET_QT_PREFIX%\lib\cmake\Qt6\Qt6Config.cmake" (
    echo Qt 6.12 MSVC 2022 development files not found. Set W11WIDGET_QT_PREFIX to the Qt prefix directory.
    exit /b 1
)
cmake.exe -S "%~dp0.." -B "%~dp0..\build-overlay" -G Ninja -DCMAKE_BUILD_TYPE=Release -DW11WIDGET_BUILD_OVERLAY=ON "-DCMAKE_PREFIX_PATH=%W11WIDGET_QT_PREFIX%"
if errorlevel 1 exit /b 1
cmake.exe --build "%~dp0..\build-overlay"
if errorlevel 1 exit /b 1
ctest.exe --test-dir "%~dp0..\build-overlay" --output-on-failure
exit /b %errorlevel%

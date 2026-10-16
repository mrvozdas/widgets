@echo off
setlocal
if not defined W11WIDGET_QT_PREFIX set "W11WIDGET_QT_PREFIX=C:\Qt\6.12.0\msvc2022_64"
if not exist "%~dp0..\build-overlay\panel-executable.txt" (
    echo Panel executable not found. Run scripts\build-panel.cmd first.
    exit /b 1
)
set "W11WIDGET_EXE="
set /p W11WIDGET_EXE=<"%~dp0..\build-overlay\panel-executable.txt"
if not exist "%W11WIDGET_EXE%" (
    echo Panel executable not found. Run scripts\build-panel.cmd first.
    exit /b 1
)
if not exist "%W11WIDGET_QT_PREFIX%\bin\Qt6Core.dll" (
    echo Qt runtime not found. Set W11WIDGET_QT_PREFIX to your Qt directory.
    exit /b 1
)
set "PATH=%W11WIDGET_QT_PREFIX%\lib\qt6\bin;%W11WIDGET_QT_PREFIX%\bin;%PATH%"
if exist "%W11WIDGET_QT_PREFIX%\lib\qt6\plugins\platforms\qwindows.dll" (
    set "QT_PLUGIN_PATH=%W11WIDGET_QT_PREFIX%\lib\qt6\plugins"
    set "QML_IMPORT_PATH=%W11WIDGET_QT_PREFIX%\lib\qt6\qml"
) else (
    set "QT_PLUGIN_PATH=%W11WIDGET_QT_PREFIX%\plugins"
    set "QML_IMPORT_PATH=%W11WIDGET_QT_PREFIX%\qml"
)
set "QT_QPA_PLATFORM=windows"
"%W11WIDGET_EXE%" %*
exit /b %errorlevel%

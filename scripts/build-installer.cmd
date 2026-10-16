@echo off
setlocal
call "%~dp0build-panel.cmd"
if errorlevel 1 exit /b 1
cmake.exe --build "%~dp0..\build-overlay" --target verify_installer_w11widget
exit /b %errorlevel%

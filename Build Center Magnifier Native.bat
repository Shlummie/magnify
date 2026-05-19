@echo off
setlocal
pushd "%~dp0"

set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" goto missing_vcvars

call "%VCVARS%"
if errorlevel 1 exit /b %errorlevel%

powershell -NoProfile -ExecutionPolicy Bypass -Command "if (Get-Process -Name CenterMagnifierNative -ErrorAction SilentlyContinue) { exit 0 } exit 1" >nul 2>nul
if not errorlevel 1 goto exe_running

cl /nologo /std:c++17 /EHsc /W4 /permissive- /O2 center_magnifier_native.cpp /Fe:CenterMagnifierNative.exe
set "BUILD_EXIT=%errorlevel%"
popd
exit /b %BUILD_EXIT%

:exe_running
echo CenterMagnifierNative.exe is running and Windows will keep the output file locked.
echo Close Center Magnifier Native from the tray or Task Manager, then run this build again.
popd
exit /b 2

:missing_vcvars
echo Visual Studio Build Tools were not found at:
echo %VCVARS%
popd
exit /b 1

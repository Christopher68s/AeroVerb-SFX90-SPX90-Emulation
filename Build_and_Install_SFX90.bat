@echo off
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build "%~dp0build" --parallel
if %ERRORLEVEL% EQU 0 (
    powershell -NoProfile -Command "Remove-Item -Path \"$env:LOCALAPPDATA\Programs\Common\VST3\SFX90.vst3\" -Recurse -Force -ErrorAction SilentlyContinue; Copy-Item -Path \"%~dp0build\AeroFX90_artefacts\Release\VST3\SFX90.vst3\" -Destination \"$env:LOCALAPPDATA\Programs\Common\VST3\\" -Recurse -Force"
    echo.
    echo [SUCCESS] SFX90.vst3 and SFX90.exe built and installed to %LOCALAPPDATA%\Programs\Common\VST3\SFX90.vst3
) else (
    echo.
    echo [ERROR] Build failed.
)
pause

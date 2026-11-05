@echo off
rem Configures and builds HF Studio (Release). Run from any terminal.
setlocal
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -prerelease -property installationPath`) do set VSDIR=%%i
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 || exit /b 1
cmake --build build --config Release || exit /b 1
echo.
echo Build OK: %~dp0build\cli\Release\hfstudio.exe

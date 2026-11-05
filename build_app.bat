@echo off
rem Builds the core (CMake) and the WinUI 3 app (MSBuild), Release x64.
setlocal
cd /d "%~dp0"
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -prerelease -property installationPath`) do set VSDIR=%%i
if not defined VSDIR (echo ERRO: Visual Studio nao encontrado & exit /b 1)
echo Visual Studio: %VSDIR%
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

rem Use the CMake bundled with Visual Studio: an older standalone CMake on PATH (e.g. on CI
rem runners) may not know the "Visual Studio 18 2026" generator.
set "CMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE%" set CMAKE=cmake

if not exist tools mkdir tools
if not exist tools\nuget.exe (
  echo Baixando nuget.exe...
  powershell -NoProfile -Command "Invoke-WebRequest https://dist.nuget.org/win-x86-commandline/latest/nuget.exe -OutFile tools\nuget.exe" || exit /b 1
)
tools\nuget.exe restore app\packages.config -PackagesDirectory app\packages -NonInteractive -Verbosity quiet || exit /b 1

"%CMAKE%" -S . -B build -G "Visual Studio 18 2026" -A x64 || exit /b 1
"%CMAKE%" --build build --config Release --target hfstudio_core -- /v:minimal /nologo || exit /b 1

msbuild app\NpuStudio.vcxproj /p:Configuration=Release /p:Platform=x64 /m /v:minimal /nologo || exit /b 1
echo.
echo Build OK: %~dp0build\app\Release\NpuStudio.exe

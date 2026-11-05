@echo off
rem Builds the core (CMake) and the WinUI 3 app (MSBuild), Release x64.
setlocal
cd /d "%~dp0"
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -prerelease -property installationPath`) do set VSDIR=%%i
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

if not exist tools\nuget.exe (
  echo Baixando nuget.exe...
  powershell -NoProfile -Command "Invoke-WebRequest https://dist.nuget.org/win-x86-commandline/latest/nuget.exe -OutFile tools\nuget.exe" || exit /b 1
)
tools\nuget.exe restore app\packages.config -PackagesDirectory app\packages -NonInteractive -Verbosity quiet || exit /b 1

cmake -S . -B build -G "Visual Studio 18 2026" -A x64 >nul || exit /b 1
cmake --build build --config Release --target hfstudio_core -- /v:minimal /nologo || exit /b 1

msbuild app\NpuStudio.vcxproj /p:Configuration=Release /p:Platform=x64 /m /v:minimal /nologo || exit /b 1
echo.
echo Build OK: %~dp0build\app\Release\NpuStudio.exe

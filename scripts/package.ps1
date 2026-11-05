<#
.SYNOPSIS
  Packages the built app into dist\: a portable zip and an Inno Setup installer.

.EXAMPLE
  .\build_app.bat
  powershell -File scripts\package.ps1 -Version 0.1.0
#>
param(
    [string]$Version = "0.0.0-dev",
    [string]$Iscc = ""   # path to ISCC.exe; auto-detected if empty
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root "build\app\Release"
$dist = Join-Path $root "dist"
$stage = Join-Path $dist "NpuStudio-$Version"

if (-not (Test-Path (Join-Path $build "NpuStudio.exe"))) { throw "NpuStudio.exe nao encontrado; rode build_app.bat antes." }

# 1. Stage the app without build leftovers (debug symbols, import libs, linker caches).
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage | Out-Null
robocopy $build $stage /E /NFL /NDL /NJH /NJS /NP /XF *.pdb *.lib *.exp *.ipdb *.iobj | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy falhou ($LASTEXITCODE)" }

# 2. App-local Visual C++ runtime, so the app runs on PCs without the VC++ Redistributable.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -prerelease -property installationPath
$crt = Get-ChildItem "$vs\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT" -Directory | Sort-Object FullName | Select-Object -Last 1
if (-not $crt) { throw "runtime do Visual C++ nao encontrado em $vs\VC\Redist" }
Copy-Item "$($crt.FullName)\*.dll" $stage -Force
Write-Host "VC++ runtime: $($crt.FullName)"

# 3. Portable zip.
$zip = Join-Path $dist "NpuStudio-$Version-portable.zip"
Remove-Item $zip -ErrorAction SilentlyContinue
Compress-Archive -Path "$stage\*" -DestinationPath $zip -CompressionLevel Optimal

# 4. Installer.
if (-not $Iscc) {
    $Iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
              "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
              "$env:ProgramFiles\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $Iscc) { throw "Inno Setup (ISCC.exe) nao encontrado. Instale: winget install JRSoftware.InnoSetup" }
# Inno's AppVersion must be numeric (x.y.z); strip any suffix like -dev or -rc1.
$numeric = ($Version -replace '^v', '') -replace '[^0-9.].*$', ''
if (-not $numeric) { $numeric = "0.0.0" }
& $Iscc /Q "/DAppVersion=$Version" "/DAppVersionNumeric=$numeric" "/DSourceDir=$stage" "/DOutputDir=$dist" `
    (Join-Path $root "installer\NpuStudio.iss")
if ($LASTEXITCODE -ne 0) { throw "ISCC falhou ($LASTEXITCODE)" }

Get-ChildItem $dist -File | ForEach-Object { "{0,-45} {1,8:N1} MB" -f $_.Name, ($_.Length / 1MB) }

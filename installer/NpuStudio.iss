; Inno Setup script for NPU Studio. Built by scripts\package.ps1, which passes:
;   AppVersion (e.g. 0.1.0), AppVersionNumeric (x.y.z), SourceDir (staged app), OutputDir.

#ifndef AppVersion
  #define AppVersion "0.0.0-dev"
#endif
#ifndef AppVersionNumeric
  #define AppVersionNumeric "0.0.0"
#endif
#ifndef SourceDir
  #error SourceDir must be defined (folder with NpuStudio.exe)
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif

[Setup]
; Fixed AppId: lets new versions upgrade the existing install. Never change it.
AppId={{3B7E1C2A-9D4F-4A6B-8E21-5C0F7A9D3E61}
AppName=NPU Studio
AppVersion={#AppVersion}
VersionInfoVersion={#AppVersionNumeric}
AppPublisher=valdeirsapara
AppPublisherURL=https://github.com/valdeirsapara/Npu-Studio
AppSupportURL=https://github.com/valdeirsapara/Npu-Studio/issues
DefaultDirName={autopf}\NPU Studio
DefaultGroupName=NPU Studio
DisableProgramGroupPage=yes
; Per-user install by default (no admin prompt); the user may choose "all users".
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
OutputDir={#OutputDir}
OutputBaseFilename=NpuStudio-Setup-{#AppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
; ISCC is 32-bit: ultra64 on ~300 MB runs out of address space unless LZMA runs in its own
; (64-bit) process.
LZMAUseSeparateProcess=yes
WizardStyle=modern
UninstallDisplayName=NPU Studio
UninstallDisplayIcon={app}\NpuStudio.exe
CloseApplications=yes

[Languages]
Name: "ptbr"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\NPU Studio"; Filename: "{app}\NpuStudio.exe"
Name: "{group}\{cm:UninstallProgram,NPU Studio}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\NPU Studio"; Filename: "{app}\NpuStudio.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\NpuStudio.exe"; Description: "{cm:LaunchProgram,NPU Studio}"; Flags: nowait postinstall skipifsilent

; Downloaded models (%LOCALAPPDATA%\HFStudio\models) and generated images are user data:
; the uninstaller leaves them in place on purpose.

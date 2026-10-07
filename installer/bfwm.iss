; BFWM installer - compile with Inno Setup 6+ (iscc)
;   iscc /DMyAppVersion=<version> installer\BFWM.iss
; Output: installer\output\BFWM-Setup-<version>.exe
; Version falls back to the VERSION file value when /D is not given.

#define MyAppName "BFWM"
#ifndef MyAppVersion
#define MyAppVersion "0.0.1"
#endif
#define MyAppPublisher "BFWM"
#define MyAppExeName "BFWM.exe"

[Setup]
AppId={{12367CC0-072C-42D2-9777-DE36BADD5575}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={localappdata}\Programs\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=output
OutputBaseFilename=BFWM-Setup-{#MyAppVersion}
SetupIconFile=..\icons\BFWM.ico
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "autostart"; Description: "Start {#MyAppName} automatically when I log in"; GroupDescription: "Startup:"; Flags: checkedonce

[Files]
Source: "..\build\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\config.lua"; DestDir: "{userappdata}\BFWM"; DestName: "config.lua"; Flags: onlyifdoesntexist uninsneveruninstall

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "{#MyAppName}"; ValueData: """{app}\{#MyAppExeName}"""; Flags: uninsdeletevalue; Tasks: autostart

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\BFWM Documentation"; Filename: "https://alex98235.github.io/BFWM/"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName} now"; Flags: nowait postinstall skipifsilent

; Inno Setup script for rEFInd_GUI (Windows).
;
; Packages the deploy\ staging folder (assembled by the build — see
; .github/workflows/windows-release.yml or the README "Windows" section) into
; a per-user installer. It installs to %LOCALAPPDATA%\rEFInd_GUI, which is
; exactly the directory the app reads/writes at runtime (Platform::dataDir()),
; so no files are duplicated and no admin rights are needed to install. The
; app itself requests Administrator at launch via its embedded manifest.

#define AppName "rEFInd GUI"
#define AppVersion "2.0.3"
#define AppExe "rEFInd_GUI.exe"

[Setup]
AppId={{6F9C2E7A-1B4D-4E8F-9C3A-7D5E2F1A0B8C}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=jlobue10
AppPublisherURL=https://github.com/jlobue10/rEFInd_GUI
DefaultDirName={localappdata}\rEFInd_GUI
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=yes
PrivilegesRequired=lowest
OutputBaseFilename=rEFInd_GUI-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\{#AppExe}

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional icons:"

[Files]
; deploy\ holds the final runtime layout: exe + Qt/MinGW DLLs + plugin dirs,
; plus windows\*.ps1, icons\, backgrounds\, and GUI\refind.conf (seed config).
Source: "..\deploy\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent runascurrentuser

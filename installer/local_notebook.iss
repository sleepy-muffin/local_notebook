; Inno Setup script for Local Notebook.
;
; The release workflow compiles this with ISCC, passing three values on the
; command line so the script itself stays free of build-time values:
;
;   /DAppVersion=<tag>       the version shown in the wizard and Add/Remove Programs
;   /DSourceDir=<abs path>   the deployed application directory to package
;   /DOutputDir=<abs path>   where the finished installer is written
;
; Each has a default below, so `iscc local_notebook.iss` still produces a
; working (if generically named) installer from a local build.

#ifndef AppVersion
  #define AppVersion "dev"
#endif

#ifndef SourceDir
  #define SourceDir "..\installer-stage"
#endif

#ifndef OutputDir
  #define OutputDir "..\dist"
#endif

#define AppName "Local Notebook"
#define AppPublisher "sleepy-muffin"
#define AppURL "https://github.com/sleepy-muffin/local_notebook"
#define AppExeName "local_notebook.exe"

[Setup]
; A fixed AppId ties every version to the same product, so an upgrade replaces
; the old install instead of appearing beside it in Add/Remove Programs.
AppId={{B3F4C2A1-7D6E-4F9A-9C1B-2E8D5A6F7C30}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}

; Per-user install: no administrator prompt, which matters more than a
; Program Files path for someone trying the app for the first time.
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
PrivilegesRequired=lowest
DisableProgramGroupPage=yes

OutputDir={#OutputDir}
OutputBaseFilename=local_notebook-{#AppVersion}-windows-x86_64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

SetupIconFile=..\assets\appicon\app.ico
UninstallDisplayIcon={app}\{#AppExeName}

ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
; Unticked by default: a desktop shortcut is a choice, not an imposition.
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; The deployed tree holds the executable, the Qt DLLs and the plugin folders
; windeployqt produces, so the subdirectory flags matter.
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(AppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

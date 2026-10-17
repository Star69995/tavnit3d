; Tavnit 3D installer (Inno Setup 6). Packs the portable build in dist\dune3d.
; Build: tools\build.cmd installer   (or: ISCC tools\installer\tavnit3d.iss)
; Silent update: Tavnit3D-<ver>-setup.exe /SILENT /SUPPRESSMSGBOXES /NORESTART [/RELAUNCH=1]

#ifndef AppVersion
  #define AppVersion "0.3.0" ; keep in sync with app_version in src/util/branding.hpp
#endif
#define AppName "Tavnit 3D"
#define AppExe "dune3d.exe"

[Setup]
; never change AppId: upgrades find the installed copy by it
AppId={{6E1E4B53-6B2C-4F43-9D45-7A1C0E3B3D21}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Tavnit 3D
AppComments=A friendly parametric 3D CAD, based on Dune 3D
DefaultDirName={autopf}\Tavnit 3D
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; per user without admin rights by default, everyone if the user chooses
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
WizardImageFile=wizard-side.bmp
WizardSmallImageFile=wizard-small.bmp
SetupIconFile=..\..\src\icons\dune3d.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
LicenseFile=..\..\LICENSE
OutputDir=..\..\dist
OutputBaseFilename=Tavnit3D-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
; wait for / close a running copy before replacing files
AppMutex=Tavnit3DAppMutex
CloseApplications=yes
RestartApplications=no
ChangesAssociations=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "hebrew"; MessagesFile: "compiler:Languages\Hebrew.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "associate"; Description: "Open .d3ddoc documents with {#AppName}"; GroupDescription: "File types:"

[Files]
Source: "..\..\dist\dune3d\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.d3ddoc"; ValueType: string; ValueName: ""; ValueData: "Tavnit3D.Document"; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Tavnit3D.Document"; ValueType: string; ValueName: ""; ValueData: "{#AppName} document"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Tavnit3D.Document\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe},0"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Tavnit3D.Document\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: associate

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
; the in-app updater passes /RELAUNCH=1 to get the new version started again
Filename: "{app}\{#AppExe}"; Flags: nowait; Check: RelaunchRequested

[Code]
function RelaunchRequested: Boolean;
begin
  Result := WizardSilent and (ExpandConstant('{param:RELAUNCH|0}') = '1');
end;

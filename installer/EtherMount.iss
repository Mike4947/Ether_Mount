; EtherMount Installer - Inno Setup 6 Wizard Script
; Bundles the app and all dependencies into a single installer exe.
; Requires: Build EtherMount first, then run package.ps1 to populate payload/

#define MyAppName "EtherMount"
#define MyAppVersion "0.0.3"
#define MyAppPublisher "EtherMount"
#define MyAppURL "https://github.com/Mike4947/Ether_Mount"
#define MyAppExeName "EtherMount.exe"

[Setup]
AppId={{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
; Output
OutputDir=output
OutputBaseFilename=EtherMount-Setup-{#MyAppVersion}
SetupIconFile=
Compression=lzma2/ultra64
SolidCompression=yes
; 64-bit only (EtherMount is x64)
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
; Wizard
WizardStyle=modern
WizardSizePercent=120,120
WizardResizable=yes
; Privileges
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "quicklaunchicon"; Description: "{cm:CreateQuickLaunchIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked; OnlyBelowVersion: 6.1; Check: not IsAdminInstallMode

[Files]
; Bundle everything from payload/ (populated by package.ps1)
Source: "payload\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon
Name: "{userappdata}\Microsoft\Internet Explorer\Quick Launch\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: quicklaunchicon

[Run]
; Optional: launch after install
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}"

[Code]
var
  WinFspNeeded: Boolean;

function WinFspInstalled: Boolean;
var
  RegPath: string;
begin
  Result := False;
  RegPath := 'SOFTWARE\WinFsp';
  if RegKeyExists(HKEY_LOCAL_MACHINE, 'SOFTWARE\WOW6432Node\WinFsp') then
    RegPath := 'SOFTWARE\WOW6432Node\WinFsp';
  if RegKeyExists(HKEY_LOCAL_MACHINE, RegPath) then
    Result := True;
  if not Result then
  if RegKeyExists(HKEY_CURRENT_USER, RegPath) then
    Result := True;
end;

procedure InitializeWizard;
begin
  { Wizard-style: pages are built-in (Welcome, SelectDir, Installing, Finished). }
  { Optional: customize welcome page }
  WizardForm.WelcomeLabel1.Caption := 'This wizard will install EtherMount on your computer.' + #13#10 + #13#10 +
    'EtherMount mounts a remote VPS (via SFTP) as a native network drive in File Explorer.' + #13#10 + #13#10 +
    'EtherMount requires WinFSP to be installed. If you do not have it, the installer can open the WinFSP download page for you.' + #13#10 + #13#10 +
    'It is recommended that you close any other applications before continuing.';
  WizardForm.WelcomeLabel2.Caption := 'Click Next to continue.';

  { Optional: check if WinFSP is installed }
  WinFspNeeded := not WinFspInstalled;
  if WinFspNeeded then
    Log('WinFSP not detected - user may need to install it separately');
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpFinished then
  begin
    if WinFspNeeded then
      WizardForm.FinishedLabel.Caption := 'EtherMount has been installed.' + #13#10 + #13#10 +
        'WinFSP was not detected on this system. EtherMount requires WinFSP to mount the network drive. ' +
        'Download it from: https://github.com/winfsp/winfsp/releases' + #13#10 + #13#10 +
        'Click Finish to exit the installer.';
  end;
end;

; Inno Setup script for the per-user Lynxer install.
;
; Stage the release payload first:
;   packaging/windows/stage-payload.sh
; Then compile with Inno Setup 6:
;   iscc packaging/windows/lynxer.iss
;
; The installer writes the --install layout under %LOCALAPPDATA%\Programs\Lynxer,
; adds <prefix>\bin and <prefix>\lib\lynxer to the user PATH (the
; runtime DLLs and lynxer.dll live in the latter), and registers an uninstall
; entry.

#define MyAppName "Lynxer"
#define MyAppVersion "0.1.9b1"
#define MyAppPublisher "andy64lol"
#define MyAppURL "https://github.com/andy64lol/Lynxer"
#define MyAppExeName "lynxer.exe"
#define PayloadDir "..\..\build\windows-payload"

[Setup]
AppId={{6E2E5E1B-6D9A-4C0B-9B7E-6C5A6B0A9F11}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={localappdata}\Programs\Lynxer
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=..\..\dist
OutputBaseFilename=lynxer-{#MyAppVersion}-windows-amd64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ChangesEnvironment=yes
UninstallDisplayIcon={app}\bin\{#MyAppExeName}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}\lib\lynxer"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#PayloadDir}\{#MyAppExeName}"; DestDir: "{app}\bin"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\bin\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"

[Registry]
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; ValueData: "{code:GetUserPath}"; Flags: preservestringtype

[Run]
Filename: "{app}\bin\{#MyAppExeName}"; Parameters: "--version"; Description: "Verify the Lynxer install"; Flags: runhidden nowait postinstall skipifsilent

[Code]
function AppendDir(Current, Dir: string): string;
begin
  if Pos(LowerCase(Dir), LowerCase(Current)) = 0 then
  begin
    if Current <> '' then
      Current := Current + ';';
    Current := Current + Dir;
  end;
  Result := Current;
end;

function GetUserPath(Param: string): string;
var
  Current: string;
begin
  if not RegQueryStringValue(HKCU, 'Environment', 'Path', Current) then
    Current := '';
  Current := AppendDir(Current, ExpandConstant('{app}\bin'));
  Current := AppendDir(Current, ExpandConstant('{app}\lib\lynxer'));
  Result := Current;
end;

procedure RemoveDirFromPath(var Current: string; Dir: string);
begin
  StringChangeEx(Current, ';' + Dir, '', True);
  StringChangeEx(Current, Dir + ';', '', True);
  StringChangeEx(Current, Dir, '', True);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Current: string;
begin
  if CurUninstallStep <> usPostUninstall then
    exit;
  if not RegQueryStringValue(HKCU, 'Environment', 'Path', Current) then
    exit;
  RemoveDirFromPath(Current, ExpandConstant('{app}\bin'));
  RemoveDirFromPath(Current, ExpandConstant('{app}\lib\lynxer'));
  RegWriteExpandStringValue(HKCU, 'Environment', 'Path', Current);
end;

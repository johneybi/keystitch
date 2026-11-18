const
  PublicUninstallKey = 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{41036EA6-3F7A-4803-8AE0-469E5E91EFCC}_is1';
  PublicServiceSHA256 = '51cb03102a757dd17f8aaa2069a5b014347890d3e1ff912ea04b4d8f49993f87';
  LegacyServiceKey = 'SYSTEM\CurrentControlSet\Services\Barrier';
  StartupKey = 'Software\Microsoft\Windows\CurrentVersion\Run';
  StartupName = 'KeyStitch';
  StartAutomatic = 2;
  ServiceStopped = 1;

type
  TServiceStatus = record
    ServiceType, CurrentState, ControlsAccepted, ExitCode,
    ServiceExitCode, CheckPoint, WaitHint: LongWord;
  end;

var
  MigrationFailed: Boolean;

function OpenSCManager(Machine, Database: Integer; Access: LongWord): THandle;
  external 'OpenSCManagerW@advapi32.dll stdcall';
function OpenService(Manager: THandle; Name: String; Access: LongWord): THandle;
  external 'OpenServiceW@advapi32.dll stdcall';
function CloseServiceHandle(Handle: THandle): Boolean;
  external 'CloseServiceHandle@advapi32.dll stdcall';
function QueryServiceStatus(Handle: THandle; var Status: TServiceStatus): Boolean;
  external 'QueryServiceStatus@advapi32.dll stdcall';

function IsPublicAutomaticService(): Boolean;
var
  Location, ImagePath, ExpectedImage: String;
  StartMode: Cardinal;
begin
  Result := False;
  if not RegQueryStringValue(HKLM64, PublicUninstallKey, 'InstallLocation', Location) then
    Exit;
  if not RegQueryStringValue(HKLM64, LegacyServiceKey, 'ImagePath', ImagePath) then
    Exit;
  if not RegQueryDWordValue(HKLM64, LegacyServiceKey, 'Start', StartMode) then
    Exit;
  ExpectedImage := AddBackslash(Location) + 'barrierd.exe';
  if (StartMode <> StartAutomatic) or (CompareText(RemoveQuotes(ImagePath), ExpectedImage) <> 0) then
    Exit;
  if not FileExists(ExpectedImage) then
    Exit;
  { Other Barrier builds can have the same Inno AppId and service name. }
  Result := CompareText(GetSHA256OfFile(ExpectedImage), PublicServiceSHA256) = 0;
end;

function RunServiceCommand(Arguments: String; var ExitCode: Integer): Boolean;
begin
  Result := Exec(ExpandConstant('{sys}\sc.exe'), Arguments, '', SW_HIDE, ewWaitUntilTerminated, ExitCode);
  if Result then
    Log(Format('sc %s returned %d', [Arguments, ExitCode]))
  else
    Log(Format('Could not run sc %s: %d', [Arguments, ExitCode]));
end;

function DisableAndStopBarrier(Service: THandle): Boolean;
var
  Status: TServiceStatus;
  ExitCode, Attempt: Integer;
begin
  Result := False;
  if not RunServiceCommand('config Barrier start= disabled', ExitCode) then
    Exit;
  if ExitCode <> 0 then
    Exit;

  if QueryServiceStatus(Service, Status) and (Status.CurrentState <> ServiceStopped) then
    RunServiceCommand('stop Barrier', ExitCode);
  for Attempt := 1 to 60 do
  begin
    if not QueryServiceStatus(Service, Status) then
      Break;
    if Status.CurrentState = ServiceStopped then
    begin
      Result := True;
      Exit;
    end;
    Sleep(250);
  end;
end;

function TransferPublicStartup(Service: THandle): Boolean;
var
  Status: TServiceStatus;
  WasRunning, HadStartupValue: Boolean;
  PreviousCommand, NewCommand: String;
  ExitCode: Integer;
begin
  Result := False;
  if not QueryServiceStatus(Service, Status) then
    Exit;
  WasRunning := Status.CurrentState <> ServiceStopped;
  HadStartupValue := RegQueryStringValue(HKLM64, StartupKey, StartupName, PreviousCommand);
  NewCommand := AddQuotes(ExpandConstant('{app}\{#MyAppExeName}'));
  if Length(NewCommand) > 260 then
    Exit;
  if not RegWriteStringValue(HKLM64, StartupKey, StartupName, NewCommand) then
    Exit;
  if DisableAndStopBarrier(Service) then
  begin
    Log('Transferred public Barrier service startup to KeyStitch at sign-in.');
    Result := True;
    Exit;
  end;

  if HadStartupValue then
    RegWriteStringValue(HKLM64, StartupKey, StartupName, PreviousCommand)
  else
    RegDeleteValue(HKLM64, StartupKey, StartupName);
  if RunServiceCommand('config Barrier start= auto', ExitCode) and (ExitCode = 0) and WasRunning then
    RunServiceCommand('start Barrier', ExitCode);
end;

procedure CurStepChanged(Step: TSetupStep);
var
  Manager, Service: THandle;
begin
  if Step <> ssPostInstall then
    Exit;
  if not IsPublicAutomaticService() then
    Exit;

  MigrationFailed := True;
  Manager := OpenSCManager(0, 0, 1); { SC_MANAGER_CONNECT }
  if Manager <> 0 then
  begin
    Service := OpenService(Manager, 'Barrier', 4); { SERVICE_QUERY_STATUS }
    if Service <> 0 then
    begin
      MigrationFailed := not TransferPublicStartup(Service);
      CloseServiceHandle(Service);
    end;
    CloseServiceHandle(Manager);
  end;
  if MigrationFailed then
  begin
    Log('KeyStitch setup failed to transfer automatic startup.');
    SuppressibleMsgBox('Could not transfer startup from the previous installation. Check the setup log.',
      mbError, MB_OK, IDOK);
  end;
end;

function GetCustomSetupExitCode(): Integer;
begin
  if MigrationFailed then
    Result := 42
  else
    Result := 0;
end;

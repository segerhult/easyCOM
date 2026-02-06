[Setup]
AppName=Virtual Serial Port (UMDF)
AppVersion=1.0.0.0
DefaultDirName={pf}\VirtualCom
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64

[Files]
Source: "..\build\windows\EasyCOM.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\windows\VirtualComBridge.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\src\windows\driver\virtual_com.inf"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\src\windows\driver\x64\Release\VirtualCom.dll"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{commondesktop}\EasyCOM"; Filename: "{app}\EasyCOM.exe"

[Run]
Filename: "cmd.exe"; Parameters: "/c copy ""{app}\VirtualCom.dll"" ""%SystemRoot%\System32\UMDF\VirtualCom.dll"""; Flags: runhidden
Filename: "cmd.exe"; Parameters: "/c pnputil /add-driver ""{app}\virtual_com.inf"" /install"; Flags: runhidden
Filename: "sc.exe"; Parameters: "create VirtualComBridge binPath= ""{app}\VirtualComBridge.exe {code:GetHost} {code:GetPort}"" start= auto"; Flags: runhidden
Filename: "sc.exe"; Parameters: "start VirtualComBridge"; Flags: runhidden

[Code]
function GetHost(Param: string): string;
var host: string;
begin
  host := '127.0.0.1';
  if InputQuery('TCP Host', 'Enter bridge TCP host:', host) then
    Result := host
  else
    Result := '127.0.0.1';
end;

function GetPort(Param: string): string;
var port: string;
begin
  port := '10000';
  if InputQuery('TCP Port', 'Enter bridge TCP port:', port) then
    Result := port
  else
    Result := '10000';
end;

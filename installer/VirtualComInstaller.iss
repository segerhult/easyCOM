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
var
  Page: TInputQueryWizardPage;

procedure InitializeWizard;
begin
  Page := CreateInputQueryPage(wpWelcome,
    'TCP Bridge Configuration', 'Configure the TCP connection details',
    'Please enter the Host and Port for the TCP bridge.');
  Page.Add('TCP Host:', False);
  Page.Add('TCP Port:', False);
  
  Page.Values[0] := '127.0.0.1';
  Page.Values[1] := '10000';
end;

function GetHost(Param: string): string;
begin
  Result := Page.Values[0];
end;

function GetPort(Param: string): string;
begin
  Result := Page.Values[1];
end;

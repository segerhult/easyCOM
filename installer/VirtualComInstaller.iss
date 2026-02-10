[Setup]
AppName=EasyCOM Suite
AppVersion=1.0.0.0
DefaultDirName={pf}\EasyCOM
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64
OutputBaseFilename=EasyCOM_Setup

[Types]
Name: "full"; Description: "Full Installation"
Name: "client"; Description: "Client Only"
Name: "server"; Description: "Server Only"
Name: "custom"; Description: "Custom Installation"; Flags: iscustom

[Components]
Name: "client"; Description: "EasyCOM Client GUI"; Types: full client
Name: "server"; Description: "Hub Server (GUI)"; Types: full server
Name: "driver"; Description: "Virtual Serial Port Driver"; Types: full

[Files]
; Client
Source: "..\build\windows\EasyCOM.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: client

; Server
Source: "..\build\windows\hub_server_gui.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: server

; Driver
Source: "..\build\windows\VirtualComBridge.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: driver
Source: "..\src\windows\driver\virtual_com.inf"; DestDir: "{app}"; Flags: ignoreversion; Components: driver
Source: "..\src\windows\driver\x64\Release\VirtualCom.dll"; DestDir: "{app}"; Flags: ignoreversion; Components: driver

[Icons]
Name: "{commondesktop}\EasyCOM Client"; Filename: "{app}\EasyCOM.exe"; Components: client
Name: "{commondesktop}\EasyCOM Hub Server"; Filename: "{app}\hub_server_gui.exe"; Components: server

[Run]
; Driver Installation (Only if driver component selected)
Filename: "cmd.exe"; Parameters: "/c copy ""{app}\VirtualCom.dll"" ""%SystemRoot%\System32\UMDF\VirtualCom.dll"""; Flags: runhidden; Components: driver
Filename: "cmd.exe"; Parameters: "/c pnputil /add-driver ""{app}\virtual_com.inf"" /install"; Flags: runhidden; Components: driver

; Bridge Service (Only if driver component selected)
; We use the input page values for the bridge configuration
Filename: "sc.exe"; Parameters: "create VirtualComBridge binPath= ""{app}\VirtualComBridge.exe {code:GetHost} {code:GetPort}"" start= auto"; Flags: runhidden; Components: driver
Filename: "sc.exe"; Parameters: "start VirtualComBridge"; Flags: runhidden; Components: driver

[Code]
var
  Page: TInputQueryWizardPage;

procedure InitializeWizard;
begin
  // Create the page but only show it if the driver component is selected?
  // Inno Setup doesn't easily hide pages based on components dynamically selected in the wizard 
  // without complex code. For simplicity, we show it, but it only affects the 'driver' component.
  
  Page := CreateInputQueryPage(wpSelectComponents,
    'Virtual Port Configuration', 'Configure the Bridge Connection',
    'If you are installing the Virtual Driver, specify the TCP Server to connect to:');
  Page.Add('TCP Host:', False);
  Page.Add('TCP Port:', False);
  
  Page.Values[0] := '127.0.0.1';
  Page.Values[1] := '10000';
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  // Skip the configuration page if the Driver component is NOT selected.
  // Note: IsComponentSelected is not available directly in ShouldSkipPage in older Inno versions 
  // or requires the wizard to have moved past the component selection page.
  // Since we placed the page AFTER wpSelectComponents, we can check.
  
  if PageID = Page.ID then
  begin
    // If 'driver' component is NOT selected, skip this page.
    if not IsComponentSelected('driver') then
      Result := True
    else
      Result := False;
  end;
end;

function GetHost(Param: string): string;
begin
  Result := Page.Values[0];
end;

function GetPort(Param: string): string;
begin
  Result := Page.Values[1];
end;

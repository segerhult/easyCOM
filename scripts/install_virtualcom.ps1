Param(
  [string]$Host = "127.0.0.1",
  [int]$Port = 10000,
  [switch]$EnableTestSigning,
  [string]$DriverBinDir = "$PSScriptRoot\x64\Release\VirtualCom"
)

$admin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole] "Administrator")
if (-not $admin) {
  Write-Error "Run this script as Administrator."
  exit 1
}

if ($EnableTestSigning) {
  & bcdedit /set testsigning on
  Write-Host "Test Signing enabled. Reboot required."
}

$umdfDir = Join-Path $env:SystemRoot "System32\UMDF"
if (-not (Test-Path $umdfDir)) { New-Item -ItemType Directory -Path $umdfDir | Out-Null }

$driverDll = Join-Path $DriverBinDir "VirtualCom.dll"
if (-not (Test-Path $driverDll)) {
  Write-Error "Driver binary not found at $driverDll. Build the UMDF driver in Visual Studio and try again."
  exit 1
}

Copy-Item $driverDll -Destination (Join-Path $umdfDir "VirtualCom.dll") -Force
Write-Host "Copied VirtualCom.dll to $umdfDir"

$infPath = Join-Path (Split-Path $PSScriptRoot -Parent) "src\windows\driver\virtual_com.inf"
if (-not (Test-Path $infPath)) {
  Write-Error "INF not found at $infPath"
  exit 1
}

& pnputil /add-driver "$infPath" /install
Write-Host "Driver package added."
Write-Host "If device is not created automatically, open Device Manager and use 'Add legacy hardware' to install 'Virtual Serial Port (UMDF)'."

$bridgeExe = Join-Path (Split-Path $PSScriptRoot -Parent) "build\windows\VirtualComBridge.exe"
if (-not (Test-Path $bridgeExe)) {
  Write-Error "Bridge executable not found at $bridgeExe. Run build_windows.bat first."
  exit 1
}

$binPath = "`"$bridgeExe`" $Host $Port"
& sc.exe query VirtualComBridge | Out-Null
if ($LASTEXITCODE -eq 0) {
  & sc.exe stop VirtualComBridge | Out-Null
  & sc.exe delete VirtualComBridge | Out-Null
}
& sc.exe create VirtualComBridge binPath= "$binPath" start= auto
& sc.exe start VirtualComBridge
Write-Host "VirtualComBridge service installed and started (Host=$Host, Port=$Port)."

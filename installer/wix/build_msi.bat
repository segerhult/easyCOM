@echo off
setlocal
set WIXBIN="C:\Program Files (x86)\WiX Toolset v3.11\bin"
if not exist installer\wix\out mkdir installer\wix\out
%WIXBIN%\candle.exe -out installer\wix\out\VirtualCom.wixobj installer\wix\VirtualCom.wxs
%WIXBIN%\light.exe -out installer\wix\out\VirtualCom.msi installer\wix\out\VirtualCom.wixobj -ext WixUIExtension
echo MSI built at installer\wix\out\VirtualCom.msi
endlocal

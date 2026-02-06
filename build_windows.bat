@echo off
echo Building Windows Tools...

if not exist build\windows mkdir build\windows

echo Building Push Client...
cl /Fe:build\windows\push_client.exe src\windows\client\push_client.c ws2_32.lib user32.lib

echo Building Hub Server (CLI)...
cl /Fe:build\windows\hub_server_cli.exe src\windows\server\hub_server_cli.c ws2_32.lib user32.lib

echo Building Hub Server (GUI)...
cl /Fe:build\windows\hub_server_gui.exe src\windows\server\hub_server_gui.c ws2_32.lib user32.lib gdi32.lib

echo Building Unified EasyCOM GUI...
cl /Fe:build\windows\EasyCOM.exe src\windows\easy_com_gui.c ws2_32.lib user32.lib gdi32.lib

echo Building Virtual COM Bridge Daemon...
cl /Fe:build\windows\VirtualComBridge.exe src\windows\driver\virtual_com_bridge.c ws2_32.lib

echo Done!
echo Binaries are in the build\windows\ folder.
pause

# Serial & TCP Port Forwarder

This project provides tools to forward TCP ports and Serial ports over a network.

## Architecture

This system uses a **Reverse Connection** model (Push Client), which is useful when the device (e.g., Arduino) is behind a firewall or NAT.

1.  **Push Client (Device Side)**: Runs on the PC with the physical serial port. It connects OUT to the Hub Server and says "I am COM3". It also supports **RFC 2217**, meaning the server can dynamically change the Baud Rate.
2.  **Hub Server (Control Side)**: Runs on your main PC. It accepts connections from multiple devices. You can view the list of connected devices and "Map" them to a local TCP port to use them.

## Project Structure

```text
├── Makefile               # Build script for Linux/macOS
├── build_windows.bat      # Build script for Windows
├── build/                 # Compiled binaries
│   ├── linux/             # Linux/macOS Binaries
│   └── windows/           # Windows Binaries
└── src/
    ├── linux/             # Linux/macOS Source
    │   ├── client/        # Push Client
    │   ├── server/        # Hub Server (CLI & GUI)
    │   └── legacy/        # Simple Forwarders
    └── windows/           # Windows Source
        ├── client/        # Push Client
        ├── server/        # Hub Server
        └── driver/        # Virtual COM Driver Skeleton
```

---

## 🪟 Windows Usage

### 1. Build
Run `build_windows.bat` in a Visual Studio Developer Command Prompt.
This will create:
*   `build\windows\push_client.exe`
*   `build\windows\hub_server_cli.exe`
*   `build\windows\hub_server_gui.exe`

### 2. Run the Hub Server (On your Main PC)
You can choose either the CLI or GUI version.

**Option A: GUI**
```cmd
build\windows\hub_server_gui.exe
```
*   A window will open listening on Port 9000.
*   When a client connects, it appears in the list.
*   Select a client and click **Map Selected to Port...**.
*   Confirm mapping (e.g., to port 10000).
*   Now you can connect your application to `localhost:10000`.

**Option B: CLI**
```cmd
build\windows\hub_server_cli.exe
```
*   Listens on Port 9000 by default.
*   Or specify port: `build\windows\hub_server_cli.exe 8080`
*   Type `list` to see devices.
*   Type `map <ID> <PORT>` to map a device.

### 3. Run the Push Client (On the Device PC)
Run this on the machine that has the Arduino/Serial Device.

**Quick Start (Defaults)**
Connects `COM3` (115200 baud) to `127.0.0.1:9000`.
```cmd
build\windows\push_client.exe
```

**Quick Start (Specific COM Port)**
Connects `COM1` (115200 baud) to `127.0.0.1:9000`.
```cmd
build\windows\push_client.exe COM1
```

**Full Configuration**
```cmd
build\windows\push_client.exe <HUB_IP> <HUB_PORT> <LOCAL_COM> <BAUD>
```
*Example:* Connect `COM3` to Hub at `192.168.1.50:9000`.
```cmd
build\windows\push_client.exe 192.168.1.50 9000 COM3 115200
```

### 4. Connect Arduino IDE (On the Server PC)
To allow the Arduino IDE to control the remote port (including setting Baud Rate), you need a Virtual COM Port driver.

#### Option A: Use Existing Tools (Recommended)
You don't need to build the driver yourself. We recommend:

1.  **HW VSP3 (Free Version)**:
    *   This tool creates a Virtual COM Port that connects directly to a TCP IP/Port.
    *   **Setup**: Install HW VSP3 -> Create COM5 -> Point to `127.0.0.1` Port `9000`.
    *   **Benefit**: You don't even need our client app on the server side; HW VSP3 talks directly to the Hub!

2.  **com0com (Open Source)**:
    *   Creates virtual pairs (e.g., `CNCA0` <-> `CNCB0`).
    *   *Note*: Requires "Test Signing Mode" on Windows 10/11.

#### Option B: Build Your Own Driver (Advanced)
If you really want to build the custom driver source included in this repo (`src/windows/driver/virtual_com.c`):
*   👉 **[Read the Step-by-Step Build Guide](DRIVER_BUILD.md)**
*   Requires Visual Studio 2019+ and Windows Driver Kit (WDK).

---

## 🐳 Docker Build

You can use Docker to build binaries for both Linux and Windows (using MinGW cross-compilation).

### 1. Build the Image
```bash
docker build -t serial-forwarder-builder .
```

### 2. Extract Binaries
Run the container to see where the files are, and then copy them out.
```bash
# Create a dummy container
docker create --name dummy serial-forwarder-builder

# Copy files to your local 'dist' folder
mkdir dist
docker cp dummy:/app/build/. dist/

# Clean up
docker rm dummy
```
Now check the `dist/` folder. You will find:
*   `dist/linux/push_client`
*   `dist/linux/hub_server_cli`
*   `dist/linux/hub_server_gui`
*   `dist/windows/push_client.exe`
*   `dist/windows/hub_server_cli.exe`
*   `dist/windows/hub_server_gui.exe`

---

## 🐧 Linux / macOS Usage

### 1. Build
Run `make` to build the tools.
```bash
make
```
This creates:
*   `build/linux/push_client`
*   `build/linux/hub_server_cli`
*   `build/linux/hub_server_gui` (Only if GTK+3 is installed)

**Prerequisites for GUI:**
*   **macOS**: `brew install gtk+3 pkg-config`
*   **Linux**: `sudo apt install libgtk-3-dev pkg-config`

### 2. Run the Hub Server
**Option A: GUI (Requires GTK+3)**
```bash
./build/linux/hub_server_gui
```
*   Displays connected clients in a window.
*   Click **Map Selected to Port...** to bridge a device.

**Option B: CLI**
```bash
./build/linux/hub_server_cli
```
*   Listens on Port 9000 by default.
*   Type `list` and `map <id> <port>`.

### 3. Run the Push Client
**Quick Start (Defaults)**
Connects `/dev/ttyUSB0` (115200 baud) to `127.0.0.1:9000`.
```bash
./build/linux/push_client
```

**Quick Start (Specific Device)**
Connects `/dev/ttyACM0` (115200 baud) to `127.0.0.1:9000`.
```bash
./build/linux/push_client /dev/ttyACM0
```

**Full Configuration**
```bash
./build/linux/push_client <HUB_IP> <HUB_PORT> <DEVICE_PATH> <BAUD>
```
*Example:*
```bash
./build/linux/push_client 192.168.1.50 9000 /dev/ttyUSB1 9600
```

# 🛠️ How to Build the Custom Virtual COM Driver

This guide explains how to build the source code in `src/windows/driver/virtual_com.c` into a functional Windows Driver.

## ⚠️ Prerequisites

Building Windows Drivers is significantly more complex than standard applications. You cannot use GCC/MinGW. You must use Microsoft tools.

1.  **Visual Studio 2019 or 2022** (Community Edition is free).
    *   During installation, select the **"Desktop development with C++"** workload.
2.  **Windows Driver Kit (WDK)**.
    *   Download from Microsoft: [Download the WDK](https://learn.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk).
    *   You must install the **WDK Visual Studio extension** after installing the WDK.

---

## 🏗️ Step-by-Step Build Instructions

### 1. Create the Project in Visual Studio
1.  Open Visual Studio.
2.  Click **Create a new project**.
3.  Search for **"User Mode Driver"**.
4.  Select **"User Mode Driver, Empty (UMDF 2)"**.
5.  Name the project `VirtualCom` and click **Create**.

### 2. Add the Source Files
1.  In the **Solution Explorer** (right panel), right-click on **Source Files**.
2.  Select **Add > Existing Item...**.
3.  Navigate to this repository and select `src/windows/driver/virtual_com.c`.
4.  Right-click on **Driver Files** (or just the project root).
5.  Select **Add > Existing Item...**.
6.  Select `src/windows/driver/virtual_com.inf`.

### 3. Configure Project Settings
1.  Right-click the **Project** (not Solution) and select **Properties**.
2.  Set **Configuration** to `All Configurations` and **Platform** to `x64`.
3.  Go to **Configuration Properties > Driver Settings > General**.
    *   Target OS Version: `Windows 10 or higher`.
    *   Target Platform: `Desktop`.
4.  Go to **Inf2Cat**.
    *   Set **Use Local Time** to `Yes` (Fixes some timestamp errors).
5.  Click **OK**.

### 4. Build the Driver
1.  Set the build target (top toolbar) to **Release** and **x64**.
2.  Click **Build > Build Solution**.
3.  If successful, Visual Studio will generate the driver files in `x64\Release\VirtualCom\`:
    *   `VirtualCom.dll` (The driver binary)
    *   `VirtualCom.inf` (The installation script)
    *   `VirtualCom.cat` (The security catalog - unsigned)

---

## 📦 How to Install (Test Signing)

Since you built this driver yourself, it is **unsigned**. Windows 10/11 will refuse to load it unless you enable Test Signing.

### 1. Enable Test Mode
Open a Command Prompt as **Administrator** and run:
```cmd
bcdedit /set testsigning on
```
**Restart your computer.** You should see "Test Mode" in the bottom right corner of your desktop.

### 2. Install the Driver
1.  Open **Device Manager**.
2.  Click **Action > Add legacy hardware**.
3.  Click **Next**.
4.  Select **"Install the hardware that I manually select from a list"**.
5.  Select **Ports (COM & LPT)** -> Next.
6.  Click **Have Disk...**.
7.  Browse to your build folder (`x64\Release\VirtualCom\`) and select `VirtualCom.inf`.
8.  Select **"Virtual Serial Port (UMDF)"** and click Next -> Finish.

You should now have a new COM port (e.g., COM3) in Device Manager!

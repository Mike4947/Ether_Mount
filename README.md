# EtherMount v0.02

Windows application that mounts a remote VPS (via SFTP) as a native network drive in File Explorer.

**Source code & documentation:** [https://github.com/Mike4947/Ether_Mount](https://github.com/Mike4947/Ether_Mount)

## Credential Security

**Your VPS credentials are never stored in the source code or committed to Git.** They are:

- Encrypted with **Windows DPAPI** (CryptProtectData)
- Stored in `%APPDATA%\EtherMount\credentials.dat` (outside the repo)
- Safe to commit your code — no secrets in the repository

## Architecture (v0.02)

| Pillar | Component | Description |
|--------|-----------|-------------|
| **Security** | `CredentialManager` | DPAPI-encrypted storage of Host, Port, Username, Password in `%APPDATA%/EtherMount/credentials.dat` |
| **GUI** | `MainWindow` + `TrayApplication` + `SettingsDialog` | Main window with toolbar (Settings, Mount VPS, Unmount VPS, Exit); system tray; inline settings and documentation |
| **VFS** | `EtherMountFS` | WinFSP skeleton: mounts `\\EtherMount\VPS` as drive Z:; SFTP mapping in future versions |

## Requirements

- **Windows 10/11**
- **C++17**
- **CMake** 3.16+
- **vcpkg** (for libssh2, Qt6)
- **WinFSP** (install separately)

## Dependencies

### vcpkg (libssh2, Qt6)

```powershell
vcpkg install
```

### WinFSP

WinFSP is not in vcpkg. Install via:

```powershell
winget install WinFsp.WinFsp
```

Or download from [WinFSP releases](https://github.com/winfsp/winfsp/releases). Choose the **Developer** option to install headers and libs.

Default install path: `C:\Program Files (x86)\WinFsp`. If different, set `WINFSP_ROOT` before configuring:

```powershell
$env:WINFSP_ROOT = "C:\Path\To\WinFsp"
```

## Building

```powershell
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

## Running

```powershell
.\build\Release\EtherMount.exe
```

The app opens a **main window** with settings and documentation, plus a **system tray icon** for quick access.

## Usage

1. **Main window** opens on startup with toolbar: **Settings** | **Mount VPS** | **Unmount VPS** | **Exit**
2. Enter **Host/IP**, **Port** (default 22), **Username**, **Password** in the settings form
3. Click **Save credentials** — encrypted with DPAPI and stored locally
4. Click **Mount VPS** — drive Z: appears (UNC: `\\EtherMount\VPS`)
5. Click **Unmount VPS** — drive is unmounted
6. **Right-click tray icon** for the same menu; **double-click** to restore the main window

## Project Structure

```
EtherMount/
├── CMakeLists.txt
├── vcpkg.json
├── README.md
├── include/EtherMount/
│   ├── CredentialManager.hpp   # Pillar 1: Secure credential storage
│   ├── SftpClient.hpp          # libssh2 SFTP client (from v0.01)
│   ├── MainWindow.hpp          # Pillar 2: Main window (toolbar, settings, docs)
│   ├── SettingsDialog.hpp     # Pillar 2: Qt Settings dialog
│   ├── TrayApplication.hpp     # Pillar 2: System tray + menu
│   └── EtherMountFS.hpp        # Pillar 3: WinFSP launcher
└── src/
    ├── main.cpp                # Entry point, wires all pillars
    ├── CredentialManager.cpp
    ├── SftpClient.cpp
    ├── SettingsDialog.cpp
    ├── MainWindow.cpp
    ├── TrayApplication.cpp
    └── EtherMountFS.cpp
```

## How the Three Pillars Wire Together

```
main.cpp
    |
    v
TrayApplication (QApplication)
    |
    +-- CredentialManager
    |       save()  -> CryptProtectData -> %APPDATA%/EtherMount/credentials.dat
    |       load()  -> CryptUnprotectData -> VpsCredentials
    |
    +-- SettingsDialog
    |       loadFromStorage() -> CredentialManager::load()
    |       saveToStorage()   -> CredentialManager::save()
    |
    +-- EtherMountFS
    |       mount(creds)   -> FspLoad, FspFileSystemCreate, SetMountPoint(Z:), StartDispatcher
    |       unmount()      -> FspFileSystemStopDispatcher, FspFileSystemDelete
    |
    +-- Tray menu actions
            Settings   -> show SettingsDialog
            Mount VPS  -> credentialManager_.load() -> fileSystem_->mount(creds)
            Unmount VPS-> fileSystem_->unmount()
            Exit       -> unmount if needed, quit()
```

## v0.02 Limitations

- **EtherMountFS** is a skeleton: Z: mounts and shows an empty root. SFTP read/write is not yet implemented.
- Credentials are stored per-user (DPAPI user context).
- Single mount point (Z:) — configurable drive letter in future.

## Next Steps (Future Versions)

- Map WinFSP operations (Read, Write, ReadDirectory, etc.) to SftpClient
- Configurable drive letter
- Optional key-based authentication

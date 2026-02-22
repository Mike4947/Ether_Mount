# EtherMount Installer Wizard

This folder contains the **Inno Setup** wizard script to create a single-file installer for EtherMount. The installer bundles the application and all dependencies (Qt, libssh2, OpenSSL, etc.) so end users do not need to install them manually.

## Prerequisites

1. **Inno Setup 6** – Download from [jrsoftware.org/isdl.php](https://jrsoftware.org/isdl.php) (free)
2. **Built EtherMount** – The main app must be built first

## Build the Installer

### Step 1: Build EtherMount

From the project root:

```powershell
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

### Step 2: Package Files

Run the package script to copy the built app and all DLLs into `installer/payload/`:

```powershell
cd installer
.\package.ps1
```

### Step 3: Compile the Installer

Open `EtherMount.iss` in Inno Setup Compiler, or run from command line:

```powershell
"C:\Users\mlebl\AppData\Local\Programs\Inno Setup 6\ISCC.exe" EtherMount.iss
```

The installer will be created at `installer/output/EtherMount-Setup-0.0.3.exe`. Add `installer/output/` and `installer/payload/` to `.gitignore` if you do not want to commit built artifacts.

## What the Installer Does

- **Wizard UI**: Welcome → Choose installation directory → Install → Finish
- **Bundles**: EtherMount.exe + Qt runtime + libssh2 + OpenSSL + all required DLLs
- **Shortcuts**: Start Menu, optional Desktop and Quick Launch
- **Uninstaller**: Full uninstall support
- **WinFSP**: If WinFSP is not detected, the Finished page reminds the user to install it (WinFSP is a system driver and must be installed separately)

## Customization

Edit `EtherMount.iss` to:

- Change `MyAppVersion`, `MyAppPublisher`, `MyAppURL`
- Add a custom icon: set `SetupIconFile` to your `.ico` path
- Add a license page: uncomment `LicenseFile` in `[Setup]`
- Add more `[Tasks]` or `[Run]` entries

## One-Line Build (from project root)

```powershell
cmake --build build --config Release; .\installer\package.ps1; & "C:\Users\mlebl\AppData\Local\Programs\Inno Setup 6\ISCC.exe" installer\EtherMount.iss
```

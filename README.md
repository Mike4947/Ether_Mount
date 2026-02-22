# EtherMount v0.01

Proof-of-concept C++ console application that establishes a secure SSH/SFTP connection to a remote server and lists directory contents.

## Requirements

- **C++17** or later
- **libssh2** (SSH/SFTP client library)
- **CMake** 3.16+
- **vcpkg** (recommended for dependency management on Windows)

## Dependencies

### Option A: vcpkg (Recommended for Windows)

1. Install [vcpkg](https://vcpkg.io/en/docs/getting-started.html) if not already installed.

2. From the project root, run:
   ```powershell
   vcpkg install
   ```
   This uses the `vcpkg.json` manifest to install libssh2.

3. Configure CMake with the vcpkg toolchain:
   ```powershell
   cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=[path-to-vcpkg]/scripts/buildsystems/vcpkg.cmake
   ```
   Or set `VCPKG_ROOT` and use:
   ```powershell
   cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
   ```

### Option B: Visual Studio with vcpkg

1. Integrate vcpkg with Visual Studio:
   ```powershell
   vcpkg integrate install
   ```

2. Open the project folder in Visual Studio (File → Open → Folder).

3. Visual Studio will detect `CMakeLists.txt` and `vcpkg.json` and configure vcpkg automatically.

### Option C: Manual libssh2 Installation

1. Build or obtain libssh2 for your platform (e.g., from [libssh2 releases](https://github.com/libssh2/libssh2/releases) or your package manager).

2. Point CMake to the installation:
   ```powershell
   cmake -B build -S . -DCMAKE_PREFIX_PATH="C:/path/to/libssh2"
   ```
   Ensure libssh2 provides a CMake config (e.g., via vcpkg or a custom `Findlibssh2.cmake`).

## Building

```powershell
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

## Running

```powershell
.\build\Debug\EtherMount.exe
```

Or from the build directory:
```powershell
.\EtherMount.exe
```

## Configuration (v0.01)

The following are hardcoded in `src/main.cpp` for this proof-of-concept:

| Variable        | Default Value   | Description                    |
|----------------|-----------------|--------------------------------|
| `DEFAULT_HOST` | `198.51.100.0`  | Mock server IP (TEST-NET range) |
| `DEFAULT_PORT` | `22`            | SSH port                       |
| `DEFAULT_USERNAME` | `testuser`  | SSH username                   |
| `DEFAULT_PASSWORD` | `testpass`  | SSH password                   |
| `DEFAULT_REMOTE_PATH` | `/var/www/` | Remote directory to list |

**Note:** `198.51.100.0` is a documentation-only address (TEST-NET-2 per RFC 5737) and will not resolve to a real host. Replace with your actual VPS IP to test against a real server.

## Project Structure

```
EtherMount/
├── CMakeLists.txt
├── vcpkg.json
├── README.md
├── include/
│   └── EtherMount/
│       └── SftpClient.hpp      # Modular SFTP client class
└── src/
    ├── main.cpp                # Console entry point
    └── SftpClient.cpp          # SftpClient implementation
```

The `SftpClient` class is designed for reuse when migrating to a background service or GUI application.

## Next Steps (Future Versions)

- Command-line or config-file based credentials
- WinFSP integration for virtual drive mounting
- Background service architecture

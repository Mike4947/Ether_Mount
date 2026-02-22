cmake_minimum_required(VERSION 3.16)
project(EtherMount VERSION 0.0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Dependencies ---
# IMPORTANT: Configure with vcpkg toolchain first:
#   cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake
find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(libssh2 CONFIG REQUIRED)

# WinFSP: Not in vcpkg. Requires full SDK (headers + lib).
# - Run winfsp-X.X.msi from https://github.com/winfsp/winfsp/releases with "Developer" option.
# - winget install WinFsp.WinFsp only installs the runtime (no headers/lib).
# - Set WINFSP_ROOT if installed to non-default location.
# - If SDK not found, builds without WinFSP (mount will show install instructions).
if(WIN32)
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
        set(WINFSP_ARCH "x64")
    else()
        set(WINFSP_ARCH "x86")
    endif()

    set(ETHERMOUNT_USE_WINFSP 0)
    if(DEFINED ENV{WINFSP_ROOT})
        set(WINFSP_ROOT "$ENV{WINFSP_ROOT}")
        if(EXISTS "${WINFSP_ROOT}/inc/winfsp/winfsp.h" AND EXISTS "${WINFSP_ROOT}/lib/winfsp-${WINFSP_ARCH}.lib")
            set(WINFSP_INC "${WINFSP_ROOT}/inc")
            set(WINFSP_LIB "${WINFSP_ROOT}/lib/winfsp-${WINFSP_ARCH}.lib")
            set(ETHERMOUNT_USE_WINFSP 1)
        endif()
    elseif(EXISTS "C:/Program Files/WinFsp/inc/winfsp/winfsp.h" AND EXISTS "C:/Program Files/WinFsp/lib/winfsp-${WINFSP_ARCH}.lib")
        set(WINFSP_ROOT "C:/Program Files/WinFsp")
        set(WINFSP_INC "${WINFSP_ROOT}/inc")
        set(WINFSP_LIB "${WINFSP_ROOT}/lib/winfsp-${WINFSP_ARCH}.lib")
        set(ETHERMOUNT_USE_WINFSP 1)
    elseif(EXISTS "C:/Program Files (x86)/WinFsp/inc/winfsp/winfsp.h" AND EXISTS "C:/Program Files (x86)/WinFsp/lib/winfsp-${WINFSP_ARCH}.lib")
        set(WINFSP_ROOT "C:/Program Files (x86)/WinFsp")
        set(WINFSP_INC "${WINFSP_ROOT}/inc")
        set(WINFSP_LIB "${WINFSP_ROOT}/lib/winfsp-${WINFSP_ARCH}.lib")
        set(ETHERMOUNT_USE_WINFSP 1)
    endif()

    if(NOT ETHERMOUNT_USE_WINFSP)
        message(STATUS "WinFSP SDK not found - building without VFS mount. "
            "Install: winfsp-X.X.msi from https://github.com/winfsp/winfsp/releases with Developer option.")
    endif()
endif()

# --- Executable ---
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
add_executable(EtherMount WIN32
    src/main.cpp
    src/SftpClient.cpp
    src/CredentialManager.cpp
    src/SettingsDialog.cpp
    src/TrayApplication.cpp
    src/MainWindow.cpp
    src/EtherMountFS.cpp
    include/EtherMount/SettingsDialog.hpp
    include/EtherMount/TrayApplication.hpp
    include/EtherMount/MainWindow.hpp
)

target_include_directories(EtherMount PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    $<$<BOOL:${ETHERMOUNT_USE_WINFSP}>:${WINFSP_INC}>
)
target_compile_definitions(EtherMount PRIVATE
    ETHERMOUNT_USE_WINFSP=${ETHERMOUNT_USE_WINFSP}
)

target_link_libraries(EtherMount PRIVATE
    Qt6::Widgets
    libssh2::libssh2
)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(EtherMount PRIVATE
        ws2_32
        Crypt32
        Shell32
    )
    if(ETHERMOUNT_USE_WINFSP)
        target_link_libraries(EtherMount PRIVATE "${WINFSP_LIB}")
        set_property(TARGET EtherMount PROPERTY LINK_FLAGS "/DELAYLOAD:winfsp-${WINFSP_ARCH}.dll")
    endif()

    # Deploy Qt runtime (DLLs + platform plugins) next to the executable
    set(WINDEPLOYQT "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/tools/Qt6/bin/windeployqt.exe")
    if(EXISTS "${WINDEPLOYQT}")
        add_custom_command(TARGET EtherMount POST_BUILD
            COMMAND "${WINDEPLOYQT}" --release --no-translations $<TARGET_FILE:EtherMount>
            COMMENT "Deploying Qt runtime..."
        )
    endif()
endif()

cmake_minimum_required(VERSION 3.16)
project(EtherMount VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Dependencies ---
# Option 1: vcpkg (recommended)
#   Set CMAKE_TOOLCHAIN_FILE to vcpkg's toolchain, or use manifest mode:
#   vcpkg install (from project root with vcpkg.json)
#
# Option 2: Manual
#   set(LIBSSH2_ROOT "C:/path/to/libssh2")
#   find_path(LIBSSH2_INCLUDE_DIR libssh2.h HINTS ${LIBSSH2_ROOT}/include)
#   find_library(LIBSSH2_LIBRARY NAMES ssh2 libssh2 HINTS ${LIBSSH2_ROOT}/lib)
#
find_package(libssh2 CONFIG REQUIRED)

# --- Executable ---
add_executable(EtherMount
    src/main.cpp
    src/SftpClient.cpp
)

target_include_directories(EtherMount PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(EtherMount PRIVATE
    libssh2::libssh2
)

# Platform-specific libraries (Winsock on Windows)
if(WIN32)
    target_link_libraries(EtherMount PRIVATE ws2_32)
endif()

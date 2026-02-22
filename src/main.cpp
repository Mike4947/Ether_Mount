/**
 * EtherMount v0.01 - Proof of Concept
 *
 * Establishes a secure SSH/SFTP connection to a remote server and lists
 * the contents of a directory to prove the connection works.
 */

#include "EtherMount/SftpClient.hpp"

#include <iostream>
#include <string>

namespace {

constexpr const char* DEFAULT_HOST = "198.51.100.0";
constexpr std::uint16_t DEFAULT_PORT = 22;
constexpr const char* DEFAULT_USERNAME = "testuser";
constexpr const char* DEFAULT_PASSWORD = "testpass";
constexpr const char* DEFAULT_REMOTE_PATH = "/var/www/";

const char* resultToString(EtherMount::Result r) {
    switch (r) {
        case EtherMount::Result::Success:
            return "Success";
        case EtherMount::Result::InitFailed:
            return "Initialization failed";
        case EtherMount::Result::SocketCreateFailed:
            return "Failed to create socket";
        case EtherMount::Result::ConnectFailed:
            return "Failed to connect to server";
        case EtherMount::Result::SessionInitFailed:
            return "Failed to initialize SSH session";
        case EtherMount::Result::HandshakeFailed:
            return "SSH handshake failed";
        case EtherMount::Result::AuthFailed:
            return "Authentication failed";
        case EtherMount::Result::SftpInitFailed:
            return "Failed to initialize SFTP session";
        case EtherMount::Result::OpenDirFailed:
            return "Failed to open remote directory";
        case EtherMount::Result::ReadDirFailed:
            return "Failed to read directory contents";
        default:
            return "Unknown error";
    }
}

} // namespace

int main() {
    std::cout << "=== EtherMount v0.01 - SFTP Connection Test ===\n\n";

    EtherMount::SftpClient client;

    // Step 1: Initialize
    std::cout << "[1/5] Initializing libssh2 and networking... ";
    auto result = client.initialize();
    if (result != EtherMount::Result::Success) {
        std::cerr << "FAILED\n  Error: " << resultToString(result) << "\n";
        return 1;
    }
    std::cout << "OK\n";

    // Step 2: Connect and authenticate
    std::cout << "[2/5] Connecting to " << DEFAULT_HOST << ":" << DEFAULT_PORT
              << " and authenticating... ";
    result = client.connect(DEFAULT_HOST, DEFAULT_PORT, DEFAULT_USERNAME,
                            DEFAULT_PASSWORD);
    if (result != EtherMount::Result::Success) {
        std::cerr << "FAILED\n  Error: " << resultToString(result);
        if (!client.getLastError().empty()) {
            std::cerr << "\n  libssh2: " << client.getLastError();
        }
        std::cerr << "\n";
        client.disconnect();
        return 1;
    }
    std::cout << "OK\n";

    // Step 3: Initialize SFTP
    std::cout << "[3/5] Initializing SFTP session... ";
    result = client.initSftp();
    if (result != EtherMount::Result::Success) {
        std::cerr << "FAILED\n  Error: " << resultToString(result) << "\n";
        client.disconnect();
        return 1;
    }
    std::cout << "OK\n";

    // Step 4: List directory
    std::cout << "[4/5] Listing contents of " << DEFAULT_REMOTE_PATH << "... ";
    std::vector<EtherMount::DirEntry> entries;
    result = client.listDirectory(DEFAULT_REMOTE_PATH, entries);
    if (result != EtherMount::Result::Success) {
        std::cerr << "FAILED\n  Error: " << resultToString(result);
        if (!client.getLastError().empty()) {
            std::cerr << "\n  libssh2: " << client.getLastError();
        }
        std::cerr << "\n";
        client.disconnect();
        return 1;
    }
    std::cout << "OK\n";

    // Step 5: Print results and disconnect
    std::cout << "[5/5] Disconnecting... ";
    client.disconnect();
    std::cout << "OK\n\n";

    // Output directory listing
    std::cout << "--- Directory listing for " << DEFAULT_REMOTE_PATH
              << " ---\n";
    if (entries.empty()) {
        std::cout << "(empty directory)\n";
    } else {
        for (const auto& e : entries) {
            std::cout << "  " << (e.is_directory ? "[DIR] " : "[FILE] ")
                      << e.name;
            if (e.size.has_value()) {
                std::cout << " (" << *e.size << " bytes)";
            }
            std::cout << "\n";
        }
    }
    std::cout << "--- End of listing ---\n";

    std::cout << "\nEtherMount v0.01 proof-of-concept completed successfully.\n";
    return 0;
}

#pragma once

#include <libssh2.h>
#include <libssh2_sftp.h>

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace EtherMount {

/// Result of a directory listing entry.
struct DirEntry {
    std::string name;
    bool is_directory;
    std::optional<uint64_t> size;
};

/// Result type for operations that can fail.
enum class Result {
    Success,
    InitFailed,
    SocketCreateFailed,
    ConnectFailed,
    SessionInitFailed,
    HandshakeFailed,
    AuthFailed,
    SftpInitFailed,
    OpenDirFailed,
    ReadDirFailed,
};

/// Modular SFTP client for SSH/SFTP operations.
/// Designed for reuse in console apps and future background services.
class SftpClient {
public:
    SftpClient();
    ~SftpClient();

    // Non-copyable, non-movable
    SftpClient(const SftpClient&) = delete;
    SftpClient& operator=(const SftpClient&) = delete;
    SftpClient(SftpClient&&) = delete;
    SftpClient& operator=(SftpClient&&) = delete;

    /// Initialize libssh2 and platform networking (e.g. Winsock on Windows).
    /// Must be called before connect.
    Result initialize();

    /// Connect to remote host and authenticate.
    /// \param host IP address or hostname
    /// \param port SSH port (typically 22)
    /// \param username SSH username
    /// \param password SSH password
    Result connect(const std::string& host, std::uint16_t port,
                   const std::string& username, const std::string& password);

    /// Initialize SFTP subsystem over the established SSH channel.
    /// Must be called after connect.
    Result initSftp();

    /// List contents of a remote directory.
    /// \param remote_path Path on the remote server (e.g. /var/www/)
    /// \param out_entries Output vector of directory entries
    Result listDirectory(const std::string& remote_path,
                         std::vector<DirEntry>& out_entries);

    /// Disconnect and release all resources.
    void disconnect();

    /// Check if currently connected.
    bool isConnected() const { return session_ != nullptr; }

    /// Get last error message from libssh2 (if any).
    std::string getLastError() const;

private:
    void cleanupSession();
    void cleanupSocket();

    libssh2_socket_t socket_{LIBSSH2_INVALID_SOCKET};
    LIBSSH2_SESSION* session_{nullptr};
    LIBSSH2_SFTP* sftp_session_{nullptr};
    bool libssh2_initialized_{false};
#ifdef _WIN32
    bool winsock_initialized_{false};
#endif
};

} // namespace EtherMount

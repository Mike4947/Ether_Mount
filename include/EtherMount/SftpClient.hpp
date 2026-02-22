#pragma once

#include <libssh2.h>
#include <libssh2_sftp.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <optional>

namespace EtherMount {

/// Result of a directory listing entry.
/// v0.03: Extended with POSIX timestamps for Windows attribute mapping.
struct DirEntry {
    std::string name;
    bool is_directory;
    std::optional<uint64_t> size;
    /// POSIX mtime (seconds since epoch); used for LastWriteTime.
    std::optional<int64_t> mtime;
    /// POSIX atime (seconds since epoch); used for LastAccessTime.
    std::optional<int64_t> atime;
};

/// File/directory handle for SFTP operations.
/// Must be closed via SftpClient::closeHandle (do not destroy without closing).
struct SftpHandle {
    LIBSSH2_SFTP_HANDLE* handle = nullptr;
    bool is_directory = false;
};

/// File attributes from SFTP (stat-like).
struct FileInfo {
    bool is_directory;
    uint64_t size;
    int64_t mtime;   // POSIX seconds since epoch
    int64_t atime;   // POSIX seconds since epoch
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
    OpenFailed,
    StatFailed,
    ReadFailed,
};

/// Modular SFTP client for SSH/SFTP operations.
/// v0.03: Thread-safe (mutex guards libssh2 calls); supports open/read/stat.
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
    Result connect(const std::string& host, std::uint16_t port,
                   const std::string& username, const std::string& password);

    /// Initialize SFTP subsystem over the established SSH channel.
    Result initSftp();

    /// List contents of a remote directory (thread-safe).
    Result listDirectory(const std::string& remote_path,
                         std::vector<DirEntry>& out_entries);

    /// Get file/directory attributes for a path (thread-safe).
    /// Returns nullopt if path does not exist.
    std::optional<FileInfo> getFileInfo(const std::string& remote_path);

    /// Open file for read-only access (thread-safe).
    /// Returns nullptr on failure.
    std::unique_ptr<SftpHandle> openFile(const std::string& remote_path);

    /// Open directory (thread-safe).
    /// Returns nullptr on failure.
    std::unique_ptr<SftpHandle> openDirectory(const std::string& remote_path);

    /// Read from file. Offset and length in bytes.
    /// Returns number of bytes read, or negative on error (thread-safe).
    int64_t readFile(SftpHandle* handle, void* buffer, uint64_t offset,
                     uint32_t length);

    /// Close handle (thread-safe). No-op if handle is null.
    void closeHandle(std::unique_ptr<SftpHandle> handle);

    /// Disconnect and release all resources.
    void disconnect();

    /// Check if currently connected.
    bool isConnected() const { return session_ != nullptr; }

    /// Get last error message from libssh2 (if any).
    std::string getLastError() const;

private:
    void cleanupSession();
    void cleanupSocket();

    mutable std::mutex mutex_;
    libssh2_socket_t socket_{LIBSSH2_INVALID_SOCKET};
    LIBSSH2_SESSION* session_{nullptr};
    LIBSSH2_SFTP* sftp_session_{nullptr};
    bool libssh2_initialized_{false};
#ifdef _WIN32
    bool winsock_initialized_{false};
#endif
};

} // namespace EtherMount

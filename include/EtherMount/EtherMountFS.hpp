#pragma once

#include "EtherMount/CredentialManager.hpp"

#include <memory>
#include <string>

namespace EtherMount {

/// WinFSP-based virtual file system launcher.
/// Mounts the VPS as a network drive (e.g. Z:) with UNC path \\EtherMount\VPS.
/// v0.03: Read-only bridge - connects SftpClient; browse dirs, read files, copy from VPS.
class EtherMountFS {
public:
    EtherMountFS();
    ~EtherMountFS();

    EtherMountFS(const EtherMountFS&) = delete;
    EtherMountFS& operator=(const EtherMountFS&) = delete;

    /// Mount the file system using the given credentials.
    /// Establishes connection context; actual SFTP mapping TBD.
    /// \param creds VPS credentials (used for future SFTP integration)
    /// \return true if mount succeeded
    bool mount(const VpsCredentials& creds);

    /// Unmount the file system and release resources.
    void unmount();

    /// Check if currently mounted.
    bool isMounted() const { return mounted_; }

    /// Get the mount point (e.g. "Z:").
    const wchar_t* getMountPoint() const { return mountPoint_; }

private:
    static constexpr const wchar_t* VOLUME_PREFIX = L"\\EtherMount\\VPS";
    static constexpr const wchar_t* MOUNT_POINT = L"Z:";

    bool startFileSystem();
    void stopFileSystem();

    wchar_t mountPoint_[4];
    void* fileSystem_{nullptr};  // FSP_FILE_SYSTEM*
    bool mounted_{false};
    VpsCredentials credentials_;
};

} // namespace EtherMount

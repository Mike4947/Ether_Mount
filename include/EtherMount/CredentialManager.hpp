#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace EtherMount {

/// VPS connection credentials.
struct VpsCredentials {
    std::string host;
    std::uint16_t port{22};
    std::string username;
    std::string password;
};

/// Result of credential operations.
enum class CredentialResult {
    Success,
    NotFound,
    DecryptFailed,
    EncryptFailed,
    WriteFailed,
    ReadFailed,
    InvalidData,
};

/// Secure credential storage using Windows DPAPI.
/// Encrypts and stores VPS credentials in a local config file.
/// Windows-only; requires Crypt32.lib.
class CredentialManager {
public:
    CredentialManager();
    ~CredentialManager();

    CredentialManager(const CredentialManager&) = delete;
    CredentialManager& operator=(const CredentialManager&) = delete;

    /// Save credentials to secure storage (encrypted with DPAPI).
    CredentialResult save(const VpsCredentials& creds);

    /// Load credentials from secure storage.
    /// Returns nullopt if not found or decryption fails.
    std::optional<VpsCredentials> load();

    /// Check if credentials have been saved.
    bool hasStoredCredentials() const;

    /// Clear stored credentials.
    CredentialResult clear();

private:
    std::string getCredentialsPath() const;
};

} // namespace EtherMount

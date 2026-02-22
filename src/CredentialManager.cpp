#include "EtherMount/CredentialManager.hpp"

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <cstring>
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "Shell32.lib")
#endif

#include <vector>

namespace EtherMount {

namespace {

#ifdef _WIN32
constexpr char CREDENTIALS_FILENAME[] = "credentials.dat";
constexpr char DELIMITER = '\x1F';  // Unit separator - unlikely in host/user/pass

std::string getAppDataPath() {
    char path[MAX_PATH];
    if (FAILED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        return "";
    }
    return std::string(path) + "\\EtherMount";
}

bool ensureDirectoryExists(const std::string& path) {
    return CreateDirectoryA(path.c_str(), nullptr) != 0 ||
           GetLastError() == ERROR_ALREADY_EXISTS;
}

std::string serialize(const VpsCredentials& creds) {
    std::ostringstream oss;
    oss << creds.host << DELIMITER << creds.port << DELIMITER
        << creds.username << DELIMITER << creds.password;
    return oss.str();
}

bool deserialize(const std::string& data, VpsCredentials& creds) {
    size_t pos = 0;
    auto next = [&]() -> std::string {
        size_t end = data.find(DELIMITER, pos);
        std::string result = (end == std::string::npos)
            ? data.substr(pos)
            : data.substr(pos, end - pos);
        pos = (end == std::string::npos) ? data.size() : end + 1;
        return result;
    };

    creds.host = next();
    if (pos >= data.size()) return false;

    std::string portStr = next();
    try {
        creds.port = static_cast<std::uint16_t>(std::stoul(portStr));
    } catch (...) {
        return false;
    }
    if (pos >= data.size()) return false;

    creds.username = next();
    creds.password = next();
    return true;
}
#endif

} // namespace

CredentialManager::CredentialManager() = default;

CredentialManager::~CredentialManager() = default;

CredentialResult CredentialManager::save(const VpsCredentials& creds) {
#ifdef _WIN32
    std::string plaintext = serialize(creds);
    if (plaintext.empty()) return CredentialResult::InvalidData;

    DATA_BLOB dataIn;
    dataIn.pbData = reinterpret_cast<BYTE*>(plaintext.data());
    dataIn.cbData = static_cast<DWORD>(plaintext.size());

    DATA_BLOB dataOut;
    if (!CryptProtectData(&dataIn, L"EtherMount", nullptr, nullptr, nullptr, 0, &dataOut)) {
        return CredentialResult::EncryptFailed;
    }

    std::string dirPath = getAppDataPath();
    if (dirPath.empty() || !ensureDirectoryExists(dirPath)) {
        LocalFree(dataOut.pbData);
        return CredentialResult::WriteFailed;
    }

    std::string filePath = dirPath + "\\" + CREDENTIALS_FILENAME;
    std::ofstream out(filePath, std::ios::binary);
    if (!out) {
        LocalFree(dataOut.pbData);
        return CredentialResult::WriteFailed;
    }

    out.write(reinterpret_cast<const char*>(dataOut.pbData), dataOut.cbData);
    LocalFree(dataOut.pbData);

    if (!out.good()) return CredentialResult::WriteFailed;
    return CredentialResult::Success;
#else
    (void)creds;
    return CredentialResult::EncryptFailed;
#endif
}

std::optional<VpsCredentials> CredentialManager::load() {
#ifdef _WIN32
    std::string filePath = getCredentialsPath();
    if (filePath.empty()) return std::nullopt;

    std::ifstream in(filePath, std::ios::binary | std::ios::ate);
    if (!in) return std::nullopt;

    std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size <= 0 || size > 64 * 1024) return std::nullopt;  // Sanity limit 64KB

    std::vector<char> encrypted(static_cast<size_t>(size));
    if (!in.read(encrypted.data(), size)) return std::nullopt;

    DATA_BLOB dataIn;
    dataIn.pbData = reinterpret_cast<BYTE*>(encrypted.data());
    dataIn.cbData = static_cast<DWORD>(size);

    DATA_BLOB dataOut;
    if (!CryptUnprotectData(&dataIn, nullptr, nullptr, nullptr, nullptr, 0, &dataOut)) {
        return std::nullopt;
    }

    std::string plaintext(reinterpret_cast<char*>(dataOut.pbData),
                         reinterpret_cast<char*>(dataOut.pbData) + dataOut.cbData);
    LocalFree(dataOut.pbData);

    VpsCredentials creds;
    if (!deserialize(plaintext, creds)) return std::nullopt;
    return creds;
#else
    return std::nullopt;
#endif
}

bool CredentialManager::hasStoredCredentials() const {
#ifdef _WIN32
    std::string path = getCredentialsPath();
    if (path.empty()) return false;
    DWORD attrs = GetFileAttributesA(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
#else
    return false;
#endif
}

CredentialResult CredentialManager::clear() {
#ifdef _WIN32
    std::string path = getCredentialsPath();
    if (path.empty()) return CredentialResult::Success;
    if (DeleteFileA(path.c_str()) != 0) return CredentialResult::Success;
    if (GetLastError() == ERROR_FILE_NOT_FOUND) return CredentialResult::Success;
    return CredentialResult::WriteFailed;
#else
    return CredentialResult::Success;
#endif
}

std::string CredentialManager::getCredentialsPath() const {
#ifdef _WIN32
    std::string dir = getAppDataPath();
    if (dir.empty()) return "";
    return dir + "\\" + CREDENTIALS_FILENAME;
#else
    return "";
#endif
}

} // namespace EtherMount

#include "EtherMount/SftpClient.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#include <cstring>
#include <sstream>

namespace EtherMount {

namespace {

#ifdef _WIN32
constexpr auto SOCKET_ERROR_VALUE = SOCKET_ERROR;
#else
constexpr auto SOCKET_ERROR_VALUE = -1;
#endif

void closeSocket(libssh2_socket_t sock) {
#ifdef _WIN32
    if (sock != LIBSSH2_INVALID_SOCKET) {
        closesocket(sock);
    }
#else
    if (sock != LIBSSH2_INVALID_SOCKET) {
        close(sock);
    }
#endif
}

} // namespace

SftpClient::SftpClient() = default;

SftpClient::~SftpClient() {
    disconnect();
}

Result SftpClient::initialize() {
#ifdef _WIN32
    WSADATA wsadata;
    int rc = WSAStartup(MAKEWORD(2, 2), &wsadata);
    if (rc != 0) {
        return Result::InitFailed;
    }
    winsock_initialized_ = true;
#endif

    int rc = libssh2_init(0);
    if (rc != 0) {
#ifdef _WIN32
        WSACleanup();
        winsock_initialized_ = false;
#endif
        return Result::InitFailed;
    }
    libssh2_initialized_ = true;
    return Result::Success;
}

Result SftpClient::connect(const std::string& host, std::uint16_t port,
                           const std::string& username,
                           const std::string& password) {
    if (!libssh2_initialized_) {
        return Result::InitFailed;
    }

    // Create TCP socket
    socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_ == LIBSSH2_INVALID_SOCKET) {
        return Result::SocketCreateFailed;
    }

    struct sockaddr_in sin {};
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = inet_addr(host.c_str());

    if (connect(socket_, reinterpret_cast<struct sockaddr*>(&sin),
                sizeof(sin)) == SOCKET_ERROR_VALUE) {
        closeSocket(socket_);
        socket_ = LIBSSH2_INVALID_SOCKET;
        return Result::ConnectFailed;
    }

    // Create SSH session
    session_ = libssh2_session_init();
    if (!session_) {
        cleanupSocket();
        return Result::SessionInitFailed;
    }

    libssh2_session_set_blocking(session_, 1);

    int rc = libssh2_session_handshake(session_, socket_);
    if (rc != 0) {
        cleanupSession();
        cleanupSocket();
        return Result::HandshakeFailed;
    }

    // Authenticate with password
    rc = libssh2_userauth_password(session_, username.c_str(),
                                   password.c_str());
    if (rc != 0) {
        cleanupSession();
        cleanupSocket();
        return Result::AuthFailed;
    }

    return Result::Success;
}

Result SftpClient::initSftp() {
    if (!session_) {
        return Result::AuthFailed;
    }

    sftp_session_ = libssh2_sftp_init(session_);
    if (!sftp_session_) {
        return Result::SftpInitFailed;
    }

    return Result::Success;
}

Result SftpClient::listDirectory(const std::string& remote_path,
                                 std::vector<DirEntry>& out_entries) {
    out_entries.clear();

    if (!sftp_session_) {
        return Result::SftpInitFailed;
    }

    LIBSSH2_SFTP_HANDLE* handle =
        libssh2_sftp_opendir(sftp_session_, remote_path.c_str());
    if (!handle) {
        return Result::OpenDirFailed;
    }

    char mem[512];
    char longentry[512];
    LIBSSH2_SFTP_ATTRIBUTES attrs;

    while (true) {
        int rc = libssh2_sftp_readdir_ex(handle, mem, sizeof(mem), longentry,
                                         sizeof(longentry), &attrs);
        if (rc > 0) {
            // Skip . and ..
            if (std::strcmp(mem, ".") == 0 || std::strcmp(mem, "..") == 0) {
                continue;
            }

            DirEntry entry;
            entry.name = mem;
            entry.is_directory =
                (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) &&
                LIBSSH2_SFTP_S_ISDIR(attrs.permissions);
            if (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) {
                entry.size = attrs.filesize;
            }

            out_entries.push_back(std::move(entry));
        } else {
            break;
        }
    }

    libssh2_sftp_closedir(handle);
    return Result::Success;
}

void SftpClient::disconnect() {
    if (sftp_session_) {
        libssh2_sftp_shutdown(sftp_session_);
        sftp_session_ = nullptr;
    }

    cleanupSession();
    cleanupSocket();

    if (libssh2_initialized_) {
        libssh2_exit();
        libssh2_initialized_ = false;
    }

#ifdef _WIN32
    if (winsock_initialized_) {
        WSACleanup();
        winsock_initialized_ = false;
    }
#endif
}

std::string SftpClient::getLastError() const {
    if (!session_) {
        return "";
    }
    char* errmsg;
    int rc = libssh2_session_last_error(session_, &errmsg, nullptr, 0);
    if (rc == 0 && errmsg) {
        return std::string(errmsg);
    }
    return "";
}

void SftpClient::cleanupSession() {
    if (session_) {
        libssh2_session_disconnect(session_, "Normal Shutdown");
        libssh2_session_free(session_);
        session_ = nullptr;
    }
}

void SftpClient::cleanupSocket() {
    if (socket_ != LIBSSH2_INVALID_SOCKET) {
#ifdef _WIN32
        shutdown(socket_, SD_BOTH);
#else
        shutdown(socket_, SHUT_RDWR);
#endif
        closeSocket(socket_);
        socket_ = LIBSSH2_INVALID_SOCKET;
    }
}

} // namespace EtherMount

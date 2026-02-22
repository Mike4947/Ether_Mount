/**
 * EtherMountFS - WinFSP + SFTP bridge (v0.03 Read-Only).
 * Maps WinFSP callbacks to SftpClient. Strictly read-only.
 */

#include "EtherMount/EtherMountFS.hpp"
#include "EtherMount/SftpClient.hpp"

#if defined(_WIN32) && ETHERMOUNT_USE_WINFSP
#include <winfsp/winfsp.h>
#endif

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace EtherMount {

#if defined(_WIN32) && ETHERMOUNT_USE_WINFSP
namespace {

// --- POSIX to Windows timestamp conversion ---
// Windows FILETIME: 100-nanosecond intervals since 1601-01-01 00:00:00 UTC.
// POSIX: seconds since 1970-01-01 00:00:00 UTC.
// Conversion: WinTime = (UnixTime * 10000000) + 116444736000000000ULL
constexpr UINT64 WINDOWS_TICK_OFFSET = 116444736000000000ULL;

static inline UINT64 posixToWindowsTime(int64_t unixSeconds) {
    if (unixSeconds <= 0) return 0;
    return static_cast<UINT64>(unixSeconds) * 10000000ULL + WINDOWS_TICK_OFFSET;
}

/// Convert WinFSP path (backslashes) to Unix path (forward slashes).
/// "\" -> "/", "\path\to\file" -> "/path/to/file"
static std::string windowsPathToUnix(PWSTR FileName) {
    std::string result;
    for (const wchar_t* p = FileName; *p; ++p) {
        if (*p == L'\\')
            result += '/';
        else if (*p < 128)
            result += static_cast<char>(*p);
    }
    if (result.empty() || result[0] != '/')
        result.insert(0, "/");
    return result;
}

/// Fill FSP_FSCTL_FILE_INFO from SFTP FileInfo.
/// Maps: is_directory -> FILE_ATTRIBUTE_DIRECTORY, size, POSIX times -> Windows times.
static void fillFileInfo(const FileInfo& info, FSP_FSCTL_FILE_INFO* FileInfo) {
    FileInfo->FileAttributes = info.is_directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    FileInfo->ReparseTag = 0;
    FileInfo->AllocationSize = (info.size + 4095) & ~4095ULL;  // Round up to 4K
    FileInfo->FileSize = info.size;
    FileInfo->CreationTime = posixToWindowsTime(info.mtime);
    FileInfo->LastAccessTime = posixToWindowsTime(info.atime);
    FileInfo->LastWriteTime = posixToWindowsTime(info.mtime);
    FileInfo->ChangeTime = posixToWindowsTime(info.mtime);
    FileInfo->IndexNumber = 0;
    FileInfo->HardLinks = 0;
    FileInfo->EaSize = 0;
}

struct OpenContext {
    std::unique_ptr<SftpHandle> handle;
    std::string remotePath;
};

struct MountContext {
    SftpClient sftp;
    VpsCredentials creds;
    bool connected = false;
};
static MountContext* g_ctx = nullptr;
static std::mutex g_ctx_mutex;

static MountContext* getContext() {
    std::lock_guard<std::mutex> lock(g_ctx_mutex);
    return g_ctx;
}

// Forward declarations
static NTSTATUS GetVolumeInfo(FSP_FILE_SYSTEM* FileSystem,
                              FSP_FSCTL_VOLUME_INFO* VolumeInfo);
static NTSTATUS GetSecurityByName(FSP_FILE_SYSTEM* FileSystem, PWSTR FileName,
                                  PUINT32 PFileAttributes,
                                  PSECURITY_DESCRIPTOR SecurityDescriptor,
                                  SIZE_T* PSecurityDescriptorSize);
static NTSTATUS Create(FSP_FILE_SYSTEM* FileSystem, PWSTR FileName,
                      UINT32 CreateOptions, UINT32 GrantedAccess,
                      UINT32 FileAttributes,
                      PSECURITY_DESCRIPTOR SecurityDescriptor,
                      UINT64 AllocationSize, PVOID* PFileContext,
                      FSP_FSCTL_FILE_INFO* FileInfo);
static NTSTATUS Open(FSP_FILE_SYSTEM* FileSystem, PWSTR FileName,
                    UINT32 CreateOptions, UINT32 GrantedAccess,
                    PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo);
static VOID Cleanup(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext,
                   PWSTR FileName, ULONG Flags);
static VOID Close(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext);
static NTSTATUS Read(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext,
                    PVOID Buffer, UINT64 Offset, ULONG Length,
                    PULONG PBytesTransferred);
static NTSTATUS ReadDirectory(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext,
                              PWSTR Pattern, PWSTR Marker, PVOID Buffer,
                              ULONG Length, PULONG PBytesTransferred);
static NTSTATUS GetFileInfo(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext,
                           FSP_FSCTL_FILE_INFO* FileInfo);
static NTSTATUS Write(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext,
                     PVOID Buffer, UINT64 Offset, ULONG Length,
                     BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo,
                     PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO* FileInfo);
static NTSTATUS SetBasicInfo(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext,
                            UINT32 FileAttributes, UINT64 CreationTime,
                            UINT64 LastAccessTime, UINT64 LastWriteTime, UINT64 ChangeTime,
                            FSP_FSCTL_FILE_INFO* FileInfo);
static NTSTATUS SetFileSize(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext,
                           UINT64 NewSize, BOOLEAN SetAllocationSize,
                           FSP_FSCTL_FILE_INFO* FileInfo);
static NTSTATUS Rename(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext,
                      PWSTR FileName, PWSTR NewFileName, BOOLEAN ReplaceIfExists);

static const FSP_FILE_SYSTEM_INTERFACE* getInterface() {
    static FSP_FILE_SYSTEM_INTERFACE iface = {};
    static bool initialized = false;
    if (!initialized) {
        iface.GetVolumeInfo = GetVolumeInfo;
        iface.GetSecurityByName = GetSecurityByName;
        iface.Create = Create;
        iface.Open = Open;
        iface.Cleanup = Cleanup;
        iface.Close = Close;
        iface.Read = Read;
        iface.Write = Write;
        iface.ReadDirectory = ReadDirectory;
        iface.GetFileInfo = GetFileInfo;
        iface.SetBasicInfo = SetBasicInfo;
        iface.SetFileSize = SetFileSize;
        iface.Rename = Rename;
        initialized = true;
    }
    return &iface;
}

static NTSTATUS GetVolumeInfo(FSP_FILE_SYSTEM* /*FileSystem*/,
                              FSP_FSCTL_VOLUME_INFO* VolumeInfo) {
    VolumeInfo->TotalSize = 1024ULL * 1024 * 1024 * 1024;  // 1 TB placeholder
    VolumeInfo->FreeSize = 1024ULL * 1024 * 1024 * 1024;
    const wchar_t* label = L"EtherMount VPS";
    VolumeInfo->VolumeLabelLength = static_cast<UINT16>(
        (wcslen(label) + 1) * sizeof(WCHAR));
    wcscpy_s(VolumeInfo->VolumeLabel, 32, label);
    return STATUS_SUCCESS;
}

static NTSTATUS GetSecurityByName(FSP_FILE_SYSTEM* /*FileSystem*/,
                                  PWSTR FileName, PUINT32 PFileAttributes,
                                  PSECURITY_DESCRIPTOR SecurityDescriptor,
                                  SIZE_T* PSecurityDescriptorSize) {
    MountContext* ctx = getContext();
    if (!ctx || !ctx->connected) return STATUS_OBJECT_NAME_NOT_FOUND;

    std::string path = windowsPathToUnix(FileName);
    auto info = ctx->sftp.getFileInfo(path);
    if (!info) return STATUS_OBJECT_NAME_NOT_FOUND;

    if (PFileAttributes) {
        *PFileAttributes = info->is_directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    }

    if (SecurityDescriptor && PSecurityDescriptorSize) {
        static SECURITY_DESCRIPTOR sd;
        static BYTE sdSelfRelative[256];
        static SIZE_T sdLen = 0;
        if (sdLen == 0) {
            InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
            SetSecurityDescriptorDacl(&sd, TRUE, nullptr, FALSE);
            DWORD len = sizeof(sdSelfRelative);
            if (MakeSelfRelativeSD(&sd, sdSelfRelative, &len)) {
                sdLen = len;
            }
        }
        if (sdLen > 0 && *PSecurityDescriptorSize < sdLen) {
            *PSecurityDescriptorSize = sdLen;
            return STATUS_BUFFER_OVERFLOW;
        }
        if (sdLen > 0) {
            memcpy(SecurityDescriptor, sdSelfRelative, sdLen);
            *PSecurityDescriptorSize = sdLen;
        }
    }

    return STATUS_SUCCESS;
}

static NTSTATUS Create(FSP_FILE_SYSTEM* /*FileSystem*/, PWSTR /*FileName*/,
                      UINT32 /*CreateOptions*/, UINT32 /*GrantedAccess*/,
                      UINT32 /*FileAttributes*/,
                      PSECURITY_DESCRIPTOR /*SecurityDescriptor*/,
                      UINT64 /*AllocationSize*/, PVOID* /*PFileContext*/,
                      FSP_FSCTL_FILE_INFO* /*FileInfo*/) {
    return STATUS_MEDIA_WRITE_PROTECTED;
}

static NTSTATUS Open(FSP_FILE_SYSTEM* /*FileSystem*/, PWSTR FileName,
                    UINT32 /*CreateOptions*/, UINT32 /*GrantedAccess*/,
                    PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo) {
    MountContext* ctx = getContext();
    if (!ctx || !ctx->connected) return STATUS_OBJECT_NAME_NOT_FOUND;

    std::string path = windowsPathToUnix(FileName);
    auto info = ctx->sftp.getFileInfo(path);
    if (!info) return STATUS_OBJECT_NAME_NOT_FOUND;

    auto openCtx = std::make_unique<OpenContext>();
    if (info->is_directory) {
        openCtx->handle = ctx->sftp.openDirectory(path);
    } else {
        openCtx->handle = ctx->sftp.openFile(path);
    }
    if (!openCtx->handle) return STATUS_ACCESS_DENIED;

    openCtx->remotePath = path;
    fillFileInfo(*info, FileInfo);
    *PFileContext = openCtx.release();
    return STATUS_SUCCESS;
}

static VOID Cleanup(FSP_FILE_SYSTEM* /*FileSystem*/, PVOID /*FileContext*/,
                   PWSTR /*FileName*/, ULONG Flags) {
    if (Flags & FspCleanupDelete) {
        return;  // Read-only: delete is a no-op
    }
}

static VOID Close(FSP_FILE_SYSTEM* /*FileSystem*/, PVOID FileContext) {
    OpenContext* ctx = static_cast<OpenContext*>(FileContext);
    if (!ctx) return;

    MountContext* mctx = getContext();
    if (mctx && ctx->handle) {
        mctx->sftp.closeHandle(std::move(ctx->handle));
    }
    delete ctx;
}

static NTSTATUS Read(FSP_FILE_SYSTEM* /*FileSystem*/, PVOID FileContext,
                    PVOID Buffer, UINT64 Offset, ULONG Length,
                    PULONG PBytesTransferred) {
    OpenContext* ctx = static_cast<OpenContext*>(FileContext);
    if (!ctx || !ctx->handle || ctx->handle->is_directory) {
        *PBytesTransferred = 0;
        return STATUS_INVALID_PARAMETER;
    }

    MountContext* mctx = getContext();
    if (!mctx || !mctx->connected) {
        *PBytesTransferred = 0;
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    int64_t n = mctx->sftp.readFile(ctx->handle.get(), Buffer, Offset, Length);
    if (n < 0) {
        *PBytesTransferred = 0;
        return STATUS_ACCESS_DENIED;
    }
    *PBytesTransferred = static_cast<ULONG>(n);
    return STATUS_SUCCESS;
}

static NTSTATUS ReadDirectory(FSP_FILE_SYSTEM* /*FileSystem*/, PVOID FileContext,
                              PWSTR /*Pattern*/, PWSTR /*Marker*/,
                              PVOID Buffer, ULONG Length,
                              PULONG PBytesTransferred) {
    OpenContext* ctx = static_cast<OpenContext*>(FileContext);
    if (!ctx || !ctx->handle || !ctx->handle->is_directory) {
        *PBytesTransferred = 0;
        return STATUS_INVALID_PARAMETER;
    }

    MountContext* mctx = getContext();
    if (!mctx || !mctx->connected) {
        *PBytesTransferred = 0;
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    std::vector<DirEntry> entries;
    if (mctx->sftp.listDirectory(ctx->remotePath, entries) != Result::Success) {
        *PBytesTransferred = 0;
        return STATUS_ACCESS_DENIED;
    }

    PVOID buffer = Buffer;
    ULONG remaining = Length;
    ULONG total = 0;

    auto addEntry = [&](const wchar_t* name, bool isDir, UINT64 size, UINT64 mtime) {
        FSP_FSCTL_FILE_INFO fi = {};
        fi.FileAttributes = isDir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        fi.ReparseTag = 0;
        fi.AllocationSize = isDir ? 0 : ((size + 4095) & ~4095ULL);
        fi.FileSize = size;
        fi.CreationTime = posixToWindowsTime(static_cast<int64_t>(mtime));
        fi.LastAccessTime = fi.CreationTime;
        fi.LastWriteTime = fi.CreationTime;
        fi.ChangeTime = fi.CreationTime;
        fi.IndexNumber = 0;
        fi.HardLinks = 0;
        fi.EaSize = 0;

        SIZE_T nameLen = (wcslen(name) + 1) * sizeof(WCHAR);
        SIZE_T dirInfoSize = FSP_FSCTL_DEFAULT_ALIGN_UP(
            sizeof(FSP_FSCTL_DIR_INFO) + nameLen);
        if (remaining < dirInfoSize) return false;

        FSP_FSCTL_DIR_INFO* di = static_cast<FSP_FSCTL_DIR_INFO*>(buffer);
        di->Size = static_cast<UINT16>(dirInfoSize);
        di->FileInfo = fi;
        di->NextOffset = 0;
        memcpy(di->FileNameBuf, name, nameLen);

        buffer = static_cast<PUINT8>(buffer) + dirInfoSize;
        remaining -= static_cast<ULONG>(dirInfoSize);
        total += static_cast<ULONG>(dirInfoSize);
        return true;
    };

    if (!addEntry(L".", true, 0, 0)) { *PBytesTransferred = total; return STATUS_SUCCESS; }
    if (!addEntry(L"..", true, 0, 0)) { *PBytesTransferred = total; return STATUS_SUCCESS; }

    for (const auto& e : entries) {
        std::wstring wname;
        for (char c : e.name) {
            wname += static_cast<wchar_t>(static_cast<unsigned char>(c));
        }
        UINT64 mtime = e.mtime.value_or(0);
        UINT64 size = e.size.value_or(0);
        if (!addEntry(wname.c_str(), e.is_directory, size, mtime)) break;
    }

    *PBytesTransferred = total;
    return STATUS_SUCCESS;
}

static NTSTATUS GetFileInfo(FSP_FILE_SYSTEM* /*FileSystem*/, PVOID FileContext,
                           FSP_FSCTL_FILE_INFO* FileInfo) {
    OpenContext* ctx = static_cast<OpenContext*>(FileContext);
    if (!ctx) return STATUS_INVALID_PARAMETER;

    MountContext* mctx = getContext();
    if (!mctx || !mctx->connected) return STATUS_OBJECT_NAME_NOT_FOUND;

    auto info = mctx->sftp.getFileInfo(ctx->remotePath);
    if (!info) return STATUS_OBJECT_NAME_NOT_FOUND;

    fillFileInfo(*info, FileInfo);
    return STATUS_SUCCESS;
}

static NTSTATUS Write(FSP_FILE_SYSTEM* /*FileSystem*/, PVOID /*FileContext*/,
                     PVOID /*Buffer*/, UINT64 /*Offset*/, ULONG /*Length*/,
                     BOOLEAN /*WriteToEndOfFile*/, BOOLEAN /*ConstrainedIo*/,
                     PULONG /*PBytesTransferred*/, FSP_FSCTL_FILE_INFO* /*FileInfo*/) {
    return STATUS_MEDIA_WRITE_PROTECTED;
}

static NTSTATUS SetBasicInfo(FSP_FILE_SYSTEM* /*FileSystem*/, PVOID /*FileContext*/,
                            UINT32 /*FileAttributes*/, UINT64 /*CreationTime*/,
                            UINT64 /*LastAccessTime*/, UINT64 /*LastWriteTime*/,
                            UINT64 /*ChangeTime*/, FSP_FSCTL_FILE_INFO* /*FileInfo*/) {
    return STATUS_MEDIA_WRITE_PROTECTED;
}

static NTSTATUS SetFileSize(FSP_FILE_SYSTEM* /*FileSystem*/, PVOID /*FileContext*/,
                           UINT64 /*NewSize*/, BOOLEAN /*SetAllocationSize*/,
                           FSP_FSCTL_FILE_INFO* /*FileInfo*/) {
    return STATUS_MEDIA_WRITE_PROTECTED;
}

static NTSTATUS Rename(FSP_FILE_SYSTEM* /*FileSystem*/, PVOID /*FileContext*/,
                      PWSTR /*FileName*/, PWSTR /*NewFileName*/, BOOLEAN /*ReplaceIfExists*/) {
    return STATUS_MEDIA_WRITE_PROTECTED;
}

}  // namespace
#endif

EtherMountFS::EtherMountFS() {
    wcscpy_s(mountPoint_, MOUNT_POINT);
}

EtherMountFS::~EtherMountFS() {
    unmount();
}

bool EtherMountFS::mount(const VpsCredentials& creds) {
#if defined(_WIN32) && ETHERMOUNT_USE_WINFSP
    if (mounted_) return false;

    credentials_ = creds;

    if (!NT_SUCCESS(FspLoad(0))) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_ctx_mutex);
        g_ctx = new MountContext();
        g_ctx->creds = creds;

        if (g_ctx->sftp.initialize() != Result::Success) {
            delete g_ctx;
            g_ctx = nullptr;
            return false;
        }
        if (g_ctx->sftp.connect(creds.host, creds.port, creds.username, creds.password) != Result::Success) {
            g_ctx->sftp.disconnect();
            delete g_ctx;
            g_ctx = nullptr;
            return false;
        }
        if (g_ctx->sftp.initSftp() != Result::Success) {
            g_ctx->sftp.disconnect();
            delete g_ctx;
            g_ctx = nullptr;
            return false;
        }
        g_ctx->connected = true;
    }

    FSP_FSCTL_VOLUME_PARAMS volumeParams = {};
    volumeParams.SectorSize = 4096;
    volumeParams.SectorsPerAllocationUnit = 1;
    volumeParams.VolumeCreationTime = 0;
    volumeParams.VolumeSerialNumber = 0;
    volumeParams.FileInfoTimeout = 1000;
    volumeParams.CaseSensitiveSearch = 0;
    volumeParams.CasePreservedNames = 1;
    volumeParams.UnicodeOnDisk = 1;
    volumeParams.PersistentAcls = 1;
    volumeParams.MaxComponentLength = 256;
    volumeParams.ReadOnlyVolume = 1;
    wcscpy_s(volumeParams.Prefix, VOLUME_PREFIX);
    wcscpy_s(volumeParams.FileSystemName, L"EtherMount");

    FSP_FILE_SYSTEM* fs = nullptr;
    wchar_t deviceName[] = L"" FSP_FSCTL_NET_DEVICE_NAME;
    NTSTATUS status = FspFileSystemCreate(
        deviceName, &volumeParams, getInterface(), &fs);

    if (!NT_SUCCESS(status)) {
        std::lock_guard<std::mutex> lock(g_ctx_mutex);
        if (g_ctx) {
            g_ctx->sftp.disconnect();
            delete g_ctx;
            g_ctx = nullptr;
        }
        return false;
    }

    fileSystem_ = fs;

    status = FspFileSystemSetMountPoint(fs, mountPoint_);
    if (!NT_SUCCESS(status)) {
        FspFileSystemDelete(fs);
        fileSystem_ = nullptr;
        std::lock_guard<std::mutex> lock(g_ctx_mutex);
        if (g_ctx) {
            g_ctx->sftp.disconnect();
            delete g_ctx;
            g_ctx = nullptr;
        }
        return false;
    }

    status = FspFileSystemStartDispatcher(fs, 0);
    if (!NT_SUCCESS(status)) {
        FspFileSystemDelete(fs);
        fileSystem_ = nullptr;
        std::lock_guard<std::mutex> lock(g_ctx_mutex);
        if (g_ctx) {
            g_ctx->sftp.disconnect();
            delete g_ctx;
            g_ctx = nullptr;
        }
        return false;
    }

    mounted_ = true;
    return true;
#else
    (void)creds;
    return false;
#endif
}

void EtherMountFS::unmount() {
#if defined(_WIN32) && ETHERMOUNT_USE_WINFSP
    if (!mounted_) return;

    FSP_FILE_SYSTEM* fs = static_cast<FSP_FILE_SYSTEM*>(fileSystem_);
    if (fs) {
        FspFileSystemStopDispatcher(fs);
        FspFileSystemDelete(fs);
        fileSystem_ = nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(g_ctx_mutex);
        if (g_ctx) {
            g_ctx->sftp.disconnect();
            delete g_ctx;
            g_ctx = nullptr;
        }
    }

    mounted_ = false;
#endif
}

}  // namespace EtherMount

/**
 * EtherMountFS - WinFSP skeleton for VPS virtual drive.
 * v0.02: Minimal implementation - mount/unmount only.
 * SFTP read/write mapping will be added in future versions.
 * When ETHERMOUNT_USE_WINFSP=0 (SDK not installed), mount() returns false.
 */

#include "EtherMount/EtherMountFS.hpp"

#if defined(_WIN32) && ETHERMOUNT_USE_WINFSP
#include <winfsp/winfsp.h>
#endif

#include <cstring>
#include <stdexcept>

namespace EtherMount {

#if defined(_WIN32) && ETHERMOUNT_USE_WINFSP
namespace {

constexpr UINT64 ALLOCATION_UNIT = 4096;
constexpr UINT64 VOLUME_SIZE = 1024ULL * 1024 * 1024;  // 1 GB placeholder

// Forward declare interface implementations
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
static NTSTATUS ReadDirectory(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext,
                              PWSTR Pattern, PWSTR Marker, PVOID Buffer,
                              ULONG Length, PULONG PBytesTransferred);

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
        iface.ReadDirectory = ReadDirectory;
        initialized = true;
    }
    return &iface;
}

static NTSTATUS GetVolumeInfo(FSP_FILE_SYSTEM* /*FileSystem*/,
                              FSP_FSCTL_VOLUME_INFO* VolumeInfo) {
    VolumeInfo->TotalSize = VOLUME_SIZE;
    VolumeInfo->FreeSize = VOLUME_SIZE;
    VolumeInfo->VolumeLabelLength = 0;
    return STATUS_SUCCESS;
}

static NTSTATUS GetSecurityByName(FSP_FILE_SYSTEM* /*FileSystem*/,
                                  PWSTR FileName, PUINT32 PFileAttributes,
                                  PSECURITY_DESCRIPTOR SecurityDescriptor,
                                  SIZE_T* PSecurityDescriptorSize) {
    // Only support root path "\"
    if (FileName[0] != L'\\' || FileName[1] != L'\0') {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    if (PFileAttributes) {
        *PFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
    }

    if (SecurityDescriptor && PSecurityDescriptorSize) {
        // Skeleton: use minimal self-relative SD with NULL DACL (everyone access)
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
    // Skeleton: deny create for now
    return STATUS_ACCESS_DENIED;
}

static NTSTATUS Open(FSP_FILE_SYSTEM* /*FileSystem*/, PWSTR FileName,
                    UINT32 /*CreateOptions*/, UINT32 /*GrantedAccess*/,
                    PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo) {
    // Only support root path "\"
    if (FileName[0] != L'\\' || FileName[1] != L'\0') {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    *PFileContext = reinterpret_cast<PVOID>(1);  // Dummy context for root

    FileInfo->FileAttributes = FILE_ATTRIBUTE_DIRECTORY;
    FileInfo->ReparseTag = 0;
    FileInfo->FileSize = 0;
    FileInfo->AllocationSize = 0;
    FileInfo->CreationTime = 0;
    FileInfo->LastAccessTime = 0;
    FileInfo->LastWriteTime = 0;
    FileInfo->ChangeTime = 0;
    FileInfo->IndexNumber = 0;
    FileInfo->HardLinks = 0;
    FileInfo->EaSize = 0;

    return STATUS_SUCCESS;
}

static VOID Cleanup(FSP_FILE_SYSTEM* /*FileSystem*/, PVOID /*FileContext*/,
                    PWSTR /*FileName*/, ULONG /*Flags*/) {}

static VOID Close(FSP_FILE_SYSTEM* /*FileSystem*/, PVOID /*FileContext*/) {}

static NTSTATUS ReadDirectory(FSP_FILE_SYSTEM* FileSystem, PVOID /*FileContext*/,
                              PWSTR /*Pattern*/, PWSTR /*Marker*/,
                              PVOID Buffer, ULONG Length,
                              PULONG PBytesTransferred) {
    // Skeleton: return empty directory (no entries yet)
    *PBytesTransferred = 0;
    return STATUS_SUCCESS;
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

    FSP_FSCTL_VOLUME_PARAMS volumeParams = {};
    volumeParams.SectorSize = static_cast<UINT16>(ALLOCATION_UNIT);
    volumeParams.SectorsPerAllocationUnit = 1;
    volumeParams.VolumeCreationTime = 0;
    volumeParams.VolumeSerialNumber = 0;
    volumeParams.FileInfoTimeout = 1000;
    volumeParams.CaseSensitiveSearch = 0;
    volumeParams.CasePreservedNames = 1;
    volumeParams.UnicodeOnDisk = 1;
    volumeParams.PersistentAcls = 1;
    volumeParams.MaxComponentLength = 256;
    wcscpy_s(volumeParams.Prefix, VOLUME_PREFIX);
    wcscpy_s(volumeParams.FileSystemName, L"EtherMount");

    FSP_FILE_SYSTEM* fs = nullptr;
    NTSTATUS status = FspFileSystemCreate(
        L"" FSP_FSCTL_NET_DEVICE_NAME, &volumeParams, getInterface(),
        &fs);

    if (!NT_SUCCESS(status)) {
        return false;
    }

    fileSystem_ = fs;

    status = FspFileSystemSetMountPoint(fs, mountPoint_);
    if (!NT_SUCCESS(status)) {
        FspFileSystemDelete(fs);
        fileSystem_ = nullptr;
        return false;
    }

    status = FspFileSystemStartDispatcher(fs, 0);
    if (!NT_SUCCESS(status)) {
        FspFileSystemDelete(fs);
        fileSystem_ = nullptr;
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
    mounted_ = false;
#endif
}

}  // namespace EtherMount

/**
 * EtherMount v0.02 - Integration Phase
 *
 * System tray application with:
 * - Pillar 1: CredentialManager (DPAPI-secured credential storage)
 * - Pillar 2: Qt6 GUI (System Tray + Settings Dialog)
 * - Pillar 3: WinFSP (Virtual File System skeleton)
 *
 * Architecture:
 *
 *   main()
 *      |
 *      v
 *   TrayApplication (QApplication subclass)
 *      |
 *      +-- CredentialManager: save/load VPS credentials (DPAPI)
 *      |
 *      +-- SettingsDialog: Qt dialog for Host, Port, Username, Password
 *      |       |
 *      |       +-- Uses CredentialManager to persist
 *      |
 *      +-- EtherMountFS: WinFSP launcher
 *      |       |
 *      |       +-- mount(creds): FspLoad, FspFileSystemCreate, SetMountPoint, StartDispatcher
 *      |       +-- unmount(): StopDispatcher, FspFileSystemDelete
 *      |
 *      +-- Tray menu: Settings | Mount VPS | Unmount VPS | Exit
 *
 * Flow:
 *   1. User runs EtherMount -> Tray icon appears
 *   2. Right-click -> Settings -> Enter credentials -> Save (CredentialManager encrypts)
 *   3. Right-click -> Mount VPS -> CredentialManager loads -> EtherMountFS.mount()
 *   4. Z: drive appears (\\EtherMount\VPS) - skeleton shows empty root
 *   5. Right-click -> Unmount VPS -> EtherMountFS.unmount()
 *   6. Right-click -> Exit -> Clean unmount, quit
 */

#include "EtherMount/TrayApplication.hpp"

#include <QMessageBox>

int main(int argc, char* argv[]) {
    EtherMount::TrayApplication app(argc, argv);

    if (!app.initTray()) {
        return 1;
    }

    return app.exec();
}

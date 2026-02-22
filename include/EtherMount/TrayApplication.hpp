#pragma once


#include <QApplication>
#include <QSystemTrayIcon>
#include <QMenu>
#include <memory>

namespace EtherMount {

class MainWindow;

/// System tray application for EtherMount.
/// Manages tray icon, context menu, and coordinates Settings/VFS.
class TrayApplication : public QApplication {
    Q_OBJECT
public:
    TrayApplication(int& argc, char* argv[]);
    ~TrayApplication();

    /// Initialize tray icon and show.
    bool initTray();

private slots:
    void onSettings();
    void onMountVps();
    void onUnmountVps();
    void onExit();
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);

private:
    void createTrayMenu();
    void updateMountState();

    QSystemTrayIcon trayIcon_;
    QMenu* trayMenu_{nullptr};
    QAction* mountAction_{nullptr};
    QAction* unmountAction_{nullptr};
    std::unique_ptr<MainWindow> mainWindow_;
};

} // namespace EtherMount

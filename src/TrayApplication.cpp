#include "EtherMount/TrayApplication.hpp"
#include "EtherMount/MainWindow.hpp"
#include "EtherMount/BrowserWindow.hpp"

#include <QAction>
#include <QMessageBox>
#include <QIcon>
#include <QPainter>
#include <QPixmap>

namespace EtherMount {

namespace {

QPixmap createTrayIconPixmap() {
    QPixmap pix(32, 32);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(66, 133, 244));  // Blue
    p.setPen(Qt::NoPen);
    p.drawEllipse(4, 4, 24, 24);
    p.setBrush(Qt::white);
    p.drawEllipse(10, 10, 12, 12);
    return pix;
}

} // namespace

TrayApplication::TrayApplication(int& argc, char* argv[])
    : QApplication(argc, argv) {
    setApplicationName("EtherMount");
    setApplicationDisplayName("EtherMount");
    setQuitOnLastWindowClosed(false);
}

TrayApplication::~TrayApplication() {
    // MainWindow destructor handles unmount
}

bool TrayApplication::initTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        QMessageBox::critical(nullptr, tr("EtherMount"),
                              tr("System tray is not available."));
        return false;
    }

    createTrayMenu();

    trayIcon_.setIcon(QIcon(createTrayIconPixmap()));
    trayIcon_.setToolTip(tr("EtherMount - SFTP VPS Mount"));
    trayIcon_.setContextMenu(trayMenu_);
    connect(&trayIcon_, &QSystemTrayIcon::activated, this, &TrayApplication::onTrayActivated);
    trayIcon_.show();

    // Show main window on startup (toolbar, settings, documentation)
    mainWindow_ = std::make_unique<MainWindow>();
    connect(mountAction_, &QAction::triggered, mainWindow_.get(), &MainWindow::onMountVps);
    connect(unmountAction_, &QAction::triggered, mainWindow_.get(), &MainWindow::onUnmountVps);
    connect(mainWindow_.get(), &MainWindow::mountedChanged, this, [this](bool) { updateMountState(); });
    mainWindow_->init();
    updateMountState();

    return true;
}

void TrayApplication::createTrayMenu() {
    trayMenu_ = new QMenu();

    auto* settingsAction = trayMenu_->addAction(tr("Settings"));
    connect(settingsAction, &QAction::triggered, this, &TrayApplication::onSettings);

    auto* browseAction = trayMenu_->addAction(tr("Browse VPS"));
    connect(browseAction, &QAction::triggered, this, &TrayApplication::showBrowserWindow);

    mountAction_ = trayMenu_->addAction(tr("Mount VPS"));
    unmountAction_ = trayMenu_->addAction(tr("Unmount VPS"));

    trayMenu_->addSeparator();

    auto* exitAction = trayMenu_->addAction(tr("Exit"));
    connect(exitAction, &QAction::triggered, this, &TrayApplication::onExit);

    updateMountState();
}

void TrayApplication::updateMountState() {
    bool mounted = mainWindow_ && mainWindow_->isMounted();
    if (mountAction_) mountAction_->setEnabled(!mounted);
    if (unmountAction_) unmountAction_->setEnabled(mounted);
}

void TrayApplication::onSettings() {
    if (mainWindow_) mainWindow_->onSettings();
}

void TrayApplication::onMountVps() {
    if (mainWindow_) mainWindow_->onMountVps();
}

void TrayApplication::onUnmountVps() {
    if (mainWindow_) mainWindow_->onUnmountVps();
}

void TrayApplication::onExit() {
    if (mainWindow_) mainWindow_->onExit();
}

void TrayApplication::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::DoubleClick && mainWindow_) {
        mainWindow_->show();
        mainWindow_->raise();
        mainWindow_->activateWindow();
    }
}

void TrayApplication::showBrowserWindow() {
    if (!browserWindow_) {
        browserWindow_ = std::make_unique<BrowserWindow>();
    }
    browserWindow_->connectAndShow();
}

} // namespace EtherMount

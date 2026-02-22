/**
 * EtherMount - SFTP VPS Mount
 *
 * System tray application. When user opens EtherMount under Network in File Explorer,
 * launches with --browser to show WinSCP-style dual-pane browser.
 */

#include "EtherMount/TrayApplication.hpp"

#include <QMessageBox>
#include <QString>

int main(int argc, char* argv[]) {
    EtherMount::TrayApplication app(argc, argv);

    bool browserMode = false;
    for (int i = 1; i < argc; ++i) {
        if (QString::fromUtf8(argv[i]) == QLatin1String("--browser")) {
            browserMode = true;
            break;
        }
    }

    if (!app.initTray()) {
        return 1;
    }

    if (browserMode) {
        app.showBrowserWindow();
    }

    return app.exec();
}

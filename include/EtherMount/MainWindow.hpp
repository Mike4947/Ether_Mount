#pragma once

#include "EtherMount/CredentialManager.hpp"

#include <QMainWindow>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QToolBar>
#include <memory>

namespace EtherMount {

class EtherMountFS;
class SettingsDialog;

/// Main application window with toolbar, settings, and documentation.
/// Shown on startup; provides professional UI with all controls visible.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

    /// Initialize UI and show window.
    void init();

    /// Whether VPS is currently mounted.
    bool isMounted() const { return mounted_; }

public slots:
    void onSettings();
    void onMountVps();
    void onUnmountVps();
    void onExit();
    void onSaveSettings();

signals:
    void mountedChanged(bool mounted);

private:
    void setupToolbar();
    void setupCentralWidget();
    void updateMountState();

    QToolBar* toolbar_{nullptr};
    QLineEdit* hostEdit_{nullptr};
    QSpinBox* portSpin_{nullptr};
    QLineEdit* usernameEdit_{nullptr};
    QLineEdit* passwordEdit_{nullptr};
    QLabel* statusLabel_{nullptr};

    std::unique_ptr<SettingsDialog> settingsDialog_;
    std::unique_ptr<EtherMountFS> fileSystem_;
    CredentialManager credentialManager_;
    bool mounted_{false};
};

} // namespace EtherMount

#include "EtherMount/MainWindow.hpp"
#include "EtherMount/SettingsDialog.hpp"
#include "EtherMount/EtherMountFS.hpp"
#include "EtherMount/ShellExtRegistrar.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

namespace EtherMount {

namespace {

constexpr char GITHUB_URL[] = "https://github.com/Mike4947/Ether_Mount";

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("EtherMount - SFTP VPS Mount"));
    setMinimumSize(520, 480);
    resize(560, 520);
}

MainWindow::~MainWindow() {
    if (mounted_ && fileSystem_) {
        fileSystem_->unmount();
    }
}

void MainWindow::init() {
    setupToolbar();
    setupCentralWidget();
    updateMountState();

    // Load saved credentials into form
    auto creds = credentialManager_.load();
    if (creds) {
        hostEdit_->setText(QString::fromStdString(creds->host));
        portSpin_->setValue(static_cast<int>(creds->port));
        usernameEdit_->setText(QString::fromStdString(creds->username));
        passwordEdit_->setText(QString::fromStdString(creds->password));
        remotePathEdit_->setText(QString::fromStdString(creds->remotePath));
        QString dl = QString::fromStdString(creds->driveLetter).toUpper();
        if (dl.isEmpty()) dl = "Z";
        driveCombo_->setCurrentText(dl + ":");
        displayNameEdit_->setText(QString::fromStdString(creds->displayName));
    }

    show();
}

void MainWindow::setupToolbar() {
    toolbar_ = addToolBar(tr("Main"));
    toolbar_->setMovable(false);
    toolbar_->setIconSize(QSize(24, 24));

    auto* settingsAction = toolbar_->addAction(tr("Settings"));
    connect(settingsAction, &QAction::triggered, this, &MainWindow::onSettings);

    toolbar_->addSeparator();

    auto* mountAction = toolbar_->addAction(tr("Mount VPS"));
    connect(mountAction, &QAction::triggered, this, &MainWindow::onMountVps);

    auto* unmountAction = toolbar_->addAction(tr("Unmount VPS"));
    connect(unmountAction, &QAction::triggered, this, &MainWindow::onUnmountVps);

    toolbar_->addSeparator();

    auto* exitAction = toolbar_->addAction(tr("Exit"));
    connect(exitAction, &QAction::triggered, this, &MainWindow::onExit);
}

void MainWindow::setupCentralWidget() {
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* central = new QWidget;
    auto* layout = new QVBoxLayout(central);
    layout->setSpacing(16);

    // --- Welcome / Explanation ---
    auto* welcomeLabel = new QLabel(this);
    welcomeLabel->setWordWrap(true);
    welcomeLabel->setTextFormat(Qt::RichText);
    welcomeLabel->setText(
        "<h2 style='margin-top:0;'>Welcome to EtherMount</h2>"
        "<p>EtherMount lets you browse your remote VPS (via SFTP) in Windows File Explorer. "
        "Use the <b>Network</b> folder for a WinSCP-style browser, or the <b>drive letter</b> (requires WinFSP).</p>"
        "<p><b>How it works:</b></p>"
        "<ol>"
        "<li>Enter your VPS connection details below (Host, Port, Username, Password).</li>"
        "<li>Set the <b>folder name</b> (e.g. \"EtherMount VPS\") — this appears under <b>Network</b> in File Explorer.</li>"
        "<li>Click <b>Save</b> to store your credentials securely (encrypted with Windows DPAPI).</li>"
        "<li><b>Option A (recommended):</b> Open File Explorer → <b>Network</b> → your folder name. A WinSCP-style browser opens.</li>"
        "<li><b>Option B:</b> Tray menu → <b>Browse VPS</b> — same browser window.</li>"
        "<li><b>Option C:</b> Click <b>Mount VPS</b> for drive letter access (requires WinFSP).</li>"
        "<li>Click <b>Unmount VPS</b> when done (drive letter only).</li>"
        "</ol>"
        "<p><b>Requirements:</b> For drive letter: WinFSP. "
        "<a href='https://github.com/winfsp/winfsp/releases'>Download WinFSP</a> with <b>Developer</b> option.</p>"
    );
    welcomeLabel->setOpenExternalLinks(true);
    welcomeLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    layout->addWidget(welcomeLabel);

    // --- Settings form ---
    auto* settingsGroup = new QGroupBox(tr("VPS Connection Settings"));
    auto* formLayout = new QFormLayout(settingsGroup);

    hostEdit_ = new QLineEdit(this);
    hostEdit_->setPlaceholderText(tr("e.g. 192.168.1.100 or vps.example.com"));
    hostEdit_->setMaxLength(256);
    formLayout->addRow(tr("Host / IP:"), hostEdit_);

    portSpin_ = new QSpinBox(this);
    portSpin_->setRange(1, 65535);
    portSpin_->setValue(22);
    formLayout->addRow(tr("Port:"), portSpin_);

    usernameEdit_ = new QLineEdit(this);
    usernameEdit_->setPlaceholderText(tr("SSH username"));
    usernameEdit_->setMaxLength(256);
    formLayout->addRow(tr("Username:"), usernameEdit_);

    passwordEdit_ = new QLineEdit(this);
    passwordEdit_->setPlaceholderText(tr("SSH password"));
    passwordEdit_->setEchoMode(QLineEdit::Password);
    passwordEdit_->setMaxLength(512);
    formLayout->addRow(tr("Password:"), passwordEdit_);

    remotePathEdit_ = new QLineEdit(this);
    remotePathEdit_->setPlaceholderText(tr("e.g. /var/www/flowdesk (default: /)"));
    remotePathEdit_->setMaxLength(512);
    formLayout->addRow(tr("Remote path:"), remotePathEdit_);

    driveCombo_ = new QComboBox(this);
    for (char c = 'D'; c <= 'Z'; ++c) {
        driveCombo_->addItem(QString(QChar(c)) + ":");
    }
    driveCombo_->setCurrentText("Z:");
    formLayout->addRow(tr("Drive letter:"), driveCombo_);

    displayNameEdit_ = new QLineEdit(this);
    displayNameEdit_->setPlaceholderText(tr("e.g. EtherMount VPS or Server (shown under This PC)"));
    displayNameEdit_->setMaxLength(64);
    displayNameEdit_->setText("EtherMount VPS");
    formLayout->addRow(tr("Folder name (This PC):"), displayNameEdit_);

    auto* saveBtn = new QPushButton(tr("Save credentials"), this);
    connect(saveBtn, &QPushButton::clicked, this, &MainWindow::onSaveSettings);
    formLayout->addRow(QString(), saveBtn);

    layout->addWidget(settingsGroup);

    // --- Status ---
    statusLabel_ = new QLabel(this);
    statusLabel_->setStyleSheet("color: #666; font-style: italic;");
    layout->addWidget(statusLabel_);

    // --- GitHub link ---
    auto* githubLabel = new QLabel(this);
    githubLabel->setOpenExternalLinks(true);
    githubLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    githubLabel->setText(
        tr("Source code & documentation: <a href='%1'>%1</a>").arg(GITHUB_URL)
    );
    layout->addWidget(githubLabel);

    layout->addStretch();

    scroll->setWidget(central);
    setCentralWidget(scroll);
}

void MainWindow::updateMountState() {
    if (statusLabel_) {
        statusLabel_->setText(mounted_
            ? tr("Status: VPS is mounted. Browse files in File Explorer.")
            : tr("Status: Not mounted. Configure settings and click Mount VPS."));
    }
    // Update toolbar actions - we'd need to store refs; for now the actions are always enabled
    // and we guard in the slot handlers
}

void MainWindow::onSettings() {
    if (!settingsDialog_) {
        settingsDialog_ = std::make_unique<SettingsDialog>(this);
        connect(settingsDialog_.get(), &QDialog::accepted, this, [this]() {
            auto creds = credentialManager_.load();
            if (creds) {
                hostEdit_->setText(QString::fromStdString(creds->host));
                portSpin_->setValue(static_cast<int>(creds->port));
                usernameEdit_->setText(QString::fromStdString(creds->username));
                passwordEdit_->setText(QString::fromStdString(creds->password));
                remotePathEdit_->setText(QString::fromStdString(creds->remotePath));
                QString dl = QString::fromStdString(creds->driveLetter).toUpper();
                if (dl.isEmpty()) dl = "Z";
                driveCombo_->setCurrentText(dl + ":");
            }
        });
    }
    settingsDialog_->loadFromStorage();
    settingsDialog_->show();
    settingsDialog_->raise();
    settingsDialog_->activateWindow();
}

void MainWindow::onMountVps() {
    if (mounted_) return;

    VpsCredentials creds;
    creds.host = hostEdit_->text().trimmed().toStdString();
    creds.port = static_cast<std::uint16_t>(portSpin_->value());
    creds.username = usernameEdit_->text().trimmed().toStdString();
    creds.password = passwordEdit_->text().toStdString();
    std::string rp = remotePathEdit_->text().trimmed().toStdString();
    creds.remotePath = (rp.empty() ? "/" : rp);
    if (creds.remotePath.size() > 1 && creds.remotePath.back() == '/') creds.remotePath.pop_back();
    if (creds.remotePath.empty()) creds.remotePath = "/";
    if (creds.remotePath[0] != '/') creds.remotePath = "/" + creds.remotePath;
    QString dl = driveCombo_->currentText().trimmed();
    creds.driveLetter = (dl.isEmpty() ? "Z" : dl.left(1).toUpper().toStdString());
    std::string dn = displayNameEdit_->text().trimmed().toStdString();
    creds.displayName = (dn.empty() ? "EtherMount VPS" : dn);

    if (creds.host.empty() || creds.username.empty()) {
        QMessageBox::information(this, tr("EtherMount"),
            tr("Please enter Host and Username, then click Save before mounting."));
        return;
    }

    if (!fileSystem_) {
        fileSystem_ = std::make_unique<EtherMountFS>();
    }

    if (!fileSystem_->mount(creds)) {
        QMessageBox::critical(this, tr("EtherMount"),
            tr("Failed to mount VPS. Check credentials and connection. "
               "Ensure WinFSP is installed (see link above)."));
        return;
    }

    mounted_ = true;
    updateMountState();
    emit mountedChanged(true);
    QMessageBox::information(this, tr("EtherMount"),
        tr("VPS mounted successfully. You can browse files in File Explorer."));
}

void MainWindow::onUnmountVps() {
    if (!mounted_ || !fileSystem_) return;

    fileSystem_->unmount();
    mounted_ = false;
    updateMountState();
    emit mountedChanged(false);
    QMessageBox::information(this, tr("EtherMount"), tr("VPS unmounted."));
}

void MainWindow::onExit() {
    if (mounted_ && fileSystem_) {
        fileSystem_->unmount();
    }
    qApp->quit();
}

void MainWindow::onSaveSettings() {
    VpsCredentials creds;
    creds.host = hostEdit_->text().trimmed().toStdString();
    creds.port = static_cast<std::uint16_t>(portSpin_->value());
    creds.username = usernameEdit_->text().trimmed().toStdString();
    creds.password = passwordEdit_->text().toStdString();
    std::string rp = remotePathEdit_->text().trimmed().toStdString();
    creds.remotePath = (rp.empty() ? "/" : rp);
    if (creds.remotePath.size() > 1 && creds.remotePath.back() == '/') creds.remotePath.pop_back();
    if (creds.remotePath.empty()) creds.remotePath = "/";
    if (creds.remotePath[0] != '/') creds.remotePath = "/" + creds.remotePath;
    QString dl = driveCombo_->currentText().trimmed();
    creds.driveLetter = (dl.isEmpty() ? "Z" : dl.left(1).toUpper().toStdString());
    std::string dn = displayNameEdit_->text().trimmed().toStdString();
    creds.displayName = (dn.empty() ? "EtherMount VPS" : dn);

    if (creds.host.empty()) {
        QMessageBox::warning(this, tr("Validation Error"), tr("Host/IP is required."));
        return;
    }
    if (creds.username.empty()) {
        QMessageBox::warning(this, tr("Validation Error"), tr("Username is required."));
        return;
    }

    auto result = credentialManager_.save(creds);
    if (result != CredentialResult::Success) {
        QMessageBox::critical(this, tr("Save Failed"),
            tr("Failed to save credentials securely."));
        return;
    }

    // Register/update Shell Namespace Extension (folder under Network)
    if (ShellExtRegistrar::registerShellExt(creds.displayName)) {
        QMessageBox::information(this, tr("EtherMount"),
            tr("Credentials saved. Open File Explorer → Network to see \"%1\" (double-click opens the browser), or use tray → Browse VPS.")
                .arg(QString::fromStdString(creds.displayName)));
    } else {
        QMessageBox::information(this, tr("EtherMount"),
            tr("Credentials saved. Ensure EtherMountShellExt.dll is next to EtherMount.exe. You can click Mount VPS."));
    }
}

} // namespace EtherMount

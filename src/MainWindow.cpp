#include "EtherMount/MainWindow.hpp"
#include "EtherMount/SettingsDialog.hpp"
#include "EtherMount/EtherMountFS.hpp"

#include <QAction>
#include <QApplication>
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
        "<p>EtherMount mounts your remote VPS (via SFTP) as a <b>native network drive</b> in Windows File Explorer. "
        "You can browse, open, and manage files on your server as if they were on a local disk.</p>"
        "<p><b>How it works:</b></p>"
        "<ol>"
        "<li>Enter your VPS connection details below (Host, Port, Username, Password).</li>"
        "<li>Click <b>Save</b> to store your credentials securely (encrypted with Windows DPAPI).</li>"
        "<li>Click <b>Mount VPS</b> — a new drive (e.g. Z:) will appear in File Explorer.</li>"
        "<li>Browse your remote files at <code>\\\\EtherMount\\VPS</code> or the assigned drive letter.</li>"
        "<li>Click <b>Unmount VPS</b> when done to disconnect.</li>"
        "</ol>"
        "<p><b>Requirements:</b> WinFSP must be installed for mounting. "
        "<a href='https://github.com/winfsp/winfsp/releases'>Download WinFSP</a> and run the installer with the <b>Developer</b> option.</p>"
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

    QMessageBox::information(this, tr("EtherMount"),
        tr("Credentials saved securely. You can now click Mount VPS."));
}

} // namespace EtherMount

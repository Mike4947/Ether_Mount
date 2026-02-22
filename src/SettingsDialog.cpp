#include "EtherMount/SettingsDialog.hpp"

#include <QMessageBox>
#include <QVBoxLayout>

namespace EtherMount {

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setupUi();
}

void SettingsDialog::setupUi() {
    setWindowTitle(tr("EtherMount - VPS Settings"));
    setMinimumWidth(400);

    auto* layout = new QVBoxLayout(this);

    auto* formLayout = new QFormLayout;

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

    layout->addLayout(formLayout);

    auto* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);

    saveButton_ = buttonBox->button(QDialogButtonBox::Save);
    saveButton_->setText(tr("Save"));

    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        if (saveToStorage()) {
            accept();
        }
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    layout->addWidget(buttonBox);
}

void SettingsDialog::setCredentials(const VpsCredentials& creds) {
    hostEdit_->setText(QString::fromStdString(creds.host));
    portSpin_->setValue(static_cast<int>(creds.port));
    usernameEdit_->setText(QString::fromStdString(creds.username));
    passwordEdit_->setText(QString::fromStdString(creds.password));
}

VpsCredentials SettingsDialog::getCredentials() const {
    VpsCredentials creds;
    creds.host = hostEdit_->text().trimmed().toStdString();
    creds.port = static_cast<std::uint16_t>(portSpin_->value());
    creds.username = usernameEdit_->text().trimmed().toStdString();
    creds.password = passwordEdit_->text().toStdString();
    return creds;
}

bool SettingsDialog::loadFromStorage() {
    auto creds = credentialManager_.load();
    if (creds) {
        setCredentials(*creds);
        return true;
    }
    return false;
}

bool SettingsDialog::saveToStorage() {
    VpsCredentials creds = getCredentials();
    if (creds.host.empty()) {
        QMessageBox::warning(this, tr("Validation Error"),
                            tr("Host/IP is required."));
        return false;
    }
    if (creds.username.empty()) {
        QMessageBox::warning(this, tr("Validation Error"),
                            tr("Username is required."));
        return false;
    }

    auto result = credentialManager_.save(creds);
    if (result != CredentialResult::Success) {
        QMessageBox::critical(this, tr("Save Failed"),
                              tr("Failed to save credentials securely."));
        return false;
    }
    return true;
}

} // namespace EtherMount

#pragma once

#include "EtherMount/CredentialManager.hpp"

#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QFormLayout>
#include <QDialogButtonBox>

namespace EtherMount {

/// Qt dialog for editing VPS connection settings.
/// Integrates with CredentialManager for save/load.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    /// Populate fields from credentials.
    void setCredentials(const VpsCredentials& creds);

    /// Get current field values as credentials.
    VpsCredentials getCredentials() const;

    /// Load from CredentialManager and populate.
    bool loadFromStorage();

    /// Save current values to CredentialManager.
    bool saveToStorage();

private:
    void setupUi();

    QLineEdit* hostEdit_{nullptr};
    QSpinBox* portSpin_{nullptr};
    QLineEdit* usernameEdit_{nullptr};
    QLineEdit* passwordEdit_{nullptr};
    QLineEdit* remotePathEdit_{nullptr};
    QComboBox* driveCombo_{nullptr};
    QLineEdit* displayNameEdit_{nullptr};
    QPushButton* saveButton_{nullptr};
    CredentialManager credentialManager_;
};

} // namespace EtherMount

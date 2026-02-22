#pragma once

#include "EtherMount/CredentialManager.hpp"
#include "EtherMount/SftpClient.hpp"

#include <QMainWindow>
#include <QTreeView>
#include <QTableWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QToolButton>
#include <QStatusBar>
#include <QLabel>
#include <QSplitter>
#include <deque>
#include <memory>

namespace EtherMount {

/// WinSCP-style dual-pane file browser: local (left) | remote SFTP (right).
/// Replicates WinSCP UI with menu bar, per-pane toolbars, and status bar.
class BrowserWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit BrowserWindow(QWidget* parent = nullptr);
    ~BrowserWindow();

    /// Connect to SFTP using stored credentials and show remote contents.
    void connectAndShow();

private slots:
    void onRefresh();
    void onUpload();
    void onDownload();
    void onBack();
    void onForward();
    void onQuit();
    void onLocalUp();
    void onLocalHome();
    void onLocalRefresh();
    void onRemoteUp();
    void onRemoteHome();
    void onRemoteRefresh();
    void onLocalPathActivated(const QString& path);
    void onRemotePathActivated();
    void onLocalPathChanged();
    void onRemotePathChanged();
    void onLocalContextMenu(const QPoint& pos);
    void onRemoteContextMenu(const QPoint& pos);
    void onNewFolder();
    void updateStatusBar();

private:
    void setupUi();
    void setupMenuBar();
    void setupMainToolbar();
    void setupLocalPane();
    void setupRemotePane();
    void setupStatusBar();
    void loadCredentials();
    void connectToSftp();
    void refreshRemote();
    void refreshLocal();
    void pushRemoteHistory(const std::string& path);
    void navigateLocal(const QString& path);
    void navigateRemote(const std::string& path);
    std::string getCurrentLocalPath() const;
    std::string getCurrentRemotePath() const;
    void doUpload(const QString& localPath, const std::string& remotePath);
    void doDownload(const std::string& remotePath, const QString& localPath);
    void doDeleteRemote(int row);
    void doOpenLocal(const QString& path);
    void doModifyWith(const QString& path);
    QString formatBytes(quint64 bytes) const;
    quint64 getLocalSelectedSize() const;
    quint64 getLocalTotalSize() const;
    int getLocalSelectedCount() const;
    int getLocalTotalCount() const;
    quint64 getRemoteSelectedSize() const;
    quint64 getRemoteTotalSize() const;
    int getRemoteSelectedCount() const;
    int getRemoteTotalCount() const;

    QTreeView* localView_{nullptr};
    QTableWidget* remoteView_{nullptr};
    QComboBox* localPathCombo_{nullptr};
    QLineEdit* localPathEdit_{nullptr};
    QComboBox* remotePathCombo_{nullptr};
    QLineEdit* remotePathEdit_{nullptr};
    QToolButton* localUpBtn_{nullptr};
    QToolButton* localHomeBtn_{nullptr};
    QToolButton* localRefreshBtn_{nullptr};
    QToolButton* remoteUpBtn_{nullptr};
    QToolButton* remoteHomeBtn_{nullptr};
    QToolButton* remoteRefreshBtn_{nullptr};
    QPushButton* backBtn_{nullptr};
    QPushButton* forwardBtn_{nullptr};
    QPushButton* refreshBtn_{nullptr};
    QPushButton* uploadBtn_{nullptr};
    QPushButton* downloadBtn_{nullptr};
    QPushButton* quitBtn_{nullptr};
    QStatusBar* statusBar_{nullptr};
    QLabel* localStatusLabel_{nullptr};
    QLabel* remoteStatusLabel_{nullptr};
    QLabel* connectionStatusLabel_{nullptr};

    std::deque<std::string> remoteBackHistory_;
    std::deque<std::string> remoteForwardHistory_;

    CredentialManager credentialManager_;
    std::unique_ptr<SftpClient> sftp_;
    std::vector<DirEntry> remoteDisplayEntries_;
    bool connected_{false};
};

}  // namespace EtherMount

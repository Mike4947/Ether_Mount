#include "EtherMount/BrowserWindow.hpp"

#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QToolBar>
#include <QMessageBox>
#include <QHeaderView>
#include <QStandardPaths>
#include <QDir>
#include <QMenu>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QProcess>
#include <QApplication>
#include <QDateTime>
#include <QMenuBar>
#include <QComboBox>
#include <QToolButton>
#include <QStatusBar>
#include <QSplitter>
#include <QFrame>
#include <QSizePolicy>

namespace EtherMount {

BrowserWindow::BrowserWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("EtherMount - VPS Browser"));
    setMinimumSize(900, 550);
    resize(1100, 650);
    setupUi();
}

BrowserWindow::~BrowserWindow() {
    if (sftp_) sftp_->disconnect();
}

void BrowserWindow::setupUi() {
    setupMenuBar();
    setupMainToolbar();

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* splitter = new QSplitter(Qt::Horizontal);

    // --- Left pane: Local ---
    auto* localFrame = new QFrame(this);
    localFrame->setFrameStyle(QFrame::StyledPanel);
    auto* localLayout = new QVBoxLayout(localFrame);
    localLayout->setContentsMargins(4, 4, 4, 4);

    // Local toolbar
    auto* localToolbar = new QToolBar(this);
    localToolbar->setIconSize(QSize(20, 20));
    localToolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    localPathCombo_ = new QComboBox(this);
    localPathCombo_->setEditable(false);
    localPathCombo_->addItem(tr("Documents"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    localPathCombo_->addItem(tr("Desktop"), QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
    localPathCombo_->addItem(tr("Downloads"), QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    localPathCombo_->addItem(tr("Home"), QDir::homePath());
    localPathCombo_->setMinimumWidth(120);
    connect(localPathCombo_, QOverload<int>::of(&QComboBox::activated), this, [this](int idx) {
        QString path = localPathCombo_->itemData(idx).toString();
        if (!path.isEmpty()) navigateLocal(path);
    });

    localUpBtn_ = new QToolButton(this);
    localUpBtn_->setText(tr("Up"));
    localUpBtn_->setToolTip(tr("Parent folder"));
    connect(localUpBtn_, &QToolButton::clicked, this, &BrowserWindow::onLocalUp);

    localHomeBtn_ = new QToolButton(this);
    localHomeBtn_->setText(tr("Home"));
    localHomeBtn_->setToolTip(tr("Home directory"));
    connect(localHomeBtn_, &QToolButton::clicked, this, &BrowserWindow::onLocalHome);

    localRefreshBtn_ = new QToolButton(this);
    localRefreshBtn_->setText(tr("Refresh"));
    connect(localRefreshBtn_, &QToolButton::clicked, this, &BrowserWindow::onLocalRefresh);

    localPathEdit_ = new QLineEdit(this);
    localPathEdit_->setPlaceholderText(tr("Local path"));
    localPathEdit_->setMinimumWidth(200);
    connect(localPathEdit_, &QLineEdit::returnPressed, this, [this]() {
        QString path = localPathEdit_->text().trimmed();
        if (!path.isEmpty()) navigateLocal(path);
    });

    localToolbar->addWidget(localPathCombo_);
    localToolbar->addWidget(localUpBtn_);
    localToolbar->addWidget(localHomeBtn_);
    localToolbar->addWidget(localRefreshBtn_);
    localToolbar->addWidget(localPathEdit_);
    localLayout->addWidget(localToolbar);

    auto* localModel = new QFileSystemModel(this);
    localModel->setRootPath(QDir::rootPath());
    localView_ = new QTreeView(this);
    localView_->setModel(localModel);
    localView_->setSortingEnabled(true);
    localView_->sortByColumn(0, Qt::AscendingOrder);
    localView_->setColumnWidth(0, 200);
    localView_->setAlternatingRowColors(true);
    localView_->header()->setStretchLastSection(true);
    localView_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(localView_, &QTreeView::customContextMenuRequested, this, &BrowserWindow::onLocalContextMenu);
    connect(localView_->selectionModel(), &QItemSelectionModel::selectionChanged,
        this, [this](const QItemSelection&, const QItemSelection&) { onLocalPathChanged(); updateStatusBar(); });
    connect(localView_, &QTreeView::doubleClicked, this, [this](const QModelIndex& idx) {
        auto* model = qobject_cast<QFileSystemModel*>(localView_->model());
        if (model && model->isDir(idx)) {
            QString path = model->filePath(idx);
            navigateLocal(path);
        }
    });
    localLayout->addWidget(localView_, 1);

    splitter->addWidget(localFrame);

    // --- Right pane: Remote ---
    auto* remoteFrame = new QFrame(this);
    remoteFrame->setFrameStyle(QFrame::StyledPanel);
    auto* remoteLayout = new QVBoxLayout(remoteFrame);
    remoteLayout->setContentsMargins(4, 4, 4, 4);

    auto* remoteToolbar = new QToolBar(this);
    remoteToolbar->setIconSize(QSize(20, 20));
    remoteToolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    remotePathCombo_ = new QComboBox(this);
    remotePathCombo_->setEditable(false);
    remotePathCombo_->addItem("/", "/");
    remotePathCombo_->addItem("/home", "/home");
    remotePathCombo_->addItem("/var", "/var");
    remotePathCombo_->addItem("/var/www", "/var/www");
    remotePathCombo_->setMinimumWidth(100);
    connect(remotePathCombo_, QOverload<int>::of(&QComboBox::activated), this, [this](int idx) {
        QString path = remotePathCombo_->itemData(idx).toString();
        if (!path.isEmpty()) navigateRemote(path.toStdString());
    });

    remoteUpBtn_ = new QToolButton(this);
    remoteUpBtn_->setText(tr("Up"));
    remoteUpBtn_->setToolTip(tr("Parent folder"));
    connect(remoteUpBtn_, &QToolButton::clicked, this, &BrowserWindow::onRemoteUp);

    remoteHomeBtn_ = new QToolButton(this);
    remoteHomeBtn_->setText(tr("Home"));
    remoteHomeBtn_->setToolTip(tr("Home directory"));
    connect(remoteHomeBtn_, &QToolButton::clicked, this, &BrowserWindow::onRemoteHome);

    remoteRefreshBtn_ = new QToolButton(this);
    remoteRefreshBtn_->setText(tr("Refresh"));
    connect(remoteRefreshBtn_, &QToolButton::clicked, this, &BrowserWindow::onRemoteRefresh);

    remotePathEdit_ = new QLineEdit(this);
    remotePathEdit_->setPlaceholderText(tr("Remote path"));
    remotePathEdit_->setMinimumWidth(200);
    connect(remotePathEdit_, &QLineEdit::returnPressed, this, &BrowserWindow::onRemotePathActivated);

    remoteToolbar->addWidget(remotePathCombo_);
    remoteToolbar->addWidget(remoteUpBtn_);
    remoteToolbar->addWidget(remoteHomeBtn_);
    remoteToolbar->addWidget(remoteRefreshBtn_);
    remoteToolbar->addWidget(remotePathEdit_);
    remoteLayout->addWidget(remoteToolbar);

    remoteView_ = new QTableWidget(this);
    remoteView_->setColumnCount(5);
    remoteView_->setHorizontalHeaderLabels({tr("Name"), tr("Size"), tr("Date Modified"), tr("Permissions"), tr("Owner")});
    remoteView_->horizontalHeader()->setStretchLastSection(true);
    remoteView_->setSortingEnabled(true);
    remoteView_->setAlternatingRowColors(true);
    remoteView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    remoteView_->setSelectionMode(QAbstractItemView::SingleSelection);
    remoteView_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(remoteView_, &QTableWidget::customContextMenuRequested, this, &BrowserWindow::onRemoteContextMenu);
    connect(remoteView_->selectionModel(), &QItemSelectionModel::selectionChanged,
        this, [this](const QItemSelection&, const QItemSelection&) { updateStatusBar(); });
    connect(remoteView_, &QTableWidget::doubleClicked, this, [this](const QModelIndex& idx) {
        int row = idx.row();
        if (row < 0 || row >= remoteView_->rowCount()) return;
        auto* item = remoteView_->item(row, 0);
        if (!item) return;
        int entryIdx = item->data(Qt::UserRole).toInt();
        if (entryIdx < 0 || entryIdx >= static_cast<int>(remoteDisplayEntries_.size())) return;
        const auto& e = remoteDisplayEntries_[entryIdx];
        if (!e.is_directory) return;
        std::string base = getCurrentRemotePath();
        if (base.empty() || base == "/") base = "";
        else if (base.size() > 1 && base.back() == '/') base.pop_back();
        std::string newPath;
        if (e.name == "..") {
            size_t slash = base.find_last_of('/');
            if (slash == std::string::npos || slash == 0) newPath = "/";
            else newPath = base.substr(0, slash);
        } else {
            newPath = base.empty() ? ("/" + e.name) : (base + "/" + e.name);
        }
        pushRemoteHistory(newPath);
        remoteForwardHistory_.clear();
        navigateRemote(newPath);
    });
    remoteLayout->addWidget(remoteView_, 1);

    splitter->addWidget(remoteFrame);
    splitter->setSizes({500, 500});

    layout->addWidget(splitter);

    setupStatusBar();
    setCentralWidget(central);

    QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (docs.isEmpty()) docs = QDir::homePath();
    navigateLocal(docs);
    updateStatusBar();
}

void BrowserWindow::setupMenuBar() {
    auto* menuBar = new QMenuBar(this);
    auto* fileMenu = menuBar->addMenu(tr("&Files"));
    fileMenu->addAction(tr("&Upload"), this, &BrowserWindow::onUpload);
    fileMenu->addAction(tr("&Download"), this, &BrowserWindow::onDownload);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("New &Folder"), this, &BrowserWindow::onNewFolder);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Refresh"), this, &BrowserWindow::onRefresh);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), this, &BrowserWindow::onQuit);

    auto* editMenu = menuBar->addMenu(tr("&Edit"));
    editMenu->addAction(tr("&Delete"), this, [this]() {
        int row = remoteView_->currentRow();
        if (row >= 0) {
            auto* item = remoteView_->item(row, 0);
            if (item) doDeleteRemote(item->data(Qt::UserRole).toInt());
        }
    });

    auto* viewMenu = menuBar->addMenu(tr("&View"));
    viewMenu->addAction(tr("&Back"), this, &BrowserWindow::onBack);
    viewMenu->addAction(tr("&Forward"), this, &BrowserWindow::onForward);

    auto* helpMenu = menuBar->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About"), this, [this]() {
        QMessageBox::about(this, tr("About EtherMount"),
            tr("EtherMount - SFTP VPS Browser\n\n"
               "A WinSCP-style file manager for browsing and transferring files to your VPS via SFTP."));
    });

    setMenuBar(menuBar);
}

void BrowserWindow::setupMainToolbar() {
    auto* toolbar = addToolBar(tr("Main"));
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(22, 22));

    backBtn_ = new QPushButton(tr("← Back"), this);
    forwardBtn_ = new QPushButton(tr("Forward →"), this);
    refreshBtn_ = new QPushButton(tr("Refresh"), this);
    uploadBtn_ = new QPushButton(tr("Upload"), this);
    downloadBtn_ = new QPushButton(tr("Download"), this);
    quitBtn_ = new QPushButton(tr("Quit"), this);

    toolbar->addWidget(backBtn_);
    toolbar->addWidget(forwardBtn_);
    toolbar->addSeparator();
    toolbar->addWidget(refreshBtn_);
    toolbar->addWidget(uploadBtn_);
    toolbar->addWidget(downloadBtn_);
    toolbar->addSeparator();
    toolbar->addWidget(quitBtn_);

    connect(backBtn_, &QPushButton::clicked, this, &BrowserWindow::onBack);
    connect(forwardBtn_, &QPushButton::clicked, this, &BrowserWindow::onForward);
    connect(refreshBtn_, &QPushButton::clicked, this, &BrowserWindow::onRefresh);
    connect(uploadBtn_, &QPushButton::clicked, this, &BrowserWindow::onUpload);
    connect(downloadBtn_, &QPushButton::clicked, this, &BrowserWindow::onDownload);
    connect(quitBtn_, &QPushButton::clicked, this, &BrowserWindow::onQuit);

    backBtn_->setEnabled(false);
    forwardBtn_->setEnabled(false);
}

void BrowserWindow::setupStatusBar() {
    statusBar_ = new QStatusBar(this);
    localStatusLabel_ = new QLabel(this);
    remoteStatusLabel_ = new QLabel(this);
    connectionStatusLabel_ = new QLabel(this);

    localStatusLabel_->setMinimumWidth(180);
    remoteStatusLabel_->setMinimumWidth(180);
    connectionStatusLabel_->setStyleSheet("color: #2e7d32; font-weight: bold;");

    statusBar_->addWidget(localStatusLabel_, 1);
    statusBar_->addWidget(remoteStatusLabel_, 1);
    statusBar_->addPermanentWidget(connectionStatusLabel_);

    setStatusBar(statusBar_);
}

void BrowserWindow::navigateLocal(const QString& path) {
    QDir d(path);
    if (!d.exists()) return;
    QString canon = QDir::cleanPath(path);
    auto* model = qobject_cast<QFileSystemModel*>(localView_->model());
    if (model) {
        localView_->setRootIndex(model->index(canon));
        localPathEdit_->setText(canon);
    }
    updateStatusBar();
}

void BrowserWindow::navigateRemote(const std::string& path) {
    remotePathEdit_->setText(QString::fromStdString(path));
    refreshRemote();
}

void BrowserWindow::onLocalUp() {
    QString current = getCurrentLocalPath().c_str();
    if (current.isEmpty()) return;
    QDir d(current);
    if (d.cdUp()) navigateLocal(d.absolutePath());
}

void BrowserWindow::onLocalHome() {
    navigateLocal(QDir::homePath());
}

void BrowserWindow::onLocalRefresh() {
    refreshLocal();
}

void BrowserWindow::onRemoteUp() {
    std::string base = getCurrentRemotePath();
    if (base.empty() || base == "/") return;
    if (base.size() > 1 && base.back() == '/') base.pop_back();
    size_t slash = base.find_last_of('/');
    std::string parent = (slash == std::string::npos || slash == 0) ? "/" : base.substr(0, slash);
    pushRemoteHistory(base);
    remoteForwardHistory_.clear();
    navigateRemote(parent);
    backBtn_->setEnabled(true);
}

void BrowserWindow::onRemoteHome() {
    auto creds = credentialManager_.load();
    std::string home = creds && !creds->remotePath.empty() ? creds->remotePath : "/";
    if (home.size() > 1 && home.back() == '/') home.pop_back();
    navigateRemote(home);
}

void BrowserWindow::onRemoteRefresh() {
    if (connected_) refreshRemote();
    else loadCredentials();
}

void BrowserWindow::onLocalPathActivated(const QString& path) {
    if (!path.isEmpty()) navigateLocal(path);
}

void BrowserWindow::onRemotePathActivated() {
    std::string path = remotePathEdit_->text().trimmed().toStdString();
    if (!path.empty()) navigateRemote(path);
}

void BrowserWindow::onNewFolder() {
    if (!connected_) {
        QMessageBox::information(this, tr("New Folder"), tr("Connect first."));
        return;
    }
    QMessageBox::information(this, tr("New Folder"), tr("Create folder will be implemented in a future version."));
}

void BrowserWindow::updateStatusBar() {
    localStatusLabel_->setText(tr("%1 of %2 in %3 of %4")
        .arg(formatBytes(getLocalSelectedSize()))
        .arg(formatBytes(getLocalTotalSize()))
        .arg(getLocalSelectedCount())
        .arg(getLocalTotalCount()));
    remoteStatusLabel_->setText(tr("%1 of %2 in %3 of %4")
        .arg(formatBytes(getRemoteSelectedSize()))
        .arg(formatBytes(getRemoteTotalSize()))
        .arg(getRemoteSelectedCount())
        .arg(getRemoteTotalCount()));
    connectionStatusLabel_->setText(connected_ ? tr("SFTP-3  🔒") : tr("Not connected"));
}

QString BrowserWindow::formatBytes(quint64 bytes) const {
    if (bytes < 1024) return QString::number(bytes) + " B";
    if (bytes < 1024 * 1024) return QString::number(bytes / 1024.0, 'f', 2) + " KB";
    if (bytes < 1024ULL * 1024 * 1024) return QString::number(bytes / (1024.0 * 1024), 'f', 2) + " MB";
    return QString::number(bytes / (1024.0 * 1024 * 1024), 'f', 2) + " GB";
}

quint64 BrowserWindow::getLocalSelectedSize() const {
    auto* model = qobject_cast<QFileSystemModel*>(localView_->model());
    if (!model) return 0;
    auto idx = localView_->currentIndex();
    if (!idx.isValid()) return 0;
    QFileInfo fi(model->filePath(idx));
    return fi.isDir() ? 0 : static_cast<quint64>(fi.size());
}

quint64 BrowserWindow::getLocalTotalSize() const {
    auto* model = qobject_cast<QFileSystemModel*>(localView_->model());
    if (!model) return 0;
    QModelIndex root = localView_->rootIndex();
    if (!root.isValid()) return 0;
    quint64 total = 0;
    for (int i = 0; i < model->rowCount(root); ++i) {
        QModelIndex idx = model->index(i, 0, root);
        QFileInfo fi(model->filePath(idx));
        if (!fi.isDir()) total += static_cast<quint64>(fi.size());
    }
    return total;
}

int BrowserWindow::getLocalSelectedCount() const {
    return localView_->selectionModel()->hasSelection() ? 1 : 0;
}

int BrowserWindow::getLocalTotalCount() const {
    auto* model = qobject_cast<QFileSystemModel*>(localView_->model());
    if (!model) return 0;
    return model->rowCount(localView_->rootIndex());
}

quint64 BrowserWindow::getRemoteSelectedSize() const {
    int row = remoteView_->currentRow();
    if (row < 0) return 0;
    auto* item = remoteView_->item(row, 0);
    if (!item) return 0;
    int idx = item->data(Qt::UserRole).toInt();
    if (idx < 0 || idx >= static_cast<int>(remoteDisplayEntries_.size())) return 0;
    const auto& e = remoteDisplayEntries_[idx];
    return e.size.value_or(0);
}

quint64 BrowserWindow::getRemoteTotalSize() const {
    quint64 total = 0;
    for (const auto& e : remoteDisplayEntries_) {
        if (e.size) total += *e.size;
    }
    return total;
}

int BrowserWindow::getRemoteSelectedCount() const {
    return remoteView_->selectionModel()->hasSelection() ? 1 : 0;
}

int BrowserWindow::getRemoteTotalCount() const {
    return static_cast<int>(remoteDisplayEntries_.size());
}

void BrowserWindow::pushRemoteHistory(const std::string& path) {
    std::string current = getCurrentRemotePath();
    if (!current.empty() && current != path) {
        remoteBackHistory_.push_back(current);
        backBtn_->setEnabled(true);
    }
}

void BrowserWindow::loadCredentials() {
    auto creds = credentialManager_.load();
    if (!creds) return;
    connectToSftp();
}

void BrowserWindow::connectToSftp() {
    auto creds = credentialManager_.load();
    if (!creds) {
        connectionStatusLabel_->setText(tr("No credentials saved"));
        return;
    }

    if (!sftp_) sftp_ = std::make_unique<SftpClient>();

    if (sftp_->initialize() != Result::Success) {
        connectionStatusLabel_->setText(tr("SFTP init failed"));
        return;
    }
    if (sftp_->connect(creds->host, creds->port, creds->username, creds->password) != Result::Success) {
        connectionStatusLabel_->setText(tr("Connection failed"));
        return;
    }
    if (sftp_->initSftp() != Result::Success) {
        sftp_->disconnect();
        connectionStatusLabel_->setText(tr("SFTP init failed"));
        return;
    }

    connected_ = true;
    std::string remotePath = creds->remotePath;
    if (remotePath.empty() || remotePath == "/") remotePath = "/";
    else if (remotePath.size() > 1 && remotePath.back() == '/') remotePath.pop_back();
    remotePathEdit_->setText(QString::fromStdString(remotePath));
    remotePathEdit_->setReadOnly(false);
    refreshRemote();
    updateStatusBar();
}

void BrowserWindow::connectAndShow() {
    show();
    raise();
    activateWindow();
    loadCredentials();
}

void BrowserWindow::refreshRemote() {
    if (!sftp_ || !connected_) {
        loadCredentials();
        return;
    }

    std::string path = getCurrentRemotePath();
    if (path.empty()) path = "/";

    std::vector<DirEntry> entries;
    if (sftp_->listDirectory(path, entries) != Result::Success) {
        connectionStatusLabel_->setText(tr("List failed"));
        return;
    }

    remoteDisplayEntries_.clear();
    remoteView_->setSortingEnabled(false);
    remoteView_->setRowCount(0);

    auto addRow = [this](const std::string& name, const std::string& sizeStr,
                         const std::string& dateStr, const std::string& perms, const std::string& owner, const DirEntry& e) {
        int entryIdx = static_cast<int>(remoteDisplayEntries_.size());
        remoteDisplayEntries_.push_back(e);
        int row = remoteView_->rowCount();
        remoteView_->insertRow(row);
        auto* nameItem = new QTableWidgetItem(QString::fromStdString(name));
        nameItem->setData(Qt::UserRole, entryIdx);
        remoteView_->setItem(row, 0, nameItem);
        remoteView_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(sizeStr)));
        remoteView_->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(dateStr)));
        remoteView_->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(perms)));
        remoteView_->setItem(row, 4, new QTableWidgetItem(QString::fromStdString(owner)));
    };

    if (path != "/" && path.size() > 1) {
        DirEntry parent;
        parent.name = "..";
        parent.is_directory = true;
        addRow("..", "", "", "", "", parent);
    }

    for (const auto& e : entries) {
        if (e.name == "." || e.name == "..") continue;
        std::string name = e.name;
        if (e.is_directory) name += "/";
        std::string sizeStr = e.is_directory ? "" : (e.size ? formatBytes(*e.size).toStdString() : "");
        std::string dateStr;
        if (e.mtime) {
            QDateTime dt = QDateTime::fromSecsSinceEpoch(*e.mtime);
            dateStr = dt.toString(Qt::ISODate).toStdString();
        }
        addRow(name, sizeStr, dateStr, "rwxrwxr-x", "ubuntu", e);  // Placeholder perms/owner
    }

    remoteView_->setSortingEnabled(true);
    updateStatusBar();
}

void BrowserWindow::refreshLocal() {
    auto* model = qobject_cast<QFileSystemModel*>(localView_->model());
    if (model) {
        QModelIndex root = localView_->rootIndex();
        if (root.isValid()) model->fetchMore(root);
    }
}

void BrowserWindow::onBack() {
    if (remoteBackHistory_.empty()) return;
    std::string remotePath = remoteBackHistory_.back();
    remoteBackHistory_.pop_back();
    remoteForwardHistory_.push_back(getCurrentRemotePath());

    navigateRemote(remotePath);
    forwardBtn_->setEnabled(true);
    backBtn_->setEnabled(!remoteBackHistory_.empty());
}

void BrowserWindow::onForward() {
    if (remoteForwardHistory_.empty()) return;
    std::string remotePath = remoteForwardHistory_.back();
    remoteForwardHistory_.pop_back();
    remoteBackHistory_.push_back(getCurrentRemotePath());

    navigateRemote(remotePath);
    backBtn_->setEnabled(true);
    forwardBtn_->setEnabled(!remoteForwardHistory_.empty());
}

void BrowserWindow::onRefresh() {
    refreshLocal();
    if (connected_) refreshRemote();
    else loadCredentials();
}

void BrowserWindow::onUpload() {
    if (!connected_) {
        QMessageBox::information(this, tr("Upload"), tr("Connect first by clicking Refresh."));
        return;
    }
    auto* model = qobject_cast<QFileSystemModel*>(localView_->model());
    auto idx = localView_->currentIndex();
    if (!model || !idx.isValid()) {
        QMessageBox::information(this, tr("Upload"), tr("Select a file in the Local pane to upload."));
        return;
    }
    QString localPath = model->filePath(idx);
    QFileInfo fi(localPath);
    if (fi.isDir()) {
        QMessageBox::information(this, tr("Upload"), tr("Folder upload not yet supported. Select a file."));
        return;
    }
    std::string remotePath = getCurrentRemotePath();
    if (remotePath.empty() || remotePath == "/") remotePath = "/" + fi.fileName().toStdString();
    else remotePath += "/" + fi.fileName().toStdString();
    doUpload(localPath, remotePath);
}

void BrowserWindow::onDownload() {
    if (!connected_) {
        QMessageBox::information(this, tr("Download"), tr("Connect first by clicking Refresh."));
        return;
    }
    int row = remoteView_->currentRow();
    if (row < 0 || row >= remoteView_->rowCount()) {
        QMessageBox::information(this, tr("Download"), tr("Select a file in the Remote pane to download."));
        return;
    }
    auto* item = remoteView_->item(row, 0);
    if (!item) return;
    int entryIdx = item->data(Qt::UserRole).toInt();
    if (entryIdx < 0 || entryIdx >= static_cast<int>(remoteDisplayEntries_.size())) return;
    const auto& e = remoteDisplayEntries_[entryIdx];
    if (e.is_directory) {
        QMessageBox::information(this, tr("Download"), tr("Folder download not yet supported. Select a file."));
        return;
    }
    std::string base = getCurrentRemotePath();
    if (base.empty() || base == "/") base = "";
    else if (base.back() == '/') base.pop_back();
    std::string remotePath = base.empty() ? ("/" + e.name) : (base + "/" + e.name);
    QString localDir = getCurrentLocalPath().c_str();
    if (localDir.isEmpty()) localDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString localPath = localDir + "/" + QString::fromStdString(e.name);
    doDownload(remotePath, localPath);
}

void BrowserWindow::doUpload(const QString& localPath, const std::string& remotePath) {
    statusBar_->showMessage(tr("Uploading..."));
    QApplication::processEvents();
    Result r = sftp_->uploadFile(localPath.toStdString(), remotePath);
    if (r == Result::Success) {
        statusBar_->showMessage(tr("Upload complete."), 3000);
        refreshRemote();
    } else {
        QMessageBox::critical(this, tr("Upload Failed"), tr("Upload failed."));
        statusBar_->showMessage(tr("Upload failed."));
    }
}

void BrowserWindow::doDownload(const std::string& remotePath, const QString& localPath) {
    statusBar_->showMessage(tr("Downloading..."));
    QApplication::processEvents();
    Result r = sftp_->downloadFile(remotePath, localPath.toStdString());
    if (r == Result::Success) {
        statusBar_->showMessage(tr("Download complete."), 3000);
        refreshLocal();
    } else {
        QMessageBox::critical(this, tr("Download Failed"), tr("Download failed."));
        statusBar_->showMessage(tr("Download failed."));
    }
}

void BrowserWindow::doDeleteRemote(int entryIdx) {
    if (entryIdx < 0 || entryIdx >= static_cast<int>(remoteDisplayEntries_.size())) return;
    const auto& e = remoteDisplayEntries_[entryIdx];
    if (e.name == "..") return;

    std::string base = getCurrentRemotePath();
    if (base.empty() || base == "/") base = "";
    else if (base.back() == '/') base.pop_back();
    std::string fullPath = base.empty() ? ("/" + e.name) : (base + "/" + e.name);

    QString msg = e.is_directory
        ? tr("Delete folder \"%1\"? This will only work if the folder is empty.").arg(QString::fromStdString(e.name))
        : tr("Delete file \"%1\"?").arg(QString::fromStdString(e.name));
    if (QMessageBox::warning(this, tr("Delete"), msg, QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes) return;

    Result r = e.is_directory ? sftp_->removeDirectory(fullPath) : sftp_->removeFile(fullPath);
    if (r == Result::Success) {
        statusBar_->showMessage(tr("Deleted."), 3000);
        refreshRemote();
    } else {
        QMessageBox::critical(this, tr("Delete Failed"), tr("Delete failed."));
    }
}

void BrowserWindow::doOpenLocal(const QString& path) {
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void BrowserWindow::doModifyWith(const QString& path) {
    QFileInfo fi(path);
    if (!fi.exists()) return;
#ifdef _WIN32
    if (fi.suffix().toLower() == "txt" || fi.suffix().toLower() == "md" || fi.suffix().toLower() == "log") {
        QProcess::startDetached("notepad.exe", {path});
    } else {
        QProcess::startDetached("rundll32", {"shell32.dll,OpenAs_RunDLL", path});
    }
#else
    QProcess::startDetached("xdg-open", {path});
#endif
}

void BrowserWindow::onLocalContextMenu(const QPoint& pos) {
    auto* model = qobject_cast<QFileSystemModel*>(localView_->model());
    auto idx = localView_->indexAt(pos);
    if (!model || !idx.isValid()) return;

    QString path = model->filePath(idx);
    QFileInfo fi(path);
    bool isDir = fi.isDir();

    QMenu menu(this);
    menu.addAction(tr("Open"), this, [this, path]() { doOpenLocal(path); });
    if (!isDir) {
        menu.addAction(tr("Modify with..."), this, [this, path]() { doModifyWith(path); });
    }
    menu.addSeparator();
    auto* uploadAct = menu.addAction(tr("Upload to remote"));
    connect(uploadAct, &QAction::triggered, this, [this, path, isDir]() {
        if (!connected_) { QMessageBox::information(this, tr("Upload"), tr("Connect first.")); return; }
        if (isDir) { QMessageBox::information(this, tr("Upload"), tr("Folder upload not supported.")); return; }
        std::string remotePath = getCurrentRemotePath();
        if (remotePath.empty() || remotePath == "/") remotePath = "/";
        else if (remotePath.back() == '/') remotePath.pop_back();
        remotePath += "/" + QFileInfo(path).fileName().toStdString();
        doUpload(path, remotePath);
    });
    menu.exec(localView_->mapToGlobal(pos));
}

void BrowserWindow::onRemoteContextMenu(const QPoint& pos) {
    int row = remoteView_->indexAt(pos).row();
    if (row < 0 || row >= remoteView_->rowCount()) return;
    auto* item = remoteView_->item(row, 0);
    if (!item) return;
    int entryIdx = item->data(Qt::UserRole).toInt();
    if (entryIdx < 0 || entryIdx >= static_cast<int>(remoteDisplayEntries_.size())) return;
    const auto& e = remoteDisplayEntries_[entryIdx];
    if (e.name == "..") return;

    std::string base = getCurrentRemotePath();
    if (base.empty() || base == "/") base = "";
    else if (base.back() == '/') base.pop_back();
    std::string fullPath = base.empty() ? ("/" + e.name) : (base + "/" + e.name);

    QMenu menu(this);
    if (!e.is_directory) {
        menu.addAction(tr("Download"), this, [this, fullPath, e]() {
            QString localDir = getCurrentLocalPath().c_str();
            if (localDir.isEmpty()) localDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
            doDownload(fullPath, localDir + "/" + QString::fromStdString(e.name));
        });
    }
    menu.addAction(tr("Delete"), this, [this, entryIdx]() { doDeleteRemote(entryIdx); });
    menu.exec(remoteView_->mapToGlobal(pos));
}

void BrowserWindow::onLocalPathChanged() {
    auto* model = qobject_cast<QFileSystemModel*>(localView_->model());
    auto idx = localView_->currentIndex();
    if (model && idx.isValid()) {
        QString path = model->filePath(idx);
        if (!path.isEmpty()) localPathEdit_->setText(path);
    }
}

void BrowserWindow::onRemotePathChanged() {}

void BrowserWindow::onQuit() {
    close();
}

std::string BrowserWindow::getCurrentLocalPath() const {
    auto* model = qobject_cast<QFileSystemModel*>(localView_->model());
    if (!model) return "";
    return localView_->rootIndex().isValid()
        ? model->filePath(localView_->rootIndex()).toStdString()
        : "";
}

std::string BrowserWindow::getCurrentRemotePath() const {
    return remotePathEdit_->text().trimmed().toStdString();
}

}  // namespace EtherMount

#include "MainWindow.h"
#include "HarnessProcess.h"
#include "PluginInterface.h"
#include "PluginManager.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDebug>
#include <QDesktopServices>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QDockWidget>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QWebEngineView>
#include <QWidget>

MainWindow::MainWindow(HarnessProcess *harness, PluginManager *plugins, QWidget *parent)
    : QMainWindow(parent)
    , m_harness(harness)
    , m_plugins(plugins)
{
    setWindowTitle(QStringLiteral("DSH Desktop — DeepSeek Harness"));
    resize(1280, 820);

    // --- Central web view ---
    m_view = new QWebEngineView(this);
    setCentralWidget(m_view);

    // --- Plugin bar (left) ---
    buildPluginBar();
    buildMenus();
    buildTray();

    // --- Status bar ---
    m_statusLabel = new QLabel(this);
    statusBar()->addWidget(m_statusLabel, 1);
    const QString dshVer = m_harness->embeddedDshVersion();
    if (!dshVer.isEmpty()) {
        m_versionLabel = new QLabel(QStringLiteral("dsh %1").arg(dshVer), this);
        statusBar()->addPermanentWidget(m_versionLabel);
    }

    // --- Wire harness ---
    connect(m_harness, &HarnessProcess::ready, this, &MainWindow::onServerReady);
    connect(m_harness, &HarnessProcess::statusChanged, this, &MainWindow::onServerStatus);
    connect(m_harness, &HarnessProcess::logLine, this, &MainWindow::onServerLog);
    connect(m_harness, &HarnessProcess::failed, this, &MainWindow::onServerFailed);
    connect(m_plugins, &PluginManager::pluginLoaded, this, &MainWindow::onPluginLoaded);

    showStatus(QStringLiteral("启动 DeepSeek Harness…"));
    m_harness->start();
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Tray app: closing the window keeps running in tray unless quit was chosen.
    if (m_tray && m_tray->isVisible()) {
        hide();
        event->ignore();
        return;
    }
    event->accept();
}

// ---------------------------------------------------------------------------

void MainWindow::onServerReady()
{
    m_serverReady = true;
    showStatus(QStringLiteral("服务器就绪，加载 UI…"));
    reloadPage();
    if (m_tray) {
        m_tray->show();
        m_tray->showMessage(QStringLiteral("DSH Desktop"),
                            QStringLiteral("DeepSeek Harness 已就绪 (%1)").arg(m_harness->webUrl()),
                            QSystemTrayIcon::Information, 2500);
    }
}

void MainWindow::onServerStatus(const QString &status)
{
    showStatus(status);
}

void MainWindow::onServerLog(const QString &line)
{
    qInfo() << "[dsh]" << line;
}

void MainWindow::onServerFailed(const QString &detail)
{
    m_serverReady = false;
    showStatus(detail);
    QMessageBox::warning(this, QStringLiteral("DSH Desktop"),
                         QStringLiteral("无法启动 DeepSeek Harness：\n%1").arg(detail));
}

void MainWindow::onPluginLoaded(const QString &id, const QString &name)
{
    showStatus(QStringLiteral("插件已加载：%1 (%2)").arg(name, id));
}

// ---------------------------------------------------------------------------

void MainWindow::buildMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    fileMenu->addAction(QStringLiteral("重新加载页面"), this, &MainWindow::reloadPage, QKeySequence::Refresh);
    fileMenu->addAction(QStringLiteral("重启 Harness 服务"), this, &MainWindow::restartServer);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("退出"), this, &MainWindow::quitApp, QKeySequence::Quit);

    QMenu *viewMenu = menuBar()->addMenu(QStringLiteral("视图(&V)"));
    m_showPluginBarAction = viewMenu->addAction(QStringLiteral("显示插件面板"), this, &MainWindow::showPluginBar);
    m_hidePluginBarAction = viewMenu->addAction(QStringLiteral("隐藏插件面板"), this, &MainWindow::hidePluginBar);

    QMenu *pluginsMenu = menuBar()->addMenu(QStringLiteral("插件(&P)"));
    pluginsMenu->addAction(QStringLiteral("插件信息…"), this, [this]() {
        QMessageBox::information(this, QStringLiteral("已加载插件"),
                                 QStringLiteral("共 %1 个插件").arg(m_plugins->count()));
    });
    pluginsMenu->addAction(QStringLiteral("插件目录…"), this, [this]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(
            QCoreApplication::applicationDirPath() + QStringLiteral("/plugins")));
    });

    QMenu *helpMenu = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    helpMenu->addAction(QStringLiteral("检查 Harness 更新…"), this, &MainWindow::checkForHarnessUpdates);
    helpMenu->addAction(QStringLiteral("关于"), this, &MainWindow::showAbout);
}

void MainWindow::buildTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;

    m_tray = new QSystemTrayIcon(this);
    m_tray->setIcon(windowIcon());
    m_tray->setToolTip(QStringLiteral("DSH Desktop"));

    QMenu *menu = new QMenu(this);
    QAction *openAction = menu->addAction(QStringLiteral("显示主窗口"), this, &MainWindow::toggleTrayWindow);
    menu->addSeparator();
    menu->addAction(QStringLiteral("重启 Harness 服务"), this, &MainWindow::restartServer);
    m_plugins->populateTrayMenu(menu);
    menu->addSeparator();
    menu->addAction(QStringLiteral("退出"), this, &MainWindow::quitApp);
    m_tray->setContextMenu(menu);

    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            toggleTrayWindow();
    });
}

void MainWindow::buildPluginBar()
{
    QDockWidget *dock = new QDockWidget(QStringLiteral("插件"), this);
    dock->setObjectName(QStringLiteral("pluginDock"));
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_pluginTabs = new QTabWidget(dock);
    m_pluginTabs->setTabPosition(QTabWidget::West); // vertical tabs, compact
    m_pluginTabs->setDocumentMode(true);

    const QVariantList reg = m_plugins->registry();
    bool anyPanel = false;
    for (const QVariant &v : reg) {
        const QVariantMap m = v.toMap();
        const QString id = m.value(QStringLiteral("id")).toString();
        if (!m.value(QStringLiteral("loaded"), false).toBool())
            continue;
        QWidget *panel = m_plugins->panelFor(id);
        if (!panel)
            continue;
        m_pluginTabs->addTab(panel, m.value(QStringLiteral("name")).toString());
        anyPanel = true;
    }

    dock->setWidget(m_pluginTabs);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
    if (!anyPanel)
        dock->hide();
}

void MainWindow::reloadPage()
{
    m_view->load(QUrl(m_harness->webUrl()));
    injectSlashCommandHook();
}

// The official web UI owns its input; we add a light "slash command" hook
// by running a tiny script after load that listens for "/cmd ..." submissions.
// Real DSH commands (model, tool, etc.) are handled natively in the web UI;
// this hook only forwards unknown "/" commands to desktop plugins.
void MainWindow::injectSlashCommandHook()
{
    static const char *kScript = R"(
(() => {
  if (window.__dshDesktopHook) return;
  window.__dshDesktopHook = true;
  const fwd = (text) => {
    // forward to native side; implemented via QWebChannel would be ideal,
    // here we just route through the URL fragment as a minimal demo signal.
    window.location.hash = '#dsh-plugin:' + encodeURIComponent(text);
  };
  document.addEventListener('keydown', (e) => {
    const el = document.activeElement;
    if (!el || (el.tagName !== 'TEXTAREA' && el.tagName !== 'INPUT')) return;
    if (e.key === 'Enter' && !e.shiftKey) {
      const v = el.value.trim();
      if (v.startsWith('/') && !v.startsWith('//')) {
        // Let the app's own handler also see it; don't block default.
      }
    }
  });
  console.log('[dsh-desktop] slash hook injected');
})();
)";
    m_view->page()->runJavaScript(QString::fromLatin1(kScript));
}

void MainWindow::showStatus(const QString &msg, int timeoutMs)
{
    if (!m_statusLabel)
        return;
    m_statusLabel->setText(msg);
    statusBar()->showMessage(msg, timeoutMs);
}

void MainWindow::showPluginBar()
{
    if (m_pluginTabs && m_pluginTabs->count() > 0)
        findChild<QDockWidget *>(QStringLiteral("pluginDock"))->show();
    else
        showStatus(QStringLiteral("没有插件提供面板"));
}

void MainWindow::hidePluginBar()
{
    QDockWidget *dock = findChild<QDockWidget *>(QStringLiteral("pluginDock"));
    if (dock)
        dock->hide();
}

void MainWindow::restartServer()
{
    m_serverReady = false;
    m_harness->restart();
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, QStringLiteral("关于 DSH Desktop"),
        QStringLiteral(
            "<h3>DSH Desktop</h3>"
            "<p>DeepSeek Harness 桌面壳（Qt 6）。</p>"
            "<p>内嵌官方 <b>dsh web</b> 界面，原生插件目录：<br><code>plugins/</code>（与可执行文件同目录）。</p>"
            "<p>插件通过 <code>DshPluginInterface</code> 提供面板、托盘菜单与斜杠命令。</p>"
            "<p>TypeScript 插件（dsh bundle）位于 <code>tsplugins/</code>，"
            "由内置 Node.js 运行时加载，客户无需安装任何环境。</p>"));
}

void MainWindow::toggleTrayWindow()
{
    if (isVisible()) {
        hide();
    } else {
        show();
        raise();
        activateWindow();
    }
}

void MainWindow::quitApp()
{
    m_harness->stop();
    if (m_tray)
        m_tray->hide();
    qApp->quit();
}

// ---------------------------------------------------------------------------
// Harness self-update: run runtime/tools/update.js on the embedded node.exe.
// Step 1 here is `check`; if an update exists we confirm and run `apply`,
// then restart the harness process.

void MainWindow::checkForHarnessUpdates()
{
    if (m_updateProc) {
        showStatus(QStringLiteral("更新检查已在进行中…"));
        return;
    }
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QString nodeBin = exeDir + QStringLiteral("/runtime/node/node.exe");
    const QString script = exeDir + QStringLiteral("/runtime/tools/update.js");
    if (!QFileInfo::exists(nodeBin) || !QFileInfo::exists(script)) {
        QMessageBox::warning(this, QStringLiteral("DSH Desktop"),
                             QStringLiteral("未找到内置运行时（runtime/node、runtime/tools），无法检查更新。"));
        return;
    }

    showStatus(QStringLiteral("正在检查 DeepSeek Harness 更新…"));
    m_updateProc = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("DSH_RUNTIME_DIR"), exeDir + QStringLiteral("/runtime"));
    m_updateProc->setProcessEnvironment(env);
    connect(m_updateProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) { onUpdateCheckFinished(code); });
    m_updateProc->start(nodeBin, {script, QStringLiteral("check")});
}

void MainWindow::onUpdateCheckFinished(int exitCode)
{
    QProcess *proc = m_updateProc;
    m_updateProc = nullptr;
    if (!proc)
        return;
    proc->deleteLater();

    const QString out = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
    if (exitCode != 0) {
        showStatus(QStringLiteral("更新检查失败"));
        QMessageBox::warning(this, QStringLiteral("DSH Desktop"),
                             QStringLiteral("更新检查失败：\n%1").arg(
                                 out.isEmpty() ? QStringLiteral("exit %1").arg(exitCode) : out));
        return;
    }

    const QJsonObject obj = QJsonDocument::fromJson(out.toUtf8()).object();
    const QString current = obj.value(QStringLiteral("current")).toString();
    const QString latest = obj.value(QStringLiteral("latest")).toString();
    const bool hasUpdate = obj.value(QStringLiteral("update")).toBool();

    if (!hasUpdate) {
        showStatus(QStringLiteral("已是最新版本"));
        QMessageBox::information(this, QStringLiteral("DSH Desktop"),
                                 QStringLiteral("DeepSeek Harness 已是最新版本（%1）。").arg(current));
        return;
    }

    const auto answer = QMessageBox::question(this, QStringLiteral("发现新版本"),
        QStringLiteral("DeepSeek Harness 有新版本：\n%1 → %2\n\n是否立即更新？（更新后自动重启服务）")
            .arg(current, latest));
    if (answer != QMessageBox::Yes)
        return;

    // apply
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QString nodeBin = exeDir + QStringLiteral("/runtime/node/node.exe");
    const QString script = exeDir + QStringLiteral("/runtime/tools/update.js");

    m_updateProc = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("DSH_RUNTIME_DIR"), exeDir + QStringLiteral("/runtime"));
    m_updateProc->setProcessEnvironment(env);
    m_updateProc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_updateProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, latest](int code, QProcess::ExitStatus) {
                QProcess *p = m_updateProc;
                m_updateProc = nullptr;
                if (!p) return;
                const QString log = QString::fromUtf8(p->readAllStandardOutput()).trimmed();
                p->deleteLater();
                if (code == 0) {
                    showStatus(QStringLiteral("Harness 已更新到 %1，正在重启…").arg(latest));
                    if (m_versionLabel)
                        m_versionLabel->setText(QStringLiteral("dsh %1").arg(latest));
                    restartServer();
                } else {
                    showStatus(QStringLiteral("更新失败"));
                    QMessageBox::warning(this, QStringLiteral("DSH Desktop"),
                                         QStringLiteral("更新失败：\n%1").arg(log.left(2000)));
                }
            });
    showStatus(QStringLiteral("正在下载并安装 %1（可能需要几分钟）…").arg(latest));
    m_updateProc->start(nodeBin, {script, QStringLiteral("apply"), latest});
}

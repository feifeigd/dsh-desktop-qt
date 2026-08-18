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
#include <QWebEnginePage>
#include <QWidget>

// QWebEnginePage::javaScriptConsoleMessage is protected in Qt 6; subclass it
// to observe page console output (used by the slash-command bridge).
class ConsolePage : public QWebEnginePage {
    Q_OBJECT
public:
    using QWebEnginePage::QWebEnginePage;
signals:
    void consoleMessage(const QString &message);
protected:
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level,
                                  const QString &message, int lineNumber,
                                  const QString &sourceID) override
    {
        Q_UNUSED(level); Q_UNUSED(lineNumber); Q_UNUSED(sourceID);
        emit consoleMessage(message);
    }
};

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

    // --- Web console -> native bridge -------------------------------------
    // The injected slash hook (see injectSlashCommandHook) routes "/cmd"
    // typed in the chat through console.log("\u0001dsh:" + encoded). We pick
    // it up here, dispatch it to the plugins, and push the reply back into
    // the page via window.__dshPluginReply(). Page console output is also
    // mirrored into the app log file (verification aid).
    auto *consolePage = new ConsolePage(m_view);
    m_view->setPage(consolePage);
    connect(consolePage, &ConsolePage::consoleMessage, this,
            [this](const QString &message) {
                qInfo() << "[web]" << message;
                const QString kPrefix = QStringLiteral("\u0001dsh:");
                if (message.startsWith(kPrefix))
                    dispatchPluginCommand(QUrl::fromPercentEncoding(message.mid(kPrefix.size()).toUtf8()));
            });

    // Navigation destroys the JS context; re-inject the hook after every
    // page load (load() returns immediately, so inject-after-load() is racy).
    connect(m_view, &QWebEngineView::loadFinished, this, [this](bool ok) {
        if (ok)
            injectSlashCommandHook();
    });

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
    // Hook re-injection happens on loadFinished (see constructor); calling
    // it right after load() would run against the stale JS context.
    m_view->load(QUrl(m_harness->webUrl()));
}

// The official web UI owns its input; we add a light "slash command" hook
// by running a tiny script after load that routes "/cmd ..." submissions to
// desktop plugins. Real DSH commands (model, tool, etc.) are handled natively
// in the web UI; this hook only forwards "/" commands that plugins handle.
//
// Channel: page -> native is console.log("\u0001dsh:" + encodeURIComponent(text)),
// consumed in the constructor via javaScriptConsoleMessage. Native -> page is
// window.__dshPluginReply(<text>) delivered via runJavaScript.
void MainWindow::injectSlashCommandHook()
{
    static const char *kScript = R"(
(() => {
  if (window.__dshDesktopHook) return;
  window.__dshDesktopHook = true;

  window.__dshUnhandled = window.__dshUnhandled || new Set();

  // Native replies here. A non-empty reply replaces the command in the input
  // (user can then Enter to send the result to the chat, or copy it). An
  // empty reply marks the command unhandled: the next Enter on the same text
  // passes through to the app's own handler.
  window.__dshPluginReply = function (reply) {
    var el = document.activeElement;
    if (!el || (el.tagName !== 'TEXTAREA' && el.tagName !== 'INPUT' && !el.isContentEditable)) {
      // after a real mouse click the focus is on the menu button, not the
      // input - fall back to the chat input so the reply is not dropped
      el = document.querySelector('textarea, input[type="text"], [contenteditable="true"]');
    }
    if (!el) return;
    if (reply && reply.length) {
      if (el.isContentEditable) el.textContent = reply;
      else el.value = reply;
      el.dispatchEvent(new Event('input', { bubbles: true }));
      el.focus();
    } else {
      var v = el.value !== undefined ? el.value.trim() : el.textContent.trim();
      if (v) window.__dshUnhandled.add(v);
    }
  };

  // Capture phase: runs BEFORE the app's own key handlers, so "/cmd" text can
  // be routed to desktop plugins instead of being sent to the model.
  document.addEventListener('keydown', function (e) {
    var el = document.activeElement;
    if (!el || (el.tagName !== 'TEXTAREA' && el.tagName !== 'INPUT' && !el.isContentEditable)) return;
    if (e.key !== 'Enter' || e.shiftKey) return;
    var v = el.value !== undefined ? el.value.trim() : el.textContent.trim();
    if (!v.startsWith('/') || v.startsWith('//')) return;
    if (window.__dshUnhandled.has(v)) return; // tried before, let the app handle it
    e.preventDefault();
    e.stopPropagation();
    console.log('\u0001dsh:' + encodeURIComponent(v));
  }, true);

  // --- Plugin commands in the native slash menu ---------------------------
  // The harness web UI renders its own "/" suggestion listbox; we can't
  // register into its internal command registry, so append our plugin
  // commands to the rendered listbox instead. Selectors are stable
  // (role/aria-label); item classes are cloned from a live item so hashed
  // class names (harness build-specific) never need to be hardcoded.
  var runPluginCmd = function (text) {
    console.log('\u0001dsh:' + encodeURIComponent(text));
  };
  var ensureMenuItems = function () {
    var listbox = document.querySelector('[role="listbox"][aria-label="触发候选建议"]');
    if (!listbox) return;
    var viewport = listbox.querySelector('[class*="viewport"]') || listbox;
    var sample = listbox.querySelector('[role="option"]');
    if (!sample) return;                     // items not rendered yet; retry on next mutation
    if (viewport.querySelector('button[data-dsh-plugin-cmd="sysinfo"]')) return; // already injected for this render
    var btn = document.createElement('button');
    btn.type = 'button';
    btn.role = 'option';
    btn.className = (sample.className || '').replace(/\s*_3e4SsG_active\s*/, '');
    btn.dataset.dshPluginCmd = 'sysinfo';
    btn.title = 'DSH Desktop 插件命令';
    var name = document.createElement('span');
    name.textContent = 'sysinfo';
    var desc = document.createElement('span');
    desc.textContent = '插件：系统信息（CPU/内存）';
    if (sample.children.length > 0) name.className = sample.children[0].className;
    if (sample.children.length > 1) desc.className = sample.children[1].className;
    btn.appendChild(name);
    btn.appendChild(desc);
    var fire = function (ev) {
      if (ev) { ev.preventDefault(); ev.stopPropagation(); }
      runPluginCmd('/sysinfo');
    };
    btn.addEventListener('click', fire);
    btn.addEventListener('keydown', function (e) {
      if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); e.stopPropagation(); runPluginCmd('/sysinfo'); }
    });
    viewport.appendChild(btn);
  };
  // The menu listbox is a persistent element (React toggles visibility instead
  // of re-inserting), so insertion observers miss it. Poll cheaply instead;
  // ensureMenuItems is idempotent and re-injects if React wipes our item.
  setInterval(ensureMenuItems, 500);
  ensureMenuItems(); // menu may already be open (restored draft) - inject now

  console.log('[dsh-desktop] slash hook injected v3');
})();
)";
    // UTF-8: the script contains non-ASCII literals (Chinese aria-label
    // selector, item text); fromLatin1 would mojibake them and the menu
    // injection would silently never match.
    m_view->page()->runJavaScript(QString::fromUtf8(kScript));
}

void MainWindow::dispatchPluginCommand(const QString &text)
{
    if (!text.startsWith(QLatin1Char('/')))
        return;

    // text is "/cmd arg1 arg2 ..." (already percent-decoded).
    QString cmd, args;
    const QString body = text.mid(1); // strip leading '/'
    const int sp = body.indexOf(QLatin1Char(' '));
    if (sp > 0) {
        cmd = body.left(sp);
        args = body.mid(sp + 1);
    } else {
        cmd = body;
    }

    const QString reply = m_plugins->runCommand(cmd, args);

    // Always deliver a reply (even empty) so the JS side can mark the command
    // as unhandled. Escape into a safe JS string literal (JSON-style).
    QString esc = reply;
    esc.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    esc.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    esc.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    esc.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    esc.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
    const QString js = QStringLiteral(
        "if (window.__dshPluginReply) window.__dshPluginReply(\"%1\");").arg(esc);
    m_view->page()->runJavaScript(js);

    if (!reply.isEmpty()) {
        qInfo() << "[plugin] /" << cmd << "handled, reply" << reply.size() << "chars";
    } else {
        qInfo() << "[plugin] /" << cmd << "not handled by any plugin";
        showStatus(QStringLiteral("没有插件处理 /%1").arg(cmd));
    }
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

#include "MainWindow.moc"

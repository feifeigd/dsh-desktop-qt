#pragma once

#include <QMainWindow>

class HarnessProcess;
class PluginManager;
class QWebEngineView;
class QSystemTrayIcon;
class QTabWidget;
class QLabel;
class QMenu;
class QAction;
class QProcess;

// Main shell window:
//   - center: QWebEngineView loading the official DSH web UI
//   - left:   plugin panel bar (native Qt widgets contributed by plugins)
//   - tray:   system tray icon (show/hide, start/stop server, quit)
//   - status bar: server status + plugin count
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(HarnessProcess *harness, PluginManager *plugins, QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onServerReady();
    void onServerStatus(const QString &status);
    void onServerLog(const QString &line);
    void onServerFailed(const QString &detail);
    void onPluginLoaded(const QString &id, const QString &name);

    void showPluginBar();
    void hidePluginBar();
    void reloadPage();
    void restartServer();
    void showAbout();
    void toggleTrayWindow();
    void quitApp();
    void checkForHarnessUpdates();
    void onUpdateCheckFinished(int exitCode);


private:
    void buildMenus();
    void buildTray();
    void buildPluginBar();
    void injectSlashCommandHook();
    void showStatus(const QString &msg, int timeoutMs = 5000);

    HarnessProcess *m_harness;
    PluginManager *m_plugins;
    QWebEngineView *m_view = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    QTabWidget *m_pluginTabs = nullptr;
    QLabel *m_statusLabel = nullptr;
    QAction *m_showPluginBarAction = nullptr;
    QAction *m_hidePluginBarAction = nullptr;
    QLabel *m_versionLabel = nullptr;
    QProcess *m_updateProc = nullptr;
    bool m_serverReady = false;
};

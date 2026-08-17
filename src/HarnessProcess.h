#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

class QNetworkAccessManager;
class QTimer;

// Manages the embedded DeepSeek Harness server.
//
// Embedded (shipping) layout — customers install NOTHING else:
//
//   <exe_dir>/runtime/node/node.exe                     portable Node.js
//   <exe_dir>/runtime/dsh/node_modules/@deepseek-ai/dsh the harness itself
//   <exe_dir>/runtime/dsh/node_modules/pnpm             bundled pnpm (updates)
//   <exe_dir>/runtime/tools/provision.js                first-run provisioner
//   <exe_dir>/runtime/tools/update.js                   dsh self-updater
//   <exe_dir>/tsplugins/<pkg>/                          shipped TS plugin demos
//
// Runtime data (profiles, sessions) live in %APPDATA%/DSH/DSHDesktop
// (DSH_HOME), never in the install dir, so updates never touch user data.
//
// Flow: provision.js (ensures web profile + injects shipped TS plugins)
//    -> node.exe .../dsh/lib/bin.js web --port <port>
//    -> poll http://127.0.0.1:<port> until it answers -> ready()
class HarnessProcess : public QObject {
    Q_OBJECT
public:
    explicit HarnessProcess(QObject *parent = nullptr);
    ~HarnessProcess() override;

    void start();
    void stop();
    void restart();

    bool isRunning() const { return m_proc && m_proc->state() != QProcess::NotRunning; }
    int webPort() const { return m_webPort; }
    QString errorString() const { return m_error; }
    QString webUrl() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_webPort); }

    // Embedded runtime present? (node.exe + dsh bin.js)
    bool runtimePresent() const;
    // Human-readable embedded dsh version (reads package.json), or empty.
    QString embeddedDshVersion() const;
    // %APPDATA%/DSH/DSHDesktop (DSH_HOME)
    QString dshHome() const;

signals:
    void ready();
    void statusChanged(const QString &status);
    void logLine(const QString &line);
    void failed(const QString &detail);

private slots:
    void onProcessOutput();
    void onProcessError(QProcess::ProcessError error);
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void pollReadiness();

private:
    void startInternal();
    bool provision();
    bool isPortListening(int port) const;
    QString findSystemDsh() const;   // dev fallback when runtime/ is absent
    int pickFreePort() const;
    void setError(const QString &e);

    QProcess *m_proc = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    QTimer *m_readyTimer = nullptr;
    int m_webPort = 3080;
    QString m_error;
    bool m_restarting = false;
    bool m_readyEmitted = false;
};

#include "HarnessProcess.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

static const int kDefaultPort = 3080;
static const int kMaxPortProbe = 20;

HarnessProcess::HarnessProcess(QObject *parent)
    : QObject(parent)
    , m_proc(new QProcess(this))
    , m_nam(new QNetworkAccessManager(this))
    , m_readyTimer(new QTimer(this))
    , m_webPort(kDefaultPort)
{
    connect(m_proc, &QProcess::readyReadStandardOutput, this, &HarnessProcess::onProcessOutput);
    connect(m_proc, &QProcess::readyReadStandardError, this, &HarnessProcess::onProcessOutput);
    connect(m_proc, &QProcess::errorOccurred, this, &HarnessProcess::onProcessError);
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &HarnessProcess::onProcessFinished);
    m_readyTimer->setInterval(500);
    connect(m_readyTimer, &QTimer::timeout, this, &HarnessProcess::pollReadiness);
}

HarnessProcess::~HarnessProcess()
{
    stop();
}

// ---------------------------------------------------------------------------
// Paths

QString HarnessProcess::dshHome() const
{
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    return appData + QStringLiteral("/DSH/DSHDesktop");
}

bool HarnessProcess::runtimePresent() const
{
    const QString exeDir = QCoreApplication::applicationDirPath();
    return QFileInfo::exists(exeDir + QStringLiteral("/runtime/node/node.exe"))
        && QFileInfo::exists(exeDir + QStringLiteral("/runtime/dsh/node_modules/@deepseek-ai/dsh/lib/bin.js"));
}

QString HarnessProcess::embeddedDshVersion() const
{
    const QString pkg = QCoreApplication::applicationDirPath()
        + QStringLiteral("/runtime/dsh/node_modules/@deepseek-ai/dsh/package.json");
    QFile f(pkg);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    return o.value(QStringLiteral("version")).toString();
}

// ---------------------------------------------------------------------------

void HarnessProcess::start()
{
    if (isRunning()) {
        emit statusChanged(QStringLiteral("already running"));
        return;
    }
    if (isPortListening(m_webPort)) {
        emit statusChanged(QStringLiteral("server already running on port %1 (adopted)").arg(m_webPort));
        m_readyEmitted = true;
        emit ready();
        return;
    }
    startInternal();
}

void HarnessProcess::restart()
{
    m_restarting = true;
    stop();
    m_restarting = false;
    m_readyEmitted = false;
    start();
}

void HarnessProcess::stop()
{
    m_readyTimer->stop();
    if (!m_proc || m_proc->state() == QProcess::NotRunning)
        return;
    // dsh treats SIGTERM as a graceful drain (exits 0 within ~5s).
    m_proc->terminate();
    if (!m_proc->waitForFinished(8000))
        m_proc->kill();
}

void HarnessProcess::startInternal()
{
    const QString exeDir = QCoreApplication::applicationDirPath();

    QString nodeBin;
    QString dshBin;

    if (runtimePresent()) {
        // ---- Embedded mode: no Node.js required on the customer machine ----
        nodeBin = exeDir + QStringLiteral("/runtime/node/node.exe");
        dshBin = exeDir + QStringLiteral("/runtime/dsh/node_modules/@deepseek-ai/dsh/lib/bin.js");
        emit statusChanged(QStringLiteral("provisioning embedded harness…"));
        if (!provision()) {
            emit failed(m_error);
            return;
        }
    } else {
        // ---- Dev fallback: use system `dsh` / `npx` if someone builds the
        // shell without the embedded runtime. Real installers always embed.
        const QString sys = findSystemDsh();
        if (sys.isEmpty()) {
            setError(QStringLiteral("未找到内置运行时（runtime/node、runtime/dsh），"
                                    "且系统也没有 dsh/npx。请重新完整安装 DSH Desktop。"));
            emit failed(m_error);
            return;
        }
        nodeBin.clear();
        dshBin = sys;
    }

    // Pick a free port (3080, then probe up).
    m_webPort = pickFreePort();
    m_error.clear();
    m_readyEmitted = false;

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("DSH_HOME"), dshHome());
    // Force the embedded runtime to ignore any stray user PATH node/pnpm.
    if (!nodeBin.isEmpty()) {
        const QString runtimeDir = exeDir + QStringLiteral("/runtime");
        env.insert(QStringLiteral("DSH_DESKTOP_EMBEDDED"), QStringLiteral("1"));
        env.insert(QStringLiteral("NODE_NO_WARNINGS"), QStringLiteral("1"));
        qputenv("DSH_DESKTOP_EMBEDDED", "1"); // visible to child too
    }
    m_proc->setProcessEnvironment(env);
    m_proc->setWorkingDirectory(QDir::homePath());

    QStringList args;
    if (!nodeBin.isEmpty()) {
        m_proc->setProgram(nodeBin);
        args << dshBin << QStringLiteral("web") << QStringLiteral("--port") << QString::number(m_webPort);
    } else if (dshBin.endsWith(QStringLiteral("npx.cmd")) || dshBin.endsWith(QStringLiteral("npx.exe"))
               || dshBin.endsWith(QStringLiteral("npx"))) {
        m_proc->setProgram(dshBin);
        args << QStringLiteral("--yes") << QStringLiteral("@deepseek-ai/dsh") << QStringLiteral("web")
             << QStringLiteral("--port") << QString::number(m_webPort);
    } else {
        m_proc->setProgram(dshBin);
        args << QStringLiteral("web") << QStringLiteral("--port") << QString::number(m_webPort);
    }

    qInfo() << "[harness] launching:" << m_proc->program() << args.join(' ')
            << "DSH_HOME=" << dshHome();
    emit statusChanged(QStringLiteral("启动 DeepSeek Harness（端口 %1）…").arg(m_webPort));
    m_proc->start();
}

// First-run / every-start provisioning with the embedded runtime:
//   runtime/tools/provision.js ensures the web profile exists and copies the
//   shipped TS plugin packages from <exe>/tsplugins into it. Runs node once,
//   synchronously (few seconds worst case), before the server starts.
bool HarnessProcess::provision()
{
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QString nodeBin = exeDir + QStringLiteral("/runtime/node/node.exe");
    const QString script = exeDir + QStringLiteral("/runtime/tools/provision.js");
    if (!QFileInfo::exists(script)) {
        setError(QStringLiteral("缺少运行时脚本 runtime/tools/provision.js（安装不完整）。"));
        return false;
    }

    QDir().mkpath(dshHome());

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("DSH_HOME"), dshHome());
    env.insert(QStringLiteral("DSH_RUNTIME_DIR"), exeDir + QStringLiteral("/runtime"));

    QProcess p;
    p.setProcessEnvironment(env);
    p.setWorkingDirectory(QDir::homePath());
    p.start(nodeBin, {script});
    if (!p.waitForStarted(5000)) {
        setError(QStringLiteral("无法启动内置 Node.js 进行初始化。"));
        return false;
    }
    if (!p.waitForFinished(120000)) {
        p.kill();
        setError(QStringLiteral("初始化超时（120s）。"));
        return false;
    }
    const QString out = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
    const QString errOut = QString::fromUtf8(p.readAllStandardError()).trimmed();
    if (!out.isEmpty())
        emit logLine(QStringLiteral("[provision] ") + out);
    if (p.exitCode() != 0) {
        setError(QStringLiteral("初始化失败：%1").arg(
            errOut.isEmpty() ? QStringLiteral("exit %1").arg(p.exitCode()) : errOut));
        return false;
    }
    return true;
}

bool HarnessProcess::isPortListening(int port) const
{
    QTcpSocket probe;
    probe.connectToHost(QHostAddress::LocalHost, port);
    return probe.waitForConnected(300);
}

int HarnessProcess::pickFreePort() const
{
    // Prefer the default; if taken (by an external dsh we do NOT want to
    // adopt), find the first free port above it.
    if (!isPortListening(kDefaultPort))
        return kDefaultPort;
    QTcpServer srv;
    for (int p = kDefaultPort + 1; p < kDefaultPort + kMaxPortProbe; ++p) {
        if (!isPortListening(p))
            return p;
    }
    // Last resort: let the OS choose.
    srv.listen(QHostAddress::LocalHost, 0);
    const int chosen = srv.serverPort();
    srv.close();
    return chosen;
}

QString HarnessProcess::findSystemDsh() const
{
#ifdef Q_OS_WIN
    const QStringList candidates = {
        QStringLiteral("dsh.cmd"), QStringLiteral("dsh.exe"),
        QStringLiteral("npx.cmd"), QStringLiteral("npx.exe"),
    };
    QStringList paths = qEnvironmentVariable("PATH").split(';', Qt::SkipEmptyParts);
    paths << QDir::homePath() + QStringLiteral("/AppData/Roaming/npm");
    for (const QString &cand : candidates) {
        for (const QString &dir : paths) {
            const QString full = QDir(dir).filePath(cand);
            if (QFileInfo::exists(full))
                return full;
        }
    }
    return QString();
#else
    QProcess which;
    which.start(QStringLiteral("dsh"), {QStringLiteral("--version")});
    which.waitForFinished(1500);
    if (which.exitCode() == 0)
        return QStringLiteral("dsh");
    QProcess npx;
    npx.start(QStringLiteral("npx"), {QStringLiteral("--version")});
    npx.waitForFinished(1500);
    if (npx.exitCode() == 0)
        return QStringLiteral("npx");
    return QString();
#endif
}

// ---------------------------------------------------------------------------

void HarnessProcess::pollReadiness()
{
    if (m_readyEmitted) {
        m_readyTimer->stop();
        return;
    }
    // HTTP probe: dsh web answers on / once the UI is served.
    QNetworkRequest req;
    req.setUrl(QUrl(webUrl()));
    req.setTransferTimeout(2000);
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (m_readyEmitted)
            return;
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError || code >= 200 && code < 400) {
            m_readyEmitted = true;
            m_readyTimer->stop();
            emit statusChanged(QStringLiteral("就绪"));
            emit ready();
        }
    });
}

void HarnessProcess::onProcessOutput()
{
    if (!m_proc)
        return;
    const QByteArray out = m_proc->readAllStandardOutput();
    const QByteArray err = m_proc->readAllStandardError();
    if (!out.isEmpty())
        emit logLine(QString::fromUtf8(out).trimmed());
    if (!err.isEmpty())
        emit logLine(QString::fromUtf8(err).trimmed());

    // Start HTTP readiness polling as soon as the process is alive; the port
    // probe is authoritative, log sniffing only gives early status text.
    if (m_proc->state() == QProcess::Running && !m_readyTimer->isActive() && !m_readyEmitted)
        m_readyTimer->start();
}

void HarnessProcess::onProcessError(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart) {
        setError(QStringLiteral("进程启动失败：%1").arg(m_proc->errorString()));
        m_readyTimer->stop();
        emit failed(m_error);
    }
}

void HarnessProcess::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    m_readyTimer->stop();
    emit statusChanged(QStringLiteral("dsh 已退出（代码 %1）").arg(exitCode));
    if (status == QProcess::CrashExit && !m_restarting)
        emit logLine(QStringLiteral("dsh 崩溃；可通过菜单重新启动。"));
}

void HarnessProcess::setError(const QString &e)
{
    m_error = e;
    qWarning() << "[harness]" << e;
}

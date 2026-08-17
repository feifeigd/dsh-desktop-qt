#include "HarnessProcess.h"
#include "MainWindow.h"
#include "PluginManager.h"

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QIcon>
#include <QLockFile>
#include <QMessageBox>
#include <QMutex>
#include <QPixmap>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTextStream>
#include <QtGlobal>

// Release builds ship WITHOUT a console window (WIN32 subsystem), so qDebug
// output goes to a rotating log file under %APPDATA%/DSH/DSHDesktop/logs.
static QFile g_logFile;
static QMutex g_logMutex;

static void fileLogHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    Q_UNUSED(ctx);
    QMutexLocker lock(&g_logMutex);
    if (!g_logFile.isOpen())
        return;
    static const char *kLevel[] = { "D", "W", "C", "F", "I" };
    QTextStream ts(&g_logFile);
    ts << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
       << " [" << kLevel[type] << "] " << msg << "\n";
    ts.flush();
}

int main(int argc, char *argv[])
{
    // Single-instance: multiple copies writing the same log / starting the same
    // embedded dsh on the same port would corrupt state. Lock in %APPDATA% and
    // keep the QLockFile alive for the whole process (static, never released).
    {
        static QLockFile *s_lock = nullptr;
        const QString lockPath = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QStringLiteral("/DSH/DSHDesktop/app.lock");
        QDir().mkpath(QFileInfo(lockPath).absolutePath());
        s_lock = new QLockFile(lockPath);
        s_lock->setStaleLockTime(0); // never auto-steal; owner holds it for life
        if (!s_lock->tryLock(100)) {
            QMessageBox::warning(nullptr, QStringLiteral("DSH Desktop"),
                                 QStringLiteral("DSH Desktop 已在运行。\n请从系统托盘打开现有窗口。"));
            return 1;
        }
    }
    {
        const QString logDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QStringLiteral("/DSH/DSHDesktop/logs");
        QDir().mkpath(logDir);
        g_logFile.setFileName(logDir + QStringLiteral("/dsh-desktop.log"));
        if (g_logFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            qInstallMessageHandler(fileLogHandler);
    }

    // WebEngine on Windows needs these flags for proper GPU/software rendering
    // in mixed environments (RDP, VMs, older GPUs).
    qputenv("QT_OPENGL", "auto");
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("DSHDesktop"));
    app.setApplicationDisplayName(QStringLiteral("DSH Desktop"));
    app.setOrganizationName(QStringLiteral("DSH"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setQuitOnLastWindowClosed(false); // tray app

    QIcon icon;
    if (icon.isNull()) {
        // Fallback: simple colored square so tray/window aren't blank.
        QPixmap pm(64, 64);
        pm.fill(QColor(77, 107, 254));
        icon = QIcon(pm);
    }
    app.setWindowIcon(icon);

    PluginManager plugins;
    plugins.scan();

    HarnessProcess harness;
    MainWindow win(&harness, &plugins);
    win.show();

    return app.exec();
}

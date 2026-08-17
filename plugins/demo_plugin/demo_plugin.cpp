#include "demo_plugin.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

SysInfoPlugin::SysInfoPlugin()
{
    m_timer.setInterval(2000);
    connect(&m_timer, &QTimer::timeout, this, &SysInfoPlugin::refresh);
}

SysInfoPlugin::~SysInfoPlugin() = default;

QString SysInfoPlugin::collectStats()
{
    // Windows: use wmic-ish via systeminfo is slow; use powershell one-liners
    // that are cheap. Fall back to env/static info if anything fails.
    QString out = QStringLiteral("DSH Desktop demo plugin\n\n");

#ifdef Q_OS_WIN
    QProcess ps;
    ps.start(QStringLiteral("powershell.exe"), {
        QStringLiteral("-NoProfile"), QStringLiteral("-Command"),
        QStringLiteral("$os=Get-CimInstance Win32_OperatingSystem;"
                       "$cpu=(Get-CimInstance Win32_Processor).LoadPercentage;"
                       "$m=Get-CimInstance Win32_OperatingSystem;"
                       "Write-Output ('CPU%: {0}%' -f $cpu);"
                       "Write-Output ('MEM: {0:N1}/{1:N1} GB' -f (($m.TotalVisibleMemorySize-$m.FreePhysicalMemory)/1MB),(($m.TotalVisibleMemorySize)/1MB))")
    });
    if (!ps.waitForFinished(4000)) {
        ps.kill();
        ps.waitForFinished(2000);
    }
    out += QString::fromLocal8Bit(ps.readAllStandardOutput()).trimmed();
    if (out.endsWith(QLatin1String("DSH Desktop demo plugin\n\n")))
        out += QStringLiteral("(无法读取系统统计)");
#else
    QProcess ps;
    ps.start(QStringLiteral("sh"), {QStringLiteral("-c"),
            QStringLiteral("echo -n 'CPU: '; top -bn1 | grep 'Cpu(s)' | head -1; "
                           "echo -n 'MEM: '; free -h | awk 'NR==2{print $3\"/\"$2}'")});
    if (!ps.waitForFinished(4000)) {
        ps.kill();
        ps.waitForFinished(2000);
    }
    out += QString::fromUtf8(ps.readAllStandardOutput()).trimmed();
#endif
    return out;
}

void SysInfoPlugin::refresh()
{
    if (m_label)
        m_label->setText(collectStats());
}

QWidget *SysInfoPlugin::createPanel(QWidget *parent)
{
    auto *w = new QWidget(parent);
    auto *lay = new QVBoxLayout(w);

    m_label = new QLabel(w);
    m_label->setWordWrap(true);
    m_label->setTextFormat(Qt::PlainText);
    m_label->setText(collectStats());
    lay->addWidget(m_label);

    auto *refreshBtn = new QPushButton(QStringLiteral("刷新"), w);
    connect(refreshBtn, &QPushButton::clicked, this, &SysInfoPlugin::refresh);
    lay->addWidget(refreshBtn);

    auto *copyBtn = new QPushButton(QStringLiteral("复制到剪贴板"), w);
    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_label ? m_label->text() : QString());
    });
    lay->addWidget(copyBtn);

    lay->addStretch(1);
    m_timer.start();
    return w;
}

void SysInfoPlugin::populateTrayMenu(QMenu *menu)
{
    if (!menu)
        return;
    QAction *act = menu->addAction(QStringLiteral("System Info…"));
    connect(act, &QAction::triggered, this, [this]() {
        QMessageBox::information(nullptr, QStringLiteral("System Info"), collectStats());
    });
}

QString SysInfoPlugin::onCommand(const QString &command, const QString &args)
{
    Q_UNUSED(args);
    if (command == QLatin1String("sysinfo"))
        return collectStats();
    return QString();
}

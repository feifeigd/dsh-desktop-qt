#pragma once

#include "PluginInterface.h"

#include <QLabel>
#include <QTimer>
#include <QWidget>

// Demo plugin: system info panel + tray action + /sysinfo slash command.
class SysInfoPlugin : public QObject, public DshPluginInterface {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DshPluginInterface_iid)
    Q_INTERFACES(DshPluginInterface)
public:
    SysInfoPlugin();
    ~SysInfoPlugin() override;

    QString id() const override { return QStringLiteral("com.example.sysinfo"); }
    QString name() const override { return QStringLiteral("System Info"); }
    QString version() const override { return QStringLiteral("0.1.0"); }
    QString description() const override { return QStringLiteral("Shows CPU/mem/disk stats in a native panel."); }

    QWidget *createPanel(QWidget *parent) override;
    void populateTrayMenu(QMenu *menu) override;
    QString onCommand(const QString &command, const QString &args) override;

private:
    void refresh();
    static QString collectStats();

    QLabel *m_label = nullptr;
    QTimer m_timer;
};

#include "PluginManager.h"
#include "PluginInterface.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QPluginLoader>

PluginManager::PluginManager(QObject *parent)
    : QObject(parent)
{
}

void PluginManager::scan()
{
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QString pluginsDir = exeDir + QStringLiteral("/plugins");

    QDir dir(pluginsDir);
    if (!dir.exists()) {
        qWarning() << "[plugins] directory does not exist:" << pluginsDir;
        return;
    }

    const QStringList dlls = dir.entryList({QStringLiteral("*.dll")}, QDir::Files, QDir::Name);
    if (dlls.isEmpty()) {
        qInfo() << "[plugins] no plugins found in" << pluginsDir;
        return;
    }

    for (const QString &dll : dlls) {
        Entry e;
        e.path = dir.absoluteFilePath(dll);

        QPluginLoader loader(e.path);
        QObject *obj = loader.instance();
        if (!obj) {
            e.error = loader.errorString();
            qWarning() << "[plugins] failed to load" << dll << ":" << e.error;
            m_plugins.append(e);
            continue;
        }

        auto *iface = qobject_cast<DshPluginInterface *>(obj);
        if (!iface) {
            e.error = QStringLiteral("does not implement DshPluginInterface");
            qWarning() << "[plugins]" << dll << e.error;
            loader.unload();
            m_plugins.append(e);
            continue;
        }

        e.iface = iface;
        e.obj = obj;
        iface->onLoad();
        emit pluginLoaded(iface->id(), iface->name());
        m_plugins.append(e);
        m_byId.insert(iface->id(), m_plugins.size() - 1);
        qInfo() << "[plugins] loaded" << iface->id() << iface->version();
    }
}

QVariantList PluginManager::registry() const
{
    QVariantList out;
    for (const Entry &e : m_plugins) {
        QVariantMap m;
        if (e.iface) {
            m = e.iface->metadata();
            m.insert(QStringLiteral("loaded"), true);
        } else {
            m.insert(QStringLiteral("id"), QStringLiteral("(unknown)"));
            m.insert(QStringLiteral("name"), QFileInfo(e.path).fileName());
            m.insert(QStringLiteral("loaded"), false);
            m.insert(QStringLiteral("error"), e.error);
        }
        out.append(m);
    }
    return out;
}

QString PluginManager::runCommand(const QString &command, const QString &args) const
{
    for (const Entry &e : m_plugins) {
        if (!e.iface)
            continue;
        const QString reply = e.iface->onCommand(command, args);
        if (!reply.isEmpty())
            return reply;
    }
    return QString();
}

QWidget *PluginManager::panelFor(const QString &id)
{
    const int idx = m_byId.value(id, -1);
    if (idx < 0 || idx >= m_plugins.size())
        return nullptr;
    Entry &e = m_plugins[idx];
    if (!e.iface)
        return nullptr;
    if (!e.panel)
        e.panel = e.iface->createPanel();
    return e.panel;
}

void PluginManager::populateTrayMenu(QMenu *menu) const
{
    if (!menu)
        return;
    bool any = false;
    for (const Entry &e : m_plugins) {
        if (!e.iface)
            continue;
        e.iface->populateTrayMenu(menu);
        any = true;
    }
    if (any)
        menu->addSeparator();
}

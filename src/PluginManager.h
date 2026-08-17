#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QWidget>

class DshPluginInterface;

// Scans <exe_dir>/plugins/*.dll with QPluginLoader, loads every library that
// implements DshPluginInterface, and exposes the registry to the shell UI.
class PluginManager : public QObject {
    Q_OBJECT
public:
    explicit PluginManager(QObject *parent = nullptr);

    // Scan + load. Called once at startup; bad DLLs are skipped, never throws.
    void scan();

    // Registry list: array of QVariantMap (id, name, version, description, loaded, error?).
    QVariantList registry() const;

    // Run "/<command> <args>" through plugins; first non-empty reply wins.
    QString runCommand(const QString &command, const QString &args) const;

    // Panel widget for a plugin id (nullptr if none / not loaded).
    QWidget *panelFor(const QString &id);

    // Ask all plugins to populate a tray menu.
    void populateTrayMenu(QMenu *menu) const;

    int count() const { return m_plugins.size(); }

signals:
    void pluginLoaded(const QString &id, const QString &name);

private:
    struct Entry {
        QString path;
        DshPluginInterface *iface = nullptr;
        QObject *obj = nullptr;
        QWidget *panel = nullptr;
        QString error;
    };
    QList<Entry> m_plugins;
    QHash<QString, int> m_byId;
};

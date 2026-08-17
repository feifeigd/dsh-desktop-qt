#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QWidget>
#include <QMenu>

// ---------------------------------------------------------------------------
// Plugin interface for DSH Desktop (Qt shell for DeepSeek Harness).
//
// Plugins are DLLs dropped into <install_dir>/plugins/. The shell scans that
// directory with QPluginLoader at startup and loads every library that
// implements DshPluginInterface. Plugins extend the DESKTOP SHELL itself
// (native Qt panels, tray menu entries, chat slash commands) — the DSH agent
// core and its own plugin ecosystem are managed inside the embedded web UI.
//
// A plugin can contribute:
//   1. createPanel()  -> a native Qt widget docked in the left plugin bar
//   2. populateTrayMenu() -> extra QActions on the system tray menu
//   3. onCommand()    -> handle "/name args..." typed in the web chat
//
// The interface is versioned via Q_DECLARE_INTERFACE; the host refuses
// plugins built against an incompatible version.
// ---------------------------------------------------------------------------

class DshPluginInterface {
public:
    virtual ~DshPluginInterface() = default;

    // --- Identity (shown in the plugin manager / UI) ---
    virtual QString id() const = 0;             // unique, e.g. "com.example.sysinfo"
    virtual QString name() const = 0;           // display name
    virtual QString version() const = 0;        // semver-ish
    virtual QString description() const = 0;    // one-liner

    // --- Metadata delivered to the shell UI (JSON-serializable) ---
    virtual QVariantMap metadata() const
    {
        return {
            {QStringLiteral("id"), id()},
            {QStringLiteral("name"), name()},
            {QStringLiteral("version"), version()},
            {QStringLiteral("description"), description()},
        };
    }

    // --- UI: native panel shown in the left plugin bar. Return nullptr if none. ---
    virtual QWidget *createPanel(QWidget *parent = nullptr) { Q_UNUSED(parent); return nullptr; }

    // --- UI: tray menu contributions (e.g. "Show System Info"). ---
    virtual void populateTrayMenu(QMenu *menu) { Q_UNUSED(menu); }

    // --- Command: handle "/<command> <args>" from the web chat.
    // Return a text reply, or empty string if not handled. ---
    virtual QString onCommand(const QString &command, const QString &args) { Q_UNUSED(command); Q_UNUSED(args); return QString(); }

    // --- Lifecycle ---
    virtual void onLoad() {}
    virtual void onUnload() {}
};

#define DshPluginInterface_iid "com.deepseek.dsh-desktop.plugin/1.0"
Q_DECLARE_INTERFACE(DshPluginInterface, DshPluginInterface_iid)

// A listener on Hyprland's event socket.
//
// The launcher switches to the app workspace before it spawns anything, which
// covers every window the launcher itself opens. It does not cover the ones it
// does not open: a game started from inside Steam, a second window from an app
// already running, a dialog. Those appear on whatever workspace is current —
// which is the launcher's — and Hyprland tiles them side by side with the
// shell. "Time Clickers" sharing the screen with the grid is what this is for.
//
// Hyprland streams these as lines on a Unix socket, so no polling and no
// window rules, which matters because this Hyprland rejects the windowrule
// syntax outright and its replacement is still migrating to a Lua config.
#pragma once

#include <QLocalSocket>
#include <QObject>
#include <QString>

class HyprlandEvents : public QObject {
    Q_OBJECT

public:
    explicit HyprlandEvents(QObject* parent = nullptr);

    // False when there is no Hyprland to listen to — another compositor, or
    // none. The launcher has to keep working in that case, just without this.
    bool listening() const;

signals:
    // A window was mapped. `workspace` is its name, which for the numbered ones
    // is the number as a string.
    void windowOpened(const QString& address, const QString& workspace,
                      const QString& windowClass, const QString& title);

private:
    void readEvents();

    QLocalSocket socket_;
    // Hyprland can split a write across reads, so a partial trailing line is
    // held here until its newline arrives rather than being parsed as a whole
    // event and discarded.
    QByteArray pending_;
};

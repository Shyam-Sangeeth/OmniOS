// The launcher on the session bus, as org.omnios.Launcher at /Launcher, for
// what the desktop asks of it. In Game Mode KWin calls ShowMenu when Meta is
// pressed alone (omni-session-select points Meta there instead of Plasma's
// start menu): the running game's menu, or the system menu. The OmniOS mark in
// the panel's corner calls ShowSystemMenu in Game Mode (org.omnios.kickoff,
// patched by scripts/make-desktop-launcher.sh): Game Mode's own menu, where
// on the desktop it opens Plasma's start menu.
#pragma once

#include <QObject>

class LauncherBus : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.omnios.Launcher")
public:
    explicit LauncherBus(QObject* parent = nullptr) : QObject(parent) {}

public slots:
    Q_SCRIPTABLE void ShowMenu() { emit menuWanted(); }
    Q_SCRIPTABLE void ShowSystemMenu() { emit systemMenuWanted(); }

signals:
    void menuWanted();
    void systemMenuWanted();
};

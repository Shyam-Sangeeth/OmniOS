// The launcher on the session bus, as org.omnios.Launcher at /Launcher, for
// what the desktop asks of it. In Game Mode KWin calls ShowMenu when Meta is
// pressed alone (omni-session-select points Meta there instead of Plasma's
// start menu): the running game's menu, or the system menu.
#pragma once

#include <QObject>

class LauncherBus : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.omnios.Launcher")
public:
    explicit LauncherBus(QObject* parent = nullptr) : QObject(parent) {}

public slots:
    Q_SCRIPTABLE void ShowMenu() { emit menuWanted(); }

signals:
    void menuWanted();
};

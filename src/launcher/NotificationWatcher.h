// The notifications that stay on screen until someone closes them, and a way
// to close them without a mouse.
//
// Plasma shows most notifications for a few seconds, but a critical one, or
// one sent with no timeout, stays in the corner until its × is clicked. A
// console may have no pointer at all, so the launcher offers "Clear
// notifications" in its system menu instead (Main.qml, openPowerMenu).
//
// There is no way to ask Plasma which notifications are showing. What it
// does offer is a watcher: a process that calls RegisterWatcher (interface
// org.kde.NotificationManager, on the notification server's own object) is
// sent every new notification, with its id,
// on /NotificationWatcher (org.kde.NotificationWatcher). This keeps the ids of
// the standing ones, and closes them with CloseNotification, which Plasma
// honours whoever asks. Notifications from before the launcher started are
// not seen; in Game Mode the launcher starts with the session.
#pragma once

#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVariantMap>

class QDBusServiceWatcher;

class NotificationWatcher : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.NotificationWatcher")

public:
    explicit NotificationWatcher(QObject* parent = nullptr);

    // How many standing notifications there are, as far as is known.
    int count() const { return static_cast<int>(standing_.size()); }

    // Closes every notification there is, standing or in the history, the
    // ones from before the launcher started too. As the history's own "Clear
    // all" does, they leave it.
    void clearAll();

public slots:
    // Plasma calls these, through D-Bus.
    Q_SCRIPTABLE void Notify(uint id, const QString& appName, uint replacesId, const QString& appIcon,
                             const QString& summary, const QString& body, const QStringList& actions,
                             const QVariantMap& hints, int timeout);
    Q_SCRIPTABLE void CloseNotification(uint id);

signals:
    void countChanged();

private:
    void registerWithPlasma();

    QSet<uint> standing_;
    uint highest_ = 0;  // the newest notification's number seen
    QDBusServiceWatcher* plasma_ = nullptr;
};

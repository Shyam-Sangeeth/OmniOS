#include "NotificationWatcher.h"

#include <algorithm>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusServiceWatcher>

namespace {

const QString kService = QStringLiteral("org.freedesktop.Notifications");
const QString kPath = QStringLiteral("/org/freedesktop/Notifications");

}  // namespace

NotificationWatcher::NotificationWatcher(QObject* parent) : QObject(parent) {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(QStringLiteral("/NotificationWatcher"), this, QDBusConnection::ExportScriptableSlots)) {
        qWarning("notification watcher: could not export /NotificationWatcher");
        return;
    }
    // Registered with the server that is running now, and again with any
    // that replaces it (plasmashell restarting forgets its watchers).
    plasma_ = new QDBusServiceWatcher(kService, bus, QDBusServiceWatcher::WatchForRegistration, this);
    connect(plasma_, &QDBusServiceWatcher::serviceRegistered, this, [this]() {
        standing_.clear();  // a new server: the old ids mean nothing
        highest_ = 0;
        emit countChanged();
        registerWithPlasma();
    });
    registerWithPlasma();
}

void NotificationWatcher::registerWithPlasma() {
    const QDBusMessage call = QDBusMessage::createMethodCall(kService, kPath, QStringLiteral("org.kde.NotificationManager"),
                                                             QStringLiteral("RegisterWatcher"));
    QDBusConnection::sessionBus().asyncCall(call);
}

void NotificationWatcher::Notify(uint id, const QString&, uint replacesId, const QString&, const QString&,
                                 const QString&, const QStringList&, const QVariantMap& hints, int timeout) {
    highest_ = std::max(highest_, id);
    if (replacesId != 0) standing_.remove(replacesId);
    // Plasma never times out a critical notification (urgency 2), nor one
    // sent with a timeout of 0; everything else goes by itself.
    const bool critical = hints.value(QStringLiteral("urgency")).toInt() == 2;
    if (critical || timeout == 0) standing_.insert(id);
    emit countChanged();
}

void NotificationWatcher::CloseNotification(uint id) {
    if (standing_.remove(id)) emit countChanged();
}

void NotificationWatcher::clearAll() {
    // Plasma numbers notifications in order, so every one there is, including
    // any shown before the launcher started watching, has a number up to the
    // highest seen. All of those are closed, the most recent thousand at most;
    // one already gone is answered with an error nobody reads.
    const uint lowest = highest_ > 1000 ? highest_ - 1000 : 1;
    QSet<uint> ids = standing_;
    for (uint id = lowest; id <= highest_; ++id) ids.insert(id);
    for (const uint id : std::as_const(ids)) {
        QDBusMessage call = QDBusMessage::createMethodCall(kService, kPath, kService, QStringLiteral("CloseNotification"));
        call << id;
        QDBusConnection::sessionBus().asyncCall(call);
    }
    standing_.clear();
    emit countChanged();
}

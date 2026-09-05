#include "HyprlandEvents.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QProcessEnvironment>

#include <unistd.h>

namespace {

// Hyprland's socket moved between releases: newer ones put it under
// $XDG_RUNTIME_DIR/hypr, older ones under /tmp/hypr. Both are checked so the
// launcher does not silently lose this the next time the image's Hyprland is
// updated.
QString eventSocketPath() {
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString signature = env.value(QStringLiteral("HYPRLAND_INSTANCE_SIGNATURE"));

    QString runtime = env.value(QStringLiteral("XDG_RUNTIME_DIR"));
    if (runtime.isEmpty())
        runtime = QStringLiteral("/run/user/%1").arg(::geteuid());

    QStringList roots{runtime + QStringLiteral("/hypr"), QStringLiteral("/tmp/hypr")};

    // The signature when it is there, and a search when it is not. Hyprland
    // does not guarantee its variables reach an exec-once child's environment —
    // this profile already lost WAYLAND_DISPLAY that way once — so relying on
    // the variable alone is how this quietly does nothing.
    if (!signature.isEmpty()) {
        for (const QString& root : roots) {
            const QString path = root + QLatin1Char('/') + signature +
                                 QStringLiteral("/.socket2.sock");
            if (QFile::exists(path)) return path;
        }
    }

    // Newest first: a previous Hyprland that died without cleaning up leaves
    // its directory behind, and connecting to that one listens to nothing.
    for (const QString& root : roots) {
        QDir dir(root);
        if (!dir.exists()) continue;
        const QFileInfoList entries =
            dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
        for (const QFileInfo& entry : entries) {
            const QString path = entry.absoluteFilePath() + QStringLiteral("/.socket2.sock");
            if (QFile::exists(path)) return path;
        }
    }
    return {};
}

}  // namespace

HyprlandEvents::HyprlandEvents(QObject* parent) : QObject(parent) {
    const QString path = eventSocketPath();
    if (path.isEmpty()) {
        // Said out loud. A silent "no window management today" is exactly the
        // kind of thing that is only noticed when a game tiles itself beside
        // the shell.
        qWarning("omni-launcher: no Hyprland event socket; stray windows will "
                 "not be moved off the launcher's workspace");
        return;
    }

    connect(&socket_, &QLocalSocket::readyRead, this, &HyprlandEvents::readEvents);
    connect(&socket_, &QLocalSocket::connected, this,
            [path]() { qWarning("omni-launcher: watching %s", qPrintable(path)); });
    connect(&socket_, &QLocalSocket::errorOccurred, this,
            [this](QLocalSocket::LocalSocketError) {
                qWarning("omni-launcher: Hyprland event socket: %s",
                         qPrintable(socket_.errorString()));
            });
    socket_.connectToServer(path, QIODevice::ReadOnly);
}

bool HyprlandEvents::listening() const {
    return socket_.state() == QLocalSocket::ConnectedState;
}

void HyprlandEvents::readEvents() {
    pending_ += socket_.readAll();

    // Events are newline separated and shaped "name>>a,b,c". Anything after the
    // last newline is an incomplete line and waits for the rest.
    int newline = pending_.indexOf('\n');
    while (newline >= 0) {
        const QString line = QString::fromUtf8(pending_.left(newline));
        pending_.remove(0, newline + 1);
        newline = pending_.indexOf('\n');

        const int separator = line.indexOf(QStringLiteral(">>"));
        if (separator < 0) continue;
        if (line.left(separator) != QLatin1String("openwindow")) continue;

        // openwindow>>address,workspace,class,title — and a title is allowed to
        // contain commas, so only the first three are split off.
        const QString payload = line.mid(separator + 2);
        const QStringList parts = payload.split(QLatin1Char(','));
        if (parts.size() < 4) continue;

        emit windowOpened(parts.at(0), parts.at(1), parts.at(2),
                          parts.mid(3).join(QLatin1Char(',')));
    }
}

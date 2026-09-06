#include "SystemStatus.h"

#include <QProcess>
#include <QVariantMap>

namespace {

// Long enough that a poll costs nothing, short enough that plugging in a cable
// shows up before anyone wonders whether it worked.
constexpr int kPollIntervalMs = 8000;

// These are quick — nmcli and bluetoothctl answer in milliseconds — so they are
// read synchronously rather than through a pile of asynchronous plumbing. The
// timeout is the safety net: a wedged tool must not take the shell down with
// it, so it is abandoned and the status simply stays as it was.
QString run(const QString& program, const QStringList& arguments, int timeoutMs = 4000) {
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(program, arguments);
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(500);
        return {};
    }
    return QString::fromUtf8(process.readAllStandardOutput());
}

// nmcli --terse escapes a colon inside a value as "\:", so a plain split would
// cut an SSID containing one in half.
QStringList terseFields(const QString& line) {
    QStringList fields;
    QString current;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (c == QLatin1Char('\\') && i + 1 < line.size()) {
            current += line.at(++i);
        } else if (c == QLatin1Char(':')) {
            fields << current;
            current.clear();
        } else {
            current += c;
        }
    }
    fields << current;
    return fields;
}

QVariantMap entry(const QString& action, const QString& label, bool enabled = true) {
    QVariantMap map;
    map.insert(QStringLiteral("action"), action);
    map.insert(QStringLiteral("label"), label);
    map.insert(QStringLiteral("enabled"), enabled);
    return map;
}

// A signal strength as something readable at a glance from a sofa.
QString bars(int signal) {
    if (signal >= 75) return QStringLiteral("▂▄▆█");
    if (signal >= 50) return QStringLiteral("▂▄▆ ");
    if (signal >= 25) return QStringLiteral("▂▄  ");
    return QStringLiteral("▂   ");
}

}  // namespace

SystemStatus::SystemStatus(QObject* parent) : QObject(parent) {
    connect(&timer_, &QTimer::timeout, this, &SystemStatus::poll);
    timer_.start(kPollIntervalMs);
    poll();
}

void SystemStatus::refresh() { poll(); }

void SystemStatus::poll() {
    const QString previousKind  = networkKind_;
    const QString previousLabel = networkLabel_;
    const QString previousBt    = bluetoothLabel_;
    const int     previousVol   = audioVolume_;
    const bool    previousMute  = audioMuted_;
    const bool    previousOn    = bluetoothPowered_;

    pollNetwork();
    pollAudio();
    pollBluetooth();

    if (networkKind_ != previousKind || networkLabel_ != previousLabel ||
        bluetoothLabel_ != previousBt || bluetoothPowered_ != previousOn ||
        audioVolume_ != previousVol || audioMuted_ != previousMute) {
        emit changed();
    }
}

void SystemStatus::pollNetwork() {
    networkKind_ = QStringLiteral("none");
    networkLabel_ = tr("Not connected");
    wifiDevice_.clear();
    wiredDevice_.clear();

    // Wired first: a machine with both plugged in is using the cable, and that
    // is the one worth reporting.
    const QString devices = run(QStringLiteral("nmcli"),
                                {QStringLiteral("-t"), QStringLiteral("-f"),
                                 QStringLiteral("DEVICE,TYPE,STATE,CONNECTION"),
                                 QStringLiteral("device"), QStringLiteral("status")});
    QString wiredName, wifiName;
    for (const QString& line : devices.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList fields = terseFields(line);
        if (fields.size() < 4) continue;
        const QString device = fields.at(0);
        const QString type   = fields.at(1);
        const QString state  = fields.at(2);
        const QString name   = fields.at(3);

        if (type == QLatin1String("ethernet")) {
            wiredDevice_ = device;
            if (state == QLatin1String("connected")) wiredName = name;
        } else if (type == QLatin1String("wifi")) {
            wifiDevice_ = device;
            if (state == QLatin1String("connected")) wifiName = name;
        }
    }

    const auto addressOf = [](const QString& device) {
        const QString shown = run(QStringLiteral("nmcli"),
                                  {QStringLiteral("-t"), QStringLiteral("-f"),
                                   QStringLiteral("IP4.ADDRESS"), QStringLiteral("device"),
                                   QStringLiteral("show"), device});
        for (const QString& line : shown.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const int colon = line.indexOf(QLatin1Char(':'));
            if (colon < 0) continue;
            // "10.0.2.15/24" — the mask is noise on a status line.
            return line.mid(colon + 1).section(QLatin1Char('/'), 0, 0);
        }
        return QString();
    };

    if (!wiredName.isEmpty()) {
        networkKind_ = QStringLiteral("ethernet");
        const QString address = addressOf(wiredDevice_);
        networkLabel_ = address.isEmpty() ? tr("Wired") : tr("Wired · %1").arg(address);
    } else if (!wifiName.isEmpty()) {
        networkKind_ = QStringLiteral("wifi");
        const QString address = addressOf(wifiDevice_);
        networkLabel_ = address.isEmpty() ? wifiName : tr("%1 · %2").arg(wifiName, address);
    }
}

void SystemStatus::pollAudio() {
    // wpctl reports "Volume: 0.45" and adds " [MUTED]" when it is muted, which
    // is a far steadier thing to parse than pactl's paragraphs.
    const QString volume = run(QStringLiteral("wpctl"),
                               {QStringLiteral("get-volume"),
                                QStringLiteral("@DEFAULT_AUDIO_SINK@")});
    if (volume.contains(QLatin1String("Volume:"))) {
        audioVolume_ = qRound(volume.section(QLatin1Char(':'), 1)
                                  .section(QLatin1Char(' '), 1, 1)
                                  .toDouble() * 100.0);
        audioMuted_ = volume.contains(QLatin1String("MUTED"));
    } else {
        audioVolume_ = -1;
        audioMuted_  = false;
    }
}

void SystemStatus::pollBluetooth() {
    const QString show = run(QStringLiteral("bluetoothctl"), {QStringLiteral("show")});
    bluetoothAvailable_ = show.contains(QLatin1String("Powered:"));
    bluetoothPowered_   = show.contains(QLatin1String("Powered: yes"));

    if (!bluetoothAvailable_) {
        bluetoothLabel_ = tr("No adapter");
    } else if (!bluetoothPowered_) {
        bluetoothLabel_ = tr("Off");
    } else {
        int connected = 0;
        const QString paired = run(QStringLiteral("bluetoothctl"),
                                   {QStringLiteral("devices"), QStringLiteral("Connected")});
        for (const QString& line : paired.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            if (line.startsWith(QLatin1String("Device "))) ++connected;
        }
        bluetoothLabel_ = connected == 0 ? tr("On, nothing connected")
                                         : tr("%n device(s) connected", nullptr, connected);
    }
}

QVariantList SystemStatus::networkEntries() {
    pollNetwork();
    QVariantList entries;

    // What it is doing now, first and unselectable: a status line, not a choice.
    entries << entry(QStringLiteral("none"), networkLabel_, false);

    if (networkKind_ == QLatin1String("wifi")) {
        entries << entry(QStringLiteral("wifi:disconnect"), tr("Disconnect"));
    }

    if (wifiDevice_.isEmpty()) {
        if (networkKind_ != QLatin1String("wifi"))
            entries << entry(QStringLiteral("none"), tr("No Wi-Fi adapter"), false);
        return entries;
    }

    // --rescan no: a rescan takes seconds and blocks, and NetworkManager keeps
    // a recent list anyway. The menu is for choosing, not for surveying.
    const QString list = run(QStringLiteral("nmcli"),
                             {QStringLiteral("-t"), QStringLiteral("-f"),
                              QStringLiteral("IN-USE,SSID,SIGNAL,SECURITY"),
                              QStringLiteral("device"), QStringLiteral("wifi"),
                              QStringLiteral("list"), QStringLiteral("--rescan"),
                              QStringLiteral("no")},
                             8000);

    QStringList seen;
    for (const QString& line : list.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList fields = terseFields(line);
        if (fields.size() < 4) continue;
        const bool    inUse    = fields.at(0).trimmed() == QLatin1String("*");
        const QString ssid     = fields.at(1);
        const int     signal   = fields.at(2).toInt();
        const QString security = fields.at(3).trimmed();

        // A hidden network has no name to show and cannot be joined by picking
        // it off a list, and the same network is often seen on several bands.
        if (ssid.isEmpty() || seen.contains(ssid)) continue;
        seen << ssid;
        if (inUse) continue;  // already the one on the status line above

        const QString lock = security.isEmpty() || security == QLatin1String("--")
                                 ? QString()
                                 : QStringLiteral(" ·");
        entries << entry(QStringLiteral("wifi:connect:") + ssid,
                         QStringLiteral("%1  %2%3").arg(bars(signal), ssid, lock));
        if (entries.size() > 10) break;  // a menu, not a survey
    }
    return entries;
}

QVariantList SystemStatus::bluetoothEntries() {
    pollBluetooth();
    QVariantList entries;

    if (!bluetoothAvailable_) {
        entries << entry(QStringLiteral("none"), tr("No bluetooth adapter"), false);
        return entries;
    }

    entries << entry(bluetoothPowered_ ? QStringLiteral("bt:off") : QStringLiteral("bt:on"),
                     bluetoothPowered_ ? tr("Turn bluetooth off") : tr("Turn bluetooth on"));

    if (!bluetoothPowered_) return entries;

    entries << entry(QStringLiteral("bt:pair"), tr("Pair a controller"));

    const QString paired = run(QStringLiteral("bluetoothctl"),
                               {QStringLiteral("devices"), QStringLiteral("Paired")});
    const QString connected = run(QStringLiteral("bluetoothctl"),
                                  {QStringLiteral("devices"), QStringLiteral("Connected")});

    for (const QString& line : paired.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        // "Device AA:BB:CC:DD:EE:FF Wireless Controller"
        if (!line.startsWith(QLatin1String("Device "))) continue;
        const QString rest = line.mid(7);
        const QString mac  = rest.section(QLatin1Char(' '), 0, 0);
        const QString name = rest.section(QLatin1Char(' '), 1).trimmed();
        if (mac.isEmpty()) continue;

        const bool isConnected = connected.contains(mac);
        entries << entry((isConnected ? QStringLiteral("bt:disconnect:")
                                      : QStringLiteral("bt:connect:")) + mac,
                         isConnected ? tr("%1  ·  disconnect").arg(name.isEmpty() ? mac : name)
                                     : (name.isEmpty() ? mac : name));
    }
    return entries;
}

QVariantList SystemStatus::audioEntries() {
    pollAudio();
    QVariantList entries;

    if (audioVolume_ < 0) {
        entries << entry(QStringLiteral("none"), tr("No audio output"), false);
        return entries;
    }

    entries << entry(QStringLiteral("none"),
                     audioMuted_ ? tr("Muted") : tr("Volume %1%").arg(audioVolume_), false);

    // Sticky: turning the volume up once is never what anyone wants, and a menu
    // that shuts after every step would have to be reopened for each press.
    QVariantMap up   = entry(QStringLiteral("audio:up"), tr("Volume up"));
    QVariantMap down = entry(QStringLiteral("audio:down"), tr("Volume down"));
    up.insert(QStringLiteral("sticky"), true);
    down.insert(QStringLiteral("sticky"), true);
    entries << up << down;

    entries << entry(QStringLiteral("audio:mute"),
                     audioMuted_ ? tr("Unmute") : tr("Mute"));

    // Outputs, so a console plugged into a television can be moved from the
    // monitor's speakers to HDMI without a terminal.
    const QString defaultSink = run(QStringLiteral("pactl"),
                                    {QStringLiteral("get-default-sink")}).trimmed();
    const QString sinks = run(QStringLiteral("pactl"), {QStringLiteral("list"),
                                                        QStringLiteral("sinks")});
    QString name;
    for (const QString& raw : sinks.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1String("Name:"))) {
            name = line.mid(5).trimmed();
        } else if (line.startsWith(QLatin1String("Description:")) && !name.isEmpty()) {
            const QString description = line.mid(12).trimmed();
            const bool current = name == defaultSink;
            entries << entry(current ? QStringLiteral("none")
                                     : QStringLiteral("audio:sink:") + name,
                             current ? tr("%1  ·  in use").arg(description) : description,
                             !current);
            name.clear();
        }
    }
    return entries;
}

void SystemStatus::act(const QString& action) {
    if (action == QLatin1String("none")) return;

    // Audio is instant and local: nothing here is worth narrating in the status
    // line, and a volume step that took a second to appear would be unusable.
    if (action == QLatin1String("audio:up") || action == QLatin1String("audio:down")) {
        const QString step = action.endsWith(QLatin1String("up")) ? QStringLiteral("5%+")
                                                                  : QStringLiteral("5%-");
        // -l 1.0 stops a held button pushing it past 100% into distortion.
        run(QStringLiteral("wpctl"), {QStringLiteral("set-volume"), QStringLiteral("-l"),
                                      QStringLiteral("1.0"),
                                      QStringLiteral("@DEFAULT_AUDIO_SINK@"), step});
        pollAudio();
        emit changed();
        return;
    }

    if (action == QLatin1String("audio:mute")) {
        run(QStringLiteral("wpctl"), {QStringLiteral("set-mute"),
                                      QStringLiteral("@DEFAULT_AUDIO_SINK@"),
                                      QStringLiteral("toggle")});
        pollAudio();
        emit changed();
        emit message(audioMuted_ ? tr("Muted") : tr("Unmuted"));
        return;
    }

    if (action.startsWith(QLatin1String("audio:sink:"))) {
        const QString sink = action.mid(QStringLiteral("audio:sink:").size());
        run(QStringLiteral("pactl"), {QStringLiteral("set-default-sink"), sink});
        pollAudio();
        emit changed();
        emit message(tr("Audio output changed"));
        return;
    }

    if (action == QLatin1String("bt:on") || action == QLatin1String("bt:off")) {
        const QString state = action.endsWith(QLatin1String("on")) ? QStringLiteral("on")
                                                                   : QStringLiteral("off");
        run(QStringLiteral("bluetoothctl"), {QStringLiteral("power"), state});
        pollBluetooth();
        emit changed();
        emit message(state == QLatin1String("on") ? tr("Bluetooth on") : tr("Bluetooth off"));
        return;
    }

    if (action == QLatin1String("bt:pair")) {
        emit runRequested(QStringLiteral("omni-pair-controller 25"),
                          tr("Hold the pairing button on the controller ..."));
        return;
    }

    if (action.startsWith(QLatin1String("bt:connect:")) ||
        action.startsWith(QLatin1String("bt:disconnect:"))) {
        const bool connecting = action.startsWith(QLatin1String("bt:connect:"));
        const QString mac = action.section(QLatin1Char(':'), 2);
        emit runRequested(QStringLiteral("bluetoothctl %1 %2")
                              .arg(connecting ? QStringLiteral("connect")
                                              : QStringLiteral("disconnect"), mac),
                          connecting ? tr("Connecting ...") : tr("Disconnecting ..."));
        return;
    }

    if (action == QLatin1String("wifi:disconnect")) {
        emit runRequested(QStringLiteral("nmcli device disconnect %1").arg(wifiDevice_),
                          tr("Disconnecting ..."));
        return;
    }

    if (action.startsWith(QLatin1String("wifi:connect:"))) {
        const QString ssid = action.mid(QStringLiteral("wifi:connect:").size());

        // A network this machine already knows comes up without anyone typing
        // anything, and so does an open one. A new secured network needs a
        // password, and there is nowhere on a console to type one — saying so
        // is better than a connection that fails with no explanation.
        const QString saved = run(QStringLiteral("nmcli"),
                                  {QStringLiteral("-t"), QStringLiteral("-f"),
                                   QStringLiteral("NAME"), QStringLiteral("connection"),
                                   QStringLiteral("show")});
        const bool known = saved.split(QLatin1Char('\n')).contains(ssid);

        emit runRequested(
            known ? QStringLiteral("nmcli connection up id %1").arg(ssid)
                  : QStringLiteral("nmcli device wifi connect %1 || "
                                   "{ echo \"error: %1 needs a password, and there is no "
                                   "keyboard flow for one yet\"; exit 1; }")
                        .arg(ssid),
            tr("Connecting to %1 ...").arg(ssid));
        return;
    }
}

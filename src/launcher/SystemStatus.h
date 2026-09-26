// Network and bluetooth, for the corner opposite the mark.
//
// A console needs to answer two questions at a glance — am I online, and is the
// controller connected — and to let both be fixed without a terminal. That is
// all this is: it reads NetworkManager and bluez through their own command line
// tools rather than binding to their D-Bus APIs, because the shell already
// shells out for everything else and one process every ten seconds is not worth
// a dependency on libnm and GDBus.
//
// The lists it produces are shaped like MenuPanel's model, so the same panel
// that serves a tile's menu serves these too.
#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

class SystemStatus : public QObject {
    Q_OBJECT

    // "ethernet", "wifi" or "none" — which glyph to draw.
    Q_PROPERTY(QString networkKind READ networkKind NOTIFY changed)
    // What it is connected to, for the tooltip line in the panel.
    Q_PROPERTY(QString networkLabel READ networkLabel NOTIFY changed)
    Q_PROPERTY(bool online READ online NOTIFY changed)

    Q_PROPERTY(bool audioAvailable READ audioAvailable NOTIFY changed)
    Q_PROPERTY(bool audioMuted READ audioMuted NOTIFY changed)
    // 0-100, or -1 when there is no sink to ask.
    Q_PROPERTY(int audioVolume READ audioVolume NOTIFY changed)

    Q_PROPERTY(bool bluetoothAvailable READ bluetoothAvailable NOTIFY changed)
    Q_PROPERTY(bool bluetoothPowered READ bluetoothPowered NOTIFY changed)
    Q_PROPERTY(QString bluetoothLabel READ bluetoothLabel NOTIFY changed)

    // A secured Wi-Fi network this machine has not joined before, waiting for
    // its password. Empty when nothing is being asked.
    Q_PROPERTY(QString wifiPasswordFor READ wifiPasswordFor NOTIFY wifiPasswordChanged)

public:
    explicit SystemStatus(QObject* parent = nullptr);

    QString networkKind() const { return networkKind_; }
    QString networkLabel() const { return networkLabel_; }
    bool    online() const { return networkKind_ != QLatin1String("none"); }

    bool    audioAvailable() const { return audioVolume_ >= 0; }
    bool    audioMuted() const { return audioMuted_; }
    int     audioVolume() const { return audioVolume_; }

    bool    bluetoothAvailable() const { return bluetoothAvailable_; }
    bool    bluetoothPowered() const { return bluetoothPowered_; }
    QString bluetoothLabel() const { return bluetoothLabel_; }
    QString wifiPasswordFor() const { return wifiPasswordFor_; }

    // Menu entries: {action, label, enabled}. Built on demand, because a list
    // of nearby networks is stale the moment it is cached.
    Q_INVOKABLE QVariantList networkEntries();
    Q_INVOKABLE QVariantList bluetoothEntries();
    Q_INVOKABLE QVariantList audioEntries();

    // Acts on one of the actions those entries carry.
    Q_INVOKABLE void act(const QString& action);

    // Re-reads everything now, rather than waiting for the next poll.
    Q_INVOKABLE void refresh();

    // Joins a new secured network with the password typed for it. The password
    // goes to NetworkManager in a file only this user can read, deleted as
    // soon as nmcli has it — never on a command line, where every process on
    // the machine could read it.
    Q_INVOKABLE void joinWifi(const QString& ssid, const QString& password);
    Q_INVOKABLE void cancelWifiPassword();

signals:
    void changed();
    // Something long enough to be worth saying out loud while it happens.
    // The controller runs it and reports the result.
    void runRequested(const QString& script, const QString& verb);
    // Something short that has already happened.
    void message(const QString& text);
    void wifiPasswordChanged();

private:
    // Split because they are asked for at very different rates. Turning the
    // volume up should not go and ask NetworkManager and bluez what they are
    // doing: each poll is half a dozen processes, and doing all three per
    // keypress made the volume move once for every three presses.
    void poll();
    void pollNetwork();
    void pollAudio();
    void pollBluetooth();

    QTimer  timer_;
    QString networkKind_  = QStringLiteral("none");
    QString networkLabel_;
    QString wifiDevice_;
    QString wiredDevice_;
    int     audioVolume_ = -1;
    bool    audioMuted_  = false;
    bool    bluetoothAvailable_ = false;
    bool    bluetoothPowered_   = false;
    QString bluetoothLabel_;
    QString wifiPasswordFor_;
    // SSID -> security, as the last network list showed it, for joining.
    QHash<QString, QString> wifiSecurity_;
};

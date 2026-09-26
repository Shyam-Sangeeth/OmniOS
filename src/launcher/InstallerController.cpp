#include "InstallerController.h"

#include "KeyDelivery.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QTimeZone>
#include <QVariantMap>

#include <algorithm>

#include <pwd.h>

namespace {

// The installer is reached with sudo -n: the live session's user may use sudo
// without a password, and -n makes a session that may not fail at once rather
// than hang on a prompt nobody can see.
const QString kSudo = QStringLiteral("sudo");
const QString kInstaller = QStringLiteral("omni-install");

// Disk sizes the way the label on the disk says them: a "1 TB" drive is
// 1,000,000,000,000 bytes, and showing it as 931 GiB makes people wonder
// whether they picked the right one.
QString sizeText(double bytes) {
    if (bytes >= 1e12) return QStringLiteral("%1 TB").arg(bytes / 1e12, 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(qRound(bytes / 1e9));
}

// "nvme" and "usb" as a person would say them.
QString transportText(const QString& transport) {
    if (transport == QLatin1String("nvme")) return QStringLiteral("NVMe");
    if (transport == QLatin1String("sata") || transport == QLatin1String("ata")) return QStringLiteral("SATA");
    if (transport == QLatin1String("usb")) return QStringLiteral("USB");
    if (transport == QLatin1String("mmc")) return QStringLiteral("SD card");
    if (transport == QLatin1String("virtio")) return QStringLiteral("Virtual");
    return {};
}

}  // namespace

InstallerController::InstallerController(QObject* parent) : QObject(parent) {
    connect(&gamepad_, &GamepadInput::keyPressed, this,
            [this](int key) { deliverKey(key, [this] { emit focusWanted(); }); });
}

bool InstallerController::fullscreen() const {
    return qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(QLatin1String("Hyprland"));
}

void InstallerController::refresh() {
    if (listing_ != nullptr || busy_) return;

    listing_ = new QProcess(this);
    connect(listing_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        QVariantList found;
        const QString output = QString::fromUtf8(listing_->readAllStandardOutput());
        if (code == 0) {
            for (const QString& line : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                const QStringList fields = line.split(QLatin1Char('\t'));
                if (fields.size() < 5) continue;
                QVariantMap disk;
                disk.insert(QStringLiteral("path"), fields.at(0));
                disk.insert(QStringLiteral("size"), sizeText(fields.at(1).toDouble()));
                disk.insert(QStringLiteral("model"), fields.at(2));
                disk.insert(QStringLiteral("transport"), transportText(fields.at(3)));
                disk.insert(QStringLiteral("contents"), fields.at(4));
                disk.insert(QStringLiteral("os"), fields.size() > 5 ? fields.at(5) : QString());
                found.append(disk);
            }
            // The safest disk first, so the one the list opens on is never
            // the one Windows lives on when there is anything else to offer.
            const auto risk = [](const QVariant& entry) {
                const QVariantMap disk = entry.toMap();
                if (!disk.value(QStringLiteral("os")).toString().isEmpty()) return 2;
                if (!disk.value(QStringLiteral("contents")).toString().isEmpty()) return 1;
                return 0;
            };
            std::stable_sort(found.begin(), found.end(),
                             [&risk](const QVariant& a, const QVariant& b) { return risk(a) < risk(b); });
        } else {
            qWarning("omni-installer: omni-install --list exited %d: %s", code,
                     listing_->readAllStandardError().constData());
        }
        disks_ = found;
        listed_ = true;
        listing_->deleteLater();
        listing_ = nullptr;
        emit disksChanged();
    });
    connect(listing_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        qWarning("omni-installer: could not run omni-install: %s",
                 qPrintable(listing_->errorString()));
        disks_.clear();
        listed_ = true;
        listing_->deleteLater();
        listing_ = nullptr;
        emit disksChanged();
    });
    listing_->start(kSudo, {QStringLiteral("-n"), kInstaller, QStringLiteral("--list")});
}

QVariantList InstallerController::timeZones() const {
    // Region/City zones only. The database also has aliases and relics —
    // "US/Pacific", "Etc/GMT+5", "Cuba" — which list the same clocks twice
    // under names nobody looks for.
    static const QStringList kRegions = {
        QStringLiteral("Africa"), QStringLiteral("America"), QStringLiteral("Antarctica"),
        QStringLiteral("Asia"), QStringLiteral("Atlantic"), QStringLiteral("Australia"),
        QStringLiteral("Europe"), QStringLiteral("Indian"), QStringLiteral("Pacific")};

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const auto offsetText = [&now](const QTimeZone& zone) {
        const int seconds = zone.offsetFromUtc(now);
        const int minutes = qAbs(seconds) / 60;
        return QStringLiteral("UTC%1%2:%3")
            .arg(seconds < 0 ? QLatin1Char('-') : QLatin1Char('+'))
            .arg(minutes / 60, 2, 10, QLatin1Char('0'))
            .arg(minutes % 60, 2, 10, QLatin1Char('0'));
    };

    QVariantList zones;
    QVariantMap utc;
    utc.insert(QStringLiteral("id"), QStringLiteral("UTC"));
    utc.insert(QStringLiteral("region"), QString());
    utc.insert(QStringLiteral("city"), QStringLiteral("UTC"));
    utc.insert(QStringLiteral("offset"), QStringLiteral("UTC+00:00"));
    zones.append(utc);

    const QList<QByteArray> ids = QTimeZone::availableTimeZoneIds();
    for (const QByteArray& raw : ids) {
        const QString id = QString::fromLatin1(raw);
        const int slash = id.indexOf(QLatin1Char('/'));
        if (slash < 0 || !kRegions.contains(id.left(slash))) continue;
        QVariantMap zone;
        zone.insert(QStringLiteral("id"), id);
        zone.insert(QStringLiteral("region"), id.left(slash));
        // "America/Argentina/Buenos_Aires" reads as "Argentina / Buenos Aires".
        zone.insert(QStringLiteral("city"),
                    id.mid(slash + 1).replace(QLatin1Char('_'), QLatin1Char(' '))
                        .replace(QLatin1Char('/'), QStringLiteral(" / ")));
        zone.insert(QStringLiteral("offset"), offsetText(QTimeZone(raw)));
        zones.append(zone);
    }
    return zones;
}

QString InstallerController::usernameProblem(const QString& name) const {
    if (name.isEmpty()) return tr("Choose a username");
    static const QRegularExpression kValid(QStringLiteral("^[a-z_][a-z0-9_-]{0,31}$"));
    if (!kValid.match(name).hasMatch()) {
        return tr("A username is lowercase letters, digits, - and _, starting with a letter");
    }
    // Taken by one of the system's own accounts. "omni" is the live user, the
    // one being renamed, so it stays available.
    if (name != QLatin1String("omni") && getpwnam(name.toLocal8Bit().constData()) != nullptr) {
        return tr("\"%1\" is used by the system; choose another").arg(name);
    }
    return {};
}

QString InstallerController::suggestUsername(const QString& fullName) const {
    // The first word, lowercased, with anything a username cannot hold
    // dropped. Accented letters are kept out rather than guessed at.
    const QString first = fullName.trimmed().section(QLatin1Char(' '), 0, 0).toLower();
    QString name;
    for (const QChar c : first) {
        if ((c >= QLatin1Char('a') && c <= QLatin1Char('z')) ||
            (c >= QLatin1Char('0') && c <= QLatin1Char('9')) || c == QLatin1Char('_') ||
            c == QLatin1Char('-')) {
            name += c;
        }
    }
    while (!name.isEmpty() && !name.at(0).isLetter() && name.at(0) != QLatin1Char('_')) name.remove(0, 1);
    return name.left(32);
}

void InstallerController::install(const QString& path, const QVariantMap& account) {
    if (busy_ || finished_) return;

    QVariantMap disk;
    for (const QVariant& entry : std::as_const(disks_)) {
        if (entry.toMap().value(QStringLiteral("path")).toString() == path) disk = entry.toMap();
    }
    if (disk.isEmpty()) {
        setError(tr("%1 is not one of the disks OmniOS can be installed on").arg(path));
        return;
    }
    // A disk that holds anything is only erased after it was typed for. The
    // screen does not offer the button without it; this is here so that no
    // other path through the code can skip it either.
    const QString installed = disk.value(QStringLiteral("os")).toString();
    const bool holdsSomething = !installed.isEmpty() ||
                                !disk.value(QStringLiteral("contents")).toString().isEmpty();
    if (holdsSomething && !account.value(QStringLiteral("eraseConfirmed")).toBool()) {
        setError(tr("%1 has files on it, and erasing it was not confirmed").arg(path));
        return;
    }

    busy_ = true;
    error_.clear();
    progress_ = 0;
    progressText_ = tr("Starting ...");
    pending_.clear();
    emit stateChanged();

    installing_ = new QProcess(this);
    // stderr is the log's business; the protocol is on stdout.
    installing_->setProcessChannelMode(QProcess::SeparateChannels);
    connect(installing_, &QProcess::readyReadStandardOutput, this, &InstallerController::readProgress);
    connect(installing_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        readProgress();
        busy_ = false;
        if (code == 0 && status == QProcess::NormalExit && progress_ >= 100) {
            finished_ = true;
        } else if (error_.isEmpty()) {
            // omni-install always says why it failed; this is for the times it
            // could not, such as being killed.
            error_ = tr("The installer stopped before it finished. The details are in /tmp/omnios-install.log.");
        }
        installing_->deleteLater();
        installing_ = nullptr;
        emit stateChanged();
    });
    connect(installing_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        busy_ = false;
        error_ = tr("Could not start the installer: %1").arg(installing_->errorString());
        installing_->deleteLater();
        installing_ = nullptr;
        emit stateChanged();
    });

    // The disk is named twice on purpose; see omni-install.
    QStringList args = {QStringLiteral("-n"), kInstaller,
                        QStringLiteral("--disk"), path,
                        QStringLiteral("--erase"), path};
    // And an operating system on it by name, which omni-install checks
    // against what it finds there itself.
    if (!installed.isEmpty()) args << QStringLiteral("--erase-os") << installed;

    QByteArray password;
    if (!account.value(QStringLiteral("skip")).toBool()) {
        const QString username = account.value(QStringLiteral("username")).toString();
        const QString problem = usernameProblem(username);
        if (!problem.isEmpty()) {
            busy_ = false;
            installing_->deleteLater();
            installing_ = nullptr;
            setError(problem);
            return;
        }
        args << QStringLiteral("--user") << username
             << QStringLiteral("--full-name") << account.value(QStringLiteral("fullName")).toString();
        if (!account.value(QStringLiteral("autologin"), true).toBool()) {
            args << QStringLiteral("--no-autologin");
        }
        password = account.value(QStringLiteral("password")).toString().toUtf8();
        if (!password.isEmpty()) args << QStringLiteral("--password-stdin");
    }
    const QString hostname = account.value(QStringLiteral("hostname")).toString();
    if (!hostname.isEmpty()) args << QStringLiteral("--hostname") << hostname;
    const QString zone = account.value(QStringLiteral("timezone")).toString();
    if (!zone.isEmpty()) args << QStringLiteral("--timezone") << zone;

    installing_->start(kSudo, args);
    // The password goes down stdin, never into args: every process on the
    // machine can read another's command line.
    if (!password.isEmpty()) {
        installing_->write(password + '\n');
        password.fill('\0');
    }
    installing_->closeWriteChannel();
}

void InstallerController::readProgress() {
    if (installing_ == nullptr) return;
    pending_ += installing_->readAllStandardOutput();

    int newline;
    while ((newline = pending_.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(pending_.left(newline)).trimmed();
        pending_.remove(0, newline + 1);

        if (line.startsWith(QLatin1String("PROGRESS "))) {
            const QString rest = line.mid(9);
            const int space = rest.indexOf(QLatin1Char(' '));
            progress_ = qBound(0, rest.left(space).toInt(), 100);
            progressText_ = space > 0 ? rest.mid(space + 1) : QString();
            emit stateChanged();
        } else if (line.startsWith(QLatin1String("ERROR "))) {
            error_ = line.mid(6);
            emit stateChanged();
        }
    }
}

void InstallerController::reset() {
    if (busy_) return;
    error_.clear();
    progress_ = 0;
    progressText_.clear();
    emit stateChanged();
    refresh();
}

void InstallerController::restart() {
    QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("reboot")});
}

void InstallerController::setError(const QString& text) {
    error_ = text;
    emit stateChanged();
}

#include "GreeterController.h"

#include "KeyDelivery.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSysInfo>
#include <QVariantMap>

#include <cstring>

#include <pwd.h>

namespace {

// Regular accounts only. System accounts live below 1000, nobody at 65534;
// the greeter user itself has no login shell.
bool isPerson(const passwd* entry) {
    if (entry->pw_uid < 1000 || entry->pw_uid >= 60000) return false;
    const QString shell = QString::fromLocal8Bit(entry->pw_shell);
    return !shell.endsWith(QLatin1String("nologin")) && !shell.endsWith(QLatin1String("false"));
}

}  // namespace

GreeterController::GreeterController(QObject* parent) : QObject(parent) {
    setpwent();
    while (const passwd* entry = getpwent()) {
        if (!isPerson(entry)) continue;
        const QString name = QString::fromLocal8Bit(entry->pw_name);
        // The name field is "Full Name,room,phone,...": the first part only.
        QString full = QString::fromLocal8Bit(entry->pw_gecos).section(QLatin1Char(','), 0, 0).trimmed();
        QVariantMap user;
        user.insert(QStringLiteral("name"), name);
        user.insert(QStringLiteral("fullName"), full.isEmpty() ? name : full);
        users_.append(user);
    }
    endpwent();

    connect(&socket_, &QLocalSocket::readyRead, this, &GreeterController::readMessages);
    connect(&socket_, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
        if (state_ != State::Idle) fail(tr("Lost contact with the sign-in service: %1").arg(socket_.errorString()));
    });

    connect(&gamepad_, &GamepadInput::keyPressed, this,
            [this](int key) { deliverKey(key, [this] { emit focusWanted(); }); });
}

QString GreeterController::hostname() const {
    return QSysInfo::machineHostName();
}

void GreeterController::signIn(const QString& user, const QString& password, const QString& mode) {
    if (state_ != State::Idle) return;

    error_.clear();
    const QString path = qEnvironmentVariable("GREETD_SOCK");
    if (path.isEmpty()) {
        fail(tr("This screen has to be started by greetd"));
        return;
    }
    if (socket_.state() != QLocalSocket::ConnectedState) {
        socket_.connectToServer(path);
        if (!socket_.waitForConnected(3000)) {
            fail(tr("Could not reach the sign-in service: %1").arg(socket_.errorString()));
            return;
        }
    }

    mode_ = mode == QLatin1String("game") ? QStringLiteral("game") : QStringLiteral("desktop");
    password_ = password.toUtf8();
    state_ = State::Authenticating;
    emit stateChanged();

    send({{QStringLiteral("type"), QStringLiteral("create_session")},
          {QStringLiteral("username"), user}});
}

void GreeterController::send(const QJsonObject& message) {
    const QByteArray json = QJsonDocument(message).toJson(QJsonDocument::Compact);
    // The length in the machine's own byte order, as greetd reads it.
    const quint32 length = static_cast<quint32>(json.size());
    QByteArray frame(sizeof(length), '\0');
    std::memcpy(frame.data(), &length, sizeof(length));
    frame += json;
    socket_.write(frame);
    socket_.flush();
}

void GreeterController::readMessages() {
    buffer_ += socket_.readAll();
    while (buffer_.size() >= static_cast<int>(sizeof(quint32))) {
        quint32 length = 0;
        std::memcpy(&length, buffer_.constData(), sizeof(length));
        if (buffer_.size() < static_cast<int>(sizeof(length) + length)) return;
        const QByteArray json = buffer_.mid(sizeof(length), length);
        buffer_.remove(0, sizeof(length) + length);
        handle(QJsonDocument::fromJson(json).object());
    }
}

void GreeterController::handle(const QJsonObject& message) {
    const QString type = message.value(QStringLiteral("type")).toString();

    if (state_ == State::Cancelling) {
        // Whatever greetd says to a cancel, the attempt is over.
        state_ = State::Idle;
        emit stateChanged();
        return;
    }

    if (type == QLatin1String("auth_message")) {
        const QString kind = message.value(QStringLiteral("auth_message_type")).toString();
        QJsonObject reply{{QStringLiteral("type"), QStringLiteral("post_auth_message_response")}};
        // PAM asks for the password as a "secret"; the rare "visible" question
        // gets the same answer, there being only one thing on the screen to
        // give it. "info" and "error" are statements, acknowledged with no
        // response.
        if (kind == QLatin1String("secret") || kind == QLatin1String("visible")) {
            reply.insert(QStringLiteral("response"), QString::fromUtf8(password_));
        }
        send(reply);
        return;
    }

    if (type == QLatin1String("success")) {
        if (state_ == State::Authenticating) {
            forgetPassword();
            state_ = State::Starting;
            // A login shell, so .bash_profile runs its session loop exactly as
            // it does after tty1 signs in by itself. The loop reads which
            // session to start from here, and knows from OMNIOS_GREETER that
            // signing out should come back to this screen.
            send({{QStringLiteral("type"), QStringLiteral("start_session")},
                  {QStringLiteral("cmd"), QJsonArray{QStringLiteral("bash"), QStringLiteral("-l")}},
                  {QStringLiteral("env"), QJsonArray{QStringLiteral("OMNIOS_START_MODE=") + mode_,
                                                     QStringLiteral("OMNIOS_GREETER=1")}}});
        } else if (state_ == State::Starting) {
            // greetd starts the session when the greeter exits.
            QCoreApplication::quit();
        }
        return;
    }

    if (type == QLatin1String("error")) {
        const bool wrongPassword =
            message.value(QStringLiteral("error_type")).toString() == QLatin1String("auth_error");
        fail(wrongPassword ? tr("That password is not right")
                           : message.value(QStringLiteral("description")).toString());
    }
}

void GreeterController::fail(const QString& text) {
    forgetPassword();
    error_ = text.isEmpty() ? tr("Signing in did not work") : text;
    // greetd keeps the half-made session until told otherwise, and will not
    // start another for the next attempt.
    if (socket_.state() == QLocalSocket::ConnectedState &&
        (state_ == State::Authenticating || state_ == State::Starting)) {
        state_ = State::Cancelling;
        send({{QStringLiteral("type"), QStringLiteral("cancel_session")}});
    } else {
        state_ = State::Idle;
    }
    emit stateChanged();
}

void GreeterController::forgetPassword() {
    password_.fill('\0');
    password_.clear();
}

void GreeterController::clearError() {
    if (error_.isEmpty()) return;
    error_.clear();
    emit stateChanged();
}

void GreeterController::powerAction(const QString& action) {
    // logind lets the active local session restart and shut down the machine;
    // the greeter's session is that session while this screen is up.
    if (action == QLatin1String("reboot") || action == QLatin1String("poweroff")) {
        QProcess::startDetached(QStringLiteral("systemctl"), {action});
    }
}

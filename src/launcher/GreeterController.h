// The sign-in screen's side of the shell (omni-launcher-qml --greeter).
//
// Shown by greetd on a machine installed with "sign in automatically" off. It
// talks greetd's IPC — a 32-bit native-endian length, then a JSON object, over
// the socket in $GREETD_SOCK — to check a password and start the session. PAM
// does the checking, inside greetd; this process never sees whether a password
// is right, only greetd's answer, and it runs as the unprivileged greeter user.
//
// greetd starts the session once this process exits, so a successful sign-in
// ends with the app quitting.
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QString>
#include <QVariantList>

#include "GamepadInput.h"

class GreeterController : public QObject {
    Q_OBJECT

    // [{ name: "shyam", fullName: "Shyam Sangeeth" }, ...] — the people who can
    // sign in: regular accounts with a login shell.
    Q_PROPERTY(QVariantList users READ users CONSTANT)
    Q_PROPERTY(QString hostname READ hostname CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)

public:
    explicit GreeterController(QObject* parent = nullptr);

    QVariantList users() const { return users_; }
    QString hostname() const;
    bool busy() const { return state_ != State::Idle; }
    QString error() const { return error_; }

    // mode is "desktop" or "game": which session the login loop starts in.
    Q_INVOKABLE void signIn(const QString& user, const QString& password, const QString& mode);
    // "reboot" or "poweroff". Anything else is ignored: the string comes from QML.
    Q_INVOKABLE void powerAction(const QString& action);
    Q_INVOKABLE void clearError();

signals:
    void stateChanged();
    void focusWanted();

private:
    enum class State { Idle, Authenticating, Starting, Cancelling };

    void send(const QJsonObject& message);
    void readMessages();
    void handle(const QJsonObject& message);
    void fail(const QString& text);
    void forgetPassword();

    QVariantList users_;
    QLocalSocket socket_;
    QByteArray buffer_;
    State state_ = State::Idle;
    QString error_;
    QString mode_;
    QByteArray password_;
    GamepadInput gamepad_;
};

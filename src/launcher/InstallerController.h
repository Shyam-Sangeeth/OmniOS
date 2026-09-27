// The disk installer's side of the shell — Phase 13.2 (OmniOS.md §15).
//
// The QML asks, this answers: which disks OmniOS can go on, and how far an
// install has got. The work itself is omni-install, run through sudo; this
// only starts it, reads its progress lines and reports them. Keeping the
// dangerous half in one script means there is exactly one place that decides
// which disks may be erased, and the UI cannot disagree with it.
#pragma once

#include <QObject>
#include <QProcess>
#include <QVariantList>

#include "GamepadInput.h"
#include "InputMode.h"

class InstallerController : public QObject {
    Q_OBJECT
    // Whether a controller is in use, and what it calls its buttons — for the
    // on-screen keyboard its text boxes bring up. See InputMode.
    Q_PROPERTY(InputMode* input READ input CONSTANT)

    // [{ path, model, transport, contents, os, size: "1.0 TB" }, ...], the
    // safest first: empty disks, then disks with files, then disks with an
    // operating system on them.
    Q_PROPERTY(QVariantList disks READ disks NOTIFY disksChanged)
    // The list has been asked for at least once, so an empty one means "none"
    // rather than "not looked yet".
    Q_PROPERTY(bool listed READ listed NOTIFY disksChanged)

    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool finished READ finished NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(int progress READ progress NOTIFY stateChanged)
    Q_PROPERTY(QString progressText READ progressText NOTIFY stateChanged)

    // Fullscreen in Game Mode, where it is the only thing on screen; a window
    // on the desktop, where it is one app among others.
    Q_PROPERTY(bool fullscreen READ fullscreen CONSTANT)
    // The smallest disk omni-install accepts, for the "no disk" message.
    Q_PROPERTY(QString minimumSize READ minimumSize CONSTANT)

    // [{ id: "Asia/Kolkata", region: "Asia", city: "Kolkata", offset: "UTC+05:30" }, ...]
    Q_PROPERTY(QVariantList timeZones READ timeZones CONSTANT)

    // The languages the installed system can be set to, from glibc's list of
    // the locales it can generate: [{ id: "de_DE.UTF-8", name: "Deutsch
    // (Deutschland)", english: "German (Germany)" }, ...], by their own names.
    Q_PROPERTY(QVariantList languages READ languages CONSTANT)
    // Keyboard layouts, from xkeyboard-config: [{ id: "de", name: "German" }, ...].
    Q_PROPERTY(QVariantList keyboards READ keyboards CONSTANT)
    // What the live session is using now, as a layout id; "us" by default.
    Q_PROPERTY(QString currentKeyboard READ currentKeyboard CONSTANT)

public:
    explicit InstallerController(QObject* parent = nullptr);

    QVariantList disks() const { return disks_; }
    bool listed() const { return listed_; }
    bool busy() const { return busy_; }
    bool finished() const { return finished_; }
    QString error() const { return error_; }
    int progress() const { return progress_; }
    QString progressText() const { return progressText_; }
    bool fullscreen() const;
    QString minimumSize() const { return QStringLiteral("24 GB"); }
    QVariantList timeZones() const;
    QVariantList languages() const;
    QVariantList keyboards() const;
    QString currentKeyboard() const;

    // The keyboard layout people who speak this language usually have, or
    // empty to leave the choice alone. Only Latin-script layouts are ever
    // suggested: a username has to be typed in Latin letters on the very next
    // page, so a Cyrillic or Arabic layout is something to choose, not to be
    // given.
    Q_INVOKABLE QString suggestKeyboard(const QString& language) const;

    // Whether a layout types Latin letters. Those that do not are installed
    // behind US English (see keyboardLayouts).
    Q_INVOKABLE bool isLatinKeyboard(const QString& layout) const;

    // Switches the live session to this layout now, through KDE's own setting,
    // so the password typed on the account page is typed as it will be on the
    // installed system.
    Q_INVOKABLE void applyKeyboard(const QString& layout);

    // Why a username cannot be used, or empty when it can. The same rules as
    // omni-install, asked here so the account screen can say so while the
    // name is being typed rather than after the disk has been chosen.
    Q_INVOKABLE QString usernameProblem(const QString& name) const;
    // A username made from a person's name: "Shyam Sangeeth" -> "shyam".
    Q_INVOKABLE QString suggestUsername(const QString& fullName) const;

    // Asks omni-install which disks it would accept. Asynchronous; disks
    // changes when the answer arrives.
    Q_INVOKABLE void refresh();

    // Erases the disk and installs. The path has to be one refresh() listed —
    // omni-install checks that again itself, but a UI that offers one disk and
    // installs to another should fail here first.
    //
    // account: { skip, fullName, username, password, hostname, autologin,
    // timezone, language, keyboard, eraseConfirmed }. With skip set the installed account is the
    // live image's. eraseConfirmed has to be true for a disk that holds
    // anything: the screen sets it only once "erase" has been typed.
    Q_INVOKABLE void install(const QString& path, const QVariantMap& account);

    // Clears a failure so the disk list can be shown again.
    Q_INVOKABLE void reset();

    Q_INVOKABLE void restart();

public:
    InputMode* input() { return &input_; }

signals:
    void disksChanged();
    void stateChanged();
    // The shell should make sure something inside it has focus before a
    // controller press is delivered, as the launcher does.
    void focusWanted();

private:
    void readProgress();
    void setError(const QString& text);

    // The layouts to configure for a choice: the layout itself, or "us,<it>"
    // for one that does not type Latin letters, so a username and password can
    // still be typed and US English is where every session starts.
    QString keyboardLayouts(const QString& layout) const;

    QVariantList disks_;
    bool listed_ = false;
    bool busy_ = false;
    bool finished_ = false;
    QString error_;
    int progress_ = 0;
    QString progressText_;

    QProcess* listing_ = nullptr;
    QProcess* installing_ = nullptr;
    QByteArray pending_;
    GamepadInput gamepad_;
    InputMode    input_{&gamepad_};
};

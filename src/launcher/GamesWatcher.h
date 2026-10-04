// ~/Games, watched, so a game copied in gets its tile without a rescan
// (OmniOS.md 7.1, 7.6).
//
// inotify over the whole tree, a watch per folder, since a game can be a
// folder of folders (PS3, PS4, Switch dumps) as easily as one file. settled()
// is the moment to rescan: something changed, and then nothing for two
// seconds.
//
// Not halfway through a copy, though. A file is noticed when it is created,
// long before the last of it is written, and a tile for half an ISO would
// start and fail. So a file created and not yet closed holds the rescan back
// until it is closed — or until half an hour has passed with nothing else
// happening, for a copy that died with the file still open. Writes count as
// changes too, for the file in a new folder that was created before the
// folder had a watch. A game in several files (a .cue and its tracks, a PS3
// folder) settles once, after the last.
//
// Folders named in `skip`, directly under the root, are left alone: Steam's
// library (polled on its own, and a Steam download writes thousands of
// files) and the BIOS folder, which holds no games.
#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

class QSocketNotifier;

class GamesWatcher : public QObject {
    Q_OBJECT

public:
    explicit GamesWatcher(QObject* parent = nullptr);
    ~GamesWatcher() override;

    // Starts watching `root` and everything under it. False, with a warning
    // logged, when inotify could not be had; the library then changes on a
    // rescan only, as it did before.
    bool start(const QString& root, const QStringList& skip);

signals:
    void settled();

private:
    void readEvents();
    void watchTree(const QString& dir);
    void forgetTree(const QString& dir);
    bool skipped(const QString& path) const;
    void settle();

    int fd_ = -1;
    QSocketNotifier* notifier_ = nullptr;
    QString root_;
    QStringList skip_;
    QHash<int, QString> dirs_;  // watch descriptor -> folder
    QSet<QString> writing_;     // files created and not closed yet
    bool changed_ = false;
    QElapsedTimer lastEvent_;
    QTimer quiet_;
};

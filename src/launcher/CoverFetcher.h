// Box art for games that have none, from libretro's thumbnail collection
// (omnios/CoverArt.h), fetched in the background after each scan.
//
// A system's listing is downloaded once and kept for a month, under
// ~/.omnios/library/thumbnails; each game is matched against it here, and only
// its image is fetched, into ~/.omnios/library/covers. A game with no match is
// noted and not asked about again for a month, so a library of homebrew does
// not send the same questions on every scan. With no network nothing is noted:
// it is tried again next time.
#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

#include <deque>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "omnios/GameLibrary.h"

class QNetworkReply;

class CoverFetcher : public QObject {
    Q_OBJECT
public:
    explicit CoverFetcher(QObject* parent = nullptr);

    // Looks for box art for every game in `library` without a cover.
    void fetchMissing(const omnios::GameLibrary& library);

    // Looks again for one game's box art now, even if none was found for it
    // lately (the game's menu, "Look for cover art").
    void retry(const omnios::Game& game);

signals:
    // `path` is the cover's file, ready to show.
    void coverReady(const QString& gameId, const QString& path);
    // A game asked about with retry() has none: the collection has no such
    // title, or (`offline`) the collection could not be reached.
    void notFound(const QString& gameId, bool offline);

private:
    struct Wanted {
        QString               id;
        std::string           lookup;  // the name it is matched by
        std::filesystem::path file;    // where its cover goes
    };

    // Queues `game` unless it has a cover, has none to find, or was found to
    // have none within the month (unless `force`).
    void enqueue(const omnios::Game& game, bool force);
    void reportMissing(const QString& id, bool offline);

    void nextSystem();
    void matchAll(const std::vector<std::string>& names);
    void downloadNext();
    void noteMissing(const QString& id);
    void saveMissing() const;

    QNetworkAccessManager net_;
    // Waiting, by system; the system being worked on and what it still needs.
    std::map<std::string, std::vector<Wanted>> queue_;
    std::string                                system_;
    std::vector<Wanted>                        wanted_;
    std::deque<std::pair<Wanted, std::string>> downloads_;  // and its box art name
    std::set<QString>                          queued_;
    bool                                       busy_ = false;
    // Games found to have no box art, and when (seconds since the epoch).
    std::map<QString, qint64>                  missing_;
    // Games retry() was asked about, whose answer is awaited.
    std::set<QString>                          asked_;
};

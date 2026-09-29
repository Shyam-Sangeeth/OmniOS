#include "CoverFetcher.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QUrl>

#include "omnios/CoverArt.h"
#include "omnios/Paths.h"

namespace {

// How long a system's listing, and a "no box art" answer, are trusted.
constexpr qint64 kMonthSecs = 30LL * 24 * 60 * 60;

QString qstr(const std::filesystem::path& path) { return QString::fromStdString(path.string()); }

std::filesystem::path thumbnailsDir() { return omnios::libraryDir() / "thumbnails"; }
std::filesystem::path missingFile() { return omnios::libraryDir() / "covers" / "none.json"; }

// A system's listing, one name a line; the file is named after the system.
std::filesystem::path indexFile(const std::string& system) { return thumbnailsDir() / (system + ".txt"); }

QNetworkRequest request(const std::string& url) {
    QNetworkRequest r(QUrl::fromEncoded(QByteArray::fromStdString(url)));
    r.setTransferTimeout(60000);
    r.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OmniOS"));
    return r;
}

}  // namespace

CoverFetcher::CoverFetcher(QObject* parent) : QObject(parent) {
    QFile file(qstr(missingFile()));
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonObject missing = QJsonDocument::fromJson(file.readAll()).object();
        for (auto it = missing.begin(); it != missing.end(); ++it)
            missing_[it.key()] = static_cast<qint64>(it.value().toDouble());
    }
}

void CoverFetcher::fetchMissing(const omnios::GameLibrary& library) {
    for (const omnios::Game& game : library.games()) enqueue(game, false);
    if (!busy_) nextSystem();
}

void CoverFetcher::retry(const omnios::Game& game) {
    const QString id = QString::fromStdString(game.id);
    asked_.insert(id);
    missing_.erase(id);
    enqueue(game, true);
    if (!busy_) nextSystem();
}

void CoverFetcher::enqueue(const omnios::Game& game, bool force) {
    if (!game.coverPath.empty()) return;
    const std::string system = omnios::thumbnailSystem(game);
    if (system.empty()) return;
    const QString id = QString::fromStdString(game.id);
    // Fetched before, and only the library forgot it (a new cache).
    const std::filesystem::path file = omnios::coverCacheFile(game);
    if (QFileInfo(qstr(file)).isFile()) {
        asked_.erase(id);
        emit coverReady(id, qstr(file));
        return;
    }
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (const auto it = missing_.find(id); !force && it != missing_.end() && now - it->second < kMonthSecs) return;
    if (!queued_.insert(id).second) return;
    queue_[system].push_back({id, omnios::thumbnailLookupName(game), file});
}

void CoverFetcher::reportMissing(const QString& id, bool offline) {
    if (asked_.erase(id) != 0) emit notFound(id, offline);
}

void CoverFetcher::nextSystem() {
    if (queue_.empty()) {
        busy_ = false;
        return;
    }
    busy_ = true;
    system_ = queue_.begin()->first;
    wanted_ = std::move(queue_.begin()->second);
    queue_.erase(queue_.begin());

    // The listing kept from before, while it is fresh.
    const QFileInfo cached(qstr(indexFile(system_)));
    if (cached.isFile() && cached.lastModified().secsTo(QDateTime::currentDateTime()) < kMonthSecs) {
        QFile file(cached.filePath());
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            std::vector<std::string> names;
            for (const QByteArray& line : file.readAll().split('\n'))
                if (!line.isEmpty()) names.push_back(line.toStdString());
            matchAll(names);
            return;
        }
    }

    QNetworkReply* reply = net_.get(request(omnios::thumbnailIndexUrl(system_)));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            // No network, or the server is away: nothing is noted, and these
            // games are asked about again on the next scan.
            qInfo("covers: no listing for %s: %s", system_.c_str(), qPrintable(reply->errorString()));
            for (const Wanted& w : wanted_) {
                queued_.erase(w.id);
                reportMissing(w.id, true);
            }
            wanted_.clear();
            nextSystem();
            return;
        }
        const std::vector<std::string> names = omnios::parseThumbnailIndex(reply->readAll().toStdString());
        QDir().mkpath(qstr(thumbnailsDir()));
        QSaveFile file(qstr(indexFile(system_)));
        if (!names.empty() && file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            for (const std::string& name : names) file.write(QByteArray::fromStdString(name + "\n"));
            file.commit();
        }
        matchAll(names);
    });
}

void CoverFetcher::matchAll(const std::vector<std::string>& names) {
    for (Wanted& w : wanted_) {
        const std::string name = omnios::bestThumbnail(w.lookup, names);
        if (name.empty() && !names.empty()) {
            noteMissing(w.id);
            queued_.erase(w.id);
            reportMissing(w.id, false);
        } else if (!name.empty()) {
            downloads_.emplace_back(std::move(w), name);
        } else {
            queued_.erase(w.id);  // an empty listing says nothing about the game
            reportMissing(w.id, true);
        }
    }
    wanted_.clear();
    saveMissing();
    downloadNext();
}

void CoverFetcher::downloadNext() {
    if (downloads_.empty()) {
        nextSystem();
        return;
    }
    auto [wanted, name] = std::move(downloads_.front());
    downloads_.pop_front();
    QNetworkReply* reply = net_.get(request(omnios::thumbnailUrl(system_, name)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, wanted = std::move(wanted)]() {
        reply->deleteLater();
        queued_.erase(wanted.id);
        const QByteArray image = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError && image.startsWith("\x89PNG")) {
            QDir().mkpath(qstr(wanted.file.parent_path()));
            QSaveFile file(qstr(wanted.file));
            if (file.open(QIODevice::WriteOnly)) {
                file.write(image);
                if (file.commit()) {
                    asked_.erase(wanted.id);
                    emit coverReady(wanted.id, qstr(wanted.file));
                }
            }
        } else if (status == 404) {
            noteMissing(wanted.id);  // listed, yet not there: not asked again soon
            saveMissing();
            reportMissing(wanted.id, false);
        } else {
            reportMissing(wanted.id, true);
        }
        downloadNext();
    });
}

void CoverFetcher::noteMissing(const QString& id) {
    missing_[id] = QDateTime::currentSecsSinceEpoch();
}

void CoverFetcher::saveMissing() const {
    QJsonObject missing;
    for (const auto& [id, when] : missing_) missing.insert(id, static_cast<double>(when));
    QDir().mkpath(qstr(missingFile().parent_path()));
    QSaveFile file(qstr(missingFile()));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(missing).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

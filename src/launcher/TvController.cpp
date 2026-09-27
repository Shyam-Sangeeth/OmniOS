#include "TvController.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QGuiApplication>
#include <QLocale>
#include <QScreen>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QTimeZone>
#include <QUrl>

#include <zlib.h>

#include <algorithm>
#include <memory>
#include <unordered_set>
#include <utility>

#include "KeyDelivery.h"
#include "omnios/Iptv.h"

namespace {

QString cacheDir() { return QDir::homePath() + QStringLiteral("/.cache/omnios/tv"); }
QString favoritesFile() { return QDir::homePath() + QStringLiteral("/.local/share/omnios/tv-favorites.json"); }

// mpv's keys, for the channels only it can play: every obvious way out
// leaves (mpv's own Esc only leaves full screen, which on a TV is nothing
// anyone wants, and it has no button for it at all).
const char kInputConf[] =
    "ESC quit\n"
    "BS quit\n"
    "q quit\n"
    "MBTN_RIGHT quit\n"
    "MBTN_BACK quit\n";

constexpr omnios::TvFacet kFacets[3] = {omnios::TvFacet::Country, omnios::TvFacet::Category,
                                        omnios::TvFacet::Language};
const char* const kCacheNames[3] = {"index-country.m3u", "index-category.m3u", "index-language.m3u"};

QStringList toQt(const std::vector<std::string>& values) {
    QStringList out;
    for (const std::string& value : values) out << QString::fromStdString(value);
    return out;
}

// A .gz file's contents; empty if it is not one, or is cut short.
std::string gunzip(const QByteArray& data) {
    std::string out;
    z_stream stream{};
    // 16 + the window: a gzip header, not zlib's own.
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) return out;
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.constData()));
    stream.avail_in = static_cast<uInt>(data.size());
    out.reserve(static_cast<std::size_t>(data.size()) * 12);
    std::vector<char> buffer(1 << 18);
    int result = Z_OK;
    while (result == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = static_cast<uInt>(buffer.size());
        result = inflate(&stream, Z_NO_FLUSH);
        if (result != Z_OK && result != Z_STREAM_END) { out.clear(); break; }
        out.append(buffer.data(), buffer.size() - stream.avail_out);
    }
    inflateEnd(&stream);
    return out;
}

// Guides are read from three hours back, for a programme that started a while
// ago, to a day and a half ahead: enough to last until the next reading.
constexpr qint64 kGuideBack = 3 * 3600;
constexpr qint64 kGuideAhead = 36 * 3600;
// Read again after this long, while the window is still open.
constexpr qint64 kGuideRefreshMs = 12LL * 3600 * 1000;

}  // namespace

TvController::TvController(QObject* parent) : QObject(parent) {
    // The pad: a remote while something plays, the keyboard's arrows and
    // Enter while browsing.
    connect(&gamepad_, &GamepadInput::keyPressed, this, [this](int key) {
        if (player_ != nullptr) { remote(key); return; }
        // Every program reading the pad sees every press. With another window
        // in front (the launcher, after the Guide button) the press is that
        // window's, and TV behind it must not act on it too. The exception is
        // the moment after mpv closes, before TV has been given focus back.
        if (QGuiApplication::applicationState() != Qt::ApplicationActive
            && !(playerClosed_.isValid() && playerClosed_.elapsed() < 3000))
            return;
        deliverKey(key, [this] { emit focusWanted(); });
    });

    QDir().mkpath(cacheDir());
    QFile conf(cacheDir() + QStringLiteral("/input.conf"));
    if (conf.open(QIODevice::WriteOnly | QIODevice::Truncate)) conf.write(kInputConf);

    // Where the viewer is, so their own country's channels open first: from
    // the language setting (the installer's choice), then from the time zone.
    // The environment's LANG is only one of these and not always there — a
    // process started outside the session has none — so the system's own
    // /etc/locale.conf is read too.
    const auto usable = [](QLocale::Territory t) {
        return t != QLocale::AnyTerritory && t != QLocale::World;
    };
    QLocale::Territory territory = QLocale::system().territory();
    if (!usable(territory)) {
        QFile locale(QStringLiteral("/etc/locale.conf"));
        if (locale.open(QIODevice::ReadOnly | QIODevice::Text)) {
            for (const QByteArray& line : locale.readAll().split('\n')) {
                if (!line.startsWith("LANG=")) continue;
                const QString value = QString::fromUtf8(line.mid(5)).remove(QLatin1Char('"'));
                territory = QLocale(value.section(QLatin1Char('.'), 0, 0)).territory();
            }
        }
    }
    if (!usable(territory)) territory = QTimeZone::systemTimeZone().territory();
    // iptv-org groups by the country's English name, as Qt spells it too for
    // all but a handful; one that does not match simply starts on all.
    if (usable(territory)) homeCountry_ = QLocale::territoryToString(territory);

    // One guide read at a time: a country's is up to 70 MB unpacked.
    pool_.setMaxThreadCount(1);
    // What is on changes with the clock: twice a minute is close enough.
    clock_.setInterval(30000);
    connect(&clock_, &QTimer::timeout, this, [this] {
        wantGuides();  // refreshed when half a day old
        if (guide_.empty()) return;
        ++guideRevision_;
        emit guideChanged();
    });
    clock_.start();

    loadCatalog();
}

TvController::~TvController() {
    if (player_ != nullptr) {
        player_->disconnect(this);
        player_->terminate();
        player_->waitForFinished(1000);
    }
}

void TvController::setMessage(const QString& text) {
    if (message_ == text) return;
    message_ = text;
    emit stateChanged();
}

void TvController::fetch(const QString& url, const QString& cacheName,
                         std::function<void(const QByteArray&, const QString&)> done, bool counted) {
    const QString path = cacheDir() + QLatin1Char('/') + cacheName;
    const QFileInfo cached(path);
    const auto readCache = [path]() {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    };
    // iptv-org rebuilds its lists daily; a copy from today is as good as new.
    if (cached.exists() && cached.lastModified().secsTo(QDateTime::currentDateTime()) < 24 * 3600) {
        done(readCache(), QString());
        return;
    }

    QNetworkRequest request{QUrl(url)};
    // A few megabytes each; generous, for a slow line.
    request.setTransferTimeout(60000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = network_.get(request);
    if (counted) {
        ++pending_;
        emit stateChanged();
    }
    connect(reply, &QNetworkReply::finished, this, [this, reply, path, cached, readCache, done, counted]() {
        reply->deleteLater();
        if (counted) {
            --pending_;
            emit stateChanged();
        }
        if (reply->error() == QNetworkReply::NoError) {
            const QByteArray data = reply->readAll();
            QFile file(path);
            if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) file.write(data);
            done(data, QString());
            return;
        }
        if (cached.exists()) {
            done(readCache(), tr("Offline  -  showing the list saved %1")
                                  .arg(QLocale().toString(cached.lastModified().date(), QLocale::ShortFormat)));
        } else {
            done(QByteArray(), tr("Could not reach iptv-org  -  is the network up?"));
        }
    });
}

void TvController::loadCatalog() {
    // The whole list, three ways; merged once all three are in.
    for (int facet = 0; facet < 3; ++facet) {
        fetch(QString::fromStdString(omnios::iptvGroupedPlaylistUrl(kFacets[facet])),
              QString::fromLatin1(kCacheNames[facet]),
              [this, facet](const QByteArray& data, const QString& note) {
                  playlists_[facet] = data;
                  if (!note.isEmpty()) loadNote_ = note;
                  if (++arrived_ == 3) buildEntries();
              });
    }
}

void TvController::buildEntries() {
    omnios::TvCatalog catalog;
    for (int facet = 0; facet < 3; ++facet) {
        catalog.add(std::string_view(playlists_[facet].constData(),
                                     static_cast<std::size_t>(playlists_[facet].size())),
                    kFacets[facet]);
        playlists_[facet].clear();  // megabytes, and read
    }

    entries_.clear();
    entries_.reserve(catalog.listings().size());
    for (const omnios::TvListing& listing : catalog.listings()) {
        Entry entry;
        entry.url = QString::fromStdString(listing.channel.url);
        const QString name = QString::fromStdString(listing.channel.name);
        entry.lowerName = name.toLower();
        entry.country = QString::fromStdString(omnios::guideCountry(listing.channel.id));
        entry.guideKey = omnios::guideKey(listing.channel.name);
        for (int facet = 0; facet < 3; ++facet) {
            entry.facets[facet] = toQt(facet == 0 ? listing.countries
                                       : facet == 1 ? listing.categories : listing.languages);
        }
        entry.map = QVariantMap{
            {QStringLiteral("name"), name},
            {QStringLiteral("logo"), QString::fromStdString(listing.channel.logo)},
            {QStringLiteral("categories"), entry.facets[1].join(QStringLiteral(", "))},
            {QStringLiteral("url"), entry.url},
            {QStringLiteral("userAgent"), QString::fromStdString(listing.channel.userAgent)},
            {QStringLiteral("referrer"), QString::fromStdString(listing.channel.referrer)}};
        entries_.push_back(std::move(entry));
    }
    std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
        return a.lowerName.localeAwareCompare(b.lowerName) < 0;
    });

    // Each country's code, as most of its channels' ids give it: "India" is
    // "in". Guides are published by code.
    byUrl_.clear();
    QHash<QString, QHash<QString, int>> votes;
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        const Entry& entry = entries_[static_cast<std::size_t>(i)];
        byUrl_.insert(entry.url, i);
        if (entry.country.isEmpty()) continue;
        for (const QString& name : entry.facets[0]) ++votes[name][entry.country];
    }
    countryCodes_.clear();
    for (auto country = votes.cbegin(); country != votes.cend(); ++country) {
        QString best;
        int most = 0;
        for (auto code = country->cbegin(); code != country->cend(); ++code)
            if (code.value() > most) { most = code.value(); best = code.key(); }
        countryCodes_.insert(country.key(), best);
    }

    // Start on the viewer's own country, when iptv-org has channels from it.
    if (!homeCountry_.isEmpty()) {
        for (const Entry& entry : entries_) {
            if (entry.facets[0].contains(homeCountry_)) { chosen_[0] = QStringList{homeCountry_}; break; }
        }
    }
    setMessage(!loadNote_.isEmpty() ? loadNote_
               : entries_.empty() ? tr("No channels  -  iptv-org's list could not be read") : QString());
    applyFilters();
    wantGuides();
}

void TvController::toggleFilter(int facet, const QString& value) {
    if (facet < 0 || facet > 2) return;
    if (value.isEmpty()) {
        if (chosen_[facet].isEmpty()) return;
        chosen_[facet].clear();
    } else if (chosen_[facet].contains(value)) {
        chosen_[facet].removeAll(value);
    } else {
        chosen_[facet].append(value);
    }
    applyFilters();
    if (facet == 0) wantGuides();
}

void TvController::setSearch(const QString& text) {
    if (search_ == text) return;
    search_ = text;
    applyFilters();
}

void TvController::setFavoritesOnly(bool on) {
    if (favoritesOnly_ == on) return;
    favoritesOnly_ = on;
    applyFilters();
}

void TvController::applyFilters() {
    QSet<QString> favoriteUrls;
    for (const QVariant& entry : favorites()) favoriteUrls.insert(entry.toString());

    const QString needle = search_.trimmed().toLower();
    // For each filter, how many channels each of its values would leave, given
    // everything else chosen — so a list says "Hindi  124" for India's news,
    // not for the whole world.
    QHash<QString, int> counts[3];
    int totals[3] = {0, 0, 0};
    QVariantList shown;

    for (const Entry& entry : entries_) {
        if (!needle.isEmpty() && !entry.lowerName.contains(needle)) continue;
        const bool favorite = favoriteUrls.contains(entry.url);
        if (favoritesOnly_ && !favorite) continue;

        bool matches[3];
        for (int f = 0; f < 3; ++f) {
            matches[f] = chosen_[f].isEmpty();
            for (const QString& value : chosen_[f]) {
                if (entry.facets[f].contains(value)) { matches[f] = true; break; }
            }
        }

        for (int f = 0; f < 3; ++f) {
            // Counted for filter f if it passes the other two.
            if (!matches[(f + 1) % 3] || !matches[(f + 2) % 3]) continue;
            ++totals[f];
            for (const QString& value : entry.facets[f]) ++counts[f][value];
        }
        if (matches[0] && matches[1] && matches[2]) {
            QVariantMap map = entry.map;
            map.insert(QStringLiteral("favorite"), favorite);
            shown.append(map);
        }
    }

    static const char* const kAll[3] = {QT_TR_NOOP("All countries"), QT_TR_NOOP("All categories"),
                                        QT_TR_NOOP("All languages")};
    for (int f = 0; f < 3; ++f) {
        QVariantList options;
        options.append(QVariantMap{{QStringLiteral("id"), QString()},
                                   {QStringLiteral("name"), tr(kAll[f])},
                                   {QStringLiteral("detail"), QString::number(totals[f])},
                                   {QStringLiteral("checked"), chosen_[f].isEmpty()}});
        QStringList values = counts[f].keys();
        // A checked one stays listed even at none, so it can be seen and unchecked.
        for (const QString& value : chosen_[f])
            if (!values.contains(value)) values << value;
        std::sort(values.begin(), values.end(),
                  [](const QString& a, const QString& b) { return a.localeAwareCompare(b) < 0; });
        for (const QString& value : values) {
            options.append(QVariantMap{{QStringLiteral("id"), value},
                                       {QStringLiteral("name"), value},
                                       {QStringLiteral("detail"), QString::number(counts[f].value(value))},
                                       {QStringLiteral("checked"), chosen_[f].contains(value)}});
        }
        options_[f] = options;
    }

    channels_ = shown;
    emit filtersChanged();
    emit channelsChanged();
}

void TvController::play(int index) {
    zapDirection_ = 0;
    skipped_ = 0;
    startPlaying(index);
}

void TvController::changeChannel(int direction) {
    // Channel up and down, round the list, as on any TV. Pressed, not
    // skipped to, so the count of dead ones skipped starts again.
    skipped_ = 0;
    zap(direction);
}

void TvController::zap(int direction) {
    const int count = static_cast<int>(channels_.size());
    if (count < 2) return;
    const int next = (playingIndex_ + direction + count) % count;
    zapDirection_ = direction;
    startPlaying(next);
    emit playingIndexChanged(next);
}

void TvController::startPlaying(int index) {
    if (index < 0 || index >= channels_.size()) return;
    const QVariantMap channel = channels_.at(index).toMap();
    const QString url = channel.value(QStringLiteral("url")).toString();
    if (!url.startsWith(QLatin1String("http://")) && !url.startsWith(QLatin1String("https://"))) return;

    // One channel at a time: the old one goes first, quietly.
    if (player_ != nullptr) {
        QProcess* old = player_;
        player_ = nullptr;
        old->disconnect(this);
        old->terminate();
        if (!old->waitForFinished(1500)) old->kill();
        old->deleteLater();
    }

    playingIndex_ = index;
    playingName_ = channel.value(QStringLiteral("name")).toString();
    playError_.clear();
    setMessage(QString());
    wantGuides();  // found by a search, it may be from anywhere

    const QString agent = channel.value(QStringLiteral("userAgent")).toString();
    const QString referrer = channel.value(QStringLiteral("referrer")).toString();
    // Most channels play in the window, under OmniOS's own controls. Only one
    // that has to be asked for as a particular browser or from a particular
    // page goes to mpv, which can say so; Qt's player cannot.
    if (agent.isEmpty() && referrer.isEmpty()) {
        inWindow_ = true;
        playingUrl_.clear();
        const int token = ++tuning_;
        emit stateChanged();
        // An HLS playlist may be a choice of qualities; the player is given
        // one (see omnios::pickHlsVariant). Anything else, as it is.
        if (!QUrl(url).path().endsWith(QLatin1String(".m3u8"), Qt::CaseInsensitive)) {
            playingUrl_ = url;
            emit stateChanged();
            return;
        }
        QNetworkRequest request{QUrl(url)};
        request.setTransferTimeout(10000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply* reply = network_.get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, url, token]() {
            reply->deleteLater();
            // Another channel since, or back to the list: not ours any more.
            if (token != tuning_ || !inWindow_) return;
            QString chosen = url;
            if (reply->error() == QNetworkReply::NoError) {
                // Up to the screen's height, and never below 1080p: a 4K copy
                // would only be scaled down on a 1080p screen, but a smaller
                // screen still gets 1080p, because the best copy is often the
                // only one with the extra languages (Sony Yay's Hindi, Telugu
                // and the rest are in its 1080p stream alone).
                const QScreen* screen = QGuiApplication::primaryScreen();
                const int height = screen
                    ? int(screen->size().height() * screen->devicePixelRatio()) : 1080;
                const std::string variant =
                    omnios::pickHlsVariant(reply->readAll().toStdString(), std::max(1080, height));
                // Relative to where the playlist came from, after redirects.
                if (!variant.empty())
                    chosen = reply->url().resolved(QUrl(QString::fromStdString(variant))).toString();
            }
            // Could not read it: the player may still manage, and says so if not.
            playingUrl_ = chosen;
            emit stateChanged();
        });
        return;
    }
    inWindow_ = false;
    playingUrl_.clear();

    QStringList args{
        // The GPU where there is one, through Wayland only; and Wayland's plain
        // software output where there is not. Left to itself, mpv on a machine
        // without a usable GPU (a VM, a broken driver) goes on to try X11
        // through XWayland and crashes on an assertion there — seen as every
        // channel being "not available".
        QStringLiteral("--vo=gpu-next,wlshm"),
        QStringLiteral("--gpu-context=waylandvk,wayland"),
        // Hardware decoding where it is known to be safe: a console decoding
        // 1080p on the CPU is a warm console.
        QStringLiteral("--hwdec=auto-safe"),
        QStringLiteral("--fs"),
        // A window at once, while the stream connects, rather than nothing.
        QStringLiteral("--force-window=immediate"),
        QStringLiteral("--no-terminal"),
        QStringLiteral("--keep-open=no"),
        QStringLiteral("--network-timeout=20"),
        QStringLiteral("--input-conf=") + cacheDir() + QStringLiteral("/input.conf"),
        QStringLiteral("--force-media-title=") + playingName_,
        // Said on screen, since this window has no Back button of its own.
        QStringLiteral("--osd-playing-msg=${media-title}\nEsc: back to channels"),
        QStringLiteral("--osd-duration=4000"),
    };
    if (!agent.isEmpty()) args << QStringLiteral("--user-agent=") + agent;
    if (!referrer.isEmpty()) args << QStringLiteral("--referrer=") + referrer;
    // "--" first: a URL is never read as an option, whatever it starts with.
    args << QStringLiteral("--") << url;

    auto* process = new QProcess(this);
    player_ = process;
    playingFor_.start();
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus) {
        process->deleteLater();
        if (process != player_) return;
        player_ = nullptr;
        playerClosed_.start();
        const QString name = playingName_;
        playingName_.clear();
        // mpv exits 2 when it could not play what it was given. Many streams
        // are offline on any given day, or only answer inside their country.
        const bool failed = !stopping_ && code != 0;
        stopping_ = false;
        if (failed && !channelFailed(name)) return;
        QProcess::startDetached(QStringLiteral("omni-kwin-activate"), {QStringLiteral("omnios-tv")});
        emit stateChanged();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || process != player_) return;
        player_ = nullptr;
        playingName_.clear();
        process->deleteLater();
        setMessage(tr("Could not start the video player (mpv)"));
        emit stateChanged();
    });
    process->start(QStringLiteral("mpv"), args);
    emit stateChanged();
}

void TvController::stop() {
    if (inWindow_) {
        inWindow_ = false;
        ++tuning_;  // a playlist still being read is no longer wanted
        playingUrl_.clear();
        playingName_.clear();
        playError_.clear();
        emit stateChanged();
        return;
    }
    if (player_ == nullptr) return;
    stopping_ = true;
    player_->terminate();
}

bool TvController::channelFailed(const QString& name) {
    // Surfing with the remote, a dead channel is skipped, as a TV skips an
    // empty one — a few in a row at most, so a network that is down does not
    // send it round the whole list.
    if (zapDirection_ != 0 && skipped_ < 5 && channels_.size() > 1) {
        ++skipped_;
        setMessage(tr("Skipped %1  -  not available").arg(name));
        zap(zapDirection_);
        return false;
    }
    setMessage(tr("%1 is not available right now  -  it may be offline, or only watchable in its own country")
                   .arg(name));
    return true;
}

void TvController::playerFailed() {
    if (!inWindow_) return;
    // Picked, not surfed to: the player stays up and says so, with the next
    // channel a press away, rather than dropping back to the list.
    if (channelFailed(playingName_)) {
        playError_ = message_;
        emit stateChanged();
    }
}

void TvController::remote(int key) {
    const int count = static_cast<int>(channels_.size());
    if (key == Qt::Key_Escape) {
        stop();
    } else if ((key == Qt::Key_Up || key == Qt::Key_Down) && count > 1) {
        changeChannel(key == Qt::Key_Down ? 1 : -1);
    } else if ((key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Tab || key == Qt::Key_Backtab)
               && count > 1) {
        // As in TV's own player: left and right, and the shoulders.
        changeChannel(key == Qt::Key_Right || key == Qt::Key_Tab ? 1 : -1);
    } else if (key == Qt::Key_F5) {
        toggleFavorite(playingIndex_);
    }
}

// ---- what is on ---------------------------------------------------------------------

void TvController::wantGuides() {
    if (entries_.empty()) return;
    // Half a day on, the programmes read then are running out: read again
    // (from the same file, if it is less than a day old; it runs days ahead).
    if (guideAge_.isValid() && guideAge_.elapsed() > kGuideRefreshMs) {
        guideCountries_.clear();
        epgIndexState_ = 0;
    }

    QStringList countries;
    for (const QString& name : chosen_[0].isEmpty() ? QStringList{homeCountry_} : chosen_[0])
        countries << countryCodes_.value(name);
    if (playingIndex_ >= 0 && playingIndex_ < channels_.size()) {
        const int at = byUrl_.value(channels_.at(playingIndex_).toMap().value(QStringLiteral("url")).toString(), -1);
        if (at >= 0) countries << entries_[static_cast<std::size_t>(at)].country;
    }
    for (const QString& country : countries) {
        if (country.isEmpty() || guideCountries_.contains(country)) continue;
        if (guideCountries_.isEmpty()) guideAge_.start();
        guideCountries_.insert(country);
        loadGuide(country);
    }
}

void TvController::loadGuide(const QString& country) {
    // Which files a country has is on epgshare01's index page: India has
    // three, most have one, many none.
    if (epgIndexState_ != 2) {
        waitingForIndex_ << country;
        if (epgIndexState_ == 1) return;
        epgIndexState_ = 1;
        fetch(QString::fromLatin1(omnios::kEpgIndexUrl.data(), static_cast<qsizetype>(omnios::kEpgIndexUrl.size())),
              QStringLiteral("epg-index.html"),
              [this](const QByteArray& data, const QString&) {
                  epgIndex_ = data;
                  epgIndexState_ = 2;
                  for (const QString& waiting : std::exchange(waitingForIndex_, {})) loadGuide(waiting);
              },
              false);
        return;
    }
    const std::string_view index(epgIndex_.constData(), static_cast<std::size_t>(epgIndex_.size()));
    for (const std::string& file : omnios::epgFilesFor(index, country.toStdString())) {
        const QString url = QString::fromStdString(file);
        fetch(url, QStringLiteral("epg-") + url.section(QLatin1Char('/'), -1),
              [this, country](const QByteArray& data, const QString&) {
                  if (!data.isEmpty()) readGuide(country, data);
              },
              false);
    }
}

void TvController::readGuide(const QString& country, const QByteArray& gzipped) {
    // Only the programmes of this country's channels are kept: a guide lists
    // many more than iptv-org has streams for.
    std::unordered_set<std::string> wanted;
    for (const Entry& entry : entries_)
        if (entry.country == country && !entry.guideKey.empty()) wanted.insert(entry.guideKey);
    if (wanted.empty()) return;

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    pool_.start([this, country, gzipped, wanted = std::move(wanted), now]() {
        const std::string xml = gunzip(gzipped);
        auto schedules = std::make_shared<std::unordered_map<std::string, omnios::Schedule>>(
            omnios::parseXmltv(xml, wanted, now - kGuideBack, now + kGuideAhead));
        QMetaObject::invokeMethod(this, [this, country, schedules]() {
            const std::string prefix = country.toStdString() + '\n';
            bool changed = false;
            for (auto& [key, schedule] : *schedules) {
                if (schedule.empty()) continue;
                omnios::Schedule& have = guide_[prefix + key];
                // Two of a country's files can both list a channel: the one
                // reaching further ahead is kept, then the fuller.
                if (!have.empty() && (have.back().stop > schedule.back().stop
                                      || (have.back().stop == schedule.back().stop && have.size() >= schedule.size())))
                    continue;
                have = std::move(schedule);
                changed = true;
            }
            if (!changed) return;
            ++guideRevision_;
            emit guideChanged();
        }, Qt::QueuedConnection);
    });
}

QVariantMap TvController::guideFor(const QString& url) const {
    if (guide_.empty()) return {};
    const int at = byUrl_.value(url, -1);
    if (at < 0) return {};
    const Entry& entry = entries_[static_cast<std::size_t>(at)];
    if (entry.guideKey.empty()) return {};
    const auto found = guide_.find(entry.country.toStdString() + '\n' + entry.guideKey);
    if (found == guide_.end()) return {};

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const omnios::OnAir air = omnios::onAir(found->second, now);
    if (air.now == nullptr && air.next == nullptr) return {};
    const auto clock = [](std::int64_t when) {
        return QLocale().toString(QDateTime::fromSecsSinceEpoch(when).time(), QLocale::ShortFormat);
    };
    QVariantMap map;
    if (air.now != nullptr) {
        map.insert(QStringLiteral("now"), QString::fromStdString(air.now->title));
        map.insert(QStringLiteral("nowDescription"), QString::fromStdString(air.now->description));
        map.insert(QStringLiteral("nowStart"), clock(air.now->start));
        map.insert(QStringLiteral("nowEnd"), clock(air.now->stop));
        map.insert(QStringLiteral("progress"),
                   double(now - air.now->start) / double(air.now->stop - air.now->start));
    }
    if (air.next != nullptr) {
        map.insert(QStringLiteral("next"), QString::fromStdString(air.next->title));
        map.insert(QStringLiteral("nextStart"), clock(air.next->start));
    }
    return map;
}

// Favourites are kept by stream address. An earlier version kept whole
// channels ({ "url": ... }); those still read.
QVariantList TvController::favorites() const {
    QFile file(favoritesFile());
    if (!file.open(QIODevice::ReadOnly)) return {};
    QVariantList urls;
    for (const QJsonValue& value : QJsonDocument::fromJson(file.readAll()).array()) {
        const QString url = value.isString() ? value.toString()
                                             : value.toObject().value(QStringLiteral("url")).toString();
        if (!url.isEmpty()) urls.append(url);
    }
    return urls;
}

void TvController::saveFavorites(const QVariantList& list) const {
    QDir().mkpath(QFileInfo(favoritesFile()).absolutePath());
    QFile file(favoritesFile());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(QJsonArray::fromVariantList(list)).toJson());
}

void TvController::toggleFavorite(int index) {
    if (index < 0 || index >= channels_.size()) return;
    QVariantMap channel = channels_.at(index).toMap();
    const QString url = channel.value(QStringLiteral("url")).toString();
    QVariantList list = favorites();
    const bool nowFavorite = !list.contains(url);
    if (nowFavorite) list.append(url);
    else list.removeAll(url);
    saveFavorites(list);

    const QString name = channel.value(QStringLiteral("name")).toString();
    if (favoritesOnly_ && !nowFavorite) {
        applyFilters();  // it leaves the list it was just taken out of
    } else {
        channel.insert(QStringLiteral("favorite"), nowFavorite);
        channels_[index] = channel;
        emit channelsChanged();
    }
    setMessage(nowFavorite ? tr("%1 added to favourites").arg(name)
                           : tr("%1 removed from favourites").arg(name));
}

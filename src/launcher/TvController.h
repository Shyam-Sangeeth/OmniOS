// OmniOS TV: free-to-air channels from iptv-org.
//
// The window filters — country, category, language, and a search, all at
// once — and plays in the window itself, under OmniOS's own controls (Qt
// Multimedia, over FFmpeg; see TvWindow.qml). A stream that answers only a
// particular browser or referring page cannot be played that way — Qt's player
// has no way to send either — and goes to mpv instead, full screen, in a
// process of its own; the controller is then a remote through this class.
// Either way: up and down change channel, B stops. See omnios/Iptv.h for where
// the channels come from, and omnios/Epg.h for what is on them.
#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>
#include <QVariantList>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "omnios/Epg.h"

#include "GamepadInput.h"
#include "InputMode.h"

class TvController : public QObject {
    Q_OBJECT
    Q_PROPERTY(InputMode* input READ input CONSTANT)

    // Each filter's choices, [{ id: "India", name: "India", detail: "738",
    // checked: true }, ...], "All ..." first (id ""). The counts are of
    // channels matching the other filters too, and a choice with none is left
    // out unless it is checked.
    Q_PROPERTY(QVariantList countryOptions READ countryOptions NOTIFY filtersChanged)
    Q_PROPERTY(QVariantList categoryOptions READ categoryOptions NOTIFY filtersChanged)
    Q_PROPERTY(QVariantList languageOptions READ languageOptions NOTIFY filtersChanged)
    // What is checked in each; empty for all. Within a filter any of them
    // will do (India or Nepal); across filters, all must (and News).
    Q_PROPERTY(QStringList countries READ countries NOTIFY filtersChanged)
    Q_PROPERTY(QStringList categories READ categories NOTIFY filtersChanged)
    Q_PROPERTY(QStringList languages READ languages NOTIFY filtersChanged)
    Q_PROPERTY(QString search READ search NOTIFY filtersChanged)
    Q_PROPERTY(bool favoritesOnly READ favoritesOnly NOTIFY filtersChanged)

    // What the filters leave: [{ name, logo, categories, url, userAgent,
    // referrer, favorite }, ...], by name.
    Q_PROPERTY(QVariantList channels READ channels NOTIFY channelsChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    // What to tell the viewer: a channel that would not play, a list from the
    // cache because the network is down. Empty when there is nothing to say.
    Q_PROPERTY(QString message READ message NOTIFY stateChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged)
    Q_PROPERTY(QString playingName READ playingName NOTIFY stateChanged)
    Q_PROPERTY(int playingIndex READ playingIndex NOTIFY stateChanged)
    // The stream for the window's own player; empty when nothing plays there
    // (nothing at all, or mpv has it).
    Q_PROPERTY(QString playingUrl READ playingUrl NOTIFY stateChanged)
    // The window's player is up: tuning in (the URL may not be known yet,
    // while the playlist is read) or playing.
    Q_PROPERTY(bool playingInWindow READ playingInWindow NOTIFY stateChanged)
    // Why the channel in the window's player will not play; empty while it
    // does, or is still trying.
    Q_PROPERTY(QString playError READ playError NOTIFY stateChanged)
    // Goes up when what guideFor() answers may have changed: a guide came in,
    // or the clock moved on. Bind to it alongside guideFor().
    Q_PROPERTY(int guideRevision READ guideRevision NOTIFY guideChanged)

public:
    explicit TvController(QObject* parent = nullptr);
    ~TvController() override;

    InputMode* input() { return &input_; }
    QVariantList countryOptions() const { return options_[0]; }
    QVariantList categoryOptions() const { return options_[1]; }
    QVariantList languageOptions() const { return options_[2]; }
    QStringList countries() const { return chosen_[0]; }
    QStringList categories() const { return chosen_[1]; }
    QStringList languages() const { return chosen_[2]; }
    QString search() const { return search_; }
    bool favoritesOnly() const { return favoritesOnly_; }
    QVariantList channels() const { return channels_; }
    bool loading() const { return pending_ > 0; }
    QString message() const { return message_; }
    bool playing() const { return player_ != nullptr || inWindow_; }
    bool playingInWindow() const { return inWindow_; }
    QString playingName() const { return playingName_; }
    int playingIndex() const { return playingIndex_; }
    QString playingUrl() const { return playingUrl_; }
    QString playError() const { return playError_; }
    int guideRevision() const { return guideRevision_; }

    // What is on the channel with stream `url`: { now, nowDescription,
    // nowStart, nowEnd, progress (0-1), next, nextStart }, times as the
    // viewer's clock shows them, each empty where the guide has nothing.
    // Empty altogether for a channel no guide covers.
    Q_INVOKABLE QVariantMap guideFor(const QString& url) const;

    // Checks or unchecks one choice in filter `facet` (0 country, 1 category,
    // 2 language); "" — the "All ..." row — unchecks them all.
    Q_INVOKABLE void toggleFilter(int facet, const QString& value);
    Q_INVOKABLE void setSearch(const QString& text);
    Q_INVOKABLE void setFavoritesOnly(bool on);

    // Plays channels[index] full screen, replacing whatever was playing.
    Q_INVOKABLE void play(int index);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void toggleFavorite(int index);
    // The next channel up (-1) or down (+1) from the one playing, as pressed.
    Q_INVOKABLE void changeChannel(int direction);
    // The window's player could not play the stream: offline, blocked
    // outside its country, gone. Surfing, the next one is tried.
    Q_INVOKABLE void playerFailed();

signals:
    void filtersChanged();
    void channelsChanged();
    void stateChanged();
    void guideChanged();
    // The window should make sure something in it has focus before a
    // controller press is delivered, as the launcher does.
    void focusWanted();
    // The index now playing, after a channel change from the remote, so the
    // grid can follow it.
    void playingIndexChanged(int index);

private:
    // One channel, ready to filter and to show.
    struct Entry {
        QVariantMap map;
        QString     url;
        QString     lowerName;
        QStringList facets[3];  // countries, categories, languages
        QString     country;    // its code, from its iptv-org id ("in"); may be empty
        std::string guideKey;   // its name, as guides match it (omnios::guideKey)
    };

    // Gets `url`, keeping a copy at `cacheName` in ~/.cache/omnios/tv. A copy
    // younger than a day is used without asking; an older one is refreshed,
    // and used anyway — saying so — when the network cannot be reached.
    // `counted` shows as loading; a guide, which nothing waits for, does not.
    void fetch(const QString& url, const QString& cacheName,
               std::function<void(const QByteArray&, const QString& note)> done, bool counted = true);
    void loadCatalog();
    void buildEntries();

    // Recomputes channels_ and every filter's choices from entries_.
    void applyFilters();
    void setMessage(const QString& text);
    void remote(int key);
    void startPlaying(int index);
    // To the next channel, pressed or skipped to.
    void zap(int direction);
    // A channel would not play, in either player: skipped when surfing, said
    // otherwise. False when it was skipped.
    bool channelFailed(const QString& name);
    QVariantList favorites() const;
    void saveFavorites(const QVariantList& list) const;

    // Guides: for the countries being looked at (those checked, or the
    // viewer's own) and the channel playing. Each country once, and again
    // once its guide is half a day old.
    void wantGuides();
    void loadGuide(const QString& country);
    // Unpacks and reads one guide file off the UI thread, then merges it.
    void readGuide(const QString& country, const QByteArray& gzipped);

    QNetworkAccessManager network_;
    GamepadInput gamepad_;
    InputMode    input_{&gamepad_};

    QByteArray        playlists_[3];
    int               arrived_ = 0;
    QString           loadNote_;
    std::vector<Entry> entries_;
    QVariantList      options_[3];
    QStringList       chosen_[3];
    QString           search_;
    bool              favoritesOnly_ = false;
    QString           homeCountry_;  // its name, as iptv-org spells it

    QVariantList channels_;
    int          pending_ = 0;
    QString      message_;

    QProcess*     player_ = nullptr;  // mpv, for a stream that needs headers
    QString       playingUrl_;        // the window's player
    bool          inWindow_ = false;
    int           tuning_ = 0;         // which channel a playlist read is for
    QString       playError_;
    int           playingIndex_ = -1;
    QString       playingName_;
    QElapsedTimer playingFor_;
    QElapsedTimer playerClosed_;  // since mpv last closed
    bool          stopping_ = false;
    // Channel surfing: the direction of the last up or down on the remote, and
    // how many dead channels in a row have been skipped since. A channel
    // chosen from the grid resets both, so it reports failing rather than
    // being skipped.
    int           zapDirection_ = 0;
    int           skipped_ = 0;

    // Into entries_, by stream URL.
    QHash<QString, int> byUrl_;
    // Each country's code, by its name: "India" -> "in".
    QHash<QString, QString> countryCodes_;
    // Programmes, by country code, a newline, and guide key.
    std::unordered_map<std::string, omnios::Schedule> guide_;
    int           guideRevision_ = 0;
    QSet<QString> guideCountries_;     // asked for already
    QByteArray    epgIndex_;           // epgshare01's list of files
    int           epgIndexState_ = 0;  // 0 not asked, 1 asking, 2 here
    QStringList   waitingForIndex_;
    QElapsedTimer guideAge_;
    QTimer        clock_;
    // Last, so it goes first: it waits for a guide still being read, whose
    // result is then posted to a controller that is still whole.
    QThreadPool   pool_;
};

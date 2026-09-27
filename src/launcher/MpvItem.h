// A video in the QML scene, played by libmpv.
//
// TV plays every channel through this, inside its own window, under its own
// controls. mpv, not Qt Multimedia, because mpv plays what TV has to play:
// streams that answer only a particular browser or referring page (Qt's player
// cannot send either), and streams whose segment addresses carry no file
// extension, which the FFmpeg inside Qt's player refuses outright (amagi's
// channels, Samsung TV Plus among them). One player, so every channel has the
// same bar, audio and subtitle lists, and guide.
//
// mpv draws into an OpenGL framebuffer the scene graph then shows (its render
// API), so the window must use OpenGL: main() sets that for --tv.
#pragma once

#include <QQuickFramebufferObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

struct mpv_handle;
struct mpv_render_context;

class MpvItem : public QQuickFramebufferObject {
    Q_OBJECT
    QML_ELEMENT

    // What to play; empty stops. Loaded once the current round of bindings
    // is done, so userAgent and referrer set alongside it apply to it.
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QString userAgent MEMBER userAgent_ NOTIFY headersChanged)
    Q_PROPERTY(QString referrer MEMBER referrer_ NOTIFY headersChanged)

    // "idle", "loading", "playing" or "failed". A live stream that ends has
    // stopped sending, which is "failed" too. (Not "state": every Item has one
    // of those already, for QML's States.)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    // Waiting for the network mid-stream, the picture frozen.
    Q_PROPERTY(bool buffering READ buffering NOTIFY statusChanged)

    // 0 to 100.
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY volumeChanged)

    // [{ id, title, language }, ...], mpv's ids, which start at 1.
    Q_PROPERTY(QVariantList audioTracks READ audioTracks NOTIFY tracksChanged)
    Q_PROPERTY(QVariantList subtitleTracks READ subtitleTracks NOTIFY tracksChanged)
    // The id playing; 0 for none (subtitles off).
    Q_PROPERTY(int audioTrack READ audioTrack WRITE setAudioTrack NOTIFY tracksChanged)
    Q_PROPERTY(int subtitleTrack READ subtitleTrack WRITE setSubtitleTrack NOTIFY tracksChanged)

public:
    explicit MpvItem(QQuickItem* parent = nullptr);
    ~MpvItem() override;

    Renderer* createRenderer() const override;

    QString source() const { return source_; }
    void setSource(const QString& source);
    QString status() const { return status_; }
    bool buffering() const { return buffering_; }
    double volume() const { return volume_; }
    void setVolume(double volume);
    bool muted() const { return muted_; }
    void setMuted(bool muted);
    QVariantList audioTracks() const { return audioTracks_; }
    QVariantList subtitleTracks() const { return subtitleTracks_; }
    int audioTrack() const { return audioTrack_; }
    void setAudioTrack(int id);
    int subtitleTrack() const { return subtitleTrack_; }
    void setSubtitleTrack(int id);

    // The same source again, from the start: a reconnect after a hiccup.
    Q_INVOKABLE void reload();
    // Stops, leaving the source as it is (reload() starts it again).
    Q_INVOKABLE void stop();

    mpv_handle* handle() const { return mpv_; }
    // Set by the renderer, which owns it, on the render thread.
    mpv_render_context* renderContext = nullptr;

signals:
    void sourceChanged();
    void headersChanged();
    void statusChanged();
    void volumeChanged();
    void tracksChanged();
    // From mpv's thread: a new frame is ready.
    void frameReady();

private:
    void load();
    void readEvents();
    void setStatus(const QString& status);

    mpv_handle* mpv_ = nullptr;
    QString source_;
    QString userAgent_;
    QString referrer_;
    bool loadPending_ = false;
    QString status_ = QStringLiteral("idle");
    bool buffering_ = false;
    double volume_ = 80;
    bool muted_ = false;
    QVariantList audioTracks_;
    QVariantList subtitleTracks_;
    int audioTrack_ = 0;
    int subtitleTrack_ = 0;
};

#include "MpvItem.h"

#include <QMetaObject>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QtQuick/qquickopenglutils.h>

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <clocale>

namespace {

void* procAddress(void*, const char* name) {
    QOpenGLContext* context = QOpenGLContext::currentContext();
    return context ? reinterpret_cast<void*>(context->getProcAddress(QByteArray(name))) : nullptr;
}

// Draws the item: mpv renders the current frame into the item's framebuffer,
// on the scene graph's thread, with its OpenGL context current.
class MpvRenderer : public QQuickFramebufferObject::Renderer {
public:
    explicit MpvRenderer(MpvItem* item) : item_(item) {}

    QOpenGLFramebufferObject* createFramebufferObject(const QSize& size) override {
        // The render context needs a current OpenGL context, which exists
        // only here, so it is made here, the first time.
        if (item_->renderContext == nullptr) {
            mpv_opengl_init_params gl{procAddress, nullptr};
            mpv_render_param params[] = {
                {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
                {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl},
                {MPV_RENDER_PARAM_INVALID, nullptr}};
            if (mpv_render_context_create(&item_->renderContext, item_->handle(), params) >= 0) {
                mpv_render_context_set_update_callback(
                    item_->renderContext,
                    [](void* item) { emit static_cast<MpvItem*>(item)->frameReady(); }, item_);
            }
        }
        return new QOpenGLFramebufferObject(size);
    }

    void render() override {
        if (item_->renderContext == nullptr) return;
        QOpenGLFramebufferObject* fbo = framebufferObject();
        mpv_opengl_fbo target{static_cast<int>(fbo->handle()), fbo->width(), fbo->height(), 0};
        int flip = 0;
        mpv_render_param params[] = {{MPV_RENDER_PARAM_OPENGL_FBO, &target},
                                     {MPV_RENDER_PARAM_FLIP_Y, &flip},
                                     {MPV_RENDER_PARAM_INVALID, nullptr}};
        mpv_render_context_render(item_->renderContext, params);
        // mpv leaves OpenGL as it likes it; the scene graph expects its own.
        QQuickOpenGLUtils::resetOpenGLState();
    }

private:
    MpvItem* item_;
};

}  // namespace

MpvItem::MpvItem(QQuickItem* parent) : QQuickFramebufferObject(parent) {
    // mpv reads and writes numbers the C way, and refuses to start otherwise.
    std::setlocale(LC_NUMERIC, "C");
    mpv_ = mpv_create();
    if (mpv_ == nullptr) return;

    const auto option = [this](const char* name, const char* value) { mpv_set_option_string(mpv_, name, value); };
    option("vo", "libmpv");
    // Hardware decoding where it is known to be safe: a console decoding
    // 1080p on the CPU is a warm console.
    option("hwdec", "auto-safe");
    // Stays alive between channels, with nothing loaded.
    option("idle", "yes");
    option("keep-open", "no");
    option("network-timeout", "20");
    option("ytdl", "no");
    option("terminal", "no");
    option("msg-level", "all=warn");
    // TV's own keys and controls, not mpv's.
    option("input-default-bindings", "no");
    option("input-vo-keyboard", "no");
    option("osc", "no");
    option("load-scripts", "no");
    option("audio-client-name", "omnios-tv");
    option("volume", "80");
    if (mpv_initialize(mpv_) < 0) {
        mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
        return;
    }

    // Its warnings, into TV's own log (/tmp/omnios-app.log from the launcher):
    // why a channel would not play is there, and nowhere else.
    mpv_request_log_messages(mpv_, qEnvironmentVariableIsSet("OMNIOS_MPV_DEBUG") ? "v" : "warn");

    mpv_observe_property(mpv_, 0, "track-list", MPV_FORMAT_NODE);
    mpv_observe_property(mpv_, 0, "volume", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, 0, "mute", MPV_FORMAT_FLAG);
    mpv_observe_property(mpv_, 0, "aid", MPV_FORMAT_STRING);
    mpv_observe_property(mpv_, 0, "sid", MPV_FORMAT_STRING);
    mpv_observe_property(mpv_, 0, "paused-for-cache", MPV_FORMAT_FLAG);

    // mpv calls these from its own threads; the work is done on this one.
    mpv_set_wakeup_callback(
        mpv_,
        [](void* item) {
            auto* self = static_cast<MpvItem*>(item);
            QMetaObject::invokeMethod(self, [self]() { self->readEvents(); }, Qt::QueuedConnection);
        },
        this);
    connect(this, &MpvItem::frameReady, this, &QQuickItem::update, Qt::QueuedConnection);
}

MpvItem::~MpvItem() {
    if (mpv_ == nullptr) return;
    mpv_set_wakeup_callback(mpv_, nullptr, nullptr);
    // The render context goes before the player it draws for.
    if (renderContext != nullptr) mpv_render_context_free(renderContext);
    mpv_terminate_destroy(mpv_);
}

QQuickFramebufferObject::Renderer* MpvItem::createRenderer() const {
    return new MpvRenderer(const_cast<MpvItem*>(this));
}

void MpvItem::setStatus(const QString& status) {
    if (status_ == status) return;
    status_ = status;
    emit statusChanged();
}

void MpvItem::setSource(const QString& source) {
    if (source_ == source) return;
    source_ = source;
    emit sourceChanged();
    // After the bindings being evaluated now, the user agent and referrer
    // among them.
    if (loadPending_) return;
    loadPending_ = true;
    QMetaObject::invokeMethod(this, [this]() {
        loadPending_ = false;
        load();
    }, Qt::QueuedConnection);
}

void MpvItem::reload() { load(); }

void MpvItem::stop() {
    if (mpv_ == nullptr) return;
    const char* stop[] = {"stop", nullptr};
    mpv_command_async(mpv_, 0, stop);
    setStatus(QStringLiteral("idle"));
}

void MpvItem::load() {
    if (mpv_ == nullptr) {
        setStatus(source_.isEmpty() ? QStringLiteral("idle") : QStringLiteral("failed"));
        return;
    }
    audioTracks_.clear();
    subtitleTracks_.clear();
    emit tracksChanged();
    buffering_ = false;

    if (source_.isEmpty()) {
        const char* stop[] = {"stop", nullptr};
        mpv_command_async(mpv_, 0, stop);
        setStatus(QStringLiteral("idle"));
        return;
    }
    // mpv's own user agent unless the channel asks for another.
    mpv_set_property_string(mpv_, "user-agent",
                            userAgent_.isEmpty() ? "libmpv" : userAgent_.toUtf8().constData());
    mpv_set_property_string(mpv_, "referrer", referrer_.toUtf8().constData());
    const QByteArray url = source_.toUtf8();
    const char* loadfile[] = {"loadfile", url.constData(), nullptr};
    mpv_command_async(mpv_, 0, loadfile);
    setStatus(QStringLiteral("loading"));
}

void MpvItem::readEvents() {
    while (mpv_ != nullptr) {
        mpv_event* event = mpv_wait_event(mpv_, 0);
        if (event->event_id == MPV_EVENT_NONE) break;
        switch (event->event_id) {
            case MPV_EVENT_LOG_MESSAGE: {
                const auto* message = static_cast<mpv_event_log_message*>(event->data);
                qWarning("mpv [%s] %s", message->prefix, QByteArray(message->text).trimmed().constData());
                break;
            }
            case MPV_EVENT_START_FILE:
                setStatus(QStringLiteral("loading"));
                break;
            case MPV_EVENT_PLAYBACK_RESTART:
                setStatus(QStringLiteral("playing"));
                break;
            case MPV_EVENT_END_FILE: {
                // An error, or the end of a live stream, which has stopped
                // sending. A stop or a new file is ours.
                const auto* end = static_cast<mpv_event_end_file*>(event->data);
                if (end->reason == MPV_END_FILE_REASON_ERROR || end->reason == MPV_END_FILE_REASON_EOF)
                    setStatus(QStringLiteral("failed"));
                break;
            }
            case MPV_EVENT_PROPERTY_CHANGE: {
                const auto* property = static_cast<mpv_event_property*>(event->data);
                const QByteArray name(property->name);
                if (property->format == MPV_FORMAT_NONE || property->data == nullptr) {
                    if (name == "track-list") {
                        audioTracks_.clear();
                        subtitleTracks_.clear();
                        emit tracksChanged();
                    }
                    break;
                }
                if (name == "volume") {
                    volume_ = *static_cast<double*>(property->data);
                    emit volumeChanged();
                } else if (name == "mute") {
                    muted_ = *static_cast<int*>(property->data) != 0;
                    emit volumeChanged();
                } else if (name == "paused-for-cache") {
                    buffering_ = *static_cast<int*>(property->data) != 0;
                    emit statusChanged();
                } else if (name == "aid" || name == "sid") {
                    const QByteArray value(*static_cast<char**>(property->data));
                    bool ok = false;
                    const int id = value.toInt(&ok);
                    (name == "aid" ? audioTrack_ : subtitleTrack_) = ok ? id : 0;
                    emit tracksChanged();
                } else if (name == "track-list") {
                    audioTracks_.clear();
                    subtitleTracks_.clear();
                    const auto* list = static_cast<mpv_node*>(property->data);
                    if (list->format == MPV_FORMAT_NODE_ARRAY) {
                        for (int i = 0; i < list->u.list->num; ++i) {
                            const mpv_node& track = list->u.list->values[i];
                            if (track.format != MPV_FORMAT_NODE_MAP) continue;
                            QVariantMap entry;
                            QByteArray type;
                            for (int k = 0; k < track.u.list->num; ++k) {
                                const QByteArray key(track.u.list->keys[k]);
                                const mpv_node& value = track.u.list->values[k];
                                if (key == "id" && value.format == MPV_FORMAT_INT64)
                                    entry.insert(QStringLiteral("id"), static_cast<int>(value.u.int64));
                                else if (key == "type" && value.format == MPV_FORMAT_STRING)
                                    type = value.u.string;
                                else if (key == "title" && value.format == MPV_FORMAT_STRING)
                                    entry.insert(QStringLiteral("title"), QString::fromUtf8(value.u.string));
                                else if (key == "lang" && value.format == MPV_FORMAT_STRING)
                                    entry.insert(QStringLiteral("language"), QString::fromUtf8(value.u.string));
                            }
                            if (type == "audio") audioTracks_.append(entry);
                            else if (type == "sub") subtitleTracks_.append(entry);
                        }
                    }
                    emit tracksChanged();
                }
                break;
            }
            default:
                break;
        }
    }
}

void MpvItem::setVolume(double volume) {
    volume_ = qBound(0.0, volume, 100.0);
    if (mpv_ != nullptr) mpv_set_property_async(mpv_, 0, "volume", MPV_FORMAT_DOUBLE, &volume_);
    emit volumeChanged();
}

void MpvItem::setMuted(bool muted) {
    muted_ = muted;
    int flag = muted ? 1 : 0;
    if (mpv_ != nullptr) mpv_set_property_async(mpv_, 0, "mute", MPV_FORMAT_FLAG, &flag);
    emit volumeChanged();
}

void MpvItem::setAudioTrack(int id) {
    if (mpv_ == nullptr) return;
    const QByteArray value = id > 0 ? QByteArray::number(id) : QByteArray("no");
    mpv_set_property_string(mpv_, "aid", value.constData());
}

void MpvItem::setSubtitleTrack(int id) {
    if (mpv_ == nullptr) return;
    const QByteArray value = id > 0 ? QByteArray::number(id) : QByteArray("no");
    mpv_set_property_string(mpv_, "sid", value.constData());
}

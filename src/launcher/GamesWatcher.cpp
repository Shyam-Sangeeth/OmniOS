#include "GamesWatcher.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSocketNotifier>

#include <cerrno>
#include <cstring>

#include <sys/inotify.h>
#include <unistd.h>

namespace {

// Quiet this long after the last change before the library is rescanned.
constexpr int kQuietMs = 2000;
// A file created and never closed stops holding the rescan back after this.
constexpr qint64 kWritingGiveUpMs = 30 * 60 * 1000;

// IN_MODIFY too: writing to a file keeps the quiet from starting. A file
// created in a folder before the folder had a watch (cp -r makes the folder
// and starts on the first file at once) was never seen created, so only its
// writes say it is still coming.
constexpr uint32_t kMask = IN_CREATE | IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_TO | IN_MOVED_FROM | IN_DELETE;

}  // namespace

GamesWatcher::GamesWatcher(QObject* parent) : QObject(parent) {
    quiet_.setSingleShot(true);
    connect(&quiet_, &QTimer::timeout, this, &GamesWatcher::settle);
}

GamesWatcher::~GamesWatcher() {
    if (fd_ >= 0) ::close(fd_);
}

bool GamesWatcher::start(const QString& root, const QStringList& skip) {
    if (fd_ >= 0) return true;
    fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd_ < 0) {
        qWarning("omni-launcher: cannot watch the games folder: %s", std::strerror(errno));
        return false;
    }
    root_ = QFileInfo(root).absoluteFilePath();
    skip_ = skip;
    watchTree(root_);
    notifier_ = new QSocketNotifier(fd_, QSocketNotifier::Read, this);
    connect(notifier_, &QSocketNotifier::activated, this, &GamesWatcher::readEvents);
    return true;
}

void GamesWatcher::watchTree(const QString& dir) {
    const auto watch = [this](const QString& path) {
        const int wd = inotify_add_watch(fd_, QFile::encodeName(path).constData(), kMask);
        if (wd >= 0) {
            dirs_.insert(wd, path);
        } else if (errno == ENOSPC) {
            // The per-user watch limit. What is watched still works; the
            // rest changes on a rescan.
            qWarning("omni-launcher: out of inotify watches at %s", qPrintable(path));
        }
    };
    if (skipped(dir)) return;
    watch(dir);
    QDirIterator it(dir, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        // The iterator walks into a skipped folder too.
        if (!skipped(path)) watch(path);
    }
}

bool GamesWatcher::skipped(const QString& path) const {
    for (const QString& name : skip_) {
        const QString top = root_ + QLatin1Char('/') + name;
        if (path == top || path.startsWith(top + QLatin1Char('/'))) return true;
    }
    return false;
}

void GamesWatcher::forgetTree(const QString& dir) {
    // A folder moved out of ~/Games keeps its watches — they follow the
    // folder, not the name — and would go on reporting from wherever it went.
    for (auto it = dirs_.begin(); it != dirs_.end();) {
        if (it.value() == dir || it.value().startsWith(dir + QLatin1Char('/'))) {
            inotify_rm_watch(fd_, it.key());
            it = dirs_.erase(it);
        } else {
            ++it;
        }
    }
    const QString prefix = dir + QLatin1Char('/');
    for (auto it = writing_.begin(); it != writing_.end();) {
        if (it->startsWith(prefix)) it = writing_.erase(it);
        else ++it;
    }
}

void GamesWatcher::readEvents() {
    alignas(inotify_event) char buffer[64 * 1024];
    for (;;) {
        const ssize_t length = ::read(fd_, buffer, sizeof buffer);
        if (length <= 0) break;
        for (ssize_t offset = 0; offset < length;) {
            const auto* event = reinterpret_cast<const inotify_event*>(buffer + offset);
            offset += static_cast<ssize_t>(sizeof(inotify_event) + event->len);

            if (event->mask & IN_Q_OVERFLOW) {
                // Events were lost: what is being written is not known any
                // more, so nothing holds the rescan back.
                writing_.clear();
                changed_ = true;
                lastEvent_.start();
                continue;
            }
            if (event->mask & IN_IGNORED) {
                dirs_.remove(event->wd);
                continue;
            }
            const auto dir = dirs_.constFind(event->wd);
            if (dir == dirs_.constEnd() || event->len == 0) continue;
            const QString path = *dir + QLatin1Char('/') + QFile::decodeName(event->name);
            if (skipped(path)) continue;

            const bool isDir = event->mask & IN_ISDIR;
            if (event->mask & (IN_CREATE | IN_MOVED_TO)) {
                if (isDir) watchTree(path);
                else if (event->mask & IN_CREATE) writing_.insert(path);
            }
            if (event->mask & IN_CLOSE_WRITE) writing_.remove(path);
            if (event->mask & (IN_MOVED_FROM | IN_DELETE)) {
                writing_.remove(path);
                if (isDir) forgetTree(path);
            }
            changed_ = true;
            lastEvent_.start();
        }
    }
    if (changed_) quiet_.start(kQuietMs);
}

void GamesWatcher::settle() {
    if (!changed_) return;
    if (!writing_.isEmpty()) {
        const qint64 left = kWritingGiveUpMs - lastEvent_.elapsed();
        if (left > 0) {
            // Its close is an event, and starts the quiet over.
            quiet_.start(static_cast<int>(left));
            return;
        }
        qInfo("omni-launcher: %lld file(s) in ~/Games still open after half an hour; rescanning anyway",
              static_cast<long long>(writing_.size()));
        writing_.clear();
    }
    changed_ = false;
    emit settled();
}

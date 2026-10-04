#include "GameListModel.h"

#include <QDateTime>
#include <QLocale>
#include <QMap>
#include <QUrl>

#include "omnios/Router.h"

namespace {

QString badgeColorFor(omnios::Platform platform) {
    // Straight from the UI spec (OmniOS.md §12) via the platform registry, so
    // the badge colours cannot drift from the rest of the system.
    return QString::fromUtf8(omnios::platformInfo(platform).badgeColor.data(),
                             static_cast<int>(omnios::platformInfo(platform).badgeColor.size()));
}

// How long ago, the way a person would say it: recent days by name, older ones
// by date.
QString lastPlayedText(const omnios::Game& game) {
    if (game.lastPlayed <= 0) {
        // Only Steam keeps a record, so only for Steam does "none" mean never.
        return game.platform == omnios::Platform::Steam
                   ? GameListModel::tr("Never played") : QString();
    }
    const QDate played = QDateTime::fromSecsSinceEpoch(game.lastPlayed).date();
    const qint64 days = played.daysTo(QDate::currentDate());
    if (days <= 0) return GameListModel::tr("Played today");
    if (days == 1) return GameListModel::tr("Played yesterday");
    if (days < 7) return GameListModel::tr("Played %n days ago", nullptr, static_cast<int>(days));
    return GameListModel::tr("Last played %1").arg(QLocale().toString(played, QLocale::ShortFormat));
}

}  // namespace

GameListModel::GameListModel(QObject* parent) : QAbstractListModel(parent) {}

int GameListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(rows_.size());
}

QVariant GameListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};

    const omnios::Game& game = gameAt(index.row());

    switch (role) {
        case IdRole:           return QString::fromStdString(game.id);
        case TitleRole:        return QString::fromStdString(game.title);
        case PlatformIdRole:   return QString::fromStdString(std::string(omnios::platformId(game.platform)));
        case PlatformNameRole: return QString::fromStdString(std::string(omnios::platformDisplayName(game.platform)));
        case BadgeColorRole:   return badgeColorFor(game.platform);
        case CoverRole:
            // Empty means "no art yet"; QML draws the generated fallback tile
            // rather than a broken image.
            return game.coverPath.empty()
                       ? QString()
                       : QUrl::fromLocalFile(QString::fromStdString(game.coverPath.string())).toString();
        case SizeRole:        return QString::fromStdString(game.displaySize());
        case DeveloperRole:   return QString::fromStdString(game.developer);
        case PublisherRole:   return QString::fromStdString(game.publisher);
        case DescriptionRole: return QString::fromStdString(game.description);
        case DetectionRole:
            return QString::fromStdString(std::string(omnios::detectionSourceName(game.detectionSource)));
        case LastPlayedRole: return lastPlayedText(game);
        case LastPlayedTimeRole: return static_cast<qint64>(game.lastPlayed);
        default: break;
    }

    // Engine, tier and playability all come from the router, so the detail
    // screen shows exactly what pressing Play would do.
    omnios::LaunchOptions options;
    options.skipAvailabilityCheck = false;
    const omnios::LaunchPlan plan = omnios::planLaunch(game, options);

    switch (role) {
        case EngineRole:
            return plan.engineDisplayName.empty()
                       ? QStringLiteral("unavailable")
                       : QString::fromStdString(plan.engineDisplayName);
        case TierRole:     return QString::fromStdString(std::string(omnios::tierName(plan.tier)));
        case PlayableRole: return plan.ok;
        default:           return {};
    }
}

QHash<int, QByteArray> GameListModel::roleNames() const {
    return {
        {IdRole, "gameId"},
        {TitleRole, "title"},
        {PlatformIdRole, "platformId"},
        {PlatformNameRole, "platformName"},
        {BadgeColorRole, "badgeColor"},
        {CoverRole, "cover"},
        {SizeRole, "sizeText"},
        {DeveloperRole, "developer"},
        {PublisherRole, "publisher"},
        {DescriptionRole, "description"},
        {EngineRole, "engineName"},
        {TierRole, "tierName"},
        {DetectionRole, "detectionSource"},
        {PlayableRole, "playable"},
        {LastPlayedRole, "lastPlayed"},
        {LastPlayedTimeRole, "lastPlayedTime"},
    };
}

void GameListModel::setLibrary(omnios::GameLibrary library) {
    // The reset begins before anything changes: views read the old rows in
    // modelAboutToBeReset (Main.qml notes the focused game there), and the
    // old rows index the old library.
    beginResetModel();
    library_ = std::move(library);
    // A system whose last game has gone is no longer offered, so it could
    // not be unticked: dropped from the filter too.
    QStringList present;
    for (const omnios::Game& game : library_.games())
        present << QString::fromStdString(std::string(omnios::platformId(game.platform)));
    systems_.removeIf([&](const QString& id) { return !present.contains(id); });
    rebuildRows();
    endResetModel();
    emit countChanged();
    emit filterChanged();
}

void GameListModel::applyFilter() {
    beginResetModel();
    rebuildRows();
    endResetModel();
    emit countChanged();
    emit filterChanged();
}

void GameListModel::rebuildRows() {
    rows_.clear();
    const std::string query = search_.toStdString();
    const auto& games = library_.games();
    for (std::size_t i = 0; i < games.size(); ++i) {
        const QString system = QString::fromStdString(std::string(omnios::platformId(games[i].platform)));
        if (!systems_.isEmpty() && !systems_.contains(system)) continue;
        if (!omnios::matchesSearch(games[i], query)) continue;
        rows_.push_back(i);
    }
}

void GameListModel::setSearch(const QString& text) {
    if (text == search_) return;
    search_ = text;
    applyFilter();
}

void GameListModel::toggleSystem(const QString& id) {
    if (id.isEmpty()) systems_.clear();
    else if (!systems_.removeOne(id)) systems_ << id;
    applyFilter();
}

void GameListModel::clearFilter() {
    if (!filtering()) return;
    search_.clear();
    systems_.clear();
    applyFilter();
}

QVariantList GameListModel::systemOptions() const {
    // Each system's count, in the order of their names.
    QMap<QString, QPair<QString, int>> byName;
    for (const omnios::Game& game : library_.games()) {
        const QString name = QString::fromStdString(std::string(omnios::platformDisplayName(game.platform)));
        auto& entry = byName[name];
        entry.first = QString::fromStdString(std::string(omnios::platformId(game.platform)));
        ++entry.second;
    }
    QVariantList options{QVariantMap{{QStringLiteral("id"), QString()},
                                     {QStringLiteral("name"), tr("All systems")},
                                     {QStringLiteral("detail"), QString::number(total())},
                                     {QStringLiteral("checked"), systems_.isEmpty()}}};
    for (auto it = byName.constBegin(); it != byName.constEnd(); ++it) {
        options << QVariantMap{{QStringLiteral("id"), it.value().first},
                               {QStringLiteral("name"), it.key()},
                               {QStringLiteral("detail"), QString::number(it.value().second)},
                               {QStringLiteral("checked"), systems_.contains(it.value().first)}};
    }
    return options;
}

void GameListModel::setCover(const QString& id, const QString& path) {
    if (!library_.setCover(id.toStdString(), std::filesystem::path(path.toStdString()))) return;
    const int row = indexOfId(id);
    if (row < 0) return;
    const QModelIndex changed = index(row, 0);
    emit dataChanged(changed, changed, {CoverRole});
}

void GameListModel::setLastPlayed(const QString& id, qint64 when) {
    if (!library_.setLastPlayed(id.toStdString(), when)) return;
    const int row = indexOfId(id);
    if (row < 0) return;
    const QModelIndex changed = index(row, 0);
    emit dataChanged(changed, changed, {LastPlayedRole, LastPlayedTimeRole});
}

QVariantMap GameListModel::get(int row) const {
    QVariantMap out;
    if (row < 0 || row >= rowCount()) return out;
    const QModelIndex idx = index(row, 0);
    const QHash<int, QByteArray> names = roleNames();
    for (auto it = names.constBegin(); it != names.constEnd(); ++it)
        out.insert(QString::fromUtf8(it.value()), data(idx, it.key()));
    return out;
}

int GameListModel::indexOfId(const QString& id) const {
    const std::string needle = id.toStdString();
    for (std::size_t row = 0; row < rows_.size(); ++row) {
        if (gameAt(static_cast<int>(row)).id == needle) return static_cast<int>(row);
    }
    return -1;
}

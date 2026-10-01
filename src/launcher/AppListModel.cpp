#include "AppListModel.h"

#include <QSet>
#include <QUrl>

#include "omnios/Apps.h"
#include "omnios/DesktopEntry.h"

namespace {

QString view(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

QString iconUrl(const std::string& path) {
    // Empty when the theme has no icon; the tile then falls back to its colour
    // wash rather than showing a broken image.
    return path.empty() ? QString()
                        : QUrl::fromLocalFile(QString::fromStdString(path)).toString();
}

// Colour for a discovered tile. There is no palette entry to look up — nothing
// here knew about the app until it was installed — so it is derived from the
// name, which at least keeps a given app the same colour between boots instead
// of shuffling the grid every refresh.
QString colourFor(const QString& name) {
    static const char* const kPalette[] = {
        "#6C63FF", "#3A8FFF", "#E85D04", "#17B2E7",
        "#E4000F", "#2FA84F", "#B5179E", "#F4A261",
    };
    unsigned hash = 2166136261u;
    for (QChar c : name) hash = (hash ^ c.unicode()) * 16777619u;
    return QString::fromLatin1(kPalette[hash % (sizeof(kPalette) / sizeof(kPalette[0]))]);
}

}  // namespace

AppListModel::AppListModel(QObject* parent) : QAbstractListModel(parent) { refresh(); }

int AppListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return rows_.size();
}

QVariant AppListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size()) return {};

    const Row& row = rows_.at(index.row());
    switch (role) {
        case IdRole:          return row.id;
        case NameRole:        return row.title;
        case DescriptionRole: return row.description;
        case BadgeColorRole:  return row.badgeColor;
        case PackageRole:     return row.package;
        case CommandRole:     return row.argv.join(QLatin1Char(' '));
        case IconRole:        return row.icon;
        case AvailableRole:   return row.available;
        case RemovableRole:   return row.removable;
        case KindRole:        return row.kind;
        case UpdateRole:      return updatable_.contains(row.id);
        default:              return {};
    }
}

QHash<int, QByteArray> AppListModel::roleNames() const {
    // Names chosen to match GameListModel where the meaning matches, so one
    // tile component serves both models.
    return {
        {IdRole, "appId"},
        {NameRole, "title"},
        {DescriptionRole, "description"},
        {BadgeColorRole, "badgeColor"},
        {AvailableRole, "playable"},
        {PackageRole, "package"},
        {CommandRole, "commandLine"},
        {IconRole, "iconSource"},
        {RemovableRole, "removable"},
        {KindRole, "kind"},
        {UpdateRole, "hasUpdate"},
    };
}

QStringList AppListModel::ids() const {
    QStringList out;
    for (const Row& row : rows_) out << row.id;
    return out;
}

void AppListModel::setUpdatable(const QSet<QString>& ids) {
    if (ids == updatable_) return;
    updatable_ = ids;
    if (!rows_.isEmpty()) emit dataChanged(index(0, 0), index(static_cast<int>(rows_.size()) - 1, 0), {UpdateRole});
}

QVariantMap AppListModel::get(int row) const {
    QVariantMap out;
    if (row < 0 || row >= rows_.size()) return out;
    const QModelIndex idx = index(row, 0);
    const QHash<int, QByteArray> names = roleNames();
    for (auto it = names.constBegin(); it != names.constEnd(); ++it)
        out.insert(QString::fromUtf8(it.value()), data(idx, it.key()));
    return out;
}

const AppListModel::Row* AppListModel::find(const QString& appId) const {
    for (const Row& row : rows_)
        if (row.id == appId) return &row;
    return nullptr;
}

QStringList AppListModel::argvFor(const QString& appId) const {
    const Row* row = find(appId);
    return row == nullptr ? QStringList() : row->argv;
}

AppListModel::Removal AppListModel::removalFor(const QString& appId) const {
    Removal removal;
    const Row* row = find(appId);
    if (row == nullptr) return removal;

    removal.known = true;
    removal.title = row->title;
    if (!row->removable) {
        removal.refusal = tr("%1 is part of OmniOS and cannot be removed").arg(row->title);
        return removal;
    }
    removal.flatpak = row->flatpak;
    // A Flatpak is addressed by its application id, which is also the name of
    // its desktop file. A native app is addressed by the file itself, because
    // only pacman knows which package put it there.
    removal.target = row->flatpak ? row->id : row->desktopPath;
    return removal;
}

void AppListModel::refresh() {
    beginResetModel();
    rows_.clear();

    QSet<QString> claimed;

    for (const omnios::App& app : omnios::allApps()) {
        Row row;
        row.id          = view(app.id);
        row.title       = view(app.name);
        row.description = view(app.description);
        row.badgeColor  = view(app.badgeColor);
        row.package     = view(app.package);
        row.icon        = iconUrl(omnios::appIconPath(app));
        row.kind        = QStringLiteral("APP");
        row.available   = omnios::appAvailable(app);
        row.removable   = !omnios::packageIsProtected(app.package);

        for (const std::string& part : omnios::appArgv(app)) row.argv << QString::fromStdString(part);

        // The built-in tile is the curated one: a name and a description
        // written for a console, plus the arguments mpv and chromium both
        // needed before they would work here at all. Its desktop entry must not
        // produce a second tile for the same program.
        claimed.insert(row.argv.value(0));
        rows_.push_back(row);
    }

    for (const omnios::DesktopApp& app : omnios::installedApps()) {
        const QString command = QString::fromStdString(app.argv.front());
        if (claimed.contains(command)) continue;

        Row row;
        row.id          = QString::fromStdString(app.id);
        row.title       = QString::fromStdString(app.name);
        row.description = QString::fromStdString(app.comment);
        row.badgeColor  = colourFor(row.title);
        row.icon        = iconUrl(omnios::iconPathFor(app.icon));
        row.desktopPath = QString::fromStdString(app.path.generic_string());
        row.flatpak     = app.flatpak;
        row.kind        = app.flatpak ? QStringLiteral("FLATPAK") : QStringLiteral("APP");
        row.available   = true;
        // Nothing discovered here can be protected: every protected package is
        // claimed above by a built-in tile.
        row.removable   = true;

        for (const std::string& part : app.argv) row.argv << QString::fromStdString(part);

        rows_.push_back(row);
    }

    endResetModel();
    emit countChanged();
}

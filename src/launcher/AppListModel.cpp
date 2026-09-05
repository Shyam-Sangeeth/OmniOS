#include "AppListModel.h"

#include <QUrl>

#include "omnios/Apps.h"

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
        case CommandRole:     return row.command;
        case IconRole:        return row.icon;
        case AvailableRole:   return row.available;
        case RemovableRole:   return row.removable;
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
    };
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

void AppListModel::refresh() {
    beginResetModel();
    rows_.clear();

    for (const omnios::App& app : omnios::allApps()) {
        Row row;
        row.id          = view(app.id);
        row.title       = view(app.name);
        row.description = view(app.description);
        row.badgeColor  = view(app.badgeColor);
        row.package     = view(app.package);
        row.icon        = iconUrl(omnios::appIconPath(app));
        row.available   = omnios::appAvailable(app);
        row.removable   = !omnios::packageIsProtected(app.package);

        QStringList parts;
        for (const std::string& part : omnios::appArgv(app)) parts << QString::fromStdString(part);
        row.command = parts.join(QLatin1Char(' '));

        rows_.push_back(row);
    }

    // Catalogue apps appear here only once they exist. An uninstalled one has
    // no command to run and would be a tile that does nothing; it lives on the
    // store tab until then.
    for (const omnios::StoreApp& app : omnios::appCatalog()) {
        if (!omnios::storeAppInstalled(app)) continue;
        Row row;
        row.id          = view(app.id);
        row.title       = view(app.name);
        row.description = view(app.description);
        row.badgeColor  = view(app.badgeColor);
        row.package     = view(app.package);
        row.command     = view(app.command);
        row.icon        = iconUrl(omnios::storeIconPath(app));
        row.available   = true;
        row.removable   = !omnios::packageIsProtected(app.package);
        rows_.push_back(row);
    }

    endResetModel();
    emit countChanged();
}

#include "StoreListModel.h"

#include <QString>
#include <QUrl>

#include "omnios/Apps.h"

namespace {

QString view(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

}  // namespace

StoreListModel::StoreListModel(QObject* parent) : QAbstractListModel(parent) {}

int StoreListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(omnios::appCatalog().size());
}

QVariant StoreListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};

    const omnios::StoreApp& app = omnios::appCatalog()[static_cast<std::size_t>(index.row())];

    switch (role) {
        case IdRole:          return view(app.id);
        case NameRole:        return view(app.name);
        case DescriptionRole: return view(app.description);
        case CategoryRole:    return view(app.category);
        case BadgeColorRole:  return view(app.badgeColor);
        case PackageRole:     return view(app.package);
        case InstalledRole:   return omnios::storeAppInstalled(app);
        case IconRole: {
            // Only an installed package has an icon on disk. Asking before then
            // is not a failure, it is simply too early — the tile falls back to
            // its colour wash and picks the icon up on the refresh that follows
            // the install.
            const std::string path = omnios::storeIconPath(app);
            return path.empty() ? QString()
                                : QUrl::fromLocalFile(QString::fromStdString(path)).toString();
        }
        default: return {};
    }
}

QHash<int, QByteArray> StoreListModel::roleNames() const {
    return {
        {IdRole, "appId"},
        {NameRole, "title"},
        {DescriptionRole, "description"},
        {CategoryRole, "category"},
        {BadgeColorRole, "badgeColor"},
        {PackageRole, "package"},
        {InstalledRole, "installed"},
        {IconRole, "iconSource"},
    };
}

QVariantMap StoreListModel::get(int row) const {
    QVariantMap out;
    if (row < 0 || row >= rowCount()) return out;
    const QModelIndex idx = index(row, 0);
    const QHash<int, QByteArray> names = roleNames();
    for (auto it = names.constBegin(); it != names.constEnd(); ++it)
        out.insert(QString::fromUtf8(it.value()), data(idx, it.key()));
    return out;
}

void StoreListModel::refresh() {
    beginResetModel();
    endResetModel();
    emit countChanged();
}

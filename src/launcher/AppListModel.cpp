#include "AppListModel.h"

#include <QString>

#include "omnios/Apps.h"

namespace {

QString view(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

}  // namespace

AppListModel::AppListModel(QObject* parent) : QAbstractListModel(parent) {}

int AppListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(omnios::allApps().size());
}

QVariant AppListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};

    const omnios::App& app = omnios::allApps()[static_cast<std::size_t>(index.row())];

    switch (role) {
        case IdRole:          return view(app.id);
        case NameRole:        return view(app.name);
        case DescriptionRole: return view(app.description);
        case BadgeColorRole:  return view(app.badgeColor);
        case PackageRole:     return view(app.package);
        case AvailableRole:   return omnios::appAvailable(app);
        case CommandRole: {
            const std::vector<std::string> argv = omnios::appArgv(app);
            QStringList parts;
            for (const std::string& part : argv) parts << QString::fromStdString(part);
            return parts.join(QLatin1Char(' '));
        }
        default: return {};
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
    };
}

QVariantMap AppListModel::get(int row) const {
    QVariantMap out;
    if (row < 0 || row >= rowCount()) return out;
    const QModelIndex idx = index(row, 0);
    const QHash<int, QByteArray> names = roleNames();
    for (auto it = names.constBegin(); it != names.constEnd(); ++it)
        out.insert(QString::fromUtf8(it.value()), data(idx, it.key()));
    return out;
}

void AppListModel::refresh() {
    // The rows never change, but availability does. A full reset is cheap for
    // a handful of entries and avoids a stale "not installed" tile.
    beginResetModel();
    endResetModel();
    emit countChanged();
}

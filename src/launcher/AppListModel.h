// Qt model over the built-in app registry.
//
// Deliberately shaped like GameListModel: the same role names where they mean
// the same thing, so GameTile.qml draws an app tile and a game tile without
// knowing which it has. A tile is a tile.
#pragma once

#include <QAbstractListModel>
#include <QVariantMap>

class AppListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        DescriptionRole,
        BadgeColorRole,
        AvailableRole,
        PackageRole,
        CommandRole,
        IconRole,
    };
    Q_ENUM(Role)

    explicit AppListModel(QObject* parent = nullptr);

    int      rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QVariantMap get(int row) const;

    // Availability depends on what is installed, which a first-boot AUR
    // install can change underneath us; re-read it on refresh.
    Q_INVOKABLE void refresh();

signals:
    void countChanged();
};

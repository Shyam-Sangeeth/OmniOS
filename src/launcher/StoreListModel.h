// Qt model over the installable app catalogue — the store tab.
//
// Shaped like AppListModel and GameListModel, with the same role names where
// the meaning matches, so GameTile.qml draws a store tile without knowing it
// is one. What differs is `installed`: a store tile is a thing you may not
// have yet, so the tile shows its state rather than assuming it can be run.
#pragma once

#include <QAbstractListModel>
#include <QVariantMap>

class StoreListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        DescriptionRole,
        CategoryRole,
        BadgeColorRole,
        PackageRole,
        InstalledRole,
        IconRole,
    };
    Q_ENUM(Role)

    explicit StoreListModel(QObject* parent = nullptr);

    int      rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QVariantMap get(int row) const;

    // Installed state changes underneath us every time the store does its job.
    Q_INVOKABLE void refresh();

signals:
    void countChanged();
};

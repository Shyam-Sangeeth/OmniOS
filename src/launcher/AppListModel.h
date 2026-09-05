// Qt model over the app registry — the Apps tab.
//
// Deliberately shaped like GameListModel: the same role names where they mean
// the same thing, so GameTile.qml draws an app tile and a game tile without
// knowing which it has. A tile is a tile.
//
// Rows are the built-in apps followed by whichever catalogue apps are actually
// installed. Installing VLC from the store has to put VLC on the Apps tab, or
// the store would be a page that changes nothing you can see.
#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVariantMap>
#include <QVector>

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
        // False when the tile may be uninstalled. Drives the menu, and is only
        // ever advice: the removal itself is refused in the controller.
        RemovableRole,
    };
    Q_ENUM(Role)

    explicit AppListModel(QObject* parent = nullptr);

    int      rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QVariantMap get(int row) const;

    // What is installed changes whenever the store runs; re-read it.
    Q_INVOKABLE void refresh();

signals:
    void countChanged();

private:
    // Flattened so row order and the built-in/catalogue split are decided in
    // one place instead of in every data() branch.
    struct Row {
        QString id;
        QString title;
        QString description;
        QString badgeColor;
        QString package;
        QString command;
        QString icon;
        bool    available = false;
        bool    removable = false;
    };

    QVector<Row> rows_;
};

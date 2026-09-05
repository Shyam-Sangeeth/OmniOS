// Qt model over the app registry — the Apps tab.
//
// Deliberately shaped like GameListModel: the same role names where they mean
// the same thing, so GameTile.qml draws an app tile and a game tile without
// knowing which it has. A tile is a tile.
//
// Rows are the built-in apps followed by everything else installed on the
// machine, found through its desktop entries. That second half is what makes
// the store worth having: install something from Flathub and it appears here,
// with no catalogue in this repository to keep in step.
#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QStringList>
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
        // False when the tile may not be uninstalled. Drives the menu, and is
        // only ever advice: the removal itself is refused in the controller.
        RemovableRole,
        // What the tile's badge shows, so where an app came from is visible
        // without opening its menu.
        KindRole,
    };
    Q_ENUM(Role)

    explicit AppListModel(QObject* parent = nullptr);

    int      rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QVariantMap get(int row) const;

    // What is installed changes whenever the store runs; re-read it.
    Q_INVOKABLE void refresh();

    // Argv for launching the app with this id, empty when unknown. A discovered
    // app carries its own command line from its desktop entry, so the
    // controller never has to re-parse one.
    QStringList argvFor(const QString& appId) const;

    // How to remove the app with this id.
    struct Removal {
        bool known   = false;
        bool flatpak = false;
        // Flatpak application id, or the desktop file path to ask pacman which
        // package owns it.
        QString target;
        QString title;
        // Set when removal is refused outright, and says why.
        QString refusal;
    };
    Removal removalFor(const QString& appId) const;

signals:
    void countChanged();

private:
    // Flattened so row order and the built-in/discovered split are decided in
    // one place instead of in every data() branch.
    struct Row {
        QString     id;
        QString     title;
        QString     description;
        QString     badgeColor;
        QString     package;
        QString     icon;
        QString     kind;
        QStringList argv;
        QString     desktopPath;
        bool        available = false;
        bool        removable = false;
        bool        flatpak   = false;
    };

    const Row* find(const QString& appId) const;

    QVector<Row> rows_;
};

// Qt model over omnios::GameLibrary — Phase 10 (OmniOS.md §12).
//
// The launcher does not reimplement any of the library, scanner or router. It
// is a view onto the same core omnictl drives, so a game that lists correctly
// on the command line lists correctly here, and a routing fix benefits both.
#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVariantMap>

#include "omnios/GameLibrary.h"

class GameListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    // Roles are the tile's vocabulary: everything Main.qml needs to draw a
    // tile and its detail screen without calling back into C++.
    enum Role {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        PlatformIdRole,
        PlatformNameRole,
        BadgeColorRole,
        CoverRole,
        SizeRole,
        DeveloperRole,
        PublisherRole,
        DescriptionRole,
        EngineRole,
        TierRole,
        DetectionRole,
        PlayableRole,
        // "Played yesterday", "Never played", ... or empty when not known.
        LastPlayedRole,
    };
    Q_ENUM(Role)

    explicit GameListModel(QObject* parent = nullptr);

    int      rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Replaces the contents wholesale. A scan is cheap enough that diffing
    // would add risk for no visible gain at this library size.
    void setLibrary(omnios::GameLibrary library);

    const omnios::GameLibrary& library() const { return library_; }

    // Index of a game by id, or -1. Used to restore focus after a rescan.
    Q_INVOKABLE int indexOfId(const QString& id) const;

    // One row as a map of role name to value. QAbstractListModel exposes roles
    // to delegates but gives QML no way to read a row outside one, and the
    // hero panel needs the focused game while the delegate does not exist yet.
    Q_INVOKABLE QVariantMap get(int row) const;

signals:
    void countChanged();

private:
    omnios::GameLibrary library_;
};

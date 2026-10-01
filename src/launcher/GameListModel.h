// Qt model over omnios::GameLibrary — Phase 10 (OmniOS.md §12).
//
// The launcher does not reimplement any of the library, scanner or router. It
// is a view onto the same core omnictl drives, so a game that lists correctly
// on the command line lists correctly here, and a routing fix benefits both.
//
// Its rows are the games the Games tab's filters leave — a search and the
// systems ticked — not the whole library: the grid, its menus and its pages
// all work by row, so filtering here keeps every one of them right. library()
// is still the whole of it.
#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <vector>

#include "omnios/GameLibrary.h"

class GameListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    // The whole library, filtered or not.
    Q_PROPERTY(int total READ total NOTIFY countChanged)
    Q_PROPERTY(QString search READ search NOTIFY filterChanged)
    // A search or a system is set.
    Q_PROPERTY(bool filtering READ filtering NOTIFY filterChanged)
    // For the System filter (FilterDropdown): "All systems", then each system
    // in the library with how many games it has.
    Q_PROPERTY(QVariantList systemOptions READ systemOptions NOTIFY filterChanged)

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
        // The same in seconds since 1970, 0 for never: for sorting by it.
        LastPlayedTimeRole,
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

    int          total() const { return static_cast<int>(library_.size()); }
    QString      search() const { return search_; }
    bool         filtering() const { return !search_.trimmed().isEmpty() || !systems_.isEmpty(); }
    QVariantList systemOptions() const;

    // The search box's text, as it is typed.
    Q_INVOKABLE void setSearch(const QString& text);
    // A system ticked or unticked, by platform id; "" unticks them all.
    Q_INVOKABLE void toggleSystem(const QString& id);
    // Back to the whole library.
    Q_INVOKABLE void clearFilter();

    // One game's cover, arrived after the scan: that tile alone changes.
    void setCover(const QString& id, const QString& path);
    // One game started just now (`when`): that row alone changes.
    void setLastPlayed(const QString& id, qint64 when);

    // Index of a game by id, or -1. Used to restore focus after a rescan.
    Q_INVOKABLE int indexOfId(const QString& id) const;

    // One row as a map of role name to value. QAbstractListModel exposes roles
    // to delegates but gives QML no way to read a row outside one, and the
    // hero panel needs the focused game while the delegate does not exist yet.
    Q_INVOKABLE QVariantMap get(int row) const;

signals:
    void countChanged();
    void filterChanged();

private:
    // rows_ again, from the library and the filters.
    void applyFilter();
    const omnios::Game& gameAt(int row) const { return library_.games()[rows_[static_cast<std::size_t>(row)]]; }

    omnios::GameLibrary library_;
    // The library's index of each row's game.
    std::vector<std::size_t> rows_;
    QString     search_;
    QStringList systems_;  // platform ids; none means every system
};

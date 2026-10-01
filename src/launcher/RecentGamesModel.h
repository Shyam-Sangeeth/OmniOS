// The Games tab's "Continue playing" row: the games played most recently,
// newest first, at most kLimit of them.
//
// A view onto GameListModel rather than a list of its own, so the row's tiles
// are the library's rows: sourceRow() gives the grid's index for one, and the
// detail page, the game menu and Play all work from that as they do for the
// grid.
#pragma once

#include <QSortFilterProxyModel>

class GameListModel;

class RecentGamesModel : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    // One screen's width of tiles, and a little more to scroll to.
    static constexpr int kLimit = 10;

    explicit RecentGamesModel(GameListModel* games, QObject* parent = nullptr);

    int count() const { return rowCount(); }

    // The game's row in GameListModel, or -1.
    Q_INVOKABLE int sourceRow(int row) const;

signals:
    void countChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
};

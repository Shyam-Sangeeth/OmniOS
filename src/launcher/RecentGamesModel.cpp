#include "RecentGamesModel.h"

#include "GameListModel.h"

RecentGamesModel::RecentGamesModel(GameListModel* games, QObject* parent) : QSortFilterProxyModel(parent) {
    setSourceModel(games);
    setSortRole(GameListModel::LastPlayedTimeRole);
    sort(0, Qt::DescendingOrder);

    // A game started moves to the front, and can push another out of the
    // last place: the filter depends on every row's time, not just its own,
    // which the proxy's own update (of the changed row alone) does not know.
    connect(games, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex&, const QModelIndex&, const QList<int>& roles) {
                if (roles.isEmpty() || roles.contains(GameListModel::LastPlayedTimeRole)) invalidate();
            });
    for (const auto signal : {&QAbstractItemModel::rowsInserted, &QAbstractItemModel::rowsRemoved})
        connect(this, signal, this, &RecentGamesModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &RecentGamesModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &RecentGamesModel::countChanged);
}

int RecentGamesModel::sourceRow(int row) const {
    if (row < 0 || row >= rowCount()) return -1;
    return mapToSource(index(row, 0)).row();
}

bool RecentGamesModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
    const QAbstractItemModel* games = sourceModel();
    const auto playedAt = [&](int row) {
        return games->index(row, 0, sourceParent).data(GameListModel::LastPlayedTimeRole).toLongLong();
    };
    const qint64 mine = playedAt(sourceRow);
    if (mine <= 0) return false;
    // In only among the kLimit latest; a tie goes to the earlier row.
    int newer = 0;
    for (int row = 0; row < games->rowCount(sourceParent) && newer < kLimit; ++row) {
        const qint64 theirs = playedAt(row);
        if (theirs > mine || (theirs == mine && row < sourceRow)) ++newer;
    }
    return newer < kLimit;
}

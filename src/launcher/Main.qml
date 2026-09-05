// The OmniOS shell (OmniOS.md §12).
//
// Two tabs, Games and Apps, rather than two stacked sections. Stacking meant
// the games grid started halfway down the screen and shrank as apps were
// added; tabs give each one the whole screen and make the library the thing
// you land on.
import QtQuick
import QtQuick.Window
import omnios

Window {
    id: window
    visible: true
    visibility: Window.FullScreen
    title: "OmniOS"
    color: Theme.background

    // 0 = Games, 1 = Apps. Games first: this is a console, and the library is
    // the point of it.
    property int currentTab: 0
    readonly property var activeGrid: currentTab === 0 ? gamesGrid : appsGrid

    property var currentGame: gamesGrid.currentIndex >= 0 && GameLibrary.count > 0
                              ? GameLibrary.get(gamesGrid.currentIndex)
                              : null

    // Background tint follows whatever is focused, in either tab.
    property color accentOfFocus: {
        if (currentTab === 0)
            return currentGame ? currentGame.badgeColor : Theme.background
        if (AppLibrary.count > 0 && appsGrid.currentIndex >= 0)
            return AppLibrary.get(appsGrid.currentIndex).badgeColor
        return Theme.background
    }

    Rectangle { anchors.fill: parent; color: Theme.background }
    Rectangle {
        anchors.fill: parent
        opacity: 0.30
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.darker(window.accentOfFocus, 3.0) }
            GradientStop { position: 0.75; color: Theme.background }
        }
        Behavior on opacity { NumberAnimation { duration: Theme.backgroundDuration } }
    }

    // ---- top bar ----------------------------------------------------------
    Item {
        id: topBar
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: 58

        Text {
            anchors { left: parent.left; leftMargin: Theme.gutter; verticalCenter: parent.verticalCenter }
            text: "OmniOS"
            color: Theme.textPrimary
            font.pixelSize: 22
            font.letterSpacing: 2
        }

        Text {
            anchors { right: parent.right; rightMargin: Theme.gutter; verticalCenter: parent.verticalCenter }
            text: Launcher.scanning ? qsTr("Scanning …") : Launcher.status
            color: Theme.textSecondary
            font.pixelSize: 13
        }
    }

    // ---- tabs -------------------------------------------------------------
    Row {
        id: tabBar
        anchors { top: topBar.bottom; left: parent.left; leftMargin: Theme.gutter }
        spacing: 26

        Repeater {
            model: [qsTr("GAMES"), qsTr("APPS")]

            Item {
                width: tabText.implicitWidth
                height: 34

                Text {
                    id: tabText
                    anchors.top: parent.top
                    text: modelData
                    color: window.currentTab === index ? Theme.textPrimary : Theme.textSecondary
                    font.pixelSize: 13
                    font.letterSpacing: 3
                    Behavior on color { ColorAnimation { duration: Theme.focusDuration } }
                }

                // Underline marks the active tab; §12 asks for one thing in
                // focus at a time and this is the cheapest honest indicator.
                Rectangle {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom; bottomMargin: 6 }
                    height: 2
                    color: Theme.accent
                    opacity: window.currentTab === index ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: Theme.focusDuration } }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: window.selectTab(index)
                }
            }
        }
    }

    Rectangle {
        id: tabRule
        anchors { top: tabBar.bottom; left: parent.left; right: parent.right }
        height: 1
        color: "#1AFFFFFF"
    }

    // ---- content ----------------------------------------------------------
    Item {
        id: content
        anchors {
            top: tabRule.bottom; topMargin: 18
            left: parent.left; leftMargin: Theme.gutter
            right: parent.right; rightMargin: Theme.gutter
            bottom: runningBanner.visible ? runningBanner.top : footer.top
            bottomMargin: 10
        }

        // ---- games ---------------------------------------------------------
        GridView {
            id: gamesGrid
            anchors.fill: parent
            // Visible even when empty: an invisible item cannot hold focus,
            // and with no games that left the whole launcher unfocused and
            // deaf to every key. The empty-state message draws over it.
            visible: window.currentTab === 0
            clip: true
            cellWidth: Theme.gridWidth + Theme.cellPadding
            cellHeight: Theme.gridHeight + Theme.cellPadding
            model: GameLibrary
            highlightMoveDuration: Theme.focusDuration

            // The delegate is the whole cell with the tile centred inside it.
            // Placed flush at the cell origin instead, a focused tile's 1.06
            // scale and its -2px focus ring both overflow the cell's left and
            // top edges and are cut off by clip: true — the ring then renders
            // as an L-shaped shadow down the right and bottom only.
            delegate: Item {
                width: gamesGrid.cellWidth
                height: gamesGrid.cellHeight

                GameTile {
                    anchors.centerIn: parent
                    title: model.title
                    platformName: model.platformName
                    badgeColor: model.badgeColor
                    cover: model.cover
                    playable: model.playable
                    selected: gamesGrid.activeFocus && parent.GridView.isCurrentItem
                              && !detailLoader.active

                    MouseArea {
                        anchors.fill: parent
                        onClicked: { gamesGrid.currentIndex = index; window.openDetail() }
                    }
                }
            }

            Keys.onReturnPressed: window.openDetail()
            Keys.onEnterPressed: window.openDetail()
            Keys.onTabPressed: window.selectTab(1)
            Keys.onBacktabPressed: window.selectTab(1)
        }

        // Only covers the games tab; the apps tab is useful with no games.
        Column {
            anchors.centerIn: parent
            spacing: 12
            visible: window.currentTab === 0 && GameLibrary.count === 0 && !Launcher.scanning

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("No games yet")
                color: Theme.textPrimary
                font.pixelSize: 30
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Copy games into %1 and press F5").arg(Launcher.gamesPath)
                color: Theme.textSecondary
                font.pixelSize: 14
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Tab  ·  switch to Apps")
                color: Theme.accent
                font.pixelSize: 13
            }
        }

        // ---- apps ----------------------------------------------------------
        GridView {
            id: appsGrid
            anchors.fill: parent
            visible: window.currentTab === 1
            clip: true
            cellWidth: Theme.gridWidth + Theme.cellPadding
            cellHeight: Theme.gridHeight + Theme.cellPadding
            model: AppLibrary
            highlightMoveDuration: Theme.focusDuration

            delegate: Item {
                width: appsGrid.cellWidth
                height: appsGrid.cellHeight

                GameTile {
                    anchors.centerIn: parent
                    title: model.title
                    platformName: qsTr("APP")
                    badgeColor: model.badgeColor
                    cover: ""
                    iconSource: model.iconSource
                    playable: model.playable
                    selected: appsGrid.activeFocus && parent.GridView.isCurrentItem

                    MouseArea {
                        anchors.fill: parent
                        onClicked: { appsGrid.currentIndex = index; window.openCurrentApp() }
                    }
                }
            }

            Keys.onReturnPressed: window.openCurrentApp()
            Keys.onEnterPressed: window.openCurrentApp()
            Keys.onTabPressed: window.selectTab(0)
            Keys.onBacktabPressed: window.selectTab(0)
        }
    }

    // ---- running strip ----------------------------------------------------
    Rectangle {
        id: runningBanner
        visible: Launcher.gameRunning
        anchors { left: parent.left; right: parent.right; bottom: footer.top }
        height: 42
        color: "#1B1830"

        Rectangle {
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
            width: 3
            color: Theme.accent
        }
        Text {
            anchors { left: parent.left; leftMargin: Theme.gutter; verticalCenter: parent.verticalCenter }
            text: qsTr("▶  %1 is running").arg(Launcher.runningTitle)
            color: Theme.textPrimary
            font.pixelSize: 14
        }
        Text {
            anchors { right: parent.right; rightMargin: Theme.gutter; verticalCenter: parent.verticalCenter }
            text: qsTr("Super  library    ·    Super+Tab  back to it")
            color: Theme.textSecondary
            font.pixelSize: 12
        }
    }

    // ---- footer -----------------------------------------------------------
    Item {
        id: footer
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
        height: 44

        Text {
            anchors { left: parent.left; leftMargin: Theme.gutter; verticalCenter: parent.verticalCenter }
            text: qsTr("↑↓←→ move    Enter open    Tab switch tab    Super home    F5 rescan")
            color: Theme.textSecondary
            font.pixelSize: 12
        }
        Text {
            anchors { right: parent.right; rightMargin: Theme.gutter; verticalCenter: parent.verticalCenter }
            text: "OmniOS " + Launcher.version
            color: "#55FFFFFF"
            font.pixelSize: 12
        }
    }

    // ---- detail overlay ---------------------------------------------------
    Loader {
        id: detailLoader
        anchors.fill: parent
        active: false
        sourceComponent: GameDetail {
            game: window.currentGame
            onClosed: { detailLoader.active = false; gamesGrid.forceActiveFocus() }
            onPlayed: {
                if (Launcher.launch(window.currentGame.gameId)) {
                    detailLoader.active = false
                    gamesGrid.forceActiveFocus()
                }
            }
        }
        onLoaded: item.forceActiveFocus()
    }

    // ---- keys -------------------------------------------------------------
    // Tab is handled on the grids themselves, not here. Qt Quick's focus
    // traversal consumes Tab before a Shortcut ever sees it, so a Shortcut
    // bound to it simply never fires — which is what happened.
    //
    // F5 is not a navigation key, so a Shortcut works and covers the case
    // where focus has gone somewhere unexpected.
    Shortcut {
        sequence: "F5"
        onActivated: Launcher.refresh()
    }

    function selectTab(index) {
        currentTab = index
        // Focus follows the tab, otherwise the arrow keys keep driving the
        // grid that is no longer on screen.
        activeGrid.forceActiveFocus()
    }

    function openCurrentApp() {
        if (AppLibrary.count === 0) return
        Launcher.launchApp(AppLibrary.get(appsGrid.currentIndex).appId)
    }

    function openDetail() {
        if (GameLibrary.count === 0) return
        detailLoader.active = true
    }

    // Re-assert focus whenever the window becomes active. Returning from an
    // app leaves Qt's focus item unset, and without this the grid is visible
    // but deaf until something is clicked.
    onActiveChanged: if (active) activeGrid.forceActiveFocus()

    Component.onCompleted: gamesGrid.forceActiveFocus()
}

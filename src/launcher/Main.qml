// The OmniOS shell (OmniOS.md §12).
//
// Three tabs — Games, Apps, Store — rather than stacked sections. Stacking
// meant the games grid started halfway down the screen and shrank as apps were
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

    // 0 = Games, 1 = Apps, 2 = Store. Games first: this is a console, and the
    // library is the point of it.
    property int currentTab: 0
    readonly property var tabs: [gamesGrid, appsGrid, storeGrid]
    readonly property var activeGrid: tabs[currentTab]

    property var currentGame: gamesGrid.currentIndex >= 0 && GameLibrary.count > 0
                              ? GameLibrary.get(gamesGrid.currentIndex)
                              : null

    // Background tint follows whatever is focused, in any tab.
    property color accentOfFocus: {
        if (currentTab === 0)
            return currentGame ? currentGame.badgeColor : Theme.background
        var model = currentTab === 1 ? AppLibrary : AppStore
        var grid = activeGrid
        if (model.count > 0 && grid.currentIndex >= 0)
            return model.get(grid.currentIndex).badgeColor
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

        // A package operation takes minutes and matters more than the scan
        // count while it runs, so it takes the same slot rather than adding
        // another line of chrome.
        Text {
            anchors { right: parent.right; rightMargin: Theme.gutter; verticalCenter: parent.verticalCenter }
            text: Launcher.packageStatus !== "" ? Launcher.packageStatus
                                                : (Launcher.scanning ? qsTr("Scanning …") : Launcher.status)
            color: Launcher.packageBusy ? Theme.accent : Theme.textSecondary
            font.pixelSize: 13
        }
    }

    // ---- tabs -------------------------------------------------------------
    Row {
        id: tabBar
        anchors { top: topBar.bottom; left: parent.left; leftMargin: Theme.gutter }
        spacing: 26

        Repeater {
            model: [qsTr("GAMES"), qsTr("APPS"), qsTr("STORE")]

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
            Keys.onBacktabPressed: window.selectTab(2)
        }

        // Only covers the games tab; the other tabs are useful with no games.
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
                    hasMenu: true
                    selected: appsGrid.activeFocus && parent.GridView.isCurrentItem

                    onMenuRequested: function (anchorItem) {
                        appsGrid.currentIndex = index
                        // "installed" here is really "can it run" — a built-in
                        // whose package is missing gets an Install entry
                        // instead of an Open one that would fail.
                        tileMenu.openFor(anchorItem, model.appId, model.title,
                                         model.removable, model.playable)
                    }

                    // Below the button in stacking order, so a click on the
                    // dots opens the menu instead of launching the app.
                    MouseArea {
                        anchors.fill: parent
                        z: -1
                        onClicked: { appsGrid.currentIndex = index; window.openCurrentApp() }
                    }
                }
            }

            Keys.onReturnPressed: window.openCurrentApp()
            Keys.onEnterPressed: window.openCurrentApp()
            Keys.onTabPressed: window.selectTab(2)
            Keys.onBacktabPressed: window.selectTab(0)
            Keys.onMenuPressed: window.openTileMenu()
            Keys.onPressed: function (event) {
                if (event.key === Qt.Key_M) { window.openTileMenu(); event.accepted = true }
            }
        }

        // ---- store ---------------------------------------------------------
        GridView {
            id: storeGrid
            anchors.fill: parent
            visible: window.currentTab === 2
            clip: true
            cellWidth: Theme.gridWidth + Theme.cellPadding
            cellHeight: Theme.gridHeight + Theme.cellPadding
            model: AppStore
            highlightMoveDuration: Theme.focusDuration

            delegate: Item {
                width: storeGrid.cellWidth
                height: storeGrid.cellHeight

                GameTile {
                    anchors.centerIn: parent
                    title: model.title
                    // The badge carries the one fact that matters here: is it
                    // on this machine already?
                    platformName: model.installed ? qsTr("INSTALLED") : model.category
                    badgeColor: model.badgeColor
                    cover: ""
                    iconSource: model.iconSource
                    hasMenu: true
                    selected: storeGrid.activeFocus && parent.GridView.isCurrentItem

                    onMenuRequested: function (anchorItem) {
                        storeGrid.currentIndex = index
                        // Nothing in the catalogue is protected, so the store
                        // never has to disable its own uninstall entry.
                        tileMenu.openFor(anchorItem, model.appId, model.title,
                                         true, model.installed)
                    }

                    MouseArea {
                        anchors.fill: parent
                        z: -1
                        onClicked: {
                            storeGrid.currentIndex = index
                            window.storePrimaryAction()
                        }
                    }
                }
            }

            // Enter does the obvious thing: install it if it is missing, open
            // it if it is already here.
            Keys.onReturnPressed: window.storePrimaryAction()
            Keys.onEnterPressed: window.storePrimaryAction()
            Keys.onTabPressed: window.selectTab(0)
            Keys.onBacktabPressed: window.selectTab(1)
            Keys.onMenuPressed: window.openTileMenu()
            Keys.onPressed: function (event) {
                if (event.key === Qt.Key_M) { window.openTileMenu(); event.accepted = true }
            }
        }

        // On a live image nothing installed here survives a reboot. Said on the
        // store tab, where it changes what a user expects, rather than buried
        // in documentation they will read afterwards.
        Text {
            anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter }
            visible: window.currentTab === 2 && Launcher.ephemeral
            text: qsTr("Live image — anything installed here is gone at the next boot")
            color: Theme.textSecondary
            font.pixelSize: 12
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
            text: window.currentTab === 0
                  ? qsTr("↑↓←→ move    Enter open    Tab switch tab    Super home    F5 rescan")
                  : qsTr("↑↓←→ move    Enter open    M menu    Tab switch tab    Super home")
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

    // ---- tile menu --------------------------------------------------------
    // One instance for the whole window, filling it, so the panel is never
    // clipped by the grid cell whose tile opened it.
    TileMenu {
        id: tileMenu
        anchors.fill: parent
        onRequested: function (action) { window.runTileAction(action) }
        onClosed: window.activeGrid.forceActiveFocus()
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
        onActivated: {
            Launcher.refresh()
            AppLibrary.refresh()
            AppStore.refresh()
        }
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

    function currentEntry() {
        var model = currentTab === 1 ? AppLibrary : AppStore
        if (model.count === 0) return null
        var grid = activeGrid
        if (grid.currentIndex < 0) return null
        return model.get(grid.currentIndex)
    }

    function storePrimaryAction() {
        var entry = currentEntry()
        if (!entry) return
        if (entry.installed) Launcher.launchApp(entry.appId)
        else Launcher.installApp(entry.appId)
    }

    // The keyboard route to the menu, anchored to the focused tile so the panel
    // lands in the same place it would have from a click.
    function openTileMenu() {
        var entry = currentEntry()
        if (!entry) return
        var grid = activeGrid
        if (!grid.currentItem) return
        var installed = currentTab === 1 ? entry.playable : entry.installed
        var removable = currentTab === 1 ? entry.removable : true
        tileMenu.openFor(grid.currentItem, entry.appId, entry.title, removable, installed)
    }

    function runTileAction(action) {
        var id = tileMenu.appId
        if (action === "open")           Launcher.launchApp(id)
        else if (action === "check")     Launcher.checkForUpdate(id)
        else if (action === "update")    Launcher.updateApp(id)
        else if (action === "install")   Launcher.installApp(id)
        else if (action === "uninstall") Launcher.removeApp(id)
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

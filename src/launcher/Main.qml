// The OmniOS shell (OmniOS.md §12).
//
// Two tabs, Games and Apps, rather than stacked sections. Stacking meant the
// games grid started halfway down the screen and shrank as apps were added;
// tabs give each one the whole screen and make the library the thing you land
// on.
//
// There is no store in Game Mode. Installing things is Discover's job on the
// desktop, and whatever it installs comes back here as a tile, because the
// Apps tab is built from the machine's desktop entries rather than from a list
// kept here. The only built-in tile is Steam.
import QtQuick
import QtQuick.Window
import omnios

Window {
    id: window
    visible: true
    // Maximised and frameless rather than full screen: Game Mode runs in the
    // Plasma session, and this leaves room for Plasma's own panel along the
    // bottom — the same taskbar, tray and clock as the desktop, rather than a
    // copy of them.
    flags: Qt.Window | Qt.FramelessWindowHint
    visibility: Window.Maximized
    title: "OmniOS"
    color: Theme.background

    // 0 = Games, 1 = Apps. Games first: this is a console, and the library is
    // the point of it.
    property int currentTab: 0

    // The last thing worth saying, and only for as long as it is worth saying
    // it. Cleared on a timer so the corner does not carry a stale sentence for
    // the rest of the session.
    property string transientStatus: ""

    Timer {
        id: statusTimer
        interval: 7000
        onTriggered: window.transientStatus = ""
    }

    function showStatus(text) {
        if (text === "") return
        transientStatus = text
        // A package operation narrates itself and should stay put until it is
        // finished; everything else is a sentence that has already happened.
        statusTimer.restart()
    }

    Connections {
        target: Launcher
        function onStatusChanged() { window.showStatus(Launcher.status) }
        function onPackageStatusChanged() { window.showStatus(Launcher.packageStatus) }
    }

    // The focused game: the "Continue playing" row's while that row has the
    // focus (it stays its own while a menu or page opened from it is up),
    // otherwise the grid's. recentRevision makes it follow the row's order,
    // which changes under the same index when a game is played.
    property bool recentActive: false
    property int recentIndex: 0
    property int recentRevision: 0
    Connections {
        target: RecentGames
        function onCountChanged() { window.recentRevision++ }
    }
    property var currentGame: {
        void window.recentRevision
        if (recentActive && RecentGames.count > 0) {
            var row = RecentGames.sourceRow(Math.min(recentIndex, RecentGames.count - 1))
            if (row >= 0) return GameLibrary.get(row)
        }
        return gamesGrid.currentIndex >= 0 && GameLibrary.count > 0
               ? GameLibrary.get(gamesGrid.currentIndex)
               : null
    }

    // A rescan rebuilds the whole list, and Steam's library is rescanned on
    // its own whenever Steam adds a game. The grid keeps its index through
    // that, not its game: a new tile ahead of the focused one moved the focus
    // onto the newcomer. And a library that was empty came back with games but
    // no current one, so A opened a detail page for nothing — no title, no
    // Play, nothing to focus, and the controller dead until Escape on a
    // keyboard. So the focused game is noted before and found again after.
    property string focusedGameId: ""
    Connections {
        target: GameLibrary
        function onModelAboutToBeReset() {
            // The grid's own game, not the row's: this puts the grid back.
            var game = gamesGrid.currentIndex >= 0 && GameLibrary.count > 0
                       ? GameLibrary.get(gamesGrid.currentIndex) : null
            window.focusedGameId = (game && game.gameId) || ""
        }
        function onModelReset() { Qt.callLater(window.refocusGame) }
    }
    function refocusGame() {
        var index = window.focusedGameId !== "" ? GameLibrary.indexOfId(window.focusedGameId) : -1
        // Gone from under an open page: close it rather than show another game.
        if (index < 0 && window.focusedGameId !== "" && detailLoader.active) {
            detailLoader.active = false
            window.focusGames()
        }
        if (index < 0 && GameLibrary.count > 0)
            index = Math.max(0, Math.min(gamesGrid.currentIndex, GameLibrary.count - 1))
        gamesGrid.currentIndex = GameLibrary.count > 0 ? index : -1
    }

    // Background tint follows whatever is focused, in any tab.
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

    // Where the system menu (F10, Start) opens from: the top-left corner.
    // Nothing is drawn here — the menu is for the keyboard and the controller,
    // and Plasma's panel underneath has the same things for the pointer.
    Item {
        id: menuAnchor
        anchors { left: parent.left; leftMargin: Theme.gutter - 12; top: parent.top; topMargin: 16 }
        width: 1
        height: 1
    }

    // Updates waiting, or a restart to finish one: there until dealt with, and
    // giving way to a transient status while that has something to say.
    Text {
        anchors { right: parent.right; rightMargin: Theme.gutter; verticalCenter: tabBar.verticalCenter
                  verticalCenterOffset: -4 }
        text: Launcher.restartRequired
              ? qsTr("Restart to finish updating")
              : Launcher.updateCount > 0 ? qsTr("%1 available").arg(window.updatesText(Launcher.updateCount)) : ""
        color: Theme.accent
        font.pixelSize: 13
        opacity: window.transientStatus === "" && text !== "" ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.focusDuration } }
    }

    // "The disk is full", "Steam could not start": for a few seconds, then gone.
    Text {
        anchors { right: parent.right; rightMargin: Theme.gutter; verticalCenter: tabBar.verticalCenter
                  verticalCenterOffset: -4 }
        width: Math.min(implicitWidth, window.width * 0.45)
        text: window.transientStatus
        color: Launcher.storageCritical ? "#E4000F" : Theme.textSecondary
        font.pixelSize: 13
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignRight
        opacity: window.transientStatus === "" ? 0 : 1
        Behavior on opacity { NumberAnimation { duration: Theme.focusDuration } }
    }

    // ---- tabs -------------------------------------------------------------
    Row {
        id: tabBar
        anchors { top: parent.top; topMargin: 24; left: parent.left; leftMargin: Theme.gutter }
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
            bottom: footer.top
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

            // "Continue playing": the games played last, newest first, above
            // the library and scrolling with it, so the one you were on is
            // a press away however long the library grows. Gone until
            // something has been played.
            header: Item {
                readonly property ListView list: recentList
                width: gamesGrid.width
                height: RecentGames.count > 0 ? recentTitle.height + 6 + gamesGrid.cellHeight + 14 : 0
                visible: RecentGames.count > 0

                Text {
                    id: recentTitle
                    text: qsTr("CONTINUE PLAYING")
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    font.letterSpacing: 3
                }

                ListView {
                    id: recentList
                    anchors { top: recentTitle.bottom; topMargin: 6; left: parent.left; right: parent.right }
                    height: gamesGrid.cellHeight
                    orientation: ListView.Horizontal
                    clip: true
                    model: RecentGames
                    highlightMoveDuration: Theme.focusDuration

                    onCurrentIndexChanged: window.recentIndex = currentIndex

                    // A whole cell with the tile centred, as in the grid, so
                    // the focused tile's scale and ring are not clipped.
                    delegate: Item {
                        width: gamesGrid.cellWidth
                        height: gamesGrid.cellHeight
                        readonly property Item menuAnchor: recentTile.menuAnchor

                        GameTile {
                            id: recentTile
                            anchors.centerIn: parent
                            title: model.title
                            platformName: model.platformName
                            badgeColor: model.badgeColor
                            cover: model.cover
                            playable: model.playable
                            hasMenu: true
                            selected: recentList.activeFocus && parent.ListView.isCurrentItem
                                      && !detailLoader.active

                            onMenuRequested: function (anchorItem) {
                                window.focusRecent(index)
                                window.openGameMenu(anchorItem)
                            }

                            MouseArea {
                                anchors.fill: parent
                                z: -1
                                onClicked: { window.focusRecent(index); window.openDetail() }
                            }
                        }
                    }

                    Keys.onReturnPressed: window.openDetail()
                    Keys.onEnterPressed: window.openDetail()
                    Keys.onTabPressed: window.selectTab(1)
                    Keys.onBacktabPressed: window.selectTab(1)
                    Keys.onDownPressed: window.leaveRecent()
                    // Kept here: unhandled, they reach the grid the row is
                    // inside, and move its focus unseen.
                    Keys.onUpPressed: {}
                    Keys.onLeftPressed: decrementCurrentIndex()
                    Keys.onRightPressed: incrementCurrentIndex()
                    Keys.onMenuPressed: window.openGameMenu(currentItem ? currentItem.menuAnchor : null)
                    Keys.onPressed: function (event) {
                        if (event.key === Qt.Key_M) {
                            window.openGameMenu(currentItem ? currentItem.menuAnchor : null)
                            event.accepted = true
                        }
                    }
                }
            }

            // The delegate is the whole cell with the tile centred inside it.
            // Placed flush at the cell origin instead, a focused tile's 1.06
            // scale and its -2px focus ring both overflow the cell's left and
            // top edges and are cut off by clip: true — the ring then renders
            // as an L-shaped shadow down the right and bottom only.
            delegate: Item {
                width: gamesGrid.cellWidth
                height: gamesGrid.cellHeight
                readonly property Item menuAnchor: gameTile.menuAnchor

                GameTile {
                    id: gameTile
                    anchors.centerIn: parent
                    title: model.title
                    platformName: model.platformName
                    badgeColor: model.badgeColor
                    cover: model.cover
                    playable: model.playable
                    // Every game has a menu (openGameMenu); Steam's adds
                    // what only Steam can do.
                    hasMenu: true
                    // The row inside the grid keeps the grid's activeFocus
                    // true too, hence recentActive.
                    selected: gamesGrid.activeFocus && parent.GridView.isCurrentItem
                              && !window.recentActive && !detailLoader.active

                    onMenuRequested: function (anchorItem) {
                        gamesGrid.currentIndex = index
                        window.leaveRecent()
                        window.openGameMenu(anchorItem)
                    }

                    // Below the menu button, as on the Apps tab.
                    MouseArea {
                        anchors.fill: parent
                        z: -1
                        onClicked: { gamesGrid.currentIndex = index; window.leaveRecent(); window.openDetail() }
                    }
                }
            }

            Keys.onReturnPressed: window.openDetail()
            Keys.onEnterPressed: window.openDetail()
            Keys.onTabPressed: window.selectTab(1)
            Keys.onBacktabPressed: window.selectTab(1)
            // Up from the top row goes into "Continue playing"; anywhere
            // else it is the grid's.
            Keys.onUpPressed: function (event) {
                var columns = Math.max(1, Math.floor(width / cellWidth))
                event.accepted = currentIndex < columns && window.enterRecent()
            }
            Keys.onMenuPressed: window.openGameMenu(window.tileMenuAnchor(currentIndex))
            Keys.onPressed: function (event) {
                if (event.key === Qt.Key_M) {
                    window.openGameMenu(window.tileMenuAnchor(currentIndex))
                    event.accepted = true
                }
            }
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
                text: Theme.hint(qsTr("Copy games into %1 and press [F5]").arg(Launcher.gamesPath))
                color: Theme.textSecondary
                font.pixelSize: 14
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: Theme.hint(qsTr("[Tab]  ·  Switch to Apps"))
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
                    platformName: model.kind
                    badgeColor: model.badgeColor
                    cover: ""
                    iconSource: model.iconSource
                    playable: model.playable
                    hasMenu: true
                    selected: appsGrid.activeFocus && parent.GridView.isCurrentItem

                    onMenuRequested: function (anchorItem) {
                        appsGrid.currentIndex = index
                        menuPanel.openFor(anchorItem, model.title,
                                          window.appMenuEntries(model.removable,
                                                                model.playable),
                                          "app", model.appId)
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
            Keys.onTabPressed: window.selectTab(0)
            Keys.onBacktabPressed: window.selectTab(0)
            Keys.onMenuPressed: window.openTileMenu()
            Keys.onPressed: function (event) {
                if (event.key === Qt.Key_M) { window.openTileMenu(); event.accepted = true }
            }
        }

        // What someone is about to need to know, on the tab where installed
        // apps turn up, rather than buried in documentation they read afterwards:
        // that nothing here survives a reboot, and how much room is left.
        //
        // The second half is not decoration. A full overlay is what makes apps
        // stop starting, and with no notice the console simply appears broken.
        Text {
            anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter }
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: window.currentTab === 1 && Launcher.storageNotice !== ""
            text: Launcher.storageNotice
            color: Launcher.storageCritical ? "#E4000F" : Theme.textSecondary
            font.pixelSize: 12
            elide: Text.ElideRight
        }
    }

    // ---- key hints ------------------------------------------------------------
    // The keyboard and controller routes, along the bottom edge, just above
    // Plasma's panel.
    Item {
        id: footer
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
        height: 36

        Text {
            anchors { left: parent.left; leftMargin: Theme.gutter; verticalCenter: parent.verticalCenter }
            // The controller's own button names when a controller is in
            // use — ✕ and ○ on a DualSense — and the keyboard's otherwise.
            readonly property var b: Launcher.buttonNames
            text: Theme.hint(Launcher.gameRunning
                  ? qsTr("%1  Library    %2  Resume or quit    ·    %3 is running")
                        .arg(Launcher.usingController ? b.guide : "[Meta][Esc]")
                        .arg(Launcher.usingController ? b.start : "[F10]")
                        .arg(Launcher.runningTitle)
                  : Launcher.usingController
                    ? (window.currentTab === 0
                       ? (window.currentGame
                          ? qsTr("%1 Open    %2 Back    %3 Game menu    %4 %5 Switch tab    %6 Menu    %7 Rescan")
                                .arg(b.south).arg(b.east).arg(b.west).arg(b.l1).arg(b.r1).arg(b.start).arg(b.north)
                          : qsTr("%1 Open    %2 Back    %3 %4 Switch tab    %5 Menu    %6 Rescan")
                                .arg(b.south).arg(b.east).arg(b.l1).arg(b.r1).arg(b.start).arg(b.north))
                       : qsTr("%1 Open    %2 Back    %3 App menu    %4 %5 Switch tab    %6 Menu")
                             .arg(b.south).arg(b.east).arg(b.west).arg(b.l1).arg(b.r1).arg(b.start))
                    : window.currentTab === 0
                      ? (window.currentGame
                         ? qsTr("[↑][↓][←][→] Move    [Enter] Open    [M] Game menu    [Tab] Switch tab    [F10] Menu    [F5] Rescan")
                         : qsTr("[↑][↓][←][→] Move    [Enter] Open    [Tab] Switch tab    [F10] Menu    [F5] Rescan"))
                      : qsTr("[↑][↓][←][→] Move    [Enter] Open    [M] App menu    [Tab] Switch tab    [F10] Menu"))
            color: Theme.textSecondary
            font.pixelSize: 12
            elide: Text.ElideRight
            width: parent.width - 2 * Theme.gutter
        }
    }

    // ---- detail overlay ---------------------------------------------------
    Loader {
        id: detailLoader
        anchors.fill: parent
        active: false
        sourceComponent: GameDetail {
            game: window.currentGame
            // Back to the grid, or to "Continue playing" if opened from there.
            onClosed: { detailLoader.active = false; window.restoreFocus() }
            onPlayed: {
                if (Launcher.launch(window.currentGame.gameId)) {
                    detailLoader.active = false
                    window.restoreFocus()
                }
            }
        }
        onLoaded: item.forceActiveFocus()
    }

    // ---- menus ------------------------------------------------------------
    // One instance for the whole window, filling it, so the panel is never
    // clipped by the grid cell whose tile opened it. Both the tile menus and
    // the system menu go through it.
    MenuPanel {
        id: menuPanel
        anchors.fill: parent
        onChosen: function (action, context) {
            // Two of the system menu's entries are doors into another menu
            // rather than actions of their own.
            if ((context === "power" || context === "running") && action === "resume") Launcher.resumeRunningGame()
            else if (context === "running" && action === "quit") Launcher.quitRunningGame()
            else if (context === "running" && action === "library") {}
            else if (context === "running" && action === "system") window.openPowerMenu()
            else if (context === "running") Launcher.steamAction(Launcher.runningGameId, action)
            else if (context === "power" && action === "clearnotes") Launcher.clearNotifications()
            else if (context === "power" && action === "sysupdate") window.confirmUpdate()
            else if (context === "power" && action === "syscheck") {
                Launcher.checkSystemUpdates(true)
                window.showStatus(qsTr("Looking for updates ..."))
            }
            else if (context === "sysupdate" && action === "go") Launcher.updateSystem()
            else if (context === "sysupdate") {}
            else if (action === "quit") Launcher.quitRunningGame()
            else if (context === "uninstall" && action === "uninstall-confirmed") Launcher.uninstallGame(menuPanel.subject)
            else if (context === "uninstall") {}
            else if (context === "power" && action === "sound") window.openAudioMenu()
            else if (context === "power" && action === "desktop") Launcher.switchToDesktop()
            else if (context === "power" && action === "install") Launcher.installOmniOS()
            else if (context === "power" && action === "network") window.openNetworkMenu()
            else if (context === "power" && action === "bluetooth") window.openBluetoothMenu()
            else if (context === "audio") {
                System.act(action)
                if (menuPanel.visible) menuPanel.updateEntries(System.audioEntries())
            }
            else if (context === "network" || context === "bluetooth") System.act(action)
            else if (action === "pair") Launcher.pairController()
            else if (context === "power") Launcher.powerAction(action)
            else if (context === "game") window.runGameAction(action)
            else window.runTileAction(action)
        }
        onClosed: window.restoreFocus()
    }

    // ---- questions that need typing ------------------------------------------
    // The administrator password — only an install whose account has one asks,
    // and only for what changes the system itself: removing or updating a
    // pacman app. And a Wi-Fi network's password, the first time it is joined.
    // With a controller, each shows the on-screen keyboard; see PromptPanel.
    PromptPanel {
        id: passwordPrompt
        open: Launcher.passwordWanted
        title: qsTr("Enter your password")
        reason: Launcher.passwordReason
        errorText: Launcher.passwordError
        checking: Launcher.passwordChecking
        useKeyboard: Launcher.usingController
        buttons: Launcher.buttonNames
        onAccepted: function (text) { Launcher.submitPassword(text) }
        onCancelled: Launcher.cancelPassword()
        onOpenChanged: if (!open) window.restoreFocus()
    }

    PromptPanel {
        id: wifiPrompt
        open: System.wifiPasswordFor !== ""
        title: qsTr("Wi-Fi password")
        reason: qsTr("%1 is secured. Enter its password to join it.").arg(System.wifiPasswordFor)
        continueLabel: qsTr("Join")
        useKeyboard: Launcher.usingController
        buttons: Launcher.buttonNames
        onAccepted: function (text) { System.joinWifi(System.wifiPasswordFor, text) }
        onCancelled: System.cancelWifiPassword()
        onOpenChanged: if (!open) window.restoreFocus()
    }

    function promptOpen() {
        return passwordPrompt.open || wifiPrompt.open
    }

    // Focus back where it belongs: an open prompt if there is one, then a
    // game's page if one is open (the grid is only behind it; with focus
    // there, the page's Play and Back stopped answering), then the grid.
    function restoreFocus() {
        if (passwordPrompt.open) passwordPrompt.takeFocus()
        else if (wifiPrompt.open) wifiPrompt.takeFocus()
        else if (menuPanel.visible) menuPanel.takeFocus()
        else if (detailLoader.item) detailLoader.item.forceActiveFocus()
        else if (currentTab === 0) focusGames()
        else appsGrid.forceActiveFocus()
    }

    // ---- keys -------------------------------------------------------------
    // Tab is handled on the grids themselves, not here. Qt Quick's focus
    // traversal consumes Tab before a Shortcut ever sees it, so a Shortcut
    // bound to it simply never fires — which is what happened.
    //
    // F5 is not a navigation key, so a Shortcut works and covers the case
    // where focus has gone somewhere unexpected.
    // The mark is clickable, but a console has to be usable with no pointer at
    // all, so the system menu gets a key of its own. Not a navigation key, so a
    // Shortcut works and reaches it from either tab.
    Shortcut {
        sequence: "F10"
        onActivated: if (!window.promptOpen()) window.openPowerMenu()
    }

    Shortcut {
        sequence: "F5"
        onActivated: {
            Launcher.refresh()
            AppLibrary.refresh()
        }
    }

    // Into the "Continue playing" row, on the game played last; false when
    // there is no row. The grid's place is kept for coming back down.
    function enterRecent() {
        var list = gamesGrid.headerItem ? gamesGrid.headerItem.list : null
        if (!list || RecentGames.count === 0) return false
        gamesGrid.positionViewAtBeginning()
        list.currentIndex = 0
        list.positionViewAtBeginning()
        recentActive = true
        focusGames()
        return true
    }

    function leaveRecent() {
        recentActive = false
        focusGames()
    }

    // A tile in the row picked with the pointer.
    function focusRecent(index) {
        var list = gamesGrid.headerItem ? gamesGrid.headerItem.list : null
        if (!list) return
        list.currentIndex = index
        recentActive = true
        focusGames()
    }

    // The Games tab's focus: the row while recentActive, else the grid. The
    // row is inside the grid, a focus scope, so the grid given the focus
    // hands it on to the row while the row still holds the scope's.
    function focusGames() {
        var list = gamesGrid.headerItem ? gamesGrid.headerItem.list : null
        if (recentActive && RecentGames.count > 0 && list) {
            list.forceActiveFocus()
            return
        }
        recentActive = false
        if (list) list.focus = false
        gamesGrid.forceActiveFocus()
    }

    function selectTab(index) {
        currentTab = index
        // Focus follows the tab, otherwise the arrow keys keep driving the
        // grid that is no longer on screen. The Games tab opens on its grid.
        if (index === 0) leaveRecent()
        else appsGrid.forceActiveFocus()
    }

    function openCurrentApp() {
        if (AppLibrary.count === 0) return
        Launcher.launchApp(AppLibrary.get(appsGrid.currentIndex).appId)
    }

    function currentEntry() {
        if (currentTab !== 1 || AppLibrary.count === 0) return null
        if (appsGrid.currentIndex < 0) return null
        return AppLibrary.get(appsGrid.currentIndex)
    }

    // What a tile's menu offers.
    //
    // An entry that does not apply is left out rather than greyed. A disabled
    // "Install" on something already installed is noise: it describes a state
    // you can see from the tile.
    //
    // The one exception is uninstalling a system app, which stays visible and
    // disabled with its reason. That is a rule, not a state — dropping it
    // silently would leave someone wondering whether the tile was special or
    // the menu was broken.
    function appMenuEntries(removable, installed) {
        var entries = []
        if (installed) {
            entries.push({ action: "open",   label: qsTr("Open"),             enabled: true })
            entries.push({ action: "check",  label: qsTr("Check for update"), enabled: true })
            entries.push({ action: "update", label: qsTr("Update"),           enabled: true })
        }
        if (removable)
            entries.push({ action: "uninstall", label: qsTr("Uninstall"), enabled: installed })
        else
            entries.push({ action: "uninstall", label: qsTr("Uninstall  ·  system app"),
                           enabled: false })
        return entries
    }

    // The keyboard route to a tile's menu, anchored to the focused tile so the
    // panel lands in the same place it would have from a click.
    function openTileMenu() {
        var entry = currentEntry()
        if (!entry || !appsGrid.currentItem) return
        menuPanel.openFor(appsGrid.currentItem, entry.title,
                          appMenuEntries(entry.removable, entry.playable),
                          "app", entry.appId)
    }

    function openAudioMenu() {
        menuPanel.openFor(menuAnchor, qsTr("SOUND"), System.audioEntries(), "audio")
    }

    function openNetworkMenu() {
        menuPanel.openFor(menuAnchor, qsTr("NETWORK"),
                          System.networkEntries(), "network")
    }

    function openBluetoothMenu() {
        menuPanel.openFor(menuAnchor, qsTr("BLUETOOTH"),
                          System.bluetoothEntries(), "bluetooth")
    }

    function openPowerMenu() {
        // While something runs, the way back into it and the way out of it
        // come first: they are why the menu was opened.
        // Updating is for an installed system only; the live image runs from
        // RAM and starts afresh every boot.
        var updates = Launcher.liveImage ? [] : Launcher.restartRequired ? [
            { action: "reboot", label: qsTr("Restart to finish updating"), enabled: true }
        ] : Launcher.updateCount > 0 ? [
            { action: "sysupdate", label: qsTr("Update system  ·  %1").arg(window.updatesText(Launcher.updateCount)),
              enabled: !Launcher.packageBusy }
        ] : [
            { action: "syscheck", label: qsTr("Check for updates"), enabled: true }
        ]
        var running = Launcher.gameRunning ? [
            { action: "resume", label: qsTr("Resume %1").arg(Launcher.runningTitle), enabled: true },
            { action: "quit",   label: qsTr("Quit %1").arg(Launcher.runningTitle),   enabled: true }
        ] : []
        // Notifications that stay until closed, which Plasma closes only
        // with a pointer; see NotificationWatcher.h.
        var notes = Launcher.standingNotifications > 0 ? [
            { action: "clearnotes",
              label: qsTr("Clear notifications  ·  %1").arg(Launcher.standingNotifications), enabled: true }
        ] : []
        menuPanel.openFor(menuAnchor, qsTr("OMNIOS %1").arg(Launcher.version), running.concat(notes, updates, [
            // Plasma's panel has these a pointer away, and a console often has
            // no pointer. Everything they offer is reachable from here too, so
            // the keyboard and the controller are not second-class.
            //
            // The benign entries come first, so the menu opens on one of them
            // rather than one press from ending the session.
            { action: "sound",     label: qsTr("Sound"),     enabled: true },
            { action: "network",   label: qsTr("Network"),   enabled: true },
            { action: "bluetooth", label: qsTr("Bluetooth"), enabled: true },
            { action: "pair",     label: qsTr("Pair a controller"), enabled: true },
            { action: "desktop",   label: qsTr("Switch to desktop"), enabled: true },
            // Only on the live image; see LauncherController::liveImage.
            { action: "install",   label: qsTr("Install OmniOS"), enabled: true, live: true },
            { action: "suspend",  label: qsTr("Sleep"),     enabled: true },
            { action: "reboot",   label: qsTr("Restart"),   enabled: true },
            { action: "poweroff", label: qsTr("Shut down"), enabled: true }
        ]).filter(function (entry) { return entry.live !== true || Launcher.liveImage }), "power")
    }

    function runTileAction(action) {
        var id = menuPanel.subject
        if (action === "open")           Launcher.launchApp(id)
        else if (action === "check")     Launcher.checkForUpdate(id)
        else if (action === "update")    Launcher.updateApp(id)
        else if (action === "uninstall") Launcher.removeApp(id)
    }

    // A game tile's three dots, scrolled into view: where its menu opens.
    // Null for a row that is not a game.
    function tileMenuAnchor(index) {
        if (index < 0) return null
        gamesGrid.positionViewAtIndex(index, GridView.Contain)
        gamesGrid.forceLayout()
        var cell = gamesGrid.itemAtIndex(index)
        return cell ? cell.menuAnchor : null
    }

    // The running game's menu, for Meta in Game Mode (Launcher.menuWanted):
    // back into it, out of it, or on to the library or the system menu. A
    // Steam game adds its page in Steam. Resume comes first, so Meta and then
    // Enter is back in the game. It opens where the game's own three dots open
    // theirs, on the game's tile (the Games tab, scrolled to it and focused);
    // at the corner for something that has no tile, such as an app.
    function openRunningGameMenu() {
        var isSteam = Launcher.runningGameId.indexOf("steam.") === 0
        var index = GameLibrary.indexOfId(Launcher.runningGameId)
        var anchor = null
        if (index >= 0) {
            detailLoader.active = false  // the tile is under a page left open
            if (window.currentTab !== 0) window.selectTab(0)
            window.recentActive = false  // its tile in the grid, where the menu opens
            gamesGrid.currentIndex = index
            anchor = window.tileMenuAnchor(index)
        }
        menuPanel.openFor(anchor || menuAnchor, Launcher.runningTitle.toUpperCase(), [
            { action: "resume", label: qsTr("Resume"), enabled: true },
            { action: "quit",   label: qsTr("Quit game"), enabled: true }
        ].concat(isSteam ? [{ action: "details", label: qsTr("Open in Steam"), enabled: true }] : [], [
            { action: "library", label: qsTr("Library"), enabled: true },
            { action: "system",  label: qsTr("System menu"), enabled: true }
        ]), "running")
    }

    // Meta: the menu for what is running, or the system menu; pressed again
    // while it is open, it closes.
    Connections {
        target: Launcher
        function onMenuWanted() {
            if (menuPanel.visible) menuPanel.close()
            else if (Launcher.gameRunning) window.openRunningGameMenu()
            else window.openPowerMenu()
        }
    }

    // A game's menu, from its three dots (or M, or the pad's left face
    // button): play it or, while it runs, resume or quit it; its page; and
    // what only Steam can do for a Steam game, or another look for cover art
    // for one that has none.
    function openGameMenu(anchorItem) {
        var game = currentGame
        if (!game || !anchorItem) return
        var isRunning = Launcher.runningGameId === game.gameId
        var first = isRunning
            ? [{ action: "resume", label: qsTr("Resume"),    enabled: true },
               { action: "quit",   label: qsTr("Quit game"), enabled: true }]
            : [{ action: "play",   label: qsTr("Play"),      enabled: game.playable }]
        var rest = [{ action: "page", label: qsTr("Details"), enabled: true }]
        if (game.platformId === "steam") {
            rest = rest.concat([
                { action: "details",   label: qsTr("Open in Steam"),      enabled: true },
                { action: "validate",  label: qsTr("Verify game files"),  enabled: true },
                { action: "uninstall", label: qsTr("Uninstall"),          enabled: true }
            ])
        } else {
            if (game.cover === "" && Launcher.coverFindable(game.gameId))
                rest.push({ action: "cover", label: qsTr("Look for cover art"), enabled: true })
            rest.push({ action: "remove", label: qsTr("Uninstall"), enabled: true })
        }
        menuPanel.openFor(anchorItem, game.title, first.concat(rest), "game", game.gameId)
    }

    // No translations ship yet, so plural forms are done here rather than
    // with qsTr's %n, which without one prints "update(s)".
    function updatesText(n) {
        return n === 1 ? qsTr("1 update") : qsTr("%1 updates").arg(n)
    }

    // An update is minutes of downloading and the whole system changing, so it
    // is asked once, saying so, with "Not now" under the cursor.
    function confirmUpdate() {
        menuPanel.openFor(menuAnchor, qsTr("UPDATE THE SYSTEM?"), [
            { action: "later", label: qsTr("Not now"), enabled: true },
            { action: "go",    label: qsTr("Update now  ·  %1, may take a while").arg(window.updatesText(Launcher.updateCount)),
              enabled: true }
        ], "sysupdate")
    }

    // Uninstalling takes the game's files away (to the Trash), so it is asked
    // once more, beside the same tile, with "Keep it" first: the safe answer is
    // the one under the cursor. Quitting is not asked: a game's menu is
    // opened on purpose, and Quit says what it does.
    function confirmUninstall(id, anchorItem) {
        var index = GameLibrary.indexOfId(id)
        var game = index >= 0 ? GameLibrary.get(index) : null
        if (!game) return
        menuPanel.openFor(anchorItem || menuAnchor, qsTr("UNINSTALL %1?").arg(game.title.toUpperCase()), [
            { action: "keep",                label: qsTr("Keep it"),                              enabled: true },
            { action: "uninstall-confirmed", label: qsTr("Uninstall  ·  moves its files to the Trash"), enabled: true }
        ], "uninstall", id)
    }

    function runGameAction(action) {
        var id = menuPanel.subject
        if (action === "remove") {
            window.confirmUninstall(id, window.tileMenuAnchor(GameLibrary.indexOfId(id)))
            return
        }
        if (action === "play") Launcher.launch(id)
        else if (action === "resume") Launcher.resumeRunningGame()
        else if (action === "page") window.openDetail()
        else if (action === "cover") Launcher.findCover(id)
        else Launcher.steamAction(id, action)
    }

    function openDetail() {
        if (!window.currentGame) return
        detailLoader.active = true
        // Already open, onLoaded does not run again.
        if (detailLoader.item) detailLoader.item.forceActiveFocus()
    }

    // A controller press is about to arrive. Qt only routes a key to the item
    // holding active focus, and after an app exits — or after the session has
    // been away on another virtual terminal — there is no such item, so the
    // press would land nowhere at all.
    //
    // Only when nothing has it: a menu that is open holds focus on purpose, and
    // stealing it back to the grid would make the controller unable to answer
    // its own menu.
    Connections {
        target: Launcher
        function onFocusWanted() {
            if (window.activeFocusItem) return
            window.restoreFocus()
        }
    }

    // Re-assert focus whenever the window becomes active. Returning from an
    // app leaves Qt's focus item unset, and without this the grid is visible
    // but deaf until something is clicked.
    onActiveChanged: if (active) restoreFocus()

    Component.onCompleted: gamesGrid.forceActiveFocus()
}

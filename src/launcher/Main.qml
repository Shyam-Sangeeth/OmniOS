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
    readonly property var tabs: [gamesGrid, appsGrid]
    readonly property var activeGrid: tabs[currentTab]

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

    property var currentGame: gamesGrid.currentIndex >= 0 && GameLibrary.count > 0
                              ? GameLibrary.get(gamesGrid.currentIndex)
                              : null

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
            text: Launcher.gameRunning
                  ? qsTr("%1  library    ·    %2 is running").arg(Launcher.usingController ? b.guide : qsTr("Guide button"))
                                                               .arg(Launcher.runningTitle)
                  : Launcher.usingController
                    ? (window.currentTab === 0
                       ? qsTr("%1 open    %2 back    %3 %4 switch tab    %5 menu    %6 rescan")
                             .arg(b.south).arg(b.east).arg(b.l1).arg(b.r1).arg(b.start).arg(b.north)
                       : qsTr("%1 open    %2 back    %3 app menu    %4 %5 switch tab    %6 menu")
                             .arg(b.south).arg(b.east).arg(b.west).arg(b.l1).arg(b.r1).arg(b.start))
                    : window.currentTab === 0
                      ? qsTr("↑↓←→ move    Enter open    Tab switch tab    F10 menu    F5 rescan")
                      : qsTr("↑↓←→ move    Enter open    M app menu    Tab switch tab    F10 menu")
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
            if (context === "power" && action === "sound") window.openAudioMenu()
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
            else window.runTileAction(action)
        }
        onClosed: window.activeGrid.forceActiveFocus()
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

    // Focus back where it belongs: an open prompt if there is one, the grid if not.
    function restoreFocus() {
        if (passwordPrompt.open) passwordPrompt.takeFocus()
        else if (wifiPrompt.open) wifiPrompt.takeFocus()
        else activeGrid.forceActiveFocus()
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
        menuPanel.openFor(menuAnchor, qsTr("OMNIOS %1").arg(Launcher.version), [
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
        ].filter(function (entry) { return entry.live !== true || Launcher.liveImage }), "power")
    }

    function runTileAction(action) {
        var id = menuPanel.subject
        if (action === "open")           Launcher.launchApp(id)
        else if (action === "check")     Launcher.checkForUpdate(id)
        else if (action === "update")    Launcher.updateApp(id)
        else if (action === "uninstall") Launcher.removeApp(id)
    }

    function openDetail() {
        if (GameLibrary.count === 0) return
        detailLoader.active = true
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

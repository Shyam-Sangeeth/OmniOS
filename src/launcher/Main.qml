// The OmniOS shell (OmniOS.md §12).
//
// Boots straight into tiles. One thing in focus at a time, the background
// tinted by the focused game, and no desktop anywhere behind it.
import QtQuick
import QtQuick.Window
import omnios

Window {
    id: window
    visible: true
    visibility: Window.FullScreen
    title: "OmniOS"
    color: Theme.background

    // The focused game drives the hero panel and the background tint.
    property var current: grid.currentIndex >= 0 && GameLibrary.count > 0
                          ? GameLibrary.get(grid.currentIndex)
                          : null

    // Background: a wash of the focused game's platform colour (§12 calls for
    // blurred cover art; a tint costs nothing under software rendering and
    // still makes the screen respond to what is selected).
    Rectangle {
        anchors.fill: parent
        color: Theme.background
    }
    Rectangle {
        id: tint
        anchors.fill: parent
        opacity: 0.30
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: window.current ? Qt.darker(window.current.badgeColor, 3.0) : Theme.background
            }
            GradientStop { position: 0.75; color: Theme.background }
        }
        Behavior on opacity {
            NumberAnimation { duration: Theme.backgroundDuration }
        }
    }

    // ---- top bar ----------------------------------------------------------
    Item {
        id: topBar
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: 64

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

    // ---- hero panel: the focused title, large -----------------------------
    Item {
        id: hero
        anchors { top: topBar.bottom; left: parent.left; right: parent.right }
        anchors.leftMargin: Theme.gutter
        anchors.rightMargin: Theme.gutter
        height: 168
        visible: GameLibrary.count > 0

        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10

            Text {
                text: window.current ? window.current.title : ""
                color: Theme.textPrimary
                font.pixelSize: 42
            }

            Row {
                spacing: 10
                Rectangle {
                    width: heroBadge.implicitWidth + 16; height: 22; radius: 4
                    color: window.current ? window.current.badgeColor : Theme.textSecondary
                    Text {
                        id: heroBadge
                        anchors.centerIn: parent
                        text: window.current ? window.current.platformName : ""
                        color: "#FFFFFF"; font.pixelSize: 12; font.bold: true
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: window.current
                          ? window.current.engineName + "  ·  " + window.current.tierName
                            + (window.current.sizeText !== "" ? "  ·  " + window.current.sizeText : "")
                          : ""
                    color: Theme.textSecondary
                    font.pixelSize: 14
                }
            }
        }
    }

    // ---- all games --------------------------------------------------------
    Text {
        id: sectionLabel
        anchors { top: hero.bottom; left: parent.left; leftMargin: Theme.gutter }
        text: qsTr("ALL GAMES")
        color: Theme.textSecondary
        font.pixelSize: 12
        font.letterSpacing: 3
        visible: GameLibrary.count > 0
    }

    GridView {
        id: grid
        anchors {
            top: sectionLabel.bottom; topMargin: 14
            left: parent.left; leftMargin: Theme.gutter
            right: parent.right; rightMargin: Theme.gutter
            bottom: footer.top; bottomMargin: 10
        }
        clip: true
        focus: true
        cellWidth: Theme.gridWidth + 18
        cellHeight: Theme.gridHeight + 18
        model: GameLibrary
        visible: GameLibrary.count > 0
        // Keep the focused tile comfortably in view rather than snapped to an
        // edge, so there is always visible context either side.
        preferredHighlightBegin: 0
        preferredHighlightEnd: height
        highlightMoveDuration: Theme.focusDuration

        delegate: GameTile {
            title: model.title
            platformName: model.platformName
            badgeColor: model.badgeColor
            cover: model.cover
            playable: model.playable
            selected: GridView.isCurrentItem && !detailLoader.active

            MouseArea {
                anchors.fill: parent
                onClicked: { grid.currentIndex = index; window.openDetail() }
            }
        }

        Keys.onReturnPressed: window.openDetail()
        Keys.onEnterPressed: window.openDetail()
        Keys.onPressed: function (event) {
            if (event.key === Qt.Key_F5) { Launcher.refresh(); event.accepted = true }
        }
    }

    // ---- empty state ------------------------------------------------------
    Column {
        anchors.centerIn: parent
        spacing: 12
        visible: GameLibrary.count === 0 && !Launcher.scanning

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("No games yet")
            color: Theme.textPrimary
            font.pixelSize: 30
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            // Tell the user exactly where to put files rather than leaving an
            // empty screen with no next action.
            text: qsTr("Copy games into %1 and press F5").arg(Launcher.gamesPath)
            color: Theme.textSecondary
            font.pixelSize: 14
        }
    }

    // ---- footer hints -----------------------------------------------------
    Item {
        id: footer
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
        height: 44

        Text {
            anchors { left: parent.left; leftMargin: Theme.gutter; verticalCenter: parent.verticalCenter }
            text: qsTr("↑↓←→ move    Enter open    Esc back    F5 rescan")
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
            game: window.current
            onClosed: { detailLoader.active = false; grid.forceActiveFocus() }
            onPlayed: {
                if (Launcher.launch(window.current.gameId)) {
                    detailLoader.active = false
                    grid.forceActiveFocus()
                }
            }
        }
        onLoaded: item.forceActiveFocus()
    }

    function openDetail() {
        if (GameLibrary.count === 0) return
        detailLoader.active = true
    }

    // A running game owns the screen; the shell says so instead of appearing
    // frozen behind it.
    Rectangle {
        anchors.fill: parent
        visible: Launcher.gameRunning
        color: "#E60A0A12"
        Column {
            anchors.centerIn: parent
            spacing: 10
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: Launcher.runningTitle
                color: Theme.textPrimary
                font.pixelSize: 30
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Running — the library returns when it exits")
                color: Theme.textSecondary
                font.pixelSize: 14
            }
        }
    }
}

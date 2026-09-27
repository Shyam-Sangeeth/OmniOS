// OmniOS TV (omni-launcher-qml --tv): free-to-air channels from iptv-org.
//
// One row of filters along the top — country, category, language, a search,
// and favourites — and the channels they leave as tiles below. A plays the
// channel full screen (TvPlayer.qml, libmpv); while it plays the controller is
// a remote (up and down volume, LB and RB channel, B stops). Maximised on the desktop; in Game Mode
// KWin makes it full screen, as it does every window opened over the launcher.
import QtQuick
import QtQuick.Window
import omnios

Window {
    id: window

    width: 1200
    height: 760
    visible: true
    visibility: Window.Maximized
    title: qsTr("OmniOS TV")
    color: Theme.background

    readonly property var buttons: Tv.input.buttonNames
    readonly property bool pad: Tv.input.usingController

    Component.onCompleted: channelGrid.forceActiveFocus()

    Connections {
        target: Tv
        function onFocusWanted() { if (!window.activeFocusItem) channelGrid.forceActiveFocus() }
        // New filters, new list: start at the top of it.
        function onFiltersChanged() { channelGrid.currentIndex = 0; channelGrid.positionViewAtBeginning() }
        // Channel up or down on the remote: the grid follows, so B lands on it.
        function onPlayingIndexChanged(index) { channelGrid.currentIndex = index }
        // Back from a channel: on the channels again, where it was.
        // (Checked by what is open, not by where focus is: the player going
        // leaves it on the window itself, which is not nowhere.)
        function onStateChanged() {
            if (!Tv.playing && !filterPopup.isOpen && !fieldKeyboard.open && !channelGrid.activeFocus)
                channelGrid.forceActiveFocus()
        }
    }

    // ---- the row ------------------------------------------------------------------
    Item {
        id: top
        anchors { left: parent.left; right: parent.right; top: parent.top
                  leftMargin: Theme.gutter; rightMargin: Theme.gutter; topMargin: 20 }
        height: row.height
        // Above the grid, so an open list draws over the tiles.
        z: 10

        Row {
            id: row
            spacing: 16

            Item {
                width: 96
                height: countryFilter.height
                Row {
                    anchors { left: parent.left; bottom: parent.bottom; bottomMargin: 8 }
                    spacing: 10
                    OmniLogo { diameter: 28; anchors.verticalCenter: parent.verticalCenter }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("TV")
                        color: Theme.textPrimary
                        font.pixelSize: 22
                        font.letterSpacing: 2
                    }
                }
            }

            FilterDropdown {
                id: countryFilter
                label: qsTr("Country")
                options: Tv.countryOptions
                facet: 0
                width: Math.min(240, (window.width - 2 * Theme.gutter - 96 - 5 * 16 - 60) / 4)
                onOpenRequested: typed => filterPopup.openFor(countryFilter, typed)
                KeyNavigation.right: categoryFilter
                KeyNavigation.down: channelGrid
            }
            FilterDropdown {
                id: categoryFilter
                label: qsTr("Category")
                options: Tv.categoryOptions
                facet: 1
                width: countryFilter.width
                onOpenRequested: typed => filterPopup.openFor(categoryFilter, typed)
                KeyNavigation.left: countryFilter
                KeyNavigation.right: languageFilter
                KeyNavigation.down: channelGrid
            }
            FilterDropdown {
                id: languageFilter
                label: qsTr("Language")
                options: Tv.languageOptions
                facet: 2
                width: countryFilter.width
                onOpenRequested: typed => filterPopup.openFor(languageFilter, typed)
                KeyNavigation.left: categoryFilter
                KeyNavigation.right: searchField.input
                KeyNavigation.down: channelGrid
            }
            Field {
                id: searchField
                keyboard: fieldKeyboard
                label: qsTr("Search")
                width: countryFilter.width
                onEdited: Tv.setSearch(text)
                // Typed with the on-screen keyboard: counted when it closes.
                onSubmitted: { Tv.setSearch(text); channelGrid.forceActiveFocus() }
                downTo: channelGrid
                tabTo: favoriteToggle
                Keys.onLeftPressed: event => {
                    if (input.cursorPosition === 0) languageFilter.forceActiveFocus()
                    else event.accepted = false
                }
                Keys.onRightPressed: event => {
                    if (input.cursorPosition === text.length) favoriteToggle.forceActiveFocus()
                    else event.accepted = false
                }
            }
            // Favourites only, or everything.
            Rectangle {
                id: favoriteToggle
                anchors.bottom: parent.bottom
                width: 60
                height: 44
                radius: 8
                activeFocusOnTab: true
                color: Tv.favoritesOnly ? Theme.accent : Theme.card
                border.width: activeFocus ? 2 : 1
                border.color: activeFocus ? Theme.focusBorder : "#26FFFFFF"
                Text {
                    anchors.centerIn: parent
                    text: "★"
                    color: Tv.favoritesOnly ? "#FFFFFF" : Theme.textSecondary
                    font.pixelSize: 20
                }
                MouseArea { anchors.fill: parent; onClicked: Tv.setFavoritesOnly(!Tv.favoritesOnly) }
                Keys.onReturnPressed: Tv.setFavoritesOnly(!Tv.favoritesOnly)
                Keys.onEnterPressed: Tv.setFavoritesOnly(!Tv.favoritesOnly)
                Keys.onSpacePressed: Tv.setFavoritesOnly(!Tv.favoritesOnly)
                KeyNavigation.left: searchField.input
                KeyNavigation.down: channelGrid
            }
        }
    }

    // What the filters leave, or what went wrong.
    Text {
        id: status
        anchors { left: parent.left; right: parent.right; top: top.bottom
                  leftMargin: Theme.gutter; rightMargin: Theme.gutter; topMargin: 14 }
        elide: Text.ElideRight
        text: Tv.playing ? qsTr("Playing %1").arg(Tv.playingName)
              : Tv.loading ? qsTr("Loading the channel list ...")
              : Tv.message !== "" ? Tv.message
              : Tv.channels.length === 1 ? qsTr("1 channel")
              : qsTr("%1 channels").arg(Tv.channels.length)
        color: Tv.message !== "" && !Tv.playing && !Tv.loading ? "#E4A000" : Theme.textSecondary
        font.pixelSize: 14
    }

    // ---- channels -------------------------------------------------------------------
    GridView {
        id: channelGrid
        anchors { left: parent.left; right: parent.right; top: status.bottom; bottom: footer.top
                  leftMargin: Theme.gutter - 8; rightMargin: Theme.gutter - 8; topMargin: 12; bottomMargin: 8 }
        clip: true
        cellWidth: Math.floor(width / Math.max(1, Math.floor(width / 200)))
        cellHeight: 188
        model: Tv.channels
        highlightMoveDuration: 0
        keyNavigationEnabled: true

        readonly property int columns: Math.max(1, Math.floor(width / cellWidth))

        delegate: Item {
            id: cell
            required property var modelData
            required property int index
            readonly property bool current: GridView.isCurrentItem && channelGrid.activeFocus
            // What is on, from the guide; empty where there is none.
            readonly property var guide: Tv.guideRevision >= 0 ? Tv.guideFor(modelData.url) : ({})
            width: channelGrid.cellWidth
            height: channelGrid.cellHeight

            Rectangle {
                anchors { fill: parent; margins: 8 }
                radius: 8
                color: Theme.card
                border.width: cell.current ? 2 : 0
                border.color: Theme.focusBorder
                scale: cell.current ? 1.04 : 1.0
                Behavior on scale { NumberAnimation { duration: Theme.focusDuration } }

                // The logo, or the name's first letter where there is none or
                // it will not load.
                Rectangle {
                    id: logoBox
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                    height: 86
                    radius: 6
                    color: "#14FFFFFF"
                    Image {
                        id: logo
                        anchors { fill: parent; margins: 8 }
                        source: cell.modelData.logo || ""
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: true
                        sourceSize.height: 140
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: logo.status !== Image.Ready
                        text: cell.modelData.name.charAt(0).toUpperCase()
                        color: Theme.textSecondary
                        font.pixelSize: 36
                    }
                    // How far into the programme on now.
                    Rectangle {
                        anchors { left: parent.left; bottom: parent.bottom }
                        visible: cell.guide.now !== undefined
                        width: parent.width * Math.max(0, Math.min(1, cell.guide.progress || 0))
                        height: 3
                        color: Theme.accent
                    }
                }
                Text {
                    id: channelName
                    anchors { left: parent.left; right: parent.right; top: logoBox.bottom
                              margins: 10; topMargin: 8 }
                    text: (cell.modelData.favorite ? "★ " : "") + cell.modelData.name
                    color: Theme.textPrimary
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    // One line when there is a programme to show under it.
                    maximumLineCount: cell.guide.now !== undefined ? 1 : 2
                    elide: Text.ElideRight
                }
                Text {
                    anchors { left: parent.left; right: parent.right; top: channelName.bottom
                              leftMargin: 10; rightMargin: 10; topMargin: 3 }
                    visible: cell.guide.now !== undefined
                    text: cell.guide.now || ""
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
            MouseArea {
                anchors.fill: parent
                onClicked: {
                    channelGrid.currentIndex = cell.index
                    channelGrid.forceActiveFocus()
                    Tv.play(cell.index)
                }
            }
        }

        Keys.onReturnPressed: if (currentIndex >= 0) Tv.play(currentIndex)
        Keys.onEnterPressed: if (currentIndex >= 0) Tv.play(currentIndex)
        // B on the channels leaves TV, as it leaves any app.
        Keys.onEscapePressed: Qt.quit()
        Keys.onUpPressed: event => {
            if (currentIndex < columns) countryFilter.forceActiveFocus()
            else event.accepted = false
        }
        Keys.onPressed: event => {
            // Y on a pad, which arrives as F5, as it does on a keyboard. Not a
            // letter: letters search.
            if (event.key === Qt.Key_F5) {
                if (currentIndex >= 0) Tv.toggleFavorite(currentIndex)
                event.accepted = true
            } else if (event.text.length === 1 && event.text.trim() !== ""
                       && !(event.modifiers & Qt.ControlModifier)) {
                searchField.input.forceActiveFocus()
                searchField.text += event.text
                Tv.setSearch(searchField.text)
                event.accepted = true
            }
        }
    }

    Text {
        anchors.centerIn: channelGrid
        visible: channelGrid.count === 0 && !Tv.loading
        text: Tv.favoritesOnly ? Theme.hint(qsTr("No favourites here yet  -  %1 on a channel adds it")
                                     .arg(window.pad ? window.buttons.north : "[F5]"))
                               : qsTr("No channels match these filters")
        color: Theme.textSecondary
        font.pixelSize: 15
    }

    // ---- hints -----------------------------------------------------------------------
    Text {
        id: footer
        anchors { left: parent.left; leftMargin: Theme.gutter; bottom: parent.bottom; bottomMargin: 14 }
        height: 22
        text: Theme.hint(window.pad
              ? qsTr("%1 Watch    %2 Favourite    %3 Back    ·    While watching:  %4 %5 Channel    ▲ ▼ Volume    %3 Stop")
                    .arg(window.buttons.south).arg(window.buttons.north).arg(window.buttons.east)
                    .arg(window.buttons.l1).arg(window.buttons.r1)
              : qsTr("[Enter] Watch    [F5] Favourite    [Esc] Back    [↑] Filters    ·    While watching:  [PgUp] [PgDn] Channel    [↑] [↓] Volume    [Esc] Stop"))
        color: Theme.textSecondary
        font.pixelSize: 12
    }

    // The channel playing, over everything, in the window itself.
    TvPlayer {
        id: tvPlayer
        z: 30
        buttons: window.buttons
        pad: window.pad
    }

    // The open filter's list, over everything.
    FilterPopup {
        id: filterPopup
        z: 15
        dropdowns: [countryFilter, categoryFilter, languageFilter]
        keyboard: fieldKeyboard
        controller: window.pad
        onToggled: (facet, id) => Tv.toggleFilter(facet, id)
    }

    // Typing with a controller: the search boxes open this on A.
    FieldKeyboard {
        id: fieldKeyboard
        inputMode: Tv.input
        z: 20
    }
}

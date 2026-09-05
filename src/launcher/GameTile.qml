// One game tile. Cover art when there is any, otherwise a generated card that
// still reads as a specific game rather than a missing-image placeholder —
// most titles will have no artwork until Phase 7.4 fetches it.
import QtQuick
import omnios

Item {
    id: tile

    property string title: ""
    property string platformName: ""
    property color  badgeColor: Theme.textSecondary
    property string cover: ""
    // App icon from the system theme. Used only when there is no cover art —
    // a game with artwork should show the artwork.
    property string iconSource: ""
    property bool   selected: false
    property bool   playable: true
    property int    baseWidth: Theme.gridWidth
    property int    baseHeight: Theme.gridHeight

    width: baseWidth
    height: baseHeight

    // Focus scales the tile rather than moving it (§12: 1.0 -> 1.06, 150ms).
    scale: selected ? 1.06 : 1.0
    z: selected ? 2 : 1
    Behavior on scale {
        NumberAnimation { duration: Theme.focusDuration; easing.type: Easing.OutCubic }
    }

    Rectangle {
        id: card
        anchors.fill: parent
        radius: 6
        color: Theme.card
        clip: true

        // Fallback art: a wash of the platform's own badge colour. Distinct per
        // platform, so a grid with no artwork is still readable at a glance.
        Rectangle {
            anchors.fill: parent
            visible: tile.cover === ""
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.darker(tile.badgeColor, 2.4) }
                GradientStop { position: 1.0; color: Theme.card }
            }
        }

        Image {
            anchors.fill: parent
            source: tile.cover
            visible: tile.cover !== ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
        }

        // The app's own icon, centred in the upper part of the tile so it does
        // not collide with the title and badge along the bottom.
        Image {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: (parent.height * 0.5) - (height / 2)
            width: 64
            height: 64
            source: tile.iconSource
            visible: tile.cover === "" && tile.iconSource !== ""
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            // Ask for the icon at the size it is drawn: scaling a 256px PNG
            // down every frame is wasted work under software rendering.
            sourceSize.width: 128
            sourceSize.height: 128
            smooth: true
        }

        // Legibility scrim so the title survives a bright cover.
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: parent.height * 0.52
            gradient: Gradient {
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 1.0; color: "#E60A0A12" }
            }
        }

        Text {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 10
            anchors.bottomMargin: 26
            text: tile.title
            color: Theme.textPrimary
            font.pixelSize: 14
            elide: Text.ElideRight
            maximumLineCount: 2
            wrapMode: Text.WordWrap
        }

        // Platform badge, colour-coded per §12.
        Rectangle {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: 10
            width: badgeText.implicitWidth + 12
            height: 16
            radius: 3
            color: tile.badgeColor
            Text {
                id: badgeText
                anchors.centerIn: parent
                text: tile.platformName
                color: "#FFFFFF"
                font.pixelSize: 10
                font.bold: true
            }
        }

        // A title whose engine is missing is dimmed rather than hidden: it is
        // installed, it just cannot start yet, and the detail screen says why.
        Rectangle {
            anchors.fill: parent
            visible: !tile.playable
            color: "#990A0A12"
        }
    }

    // Focus ring (§12: white glow).
    Rectangle {
        anchors.fill: card
        anchors.margins: -2
        radius: 8
        color: "transparent"
        border.width: 2
        border.color: Theme.focusBorder
        opacity: tile.selected ? 1.0 : 0.0
        Behavior on opacity {
            NumberAnimation { duration: Theme.focusDuration }
        }
    }
}

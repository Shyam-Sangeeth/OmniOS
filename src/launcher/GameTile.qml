// One tile — a game with cover art, or an app with a themed icon.
//
// The tile is split into an artwork region and a label strip. Keeping them
// separate is what stops the two cases interfering: the earlier version drew
// a legibility scrim over every tile and centred the icon in the whole tile,
// so an app icon was both dimmed by a gradient meant for photographs and
// half-hidden behind its own title.
import QtQuick
import omnios

Item {
    id: tile

    property string title: ""
    property string platformName: ""
    property color  badgeColor: Theme.textSecondary
    property string cover: ""
    // App icon from the system theme. Used only when there is no cover art.
    property string iconSource: ""
    property bool   selected: false
    property bool   playable: true
    // Shows the overflow button. Games have nothing to put in a menu yet, so
    // they leave it off rather than opening an empty one.
    property bool   hasMenu: false
    // Emitted with the button itself, so the menu can anchor to it.
    signal menuRequested(var anchorItem)
    // What the tile's menu opens beside, however it was asked for: the three
    // dots, M, or Meta while this game runs.
    readonly property Item menuAnchor: menuButton

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

        // ---- artwork region ------------------------------------------------
        Item {
            id: art
            anchors { left: parent.left; right: parent.right; top: parent.top }
            anchors.bottom: label.top

            // Colour wash, from the platform or app badge colour. Gives every
            // tile an identity even with no artwork and no icon.
            Rectangle {
                anchors.fill: parent
                visible: tile.cover === ""
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.darker(tile.badgeColor, 2.0) }
                    GradientStop { position: 1.0; color: Qt.darker(tile.badgeColor, 3.6) }
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

            // Scrim only over cover art. A photograph needs it so the title
            // stays readable; a flat wash does not, and dimming an icon with
            // it is what made the app tiles look washed out.
            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: parent.height * 0.45
                visible: tile.cover !== ""
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 1.0; color: "#CC0A0A12" }
                }
            }

            // Centred in the artwork region, which is above the label strip
            // rather than the middle of the whole tile.
            Image {
                anchors.centerIn: parent
                width: Math.min(72, parent.height * 0.62)
                height: width
                source: tile.iconSource
                visible: tile.cover === "" && tile.iconSource !== ""
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                // Request it near the drawn size: rescaling a 256px PNG every
                // frame is wasted work under software rendering.
                sourceSize.width: 144
                sourceSize.height: 144
                smooth: true
            }
        }

        // ---- label strip ---------------------------------------------------
        Item {
            id: label
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 46

            Rectangle {
                anchors.fill: parent
                color: "#E60A0A12"
            }

            Text {
                anchors {
                    left: parent.left; right: parent.right
                    top: parent.top; topMargin: 5
                    leftMargin: 8; rightMargin: 8
                }
                text: tile.title
                color: Theme.textPrimary
                font.pixelSize: 13
                elide: Text.ElideRight
            }

            Rectangle {
                anchors { left: parent.left; leftMargin: 8; bottom: parent.bottom; bottomMargin: 7 }
                width: badgeText.implicitWidth + 12
                height: 15
                radius: 3
                color: tile.badgeColor
                Text {
                    id: badgeText
                    anchors.centerIn: parent
                    text: tile.platformName
                    color: "#FFFFFF"
                    font.pixelSize: 9
                    font.bold: true
                }
            }
        }

        // Installed but unable to start: dimmed rather than hidden, with the
        // reason on the detail screen.
        Rectangle {
            anchors.fill: parent
            visible: !tile.playable
            color: "#990A0A12"
        }

        // ---- overflow button -----------------------------------------------
        // Bottom-right of the card, over the label strip. The badge is
        // left-aligned, so the two never meet. Inside the clipping card on
        // purpose: it should ride the tile's corner radius, not float past it.
        Rectangle {
            id: menuButton
            anchors { right: parent.right; bottom: parent.bottom; margins: 6 }
            width: 24
            height: 24
            radius: 4
            visible: tile.hasMenu
            // White tint rather than a solid fill: the tile underneath still
            // reads through it, which keeps it from looking bolted on.
            color: menuHover.containsMouse ? "#3DFFFFFF" : "#1FFFFFFF"
            border.width: 1
            border.color: "#26FFFFFF"
            Behavior on color { ColorAnimation { duration: Theme.focusDuration } }

            Row {  // three dots side by side: "more"
                anchors.centerIn: parent
                spacing: 3
                Repeater {
                    model: 3
                    Rectangle {
                        width: 3
                        height: 3
                        radius: 1.5
                        color: Theme.textPrimary
                    }
                }
            }

            MouseArea {
                id: menuHover
                anchors.fill: parent
                hoverEnabled: true
                onClicked: tile.menuRequested(menuButton)
            }
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

// The running game's save slots, to save into or load from: each with the
// picture RetroArch keeps of the game as it was saved, and when. Opened from
// the game menu's Save state and Load state (Launcher.stateSlots).
//
// Saving lands on the first empty slot, so nothing is saved over by a press
// of A; loading lands on the newest save. An empty slot cannot be loaded.
import QtQuick
import omnios

Item {
    id: slotsPanel

    // "save" or "load", and the game's title, set by openFor().
    property string mode: "save"
    property string title: ""
    property var slots: []
    // A controller's button names, for the hints.
    property var buttons: ({ south: "A", east: "B" })
    property bool usingController: false
    // The shape of the console's screen. The picture is the emulator's frame
    // as it made it, whose pixels need not be square: Nestopia's is 602x224
    // for the NES's 4:3, so it is stretched to this rather than shown as is.
    property real pictureAspect: 4 / 3

    signal chosen(string mode, int slot)
    signal closed()

    visible: false
    z: 100

    function openFor(slotMode, gameTitle, slotList) {
        mode = slotMode
        title = gameTitle
        slots = slotList
        var start = 0
        if (mode === "save") {
            start = -1
            for (var i = 0; i < slots.length && start < 0; ++i) if (!slots[i].used) start = i
            if (start < 0) start = 0
        } else {
            var newest = -1, newestTime = -1
            for (var j = 0; j < slots.length; ++j) {
                if (slots[j].used && slots[j].time > newestTime) { newestTime = slots[j].time; newest = j }
            }
            start = Math.max(0, newest)
        }
        grid.currentIndex = start
        visible = true
        grid.forceActiveFocus()
    }

    function takeFocus() {
        grid.forceActiveFocus()
    }

    function close() {
        visible = false
        closed()
    }

    function choose(index) {
        var slot = slots[index]
        if (!slot || (mode === "load" && !slot.used)) return
        visible = false
        chosen(mode, slot.slot)
        closed()
    }

    // Dims what is behind, and a click outside the panel closes it.
    Rectangle {
        anchors.fill: parent
        color: "#B30A0A12"
        MouseArea { anchors.fill: parent; onClicked: slotsPanel.close() }
    }

    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: grid.width + 40
        height: heading.height + grid.height + hint.height + 64
        radius: 10
        color: "#F21A1826"
        border.width: 1
        border.color: "#26FFFFFF"
        MouseArea { anchors.fill: parent }  // clicks on the panel stay here

        Text {
            id: heading
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 20 }
            text: (slotsPanel.mode === "save" ? qsTr("SAVE STATE") : qsTr("LOAD STATE"))
                  + "  ·  " + slotsPanel.title.toUpperCase()
            color: Theme.textSecondary
            font.pixelSize: 12
            font.letterSpacing: 2
            elide: Text.ElideRight
        }

        GridView {
            id: grid
            anchors { top: heading.bottom; topMargin: 16; horizontalCenter: parent.horizontalCenter }
            readonly property int columns: 3
            cellWidth: 236
            cellHeight: 206
            width: cellWidth * columns
            height: cellHeight * Math.ceil(Math.max(1, slotsPanel.slots.length) / columns)
            interactive: false
            model: slotsPanel.slots
            highlightMoveDuration: 0

            delegate: Item {
                id: cell
                width: grid.cellWidth
                height: grid.cellHeight
                readonly property bool current: grid.activeFocus && GridView.isCurrentItem
                readonly property bool usable: modelData.used || slotsPanel.mode === "save"

                Rectangle {
                    anchors { fill: parent; margins: 8 }
                    radius: 8
                    color: Theme.card
                    border.width: cell.current ? 2 : 1
                    border.color: cell.current ? Theme.focusBorder : "#1FFFFFFF"
                    opacity: cell.usable ? 1.0 : 0.45
                    scale: cell.current ? 1.04 : 1.0
                    Behavior on scale { NumberAnimation { duration: Theme.focusDuration } }

                    // The game as it was saved, or the slot's number on an
                    // empty one.
                    Rectangle {
                        id: art
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 6 }
                        height: width / slotsPanel.pictureAspect
                        radius: 5
                        color: "#0A0A12"
                        clip: true
                        Image {
                            anchors.fill: parent
                            source: modelData.picture || ""
                            visible: source !== ""
                            fillMode: Image.Stretch
                            asynchronous: true
                            cache: false
                            smooth: false  // pixel art, kept sharp
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: !modelData.used || !modelData.picture
                            text: modelData.used ? qsTr("No picture") : qsTr("Empty")
                            color: Theme.textSecondary
                            font.pixelSize: 14
                        }
                    }
                    Text {
                        anchors { left: parent.left; leftMargin: 10; top: art.bottom; topMargin: 6 }
                        text: qsTr("Slot %1").arg(modelData.slot + 1)
                        color: Theme.textPrimary
                        font.pixelSize: 13
                    }
                    Text {
                        anchors { right: parent.right; rightMargin: 10; top: art.bottom; topMargin: 7 }
                        text: modelData.when || ""
                        color: Theme.textSecondary
                        font.pixelSize: 11
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: { grid.currentIndex = index; slotsPanel.choose(index) }
                    }
                }
            }

            Keys.onReturnPressed: slotsPanel.choose(currentIndex)
            Keys.onEnterPressed: slotsPanel.choose(currentIndex)
            Keys.onEscapePressed: slotsPanel.close()
            // Its own arrows only; nothing behind the panel moves.
            Keys.onLeftPressed: if (currentIndex % columns > 0) currentIndex--
            Keys.onRightPressed: if (currentIndex % columns < columns - 1 && currentIndex + 1 < count) currentIndex++
            Keys.onUpPressed: if (currentIndex >= columns) currentIndex -= columns
            Keys.onDownPressed: if (currentIndex + columns < count) currentIndex += columns
            Keys.onTabPressed: {}
            Keys.onBacktabPressed: {}
        }

        Text {
            id: hint
            anchors { left: parent.left; leftMargin: 20; bottom: parent.bottom; bottomMargin: 16 }
            text: Theme.hint(qsTr("%1 %2    %3 Back")
                             .arg(slotsPanel.usingController ? slotsPanel.buttons.south : "[Enter]")
                             .arg(slotsPanel.mode === "save"
                                  ? (slotsPanel.slots[grid.currentIndex] && slotsPanel.slots[grid.currentIndex].used
                                     ? qsTr("Save over this") : qsTr("Save here"))
                                  : qsTr("Load this"))
                             .arg(slotsPanel.usingController ? slotsPanel.buttons.east : "[Esc]"))
            color: Theme.textSecondary
            font.pixelSize: 12
        }
    }
}

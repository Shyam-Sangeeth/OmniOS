// The panel behind a tile's three dots, and behind the taskbar's buttons.
//
// A plain Item rather than a Popup: Qt Quick Controls is not linked in, and the
// menu only ever needs to be one panel anchored to something. It is owned by
// the window rather than by the tile that opens it, so it draws above every
// other tile instead of being clipped by the cell it belongs to — a menu that
// appears half-cut behind its neighbours is worse than no menu.
//
// It knows nothing about apps or power. The caller passes the entries and gets
// back the action that was chosen, which is what lets the same component serve
// a tile's menu and the system menu without either of them growing a special
// case in here.
import QtQuick
import omnios

Item {
    id: menu

    // Set together by openFor().
    property string heading: ""
    // The panel's colour. Nearly opaque by default; a menu over moving video
    // (TV's audio and subtitles) is lighter, so the picture shows through.
    property color panelColor: "#F21A1826"
    // Closes by itself after this many milliseconds untouched; 0 never does.
    // A menu over a playing channel goes, as a TV's does; the launcher's wait.
    property int idleTimeout: 0
    // [{ action: "open", label: "Open", enabled: true }, …]
    property var entries: []
    // Free tag so the caller can tell which menu answered.
    property string context: ""
    // Whatever the caller wants to remember for the duration, such as the id of
    // the tile the menu belongs to.
    property var subject: null

    signal chosen(string action, string context)
    signal closed()

    visible: false
    z: 100

    function openFor(item, menuHeading, menuEntries, menuContext, menuSubject) {
        heading = menuHeading
        entries = menuEntries
        context = menuContext
        subject = menuSubject === undefined ? null : menuSubject
        fitWidth()

        // Anchor under the item's bottom-right, then pull back inside the
        // window: an item near an edge would otherwise open a panel that runs
        // off the screen.
        //
        // Where there is no room below — the taskbar along the bottom — it
        // opens above instead, as a panel's menus do. Pulled back inside the
        // window from there, it would cover the very button that opened it.
        // The height is worked out from the entries rather than read from the
        // panel, whose list has not laid the new entries out yet.
        var expected = header.y + header.height + 2 + menuEntries.length * 32 + 10
        var origin = item.mapToItem(menu, item.width, item.height)
        var top = item.mapToItem(menu, 0, 0).y
        panel.x = Math.max(8, Math.min(origin.x - panel.width, menu.width - panel.width - 8))
        if (origin.y - 4 + expected > menu.height - 8 && top - expected - 6 >= 8)
            panel.y = top - expected - 6
        else
            panel.y = Math.max(8, Math.min(origin.y - 4, menu.height - expected - 8))

        visible = true
        // Land on something usable. Opening on a greyed-out entry looks like
        // the menu came up broken.
        list.currentIndex = Math.max(0, firstEnabled(0, 1))
        list.forceActiveFocus()
        touched()
    }

    // Focus back on the open menu, after the window was away: opened from Meta
    // while a game had the front, the launcher came forward just after, and
    // Enter went to the grid under the menu.
    function takeFocus() {
        list.forceActiveFocus()
    }

    function close() {
        idle.stop()
        visible = false
        closed()
    }

    // Something was done in the menu: the idle time starts again.
    function touched() {
        if (idleTimeout > 0 && visible) idle.restart()
    }
    Timer {
        id: idle
        interval: menu.idleTimeout
        onTriggered: if (menu.visible) menu.close()
    }

    // Catches the click that dismisses the menu, and stops it reaching whatever
    // is underneath — otherwise dismissing the menu would also launch an app.
    MouseArea {
        anchors.fill: parent
        onClicked: menu.close()
    }

    // Measures the longest label at the rows' font, so an entry that needs
    // the room (a game's full title, a warning) gets it instead of an ellipsis.
    TextMetrics { id: labelMetrics; font.pixelSize: 13 }
    // Set once per list of entries, not bound: measuring writes the metrics'
    // text, and a binding that reads what it writes loops.
    function fitWidth() {
        var widest = 0
        for (var i = 0; i < entries.length; ++i) {
            labelMetrics.text = entries[i].label
            widest = Math.max(widest, labelMetrics.advanceWidth)
        }
        // The label's 14 + 12 of margin, and a little air.
        panel.width = Math.min(menu.width - 16, Math.max(210, Math.ceil(widest) + 34))
    }

    Rectangle {
        id: panel
        // 210, or wider for a long entry; set by fitWidth when entries arrive.
        width: 210
        height: header.y + header.height + list.contentHeight + 10
        radius: 8
        color: menu.panelColor
        border.width: 1
        border.color: "#26FFFFFF"

        Text {
            id: header
            anchors { left: parent.left; right: parent.right; top: parent.top }
            anchors.margins: 12
            height: implicitHeight + 8
            text: menu.heading
            color: Theme.textSecondary
            font.pixelSize: 11
            font.letterSpacing: 1.5
            elide: Text.ElideRight
        }

        ListView {
            id: list
            anchors {
                left: parent.left; right: parent.right
                top: header.bottom; topMargin: 2
            }
            height: contentHeight
            model: menu.entries
            // Not flickable: the menu is a handful of fixed rows and a stray
            // drag scrolling them under the cursor would be nothing but a bug.
            interactive: false
            // Arrow keys are handled below, one entry at a time, skipping the
            // disabled ones. ListView's own key navigation is off anyway —
            // keyNavigationEnabled is bound to interactive by default, which is
            // exactly why Down did nothing here at first.

            delegate: Item {
                width: list.width
                height: 32

                Rectangle {
                    anchors { fill: parent; leftMargin: 4; rightMargin: 4 }
                    radius: 4
                    color: Theme.accent
                    opacity: list.activeFocus && list.currentIndex === index
                             && modelData.enabled ? 0.30 : 0
                }

                Text {
                    anchors {
                        left: parent.left; leftMargin: 14
                        right: parent.right; rightMargin: 12
                        verticalCenter: parent.verticalCenter
                    }
                    text: modelData.label
                    color: modelData.enabled ? Theme.textPrimary : "#55FFFFFF"
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: modelData.enabled
                    hoverEnabled: true
                    onEntered: { list.currentIndex = index; menu.touched() }
                    onClicked: menu.choose(index)
                }
            }

            Keys.onDownPressed: menu.step(1)
            Keys.onUpPressed: menu.step(-1)
            Keys.onReturnPressed: menu.choose(currentIndex)
            Keys.onEnterPressed: menu.choose(currentIndex)
            Keys.onEscapePressed: menu.close()
        }
    }

    // First selectable entry at or after `from`, walking in `dir` and wrapping.
    // Returns -1 when every entry is disabled, which no caller produces today
    // but which the callers do not assume.
    function firstEnabled(from, dir) {
        var items = menu.entries
        for (var i = 0; i < items.length; ++i) {
            var index = (((from + dir * i) % items.length) + items.length) % items.length
            if (items[index].enabled) return index
        }
        return -1
    }

    function step(dir) {
        var next = firstEnabled(list.currentIndex + dir, dir)
        if (next >= 0) list.currentIndex = next
        touched()
    }

    // Replaces the entries without losing the highlight, so a menu that stays
    // open after a press can show the new state under the cursor.
    function updateEntries(newEntries) {
        var index = list.currentIndex
        entries = newEntries
        list.currentIndex = Math.min(index, Math.max(0, newEntries.length - 1))
    }

    function choose(index) {
        var entry = menu.entries[index]
        if (!entry || !entry.enabled) return
        var action = entry.action
        var tag = menu.context

        // A sticky entry is one you press repeatedly — turning the volume up
        // once is never what anyone means — so the menu stays where it is and
        // the caller refreshes what it says.
        if (entry.sticky === true) {
            chosen(action, tag)
            return
        }

        close()
        chosen(action, tag)
    }

    Keys.onEscapePressed: close()
}

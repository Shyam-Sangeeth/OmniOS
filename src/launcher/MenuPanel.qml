// The panel behind a tile's three dots, and behind the logo.
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

        // Anchor under the item's bottom-right, then pull back inside the
        // window: an item near an edge would otherwise open a panel that runs
        // off the screen.
        var origin = item.mapToItem(menu, item.width, item.height)
        panel.x = Math.max(8, Math.min(origin.x - panel.width, menu.width - panel.width - 8))
        panel.y = Math.max(8, Math.min(origin.y - 4, menu.height - panel.height - 8))

        visible = true
        // Land on something usable. Opening on a greyed-out entry looks like
        // the menu came up broken.
        list.currentIndex = Math.max(0, firstEnabled(0, 1))
        list.forceActiveFocus()
    }

    function close() {
        visible = false
        closed()
    }

    // Catches the click that dismisses the menu, and stops it reaching whatever
    // is underneath — otherwise dismissing the menu would also launch an app.
    MouseArea {
        anchors.fill: parent
        onClicked: menu.close()
    }

    Rectangle {
        id: panel
        width: 210
        height: header.y + header.height + list.contentHeight + 10
        radius: 8
        color: "#F21A1826"
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
                    onEntered: list.currentIndex = index
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

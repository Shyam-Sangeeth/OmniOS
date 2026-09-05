// The overflow menu behind a tile's three dots.
//
// A plain Item rather than a Popup: Qt Quick Controls is not linked in, and the
// menu only ever needs to be one panel anchored to a tile. It is opened by the
// grid, not by the tile, so it draws above every other tile instead of being
// clipped by the cell it belongs to — a menu that appears half-cut behind its
// neighbours is worse than no menu.
import QtQuick
import omnios

Item {
    id: menu

    // Set together by open().
    property string appId: ""
    property string appTitle: ""
    property bool   removable: false
    property bool   installed: true

    signal requested(string action)

    visible: false
    z: 100

    function openFor(item, id, title, canRemove, isInstalled) {
        appId = id
        appTitle = title
        removable = canRemove
        installed = isInstalled

        // Anchor to the tile's bottom-right, then pull back inside the window.
        // Tiles on the last column would otherwise open a panel that runs off
        // the screen edge.
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

    signal closed()

    // Catches the click that dismisses the menu, and stops it reaching the tile
    // underneath — otherwise dismissing the menu would also launch an app.
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
            text: menu.appTitle
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
            // Not flickable: the menu is five fixed rows and a stray drag
            // scrolling them under the cursor would be nothing but a bug.
            interactive: false
            // Arrow keys are handled below, one entry at a time, skipping the
            // disabled ones. ListView's own key navigation is off anyway —
            // keyNavigationEnabled is bound to interactive by default, which is
            // exactly why Down did nothing here at first.

            // An entry that does not apply is left out rather than greyed. A
            // disabled "Install" on something already installed is noise: it
            // describes a state you can see from the tile.
            //
            // The one exception is uninstalling a system app, which stays
            // visible and disabled with its reason. That is a rule, not a
            // state — dropping it silently would leave someone wondering
            // whether the tile was special or the menu was broken.
            model: {
                var entries = []
                if (menu.installed) {
                    entries.push({ action: "open",   label: qsTr("Open"),             enabled: true })
                    entries.push({ action: "check",  label: qsTr("Check for update"), enabled: true })
                    entries.push({ action: "update", label: qsTr("Update"),           enabled: true })
                }
                if (menu.removable)
                    entries.push({ action: "uninstall", label: qsTr("Uninstall"), enabled: menu.installed })
                else
                    entries.push({ action: "uninstall", label: qsTr("Uninstall  ·  system app"),
                                   enabled: false })
                return entries
            }

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
    // Returns -1 when every entry is disabled, which cannot happen today — the
    // update check is always available — but the callers do not assume it.
    function firstEnabled(from, dir) {
        var entries = list.model
        for (var i = 0; i < entries.length; ++i) {
            var index = (((from + dir * i) % entries.length) + entries.length) % entries.length
            if (entries[index].enabled) return index
        }
        return -1
    }

    function step(dir) {
        var next = firstEnabled(list.currentIndex + dir, dir)
        if (next >= 0) list.currentIndex = next
    }

    function choose(index) {
        var entry = list.model[index]
        if (!entry || !entry.enabled) return
        close()
        requested(entry.action)
    }

    Keys.onEscapePressed: close()
}

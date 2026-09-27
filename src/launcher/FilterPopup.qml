// The open filter's list: one for the whole window, filling it, so it sits
// over everything and takes every click. A click outside the list closes it —
// or, on another filter, opens that one instead — and nothing behind it is
// ever touched.
//
// The list is multi-select: A, Enter or a click checks or unchecks a choice
// and the list stays open for the next; "All ..." unchecks everything. A
// search box on top narrows it, closest match first. B or Esc closes.
import QtQuick
import omnios

Item {
    id: popup

    // The FilterDropdowns it serves, to open another from a click.
    property var dropdowns: []
    // The window's FieldKeyboard, for the search box with a controller.
    property Item keyboard: null
    // A controller is in use: open on the list rather than the search box.
    property bool controller: false

    // The open filter; null while closed.
    property Item dropdown: null
    readonly property bool isOpen: dropdown !== null

    signal toggled(int facet, string id)

    anchors.fill: parent
    visible: isOpen

    readonly property var options: dropdown ? dropdown.options : []

    // The rows, as they were when the list opened. Picking changes ticks and
    // counts, not which choices there are, so the rows stay put and read
    // those live (current, below); a new model would send the highlight to
    // the top and back on every pick. They are rebuilt only when the choices
    // themselves change.
    property var rows: []
    // The latest of each choice, by id.
    readonly property var current: {
        var byId = {}
        for (var i = 0; i < options.length; ++i) byId[options[i].id] = options[i]
        return byId
    }
    onOptionsChanged: {
        if (!isOpen) return
        var same = options.length === rows.length
        for (var i = 0; same && i < options.length; ++i) same = options[i].id === rows[i].id
        if (!same) rows = options
    }

    // Closest first: the earlier the match in the name, the higher it comes.
    readonly property var shown: {
        var q = search.text.trim().toLowerCase()
        if (q === "") return rows
        var hits = []
        for (var i = 0; i < rows.length; ++i) {
            var at = rows[i].name.toLowerCase().indexOf(q)
            if (at >= 0) hits.push({ at: at, order: i, option: rows[i] })
        }
        hits.sort(function (a, b) { return a.at - b.at || a.order - b.order })
        return hits.map(function (h) { return h.option })
    }

    // The choice last toggled, to keep the highlight on it when the counts
    // change and the list is rebuilt around it.
    property string lastId: ""

    function openFor(d, typed) {
        if (dropdown) dropdown.open = false
        dropdown = d
        d.open = true
        rows = d.options
        lastId = ""
        search.text = typed || ""
        list.currentIndex = 0
        list.positionViewAtBeginning()
        if (popup.controller && !typed) list.forceActiveFocus()
        else search.input.forceActiveFocus()
    }

    function close() {
        var d = dropdown
        if (!d) return
        d.open = false
        dropdown = null
        d.forceActiveFocus()
    }

    function toggleCurrent() {
        var option = shown[list.currentIndex]
        if (!option || !dropdown) return
        lastId = option.id
        popup.toggled(dropdown.facet, option.id)
    }

    // After the list has taken its new model, which resets its highlight to
    // the top: run now, this would be undone straight away.
    onShownChanged: Qt.callLater(keepPlace)
    function keepPlace() {
        if (lastId === "") return
        for (var i = 0; i < shown.length; ++i) {
            if (shown[i].id === lastId) {
                list.currentIndex = i
                list.positionViewAtIndex(i, ListView.Contain)
                return
            }
        }
    }

    // Everything outside the list.
    MouseArea {
        anchors.fill: parent
        onClicked: mouse => {
            for (var i = 0; i < popup.dropdowns.length; ++i) {
                var d = popup.dropdowns[i]
                var p = d.mapFromItem(popup, mouse.x, mouse.y)
                if (d.contains(p)) {
                    if (d === popup.dropdown) popup.close()
                    else popup.openFor(d, "")
                    return
                }
            }
            popup.close()
        }
        onWheel: wheel => { wheel.accepted = true }
    }

    Rectangle {
        id: panel
        readonly property point at: popup.dropdown
            ? popup.dropdown.mapToItem(popup, 0, popup.dropdown.height + 6) : Qt.point(0, 0)
        x: Math.min(at.x, popup.width - width - 8)
        y: at.y
        width: Math.max(popup.dropdown ? popup.dropdown.width : 0, 320)
        height: Math.min(popup.height - y - 16, search.height + 24 + Math.max(1, list.count) * 44 + 12)
        radius: 10
        // Opaque: the tiles' names showing through read as part of the list.
        color: "#1A1826"
        border.width: 1
        border.color: "#33FFFFFF"

        // Clicks on the panel's own background stay here.
        MouseArea { anchors.fill: parent; onWheel: wheel => { wheel.accepted = false } }

        Field {
            id: search
            keyboard: popup.keyboard
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
            label: ""
            onEdited: { popup.lastId = ""; list.currentIndex = 0 }
            downTo: list
            tabTo: list
            onSubmitted: list.forceActiveFocus()
            Keys.onEscapePressed: popup.close()
        }
        ChoiceList {
            id: list
            anchors { left: parent.left; right: parent.right; top: search.bottom; topMargin: 10
                      bottom: parent.bottom; margins: 6 }
            model: popup.shown
            multi: true
            detailRole: "detail"
            checkedOf: id => { var o = popup.current[id]; return !!o && !!o.checked }
            detailOf: id => { var o = popup.current[id]; return o ? o.detail : "" }
            search: search
            empty: qsTr("Nothing matches \"%1\"").arg(search.text)
            onPicked: popup.toggleCurrent()
            onBackWanted: popup.close()
            onLeftWanted: {}
            onRightWanted: {}
            onDownWanted: {}
        }
    }

    Keys.onEscapePressed: close()
}

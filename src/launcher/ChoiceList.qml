// A list to choose one entry from, on the installer's pages: languages and
// keyboard layouts. Entries are { id, name, ... }; the chosen one is ticked,
// and detailRole names what goes on the right of each row.
//
// Built for a controller as much as a keyboard: A picks, the D-pad moves and
// leaves the list at its edges (up into the search box above it, down to the
// page's buttons, left and right to a list beside it), and the shoulder
// buttons — arriving as Tab and Backtab — jump a letter at a time. Typing
// while it has focus goes into the search box.
import QtQuick
import omnios

ListView {
    id: list

    property string chosen
    // Several can be chosen: each entry's own `checked` says which, and rows
    // show a checkbox rather than a tick.
    property bool multi: false
    // Where a row's tick and detail come from, when they change more often
    // than the rows do: function(id) -> value. A new model resets the list's
    // highlight to the top, so a list whose ticks and counts change on every
    // pick reads them live through these instead of being given a new model.
    property var checkedOf: null
    property var detailOf: null
    property string detailRole: ""
    // The Field above the list that filters it.
    property Item search: null
    // What to say when the search leaves nothing.
    property string empty

    signal picked()
    signal leftWanted()
    signal rightWanted()
    signal downWanted()
    signal backWanted()

    clip: true
    spacing: 4
    keyNavigationEnabled: true
    highlightMoveDuration: 0
    currentIndex: 0

    // Puts the highlight on the chosen entry, where the list opens.
    function showChosen() {
        for (var i = 0; i < count; ++i) {
            if (model[i].id === chosen) {
                currentIndex = i
                positionViewAtIndex(i, ListView.Center)
                return
            }
        }
    }

    function jumpLetter(direction) {
        var i = currentIndex
        if (i < 0 || count === 0) return
        var initial = function (k) { return model[k].name.charAt(0).toUpperCase() }
        var letter = initial(i)
        if (direction > 0) {
            while (i < count - 1 && initial(i) === letter) i++
        } else if (i > 0 && initial(i - 1) === letter) {
            // Back to the start of this letter...
            while (i > 0 && initial(i - 1) === letter) i--
        } else if (i > 0) {
            // ...or, already there, to the start of the one before.
            i--
            var previous = initial(i)
            while (i > 0 && initial(i - 1) === previous) i--
        }
        currentIndex = i
        positionViewAtIndex(i, ListView.Beginning)
    }

    delegate: Rectangle {
        id: row
        required property var modelData
        required property int index
        readonly property bool current: ListView.isCurrentItem
        readonly property bool picked: !list.multi ? modelData.id === list.chosen
                                     : list.checkedOf ? list.checkedOf(modelData.id)
                                     : !!modelData.checked
        width: list.width
        height: 40
        radius: 6
        color: current && list.activeFocus ? "#2A2750" : "transparent"
        border.width: current && list.activeFocus ? 2 : 0
        border.color: Theme.focusBorder

        Text {
            anchors {
                left: parent.left; leftMargin: 14
                right: detail.left; rightMargin: 12
                verticalCenter: parent.verticalCenter
            }
            elide: Text.ElideRight
            text: (list.multi ? (row.picked ? "☑  " : "☐  ")
                              : (row.picked ? "✓  " : "")) + row.modelData.name
            color: row.picked ? Theme.textPrimary : "#C8C8D8"
            font.pixelSize: 15
            font.weight: row.picked ? Font.DemiBold : Font.Normal
        }
        Text {
            id: detail
            anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
            width: Math.min(implicitWidth, row.width * 0.45)
            elide: Text.ElideRight
            text: list.detailOf ? list.detailOf(row.modelData.id)
                  : list.detailRole ? row.modelData[list.detailRole] : ""
            color: Theme.textSecondary
            font.pixelSize: 13
        }
        MouseArea {
            anchors.fill: parent
            onClicked: {
                list.currentIndex = row.index
                list.forceActiveFocus()
                list.picked()
            }
        }
    }

    Keys.onReturnPressed: picked()
    Keys.onEnterPressed: picked()
    Keys.onEscapePressed: backWanted()
    Keys.onLeftPressed: leftWanted()
    Keys.onRightPressed: rightWanted()
    Keys.onUpPressed: event => {
        if (currentIndex <= 0 && search) search.input.forceActiveFocus()
        else event.accepted = false
    }
    Keys.onDownPressed: event => {
        if (currentIndex >= count - 1) downWanted()
        else event.accepted = false
    }
    Keys.onTabPressed: jumpLetter(1)
    Keys.onBacktabPressed: jumpLetter(-1)
    Keys.onPressed: event => {
        if (search && event.text.length === 1 && event.text.trim() !== ""
                && !(event.modifiers & Qt.ControlModifier)) {
            search.input.forceActiveFocus()
            search.text += event.text
            list.currentIndex = 0
            event.accepted = true
        }
    }

    Text {
        anchors.centerIn: parent
        visible: list.count === 0
        text: list.empty
        color: Theme.textSecondary
        font.pixelSize: 15
    }
}

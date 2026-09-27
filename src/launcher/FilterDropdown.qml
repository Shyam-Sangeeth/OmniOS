// A filter in a row of them: its label over what is checked in it. Opening it
// is the window's FilterPopup's job — one list for the whole window, over
// everything, so a click on an option can never reach a tile behind it and
// only one list is ever open.
//
// options: [{ id, name, detail, checked }, ...], "All ..." first with id "".
import QtQuick
import omnios

FocusScope {
    id: dropdown

    property string label
    property var options: []
    // Which filter this is, for the popup: 0 country, 1 category, 2 language.
    property int facet: 0
    // The popup has this one's list open.
    property bool open: false

    // `typed` is a first letter typed on the closed filter, to search with.
    signal openRequested(string typed)

    width: 230
    height: column.implicitHeight

    // "All countries", "India", "India, Nepal", "India + 2 more".
    readonly property string summary: {
        var names = []
        for (var i = 0; i < options.length; ++i)
            if (options[i].checked && options[i].id !== "") names.push(options[i].name)
        if (names.length === 0) return options.length > 0 ? options[0].name : ""
        if (names.length <= 2) return names.join(", ")
        return qsTr("%1 + %2 more").arg(names[0]).arg(names.length - 1)
    }

    Column {
        id: column
        width: parent.width
        spacing: 6

        Text {
            text: dropdown.label
            color: Theme.textSecondary
            font.pixelSize: 13
        }
        Rectangle {
            id: box
            width: parent.width
            height: 44
            radius: 8
            color: Theme.card
            focus: true
            border.width: activeFocus || dropdown.open ? 2 : 1
            border.color: activeFocus || dropdown.open ? Theme.accent : "#26FFFFFF"

            Text {
                anchors { left: parent.left; leftMargin: 14; right: arrow.left; rightMargin: 8
                          verticalCenter: parent.verticalCenter }
                text: dropdown.summary
                color: Theme.textPrimary
                font.pixelSize: 15
                elide: Text.ElideRight
            }
            Text {
                id: arrow
                anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                text: dropdown.open ? "▴" : "▾"
                color: Theme.textSecondary
                font.pixelSize: 14
            }
            MouseArea {
                anchors.fill: parent
                onClicked: dropdown.openRequested("")
            }

            Keys.onReturnPressed: dropdown.openRequested("")
            Keys.onEnterPressed: dropdown.openRequested("")
            Keys.onSpacePressed: dropdown.openRequested("")
            // Typing on a closed filter opens it, searching.
            Keys.onPressed: event => {
                if (event.text.length === 1 && event.text.trim() !== ""
                        && !(event.modifiers & Qt.ControlModifier)) {
                    dropdown.openRequested(event.text)
                    event.accepted = true
                }
            }
        }
    }
}

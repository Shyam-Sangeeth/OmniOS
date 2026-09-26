// A labelled text box for the full-screen pages. Qt Quick Controls is not
// linked into this binary, so it is a TextInput in a Rectangle, styled like
// the rest of the shell.
import QtQuick
import omnios

Column {
    id: field

    property string label
    property alias text: box.text
    property alias input: box
    property bool secret: false
    property bool invalid: false
    // Where Tab, Up and Down go from this box. Left and Right are not taken:
    // inside a text box they move the cursor.
    property Item tabTo: null
    property Item upTo: null
    property Item downTo: null

    // Typed into, as opposed to set from code.
    signal edited()
    // Return in the box.
    signal submitted()

    spacing: 6

    Text {
        text: field.label
        color: Theme.textSecondary
        font.pixelSize: 13
        visible: text.length > 0
    }
    Rectangle {
        width: field.width
        height: 44
        radius: 8
        color: Theme.card
        border.width: box.activeFocus ? 2 : 1
        border.color: field.invalid ? Theme.danger
                    : box.activeFocus ? Theme.accent : "#26FFFFFF"
        TextInput {
            id: box
            anchors { fill: parent; leftMargin: 14; rightMargin: 14 }
            verticalAlignment: TextInput.AlignVCenter
            color: Theme.textPrimary
            selectionColor: Theme.accent
            font.pixelSize: 16
            clip: true
            selectByMouse: true
            echoMode: field.secret ? TextInput.Password : TextInput.Normal
            passwordCharacter: "•"
            onTextEdited: field.edited()
            onAccepted: field.submitted()
            KeyNavigation.tab: field.tabTo
            KeyNavigation.up: field.upTo
            KeyNavigation.down: field.downTo
        }
    }
}

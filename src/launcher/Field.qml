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
    // The window's FieldKeyboard, if it has one. A controller's A (✕) on the
    // box then opens it on this field, rather than submitting a box nobody
    // could have typed into.
    property Item keyboard: null
    readonly property bool controllerTyping: !!keyboard && keyboard.controller
    // After the keyboard's Done, go on to the next box (downTo, else tabTo).
    // Off where Return in this box is the whole point, like signing in.
    property bool doneMovesOn: true

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
            Keys.onPressed: event => {
                if (field.keyboard && event.nativeScanCode === Theme.controllerScanCode
                        && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
                    field.keyboard.show(field)
                    event.accepted = true
                }
            }
        }
        // With a controller in hand an empty box says how to fill it in.
        Text {
            anchors { fill: box }
            verticalAlignment: Text.AlignVCenter
            visible: box.text === "" && box.activeFocus && field.controllerTyping
            text: Theme.hint(qsTr("%1  Type").arg(field.controllerTyping ? field.keyboard.buttons.south : ""))
            color: Theme.textSecondary
            font.pixelSize: 15
        }
    }
}

// A question that needs typing, over the whole window: the administrator
// password, a Wi-Fi password. A text box, a line for what went wrong, Cancel
// and Continue — and, when the controller is what is being used, the
// on-screen keyboard under the box, with the focus on it.
//
// It owns no state of its own beyond the text. Whoever opens it binds `open`,
// the wording and the error, and answers `accepted` and `cancelled`.
import QtQuick
import omnios

Rectangle {
    id: prompt

    property bool open: false
    property string title
    property string reason
    property bool secret: true
    property string errorText: ""
    // Waiting on an answer: Continue goes inert and the error line says so.
    property bool checking: false
    property string continueLabel: qsTr("Continue")
    // Show the on-screen keyboard. Bound to whether the controller is in use,
    // so it appears for a controller and stays out of a keyboard's way.
    property bool useKeyboard: false
    // The controller's button names, for the keyboard's legend.
    property var buttons: ({ south: "A", east: "B", west: "X", north: "Y",
                             l1: "L1", r1: "R1", start: "Start", guide: "Guide" })

    signal accepted(string text)
    signal cancelled()

    // An error is about the last try. Once the next one is being typed it is
    // out of date, and a red line under a box someone is filling in again
    // reads as though the new text were wrong too.
    property bool typedSinceError: false
    readonly property bool showError: errorText !== "" && !typedSinceError

    // Puts the focus where typing happens: the keyboard for a controller, the
    // box for a keyboard.
    function takeFocus() {
        if (useKeyboard) keyboard.forceActiveFocus()
        else field.input.forceActiveFocus()
    }

    function accept() {
        if (field.text.length === 0 || checking) return
        accepted(field.text)
    }

    anchors.fill: parent
    visible: open
    color: "#D90A0A12"

    // Clicks stop here rather than reaching the grid behind.
    MouseArea { anchors.fill: parent }

    onOpenChanged: {
        field.text = ""
        keyboard.row = 1
        keyboard.col = 0
        keyboard.shifted = false
        keyboard.symbols = false
        if (open) takeFocus()
    }
    // Picking up a controller half way through moves the focus to the keyboard,
    // and touching a real keyboard moves it back to the box.
    onUseKeyboardChanged: if (open) takeFocus()
    // A wrong answer is typed again from nothing, not edited.
    // A second wrong answer brings the same error text, which changes nothing
    // and so would not clear the box again; a check finishing does.
    onCheckingChanged: {
        if (!checking && open && errorText !== "") {
            typedSinceError = false
            field.text = ""
            takeFocus()
        }
    }
    onErrorTextChanged: {
        typedSinceError = false
        if (open && errorText !== "") {
            field.text = ""
            takeFocus()
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.max(480, keyboard.visible ? keyboard.implicitWidth + 64 : 0)
        height: column.implicitHeight + 64
        radius: 14
        color: Theme.card
        border.width: 1
        border.color: "#26FFFFFF"

        Column {
            id: column
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 32 }
            spacing: 16

            Text {
                text: prompt.title
                color: Theme.textPrimary
                font.pixelSize: 22
                font.weight: Font.DemiBold
            }
            Text {
                width: parent.width
                text: prompt.reason
                color: Theme.textSecondary
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }
            Field {
                id: field
                // Programmatic typing (the on-screen keyboard) counts too, so
                // this watches the text rather than Field's edited().
                Connections {
                    target: field.input
                    function onTextChanged() { if (field.text.length > 0) prompt.typedSinceError = true }
                }
                width: parent.width
                secret: prompt.secret
                invalid: prompt.showError
                tabTo: continueButton
                downTo: prompt.useKeyboard ? keyboard : continueButton
                onSubmitted: prompt.accept()
                Keys.onEscapePressed: prompt.cancelled()
            }
            // A fixed height, so nothing below moves when a message appears.
            Text {
                width: parent.width
                height: 18
                text: prompt.checking ? qsTr("Checking …") : prompt.showError ? prompt.errorText : ""
                color: prompt.checking ? Theme.textSecondary : Theme.danger
                font.pixelSize: 13
            }
            OnScreenKeyboard {
                id: keyboard
                anchors.horizontalCenter: parent.horizontalCenter
                visible: prompt.useKeyboard
                target: field.input
                buttons: prompt.buttons
                onDone: prompt.accept()
                onCancelled: prompt.cancelled()
            }
            Row {
                anchors.right: parent.right
                spacing: 12
                ActionButton {
                    id: cancelButton
                    text: qsTr("Cancel")
                    onActivated: prompt.cancelled()
                    KeyNavigation.right: continueButton
                    KeyNavigation.up: field.input
                    KeyNavigation.tab: field.input
                    Keys.onEscapePressed: prompt.cancelled()
                }
                ActionButton {
                    id: continueButton
                    text: prompt.continueLabel
                    available: !prompt.checking && field.text.length > 0
                    onActivated: prompt.accept()
                    KeyNavigation.left: cancelButton
                    KeyNavigation.up: field.input
                    KeyNavigation.tab: cancelButton
                    Keys.onEscapePressed: prompt.cancelled()
                }
            }
        }
    }
}

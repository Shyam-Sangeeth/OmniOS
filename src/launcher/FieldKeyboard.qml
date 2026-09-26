// The on-screen keyboard for a page of Fields: the installer's, the sign-in
// screen's. A Field whose `keyboard` is this opens it when a controller's A
// (✕) lands on the box; it then covers the page, shows what is being typed
// into which box, and hands the focus back to that box when it closes.
//
//   Done    keep the text and move on, as Return in the box would
//   Cancel  put the box back the way it was
//
// Touching a real keyboard or the mouse closes it too, keeping the text, with
// the focus in the box to carry on typing there.
import QtQuick
import omnios

Rectangle {
    id: panel

    // The window's InputMode (Installer.input, Greeter.input).
    property QtObject inputMode: null
    readonly property bool controller: !!inputMode && inputMode.usingController
    readonly property var buttons: inputMode ? inputMode.buttonNames
                                             : { "south": "A", "east": "B", "west": "X", "north": "Y",
                                                 "l1": "L1", "r1": "R1", "start": "Start", "guide": "Guide" }

    // The Field being typed into; null while closed.
    property Item field: null
    readonly property bool open: field !== null
    // What the box held when this opened, for Cancel.
    property string before: ""

    function show(f) {
        before = f.text
        field = f
        keyboard.row = 1
        keyboard.col = 0
        keyboard.shifted = false
        keyboard.symbols = false
        keyboard.forceActiveFocus()
    }

    // how: "done", "cancel", or "drop" (a real keyboard took over).
    function finish(how) {
        var f = field
        if (!f) return
        if (how === "cancel") f.text = before
        field = null
        f.input.forceActiveFocus()
        if (how !== "done") return
        // Whatever Return does in that box; and, unless that was the point,
        // the next box, so a controller walks down a form one Done at a time.
        f.submitted()
        if (f.doneMovesOn && f.input.activeFocus) {
            var next = f.downTo || f.tabTo
            if (next) next.forceActiveFocus()
        }
    }

    anchors.fill: parent
    visible: open
    color: "#D90A0A12"

    // Clicks stop here rather than reaching the page behind, though one still
    // counts as the mouse being back, and closes this.
    MouseArea { anchors.fill: parent }

    Connections {
        target: panel.inputMode
        function onUsingControllerChanged() {
            if (!panel.inputMode.usingController) panel.finish("drop")
        }
    }

    Column {
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 40 }
        width: keyboard.implicitWidth
        spacing: 14

        // The box being filled in, since the page it is on is behind this.
        Text {
            text: panel.field ? panel.field.label : ""
            color: Theme.textSecondary
            font.pixelSize: 13
            visible: text.length > 0
        }
        Rectangle {
            width: parent.width
            height: 44
            radius: 8
            color: Theme.card
            border.width: 2
            border.color: Theme.accent
            Text {
                anchors { fill: parent; leftMargin: 14; rightMargin: 14 }
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideLeft
                color: Theme.textPrimary
                font.pixelSize: 16
                text: {
                    if (!panel.field) return ""
                    var t = panel.field.text
                    return (panel.field.secret ? "•".repeat(t.length) : t) + "▏"
                }
            }
        }
        OnScreenKeyboard {
            id: keyboard
            target: panel.field ? panel.field.input : null
            buttons: panel.buttons
            onDone: panel.finish("done")
            onCancelled: panel.finish("cancel")
        }
    }
}

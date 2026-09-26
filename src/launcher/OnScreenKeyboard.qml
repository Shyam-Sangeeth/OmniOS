// A keyboard for a controller. It types into a TextInput given as target, and
// is driven entirely by the buttons a gamepad has (Xbox names; a DualSense has
// ✕ □ △ ○ in the same places, and the legend says so):
//
//   D-pad  move        A  type the key       X  delete      Y  space
//   L1     Shift       R1 symbols            Start  done    B  cancel
//
// Controller presses arrive as ordinary key events marked with a scan code no
// keyboard sends (Theme.controllerScanCode). Anything else is a real keyboard,
// and is typed straight into the target, so the two can be mixed.
//
// Qt Virtual Keyboard was the obvious alternative and the wrong one here: it
// is built for touch, and moving around it with arrow keys is a build-time
// option Arch does not enable.
import QtQuick
import omnios

Rectangle {
    id: osk

    // The TextInput to type into (Field.input).
    property Item target: null
    // What the pad in hand calls its buttons, for the legend: ✕ on a
    // PlayStation pad where an Xbox pad has A. See LauncherController.
    property var buttons: ({ south: "A", east: "B", west: "X", north: "Y",
                             l1: "L1", r1: "R1", start: "Start", guide: "Guide" })
    // One-shot, as on a phone: the next letter only.
    property bool shifted: false
    property bool symbols: false

    signal done()
    signal cancelled()

    readonly property var letterRows: [
        ["1", "2", "3", "4", "5", "6", "7", "8", "9", "0"],
        ["q", "w", "e", "r", "t", "y", "u", "i", "o", "p"],
        ["a", "s", "d", "f", "g", "h", "j", "k", "l", "'"],
        ["z", "x", "c", "v", "b", "n", "m", ",", ".", "-"]
    ]
    readonly property var symbolRows: [
        ["!", "@", "#", "$", "%", "^", "&", "*", "(", ")"],
        ["~", "`", "_", "=", "+", "[", "]", "{", "}", "\\"],
        ["|", ";", ":", "\"", "<", ">", "?", "/", "€", "£"],
        ["¥", "§", "°", "·", "×", "÷", "¿", "¡", "«", "»"]
    ]
    // The last row, in units of one key's width: ten in all, like the others.
    readonly property var actionRow: [
        { action: "shift",   label: "⇧",      span: 1 },
        { action: "symbols", label: "?123",   span: 2 },
        { action: "space",   label: "Space",  span: 4 },
        { action: "back",    label: "⌫",      span: 1 },
        { action: "done",    label: "Done",   span: 2 }
    ]

    readonly property int columns: 10
    readonly property var rows: symbols ? symbolRows : letterRows
    readonly property int keySize: 44
    readonly property int keyGap: 6

    // The highlighted key. In the last row, col is a unit position, and the
    // key under it is whichever action spans that unit.
    property int row: 1
    property int col: 0

    implicitWidth: columns * keySize + (columns - 1) * keyGap + 24
    implicitHeight: 5 * keySize + 4 * keyGap + 24 + legend.height + 8
    radius: 10
    color: "#0F0E18"
    border.width: 1
    border.color: "#26FFFFFF"

    function actionAt(unit) {
        var start = 0
        for (var i = 0; i < actionRow.length; ++i) {
            if (unit < start + actionRow[i].span) return i
            start += actionRow[i].span
        }
        return actionRow.length - 1
    }

    function labelFor(key) {
        return shifted && !symbols ? key.toUpperCase() : key
    }

    function type(text) {
        if (!target) return
        target.insert(target.cursorPosition, text)
    }

    function backspace() {
        if (!target || target.cursorPosition === 0) return
        target.remove(target.cursorPosition - 1, target.cursorPosition)
    }

    function press() {
        if (row < rows.length) {
            type(labelFor(rows[row][col]))
            shifted = false
            return
        }
        var action = actionRow[actionAt(col)].action
        if (action === "shift") shifted = !shifted
        else if (action === "symbols") symbols = !symbols
        else if (action === "space") type(" ")
        else if (action === "back") backspace()
        else if (action === "done") done()
    }

    function move(dRow, dCol) {
        row = (row + dRow + 5) % 5
        col = (col + dCol + columns) % columns
    }

    Keys.onPressed: function (event) {
        if (event.nativeScanCode === Theme.controllerScanCode) {
            switch (event.key) {
            case Qt.Key_Up:      move(-1, 0); break
            case Qt.Key_Down:    move(1, 0); break
            case Qt.Key_Left:
                // In the last row, left and right step a whole key at a time.
                if (row === rows.length) {
                    var i = actionAt(col)
                    var start = 0
                    for (var j = 0; j < (i + actionRow.length - 1) % actionRow.length; ++j) start += actionRow[j].span
                    col = start
                } else move(0, -1)
                break
            case Qt.Key_Right:
                if (row === rows.length) {
                    var k = (actionAt(col) + 1) % actionRow.length
                    var at = 0
                    for (var m = 0; m < k; ++m) at += actionRow[m].span
                    col = at
                } else move(0, 1)
                break
            case Qt.Key_Return:  press(); break
            case Qt.Key_M:       backspace(); break
            case Qt.Key_F5:      type(" "); break
            case Qt.Key_Backtab: shifted = !shifted; break
            case Qt.Key_Tab:     symbols = !symbols; break
            case Qt.Key_F10:     done(); break
            case Qt.Key_Escape:  cancelled(); break
            default: return
            }
            event.accepted = true
            return
        }
        // A real keyboard: type into the box, as though it had focus.
        if (event.key === Qt.Key_Backspace) backspace()
        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) done()
        else if (event.key === Qt.Key_Escape) cancelled()
        else if (event.text.length > 0 && event.text.charCodeAt(0) >= 32) type(event.text)
        else return
        event.accepted = true
    }

    Column {
        id: grid
        anchors { top: parent.top; horizontalCenter: parent.horizontalCenter; topMargin: 12 }
        spacing: osk.keyGap

        Repeater {
            model: osk.rows
            Row {
                id: keyRow
                property int rowIndex: index
                spacing: osk.keyGap
                Repeater {
                    model: modelData
                    Rectangle {
                        readonly property bool lit: osk.activeFocus && osk.row === keyRow.rowIndex && osk.col === index
                        width: osk.keySize
                        height: osk.keySize
                        radius: 6
                        color: lit ? Theme.accent : Theme.card
                        border.width: lit ? 2 : 1
                        border.color: lit ? Theme.focusBorder : "#1FFFFFFF"
                        Text {
                            anchors.centerIn: parent
                            text: osk.labelFor(modelData)
                            color: Theme.textPrimary
                            font.pixelSize: 17
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: { osk.row = keyRow.rowIndex; osk.col = index; osk.press() }
                        }
                    }
                }
            }
        }

        Row {
            spacing: osk.keyGap
            Repeater {
                model: osk.actionRow
                Rectangle {
                    readonly property bool lit: osk.activeFocus && osk.row === osk.rows.length
                                                && osk.actionAt(osk.col) === index
                    readonly property bool on: (modelData.action === "shift" && osk.shifted)
                                               || (modelData.action === "symbols" && osk.symbols)
                    width: modelData.span * osk.keySize + (modelData.span - 1) * osk.keyGap
                    height: osk.keySize
                    radius: 6
                    color: lit ? Theme.accent : on ? "#3A3570" : "#22212E"
                    border.width: lit ? 2 : 1
                    border.color: lit ? Theme.focusBorder : "#1FFFFFFF"
                    Text {
                        anchors.centerIn: parent
                        text: modelData.action === "symbols" && osk.symbols ? "abc" : modelData.label
                        color: Theme.textPrimary
                        font.pixelSize: 15
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            osk.row = osk.rows.length
                            var at = 0
                            for (var i = 0; i < index; ++i) at += osk.actionRow[i].span
                            osk.col = at
                            osk.press()
                        }
                    }
                }
            }
        }
    }

    Text {
        id: legend
        anchors { top: grid.bottom; topMargin: 10; horizontalCenter: parent.horizontalCenter }
        text: qsTr("%1 type    %2 delete    %3 space    %4 shift    %5 symbols    %6 done    %7 cancel")
                  .arg(osk.buttons.south).arg(osk.buttons.west).arg(osk.buttons.north)
                  .arg(osk.buttons.l1).arg(osk.buttons.r1).arg(osk.buttons.start).arg(osk.buttons.east)
        color: Theme.textSecondary
        font.pixelSize: 12
    }
}

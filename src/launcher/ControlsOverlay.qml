import QtQuick
import omnios

// Which key plays which of the console's buttons, as a slim bar along the
// bottom of a game played from the keyboard (ControlsOverlay.h). Each entry is
// its keys as keycaps, then the console's own names for them in the same
// order — "Z X A S  ✕ ○ □ △" on a PlayStation. Always one line: on a screen
// too narrow for it, the line is shrunk to fit rather than wrapped.
Rectangle {
    id: bar

    // [{ keys: ["Z", ...], button: "✕ ..." }, ...], from omnios::keyboardControls.
    property var rows: []

    readonly property int hpad: 16
    readonly property int vpad: 4
    readonly property real fit: line.implicitWidth > 0
                                ? Math.min(1, (width - 2 * hpad) / line.implicitWidth) : 1

    implicitHeight: Math.ceil(line.implicitHeight * fit) + 2 * vpad
    color: Qt.rgba(0.04, 0.04, 0.07, 0.8)

    Row {
        id: line
        x: (bar.width - implicitWidth * bar.fit) / 2
        y: bar.vpad
        spacing: 18
        scale: bar.fit
        transformOrigin: Item.TopLeft

        Repeater {
            model: bar.rows
            delegate: Row {
                required property var modelData
                spacing: 5
                Text {
                    textFormat: Text.StyledText
                    text: modelData.keys.map(function (k) { return PadGlyphs.key(k) }).join(" ")
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: modelData.button
                    color: Theme.textPrimary
                    font.pixelSize: 13
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }

        // The keyboard's Guide button.
        Text {
            textFormat: Text.StyledText
            text: Theme.hint(qsTr("[Meta][Esc] Library"))
            color: Theme.textSecondary
            font.pixelSize: 13
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}

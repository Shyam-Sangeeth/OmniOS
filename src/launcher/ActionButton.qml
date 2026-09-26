// A button for the full-screen pages — the installer and the sign-in screen.
// Enter, Space and a click all press it, and it shows focus the same way the
// launcher's tiles do, so a controller always knows where it is.
import QtQuick
import omnios

Rectangle {
    id: button

    property string text
    property bool dangerous: false
    // Shown, reachable, and inert. Not "enabled: false": a disabled item
    // cannot take focus, and a controller then has no way to learn why the
    // button it is looking at does nothing.
    property bool available: true

    signal activated()

    function trigger() { if (available) activated() }

    opacity: available ? 1 : 0.45
    implicitWidth: Math.max(160, label.implicitWidth + 48)
    implicitHeight: 48
    radius: 8
    color: dangerous ? (activeFocus ? Theme.danger : "#3A1C22")
                     : (activeFocus ? Theme.accent : Theme.card)
    border.width: activeFocus ? 2 : 1
    border.color: activeFocus ? Theme.focusBorder : "#26FFFFFF"

    Behavior on color { ColorAnimation { duration: Theme.focusDuration } }

    Text {
        id: label
        anchors.centerIn: parent
        text: button.text
        color: Theme.textPrimary
        font.pixelSize: 16
        font.weight: Font.DemiBold
    }

    MouseArea {
        anchors.fill: parent
        onClicked: { button.forceActiveFocus(); button.trigger() }
    }
    Keys.onReturnPressed: trigger()
    Keys.onEnterPressed: trigger()
    Keys.onSpacePressed: trigger()
}

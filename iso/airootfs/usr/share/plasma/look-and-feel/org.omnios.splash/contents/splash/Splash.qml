// The screen shown while the Plasma desktop starts, in place of KDE's.
//
// It is the boot splash carried on: the same images from the plymouth theme,
// at the same size and in the same place, on the same background. Plymouth
// hands the screen over and Plasma's splash picks it up with nothing moving,
// so from power button to desktop there is one OmniOS screen rather than an
// OmniOS one followed by a KDE one. The positions below are omnios.script's,
// line for line; change one and change the other.
//
// The images are read from the plymouth theme rather than copied here, so the
// two splashes cannot drift apart. The SVG mark is the fallback if that theme
// is ever missing — a smaller logo is better than an empty screen.
//
// Plain QtQuick and nothing else: this runs before the desktop exists, and
// the fewer modules it pulls in, the fewer ways it has to fail into a blank
// screen.
import QtQuick

Rectangle {
    id: root

    // Set by KSplash as the desktop comes up. The splash does not need it —
    // KSplash closes it when the desktop is ready — but it has to exist.
    property int stage

    readonly property string art: "file:///usr/share/plymouth/themes/omnios/"

    // #0A0A12, the launcher background and the boot splash's.
    color: "#0A0A12"

    Image {
        id: logo
        // Centred, then lifted 30 pixels, as the boot splash places it.
        x: Math.round(root.width / 2 - width / 2)
        y: Math.round(root.height / 2 - height / 2 - 30)
        source: root.art + "logo.png"
        // Plymouth draws images at their own size. Keeping it so here is what
        // makes the handover invisible.
        fillMode: Image.Pad
        smooth: false

        onStatusChanged: {
            if (status === Image.Error)
                source = "file:///usr/share/icons/hicolor/scalable/apps/omnios.svg"
        }
    }

    Image {
        id: spinner
        property int frame: 0

        x: Math.round(root.width / 2 - width / 2)
        y: logo.y + logo.height + 22
        source: root.art + "progress-" + frame + ".png"
        visible: status === Image.Ready

        // Ten frames a second, the rate the boot splash turns at.
        Timer {
            interval: 100
            running: true
            repeat: true
            onTriggered: spinner.frame = (spinner.frame + 1) % 12
        }
    }
}

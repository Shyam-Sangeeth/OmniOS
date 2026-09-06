// One indicator in the top-right corner: a glyph that says what the machine is
// doing, and a button that opens the panel for changing it.
//
// The glyphs are drawn rather than fetched from an icon theme. There are three
// of them, they have to read at 18 pixels against a dark ground, and a themed
// icon at that size is a smudge — Canvas keeps them crisp and keeps the shell
// from depending on whichever icon set happens to be installed.
import QtQuick
import omnios

Rectangle {
    id: button

    // "lan", "wifi", "offline" or "bluetooth".
    property string glyph: "offline"
    // Connected, powered — drawn in the accent rather than dimmed.
    property bool active: false
    // Its panel is open.
    property bool highlighted: false

    signal triggered()

    width: 34
    height: 34
    radius: 8
    color: hover.containsMouse || highlighted ? "#1FFFFFFF" : "transparent"
    Behavior on color { ColorAnimation { duration: Theme.focusDuration } }

    Canvas {
        id: art
        anchors.centerIn: parent
        width: 20
        height: 20

        readonly property color ink: button.active ? Theme.accent : Theme.textSecondary

        onInkChanged: requestPaint()
        Component.onCompleted: requestPaint()

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = art.ink
            ctx.fillStyle = art.ink
            ctx.lineWidth = 1.6
            ctx.lineCap = "round"
            ctx.lineJoin = "round"

            var w = width
            var h = height

            if (button.glyph === "bluetooth") {
                // The rune: a vertical spine with two bowties crossing it.
                var cx = w * 0.5
                ctx.beginPath()
                ctx.moveTo(cx - w * 0.18, h * 0.30)
                ctx.lineTo(cx + w * 0.18, h * 0.70)
                ctx.lineTo(cx, h * 0.86)
                ctx.lineTo(cx, h * 0.14)
                ctx.lineTo(cx + w * 0.18, h * 0.30)
                ctx.lineTo(cx - w * 0.18, h * 0.70)
                ctx.stroke()
                return
            }

            if (button.glyph === "lan") {
                // A socket with three legs: the shape on the back of every
                // router, and unmistakably not wireless.
                ctx.strokeRect(w * 0.30, h * 0.58, w * 0.40, h * 0.28)
                ctx.beginPath()
                ctx.moveTo(w * 0.5, h * 0.58)
                ctx.lineTo(w * 0.5, h * 0.34)
                ctx.moveTo(w * 0.16, h * 0.34)
                ctx.lineTo(w * 0.84, h * 0.34)
                ctx.moveTo(w * 0.16, h * 0.34)
                ctx.lineTo(w * 0.16, h * 0.20)
                ctx.moveTo(w * 0.84, h * 0.34)
                ctx.lineTo(w * 0.84, h * 0.20)
                ctx.stroke()
                return
            }

            // Wi-Fi: three arcs and a dot. Offline is the same mark struck
            // through, so the two read as the same thing in two states rather
            // than as two unrelated symbols.
            var ox = w * 0.5
            var oy = h * 0.78
            for (var i = 1; i <= 3; ++i) {
                ctx.beginPath()
                ctx.arc(ox, oy, i * w * 0.19, Math.PI * 1.25, Math.PI * 1.75)
                ctx.stroke()
            }
            ctx.beginPath()
            ctx.arc(ox, oy, w * 0.055, 0, Math.PI * 2)
            ctx.fill()

            if (button.glyph === "offline") {
                ctx.beginPath()
                ctx.moveTo(w * 0.18, h * 0.18)
                ctx.lineTo(w * 0.82, h * 0.84)
                ctx.stroke()
            }
        }
    }

    // Repaint when the state behind the glyph changes.
    onGlyphChanged: art.requestPaint()
    onActiveChanged: art.requestPaint()

    MouseArea {
        id: hover
        anchors.fill: parent
        hoverEnabled: true
        onClicked: button.triggered()
    }
}

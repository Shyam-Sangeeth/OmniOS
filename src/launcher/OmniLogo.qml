// The OmniOS mark: an orbit ring around a play triangle.
//
// Drawn rather than loaded from a file, and drawn to match the boot splash's
// mark (tools/make-splash-assets.ps1) rather than being a second logo that
// happens to sit in the same product. The splash renders it at 760px for a
// full screen; this one has to read at 30, so it is the same shapes with the
// wordmark and the tagline dropped — at this size they would be mud.
//
// Canvas, not QtQuick.Shapes: the shell runs under the software renderer in
// any machine without a GPU worth the name, and Canvas is core QtQuick with no
// extra module to link or fail to find.
import QtQuick
import omnios

Canvas {
    id: mark

    // The whole mark scales from this; nothing inside is a fixed pixel count.
    property real diameter: 30
    property color from: Theme.accent
    property color to: "#3A8FFF"

    implicitWidth: diameter
    implicitHeight: diameter
    width: diameter
    height: diameter

    // Repaint whenever the colours change, or a theme switch would leave the
    // old pixels on screen.
    onFromChanged: requestPaint()
    onToChanged: requestPaint()
    onDiameterChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d")
        ctx.reset()

        var cx = width / 2
        var cy = height / 2
        var gradient = ctx.createLinearGradient(0, 0, width, height)
        gradient.addColorStop(0, mark.from)
        gradient.addColorStop(1, mark.to)

        // Outer ring, left open at the lower right so it reads as an orbit
        // rather than a full circle — the same gap the splash leaves.
        ctx.strokeStyle = gradient
        ctx.lineCap = "round"
        ctx.lineWidth = Math.max(1.5, width * 0.085)
        ctx.beginPath()
        ctx.arc(cx, cy, width * 0.44, Math.PI * 0.62, Math.PI * 0.30)
        ctx.stroke()

        // Inner arc: a short counterweight on the opposite side.
        ctx.lineWidth = Math.max(1, width * 0.055)
        ctx.beginPath()
        ctx.arc(cx, cy, width * 0.30, Math.PI * 1.58, Math.PI * 1.98)
        ctx.stroke()

        // Play triangle, nudged right by a whisker: a triangle centred on its
        // bounding box looks left-heavy against a circle.
        var r = width * 0.19
        ctx.fillStyle = gradient
        ctx.beginPath()
        ctx.moveTo(cx - r * 0.72 + width * 0.02, cy - r)
        ctx.lineTo(cx + r + width * 0.02, cy)
        ctx.lineTo(cx - r * 0.72 + width * 0.02, cy + r)
        ctx.closePath()
        ctx.fill()
    }
}

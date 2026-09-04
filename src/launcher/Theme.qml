// Design tokens straight from OmniOS.md §12. Kept in one place so the spec and
// the shell cannot drift apart.
pragma Singleton
import QtQuick

QtObject {
    readonly property color background:    "#0A0A12"
    readonly property color card:          "#16161F"
    readonly property color focusBorder:   "#FFFFFF"
    readonly property color accent:        "#6C63FF"
    readonly property color textPrimary:   "#F0F0F5"
    readonly property color textSecondary: "#8888AA"

    // Tile sizes (§12). The focused recent tile grows rather than the row
    // re-flowing, so nothing shifts under the cursor.
    readonly property int recentWidth:   220
    readonly property int recentHeight:  240
    readonly property int focusedWidth:  240
    readonly property int focusedHeight: 260
    readonly property int gridWidth:     160
    readonly property int gridHeight:    180

    // Animation timings (§12).
    readonly property int focusDuration:      150
    readonly property int slideDuration:      300
    readonly property int backgroundDuration: 400

    readonly property int gutter: 28
}

// The channel playing, inside TV's own window (libmpv; see MpvItem.h).
//
// A bar along the top — Back, the channel, its audio and subtitles, the
// volume, favourite — shows when the mouse moves or a key is pressed, and
// fades after a few seconds of neither.
//
//   Up / Down               volume          M        mute
//   PgUp / PgDn, LB / RB    channel         Enter    into the bar
//   Esc, B, Backspace, right-click: back
//
// Left and Right do not change channel. A stick held to one side repeats
// its direction several times a second, and each repeat was a channel: a
// nudge sent the picture racing through the list. Channel is the shoulder
// buttons, which never repeat, and one press is one channel.
//
// In the bar, Left and Right move between its controls and Enter uses one;
// Esc goes back to the picture. A channel that will not play says so here,
// with the next one and the way back as buttons, rather than leaving a black
// screen; surfing skips it instead — TvController decides which.
import QtQuick
import omnios

FocusScope {
    id: player

    // What the pad in hand calls its buttons, and whether it is in use.
    property var buttons: ({ south: "A", east: "B", west: "X", north: "Y", l1: "LB", r1: "RB" })
    property bool pad: false

    readonly property bool active: Tv.playingInWindow
    readonly property bool failed: Tv.playError !== ""
    readonly property var channel: Tv.playingIndex >= 0 && Tv.playingIndex < Tv.channels.length
                                   ? Tv.channels[Tv.playingIndex] : null
    // What is on it, from the guide; empty where there is none.
    readonly property var guide: channel && Tv.guideRevision >= 0 ? Tv.guideFor(channel.url) : ({})
    readonly property bool hasGuide: guide.now !== undefined || guide.next !== undefined
    readonly property bool loading: active && !failed && (media.status !== "playing" || media.buffering)

    anchors.fill: parent
    visible: active

    function showControls() {
        controls.shown = true
        hideTimer.restart()
    }

    // The volume, in steps of 5%, kept across channels: it is one player.
    function changeVolume(step) {
        media.muted = false
        media.volume = Math.max(0, Math.min(100, Math.round((media.volume + step * 100) / 5) * 5))
        showControls()
    }

    // The keys to the picture: volume, channel, into the bar (below, on the
    // player itself). Not the player's own forceActiveFocus(): the player is a
    // focus scope, and a scope given focus hands it straight back to whichever
    // child had it last — the bar's button, so leaving the bar never did.
    // This item is that child instead; it handles nothing, so every key it
    // gets goes on up to the player's handler.
    Item { id: picture; focus: true }
    function toPicture() { picture.forceActiveFocus() }

    onActiveChanged: if (active) { toPicture(); showControls() }

    Rectangle { anchors.fill: parent; color: "black" }

    // The picture, and the sound: every channel, whatever it needs (MpvItem.h).
    MpvItem {
        id: media
        anchors.fill: parent
        visible: !player.failed
        source: Tv.playingUrl
        userAgent: player.channel && player.channel.userAgent ? player.channel.userAgent : ""
        referrer: player.channel && player.channel.referrer ? player.channel.referrer : ""
        onSourceChanged: {
            player.played = false
            player.retries = 0
            if (source !== "") watchdog.restart()
        }
        onStatusChanged: {
            if (status === "playing") {
                watchdog.stop()
                player.played = true
                if (player.retries > 0) steady.restart()
            }
            // A live channel does not end; one that does has stopped sending.
            else if (status === "failed") player.trouble()
        }
    }

    // Live streams hiccup: a server that drops the connection at an ad break,
    // a playlist that stops for a moment. A channel that has played is
    // reconnected, a few times, before it is given up on; one that never
    // started is given up on at once, since that is what a dead channel looks
    // like.
    property bool played: false
    property int retries: 0
    // A minute of playing after a reconnect: the next hiccup gets its three
    // tries afresh.
    Timer { id: steady; interval: 60000; onTriggered: if (player.played) player.retries = 0 }
    readonly property bool reconnecting: retries > 0 && !played
    function trouble() {
        if (player.failed) return
        if ((played || retries > 0) && retries < 3) {
            ++retries
            played = false
            watchdog.restart()
            media.reload()
            return
        }
        Tv.playerFailed()
    }
    // Neither playing nor failing: a stream that never answers is as dead as
    // one that says so.
    Timer {
        id: watchdog
        interval: 25000
        onTriggered: if (player.active && !player.failed) player.trouble()
    }
    Connections {
        target: Tv
        // Failed: stop trying, and keep the controls up with the message.
        function onStateChanged() {
            if (player.failed) { media.stop(); watchdog.stop(); player.showControls() }
        }
    }

    // The mouse: moving shows the controls, a right-click goes back.
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.BackButton
        cursorShape: controls.shown ? Qt.ArrowCursor : Qt.BlankCursor
        onPositionChanged: player.showControls()
        onClicked: mouse => {
            if (mouse.button === Qt.RightButton || mouse.button === Qt.BackButton) Tv.stop()
            else player.showControls()
        }
        onWheel: wheel => player.changeVolume(wheel.angleDelta.y > 0 ? 0.05 : -0.05)
    }

    // ---- tracks ---------------------------------------------------------------------
    // A language by its code, as mpv gives it ("hin", "en"), in its own
    // words: हिन्दी, English. A code nothing knows is shown as it is.
    function languageName(code) {
        if (!code || code === "und") return ""
        var locale = Qt.locale(code)
        if (locale.name === "C") return code
        var name = locale.nativeLanguageName
        return name ? name.charAt(0).toUpperCase() + name.slice(1) : code
    }
    // What a track is called: its title and language where it has them. One
    // with neither is the channel's own sound.
    function trackName(track) {
        var title = track && track.title ? track.title : ""
        var language = track ? languageName(track.language) : ""
        if (title && language && title !== language) return title + " (" + language + ")"
        return title || language || qsTr("Main audio")
    }
    // The button a track list was opened from, to come back to when it was
    // reached with the keys or the pad. Opened with the mouse, the list hands
    // back to the picture, so the bar can fade.
    property Item trackOpener: null
    function openTracks(kind, anchor) {
        trackOpener = anchor.activeFocus ? anchor : null
        var tracks = kind === "audio" ? media.audioTracks : media.subtitleTracks
        var current = kind === "audio" ? media.audioTrack : media.subtitleTrack
        var entries = []
        if (kind === "subtitles")
            entries.push({ action: "0", label: (current <= 0 ? "✓  " : "     ") + qsTr("Off"), enabled: true })
        // One entry per name. A stream offered in several qualities carries
        // a copy of its sound in each, and a list of eight "Main audio"s is
        // one choice written eight times. The entry stands for the copy being
        // played when it is one of them, the first otherwise.
        var groups = []   // [{ name, id, ticked }], in first-seen order
        var byName = {}
        for (var i = 0; i < tracks.length; ++i) {
            var name = trackName(tracks[i])
            // Unnamed subtitles are numbered rather than merged: they are
            // not copies of one another the way unnamed sound is.
            if (kind === "subtitles" && name === qsTr("Main audio")) name = qsTr("Subtitles %1").arg(i + 1)
            if (!(name in byName)) {
                byName[name] = groups.length
                groups.push({ name: name, id: tracks[i].id, ticked: false })
            }
            if (tracks[i].id === current) {
                groups[byName[name]].id = tracks[i].id
                groups[byName[name]].ticked = true
            }
        }
        for (var n = 0; n < groups.length; ++n)
            entries.push({ action: String(groups[n].id),
                           label: (groups[n].ticked ? "✓  " : "     ") + groups[n].name, enabled: true })
        // Always opens, and says what there is: a list that does nothing when
        // pressed reads as broken, not as "this channel has one".
        if (kind === "audio" && tracks.length === 0)
            entries.push({ action: "none", label: "✓  " + qsTr("Main audio"), enabled: false })
        if (kind === "subtitles" && tracks.length === 0)
            entries.push({ action: "none", label: "     " + qsTr("This channel has none"), enabled: false })
        trackMenu.openFor(anchor, kind === "audio" ? qsTr("AUDIO") : qsTr("SUBTITLES"), entries, kind)
    }

    // ---- connecting ---------------------------------------------------------------
    Column {
        anchors.centerIn: parent
        spacing: 14
        visible: player.loading
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 44; height: 44; radius: 22
            color: "transparent"
            border.width: 4
            border.color: "#33FFFFFF"
            Rectangle {
                width: 12; height: 12; radius: 6
                color: Theme.accent
                x: parent.width / 2 - 6; y: -4
            }
            RotationAnimation on rotation {
                running: player.loading
                from: 0; to: 360; duration: 1000
                loops: Animation.Infinite
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: player.reconnecting ? qsTr("Reconnecting to %1 ...").arg(Tv.playingName)
                                      : qsTr("Tuning in to %1 ...").arg(Tv.playingName)
            color: "#DDFFFFFF"
            font.pixelSize: 16
        }
    }

    // ---- will not play ------------------------------------------------------------
    Rectangle {
        anchors.centerIn: parent
        visible: player.failed
        width: Math.min(parent.width - 80, 560)
        height: failColumn.implicitHeight + 48
        radius: 12
        color: Theme.card
        Column {
            id: failColumn
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 24 }
            spacing: 18
            Text {
                width: parent.width
                text: qsTr("This channel is not available right now")
                color: Theme.textPrimary
                font.pixelSize: 20
                wrapMode: Text.WordWrap
            }
            Text {
                width: parent.width
                text: qsTr("%1 may be offline, or only watchable in its own country.").arg(Tv.playingName)
                color: Theme.textSecondary
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }
            Row {
                spacing: 12
                ActionButton {
                    id: nextButton
                    text: qsTr("Next channel")
                    onActivated: Tv.changeChannel(1)
                    KeyNavigation.right: failBack
                }
                ActionButton {
                    id: failBack
                    text: qsTr("Back to channels")
                    onActivated: Tv.stop()
                    KeyNavigation.left: nextButton
                }
            }
        }
    }
    onFailedChanged: if (failed) nextButton.forceActiveFocus(); else if (active) player.toPicture()

    // ---- the bar ---------------------------------------------------------------------
    // One control in it: a rounded label that the mouse clicks and the keyboard
    // or the pad reaches with Left and Right.
    component BarButton: Rectangle {
        id: button
        property string text
        property bool lit: false
        property bool available: true
        signal activated()
        width: Math.max(44, label.implicitWidth + 28)
        height: 40
        radius: 20
        opacity: available ? 1 : 0.45
        color: lit ? Theme.accent : (hover.containsMouse || activeFocus ? "#40FFFFFF" : "#1FFFFFFF")
        border.width: activeFocus ? 2 : 0
        border.color: Theme.focusBorder
        Text { id: label; anchors.centerIn: parent; text: button.text; color: "white"; font.pixelSize: 15 }
        MouseArea {
            id: hover
            anchors.fill: parent
            hoverEnabled: true
            onClicked: if (button.available) { button.activated(); player.showControls() }
        }
        Keys.onReturnPressed: { if (available) activated(); player.showControls() }
        Keys.onEnterPressed: { if (available) activated(); player.showControls() }
        Keys.onSpacePressed: { if (available) activated(); player.showControls() }
    }

    Item {
        id: controls
        property bool shown: true
        anchors.fill: parent
        opacity: shown || player.loading || player.failed || trackMenu.visible ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 250 } }

        Rectangle {
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 84
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#CC000000" }
                GradientStop { position: 1.0; color: "#00000000" }
            }
        }

        Item {
            id: bar
            anchors { left: parent.left; right: parent.right; top: parent.top
                      leftMargin: 24; rightMargin: 24; topMargin: 16 }
            height: 40

            // The controls, in order, for Left and Right.
            readonly property var order: [backButton, audioButton, subtitleButton,
                                          quieter, volumeLevel, louder, favoriteButton]
            readonly property bool inside: {
                for (var i = 0; i < order.length; ++i) if (order[i].activeFocus) return true
                return false
            }
            function step(direction) {
                var at = -1
                for (var i = 0; i < order.length; ++i) if (order[i].activeFocus) at = i
                var next = Math.max(0, Math.min(order.length - 1, at + direction))
                order[next].forceActiveFocus()
                player.showControls()
            }

            Row {
                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                spacing: 16
                BarButton { id: backButton; text: qsTr("←  Back"); onActivated: Tv.stop() }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: Tv.playingName
                    color: "white"
                    font.pixelSize: 18
                    elide: Text.ElideRight
                    width: Math.min(implicitWidth, bar.width - rightControls.width - backButton.width - 60)
                }
            }

            Row {
                id: rightControls
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                spacing: 10
                BarButton {
                    id: audioButton
                    text: qsTr("Audio ▾")
                    onActivated: player.openTracks("audio", audioButton)
                }
                BarButton {
                    id: subtitleButton
                    text: media.subtitleTrack > 0 ? qsTr("Subtitles: on ▾") : qsTr("Subtitles ▾")
                    onActivated: player.openTracks("subtitles", subtitleButton)
                }
                Item { width: 8; height: 1 }
                BarButton { id: quieter; text: "−"; onActivated: player.changeVolume(-0.05) }
                BarButton {
                    id: volumeLevel
                    // Muted, or how loud; pressing it mutes and unmutes.
                    text: media.muted ? qsTr("Muted") : qsTr("Volume %1%").arg(Math.round(media.volume))
                    lit: media.muted
                    onActivated: media.muted = !media.muted
                }
                BarButton { id: louder; text: "+"; onActivated: player.changeVolume(0.05) }
                Item { width: 8; height: 1 }
                BarButton {
                    id: favoriteButton
                    text: "★"
                    lit: !!player.channel && !!player.channel.favorite
                    onActivated: Tv.toggleFavorite(Tv.playingIndex)
                }
            }

            // Inside the bar: Left and Right move, Esc goes back to the picture.
            Keys.onLeftPressed: step(-1)
            Keys.onRightPressed: step(1)
            Keys.onEscapePressed: { player.toPicture(); player.showControls() }
            Keys.onPressed: event => {
                if (event.key === Qt.Key_Back || event.key === Qt.Key_Backspace) {
                    player.toPicture(); event.accepted = true
                }
            }
        }

        // ---- what is on: now, how far in, and next --------------------------------
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 240
            visible: player.hasGuide && !player.failed
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#00000000" }
                GradientStop { position: 1.0; color: "#D9000000" }
            }
        }
        Column {
            id: onNow
            anchors { left: parent.left; bottom: parent.bottom; leftMargin: 40; bottomMargin: 56 }
            width: Math.min(parent.width - 80, 760)
            spacing: 8
            visible: player.hasGuide && !player.failed

            Row {
                spacing: 12
                visible: player.guide.now !== undefined
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("NOW")
                    color: Theme.accent
                    font.pixelSize: 13
                    font.bold: true
                    font.letterSpacing: 1.5
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: (player.guide.nowStart || "") + "  –  " + (player.guide.nowEnd || "")
                    color: "#CCFFFFFF"
                    font.pixelSize: 13
                }
            }
            Text {
                width: parent.width
                visible: player.guide.now !== undefined
                text: player.guide.now || ""
                color: "white"
                font.pixelSize: 26
                elide: Text.ElideRight
            }
            Rectangle {
                visible: player.guide.now !== undefined
                width: Math.min(parent.width, 420)
                height: 4
                radius: 2
                color: "#40FFFFFF"
                Rectangle {
                    width: parent.width * Math.max(0, Math.min(1, player.guide.progress || 0))
                    height: parent.height
                    radius: 2
                    color: Theme.accent
                }
            }
            Text {
                width: parent.width
                visible: text !== ""
                text: player.guide.nowDescription || ""
                color: "#CCFFFFFF"
                font.pixelSize: 14
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                visible: player.guide.next !== undefined
                text: qsTr("Next  %1   %2").arg(player.guide.nextStart || "").arg(player.guide.next || "")
                color: "#CCFFFFFF"
                font.pixelSize: 14
                elide: Text.ElideRight
            }
        }

        Text {
            anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 20 }
            text: Theme.hint(trackMenu.visible
                  ? (player.pad ? qsTr("▲ ▼ Move    %1 Choose    %2 Close")
                                      .arg(player.buttons.south).arg(player.buttons.east)
                                : qsTr("[↑] [↓] Move    [Enter] Choose    [Esc] Close"))
                  : bar.inside
                  ? (player.pad ? qsTr("◀ ▶ Move    %1 Choose    %2 Back to the picture")
                                      .arg(player.buttons.south).arg(player.buttons.east)
                                : qsTr("[←] [→] Move    [Enter] Choose    [Esc] Back to the picture"))
                  : player.pad
                    ? qsTr("▲ ▼ Volume    %1 %2 Channel    %3 More    %4 Back")
                          .arg(player.buttons.l1).arg(player.buttons.r1)
                          .arg(player.buttons.south).arg(player.buttons.east)
                    : qsTr("[↑] [↓] Volume    [PgUp] [PgDn] Channel    [M] Mute    [Enter] More    [Esc] Back"))
            color: "#CCFFFFFF"
            font.pixelSize: 13
            style: Text.Outline
            styleColor: "#99000000"
        }
    }
    Timer {
        id: hideTimer
        // Longer while something in the bar has the focus, for someone
        // deciding; then it fades all the same, and the keys go back to the
        // picture, as on a TV. An open list closes itself first (below).
        interval: bar.inside ? 8000 : 3500
        onTriggered: {
            if (trackMenu.visible) return
            if (bar.inside) player.toPicture()
            controls.shown = false
        }
    }

    MenuPanel {
        id: trackMenu
        anchors.fill: parent
        z: 50
        panelColor: "#A61A1826"
        idleTimeout: 8000
        onChosen: (action, context) => {
            if (context === "audio") media.audioTrack = Number(action)
            else if (context === "subtitles") media.subtitleTrack = Number(action)
        }
        onClosed: {
            if (player.trackOpener) player.trackOpener.forceActiveFocus()
            else player.toPicture()
            player.showControls()
        }
    }

    // ---- keys on the picture: the keyboard's, and the pad's, which arrive as keys ------
    Keys.onPressed: event => {
        switch (event.key) {
        case Qt.Key_Escape:
        case Qt.Key_Back:
        case Qt.Key_Backspace:
            Tv.stop(); break
        case Qt.Key_Up:
            player.changeVolume(0.05); break
        case Qt.Key_Down:
            player.changeVolume(-0.05); break
        case Qt.Key_M:
            media.muted = !media.muted; player.showControls(); break
        case Qt.Key_PageUp:
        case Qt.Key_Backtab:
        case Qt.Key_PageDown:
        case Qt.Key_Tab:
            // A key held down repeats; the channel changes once per press.
            if (!event.isAutoRepeat)
                Tv.changeChannel(event.key === Qt.Key_PageDown || event.key === Qt.Key_Tab ? 1 : -1)
            player.showControls(); break
        case Qt.Key_Return:
        case Qt.Key_Enter:
            // Into the bar, on Back, so the whole of it is a press away.
            player.showControls(); backButton.forceActiveFocus(); break
        case Qt.Key_F5:
            Tv.toggleFavorite(Tv.playingIndex); player.showControls(); break
        default:
            player.showControls(); return
        }
        event.accepted = true
    }
}

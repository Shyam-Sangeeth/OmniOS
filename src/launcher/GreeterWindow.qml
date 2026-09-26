// The OmniOS sign-in screen (omni-launcher-qml --greeter).
//
// Shown by greetd, full screen under cage, on a machine installed with "sign in
// automatically" turned off. One column in the middle: the time, whose account
// this is, the password, and which session to start in. Restart and Shut down
// sit in the corner, because the one thing everyone needs from a sign-in screen
// they cannot sign in to is a way to turn the machine off.
//
// Named GreeterWindow, not Greeter: a QML file is a type named after itself,
// and inside it that name wins over the "Greeter" context property — see
// InstallerWindow.qml for what that did once.
import QtQuick
import omnios

Window {
    id: window

    visible: true
    visibility: Window.FullScreen
    title: qsTr("Sign in to OmniOS")
    color: Theme.background

    property int userIndex: 0
    property string mode: "desktop"
    readonly property var user: Greeter.users.length > 0 ? Greeter.users[userIndex] : null
    property date now: new Date()

    Component.onCompleted: passwordField.input.forceActiveFocus()

    Connections {
        target: Greeter
        function onFocusWanted() { if (!window.activeFocusItem) passwordField.input.forceActiveFocus() }
        // A refused password is cleared and the box takes focus again, ready
        // for the next try.
        function onStateChanged() {
            if (!Greeter.busy && Greeter.error !== "") {
                passwordField.text = ""
                passwordField.input.forceActiveFocus()
            }
        }
    }

    Timer {
        interval: 1000
        repeat: true
        running: true
        onTriggered: window.now = new Date()
    }

    function signIn() {
        if (!user || Greeter.busy) return
        Greeter.signIn(user.name, passwordField.text, mode)
    }

    // ---- corner: the machine's name ------------------------------------------
    Row {
        anchors { left: parent.left; top: parent.top; margins: 40 }
        spacing: 14
        OmniLogo { diameter: 34; anchors.verticalCenter: parent.verticalCenter }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: Greeter.hostname
            color: Theme.textSecondary
            font.pixelSize: 15
            font.letterSpacing: 1
        }
    }

    // ---- the middle -----------------------------------------------------------
    Column {
        id: middle
        anchors.centerIn: parent
        width: Math.min(420, parent.width - 80)
        spacing: 18

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: Qt.formatTime(window.now, "hh:mm")
            color: Theme.textPrimary
            font.pixelSize: 84
            font.weight: Font.Light
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: Qt.formatDate(window.now, "dddd d MMMM")
            color: Theme.textSecondary
            font.pixelSize: 18
        }

        Item { width: 1; height: 24 }

        // Whose account. With more than one, Left and Right on this row pick.
        Item {
            id: userRow
            width: parent.width
            height: 64
            activeFocusOnTab: Greeter.users.length > 1
            KeyNavigation.down: passwordField.input
            Keys.onLeftPressed: pickUser(-1)
            Keys.onRightPressed: pickUser(1)

            function pickUser(step) {
                var count = Greeter.users.length
                if (count < 2) return
                window.userIndex = (window.userIndex + step + count) % count
                Greeter.clearError()
            }

            Rectangle {
                anchors.fill: parent
                radius: 10
                color: userRow.activeFocus ? "#2A2750" : "transparent"
                border.width: userRow.activeFocus ? 2 : 0
                border.color: Theme.focusBorder
            }
            Row {
                anchors.centerIn: parent
                spacing: 14
                Text {
                    visible: Greeter.users.length > 1
                    anchors.verticalCenter: parent.verticalCenter
                    text: "‹"
                    color: Theme.textSecondary
                    font.pixelSize: 28
                }
                Rectangle {
                    width: 44; height: 44; radius: 22
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.accent
                    Text {
                        anchors.centerIn: parent
                        text: window.user ? window.user.fullName.charAt(0).toUpperCase() : "?"
                        color: "#FFFFFF"
                        font.pixelSize: 20
                        font.weight: Font.DemiBold
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: window.user ? window.user.fullName : qsTr("No accounts")
                    color: Theme.textPrimary
                    font.pixelSize: 22
                }
                Text {
                    visible: Greeter.users.length > 1
                    anchors.verticalCenter: parent.verticalCenter
                    text: "›"
                    color: Theme.textSecondary
                    font.pixelSize: 28
                }
            }
        }

        Field {
            id: passwordField
            keyboard: fieldKeyboard
            width: parent.width
            secret: true
            label: qsTr("Password")
            upTo: Greeter.users.length > 1 ? userRow : null
            downTo: window.desktopChoice
            tabTo: window.desktopChoice
            onEdited: Greeter.clearError()
            onSubmitted: window.signIn()
            doneMovesOn: false
        }

        // Which session to start in: the same two the boot menu offers.
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 10

            Repeater {
                model: [
                    { mode: "desktop", label: qsTr("Desktop") },
                    { mode: "game", label: qsTr("Game Mode") }
                ]
                delegate: Rectangle {
                    id: choice
                    required property var modelData
                    required property int index
                    readonly property bool picked: window.mode === modelData.mode
                    width: 150
                    height: 40
                    radius: 20
                    color: picked ? Theme.accent : (activeFocus ? "#2A2750" : Theme.card)
                    border.width: activeFocus ? 2 : 1
                    border.color: activeFocus ? Theme.focusBorder : "#26FFFFFF"
                    activeFocusOnTab: true

                    Text {
                        anchors.centerIn: parent
                        text: choice.modelData.label
                        color: Theme.textPrimary
                        font.pixelSize: 15
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: { choice.forceActiveFocus(); window.mode = choice.modelData.mode }
                    }
                    Keys.onReturnPressed: window.mode = modelData.mode
                    Keys.onSpacePressed: window.mode = modelData.mode
                    Keys.onLeftPressed: if (index > 0) window.desktopChoice.forceActiveFocus()
                    Keys.onRightPressed: if (index === 0) window.gameChoice.forceActiveFocus()
                    Keys.onUpPressed: passwordField.input.forceActiveFocus()
                    Keys.onDownPressed: signInButton.forceActiveFocus()

                    Component.onCompleted: {
                        if (index === 0) window.desktopChoice = choice
                        else window.gameChoice = choice
                    }
                }
            }
        }

        ActionButton {
            id: signInButton
            width: parent.width
            text: Greeter.busy ? qsTr("Signing in ...") : qsTr("Sign in")
            available: !Greeter.busy && window.user !== null
            onActivated: window.signIn()
            KeyNavigation.up: window.desktopChoice
            // Down from signing in is the corner with Restart and Shut down,
            // so a controller can reach them.
            KeyNavigation.down: restartButton
            KeyNavigation.tab: restartButton
        }

        // Room for two lines whether or not there is anything to say. The
        // column is centred, so a message that took space only when it
        // appeared moved the password box out from under the typing.
        Text {
            width: parent.width
            height: 44
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignTop
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
            text: Greeter.error
            color: Theme.danger
            font.pixelSize: 15
        }
    }

    // The two session choices, for the navigation above.
    property Item desktopChoice: null
    property Item gameChoice: null

    // ---- corner: turning the machine off ---------------------------------------
    Row {
        anchors { right: parent.right; bottom: parent.bottom; margins: 40 }
        spacing: 12
        ActionButton {
            id: restartButton
            text: qsTr("Restart")
            onActivated: Greeter.powerAction("reboot")
            KeyNavigation.right: shutdownButton
            KeyNavigation.up: signInButton
            Keys.onEscapePressed: passwordField.input.forceActiveFocus()
        }
        ActionButton {
            id: shutdownButton
            text: qsTr("Shut down")
            onActivated: Greeter.powerAction("poweroff")
            KeyNavigation.left: restartButton
            KeyNavigation.up: signInButton
            KeyNavigation.tab: passwordField.input
            Keys.onEscapePressed: passwordField.input.forceActiveFocus()
        }
    }

    // Typing with a controller: the fields above open this on A (✕).
    FieldKeyboard {
        id: fieldKeyboard
        inputMode: Greeter.input
    }
}

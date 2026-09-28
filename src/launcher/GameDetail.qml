// Game detail screen (OmniOS.md §12). Says what will actually run and at which
// tier, so "why is this slow" and "why will this not start" are answered before
// the user presses Play rather than after.
import QtQuick
import omnios

Item {
    id: detail

    property var game: null
    signal closed()
    signal played()

    // Asked of the launcher, not read from `game`: that is the game list's
    // snapshot, and an emulator installed from this page changes the answer.
    // Asked again whenever an install starts or ends.
    readonly property string gameId: game ? game.gameId : ""
    readonly property bool playable: gameId !== "" && (Launcher.packageBusy, Launcher.isPlayable(gameId))
    readonly property var missing: gameId !== "" ? (Launcher.packageBusy, Launcher.missingEngine(gameId)) : ({})
    readonly property bool canInstall: missing.app !== undefined && !Launcher.packageBusy
    // A one-time step before it can play, such as the PS3's system software.
    readonly property var setup: gameId !== "" ? (Launcher.packageBusy, Launcher.setupStep(gameId)) : ({})
    readonly property bool canSetUp: setup.label !== undefined && !Launcher.packageBusy
    function primary() {
        if (playable) played()
        else if (canInstall) Launcher.installEngine(gameId)
        else if (canSetUp) Launcher.runSetupStep(gameId)
    }

    focus: visible

    Rectangle {
        anchors.fill: parent
        color: "#F20A0A12"
    }

    // Slides up from the bottom (§12: 300ms ease-out).
    Item {
        id: sheet
        anchors.fill: parent
        y: detail.visible ? 0 : 40
        opacity: detail.visible ? 1 : 0
        Behavior on y { NumberAnimation { duration: Theme.slideDuration; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: Theme.slideDuration } }

        Column {
            anchors.centerIn: parent
            width: Math.min(parent.width - 160, 900)
            spacing: 18

            Rectangle {
                width: parent.width
                height: 200
                radius: 8
                color: Theme.card
                clip: true

                Rectangle {
                    anchors.fill: parent
                    visible: !detail.game || detail.game.cover === ""
                    gradient: Gradient {
                        GradientStop {
                            position: 0.0
                            color: detail.game ? Qt.darker(detail.game.badgeColor, 2.2) : Theme.card
                        }
                        GradientStop { position: 1.0; color: Theme.card }
                    }
                }
                Image {
                    anchors.fill: parent
                    source: detail.game ? detail.game.cover : ""
                    visible: detail.game && detail.game.cover !== ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }
            }

            Row {
                spacing: 14
                Text {
                    text: detail.game ? detail.game.title : ""
                    color: Theme.textPrimary
                    font.pixelSize: 34
                }
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: platformLabel.implicitWidth + 16
                    height: 22
                    radius: 4
                    color: detail.game ? detail.game.badgeColor : Theme.textSecondary
                    Text {
                        id: platformLabel
                        anchors.centerIn: parent
                        text: detail.game ? detail.game.platformName : ""
                        color: "#FFFFFF"
                        font.pixelSize: 12
                        font.bold: true
                    }
                }
            }

            Rectangle { width: parent.width; height: 1; color: "#22FFFFFF" }

            Text {
                width: parent.width
                text: {
                    if (!detail.game) return ""
                    // Naming the tier is honest about what to expect: an
                    // emulated title is not going to feel like a native one.
                    var line = qsTr("Execution: %1  ·  %2").arg(detail.game.engineName).arg(detail.game.tierName)
                    if (detail.game.sizeText !== "") line += "  ·  " + detail.game.sizeText
                    if (detail.game.lastPlayed !== "") line += "  ·  " + detail.game.lastPlayed
                    return line
                }
                color: Theme.textSecondary
                font.pixelSize: 15
            }

            Text {
                width: parent.width
                visible: detail.game && detail.game.description !== ""
                text: detail.game ? detail.game.description : ""
                color: Theme.textSecondary
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }

            Text {
                width: parent.width
                visible: detail.game && !detail.playable
                text: !detail.game ? ""
                      : detail.missing.app !== undefined
                        ? qsTr("%1 plays this, and it is not installed yet. Install gets it from Flathub  -  a few minutes the first time, and nothing to set up.")
                              .arg(detail.missing.name)
                        : Launcher.launchProblem(detail.gameId) || qsTr("No execution layer available for this title.")
                color: "#E4A000"
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }

            Row {
                spacing: 12

                // Play, or what it takes to be able to: Install, then Play.
                Rectangle {
                    readonly property bool active: detail.playable || detail.canInstall || detail.canSetUp
                    width: Math.max(160, primaryLabel.implicitWidth + 40); height: 44; radius: 6
                    color: active ? Theme.accent : Theme.card
                    opacity: active ? 1.0 : 0.5
                    Text {
                        id: primaryLabel
                        anchors.centerIn: parent
                        text: detail.playable ? qsTr("PLAY")
                              : detail.missing.app !== undefined
                                ? (Launcher.packageBusy ? qsTr("INSTALLING ...") : qsTr("INSTALL %1").arg(detail.missing.name.toUpperCase()))
                                : detail.setup.label !== undefined
                                  ? (Launcher.packageBusy ? qsTr("WAITING ...") : detail.setup.label.toUpperCase())
                                  : qsTr("PLAY")
                        color: "#FFFFFF"
                        font.pixelSize: 16
                        font.bold: true
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: parent.active
                        onClicked: detail.primary()
                    }
                }

                Rectangle {
                    width: 120; height: 44; radius: 6
                    color: Theme.card
                    Text {
                        anchors.centerIn: parent
                        text: qsTr("Back")
                        color: Theme.textPrimary
                        font.pixelSize: 15
                    }
                    MouseArea { anchors.fill: parent; onClicked: detail.closed() }
                }
            }

            // How an install is going, here where it was asked for.
            Text {
                width: parent.width
                visible: (detail.missing.app !== undefined || detail.setup.label !== undefined) && Launcher.packageStatus !== ""
                text: Launcher.packageStatus
                color: Theme.textSecondary
                font.pixelSize: 14
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: detail.game ? Launcher.launchCommand(detail.game.gameId) : ""
                color: "#55FFFFFF"
                font.pixelSize: 11
                font.family: "monospace"
                elide: Text.ElideRight
            }
        }
    }

    Keys.onPressed: function (event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace) {
            detail.closed(); event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            detail.primary()
            event.accepted = true
        }
    }
}

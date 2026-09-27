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
                visible: detail.game && !detail.game.playable
                text: detail.game ? Launcher.installHint(detail.game.gameId) !== ""
                                    ? qsTr("Not installed. Install with:  %1").arg(Launcher.installHint(detail.game.gameId))
                                    : qsTr("No execution layer available for this title.")
                                  : ""
                color: "#E4A000"
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }

            Row {
                spacing: 12

                Rectangle {
                    width: 160; height: 44; radius: 6
                    color: detail.game && detail.game.playable ? Theme.accent : Theme.card
                    opacity: detail.game && detail.game.playable ? 1.0 : 0.5
                    Text {
                        anchors.centerIn: parent
                        text: qsTr("PLAY")
                        color: "#FFFFFF"
                        font.pixelSize: 16
                        font.bold: true
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: detail.game && detail.game.playable
                        onClicked: detail.played()
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
            if (detail.game && detail.game.playable) detail.played()
            event.accepted = true
        }
    }
}

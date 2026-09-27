// "Install OmniOS" — the screen in front of omni-install (Phase 13.2).
//
// One screen per step: choose a disk, the language and keyboard, make the account,
// pick the time zone, confirm, watch, restart. It
// runs from the same binary as the launcher (omni-launcher-qml --install) so
// it has the same look and the same controller support: on a console the
// person installing may well be holding a gamepad, not a mouse.
//
// The file is InstallerWindow.qml, not Installer.qml, and that is not taste. A
// QML file is a type named after itself, and inside it that type name wins
// over a context property of the same name — so in "Installer.qml",
// "Installer.refresh()" asked the window type for an attached property and got
// undefined, every "Installer.error !== ''" was true, and the installer opened
// on its failure page before anything had been tried.
//
// The one decision that destroys data is made on the confirm screen, and that
// screen opens with Back focused, not Erase. A disk with anything on it — files,
// or a whole operating system — also needs "erase" typed before the red button
// does anything at all. Getting there takes a disk chosen
// on purpose and then a second, separate press on the red button — a press
// that lands by accident or a controller button held down cannot do it.
import QtQuick
import omnios

Window {
    id: window

    width: 1000
    height: 680
    minimumWidth: 720
    minimumHeight: 540
    visible: true
    visibility: Installer.fullscreen ? Window.FullScreen : Window.Windowed
    title: qsTr("Install OmniOS")
    color: Theme.background

    readonly property color danger: Theme.danger

    // The disk picked from the list: { path, model, size, transport, contents }.
    property var chosen: null
    // Where the steps before the install have got to: "choose", "language",
    // "account", "timezone" or "confirm".
    property string step: "choose"

    // The account, as the account screen leaves it. skipAccount keeps the live
    // image's own passwordless "omni", for anyone who would rather not spell
    // out a name and password on the on-screen keyboard.
    property bool skipAccount: false
    property bool usernameEdited: false
    property bool hostnameEdited: false
    property string accountProblem: ""
    property string timezone: "Asia/Kolkata"
    // The installed system's language, and its keyboard layout (an xkb id).
    // The layout starts as whatever the live session has.
    property string language: "en_IN"
    property string keyboard: Installer.currentKeyboard
    // Chosen by hand, after which picking a language stops suggesting one.
    property bool keyboardPicked: false

    // The chosen disk holds files or an operating system, so erasing it has
    // to be typed for; and whether it has been.
    readonly property bool holdsSomething: !!chosen && (!!chosen.os || !!chosen.contents)
    readonly property bool eraseAllowed: !holdsSomething
                                         || eraseField.text.trim().toLowerCase() === "erase"

    readonly property string page: Installer.busy ? "installing"
                                 : Installer.finished ? "done"
                                 : Installer.error ? "failed"
                                 : step

    onPageChanged: focusPage()

    Component.onCompleted: {
        Installer.refresh()
        focusPage()
    }

    // Closing mid-install would kill omni-install with the disk half written.
    onClosing: close => { if (Installer.busy) close.accepted = false }

    Connections {
        target: Installer
        function onFocusWanted() { if (!window.activeFocusItem) window.focusPage() }
        // A disk can be listed after the page opened; land on it.
        function onDisksChanged() { if (window.page === "choose") window.focusPage() }
    }

    // Someone may plug a disk in after opening this. The list follows.
    Timer {
        interval: 5000
        repeat: true
        running: window.page === "choose"
        onTriggered: Installer.refresh()
    }

    function focusPage() {
        if (page === "choose") {
            if (diskList.count > 0) diskList.forceActiveFocus()
            else closeButton.forceActiveFocus()
        } else if (page === "language") {
            languageSearch.text = ""
            keyboardSearch.text = ""
            languageList.showChosen()
            keyboardList.showChosen()
            languageList.forceActiveFocus()
        } else if (page === "account") {
            nameField.input.forceActiveFocus()
        } else if (page === "timezone") {
            // On the zone already chosen (India's, unless changed), so A
            // there keeps it and moves on rather than picking whatever row
            // happens to be first.
            zoneSearch.text = ""
            for (var i = 0; i < zonePage.zones.length; ++i) {
                if (zonePage.zones[i].id === timezone) { zoneList.currentIndex = i; break }
            }
            zoneList.forceActiveFocus()
            zoneList.positionViewAtIndex(zoneList.currentIndex, ListView.Center)
        } else if (page === "confirm") {
            backButton.forceActiveFocus()
        } else if (page === "installing") {
            installingPage.forceActiveFocus()
        } else if (page === "done") {
            restartButton.forceActiveFocus()
        } else if (page === "failed") {
            retryButton.forceActiveFocus()
        }
    }

    // ---- building blocks ---------------------------------------------------

    component Heading: Column {
        property string title
        property string subtitle
        spacing: 10
        width: parent ? parent.width : 0

        Text {
            text: parent.title
            color: Theme.textPrimary
            font.pixelSize: 30
            font.weight: Font.DemiBold
            width: parent.width
            wrapMode: Text.WordWrap
        }
        Text {
            text: parent.subtitle
            visible: text.length > 0
            color: Theme.textSecondary
            font.pixelSize: 16
            lineHeight: 1.3
            width: parent.width
            wrapMode: Text.WordWrap
        }
    }

    // On/off, driven by Enter, Space or a click.
    component Switch: Rectangle {
        id: toggle
        property string text
        property string detail
        property bool checked: true
        implicitHeight: 64
        radius: 8
        color: activeFocus ? "#2A2750" : Theme.card
        border.width: activeFocus ? 2 : 1
        border.color: activeFocus ? Theme.focusBorder : "#26FFFFFF"

        Column {
            anchors {
                left: parent.left; leftMargin: 16
                right: knob.left; rightMargin: 16
                verticalCenter: parent.verticalCenter
            }
            spacing: 4
            Text { text: toggle.text; color: Theme.textPrimary; font.pixelSize: 16 }
            Text {
                text: toggle.detail
                color: Theme.textSecondary
                font.pixelSize: 12
                width: parent.width
                elide: Text.ElideRight
            }
        }
        Rectangle {
            id: knob
            anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
            width: 44; height: 24; radius: 12
            color: toggle.checked ? Theme.accent : "#3A3A4A"
            Rectangle {
                width: 18; height: 18; radius: 9
                y: 3
                x: toggle.checked ? parent.width - width - 3 : 3
                color: "#FFFFFF"
                Behavior on x { NumberAnimation { duration: Theme.focusDuration } }
            }
        }
        MouseArea {
            anchors.fill: parent
            onClicked: { toggle.forceActiveFocus(); toggle.checked = !toggle.checked }
        }
        Keys.onReturnPressed: checked = !checked
        Keys.onEnterPressed: checked = !checked
        Keys.onSpacePressed: checked = !checked
    }

    // "Samsung SSD 980 · 1.0 TB · NVMe"
    function describe(disk) {
        if (!disk) return ""
        var parts = [disk.model, disk.size]
        if (disk.transport) parts.push(disk.transport)
        return parts.join("  ·  ")
    }

    // ---- frame ---------------------------------------------------------------

    Item {
        anchors.fill: parent
        anchors.margins: 48

        Row {
            id: brand
            spacing: 14
            OmniLogo { diameter: 36; anchors.verticalCenter: parent.verticalCenter }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Install OmniOS")
                color: Theme.textSecondary
                font.pixelSize: 15
                font.letterSpacing: 2
                font.capitalization: Font.AllUppercase
            }
        }

        Item {
            id: body
            anchors {
                top: brand.bottom; topMargin: 36
                left: parent.left; right: parent.right; bottom: parent.bottom
            }

            // ---- 1. choose ---------------------------------------------------
            Item {
                anchors.fill: parent
                visible: window.page === "choose"

                Heading {
                    id: chooseHeading
                    title: qsTr("Choose a disk for OmniOS")
                    subtitle: qsTr("OmniOS takes a whole disk for itself. Everything already on the disk you choose will be erased.")
                }

                ListView {
                    id: diskList
                    anchors {
                        top: chooseHeading.bottom; topMargin: 28
                        left: parent.left; right: parent.right
                        bottom: chooseButtons.top; bottomMargin: 24
                    }
                    spacing: 10
                    clip: true
                    model: Installer.disks
                    keyNavigationEnabled: true
                    highlightMoveDuration: Theme.focusDuration

                    delegate: Rectangle {
                        id: diskRow
                        required property var modelData
                        required property int index
                        readonly property bool current: diskList.activeFocus && ListView.isCurrentItem

                        width: diskList.width
                        height: diskRow.modelData.os ? 112 : 88
                        radius: 10
                        color: current ? "#2A2750" : Theme.card
                        border.width: current ? 2 : 1
                        border.color: current ? Theme.focusBorder : "#1FFFFFFF"

                        Column {
                            anchors {
                                left: parent.left; leftMargin: 22
                                right: parent.right; rightMargin: 22
                                verticalCenter: parent.verticalCenter
                            }
                            spacing: 6
                            Text {
                                text: window.describe(diskRow.modelData)
                                color: Theme.textPrimary
                                font.pixelSize: 18
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                                width: parent.width
                            }
                            Text {
                                text: diskRow.modelData.contents
                                      ? qsTr("%1  ·  has %2").arg(diskRow.modelData.path).arg(diskRow.modelData.contents)
                                      : qsTr("%1  ·  nothing recognisable on it").arg(diskRow.modelData.path)
                                color: Theme.textSecondary
                                font.pixelSize: 14
                                elide: Text.ElideRight
                                width: parent.width
                            }
                            Text {
                                visible: !!diskRow.modelData.os
                                text: qsTr("\u26a0  %1 is installed on this disk").arg(diskRow.modelData.os)
                                color: window.danger
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                diskList.currentIndex = diskRow.index
                                diskList.forceActiveFocus()
                                window.pick(diskRow.modelData)
                            }
                        }
                    }

                    Keys.onReturnPressed: window.pick(Installer.disks[currentIndex])
                    Keys.onEnterPressed: window.pick(Installer.disks[currentIndex])
                    Keys.onEscapePressed: Qt.quit()
                    // Below the last disk is the Close button.
                    Keys.onDownPressed: event => {
                        if (currentIndex === count - 1) closeButton.forceActiveFocus()
                        else event.accepted = false
                    }
                }

                Text {
                    anchors.centerIn: diskList
                    width: Math.min(parent.width, 640)
                    visible: diskList.count === 0
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    font.pixelSize: 16
                    lineHeight: 1.3
                    text: !Installer.listed
                          ? qsTr("Looking for disks ...")
                          : qsTr("There is no disk OmniOS can go on. It needs a disk of at least %1, and the USB stick it is running from is never offered. Plug a disk in and it will appear here.")
                                .arg(Installer.minimumSize)
                }

                Row {
                    id: chooseButtons
                    anchors { right: parent.right; bottom: parent.bottom }
                    spacing: 14
                    ActionButton {
                        id: closeButton
                        text: qsTr("Close")
                        onActivated: Qt.quit()
                        Keys.onUpPressed: if (diskList.count > 0) diskList.forceActiveFocus()
                        Keys.onEscapePressed: Qt.quit()
                    }
                }
            }

            // ---- 2. account ---------------------------------------------------
            Item {
                anchors.fill: parent
                visible: window.page === "account"

                Heading {
                    id: accountHeading
                    title: qsTr("Create your account")
                    subtitle: qsTr("This is who OmniOS signs in as. The password is what it asks for when it needs to be sure it is you.")
                }

                Grid {
                    id: accountGrid
                    anchors { top: accountHeading.bottom; topMargin: 24; left: parent.left; right: parent.right }
                    columns: 2
                    columnSpacing: 28
                    rowSpacing: 16
                    readonly property real cell: (width - columnSpacing) / 2

                    Field {
                        id: nameField
                        keyboard: fieldKeyboard
                        width: accountGrid.cell
                        label: qsTr("Your name")
                        onEdited: {
                            if (!window.usernameEdited) usernameField.text = Installer.suggestUsername(text)
                            if (!window.hostnameEdited) hostnameField.text = window.suggestHostname()
                            window.recheck()
                        }
                        tabTo: usernameField.input
                        downTo: usernameField.input
                    }
                    Field {
                        id: passwordField
                        keyboard: fieldKeyboard
                        width: accountGrid.cell
                        label: qsTr("Password")
                        secret: true
                        onEdited: window.recheck()
                        invalid: window.accountProblem !== "" && text === ""
                        tabTo: confirmField.input
                        downTo: confirmField.input
                    }
                    Field {
                        id: usernameField
                        keyboard: fieldKeyboard
                        width: accountGrid.cell
                        label: qsTr("Username")
                        invalid: window.accountProblem !== "" && Installer.usernameProblem(text) !== ""
                        onEdited: {
                            window.usernameEdited = text !== ""
                            if (!window.hostnameEdited) hostnameField.text = window.suggestHostname()
                            window.recheck()
                        }
                        tabTo: hostnameField.input
                        upTo: nameField.input
                        downTo: hostnameField.input
                    }
                    Field {
                        id: confirmField
                        keyboard: fieldKeyboard
                        width: accountGrid.cell
                        label: qsTr("Password again")
                        secret: true
                        onEdited: window.recheck()
                        invalid: window.accountProblem !== "" && text !== passwordField.text
                        tabTo: autologinSwitch
                        upTo: passwordField.input
                        downTo: autologinSwitch
                    }
                    Field {
                        id: hostnameField
                        keyboard: fieldKeyboard
                        width: accountGrid.cell
                        label: qsTr("Computer name")
                        text: "omnios"
                        invalid: window.accountProblem !== "" && !window.validHostname(text)
                        onEdited: {
                            window.hostnameEdited = text !== ""
                            window.recheck()
                        }
                        tabTo: passwordField.input
                        upTo: usernameField.input
                        downTo: accountNext
                    }
                    Switch {
                        id: autologinSwitch
                        width: accountGrid.cell
                        text: qsTr("Sign in automatically")
                        detail: checked ? qsTr("Starts straight into OmniOS, like a console")
                                        : qsTr("Asks for the password every time it starts")
                        KeyNavigation.tab: accountNext
                        KeyNavigation.up: confirmField.input
                        KeyNavigation.down: accountNext
                    }
                }

                Text {
                    anchors { top: accountGrid.bottom; topMargin: 18; left: parent.left; right: parent.right }
                    text: window.accountProblem
                    visible: text.length > 0
                    color: window.danger
                    font.pixelSize: 15
                    wrapMode: Text.WordWrap
                }

                Row {
                    id: accountButtons
                    anchors { right: parent.right; bottom: parent.bottom }
                    spacing: 14
                    ActionButton {
                        id: accountBack
                        text: qsTr("Back")
                        onActivated: window.step = "language"
                        KeyNavigation.right: accountSkip
                        Keys.onEscapePressed: window.step = "language"
                    }
                    ActionButton {
                        id: accountSkip
                        text: qsTr("Skip")
                        onActivated: {
                            window.skipAccount = true
                            window.accountProblem = ""
                            window.step = "timezone"
                        }
                        KeyNavigation.left: accountBack
                        KeyNavigation.right: accountNext
                        Keys.onEscapePressed: window.step = "language"
                    }
                    ActionButton {
                        id: accountNext
                        text: qsTr("Next")
                        onActivated: window.acceptAccount()
                        KeyNavigation.left: accountSkip
                        KeyNavigation.up: autologinSwitch
                        Keys.onEscapePressed: window.step = "language"
                    }
                }

                // Why Skip is there, for someone holding only a controller.
                Text {
                    anchors {
                        left: parent.left; bottom: parent.bottom
                        right: accountButtons.left; rightMargin: 24
                    }
                    height: 48
                    verticalAlignment: Text.AlignVCenter
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    text: qsTr("No keyboard? %1 on a box types with the controller, or Skip keeps an account named \"omni\" with no password, as on this USB stick.")
                              .arg(Installer.input.buttonNames.south)
                }

                // Enter on any field is Next, as on any form.
                Keys.onReturnPressed: window.acceptAccount()
                Keys.onEnterPressed: window.acceptAccount()
                Keys.onEscapePressed: window.step = "language"
            }

            // ---- 1b. language and keyboard -----------------------------------
            // Before the account, because of the keyboard: the layout applies
            // at once, so the password typed on the next page is typed as it
            // will be at every sign-in afterwards.
            Item {
                id: languagePage
                anchors.fill: parent
                visible: window.page === "language"
                Keys.onEscapePressed: window.step = "choose"

                readonly property var languages: window.filtered(Installer.languages, languageSearch.text,
                                                                 ["name", "english", "id"])
                readonly property var keyboards: window.filtered(Installer.keyboards, keyboardSearch.text,
                                                                 ["name", "id"])

                Heading {
                    id: languageHeading
                    title: qsTr("Language and keyboard")
                    subtitle: qsTr("The language OmniOS and its apps use, and the keyboard you type on. The keyboard changes now, so the password you choose next types the same once OmniOS is installed.")
                }

                Item {
                    id: languageColumn
                    anchors {
                        top: languageHeading.bottom; topMargin: 20
                        left: parent.left; bottom: languageButtons.top; bottomMargin: 20
                    }
                    width: (parent.width - 28) / 2

                    Field {
                        id: languageSearch
                        keyboard: fieldKeyboard
                        width: parent.width
                        label: qsTr("Search languages")
                        onEdited: languageList.currentIndex = 0
                        downTo: languageList
                        tabTo: keyboardSearch.input
                        onSubmitted: languageList.forceActiveFocus()
                    }
                    ChoiceList {
                        id: languageList
                        anchors {
                            top: languageSearch.bottom; topMargin: 12
                            left: parent.left; right: parent.right; bottom: parent.bottom
                        }
                        model: languagePage.languages
                        chosen: window.language
                        detailRole: "english"
                        search: languageSearch
                        empty: qsTr("No language matches \"%1\"").arg(languageSearch.text)
                        onPicked: window.pickLanguage()
                        onRightWanted: keyboardList.forceActiveFocus()
                        onDownWanted: languageNext.forceActiveFocus()
                        onBackWanted: window.step = "choose"
                    }
                }

                Item {
                    id: keyboardColumn
                    anchors {
                        top: languageHeading.bottom; topMargin: 20
                        right: parent.right; bottom: languageButtons.top; bottomMargin: 20
                    }
                    width: (parent.width - 28) / 2

                    Field {
                        id: keyboardSearch
                        keyboard: fieldKeyboard
                        width: parent.width
                        label: qsTr("Search keyboards")
                        onEdited: keyboardList.currentIndex = 0
                        downTo: keyboardList
                        tabTo: keyboardList
                        onSubmitted: keyboardList.forceActiveFocus()
                    }
                    ChoiceList {
                        id: keyboardList
                        anchors {
                            top: keyboardSearch.bottom; topMargin: 12
                            left: parent.left; right: parent.right; bottom: parent.bottom
                        }
                        model: languagePage.keyboards
                        chosen: window.keyboard
                        detailRole: "id"
                        search: keyboardSearch
                        empty: qsTr("No keyboard matches \"%1\"").arg(keyboardSearch.text)
                        onPicked: window.pickKeyboard()
                        onLeftWanted: languageList.forceActiveFocus()
                        onDownWanted: languageNext.forceActiveFocus()
                        onBackWanted: window.step = "choose"
                    }
                }

                // A layout without Latin letters comes second, and says so:
                // otherwise the next page's username could not be typed.
                Text {
                    anchors {
                        left: parent.left; right: languageButtons.left; rightMargin: 24
                        verticalCenter: languageButtons.verticalCenter
                    }
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    text: Installer.isLatinKeyboard(window.keyboard)
                          ? qsTr("%1  ·  %2").arg(window.languageName(window.language))
                                              .arg(window.keyboardName(window.keyboard))
                          : qsTr("%1 comes after English (US), which usernames are typed in. Meta+Alt+K switches between them.")
                                .arg(window.keyboardName(window.keyboard))
                }

                Row {
                    id: languageButtons
                    anchors { right: parent.right; bottom: parent.bottom }
                    spacing: 14
                    ActionButton {
                        id: languageBack
                        text: qsTr("Back")
                        onActivated: window.step = "choose"
                        KeyNavigation.right: languageNext
                        KeyNavigation.up: languageList
                        Keys.onEscapePressed: window.step = "choose"
                    }
                    ActionButton {
                        id: languageNext
                        text: qsTr("Next")
                        onActivated: window.step = "account"
                        KeyNavigation.left: languageBack
                        KeyNavigation.up: keyboardList
                        Keys.onEscapePressed: window.step = "choose"
                    }
                }
            }

            // ---- 3. time zone ------------------------------------------------
            Item {
                id: zonePage
                anchors.fill: parent
                visible: window.page === "timezone"
                Keys.onEscapePressed: window.step = "account"

                // Everything the search leaves, as the list shows it.
                readonly property var zones: {
                    var all = Installer.timeZones
                    var q = zoneSearch.text.trim().toLowerCase().replace(/ /g, "_")
                    if (q === "") return all
                    return all.filter(function (z) { return z.id.toLowerCase().indexOf(q) >= 0 })
                }

                Heading {
                    id: zoneHeading
                    title: qsTr("Choose your time zone")
                    subtitle: qsTr("It sets the clock. Type a city to find it, or scroll; %1 and %2 on a controller jump a region at a time.")
                              .arg(Installer.input.buttonNames.l1).arg(Installer.input.buttonNames.r1)
                }

                Field {
                    id: zoneSearch
                    keyboard: fieldKeyboard
                    anchors { top: zoneHeading.bottom; topMargin: 20; left: parent.left }
                    width: Math.min(parent.width, 420)
                    label: qsTr("Search")
                    onEdited: zoneList.currentIndex = 0
                    downTo: zoneList
                    tabTo: zoneList
                    onSubmitted: zoneList.forceActiveFocus()
                }

                ListView {
                    id: zoneList
                    anchors {
                        top: zoneSearch.bottom; topMargin: 16
                        left: parent.left; right: parent.right
                        bottom: zoneButtons.top; bottomMargin: 20
                    }
                    clip: true
                    spacing: 4
                    model: zonePage.zones
                    keyNavigationEnabled: true
                    highlightMoveDuration: 0
                    currentIndex: 0

                    delegate: Rectangle {
                        id: zoneRow
                        required property var modelData
                        required property int index
                        readonly property bool current: ListView.isCurrentItem
                        readonly property bool picked: modelData.id === window.timezone
                        width: zoneList.width
                        height: 40
                        radius: 6
                        color: current && zoneList.activeFocus ? "#2A2750" : "transparent"
                        border.width: current && zoneList.activeFocus ? 2 : 0
                        border.color: Theme.focusBorder

                        Text {
                            anchors { left: parent.left; leftMargin: 14; verticalCenter: parent.verticalCenter }
                            text: (zoneRow.picked ? "\u2713  " : "") + zoneRow.modelData.city
                                  + (zoneRow.modelData.region ? "   \u00b7   " + zoneRow.modelData.region : "")
                            color: zoneRow.picked ? Theme.textPrimary : "#C8C8D8"
                            font.pixelSize: 15
                            font.weight: zoneRow.picked ? Font.DemiBold : Font.Normal
                        }
                        Text {
                            anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                            text: zoneRow.modelData.offset
                            color: Theme.textSecondary
                            font.pixelSize: 13
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                zoneList.currentIndex = zoneRow.index
                                zoneList.forceActiveFocus()
                                window.timezone = zoneRow.modelData.id
                            }
                        }
                    }

                    // Enter picks; picking again moves on, so a controller gets
                    // through with A, A.
                    Keys.onReturnPressed: window.pickZone()
                    Keys.onEnterPressed: window.pickZone()
                    Keys.onEscapePressed: window.step = "account"
                    Keys.onUpPressed: event => {
                        if (currentIndex === 0) zoneSearch.input.forceActiveFocus()
                        else event.accepted = false
                    }
                    Keys.onDownPressed: event => {
                        if (currentIndex === count - 1) zoneNext.forceActiveFocus()
                        else event.accepted = false
                    }
                    // A region at a time: the controller's shoulder buttons
                    // arrive as Tab and Backtab.
                    Keys.onTabPressed: window.jumpRegion(1)
                    Keys.onBacktabPressed: window.jumpRegion(-1)
                    // Typing while the list has focus searches, as it would
                    // anywhere else.
                    Keys.onPressed: event => {
                        if (event.text.length === 1 && event.text.trim() !== "" && !(event.modifiers & Qt.ControlModifier)) {
                            zoneSearch.input.forceActiveFocus()
                            zoneSearch.text += event.text
                            zoneList.currentIndex = 0
                            event.accepted = true
                        }
                    }
                }

                Text {
                    anchors.centerIn: zoneList
                    visible: zoneList.count === 0
                    text: qsTr("No time zone matches \"%1\"").arg(zoneSearch.text)
                    color: Theme.textSecondary
                    font.pixelSize: 15
                }

                Row {
                    id: zoneButtons
                    anchors { right: parent.right; bottom: parent.bottom }
                    spacing: 14
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Chosen: %1").arg(window.timezone)
                        color: Theme.textSecondary
                        font.pixelSize: 14
                        rightPadding: 12
                    }
                    ActionButton {
                        id: zoneBack
                        text: qsTr("Back")
                        onActivated: window.step = "account"
                        KeyNavigation.right: zoneNext
                        Keys.onEscapePressed: window.step = "account"
                    }
                    ActionButton {
                        id: zoneNext
                        text: qsTr("Next")
                        onActivated: window.step = "confirm"
                        KeyNavigation.left: zoneBack
                        KeyNavigation.up: zoneList
                        Keys.onEscapePressed: window.step = "account"
                    }
                }
            }

            // ---- 4. confirm --------------------------------------------------
            Item {
                anchors.fill: parent
                visible: window.page === "confirm"

                Heading {
                    id: confirmHeading
                    title: qsTr("Erase this disk and install OmniOS?")
                    subtitle: qsTr("This cannot be undone.")
                }

                Rectangle {
                    id: confirmCard
                    anchors { top: confirmHeading.bottom; topMargin: 28; left: parent.left; right: parent.right }
                    height: confirmColumn.implicitHeight + 44
                    radius: 10
                    color: "#1F1418"
                    border.width: 1
                    border.color: window.danger

                    Column {
                        id: confirmColumn
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 22 }
                        spacing: 10
                        Text {
                            text: window.describe(window.chosen)
                            color: Theme.textPrimary
                            font.pixelSize: 20
                            font.weight: Font.DemiBold
                            width: parent.width
                            elide: Text.ElideRight
                        }
                        Text {
                            text: window.chosen ? window.chosen.path : ""
                            color: Theme.textSecondary
                            font.pixelSize: 14
                        }
                        Text {
                            width: parent.width
                            wrapMode: Text.WordWrap
                            color: Theme.textPrimary
                            font.pixelSize: 16
                            lineHeight: 1.3
                            text: window.chosen && window.chosen.os
                                  ? qsTr("%1 is installed on this disk. Erasing it removes %1 and everything stored with it — documents, games, other accounts — and the machine will no longer start %1.").arg(window.chosen.os)
                                  : window.chosen && window.chosen.contents
                                  ? qsTr("Everything on this disk is erased, including %1. If anything on it matters, go back and copy it somewhere else first.").arg(window.chosen.contents)
                                  : qsTr("Everything on this disk is erased.")
                        }
                    }
                }

                // What the new system will be, so a typo in the username is
                // caught here rather than at the first sign-in.
                Column {
                    anchors { top: confirmCard.bottom; topMargin: 22; left: parent.left; right: parent.right }
                    spacing: 8
                    Text {
                        color: Theme.textSecondary
                        font.pixelSize: 15
                        width: parent.width
                        elide: Text.ElideRight
                        text: window.skipAccount
                              ? qsTr("Account:  omni, no password, signs in automatically")
                              : qsTr("Account:  %1 (%2), %3")
                                    .arg(nameField.text.trim() || usernameField.text)
                                    .arg(usernameField.text)
                                    .arg(autologinSwitch.checked ? qsTr("signs in automatically")
                                                                 : qsTr("asks for the password"))
                    }
                    Text {
                        color: Theme.textSecondary
                        font.pixelSize: 15
                        text: qsTr("Computer name:  %1").arg(window.skipAccount ? "omnios" : hostnameField.text)
                    }
                    Text {
                        color: Theme.textSecondary
                        font.pixelSize: 15
                        text: qsTr("Time zone:  %1").arg(window.timezone)
                    }
                    Text {
                        color: Theme.textSecondary
                        font.pixelSize: 15
                        width: parent.width
                        elide: Text.ElideRight
                        text: qsTr("Language:  %1   ·   Keyboard:  %2")
                                  .arg(window.languageName(window.language))
                                  .arg(window.keyboardName(window.keyboard))
                    }
                }

                // Anything on the disk has to be typed for. A blank disk skips
                // this: there is nothing on it to lose.
                Field {
                    id: eraseField
                    keyboard: fieldKeyboard
                    visible: window.holdsSomething
                    anchors {
                        left: parent.left; bottom: parent.bottom
                        right: confirmButtons.left; rightMargin: 28
                    }
                    label: qsTr("Type %1 to confirm").arg("erase")
                    invalid: text !== "" && !window.eraseAllowed
                    tabTo: backButton
                    downTo: backButton
                    onSubmitted: if (window.eraseAllowed) eraseButton.forceActiveFocus()
                }

                Row {
                    id: confirmButtons
                    anchors { right: parent.right; bottom: parent.bottom }
                    spacing: 14
                    ActionButton {
                        id: backButton
                        text: qsTr("Back")
                        onActivated: window.step = "timezone"
                        KeyNavigation.right: eraseButton
                        KeyNavigation.up: eraseField.visible ? eraseField.input : null
                        KeyNavigation.left: eraseField.visible ? eraseField.input : null
                        Keys.onEscapePressed: window.step = "timezone"
                    }
                    ActionButton {
                        id: eraseButton
                        text: qsTr("Erase and install")
                        dangerous: true
                        available: window.eraseAllowed
                        onActivated: Installer.install(window.chosen.path, window.account())
                        KeyNavigation.left: backButton
                        KeyNavigation.up: eraseField.visible ? eraseField.input : null
                        Keys.onEscapePressed: window.step = "timezone"
                    }
                }
            }

            // ---- 3. installing -------------------------------------------------
            FocusScope {
                id: installingPage
                anchors.fill: parent
                visible: window.page === "installing"
                // Nothing to press while it runs, but the scope still takes
                // the keys so none of them reach a hidden button behind it.
                Keys.onPressed: event => { event.accepted = true }

                Heading {
                    id: installingHeading
                    title: qsTr("Installing OmniOS")
                    subtitle: qsTr("Keep the machine on, and leave the USB stick in until this finishes.")
                }

                Column {
                    anchors { top: installingHeading.bottom; topMargin: 48; left: parent.left; right: parent.right }
                    spacing: 16

                    Rectangle {
                        width: parent.width
                        height: 12
                        radius: 6
                        color: Theme.card
                        Rectangle {
                            width: parent.width * Installer.progress / 100
                            height: parent.height
                            radius: parent.radius
                            color: Theme.accent
                            Behavior on width { NumberAnimation { duration: 400 } }
                        }
                    }
                    Item {
                        width: parent.width
                        height: stepText.implicitHeight
                        Text {
                            id: stepText
                            text: Installer.progressText
                            color: Theme.textSecondary
                            font.pixelSize: 16
                        }
                        Text {
                            anchors.right: parent.right
                            text: Installer.progress + "%"
                            color: Theme.textPrimary
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }

            // ---- 4a. done ------------------------------------------------------
            Item {
                anchors.fill: parent
                visible: window.page === "done"

                Heading {
                    title: qsTr("OmniOS is installed")
                    // Model and size, because the model alone can be as vague
                    // as "Disk" and the boot menu may list several.
                    subtitle: qsTr("Remove the USB stick, then restart. If the machine starts something else, choose %1 in its boot menu.")
                                .arg(window.chosen ? qsTr("%1 (%2)").arg(window.chosen.model).arg(window.chosen.size)
                                                   : qsTr("the disk"))
                }

                Row {
                    anchors { right: parent.right; bottom: parent.bottom }
                    spacing: 14
                    ActionButton {
                        id: notNowButton
                        text: qsTr("Not now")
                        onActivated: Qt.quit()
                        KeyNavigation.right: restartButton
                    }
                    ActionButton {
                        id: restartButton
                        text: qsTr("Restart")
                        onActivated: Installer.restart()
                        KeyNavigation.left: notNowButton
                    }
                }
            }

            // ---- 4b. failed ----------------------------------------------------
            Item {
                anchors.fill: parent
                visible: window.page === "failed"

                Heading {
                    id: failedHeading
                    title: qsTr("The install did not finish")
                    subtitle: Installer.error
                }
                Text {
                    anchors { top: failedHeading.bottom; topMargin: 20; left: parent.left; right: parent.right }
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    font.pixelSize: 15
                    lineHeight: 1.3
                    text: qsTr("The disk may have been partly erased; nothing else on the machine was touched. Everything the installer did is written to /tmp/omnios-install.log.")
                }

                Row {
                    anchors { right: parent.right; bottom: parent.bottom }
                    spacing: 14
                    ActionButton {
                        id: failedCloseButton
                        text: qsTr("Close")
                        onActivated: Qt.quit()
                        KeyNavigation.right: retryButton
                    }
                    ActionButton {
                        id: retryButton
                        text: qsTr("Back to disks")
                        onActivated: { window.step = "choose"; Installer.reset() }
                        KeyNavigation.left: failedCloseButton
                    }
                }
            }
        }
    }

    function pick(disk) {
        if (!disk) return
        // A word typed for one disk does not carry over to another.
        if (!chosen || chosen.path !== disk.path) eraseField.text = ""
        chosen = disk
        step = "language"
    }

    function validHostname(name) {
        return /^[a-z0-9]([a-z0-9-]{0,61}[a-z0-9])?$/.test(name)
    }

    // "shyam" -> "shyam-omnios", until someone types a name of their own.
    function suggestHostname() {
        var user = usernameField.text
        return user ? user + "-omnios" : "omnios"
    }

    // The first thing wrong with the form and the field it is in, or an empty
    // text when the form is ready.
    function checkAccount() {
        var name = nameField.text.trim()
        if (/[:,]/.test(name))
            return { text: qsTr("Your name cannot contain a colon or a comma"), field: nameField }
        var userProblem = Installer.usernameProblem(usernameField.text)
        if (userProblem !== "") return { text: userProblem, field: usernameField }
        if (!validHostname(hostnameField.text))
            return { text: qsTr("A computer name is lowercase letters, digits and -, and cannot start or end with -"),
                     field: hostnameField }
        if (passwordField.text === "") return { text: qsTr("Choose a password"), field: passwordField }
        if (passwordField.text !== confirmField.text)
            return { text: qsTr("The two passwords are different"), field: confirmField }
        return { text: "", field: null }
    }

    // A problem shown after Next goes as soon as it is fixed. It is not
    // replaced by the next one: typing the first password would otherwise be
    // greeted with "the two passwords are different" before there was a
    // chance to type the second. What else is wrong is Next's to say.
    function recheck() {
        if (accountProblem !== "" && checkAccount().text !== accountProblem) accountProblem = ""
    }

    function acceptAccount() {
        var problem = checkAccount()
        accountProblem = problem.text
        if (problem.field) {
            problem.field.input.forceActiveFocus()
            return
        }
        skipAccount = false
        step = "timezone"
    }

    function account() {
        if (skipAccount) return { skip: true, timezone: timezone, language: language,
                                  keyboard: keyboard, eraseConfirmed: eraseAllowed }
        return {
            skip: false,
            eraseConfirmed: eraseAllowed,
            fullName: nameField.text.trim(),
            username: usernameField.text,
            password: passwordField.text,
            hostname: hostnameField.text,
            autologin: autologinSwitch.checked,
            timezone: timezone,
            language: language,
            keyboard: keyboard
        }
    }

    // Entries of a list whose fields (keys) contain what was typed.
    function filtered(all, text, keys) {
        var q = text.trim().toLowerCase()
        if (q === "") return all
        return all.filter(function (entry) {
            return keys.some(function (k) { return String(entry[k]).toLowerCase().indexOf(q) >= 0 })
        })
    }

    function nameIn(list, id) {
        for (var i = 0; i < list.length; ++i) if (list[i].id === id) return list[i].name
        return id
    }
    function languageName(id) { return nameIn(Installer.languages, id) }
    function keyboardName(id) { return nameIn(Installer.keyboards, id) }

    // A picks a language and goes on to the keyboards, landing on the layout
    // that language suggests — unless a layout was already picked by hand.
    function pickLanguage() {
        var entry = languagePage.languages[languageList.currentIndex]
        if (!entry) return
        language = entry.id
        if (!keyboardPicked) {
            var suggested = Installer.suggestKeyboard(entry.id)
            if (suggested !== "" && suggested !== keyboard
                    && keyboardName(suggested) !== suggested) setKeyboard(suggested)
        }
        keyboardSearch.text = ""
        keyboardList.showChosen()
        keyboardList.forceActiveFocus()
    }

    // A picks a layout, which the live session switches to at once, and goes
    // on to Next.
    function pickKeyboard() {
        var entry = languagePage.keyboards[keyboardList.currentIndex]
        if (!entry) return
        keyboardPicked = true
        setKeyboard(entry.id)
        languageNext.forceActiveFocus()
    }

    function setKeyboard(id) {
        keyboard = id
        Installer.applyKeyboard(id)
    }

    // Enter on a zone picks it; Enter on the one already picked moves on.
    function pickZone() {
        var zone = zonePage.zones[zoneList.currentIndex]
        if (!zone) return
        if (zone.id === timezone) step = "confirm"
        else timezone = zone.id
    }

    function jumpRegion(direction) {
        var zones = zonePage.zones
        var i = zoneList.currentIndex
        if (i < 0 || zones.length === 0) return
        var region = zones[i].region
        if (direction > 0) {
            while (i < zones.length - 1 && zones[i].region === region) i++
        } else {
            // Back to the start of this region, or of the one before it when
            // already at the start.
            if (i > 0 && zones[i - 1].region === region) {
                while (i > 0 && zones[i - 1].region === region) i--
            } else if (i > 0) {
                i--
                var previous = zones[i].region
                while (i > 0 && zones[i - 1].region === previous) i--
            }
        }
        zoneList.currentIndex = i
        zoneList.positionViewAtIndex(i, ListView.Beginning)
    }

    // Typing with a controller: the fields above open this on A (✕).
    FieldKeyboard {
        id: fieldKeyboard
        inputMode: Installer.input
    }
}

// SPDX-License-Identifier: GPL-2.0-or-later
// Layout adapted from VLC Redesign by Juxtopposed, CC BY 4.0.
// https://www.figma.com/community/file/1691465999134370980/vlc-redesign
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

FocusScope {
    id: root
    required property var backend
    property int page: 0
    property string folder: backend.defaultFolder
    property int scheme: 2
    property color accentColor: "#d37918"
    property bool onlineMetadata: true
    property bool systemLight: false
    readonly property bool previewLight: scheme === 1 || (scheme === 0 && systemLight)
    readonly property real contentScale: Math.min(width / 1090, height / 759)
    readonly property var accents: ["#d37918", "#e15032", "#c49e23", "#19a234", "#18aad3", "#185dd3", "#8e42ff", "#f052b3", "#afafaf"]
    readonly property var colorNames: [qsTr("Orange"), qsTr("Red"), qsTr("Yellow"), qsTr("Green"), qsTr("Cyan"), qsTr("Blue"), qsTr("Purple"), qsTr("Pink"), qsTr("White")]
    signal finished()
    signal dismissRequested()
    implicitWidth: 1090; implicitHeight: 759
    focus: true

    function next() {
        folderField.focus = false
        if (page === 0 && !backend.validateFolder(folder)) return
        if (page < 2) { page++; nextButton.forceActiveFocus() }
        else if (backend.finish(folder, scheme, accentColor.toString(), onlineMetadata)) finished()
    }
    function dismiss() {
        backend.skip()
        dismissRequested()
    }
    function keycapWidth(key, textWidth, compact) {
        if (compact) return Math.max(40, textWidth + 24)
        if (key === "Space") return Math.max(93, textWidth + 32)
        if (key === "Shift") return Math.max(92, textWidth + 32)
        if (["Left", "Right", "Up", "Down"].includes(key)) return 46
        if ([",", "."].includes(key)) return 35
        if (["<", ">"].includes(key)) return 41
        return Math.max(56, textWidth + 24)
    }
    function keyParts(sequence) {
        const parts = sequence.split("+")
        // Native key names use a final plus sign for the Plus key.
        if (parts.length > 1 && parts[parts.length - 1] === "") {
            parts.pop()
            if (parts[parts.length - 1] === "") parts[parts.length - 1] = "+"
        }
        return parts
    }
    Keys.onEscapePressed: dismiss()
    Rectangle { anchors.fill: parent; radius: 14 * root.contentScale; color: "#0e0e0e" }
    Item {
        id: scene
        width: 1090; height: 759; scale: root.contentScale
        anchors.centerIn: parent
        component Label: Text {
            color: "#8f8f8f"; font.pixelSize: 16; font.weight: Font.Light; wrapMode: Text.WordWrap
        }
        component Action: RedesignButton {
            width: 100; height: 32; captionFontSize: 16
            captionFontWeight: Font.DemiBold
            accentColor: root.accentColor
            background: Rectangle {
                radius: 6; color: parent.accent ? root.accentColor : parent.hovered ? "#292929" : "#1b1b1b"
                border.color: parent.visualFocus ? "#e5e5e5" : "transparent"
            }
        }
        RedesignButton {
            objectName: "onboardingClose"
            x: 1038; y: 12; width: 32; height: 32; z: 1
            visible: root.page !== 1
            iconName: "close"; text: qsTr("Close setup")
            accentColor: root.accentColor
            onClicked: root.dismiss()
        }
        Item {
            objectName: "onboardingWelcome"
            anchors.fill: parent; visible: root.page === 0
            RedesignIcon { objectName: "onboardingWelcomeCone"; x: 475; y: 116; width: 140; height: 140; name: "logo"; color: root.accentColor }
            Label { x: 248; y: 286; width: 594; height: 50; text: qsTr("Welcome to Hakone!"); color: "#eaeaea"; font.pixelSize: 32; horizontalAlignment: Text.AlignHCenter; leftPadding: -0.5; rightPadding: -leftPadding }
            Label {
                x: 248; y: 366; width: 594; height: 84; lineHeight: 1.4; color: "#969696"
                text: qsTr("Hakone is a free and open source media player.\nPlay videos and music from files, discs, and network streams.\nYou can change all settings later.")
            }
            Label { x: 248; y: 500; width: 594; text: qsTr("Select a media folder to start with"); font.pixelSize: 14; font.weight: Font.Normal }
            C.TextField {
                id: folderField; objectName: "onboardingFolder"
                x: 248; y: 534; width: 469; height: 34
                enabled: root.backend.hasMediaLibrary
                text: root.folder; placeholderText: qsTr("No folder selected")
                color: "#c6c6c6"; placeholderTextColor: "#8f8f8f"; font.pixelSize: 16
                leftPadding: 12; selectByMouse: true
                Accessible.name: qsTr("Media folder")
                onTextEdited: root.folder = text
                background: Rectangle { color: "#090909"; radius: 4; border.width: 2; border.color: folderField.activeFocus ? root.accentColor : "#191919" }
            }
            Action {
                objectName: "onboardingBrowse"
                x: 729; y: 533.5; width: 114; height: 35
                captionFontWeight: Font.Normal; captionVerticalOffset: -0.5
                caption: qsTr("Browse"); text: caption; enabled: root.backend.hasMediaLibrary
                onClicked: { const chosen = root.backend.browseFolder(root.folder); if (chosen.length) root.folder = chosen }
            }
            Label {
                x: 248; y: 578; width: 594; font.pixelSize: 13
                visible: !root.backend.hasMediaLibrary
                text: qsTr("This build has no media library. You can open files from the player.")
            }
            C.CheckBox {
                id: metadata; objectName: "onboardingMetadata"
                x: 248; y: 612; width: 555; height: 32
                checked: root.onlineMetadata; onToggled: root.onlineMetadata = checked
                text: qsTr("Get cover art and media information online")
                font.pixelSize: 14; leftPadding: 30
                contentItem: Text { text: metadata.text; font: metadata.font; color: "#8f8f8f"; verticalAlignment: Text.AlignVCenter }
                indicator: Rectangle {
                    x: 0; y: 6; width: 20; height: 20; radius: 4
                    color: metadata.checked ? root.accentColor : "#090909"
                    border.color: metadata.visualFocus ? "#e5e5e5" : "#292929"
                    RedesignIcon { anchors.fill: parent; name: "check"; color: "#161616"; visible: metadata.checked }
                }
                background: Item { }
            }
            RedesignButton {
                id: privacyInfo; objectName: "onboardingPrivacyInfo"
                x: 809; y: 610; width: 36; height: 36
                iconName: "info"; accentColor: root.accentColor
                text: qsTr("Online media information")
                onClicked: policy.open()
            }
            C.Popup {
                id: policy; objectName: "onboardingPrivacyPolicy"
                parent: scene; x: 248; y: 450; width: 594; padding: 18
                modal: true; closePolicy: C.Popup.CloseOnEscape | C.Popup.CloseOnPressOutside
                onOpened: contentItem.forceActiveFocus()
                background: Rectangle { color: "#191919"; radius: 8; border.color: "#292929" }
                contentItem: Label {
                    focus: true
                    Keys.onEscapePressed: policy.close()
                    text: qsTr("Hakone does not collect personal data. If you allow online media information, Hakone can request cover art and track details from online services. These requests can identify your media to those services. Clear the option to keep these requests off.")
                    lineHeight: 1.4
                }
            }
        }
        Item {
            objectName: "onboardingAppearance"
            anchors.fill: parent; visible: root.page === 1
            RedesignPlayerView {
                id: preview; objectName: "onboardingPlayerPreview"
                x: 17; y: 19; width: 1920; height: 1080
                // Scale the complete scene so fractional text sizes survive.
                transformOrigin: Item.TopLeft; scale: 1056 / width
                scaleFactor: 1; timeLabelFontSize: 15; presentationOnly: true
                accentColor: root.accentColor; lightTheme: root.previewLight
                hasVideo: true; started: true; seekable: true; hasPrevious: true; hasNext: true
                videoContent: Component { Rectangle { color: root.previewLight ? "#dedede" : "#090909" } }
                mediaTitle: ""; locationTitle: ""; volume: .3; position: .08
                timeText: "00:02:10"; lengthText: "00:25:40"
                onCloseRequested: root.dismiss()
            }
            Label {
                x: 100; y: 180; width: 890
                text: qsTr("Select your preferred theme and color.")
                horizontalAlignment: Text.AlignHCenter; font.pixelSize: 24
                color: root.previewLight ? preview.theme.foreground : "#969696"
                // The white-accent reference uses a regular heading.
                font.weight: root.accentColor.toString() === "#afafaf" ? Font.Normal : Font.Light
                leftPadding: root.accentColor.toString() === "#afafaf" ? 0 : -0.5; rightPadding: -leftPadding
                topPadding: -0.25; bottomPadding: -topPadding
            }
            Row {
                x: 327; y: 282; spacing: 32
                Repeater {
                    model: [{label: qsTr("Light"), scheme: 1}, {label: qsTr("Dark"), scheme: 2}, {label: qsTr("System"), scheme: 0}]
                    C.AbstractButton {
                        id: schemeButton
                        required property var modelData
                        objectName: "onboardingScheme_" + modelData.scheme
                        width: 124; height: 160; hoverEnabled: true
                        Accessible.name: modelData.label
                        Accessible.role: Accessible.RadioButton
                        Accessible.checkable: true; Accessible.checked: root.scheme === modelData.scheme
                        onClicked: root.scheme = modelData.scheme
                        background: Item { }
                        contentItem: Item {
                            Rectangle {
                                x: 0; y: 0; width: 124; height: 124; radius: 24; color: "transparent"
                                border.width: root.scheme === schemeButton.modelData.scheme ? 4 : schemeButton.visualFocus ? 2 : 0
                                border.color: schemeButton.visualFocus ? "#e5e5e5" : root.accentColor
                            }
                            Rectangle { x: 12; y: 12; width: 100; height: 100; radius: 12; color: schemeButton.modelData.scheme === 1 ? "#afafaf" : "#222222" }
                            RedesignSvg {
                                x: 12; y: 12; width: 100; height: 100; visible: schemeButton.modelData.scheme === 0
                                svg: '<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100"><path d="M15 0H88Q100 0 100 12V88Q100 100 88 100H76Z" fill="#afafaf"/></svg>'
                            }
                            Label { y: 141.5; width: parent.width; text: schemeButton.modelData.label; horizontalAlignment: Text.AlignHCenter; color: root.previewLight ? preview.theme.foreground : "#8f8f8f" }
                        }
                    }
                }
            }
            Row {
                x: 41; y: 655; spacing: 16
                Repeater {
                    model: root.accents
                    C.AbstractButton {
                        id: colorButton
                        required property string modelData
                        required property int index
                        objectName: "onboardingAccent_" + index
                        width: 64; height: 64; hoverEnabled: true
                        Accessible.name: root.colorNames[index]
                        Accessible.role: Accessible.RadioButton
                        Accessible.checkable: true; Accessible.checked: Qt.colorEqual(root.accentColor, modelData)
                        onClicked: root.accentColor = modelData
                        background: Rectangle {
                            radius: 32; color: "transparent"
                            border.width: Qt.colorEqual(root.accentColor, colorButton.modelData) ? 4 : colorButton.visualFocus ? 2 : 0
                            border.color: colorButton.visualFocus ? "#e5e5e5" : Qt.alpha(colorButton.modelData, .3)
                        }
                        contentItem: Item {
                            Rectangle { x: 12; y: 12; width: 40; height: 40; radius: 20; color: colorButton.modelData; opacity: Qt.colorEqual(root.accentColor, colorButton.modelData) || colorButton.hovered ? 1 : .5 }
                        }
                    }
                }
            }
        }
        Item {
            objectName: "onboardingShortcuts"
            anchors.fill: parent; visible: root.page === 2
            Label { x: 100; y: 66.75; width: 890; text: qsTr("Learn the basic keyboard shortcuts."); horizontalAlignment: Text.AlignHCenter; font.pixelSize: 24; font.weight: Font.Normal; color: "#969696" }
            Repeater {
                model: root.backend.shortcuts
                Row {
                    id: shortcutRow
                    required property var modelData
                    required property int index
                    readonly property bool compactKeys: modelData.keys.length > 1
                        && Array.from(modelData.keys).every(key => key.includes("+"))
                    objectName: "onboardingShortcutRow_" + index
                    x: index < 6 ? 62 : 605; y: 154 + (index % 6) * 82
                    width: 475; height: 42
                    Label { width: 240; anchors.verticalCenter: parent.verticalCenter; text: parent.modelData.title; font.pixelSize: 16; color: "#969696"; topPadding: 0.5; bottomPadding: -topPadding }
                    Row {
                        width: 235; spacing: 6
                        scale: Math.min(1, width / implicitWidth)
                        transformOrigin: Item.TopLeft
                        Repeater {
                            model: parent.parent.modelData.keys
                            Row {
                                id: keyGroup
                                required property string modelData
                                required property int index
                                spacing: 6
                                Label { visible: parent.index > 0; text: "/"; color: "#969696"; anchors.verticalCenter: parent.verticalCenter }
                                Row {
                                    spacing: 4
                                    Repeater {
                                        model: root.keyParts(modelData)
                                        Item {
                                            id: keycap
                                            required property string modelData
                                            required property int index
                                            objectName: "onboardingKeycap_" + shortcutRow.index + "_" + keyGroup.index + "_" + index
                                            width: root.keycapWidth(modelData, keyLabel.implicitWidth, shortcutRow.compactKeys); height: 42
                                            Rectangle { objectName: keycap.objectName + "_shadow"; y: 7; width: parent.width; height: parent.height; radius: 12; color: "#222222" }
                                            Rectangle { anchors.fill: parent; radius: 12; color: "#1b1b1b"; border.color: "#373737"; border.width: 3 }
                                            Label {
                                                id: keyLabel; anchors.centerIn: parent; font.pixelSize: 16
                                                anchors.horizontalCenterOffset: keycap.modelData === "Space" ? -1 : 0
                                                topPadding: 0.5; bottomPadding: -topPadding
                                                text: ({Left: "←", Right: "→", Up: "↑", Down: "↓"})[keycap.modelData]
                                                    || (["Shift", "Ctrl", "Alt", "Meta"].includes(keycap.modelData) ? keycap.modelData.toUpperCase() : keycap.modelData)
                                                color: "#eaeaea"
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            Label { x: 62; y: 675.5; text: qsTr("Find the full list in the menu."); font.pixelSize: 16; font.weight: Font.Normal; color: "#6b6b6b" }
        }
        Label {
            id: error; objectName: "onboardingError"
            x: 248; y: root.page === 0 ? 576 : 628; width: 594
            visible: root.backend.errorMessage.length > 0
            text: root.backend.errorMessage; color: "#e56b6b"; font.pixelSize: 13
            Accessible.role: Accessible.AlertMessage
        }
        Action {
            objectName: "onboardingBack"
            x: 820; y: 671; visible: root.page > 0
            caption: qsTr("Back"); text: caption
            onClicked: { root.page--; nextButton.forceActiveFocus() }
        }
        Action {
            id: nextButton; objectName: "onboardingNext"
            x: 928; y: 671; accent: true
            caption: root.page === 2 ? qsTr("Finish") : qsTr("Next"); text: caption
            onClicked: root.next()
        }
    }
}

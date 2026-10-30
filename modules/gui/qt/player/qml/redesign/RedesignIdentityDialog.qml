// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

// The trusted identity choice: the user picks the series this episode belongs
// to. Nothing is saved until the user selects a row and chooses Use series,
// also when there is only one row.
C.Dialog {
    id: root
    popupType: C.Popup.Item
    property real scaleFactor: 1
    property bool lightTheme: false
    property color accentColor: "#d37918"
    property Component videoBackdropContent
    // AddonIdentityChoice, or null when add-ons are not available.
    property var backend: null
    property int selectedRow: -1
    readonly property string choiceState: backend ? backend.state : "failed"
    readonly property var rows: backend ? backend.candidates : []
    readonly property bool busy: choiceState === "searching" || choiceState === "applying"
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    readonly property real pixelRatio: parent ? parent.Screen.devicePixelRatio : 1
    title: qsTr("Choose series")
    x: parent ? Math.round((parent.width - width) * pixelRatio / 2) / pixelRatio : 0
    y: parent ? Math.round((parent.height - height) * pixelRatio / 2) / pixelRatio : 0
    width: Math.max(0, Math.min(theme.dp(520), parent ? parent.width - theme.dp(32) : theme.dp(520)))
    height: Math.min(theme.dp(480), parent ? Math.max(0, parent.height - theme.dp(32)) : theme.dp(480))
    padding: theme.dp(24)
    modal: true; dim: true; focus: true
    closePolicy: C.Popup.CloseOnEscape
    header: null; footer: null
    background: RedesignDialogSurface { theme: root.theme; videoBackdropContent: root.videoBackdropContent }
    C.Overlay.modal: Rectangle { color: "#80000000" }

    function begin() {
        selectedRow = -1
        if (!visible) open()
        if (backend) backend.start("")
        queryField.text = backend ? backend.query : ""
    }
    function search() {
        selectedRow = -1
        if (backend && !busy) backend.start(queryField.text)
    }
    function use() {
        if (backend && choiceState === "choosing" && selectedRow >= 0) backend.choose(selectedRow)
    }
    onClosed: if (backend && choiceState !== "done") backend.cancel()
    onRowsChanged: selectedRow = -1

    contentItem: ColumnLayout {
        Accessible.role: Accessible.Dialog
        Accessible.name: root.title
        spacing: root.theme.dp(12)
        Text {
            Layout.fillWidth: true
            text: root.title; textFormat: Text.PlainText
            color: root.theme.bright; font.pixelSize: root.theme.dp(20)
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: root.theme.dp(8)
            C.TextField {
                id: queryField
                objectName: "identitySearchField"
                Layout.fillWidth: true; Layout.preferredHeight: root.theme.dp(37)
                enabled: !root.busy && root.backend !== null
                selectByMouse: true
                placeholderText: qsTr("Series name")
                font.pixelSize: root.theme.dp(14)
                color: root.theme.playerHeading
                selectionColor: root.theme.accent; selectedTextColor: root.theme.field
                leftPadding: root.theme.dp(10); rightPadding: leftPadding
                background: Rectangle {
                    color: root.theme.field; radius: root.theme.dp(6)
                    border.width: root.theme.dp(1)
                    border.color: queryField.activeFocus ? root.theme.accent : root.theme.border
                }
                Accessible.name: qsTr("Series name")
                Keys.onReturnPressed: root.search()
                Keys.onEnterPressed: root.search()
            }
            RedesignButton {
                objectName: "identitySearch"
                Layout.preferredHeight: root.theme.dp(37)
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                enabled: !root.busy && root.backend !== null
                caption: qsTr("Search"); text: caption
                onClicked: root.search()
            }
        }
        ListView {
            id: list
            objectName: "identityChoices"
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            model: root.rows
            currentIndex: root.selectedRow
            keyNavigationEnabled: true
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Series found")
            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index
                width: ListView.view.width
                height: root.theme.dp(56)
                radius: root.theme.dp(6)
                color: root.selectedRow === index ? root.theme.progress : mouse.containsMouse ? root.theme.hover : "transparent"
                border.width: root.selectedRow === index ? root.theme.dp(1) : 0
                border.color: root.theme.accent
                Accessible.role: Accessible.ListItem
                Accessible.name: [modelData.name, modelData.detail, modelData.source].filter(v => !!v).join(", ")
                Accessible.selected: root.selectedRow === index
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    x: root.theme.dp(12); width: parent.width - root.theme.dp(24)
                    spacing: root.theme.dp(2)
                    Text {
                        width: parent.width
                        text: row.modelData.name; textFormat: Text.PlainText; elide: Text.ElideRight
                        color: root.theme.playerHeading; font.pixelSize: root.theme.dp(15)
                    }
                    Text {
                        width: parent.width
                        text: [row.modelData.detail, row.modelData.source].filter(v => !!v).join(" · ")
                        textFormat: Text.PlainText; elide: Text.ElideRight
                        color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
                    }
                }
                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.selectedRow = row.index
                    onDoubleClicked: { root.selectedRow = row.index; root.use() }
                }
            }
            Keys.onUpPressed: root.selectedRow = Math.max(0, root.selectedRow - 1)
            Keys.onDownPressed: root.selectedRow = Math.min(count - 1, root.selectedRow + 1)
        }
        Text {
            objectName: "identityMessage"
            Layout.fillWidth: true
            visible: text.length > 0
            text: !root.backend ? qsTr("Add-ons are not available.")
                : root.choiceState === "choosing" && !root.backend.message ? qsTr("Select the series this episode belongs to.")
                : root.backend.message
            textFormat: Text.PlainText; wrapMode: Text.Wrap
            color: root.choiceState === "failed" ? root.theme.danger : root.theme.foreground
            font.pixelSize: root.theme.dp(14)
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: root.theme.dp(8)
            RedesignButton {
                objectName: "identityCancel"
                Layout.preferredHeight: root.theme.dp(32)
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                caption: root.choiceState === "done" ? qsTr("Close") : qsTr("Cancel"); text: caption
                onClicked: root.close()
            }
            RedesignButton {
                objectName: "identityUse"
                Layout.preferredHeight: root.theme.dp(32)
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                visible: root.choiceState !== "done"
                enabled: root.choiceState === "choosing" && root.selectedRow >= 0
                caption: qsTr("Use series"); text: caption
                foreground: root.theme.accent
                onClicked: root.use()
            }
        }
    }
}

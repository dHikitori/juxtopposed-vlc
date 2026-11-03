// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

C.Dialog {
    id: root
    property real scaleFactor: 1
    property bool lightTheme: false
    property color accentColor: "#d37918"
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    property int requestId: 0
    property bool subtitles: true
    property bool loading: false
    property bool choosing: false
    property string playlistName: ""
    property string errorMessage: ""
    property var items: []
    property alias selectedIndex: fileChoice.currentIndex
    readonly property var selectedFile: selectedIndex >= 0 && selectedIndex < items.length ? items[selectedIndex] : null
    readonly property bool valid: !loading && selectedFile !== null && selectedFile.enabled !== false
    signal tracksRequested(url media, bool subtitles)
    title: subtitles ? qsTr("Add Subtitle Tracks") : qsTr("Add Audio Tracks")
    anchors.centerIn: parent
    width: Math.min(theme.dp(714), parent ? parent.width - theme.dp(32) : theme.dp(714))
    height: Math.min(theme.dp(370), parent ? parent.height - theme.dp(32) : theme.dp(370))
    padding: 0
    modal: true; dim: true; focus: true
    closePolicy: choosing ? C.Popup.NoAutoClose : C.Popup.CloseOnEscape
    background: Rectangle { color: root.theme.background; radius: root.theme.dp(12) }
    C.Overlay.modal: Rectangle { color: "#80000000" }
    function begin(key, useSubtitles) {
        if (choosing) return -1
        ++requestId; subtitles = useSubtitles; loading = true; items = []; playlistName = ""; errorMessage = ""
        open(); return requestId
    }
    function load(request, result) {
        if (!visible || request !== requestId) return
        loading = false
        if (!result.success) { errorMessage = result.error || qsTr("Hakone cannot read this playlist. Select it again."); return }
        playlistName = result.name || ""
        items = (result.items || []).map(item => Object.assign({}, item, {label: qsTr("%1 — %2").arg(item.name).arg(item.location)}))
        selectedIndex = items.findIndex(item => item.enabled !== false)
        if (!items.length) errorMessage = qsTr("This playlist has no files that Hakone can use.")
        else if (selectedIndex < 0) errorMessage = qsTr("The files in this playlist are not available.")
        else fileChoice.forceActiveFocus()
    }
    function submit() {
        if (!valid || choosing) return
        choosing = true
        tracksRequested(selectedFile.url, subtitles)
    }
    function finish() { choosing = false; accept() }
    Shortcut { sequence: "Ctrl+Return"; enabled: root.visible && root.valid && !root.choosing; onActivated: root.submit() }
    Shortcut { sequence: "Ctrl+Enter"; enabled: root.visible && root.valid && !root.choosing; onActivated: root.submit() }
    header: Item {
        implicitHeight: root.theme.dp(50)
        Rectangle {
            anchors.fill: parent; radius: root.theme.dp(12); color: root.theme.field
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: parent.height / 2; color: parent.color }
        }
        Text { anchors.left: parent.left; anchors.leftMargin: root.theme.dp(20); anchors.verticalCenter: parent.verticalCenter; text: root.title; color: root.theme.foreground; font.pixelSize: root.theme.dp(14) }
        RedesignButton {
            objectName: "playlistTrackClose"
            anchors.right: parent.right; anchors.rightMargin: root.theme.dp(8); anchors.verticalCenter: parent.verticalCenter
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            iconName: "close"; text: qsTr("Close"); enabled: !root.choosing; onClicked: root.reject()
        }
    }
    contentItem: C.ScrollView {
        id: scroll
        clip: true; contentWidth: availableWidth
        C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
        ColumnLayout {
            width: scroll.availableWidth
            spacing: root.theme.dp(12)
            Text {
                Layout.fillWidth: true; Layout.leftMargin: root.theme.dp(20); Layout.rightMargin: root.theme.dp(20); Layout.topMargin: root.theme.dp(24)
                text: root.loading ? qsTr("Loading playlist…") : root.playlistName
                color: root.theme.bright; font.pixelSize: root.theme.dp(16); wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true; Layout.leftMargin: root.theme.dp(20); Layout.rightMargin: root.theme.dp(20)
                visible: !root.loading && root.items.length > 0
                text: qsTr("Select a file. Then select the tracks for that file.")
                color: root.theme.foreground; font.pixelSize: root.theme.dp(14); wrapMode: Text.WordWrap
            }
            RedesignComboBox {
                id: fileChoice
                objectName: "playlistTrackFile"
                Layout.fillWidth: true; Layout.leftMargin: root.theme.dp(20); Layout.rightMargin: root.theme.dp(20)
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                model: root.items; textRole: "label"; restrictChoices: true
                enabled: !root.loading && !root.choosing && count > 0
                Accessible.name: qsTr("File for these tracks")
            }
            TextEdit {
                objectName: "playlistTrackLocation"
                Layout.fillWidth: true; Layout.preferredHeight: contentHeight
                Layout.leftMargin: root.theme.dp(20); Layout.rightMargin: root.theme.dp(20); Layout.bottomMargin: root.theme.dp(20)
                text: root.selectedFile ? root.selectedFile.location : ""
                textFormat: TextEdit.PlainText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere
                color: root.theme.foreground; font.pixelSize: root.theme.dp(12)
                selectionColor: root.theme.accent; selectedTextColor: root.theme.field
            }
        }
    }
    footer: Item {
        implicitHeight: root.theme.dp(70) + (error.visible ? error.implicitHeight + root.theme.dp(12) : 0)
        Text {
            id: error
            objectName: "playlistTrackError"
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: root.theme.dp(20); anchors.topMargin: root.theme.dp(8)
            visible: root.errorMessage.length > 0
            text: root.errorMessage; color: root.theme.danger; font.pixelSize: root.theme.dp(14); wrapMode: Text.WordWrap
            Accessible.role: Accessible.AlertMessage
        }
        RedesignButton {
            objectName: "playlistTrackCancel"
            x: root.theme.dp(20); anchors.bottom: parent.bottom; anchors.bottomMargin: root.theme.dp(19)
            implicitWidth: root.theme.dp(100); implicitHeight: root.theme.dp(32)
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            caption: root.items.length ? qsTr("Cancel") : qsTr("Close"); text: caption
            enabled: !root.choosing; onClicked: root.reject()
        }
        RedesignButton {
            objectName: "playlistTrackSelect"
            anchors.right: parent.right; anchors.rightMargin: root.theme.dp(20); anchors.bottom: parent.bottom; anchors.bottomMargin: root.theme.dp(19)
            implicitWidth: root.theme.dp(140); implicitHeight: root.theme.dp(32)
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            caption: qsTr("Select tracks"); text: caption; enabled: root.valid && !root.choosing; accent: true
            onClicked: root.submit()
        }
    }
}

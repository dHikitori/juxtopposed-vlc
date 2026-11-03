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
    property string playlistKey: ""
    property var details: ({})
    property int requestId: 0
    property bool loading: false
    property bool saving: false
    property string errorMessage: ""
    property alias playlistName: nameField.text
    property url cover: ""
    readonly property bool readOnly: details.readOnly !== false
    readonly property bool dirty: playlistName.trim() !== (details.name || "") || cover.toString() !== (details.cover || "")
    readonly property bool valid: !loading && !readOnly && playlistName.trim().length > 0 && dirty
    signal coverRequested()
    signal saveRequested(string key, string name, url cover, int request)
    title: qsTr("Properties & Metadata")
    anchors.centerIn: parent
    width: Math.min(theme.dp(714), parent ? parent.width - theme.dp(32) : theme.dp(714))
    height: Math.min(theme.dp(620), parent ? parent.height - theme.dp(32) : theme.dp(620))
    padding: 0
    modal: true; dim: true; focus: true
    closePolicy: saving ? C.Popup.NoAutoClose : C.Popup.CloseOnEscape
    background: Rectangle { color: root.theme.background; radius: root.theme.dp(12) }
    C.Overlay.modal: Rectangle { color: "#80000000" }
    function begin(key) {
        if (saving) return -1
        ++requestId; playlistKey = key; details = {}; playlistName = ""; cover = ""; errorMessage = ""
        loading = true; open(); return requestId
    }
    function load(request, result) {
        if (!visible || request !== requestId) return
        loading = false
        if (!result.success) { errorMessage = result.error || qsTr("Hakone cannot read this playlist. Try again."); return }
        details = result; playlistName = result.name || ""; cover = result.cover || ""
        if (!readOnly) nameField.forceActiveFocus()
    }
    function submit() {
        if (!valid || saving) return
        saving = true; errorMessage = ""
        saveRequested(playlistKey, playlistName.trim(), cover, requestId)
    }
    function finish(request, result) {
        if (!visible || request !== requestId) return
        saving = false
        if (result.success) accept()
        else errorMessage = result.error || qsTr("Hakone cannot save this playlist. Try again.")
    }
    function duration(milliseconds) {
        const seconds = Math.max(0, Math.floor(Number(milliseconds || 0) / 1000))
        return [Math.floor(seconds / 3600), Math.floor(seconds / 60) % 60, seconds % 60]
            .map(value => String(value).padStart(2, "0")).join(":")
    }
    Shortcut { sequence: "Ctrl+Return"; enabled: root.visible && root.valid && !root.saving; onActivated: root.submit() }
    Shortcut { sequence: "Ctrl+Enter"; enabled: root.visible && root.valid && !root.saving; onActivated: root.submit() }
    header: Item {
        implicitHeight: root.theme.dp(50)
        Rectangle {
            anchors.fill: parent; radius: root.theme.dp(12); color: root.theme.field
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: parent.height / 2; color: parent.color }
        }
        Text { anchors.left: parent.left; anchors.leftMargin: root.theme.dp(20); anchors.verticalCenter: parent.verticalCenter; text: root.title; color: root.theme.foreground; font.pixelSize: root.theme.dp(14) }
        RedesignButton {
            objectName: "playlistPropertiesClose"
            anchors.right: parent.right; anchors.rightMargin: root.theme.dp(8); anchors.verticalCenter: parent.verticalCenter
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            iconName: "close"; text: qsTr("Close"); enabled: !root.saving; onClicked: root.reject()
        }
    }
    contentItem: C.ScrollView {
        id: scroll
        clip: true; contentWidth: availableWidth
        C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
        ColumnLayout {
            width: scroll.availableWidth
            spacing: root.theme.dp(16)
            Text {
                objectName: "playlistPropertiesLoading"
                Layout.fillWidth: true; Layout.margins: root.theme.dp(20)
                visible: root.loading
                text: qsTr("Loading playlist…"); color: root.theme.foreground; font.pixelSize: root.theme.dp(14)
            }
            RowLayout {
                visible: !root.loading && root.details.success === true
                Layout.fillWidth: true; Layout.topMargin: root.theme.dp(24)
                Layout.leftMargin: root.theme.dp(20); Layout.rightMargin: root.theme.dp(20)
                spacing: root.theme.dp(24)
                ColumnLayout {
                    Layout.fillWidth: true; Layout.alignment: Qt.AlignTop; spacing: root.theme.dp(12)
                    Text { text: qsTr("Playlist Details"); color: root.theme.bright; font.pixelSize: root.theme.dp(16) }
                    Text { text: qsTr("Name"); color: root.theme.foreground; font.pixelSize: root.theme.dp(14) }
                    C.TextField {
                        id: nameField
                        objectName: "playlistPropertiesName"
                        Layout.fillWidth: true; Layout.preferredHeight: root.theme.dp(34)
                        color: root.theme.bright; font.pixelSize: root.theme.dp(14)
                        selectByMouse: true; readOnly: root.readOnly || root.saving
                        selectionColor: root.theme.accent; selectedTextColor: root.theme.field
                        Accessible.name: qsTr("Playlist name")
                        background: Rectangle { color: root.theme.field; radius: root.theme.dp(4); border.color: nameField.activeFocus ? root.theme.accent : "transparent" }
                        onAccepted: root.submit()
                    }
                    Text {
                        Layout.fillWidth: true; visible: root.readOnly
                        text: qsTr("You cannot change this playlist here."); color: root.theme.foreground
                        font.pixelSize: root.theme.dp(12); wrapMode: Text.WordWrap
                    }
                }
                Item {
                    Layout.preferredWidth: root.theme.dp(108); Layout.preferredHeight: root.theme.dp(108); Layout.alignment: Qt.AlignTop
                    Rectangle { anchors.fill: parent; color: root.theme.field; radius: root.theme.dp(4) }
                    Image { id: preview; objectName: "playlistPropertiesPreview"; anchors.fill: parent; anchors.margins: root.theme.dp(4); source: root.cover; fillMode: Image.PreserveAspectCrop; asynchronous: true; sourceSize: Qt.size(216, 216) }
                    RedesignIcon { anchors.centerIn: parent; width: root.theme.dp(48); height: width; name: "playlist"; color: root.theme.foreground; visible: preview.status !== Image.Ready }
                    RedesignButton {
                        objectName: "playlistPropertiesCover"
                        anchors.centerIn: parent
                        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                        iconName: "add"; text: qsTr("Change thumbnail"); visible: !root.readOnly; enabled: !root.saving
                        background: Rectangle { color: parent.hovered ? root.theme.hover : root.theme.panel; radius: root.theme.dp(4); border.color: parent.visualFocus ? root.theme.accent : "transparent" }
                        onClicked: root.coverRequested()
                    }
                    RedesignButton {
                        objectName: "playlistPropertiesRemoveCover"
                        anchors.right: parent.right; anchors.top: parent.top
                        implicitWidth: root.theme.dp(28); implicitHeight: root.theme.dp(28); iconSize: root.theme.dp(18)
                        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                        iconName: "close"; text: qsTr("Remove thumbnail"); visible: !root.readOnly && root.cover.toString().length > 0; enabled: !root.saving
                        onClicked: root.cover = ""
                    }
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.leftMargin: root.theme.dp(20); Layout.rightMargin: root.theme.dp(20); height: root.theme.dp(1); color: root.theme.border; visible: root.details.success === true }
            GridLayout {
                objectName: "playlistPropertiesMetadata"
                Layout.fillWidth: true; Layout.leftMargin: root.theme.dp(20); Layout.rightMargin: root.theme.dp(20)
                visible: root.details.success === true
                columns: 2; columnSpacing: root.theme.dp(20); rowSpacing: root.theme.dp(12)
                Repeater {
                    model: [
                        [qsTr("Files"), root.details.count || 0],
                        [qsTr("Video files"), root.details.videoCount || 0],
                        [qsTr("Audio files"), root.details.audioCount || 0],
                        [qsTr("Other files"), root.details.unknownCount || 0],
                        [qsTr("Unavailable files"), root.details.missingCount || 0],
                        [qsTr("Duration"), root.duration(root.details.duration)],
                        [qsTr("Unknown durations"), root.details.unknownDurationCount || 0],
                        [qsTr("Created"), root.details.created ? new Date(root.details.created * 1000).toLocaleString(Qt.locale(), Locale.LongFormat) : qsTr("Unknown")]
                    ]
                    RowLayout {
                        required property var modelData
                        Layout.columnSpan: 2; Layout.fillWidth: true
                        spacing: root.theme.dp(20)
                        Text { Layout.preferredWidth: Math.max(root.theme.dp(100), scroll.availableWidth * .4); text: modelData[0]; color: root.theme.foreground; font.pixelSize: root.theme.dp(14); wrapMode: Text.WordWrap }
                        Text { Layout.fillWidth: true; text: modelData[1]; color: root.theme.bright; font.pixelSize: root.theme.dp(14); wrapMode: Text.WordWrap }
                    }
                }
            }
            ColumnLayout {
                visible: root.details.success === true; Layout.fillWidth: true
                Layout.leftMargin: root.theme.dp(20); Layout.rightMargin: root.theme.dp(20); Layout.bottomMargin: root.theme.dp(20)
                Text { text: qsTr("Source"); color: root.theme.foreground; font.pixelSize: root.theme.dp(14) }
                TextEdit {
                    objectName: "playlistPropertiesSource"
                    Layout.fillWidth: true; Layout.preferredHeight: contentHeight
                    text: root.details.source || qsTr("Saved in the Hakone library"); textFormat: TextEdit.PlainText
                    readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere
                    color: root.theme.bright; font.pixelSize: root.theme.dp(14)
                    selectionColor: root.theme.accent; selectedTextColor: root.theme.field
                }
            }
        }
    }
    footer: Item {
        implicitHeight: root.theme.dp(70) + (error.visible ? error.implicitHeight + root.theme.dp(12) : 0)
        Text {
            id: error
            objectName: "playlistPropertiesError"
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: root.theme.dp(20); anchors.topMargin: root.theme.dp(8)
            visible: root.errorMessage.length > 0
            text: root.errorMessage; color: root.theme.danger; font.pixelSize: root.theme.dp(14); wrapMode: Text.WordWrap
            Accessible.role: Accessible.AlertMessage
        }
        RedesignButton {
            objectName: "playlistPropertiesCancel"
            x: root.theme.dp(20); anchors.bottom: parent.bottom; anchors.bottomMargin: root.theme.dp(19)
            implicitWidth: root.theme.dp(100); implicitHeight: root.theme.dp(32)
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            caption: root.dirty && !root.readOnly ? qsTr("Cancel") : qsTr("Close"); text: caption; enabled: !root.saving
            onClicked: root.reject()
        }
        RedesignButton {
            objectName: "playlistPropertiesSave"
            anchors.right: parent.right; anchors.rightMargin: root.theme.dp(20); anchors.bottom: parent.bottom; anchors.bottomMargin: root.theme.dp(19)
            implicitWidth: root.theme.dp(100); implicitHeight: root.theme.dp(32)
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            caption: root.saving ? qsTr("Saving…") : qsTr("Save"); text: caption
            visible: !root.readOnly && !root.loading; enabled: root.valid && !root.saving; accent: true
            onClicked: root.submit()
        }
    }
}

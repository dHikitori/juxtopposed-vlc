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
    property var info: ({})
    property var requestId: 0
    property bool loading: false
    property string errorMessage: ""
    title: qsTr("Folder Info")
    anchors.centerIn: parent
    width: Math.min(theme.dp(714), parent ? parent.width - theme.dp(32) : theme.dp(714))
    height: Math.min(theme.dp(570), parent ? parent.height - theme.dp(32) : theme.dp(570))
    padding: 0
    modal: true; dim: true; focus: true; closePolicy: C.Popup.CloseOnEscape
    background: Rectangle { color: root.theme.background; radius: root.theme.dp(12) }
    C.Overlay.modal: Rectangle { color: "#80000000" }
    function begin(request) { requestId = request; loading = true; info = {}; errorMessage = ""; open() }
    function load(request, result) {
        if (!visible || request !== requestId) return
        loading = false; info = result
        if (!result.success) errorMessage = qsTr("VLC cannot read this folder. Make sure that you can read the folder. Try again.")
    }
    function date(value) {
        if (value === null || value === undefined) return qsTr("Unknown")
        const result = new Date(Number(value))
        return Number.isFinite(result.getTime()) ? result.toLocaleString(Qt.locale(), Locale.LongFormat) : qsTr("Unknown")
    }
    function size(value) {
        const total = Math.max(0, Number(value || 0))
        const exact = total.toLocaleString(Qt.locale(), "f", 0) + " B"
        if (total < 1024) return exact
        const units = ["KiB", "MiB", "GiB", "TiB", "PiB", "EiB"]
        let scaled = total / 1024, index = 0
        while (scaled >= 1024 && index < units.length - 1) { scaled /= 1024; ++index }
        return qsTr("%1 %2 (%3)").arg(scaled.toLocaleString(Qt.locale(), "f", 2)).arg(units[index]).arg(exact)
    }
    header: Item {
        implicitHeight: root.theme.dp(50)
        Rectangle {
            anchors.fill: parent; radius: root.theme.dp(12); color: root.theme.field
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: parent.height / 2; color: parent.color }
        }
        Text { anchors.left: parent.left; anchors.leftMargin: root.theme.dp(20); anchors.verticalCenter: parent.verticalCenter; text: root.title; color: root.theme.foreground; font.pixelSize: root.theme.dp(14) }
        RedesignButton {
            objectName: "folderInfoHeaderClose"
            anchors.right: parent.right; anchors.rightMargin: root.theme.dp(8); anchors.verticalCenter: parent.verticalCenter
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            iconName: "close"; text: qsTr("Close"); onClicked: root.reject()
        }
    }
    contentItem: C.ScrollView {
        id: scroll
        objectName: "folderInfoScroll"
        clip: true; contentWidth: availableWidth
        C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
        ColumnLayout {
            width: scroll.availableWidth
            spacing: root.theme.dp(16)
            Text {
                objectName: "folderInfoName"
                Layout.fillWidth: true; Layout.leftMargin: root.theme.dp(20); Layout.rightMargin: root.theme.dp(20); Layout.topMargin: root.theme.dp(24)
                text: root.loading ? qsTr("Loading folder…") : root.info.name || ""
                color: root.theme.bright; font.pixelSize: root.theme.dp(16); wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true; Layout.leftMargin: root.theme.dp(20); Layout.rightMargin: root.theme.dp(20)
                visible: root.info.success === true
                text: qsTr("Counts and size apply to this folder only.")
                color: root.theme.foreground; font.pixelSize: root.theme.dp(14); wrapMode: Text.WordWrap
            }
            Repeater {
                model: root.info.success === true ? [
                    [qsTr("Files"), Number(root.info.files || 0).toLocaleString(Qt.locale(), "f", 0)],
                    [qsTr("Subfolders"), Number(root.info.folders || 0).toLocaleString(Qt.locale(), "f", 0)],
                    [qsTr("Size of files"), root.size(root.info.size)],
                    [qsTr("Created"), root.date(root.info.created)],
                    [qsTr("Modified"), root.date(root.info.modified)],
                    [qsTr("Access"), root.info.writable ? qsTr("Read and write") : qsTr("Read only")]
                ] : []
                RowLayout {
                    required property var modelData
                    Layout.fillWidth: true; Layout.leftMargin: root.theme.dp(20); Layout.rightMargin: root.theme.dp(20)
                    spacing: root.theme.dp(20)
                    Text { Layout.preferredWidth: Math.max(root.theme.dp(100), scroll.availableWidth * .3); text: modelData[0]; color: root.theme.foreground; font.pixelSize: root.theme.dp(14); wrapMode: Text.WordWrap }
                    Text { Layout.fillWidth: true; text: modelData[1]; color: root.theme.bright; font.pixelSize: root.theme.dp(14); wrapMode: Text.WordWrap }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true; Layout.leftMargin: root.theme.dp(20); Layout.rightMargin: root.theme.dp(20); Layout.bottomMargin: root.theme.dp(20)
                visible: !root.loading && !!root.info.location
                Text { text: qsTr("Location"); color: root.theme.foreground; font.pixelSize: root.theme.dp(14) }
                TextEdit {
                    objectName: "folderInfoLocation"
                    Layout.fillWidth: true; Layout.preferredHeight: contentHeight
                    text: root.info.location || ""; textFormat: TextEdit.PlainText
                    readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere
                    color: root.theme.bright; font.pixelSize: root.theme.dp(14)
                    selectionColor: root.theme.accent; selectedTextColor: root.theme.field
                }
            }
        }
    }
    footer: Column {
        RedesignAddonSections {
            width: parent.width
            hook: "media.details.sections@1"
            hostContext: ({surface: "media.details.sections@1", identity: {title: root.info.name || ""}})
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        }
        Item {
        width: parent.width
        implicitHeight: root.theme.dp(70) + (error.visible ? error.implicitHeight + root.theme.dp(12) : 0)
        Text {
            id: error
            objectName: "folderInfoError"
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: root.theme.dp(20); anchors.topMargin: root.theme.dp(8)
            visible: root.errorMessage.length > 0
            text: root.errorMessage; color: root.theme.danger; font.pixelSize: root.theme.dp(14); wrapMode: Text.WordWrap
            Accessible.role: Accessible.AlertMessage
        }
        RedesignButton {
            objectName: "folderInfoClose"
            anchors.right: parent.right; anchors.rightMargin: root.theme.dp(20); anchors.bottom: parent.bottom; anchors.bottomMargin: root.theme.dp(19)
            implicitWidth: root.theme.dp(100); implicitHeight: root.theme.dp(32)
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            caption: qsTr("Close"); text: caption; onClicked: root.reject()
        }
        }
    }
}

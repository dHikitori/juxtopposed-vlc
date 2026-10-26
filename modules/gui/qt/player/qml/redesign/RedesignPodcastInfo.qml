// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

C.Dialog {
    id: root
    property real scaleFactor: 1
    property bool lightTheme: false
    property color accentColor: "#d37918"
    property var info: ({})
    readonly property var episode: info.podcast || ({})
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    title: qsTr("Episode details")
    anchors.centerIn: parent
    width: Math.min(theme.dp(680), parent ? parent.width - theme.dp(32) : theme.dp(680))
    height: Math.min(theme.dp(600), parent ? parent.height - theme.dp(32) : theme.dp(600))
    modal: true; dim: true; focus: true
    closePolicy: C.Popup.CloseOnEscape | C.Popup.CloseOnPressOutside
    padding: theme.dp(20)
    background: Rectangle { radius: root.theme.dp(12); color: root.theme.background }
    C.Overlay.modal: Rectangle { color: "#80000000" }
    header: Item {
        implicitHeight: root.theme.dp(50)
        Text { anchors.left: parent.left; anchors.leftMargin: root.theme.dp(20); anchors.verticalCenter: parent.verticalCenter; text: root.title; color: root.theme.foreground; font.pixelSize: root.theme.dp(16) }
        RedesignButton {
            anchors.right: parent.right; anchors.rightMargin: root.theme.dp(8); anchors.verticalCenter: parent.verticalCenter
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            iconName: "close"; text: qsTr("Close"); onClicked: root.close()
        }
    }
    contentItem: C.ScrollView {
        id: scroll
        clip: true; contentWidth: availableWidth
        C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
        ColumnLayout {
            width: scroll.availableWidth
            spacing: root.theme.dp(14)
            Text { Layout.fillWidth: true; text: root.info.name || ""; textFormat: Text.PlainText; wrapMode: Text.WordWrap; color: root.theme.bright; font.pixelSize: root.theme.dp(18) }
            Text { Layout.fillWidth: true; text: [root.episode.show, root.episode.author, root.episode.date].filter(value => !!value).join("\n"); textFormat: Text.PlainText; wrapMode: Text.WordWrap; color: root.theme.foreground; font.pixelSize: root.theme.dp(14) }
            Text {
                Layout.fillWidth: true
                text: [root.episode.season ? qsTr("Season %1").arg(root.episode.season) : "", root.episode.episode ? qsTr("Episode %1").arg(root.episode.episode) : "", root.episode.duration && root.episode.duration.valid && root.episode.duration.valid() ? root.episode.duration.formatHMS(0) : "", root.episode.episodeType, ["true", "yes"].includes(root.episode.explicit) ? qsTr("Explicit content") : ""].filter(value => !!value).join(" · ")
                textFormat: Text.PlainText; wrapMode: Text.WordWrap; color: root.theme.playerLabel; font.pixelSize: root.theme.dp(14)
            }
            C.TextArea {
                objectName: "episodeNotes"
                Layout.fillWidth: true
                text: root.episode.description || ""
                textFormat: TextEdit.PlainText; readOnly: true; wrapMode: TextEdit.Wrap
                selectByMouse: true
                color: root.theme.foreground; font.pixelSize: root.theme.dp(14)
                background: null; padding: 0
            }
            RedesignButton {
                visible: !!root.episode.transcript
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                caption: qsTr("Open transcript"); text: caption
                onClicked: if (/^https?:\/\//i.test(root.episode.transcript)) Qt.openUrlExternally(root.episode.transcript)
            }
            RedesignAddonSections {
                Layout.fillWidth: true
                hook: "media.details.sections@1"
                hostContext: ({surface: "media.details.sections@1", identity: {title: root.info.name || ""}})
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            }
        }
    }
}

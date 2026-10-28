// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts

Column {
    id: root
    required property var registry
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme }
    spacing: theme.dp(8)
    Text { text: qsTr("Add-on views"); color: root.theme.settingsHeading; font.pixelSize: root.theme.dp(16) }
    Text {
        width: parent.width
        text: qsTr("Select a replacement for each view. The app uses the built-in view if an add-on fails.")
        textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
    }
    Repeater {
        model: [{hook: "home.content@1", label: qsTr("Home")},
            {hook: "actions.subtitles.open@1", label: qsTr("Open subtitle search")},
            {hook: "subtitles.search.content@1", label: qsTr("Subtitle selection")},
            {hook: "dialog.add-to-playlist.content@1", label: qsTr("Playlist selection")}]
        Column {
            id: row
            required property var modelData
            readonly property var choices: {
                root.registry.revision
                return [{value: "", label: qsTr("Built-in")}].concat(root.registry.replacementChoices(modelData.hook).map(entry => ({value: entry.id, label: entry.name || entry.id})))
            }
            readonly property string selected: { root.registry.revision; return root.registry.replacement(modelData.hook).id || "" }
            width: root.width; spacing: root.theme.dp(4)
            Text { text: row.modelData.label; textFormat: Text.PlainText; color: root.theme.foreground; font.pixelSize: root.theme.dp(14) }
            RedesignComboBox {
                width: parent.width
                objectName: "addonReplacement_" + row.modelData.hook
                model: row.choices; textRole: "label"; valueRole: "value"
                labelTextFormat: Text.PlainText
                currentIndex: Math.max(0, row.choices.findIndex(entry => entry.value === row.selected))
                Accessible.name: row.modelData.label
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onActivated: index => root.registry.selectReplacement(row.modelData.hook, row.choices[index].value)
                contentItem: Text { text: parent.displayText; textFormat: Text.PlainText; color: root.theme.foreground; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight; font.pixelSize: root.theme.dp(14) }
            }
        }
    }
}

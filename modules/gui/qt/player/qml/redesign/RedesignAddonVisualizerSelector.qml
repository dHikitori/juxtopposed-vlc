// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

Column {
    id: root
    required property var controller
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme }
    readonly property var choices: [{value: "", label: qsTr("Artwork")}].concat((controller.availableVisualizers || []).filter(entry => entry.id).map(entry => ({value: entry.id, label: entry.name || entry.id})))
    spacing: theme.dp(6)
    Text { text: qsTr("Music visualizer"); color: root.theme.foreground; font.pixelSize: root.theme.dp(14) }
    RedesignComboBox {
        objectName: "addonVisualizerSelection"
        width: parent.width
        model: root.choices; textRole: "label"; valueRole: "value"; labelTextFormat: Text.PlainText
        currentIndex: Math.max(0, root.choices.findIndex(entry => entry.value === root.controller.selectedVisualizer))
        Accessible.name: qsTr("Music visualizer")
        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
        onActivated: index => root.controller.selectVisualizer(root.choices[index].value)
    }
    Text {
        width: parent.width
        visible: text.length > 0
        text: root.controller.errorMessage || ""; textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: root.theme.danger; font.pixelSize: root.theme.dp(12)
        Accessible.role: Accessible.AlertMessage
    }
}

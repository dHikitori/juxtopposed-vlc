// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var controller
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme }
    readonly property var choices: [{value: "", label: qsTr("Artwork")}].concat((controller.availableVisualizers || []).filter(entry => entry.id).map(entry => ({value: entry.id, label: entry.name || entry.id})))
    spacing: 0
    RowLayout {
        objectName: "addonVisualizerRow"
        Layout.fillWidth: true; Layout.minimumHeight: root.theme.dp(46)
        spacing: root.theme.dp(10)
        Text {
            objectName: "addonVisualizerLabel"
            Layout.fillWidth: true; Layout.minimumWidth: 0
            Layout.preferredWidth: root.theme.dp(320 * 57 / 43 - 10)
            text: qsTr("Music visualizer"); textFormat: Text.PlainText; wrapMode: Text.Wrap
            color: root.theme.foreground; font.pixelSize: root.theme.dp(14)
        }
        RedesignComboBox {
            objectName: "addonVisualizerSelection"
            Layout.fillWidth: true; Layout.minimumWidth: 0
            Layout.preferredWidth: root.theme.dp(320); Layout.maximumWidth: root.theme.dp(320)
            model: root.choices; textRole: "label"; valueRole: "value"; labelTextFormat: Text.PlainText
            currentIndex: Math.max(0, root.choices.findIndex(entry => entry.value === root.controller.selectedVisualizer))
            Accessible.name: qsTr("Music visualizer")
            scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
            onActivated: index => {
                root.controller.selectVisualizer(root.choices[index].value)
                currentIndex = Qt.binding(() => Math.max(0, root.choices.findIndex(entry => entry.value === root.controller.selectedVisualizer)))
            }
        }
    }
    Text {
        Layout.fillWidth: true; Layout.bottomMargin: visible ? root.theme.dp(6) : 0
        visible: text.length > 0
        text: root.controller.errorMessage || ""; textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: root.theme.danger; font.pixelSize: root.theme.dp(12)
        Accessible.role: Accessible.AlertMessage
    }
}

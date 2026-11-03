// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

ColumnLayout {
    id: root
    property var plugins: []
    property real scaleFactor: 1
    property bool lightTheme: false
    property color accentColor: "#d37918"
    property string sortColumn: "name"
    property bool sortDescending: false
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    readonly property real typeWidth: Math.min(theme.dp(210), Math.max(theme.dp(70), width * .28))
    readonly property real priorityWidth: theme.dp(72)
    readonly property var visiblePlugins: {
        const query = search.text.trim().toLocaleLowerCase()
        const rows = plugins.filter(plugin => !query || [plugin.name, plugin.type, plugin.identifier].some(value => String(value || "").toLocaleLowerCase().includes(query)))
        const direction = sortDescending ? -1 : 1
        return rows.sort((a, b) => {
            const difference = sortColumn === "priority" ? Number(a.priority) - Number(b.priority)
                : String(a[sortColumn] || "").localeCompare(String(b[sortColumn] || ""))
            return direction * difference || String(a.name || "").localeCompare(String(b.name || ""))
                || String(a.identifier || "").localeCompare(String(b.identifier || ""))
        })
    }
    spacing: theme.dp(8)
    function sortBy(column) {
        sortDescending = sortColumn === column ? !sortDescending : false
        sortColumn = column
    }
    Text { text: qsTr("Plugins"); color: theme.settingsHeading; font.pixelSize: theme.dp(16); font.weight: Font.Medium; Layout.topMargin: theme.dp(20); Layout.bottomMargin: theme.dp(2) }
    Rectangle {
        Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredHeight: theme.dp(228)
        color: theme.field; radius: theme.dp(6); border.width: theme.dp(2)
        border.color: list.activeFocus ? theme.accent : root.lightTheme ? theme.border : "#191919"
        ColumnLayout {
            anchors.fill: parent; anchors.margins: theme.dp(2); spacing: 0
            RowLayout {
                Layout.fillWidth: true; Layout.preferredHeight: theme.dp(34)
                Layout.leftMargin: theme.dp(10); Layout.rightMargin: theme.dp(10); spacing: theme.dp(10)
                Header { objectName: "pluginsNameHeader"; Layout.fillWidth: true; Layout.minimumWidth: 0; column: "name"; caption: qsTr("Name"); text: qsTr("Sort by name") }
                Header { objectName: "pluginsTypeHeader"; Layout.preferredWidth: root.typeWidth; Layout.minimumWidth: 0; column: "type"; caption: qsTr("Type"); text: qsTr("Sort by type") }
                Header {
                    objectName: "pluginsPriorityHeader"
                    Layout.preferredWidth: root.priorityWidth; Layout.minimumWidth: 0; column: "priority"; caption: qsTr("Priority")
                    text: qsTr("Sort by priority. Hakone uses priority to select a plugin when more than one plugin can do the same task.")
                }
            }
            Rectangle { Layout.fillWidth: true; height: theme.dp(1); color: root.lightTheme ? theme.border : "#191919" }
            C.ScrollView {
                Layout.fillWidth: true; Layout.fillHeight: true; contentWidth: availableWidth; clip: true
                ListView {
                    id: list
                    objectName: "pluginsList"
                    model: root.visiblePlugins; currentIndex: -1
                    boundsBehavior: Flickable.StopAtBounds
                    onModelChanged: currentIndex = -1
                    Keys.onPressed: event => {
                        if (![Qt.Key_Up, Qt.Key_Down, Qt.Key_Home, Qt.Key_End].includes(event.key) || !count) return
                        currentIndex = event.key === Qt.Key_Home ? 0 : event.key === Qt.Key_End ? count - 1
                            : Math.max(0, Math.min(count - 1, currentIndex + (event.key === Qt.Key_Up ? -1 : 1)))
                        positionViewAtIndex(currentIndex, ListView.Contain); event.accepted = true
                    }
                    delegate: C.ItemDelegate {
                        id: row
                        required property int index
                        required property var modelData
                        objectName: "pluginRow_" + index
                        width: ListView.view.width; height: theme.dp(34)
                        leftPadding: theme.dp(10); rightPadding: theme.dp(10)
                        highlighted: ListView.isCurrentItem
                        Accessible.name: qsTr("%1, %2, priority %3").arg(modelData.name).arg(modelData.type).arg(modelData.priority)
                        background: Rectangle { color: row.highlighted ? theme.selected : row.hovered ? theme.hover : "transparent"; radius: theme.dp(4) }
                        contentItem: RowLayout {
                            spacing: theme.dp(10)
                            Cell { objectName: "pluginName"; Layout.fillWidth: true; Layout.minimumWidth: 0; text: row.modelData.name }
                            Cell { objectName: "pluginType"; Layout.preferredWidth: root.typeWidth; Layout.minimumWidth: 0; text: row.modelData.type; color: theme.playerLabel }
                            Cell { objectName: "pluginPriority"; Layout.preferredWidth: root.priorityWidth; Layout.minimumWidth: 0; text: row.modelData.priority; horizontalAlignment: Text.AlignRight }
                        }
                        onClicked: { list.currentIndex = index; list.forceActiveFocus() }
                        RedesignToolTip {
                            theme: root.theme; contentTextFormat: Text.PlainText
                            text: qsTr("%1\nType: %2\nPriority: %3\nPlugin: %4").arg(row.modelData.name).arg(row.modelData.type).arg(row.modelData.priority).arg(row.modelData.identifier)
                            visible: row.hovered
                        }
                    }
                }
            }
        }
        Text {
            anchors.centerIn: parent; visible: root.visiblePlugins.length === 0
            text: qsTr("No plugins found."); color: theme.playerLabel; font.pixelSize: theme.dp(14)
        }
    }
    RowLayout {
        Layout.fillWidth: true; spacing: theme.dp(10)
        C.TextField {
            id: search
            objectName: "pluginsSearch"
            Layout.fillWidth: true; Layout.preferredWidth: theme.dp(210); Layout.maximumWidth: theme.dp(210); Layout.minimumWidth: 0
            implicitHeight: theme.dp(36)
            placeholderText: qsTr("Search"); placeholderTextColor: root.lightTheme ? theme.foreground : "#404040"
            color: theme.foreground; font.pixelSize: theme.dp(14); selectByMouse: true
            Accessible.name: qsTr("Search plugins")
            background: Rectangle { color: theme.field; radius: theme.dp(6); border.width: theme.dp(2); border.color: search.activeFocus ? theme.accent : root.lightTheme ? theme.border : "#191919" }
        }
        Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }
        Text {
            objectName: "pluginsCount"
            text: qsTr("%n plugin", "%n plugins", root.visiblePlugins.length)
            color: theme.playerLabel; font.pixelSize: theme.dp(12)
        }
    }
    component Cell: Text {
        textFormat: Text.PlainText; color: theme.foreground; font.pixelSize: theme.dp(14); elide: Text.ElideRight
    }
    component Header: RedesignButton {
        required property string column
        implicitHeight: theme.dp(30)
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        selected: root.sortColumn === column
        onClicked: root.sortBy(column)
        contentItem: RowLayout {
            spacing: theme.dp(3)
            Text { Layout.fillWidth: true; Layout.minimumWidth: 0; text: parent.parent.caption; textFormat: Text.PlainText; color: parent.parent.selected ? theme.accent : theme.foreground; font.pixelSize: theme.dp(13); elide: Text.ElideRight }
            Item {
                visible: parent.parent.selected
                implicitWidth: theme.dp(14); implicitHeight: theme.dp(14)
                Layout.preferredWidth: theme.dp(14); Layout.preferredHeight: theme.dp(14)
                RedesignIcon {
                    anchors.fill: parent
                    name: root.sortDescending ? "chevron-down" : "chevron-up"; color: theme.accent
                }
            }
        }
    }
}

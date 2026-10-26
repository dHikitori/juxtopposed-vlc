// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root
    property var providers: []
    property string feature: ""
    property string heading: qsTr("Metadata providers")
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme }
    signal enabledRequested(string id, bool enabled)
    signal moveRequested(string id, int direction)
    signal configureRequested(string packageId)
    spacing: theme.dp(8)
    Text {
        Layout.fillWidth: true
        text: root.heading; textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: root.theme.settingsHeading; font.pixelSize: root.theme.dp(16)
    }
    Text {
        Layout.fillWidth: true
        text: qsTr("The app uses this provider order for the selected feature.")
        textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
    }
    Repeater {
        model: root.providers
        Rectangle {
            id: row
            required property var modelData
            required property int index
            objectName: "addonProvider_" + modelData.id
            Layout.fillWidth: true
            implicitHeight: content.implicitHeight + root.theme.dp(20)
            color: root.theme.field; radius: root.theme.dp(6)
            ColumnLayout {
                id: content
                anchors.fill: parent; anchors.margins: root.theme.dp(10)
                spacing: root.theme.dp(5)
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: (row.index + 1) + ". " + row.modelData.name
                        textFormat: Text.PlainText; wrapMode: Text.Wrap
                        color: root.theme.foreground; font.pixelSize: root.theme.dp(14)
                    }
                    RedesignSwitchRow {
                        objectName: "addonProviderEnabled_" + row.modelData.id
                        accessibleName: row.modelData.name
                        checked: row.modelData.enabled === true
                        enabled: row.modelData.available !== false
                        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                        onToggled: checked => root.enabledRequested(row.modelData.id, checked)
                    }
                }
                Text {
                    objectName: "addonProviderStatus_" + row.modelData.id
                    Layout.fillWidth: true
                    text: row.modelData.status || (row.modelData.configured === false ? qsTr("Not configured") : row.modelData.enabled ? qsTr("Ready") : qsTr("Disabled"))
                    textFormat: Text.PlainText; wrapMode: Text.Wrap
                    color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
                }
                Flow {
                    Layout.fillWidth: true; spacing: root.theme.dp(6)
                    RedesignButton {
                        objectName: "addonProviderUp_" + row.modelData.id
                        iconName: "chevron-up"; text: qsTr("Increase the priority of %1").arg(row.modelData.name)
                        enabled: row.index > 0
                        tooltipTextFormat: Text.PlainText
                        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                        onClicked: root.moveRequested(row.modelData.id, -1)
                    }
                    RedesignButton {
                        objectName: "addonProviderDown_" + row.modelData.id
                        iconName: "chevron-down"; text: qsTr("Decrease the priority of %1").arg(row.modelData.name)
                        enabled: row.index + 1 < root.providers.length
                        tooltipTextFormat: Text.PlainText
                        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                        onClicked: root.moveRequested(row.modelData.id, 1)
                    }
                    RedesignButton {
                        objectName: "addonProviderConfigure_" + row.modelData.id
                        caption: qsTr("Configure…"); text: qsTr("Configure %1").arg(row.modelData.name)
                        visible: !!row.modelData.packageId
                        tooltipTextFormat: Text.PlainText
                        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                        onClicked: root.configureRequested(row.modelData.packageId)
                    }
                }
            }
        }
    }
    Text {
        objectName: "addonProvidersEmpty"
        Layout.fillWidth: true; visible: root.providers.length === 0
        text: qsTr("Install a provider for this feature."); textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: root.theme.foreground; font.pixelSize: root.theme.dp(14)
    }
}

// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

Item {
    id: root
    property var registry: typeof redesignAddonUi === "undefined" ? null : redesignAddonUi
    required property string hook
    property var hostContext: ({surface: hook})
    property Component builtIn
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    readonly property var replacement: {
        if (!registry) return ({})
        registry.revision
        return registry.replacement(hook)
    }
    readonly property bool replaced: mount.usable
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme }
    ColumnLayout {
        anchors.fill: parent; spacing: root.theme.dp(6)
        RedesignButton {
            objectName: "addonSurfaceRecovery"
            visible: !!root.replacement.id
            caption: qsTr("Use built-in view"); text: caption
            tooltipTextFormat: Text.PlainText
            scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
            onClicked: root.registry.selectReplacement(root.hook, "")
        }
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true
            Loader { anchors.fill: parent; active: !root.replaced; sourceComponent: root.builtIn }
            Flickable {
                anchors.fill: parent; visible: root.replaced
                contentWidth: width; contentHeight: mount.height
                boundsBehavior: Flickable.StopAtBounds; clip: true
                C.ScrollBar.vertical: C.ScrollBar { }
                RedesignAddonMount {
                    id: mount
                    width: parent.width; height: implicitHeight
                    registry: root.registry; contributionId: root.replacement.id || ""
                    hostContext: root.hostContext
                    scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                }
            }
        }
    }
}

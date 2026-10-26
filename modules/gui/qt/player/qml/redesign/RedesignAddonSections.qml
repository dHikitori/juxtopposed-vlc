// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

Column {
    id: root
    property var registry: typeof redesignAddonUi === "undefined" ? null : redesignAddonUi
    required property string hook
    property var hostContext: ({surface: hook})
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    readonly property var contributions: {
        if (!registry) return []
        registry.revision
        return registry.sections(hook).slice(0, 8)
    }
    spacing: 12 * scaleFactor
    Repeater {
        model: root.contributions
        RedesignAddonMount {
            required property var modelData
            width: root.width; height: implicitHeight
            registry: root.registry; contributionId: modelData.id
            hostContext: root.hostContext
            scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
        }
    }
}

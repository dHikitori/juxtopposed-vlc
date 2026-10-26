// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

ColumnLayout {
    id: root
    required property var backend
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme }
    readonly property var choices: {
        backend.revision
        return [{value: "", label: qsTr("Built-in")}].concat((backend.availableThemes || []).map(entry => ({value: entry.id, label: entry.name})))
    }
    readonly property bool missingVariant: {
        const selected = (backend.availableThemes || []).find(entry => entry.id === backend.selectedTheme)
        return !!selected && (root.lightTheme ? selected.light === false : selected.dark === false)
    }
    spacing: theme.dp(6)
    Text {
        Layout.fillWidth: true
        text: qsTr("Theme pack"); textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: root.theme.foreground; font.pixelSize: root.theme.dp(14)
    }
    RedesignComboBox {
        id: selector
        objectName: "addonThemeSelection"
        Layout.fillWidth: true
        model: root.choices; textRole: "label"; valueRole: "value"
        currentIndex: Math.max(0, root.choices.findIndex(entry => entry.value === root.backend.selectedTheme))
        Accessible.name: qsTr("Theme pack")
        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
        onActivated: index => root.backend.selectTheme(root.choices[index].value)
        contentItem: Text {
            text: selector.displayText; textFormat: Text.PlainText; elide: Text.ElideRight
            color: root.theme.foreground; font.pixelSize: root.theme.dp(14); verticalAlignment: Text.AlignVCenter
        }
        delegate: C.ItemDelegate {
            id: choice
            required property int index
            width: ListView.view.width; implicitHeight: root.theme.dp(36)
            highlighted: selector.highlightedIndex === index
            contentItem: Text { text: root.choices[choice.index].label; textFormat: Text.PlainText; elide: Text.ElideRight; color: root.theme.foreground; font.pixelSize: root.theme.dp(14) }
            background: Rectangle { color: choice.highlighted ? root.theme.selected : choice.hovered ? root.theme.hover : root.theme.panel }
        }
    }
    Text {
        Layout.fillWidth: true
        text: qsTr("The app changes the theme immediately. Enable a theme add-on to add its theme to this list.")
        textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: root.theme.foreground; font.pixelSize: root.theme.dp(12)
    }
    RowLayout {
        Layout.fillWidth: true
        Text { Layout.fillWidth: true; text: qsTr("High contrast"); textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.foreground; font.pixelSize: root.theme.dp(14) }
        RedesignButton {
            id: contrast
            objectName: "addonThemeHighContrast"
            checkable: true; checked: root.backend.highContrast === true; selected: checked
            caption: checked ? qsTr("On") : qsTr("Off"); text: qsTr("High contrast")
            enabled: root.backend.platformHighContrast !== true
            scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
            onClicked: {
                root.backend.setHighContrast(checked)
                checked = Qt.binding(() => root.backend.highContrast === true)
            }
        }
    }
    RedesignButton {
        objectName: "addonThemeReducedMotion"
        checkable: true; checked: root.backend.reducedMotion === true; selected: checked
        caption: qsTr("Reduce motion for add-on visuals"); text: caption
        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
        onClicked: {
            root.backend.setReducedMotion(checked)
            checked = Qt.binding(() => root.backend.reducedMotion === true)
        }
    }
    RedesignButton {
        objectName: "addonThemeRestoreDefault"
        visible: !!root.backend.selectedTheme
        caption: qsTr("Use built-in theme"); text: caption
        tooltipTextFormat: Text.PlainText
        captionMaximumWidth: Math.max(0, root.width - root.theme.dp(24))
        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
        onClicked: root.backend.selectTheme("")
    }
    Text {
        objectName: "addonThemeFallbackStatus"
        Layout.fillWidth: true
        visible: text.length > 0
        text: root.backend.errorMessage || (root.missingVariant ? qsTr("This theme uses built-in colors for the current light or dark mode.") : "")
        textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: root.backend.usingFallback ? root.theme.danger : root.theme.foreground
        font.pixelSize: root.theme.dp(12)
        Accessible.role: root.backend.usingFallback ? Accessible.AlertMessage : Accessible.StaticText
    }
}

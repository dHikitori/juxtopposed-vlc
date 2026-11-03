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
    spacing: 0
    RowLayout {
        objectName: "addonThemeRow"
        Layout.fillWidth: true
        Layout.minimumHeight: root.theme.dp(46)
        spacing: root.theme.dp(10)
        Item {
            Layout.fillWidth: true; Layout.minimumWidth: 0
            Layout.preferredWidth: root.theme.dp(320 * 57 / 43 - 10)
            implicitHeight: label.implicitHeight + root.theme.dp(10)
            HoverHandler { id: rowHover }
            Text {
                id: label
                objectName: "addonThemeLabel"
                width: Math.min(implicitWidth, Math.max(0, parent.width - root.theme.dp(70)))
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Theme pack"); textFormat: Text.PlainText; wrapMode: Text.Wrap
                color: root.theme.foreground; font.pixelSize: root.theme.dp(14)
            }
            // Same help control and reset rule as RedesignPreferenceRow.
            C.ToolButton {
                id: help
                objectName: "addonThemeHelp"
                readonly property string helpText: qsTr("Choose an enabled theme pack. The app applies the theme at once.")
                width: root.theme.dp(24); height: width; padding: root.theme.dp(3)
                x: label.width + root.theme.dp(8); anchors.verticalCenter: parent.verticalCenter
                focusPolicy: Qt.StrongFocus
                hoverEnabled: true
                Accessible.name: qsTr("About theme packs"); Accessible.description: helpText
                onClicked: hint.visible ? hint.close() : hint.open()
                contentItem: RedesignIcon {
                    name: "info"; color: root.theme.foreground
                    opacity: help.hovered || help.visualFocus || hint.visible ? 1 : 0.4
                }
                background: Rectangle {
                    radius: root.theme.dp(4)
                    color: help.hovered ? root.theme.hover : "transparent"
                    border.width: help.visualFocus ? 1 : 0
                    border.color: root.theme.accent
                }
                C.Popup {
                    id: hint
                    objectName: "addonThemeHelpPopup"
                    popupType: C.Popup.Item
                    y: help.height + root.theme.dp(4)
                    width: Math.min(root.theme.dp(360), root.width)
                    x: Math.max(-help.x, Math.min(0, root.width - help.x - width))
                    padding: root.theme.dp(12); margins: root.theme.dp(6)
                    focus: true
                    closePolicy: C.Popup.CloseOnEscape | C.Popup.CloseOnPressOutside
                    contentItem: Text {
                        text: help.helpText; textFormat: Text.PlainText; wrapMode: Text.Wrap
                        color: root.theme.bright; font.pixelSize: root.theme.dp(14)
                    }
                    background: Rectangle { radius: root.theme.dp(6); color: root.theme.panel; border.width: 1; border.color: root.theme.border }
                }
            }
            RedesignToolTip {
                theme: root.theme; text: help.helpText
                visible: (rowHover.hovered || help.visualFocus) && !hint.visible && !reset.hovered && !reset.visualFocus
                delay: help.hovered || help.visualFocus ? 0 : 500
            }
            RedesignButton {
                id: reset
                objectName: "addonThemeRestoreDefault"
                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                width: root.theme.dp(26); height: root.theme.dp(28); iconSize: root.theme.dp(18)
                iconName: "history"; text: qsTr("Reset to default"); tooltipTextFormat: Text.PlainText
                Accessible.name: qsTr("Reset theme pack to default")
                // Keyboard focus in the row also shows it, so Tab can reach it;
                // focus left by a mouse click does not.
                visible: rowHover.hovered || hovered || visualFocus || help.visualFocus || selector.visualFocus
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.backend.selectTheme("")
            }
        }
        RedesignComboBox {
            id: selector
            objectName: "addonThemeSelection"
            Layout.fillWidth: true; Layout.minimumWidth: 0
            Layout.preferredWidth: root.theme.dp(320); Layout.maximumWidth: root.theme.dp(320)
            model: root.choices; textRole: "label"; valueRole: "value"; labelTextFormat: Text.PlainText
            currentIndex: Math.max(0, root.choices.findIndex(entry => entry.value === root.backend.selectedTheme))
            Accessible.name: qsTr("Theme pack")
            scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
            onActivated: index => {
                root.backend.selectTheme(root.choices[index].value)
                currentIndex = Qt.binding(() => Math.max(0, root.choices.findIndex(entry => entry.value === root.backend.selectedTheme)))
            }
        }
    }
    Text {
        objectName: "addonThemeFallbackStatus"
        Layout.fillWidth: true
        Layout.bottomMargin: visible ? root.theme.dp(6) : 0
        visible: text.length > 0
        text: root.backend.errorMessage || (root.missingVariant ? qsTr("This theme uses built-in colors for the current light or dark mode.") : "")
        textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: root.backend.usingFallback ? root.theme.danger : root.theme.foreground
        font.pixelSize: root.theme.dp(12)
        Accessible.role: root.backend.usingFallback ? Accessible.AlertMessage : Accessible.StaticText
    }
}

// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts

Column {
    id: root
    required property var backend
    required property var colorScheme
    property var themeBackend: null
    property var appearanceRows: []
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }

    Text {
        topPadding: theme.dp(7.5); bottomPadding: theme.dp(9.5)
        text: qsTr("Appearance"); color: theme.settingsHeading
        font.pixelSize: theme.dp(16); font.weight: Font.Medium
    }
    RedesignPreferenceRow {
        id: language
        objectName: "settingsInterfaceLanguage"
        width: parent.width; backend: root.backend
        optionName: "language"; label: qsTr("Interface language")
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
    }
    Loader {
        width: parent.width; active: root.themeBackend !== null
        sourceComponent: RedesignAddonThemeSelector {
            backend: root.themeBackend
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        }
    }
    RowLayout {
        width: parent.width; height: Math.max(theme.dp(46), themeLabel.implicitHeight + theme.dp(10))
        spacing: theme.dp(10)
        Text {
            id: themeLabel
            Layout.fillWidth: true; Layout.minimumWidth: 0
            Layout.preferredWidth: theme.dp(320 * 57 / 43 - 10)
            text: qsTr("Theme"); textFormat: Text.PlainText; wrapMode: Text.Wrap
            color: theme.foreground; font.pixelSize: theme.dp(14)
        }
        Rectangle {
            id: themeChoices
            objectName: "settingsThemeChoices"
            Layout.fillWidth: true; Layout.minimumWidth: 0
            Layout.preferredWidth: theme.dp(320); Layout.maximumWidth: theme.dp(320)
            height: theme.dp(36); radius: theme.dp(6); color: theme.field
            Row {
                anchors.fill: parent; anchors.margins: theme.dp(2); spacing: theme.dp(2)
                Repeater {
                    model: [{label: qsTr("Dark"), value: 2}, {label: qsTr("Light"), value: 1}, {label: qsTr("System"), value: 0}]
                    RedesignButton {
                        required property var modelData
                        objectName: "settingsThemeMode" + modelData.value
                        width: (themeChoices.width - theme.dp(8)) / 3; height: theme.dp(32)
                        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                        caption: modelData.label; text: caption
                        captionMaximumWidth: Math.max(0, width - theme.dp(8))
                        selected: root.colorScheme.scheme === modelData.value
                        background: Rectangle {
                            color: parent.selected ? theme.progress : root.lightTheme ? theme.background : "#191919"
                            radius: theme.dp(4); border.width: parent.visualFocus ? theme.dp(2) : 0; border.color: theme.focusRing
                        }
                        onClicked: {
                            root.colorScheme.setData(root.colorScheme.index(modelData.value, 0), true, Qt.CheckStateRole)
                            root.backend.save()
                        }
                    }
                }
            }
        }
    }
    RowLayout {
        objectName: "settingsAccentRow"
        width: parent.width; height: Math.max(theme.dp(46), accentLabel.implicitHeight + theme.dp(10))
        spacing: theme.dp(10)
        Item {
            Layout.fillWidth: true; Layout.minimumWidth: 0
            Layout.preferredWidth: theme.dp(320 * 57 / 43 - 10)
            implicitHeight: accentLabel.implicitHeight + theme.dp(10)
            HoverHandler { id: accentHover }
            Text {
                id: accentLabel
                width: Math.min(implicitWidth, Math.max(0, parent.width - theme.dp(34)))
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Accent color"); textFormat: Text.PlainText; wrapMode: Text.Wrap
                color: theme.foreground; font.pixelSize: theme.dp(14)
            }
            RedesignButton {
                objectName: "settingsAccentReset"
                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                width: theme.dp(26); height: theme.dp(28); iconSize: theme.dp(18)
                iconName: "history"; text: qsTr("Reset to default"); tooltipTextFormat: Text.PlainText
                Accessible.name: qsTr("Reset accent color to default")
                visible: accentHover.hovered || accentChoices.activeFocus || hovered || visualFocus
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                onClicked: root.backend.stage("qt-accent-color", "default")
            }
        }
        FocusScope {
            id: accentChoices
            objectName: "settingsAccentChoices"
            Layout.fillWidth: true; Layout.minimumWidth: 0
            Layout.preferredWidth: theme.dp(320); Layout.maximumWidth: theme.dp(320)
            height: theme.dp(36)
            Rectangle { anchors.fill: parent; color: theme.field; radius: theme.dp(6) }
            Row {
                anchors.fill: parent
                Repeater {
                    model: ["#d37918", "#e15032", "#c49e23", "#19a234", "#18aad3", "#185dd3", "#8e42ff", "#f052b3", "#afafaf"]
                    RedesignButton {
                        required property string modelData
                        objectName: "settingsAccent" + modelData.substring(1)
                        readonly property bool active: theme.baseAccent.toString() === modelData
                        width: accentChoices.width / 9; height: theme.dp(36)
                        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                        text: qsTr("Accent color %1").arg(modelData)
                        Accessible.role: Accessible.RadioButton; Accessible.checkable: true; Accessible.checked: active
                        background: Rectangle {
                            anchors.fill: parent; anchors.margins: theme.dp(2)
                            color: parent.active ? theme.progress : "transparent"; radius: theme.dp(4)
                            border.width: parent.visualFocus ? theme.dp(2) : 0; border.color: theme.focusRing
                        }
                        contentItem: Item {
                            Rectangle {
                                anchors.centerIn: parent
                                width: Math.min(theme.dp(20), parent.width - theme.dp(8)); height: width
                                radius: theme.dp(1); color: parent.parent.modelData; opacity: parent.parent.active ? 1 : 0.7
                            }
                        }
                        onClicked: root.backend.stage("qt-accent-color", modelData)
                    }
                }
            }
            TapHandler { acceptedButtons: Qt.RightButton; onTapped: accentMenu.popup() }
            Keys.onMenuPressed: accentMenu.popup()
            Keys.onPressed: event => {
                if (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier)) {
                    accentMenu.popup(); event.accepted = true
                }
            }
            RedesignActionMenu {
                id: accentMenu
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                entries: [{label: qsTr("Custom accent color"), action: "custom"}]
                onActionRequested: {
                    const color = root.backend.chooseColor(theme.baseAccent.toString())
                    if (color.length) root.backend.stage("qt-accent-color", color)
                }
            }
        }
    }
    RedesignSwitchRow {
        objectName: "addonThemeHighContrast"
        width: parent.width; height: theme.dp(42)
        visible: root.themeBackend !== null
        label: qsTr("High contrast")
        checked: root.themeBackend ? root.themeBackend.highContrast === true : false
        enabled: root.themeBackend ? root.themeBackend.platformHighContrast !== true : false
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        onToggled: checked => root.themeBackend.setHighContrast(checked)
    }
    RedesignSwitchRow {
        objectName: "addonThemeReducedMotion"
        width: parent.width; height: theme.dp(42)
        visible: root.themeBackend !== null
        label: qsTr("Reduce motion for add-on visuals")
        checked: root.themeBackend ? root.themeBackend.reducedMotion === true : false
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        onToggled: checked => root.themeBackend.setReducedMotion(checked)
    }
    Repeater {
        model: root.appearanceRows
        delegate: RedesignPreferenceRow {
            required property var modelData
            width: parent.width
            backend: root.backend; optionName: modelData[0]; label: modelData[1]
            presentation: modelData[2] || {}
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        }
    }
}

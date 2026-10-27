// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C
import "AddonUi.js" as Contract

ColumnLayout {
    id: root
    required property string fieldName
    required property var definition
    property var currentValue
    property var messages: ({})
    property var secretStatus: ({})
    property string secretOperation: "keep"
    property bool sessionOnly: false
    property bool credentialPersistenceAvailable: true
    property string validationError: ""
    property int resetGeneration: 0
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme }
    readonly property bool secret: Contract.fieldSecret(definition)
    readonly property string control: definition["x-hakone-control"] || ""
    readonly property string label: localize(definition["x-hakone-label"] || definition.title || fieldName)
    readonly property string help: localize(definition["x-hakone-help"] || definition.description || "")
    readonly property var choices: definition.enum || []
    signal valueEdited(var value)
    signal secretEdited(string operation, string value, bool sessionOnly)
    signal pickerRequested(string fieldName, string kind)
    signal connectionRequested(string fieldName)
    spacing: theme.dp(6)
    function localize(key) { return String(Contract.own(messages, key) ? messages[key] : key) }
    function clearSensitiveInput() { secretInput.clear() }
    onResetGenerationChanged: clearSensitiveInput()
    onSecretOperationChanged: if (secretOperation !== "replace") clearSensitiveInput()
    Component.onDestruction: clearSensitiveInput()

    Text {
        Layout.fillWidth: true
        text: root.label; textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: root.theme.bright; font.pixelSize: root.theme.dp(14)
    }
    RedesignSwitchRow {
        objectName: "addonBoolean_" + root.fieldName
        visible: !root.secret && root.definition.type === "boolean" && !root.choices.length
        Layout.fillWidth: true
        accessibleName: root.label; checked: root.currentValue === true
        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
        onToggled: checked => root.valueEdited(checked)
    }
    RedesignComboBox {
        id: choice
        objectName: "addonChoice_" + root.fieldName
        visible: !root.secret && root.choices.length > 0
        Layout.fillWidth: true
        model: root.choices.map(value => ({label: String(value), value: value}))
        textRole: "label"; valueRole: "value"
        currentIndex: root.choices.indexOf(root.currentValue)
        Accessible.name: root.label
        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
        onActivated: index => root.valueEdited(root.choices[index])
        contentItem: Text {
            text: choice.displayText; textFormat: Text.PlainText; elide: Text.ElideRight
            color: root.theme.foreground; font.pixelSize: root.theme.dp(14); verticalAlignment: Text.AlignVCenter
        }
        delegate: C.ItemDelegate {
            id: option
            required property int index
            width: ListView.view.width; implicitHeight: root.theme.dp(36)
            highlighted: choice.highlightedIndex === index
            contentItem: Text {
                text: String(root.choices[option.index]); textFormat: Text.PlainText; elide: Text.ElideRight
                color: root.theme.foreground; font.pixelSize: root.theme.dp(14); verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle { color: option.highlighted ? root.theme.selected : option.hovered ? root.theme.hover : root.theme.panel }
        }
    }
    C.TextField {
        id: ordinaryInput
        objectName: "addonInput_" + root.fieldName
        visible: !root.secret && !root.choices.length && root.definition.type !== "boolean"
            && !["file", "folder", "account"].includes(root.control) && !(root.definition.type === "array" && root.definition.items.enum)
        Layout.fillWidth: true
        implicitHeight: root.theme.dp(36)
        text: root.currentValue === undefined ? "" : Array.isArray(root.currentValue) ? root.currentValue.join(", ") : String(root.currentValue)
        maximumLength: root.definition.type === "string" ? Math.min(root.definition.maxLength === undefined ? 4096 : root.definition.maxLength, 4096) : 16384
        color: root.theme.foreground; selectionColor: root.theme.accent; selectedTextColor: root.theme.field
        font.pixelSize: root.theme.dp(14); selectByMouse: true
        Accessible.name: root.label
        onTextEdited: {
            if (root.definition.type === "array") root.valueEdited(text.trim() ? text.split(",").map(entry => entry.trim()) : [])
            else if (["integer", "number"].includes(root.definition.type)) root.valueEdited(text.trim() && Number.isFinite(Number(text)) ? Number(text) : text)
            else root.valueEdited(text)
        }
        background: Rectangle { radius: root.theme.dp(6); color: root.theme.field; border.width: 1; border.color: ordinaryInput.activeFocus ? root.theme.accent : root.theme.border }
    }
    ColumnLayout {
        visible: !root.secret && root.definition.type === "array" && !!root.definition.items.enum
        Layout.fillWidth: true; spacing: 0
        Repeater {
            model: root.definition.type === "array" ? root.definition.items.enum || [] : []
            RowLayout {
                id: choiceRow
                required property string modelData
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: choiceRow.modelData; textFormat: Text.PlainText; wrapMode: Text.Wrap
                    color: root.theme.foreground; font.pixelSize: root.theme.dp(14)
                }
                RedesignSwitchRow {
                    accessibleName: root.label + ": " + choiceRow.modelData
                    checked: Array.isArray(root.currentValue) && root.currentValue.includes(choiceRow.modelData)
                    scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                    onToggled: checked => root.valueEdited(checked ? (root.currentValue || []).concat([choiceRow.modelData]) : (root.currentValue || []).filter(value => value !== choiceRow.modelData))
                }
            }
        }
    }
    RedesignButton {
        visible: !root.secret && ["file", "folder", "account"].includes(root.control)
        Layout.fillWidth: true
        caption: root.control === "account" ? qsTr("Connect account…") : qsTr("Select %1…").arg(root.control === "folder" ? qsTr("folder") : qsTr("file"))
        text: root.label + ": " + caption; tooltipTextFormat: Text.PlainText
        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
        onClicked: root.control === "account" ? root.connectionRequested(root.fieldName) : root.pickerRequested(root.fieldName, root.control)
    }
    ColumnLayout {
        visible: root.secret
        Layout.fillWidth: true; spacing: root.theme.dp(6)
        Text {
            objectName: "addonSecretStatus_" + root.fieldName
            Layout.fillWidth: true; textFormat: Text.PlainText; wrapMode: Text.Wrap
            text: root.secretOperation === "delete" ? qsTr("When you select Save, the app removes this credential.")
                : root.secretStatus.present ? root.secretStatus.sessionOnly ? qsTr("The credential is available only for this session.") : qsTr("The app has a saved credential.")
                : qsTr("The app does not have a saved credential.")
            color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
        }
        C.TextField {
            id: secretInput
            objectName: "addonSecretInput_" + root.fieldName
            visible: root.secretOperation === "replace"
            Layout.fillWidth: true; implicitHeight: root.theme.dp(36)
            echoMode: TextInput.Password
            inputMethodHints: Qt.ImhHiddenText | Qt.ImhNoPredictiveText | Qt.ImhSensitiveData
            maximumLength: Math.min(root.definition.maxLength === undefined ? 4096 : root.definition.maxLength, 4096)
            color: root.theme.foreground; font.pixelSize: root.theme.dp(14)
            Accessible.name: qsTr("Replacement for %1").arg(root.label)
            onTextEdited: root.secretEdited("replace", text, root.sessionOnly)
            background: Rectangle { radius: root.theme.dp(6); color: root.theme.field; border.width: 1; border.color: secretInput.activeFocus ? root.theme.accent : root.theme.border }
        }
        Text {
            Layout.fillWidth: true
            visible: !root.credentialPersistenceAvailable && root.secretOperation === "replace"
            text: qsTr("The secure credential store is not available. You can keep the replacement only for this session.")
            textFormat: Text.PlainText; wrapMode: Text.Wrap
            color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
        }
        RedesignSwitchRow {
            objectName: "addonSecretSessionOnly_" + root.fieldName
            Layout.fillWidth: true; visible: root.secretOperation === "replace"
            label: qsTr("Keep the credential only for this session")
            checked: root.sessionOnly
            scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
            onToggled: checked => root.secretEdited("replace", secretInput.text, checked)
        }
        Flow {
            Layout.fillWidth: true; spacing: root.theme.dp(6)
            RedesignButton {
                objectName: "addonSecretReplace_" + root.fieldName
                caption: root.secretStatus.present ? qsTr("Replace") : qsTr("Enter a credential"); text: caption
                enabled: root.secretOperation !== "replace"
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: { root.secretEdited("replace", "", false); secretInput.forceActiveFocus() }
            }
            RedesignButton {
                objectName: "addonSecretKeep_" + root.fieldName
                visible: root.secretOperation !== "keep"
                caption: root.secretStatus.present ? qsTr("Keep the saved credential") : qsTr("Cancel credential change"); text: caption
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: { secretInput.clear(); root.secretEdited("keep", "", false) }
            }
            RedesignButton {
                objectName: "addonSecretDelete_" + root.fieldName
                visible: root.secretStatus.present === true && root.secretOperation !== "delete"
                caption: qsTr("Remove"); text: caption
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: { secretInput.clear(); root.secretEdited("delete", "", false) }
            }
        }
    }
    Text {
        Layout.fillWidth: true; visible: root.help.length > 0
        text: root.help; textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: root.theme.foreground; font.pixelSize: root.theme.dp(12)
    }
    Text {
        objectName: "addonFieldError_" + root.fieldName
        Layout.fillWidth: true; visible: text.length > 0
        text: root.validationError; textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: root.theme.danger; font.pixelSize: root.theme.dp(12)
        Accessible.role: Accessible.AlertMessage
    }
}

// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import "AddonUi.js" as Contract

ColumnLayout {
    id: root
    required property var backend
    property string packageId: ""
    property var messages: ({})
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme }
    readonly property bool dirty: JSON.stringify(draftValues) !== JSON.stringify(initialValues) || Object.keys(secretOperations).length > 0
    readonly property bool credentialPersistenceAvailable: !backend || backend.credentialPersistenceAvailable === undefined || backend.credentialPersistenceAvailable === true
    readonly property bool validSchema: schemaError.length === 0
    property string errorMessage: ""
    property bool conflict: false
    property var fieldErrors: ({})
    property var schema: ({type: "object", properties: {}, additionalProperties: false})
    property var draftValues: ({})
    property var secretStatus: ({})
    // Protected form state: never forward these operations through addon view state.
    property var secretOperations: ({})
    property var initialValues: ({})
    property var initialBackendValues: ({})
    property string schemaError: ""
    property int resetGeneration: 0
    property bool saving: false
    property bool ready: false
    signal saved()
    signal discarded()
    signal pickerRequested(string fieldName, string kind)
    signal connectionRequested(string fieldName)
    spacing: theme.dp(12)

    function reload() {
        secretOperations = ({}); fieldErrors = ({}); errorMessage = ""; conflict = false
        resetGeneration++
        if (!backend || !packageId.length) {
            schema = {type: "object", properties: {}, additionalProperties: false}
            schemaError = ""; draftValues = ({}); initialValues = ({}); initialBackendValues = ({}); secretStatus = ({})
            return
        }
        const nextSchema = backend.settingsSchema(packageId)
        schemaError = Contract.validateSettingsSchema(nextSchema)
        if (schemaError.length) { schema = {type: "object", properties: {}, additionalProperties: false}; draftValues = ({}); initialValues = ({}); initialBackendValues = ({}); secretStatus = ({}); return }
        schema = Contract.clone(nextSchema)
        initialBackendValues = Contract.clone(backend.settingsValues(packageId) || {})
        secretStatus = Contract.clone(backend.settingsSecretStatus(packageId) || {})
        initialValues = Contract.defaults(schema, initialBackendValues)
        draftValues = Contract.clone(initialValues)
    }
    function observeBackend() {
        if (!ready || saving || !backend || !packageId.length) return
        if (!dirty) { reload(); return }
        if (JSON.stringify(backend.settingsSchema(packageId)) !== JSON.stringify(schema)
                || JSON.stringify(backend.settingsValues(packageId) || {}) !== JSON.stringify(initialBackendValues)
                || JSON.stringify(backend.settingsSecretStatus(packageId) || {}) !== JSON.stringify(secretStatus)) {
            conflict = true
            errorMessage = qsTr("The saved settings changed. Select Cancel to load the current settings.")
        }
    }
    function setValue(name, value) {
        if (!Contract.own(schema.properties, name) || Contract.fieldSecret(schema.properties[name])) return
        draftValues = Object.assign({}, draftValues, {[name]: value})
        fieldErrors = Object.assign({}, fieldErrors, {[name]: ""})
    }
    function editSecret(name, operation, value, sessionOnly) {
        if (!Contract.own(schema.properties, name) || !Contract.fieldSecret(schema.properties[name]) || !["keep", "replace", "delete"].includes(operation)) return
        const next = Object.assign({}, secretOperations)
        if (operation === "keep") delete next[name]
        else next[name] = operation === "replace" ? {operation: operation, value: value, sessionOnly: sessionOnly === true} : {operation: operation}
        secretOperations = next
        fieldErrors = Object.assign({}, fieldErrors, {[name]: ""})
    }
    function discard() { reload(); discarded() }
    function resetDefaults() {
        if (conflict) return
        draftValues = Contract.defaults(schema, {})
        secretOperations = ({}); fieldErrors = ({}); errorMessage = ""; resetGeneration++
    }
    function save() {
        observeBackend()
        if (!validSchema || conflict || !backend || !packageId.length || saving) return false
        fieldErrors = Contract.settingsErrors(schema, draftValues, secretStatus, secretOperations, credentialPersistenceAvailable)
        if (Object.keys(fieldErrors).length) { errorMessage = qsTr("Do a check of the highlighted settings."); return false }
        saving = true
        // The backend revalidates the complete object and commits credential ops atomically.
        const success = backend.saveAddonSettings(packageId, Contract.clone(draftValues), Contract.clone(secretOperations))
        saving = false
        if (!success) { errorMessage = qsTr("The app cannot save these settings. Your saved settings did not change."); return false }
        reload(); saved(); return true
    }
    onPackageIdChanged: if (ready) reload()
    onBackendChanged: if (ready) reload()
    Component.onCompleted: { ready = true; reload() }
    Component.onDestruction: secretOperations = ({})
    Connections {
        target: root.backend
        ignoreUnknownSignals: true
        function onRevisionChanged() { root.observeBackend() }
        function onChanged() { root.observeBackend() }
    }
    Repeater {
        model: root.validSchema ? Object.keys(root.schema.properties) : []
        RedesignAddonField {
            required property string modelData
            objectName: "addonField_" + modelData
            Layout.fillWidth: true
            fieldName: modelData; definition: root.schema.properties[modelData]
            currentValue: root.draftValues[modelData]
            messages: root.messages; secretStatus: root.secretStatus[modelData] || ({})
            secretOperation: root.secretOperations[modelData] ? root.secretOperations[modelData].operation : "keep"
            sessionOnly: !!root.secretOperations[modelData] && root.secretOperations[modelData].sessionOnly === true
            credentialPersistenceAvailable: root.credentialPersistenceAvailable
            resetGeneration: root.resetGeneration
            validationError: root.fieldErrors[modelData] || ""
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            visible: !definition["x-hakone-visibleWhen"] || root.draftValues[definition["x-hakone-visibleWhen"].field] === definition["x-hakone-visibleWhen"].equals
            onValueEdited: value => root.setValue(fieldName, value)
            onSecretEdited: (operation, value, sessionOnly) => root.editSecret(fieldName, operation, value, sessionOnly)
            onPickerRequested: (name, kind) => root.pickerRequested(name, kind)
            onConnectionRequested: name => root.connectionRequested(name)
        }
    }
    Text {
        objectName: "addonSettingsError"
        Layout.fillWidth: true; visible: text.length > 0
        text: root.schemaError || root.errorMessage; textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: root.theme.danger; font.pixelSize: root.theme.dp(13)
        Accessible.role: Accessible.AlertMessage
    }
    Flow {
        Layout.fillWidth: true; spacing: root.theme.dp(8)
        RedesignButton {
            objectName: "addonSettingsSave"
            caption: qsTr("Save"); text: caption; primaryAction: true
            enabled: root.validSchema && root.dirty && !root.conflict && !root.saving
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            onClicked: root.save()
        }
        RedesignButton {
            objectName: "addonSettingsCancel"
            caption: qsTr("Cancel"); text: caption; enabled: !root.saving
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            onClicked: root.discard()
        }
        RedesignButton {
            objectName: "addonSettingsDefaults"
            caption: qsTr("Use default settings"); text: caption; enabled: root.validSchema && !root.conflict && !root.saving
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            onClicked: root.resetDefaults()
        }
    }
}

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
    readonly property bool validSchema: schemaError.length === 0 && (!displayMode || loaded)
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
    // Fixed for this backend. Losing an attached view never enables legacy I/O.
    property bool displayMode: false
    property bool loaded: false
    property bool retired: false
    property bool preparing: false
    property string settingsTicket: ""
    property int ticketGeneration: 0
    property var ticketOwner: null
    property string ticketPackage: ""
    property string capturedStamp: ""
    property var preparedMessages: ({})
    // Display names of picked files and folders, by handle.
    property var handleNames: ({})
    property var sessionValues: ({})
    property Component videoBackdropContent
    readonly property bool sessionSupported: !!backend && typeof backend.setSessionSettings === "function"
    readonly property bool dataReady: {
        if (!displayMode) return true
        if (!backend || !settingsTicket.length || retired) return false
        backend.catalogueRevision; backend.catalogueReady
        return backend.catalogueReady === true && typeof backend.displayRequestCurrent === "function"
            && backend.displayRequestCurrent(settingsTicket) && typeof backend.displaySettingsState === "function"
            && backend.displaySettingsState(packageId) === "ready"
    }
    readonly property string displayError: displayMode && backend ? backend.catalogueError || "" : ""
    signal saved()
    signal discarded()
    signal pickerRequested(string fieldName, string kind)
    signal connectionRequested(string fieldName)
    spacing: theme.dp(12)

    function setBackendMode() {
        displayMode = !!backend && typeof backend.displayCatalogueAttached === "function" && backend.displayCatalogueAttached()
    }
    function releaseTicket() {
        ++ticketGeneration
        const owner = ticketOwner, ticket = settingsTicket
        settingsTicket = ""; ticketOwner = null; ticketPackage = ""; preparing = false
        try { if (owner && ticket.length && typeof owner.releaseDisplayRequest === "function") owner.releaseDisplayRequest(ticket) } catch (error) {}
    }
    function readHandles() {
        handleNames = backend && typeof backend.settingsHandleNames === "function" ? Contract.plain(backend.settingsHandleNames(packageId), {}) : ({})
        sessionValues = sessionSupported ? Contract.plain(backend.sessionSettings(packageId), {}) : ({})
    }
    function pickPath(name) {
        if (!backend || typeof backend.pickSettingsPath !== "function" || !backend.pickSettingsPath(packageId, name))
            errorMessage = qsTr("The app cannot open a file selection here.")
    }
    function connectAccount(name) {
        if (dirty) { errorMessage = qsTr("Save or cancel your changes before you connect an account."); return }
        let setup = null
        try { setup = Qt.createQmlObject("import VLC.Dialogs\nAddonAccountSetup {}", root, "addonAccountSetup") } catch (error) { setup = null }
        if (!setup) { errorMessage = qsTr("The app cannot connect accounts here."); return }
        setup.manager = backend; setup.packageId = packageId; setup.field = name
        const field = schema.properties[name] || {}
        accountDialog.setup = setup
        accountDialog.accountLabel = String(Contract.own(messages, field["x-hakone-label"]) ? messages[field["x-hakone-label"]] : field["x-hakone-label"] || field.title || name)
        accountDialog.open()
    }
    function applyForSession() {
        if (!sessionSupported || conflict || saving) return false
        const changed = {}
        for (const name of Object.keys(schema.properties)) {
            const field = schema.properties[name]
            if (Contract.fieldSecret(field) || ["file", "folder"].includes(field["x-hakone-control"])) continue
            if (JSON.stringify(draftValues[name]) !== JSON.stringify(initialValues[name])) changed[name] = draftValues[name]
        }
        fieldErrors = Contract.settingsErrors(schema, draftValues, secretStatus, {}, credentialPersistenceAvailable)
        for (const name of Object.keys(fieldErrors)) if (!Contract.own(changed, name)) delete fieldErrors[name]
        if (Object.keys(fieldErrors).length) { errorMessage = qsTr("Do a check of the highlighted settings."); return false }
        if (!backend.setSessionSettings(packageId, Object.assign({}, sessionValues, changed))) {
            errorMessage = qsTr("The app cannot use these settings for this session."); return false
        }
        sessionValues = Contract.plain(backend.sessionSettings(packageId), {})
        draftValues = Contract.clone(initialValues); secretOperations = ({}); errorMessage = ""
        return true
    }
    function stopSessionValues() {
        if (sessionSupported && backend.setSessionSettings(packageId, {})) sessionValues = ({})
    }
    function clearDraft() {
        secretOperations = ({}); fieldErrors = ({}); draftValues = ({}); initialValues = ({}); initialBackendValues = ({}); secretStatus = ({})
        schema = {type: "object", properties: {}, additionalProperties: false}; schemaError = ""; preparedMessages = ({}); resetGeneration++
    }
    function reload() {
        if (!displayMode) { loadLegacy(); return }
        clearDraft(); loaded = false; retired = false; conflict = false; errorMessage = ""
        capturedStamp = backend && typeof backend.displaySettingsStamp === "function" ? backend.displaySettingsStamp(packageId) : ""
        if (!backend || !packageId.length) { releaseTicket(); return }
        if (ticketOwner !== backend || ticketPackage !== packageId || settingsTicket.length && backend.catalogueReady === true
                && !backend.displayRequestCurrent(settingsTicket)) releaseTicket()
        if (!settingsTicket.length && typeof backend.prepareDisplaySettings === "function") {
            const owner = backend, id = packageId, generation = ++ticketGeneration
            preparing = true; ticketOwner = owner; ticketPackage = id
            const ticket = owner.prepareDisplaySettings(id)
            if (generation !== ticketGeneration || owner !== backend || id !== packageId || retired) {
                // A synchronous close/rebind owns the newer form state.
                try { if (ticket.length && typeof owner.releaseDisplayRequest === "function") owner.releaseDisplayRequest(ticket) } catch (error) {}
                return
            }
            settingsTicket = ticket; preparing = false
        }
        if (!settingsTicket.length) { errorMessage = qsTr("The app cannot prepare these settings. Close another settings window and try again."); return }
        observeBackend()
    }
    function observeBackend() {
        if (!displayMode) { observeLegacy(); return }
        if (!ready || saving || preparing || retired || !backend || !settingsTicket.length) return
        if (backend.catalogueReady !== true) return
        if (typeof backend.displayRequestCurrent !== "function" || !backend.displayRequestCurrent(settingsTicket)) {
            retired = true; conflict = true; releaseTicket()
            errorMessage = qsTr("The settings view stopped. Select Cancel and open the settings again."); return
        }
        const stamp = typeof backend.displaySettingsStamp === "function" ? backend.displaySettingsStamp(packageId) : ""
        if (!stamp.length || capturedStamp.length && stamp !== capturedStamp) {
            conflict = true; errorMessage = qsTr("The package or saved settings changed. Select Cancel and open the settings again."); return
        }
        if (!capturedStamp.length) capturedStamp = stamp
        const state = backend.displaySettingsState(packageId)
        if (state !== "ready") {
            if (state === "unavailable") schemaError = qsTr("The app cannot load these settings. Select Cancel and try again.")
            return
        }
        if (loaded) return // Preserve private edits through unrelated refreshes.
        const nextSchema = Contract.plain(backend.displaySettingsSchema(packageId), {})
        schemaError = Contract.validateSettingsSchema(nextSchema)
        if (schemaError.length) return
        schema = Contract.clone(nextSchema)
        preparedMessages = Contract.clone(backend.displaySettingsMessages(packageId) || {})
        initialBackendValues = Contract.clone(backend.displaySettingsValues(packageId) || {})
        secretStatus = Contract.clone(backend.displaySettingsSecretStatus(packageId) || {})
        initialValues = Contract.defaults(schema, initialBackendValues); draftValues = Contract.clone(initialValues); loaded = true
        readHandles()
    }
    function currentForSave() {
        return !displayMode || !!backend && settingsTicket.length > 0 && !retired
            && backend.catalogueReady === true && backend.displayRequestCurrent(settingsTicket)
            && backend.displaySettingsState(packageId) === "ready"
            && backend.displaySettingsStamp(packageId) === capturedStamp
    }
    function retryPreparation() {
        if (!displayMode || !backend || saving || retired) return
        if (!settingsTicket.length) { reload(); return }
        if (typeof backend.refreshDisplayCatalogue === "function") backend.refreshDisplayCatalogue()
    }
    function retirePackage(id) {
        if (!displayMode || saving || id !== packageId) return
        retired = true; conflict = true; releaseTicket()
        errorMessage = qsTr("The add-on stopped. Select Cancel and open the settings again.")
    }
    function loadLegacy() {
        secretOperations = ({}); fieldErrors = ({}); errorMessage = ""; conflict = false
        resetGeneration++
        if (!backend || !packageId.length) {
            schema = {type: "object", properties: {}, additionalProperties: false}
            schemaError = ""; draftValues = ({}); initialValues = ({}); initialBackendValues = ({}); secretStatus = ({})
            return
        }
        const nextSchema = Contract.plain(backend.settingsSchema(packageId), {})
        schemaError = Contract.validateSettingsSchema(nextSchema)
        if (schemaError.length) { schema = {type: "object", properties: {}, additionalProperties: false}; draftValues = ({}); initialValues = ({}); initialBackendValues = ({}); secretStatus = ({}); return }
        schema = Contract.clone(nextSchema)
        initialBackendValues = Contract.clone(backend.settingsValues(packageId) || {})
        secretStatus = Contract.clone(backend.settingsSecretStatus(packageId) || {})
        initialValues = Contract.defaults(schema, initialBackendValues)
        draftValues = Contract.clone(initialValues)
        readHandles()
    }
    function observeLegacy() {
        if (!ready || saving || !backend || !packageId.length) return
        if (!dirty) { loadLegacy(); return }
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
    function discard() {
        if (displayMode) { releaseTicket(); clearDraft(); loaded = false; capturedStamp = "" }
        else reload()
        discarded()
    }
    function resetDefaults() {
        if (conflict || displayMode && !dataReady) return
        draftValues = Contract.defaults(schema, {})
        secretOperations = ({}); fieldErrors = ({}); errorMessage = ""; resetGeneration++
    }
    function save() {
        observeBackend()
        if (!validSchema || conflict || !backend || !packageId.length || saving || !currentForSave()) return false
        fieldErrors = Contract.settingsErrors(schema, draftValues, secretStatus, secretOperations, credentialPersistenceAvailable)
        if (Object.keys(fieldErrors).length) { errorMessage = qsTr("Do a check of the highlighted settings."); return false }
        saving = true
        // The backend revalidates the complete object and commits credential ops atomically.
        const success = displayMode
            ? backend.saveDisplaySettings(packageId, capturedStamp, Contract.clone(draftValues), Contract.clone(secretOperations))
            : backend.saveAddonSettings(packageId, Contract.clone(draftValues), Contract.clone(secretOperations))
        saving = false
        if (!success) { errorMessage = qsTr("The app cannot save these settings. Your saved settings did not change."); return false }
        if (displayMode) releaseTicket(); else reload()
        saved(); return true
    }
    onPackageIdChanged: if (ready) reload()
    onBackendChanged: if (ready) { releaseTicket(); setBackendMode(); reload() }
    Component.onCompleted: { setBackendMode(); ready = true; reload() }
    Component.onDestruction: { releaseTicket(); secretOperations = ({}) }
    Connections {
        target: root.backend
        ignoreUnknownSignals: true
        function onRevisionChanged() { root.observeBackend() }
        function onChanged() { root.observeBackend() }
        function onCatalogueChanged() { root.observeBackend() }
        function onCatalogueFreshnessChanged() { root.observeBackend() }
        function onPackageWithdrawn(id) { root.retirePackage(id) }
        function onRetiring() { root.retirePackage(root.packageId) }
        function onSettingsPathPicked(id, field, handle, name) {
            if (id !== root.packageId || !Contract.own(root.schema.properties, field)) return
            root.handleNames = Object.assign({}, root.handleNames, {[handle]: name})
            root.setValue(field, handle)
        }
    }
    RedesignAccountSetupDialog {
        id: accountDialog
        parent: root.Window.contentItem || root
        credentialPersistenceAvailable: root.credentialPersistenceAvailable
        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
        videoBackdropContent: root.videoBackdropContent
        onConnected: root.reload()
        onClosed: { if (setup) setup.destroy(); setup = null }
    }
    Text {
        objectName: "addonSettingsLoading"
        Layout.fillWidth: true
        visible: root.displayMode && !root.loaded && !root.conflict && root.settingsTicket.length > 0
        text: root.displayError || qsTr("The app prepares these settings.")
        textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
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
            handleName: typeof currentValue === "string" && Contract.own(root.handleNames, currentValue) ? String(root.handleNames[currentValue]) : ""
            onPickerRequested: (name, kind) => { root.pickPath(name); root.pickerRequested(name, kind) }
            onConnectionRequested: name => { root.connectAccount(name); root.connectionRequested(name) }
        }
    }
    Text {
        objectName: "addonSettingsSessionNote"
        Layout.fillWidth: true
        visible: root.sessionSupported && Object.keys(root.sessionValues).length > 0
        text: qsTr("Temporary settings are in use until the app closes. They are not saved.")
        textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
    }
    Text {
        objectName: "addonSettingsError"
        Layout.fillWidth: true; visible: text.length > 0
        text: root.schemaError || root.errorMessage || root.displayError; textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: root.theme.danger; font.pixelSize: root.theme.dp(13)
        Accessible.role: Accessible.AlertMessage
    }
    Flow {
        Layout.fillWidth: true; spacing: root.theme.dp(8)
        RedesignButton {
            objectName: "addonSettingsSave"
            caption: qsTr("Save"); text: caption; primaryAction: true
            enabled: root.validSchema && root.dirty && !root.conflict && !root.saving && (!root.displayMode || root.dataReady)
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            onClicked: root.save()
        }
        RedesignButton {
            objectName: "addonSettingsSession"
            visible: root.sessionSupported
            caption: qsTr("Use until the app closes"); text: caption
            enabled: root.validSchema && root.dirty && !root.conflict && !root.saving && Object.keys(root.secretOperations).length === 0
                && (!root.displayMode || root.dataReady)
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            onClicked: root.applyForSession()
        }
        RedesignButton {
            objectName: "addonSettingsStopSession"
            visible: root.sessionSupported && Object.keys(root.sessionValues).length > 0
            caption: qsTr("Stop temporary settings"); text: caption; enabled: !root.saving
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            onClicked: root.stopSessionValues()
        }
        RedesignButton {
            objectName: "addonSettingsCancel"
            caption: qsTr("Cancel"); text: caption; enabled: !root.saving
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            onClicked: root.discard()
        }
        RedesignButton {
            objectName: "addonSettingsRetry"
            caption: qsTr("Try again"); text: caption
            visible: root.displayMode && !root.dataReady && !root.conflict && root.displayError.length > 0
            enabled: !root.saving && root.backend && root.backend.catalogueLoading !== true
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            onClicked: root.retryPreparation()
        }
        RedesignButton {
            objectName: "addonSettingsDefaults"
            caption: qsTr("Use default settings"); text: caption; enabled: root.validSchema && !root.conflict && !root.saving && (!root.displayMode || root.dataReady)
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            onClicked: root.resetDefaults()
        }
    }
}

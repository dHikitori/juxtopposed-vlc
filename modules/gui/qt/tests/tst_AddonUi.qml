// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"
import "../player/qml/redesign/AddonUi.js" as Contract

TestCase {
    id: testCase
    name: "AddonUi"
    width: 900; height: 1000; visible: true; when: windowShown
    QtObject {
        id: backend
        property int revision: 0
        property var schema: ({type: "object", additionalProperties: false, properties: {}})
        property var values: ({})
        property var secrets: ({})
        property bool acceptSave: true
        property bool credentialPersistenceAvailable: true
        property int saveCount: 0
        property var lastValues: ({})
        property var lastOperations: ({})
        function settingsSchema(id) { return schema }
        function settingsValues(id) { return values }
        function settingsSecretStatus(id) { return secrets }
        function saveAddonSettings(id, nextValues, operations) {
            saveCount++; lastValues = nextValues; lastOperations = operations
            if (!acceptSave) return false
            values = Contract.clone(nextValues)
            for (const field of Object.keys(operations)) {
                if (operations[field].operation === "replace") secrets = Object.assign({}, secrets, {[field]: {present: true}})
                else if (operations[field].operation === "delete") secrets = Object.assign({}, secrets, {[field]: {present: false}})
            }
            revision++
            return true
        }
    }
    RedesignAddonSettings { id: settings; width: 450; backend: backend; packageId: "org.example.radio" }
    RedesignAddonView { id: view; x: 460; width: 420 }
    RedesignAddonProviders { id: providers; y: 650; width: 450; feature: "subtitles" }
    SignalSpy { id: commandSpy; target: view; signalName: "commandRequested" }
    SignalSpy { id: enabledSpy; target: providers; signalName: "enabledRequested" }
    SignalSpy { id: moveSpy; target: providers; signalName: "moveRequested" }
    SignalSpy { id: configureSpy; target: providers; signalName: "configureRequested" }
    function field(name) { return findChild(settings, name) }
    function viewDocument(root) { return {contract: "ui@1", id: "example", root: root} }
    function init() {
        backend.schema = {type: "object", additionalProperties: false,
            properties: {enabled: {type: "boolean", default: true, title: "Enabled"},
                count: {type: "integer", minimum: 1, maximum: 9, default: 3},
                apiKey: {type: "string", minLength: 1, "x-vlc-secret": true},
                optionalKey: {type: "string", "x-vlc-secret": true}}, required: ["apiKey"]}
        backend.values = {enabled: true, count: 3}
        backend.secrets = {apiKey: {present: true}, optionalKey: {present: true, sessionOnly: true}}
        backend.saveCount = 0; backend.acceptSave = true; backend.credentialPersistenceAvailable = true; backend.lastOperations = ({}); backend.lastValues = ({})
        settings.width = 450; settings.lightTheme = false; settings.reload()
        view.document = null; view.state = ({}); view.messages = ({}); view.assetUrls = ({})
        view.registeredCommands = ["org.example.radio/play"]
        view.lightTheme = false
        providers.providers = []
        commandSpy.clear(); enabledSpy.clear(); moveSpy.clear(); configureSpy.clear()
        wait(20)
    }
    function test_draftsAndCancelNeverSave() {
        verify(settings.validSchema); verify(!settings.dirty)
        settings.setValue("count", 5)
        verify(settings.dirty); compare(backend.values.count, 3)
        settings.editSecret("apiKey", "replace", "unsaved-private-key")
        settings.discard()
        compare(settings.draftValues.count, 3); compare(Object.keys(settings.secretOperations).length, 0)
        compare(backend.saveCount, 0); verify(!settings.dirty)
    }
    function test_requiredExistingSecretIsKeptWithoutReentry() {
        settings.setValue("count", 4)
        verify(settings.save())
        compare(backend.lastValues.count, 4)
        verify(!Contract.own(backend.lastValues, "apiKey"))
        compare(Object.keys(backend.lastOperations).length, 0)
        compare(backend.saveCount, 1); verify(!settings.dirty)
    }
    function test_requiredSecretDeletionRevokesAndRefreshesStatus() {
        settings.editSecret("apiKey", "delete", "")
        verify(settings.save()); compare(backend.saveCount, 1)
        compare(backend.lastOperations.apiKey.operation, "delete")
        verify(!backend.secrets.apiKey.present)
        compare(field("addonSecretStatus_apiKey").text, "The app does not have a saved credential.")
        compare(Object.keys(settings.secretOperations).length, 0)
        settings.setValue("count", 4)
        verify(!settings.save()); verify(settings.fieldErrors.apiKey.length > 0)
    }
    function test_cancelSecretDeletionKeepsTheSavedKey() {
        settings.editSecret("apiKey", "delete", "")
        settings.discard()
        verify(backend.secrets.apiKey.present); compare(backend.saveCount, 0)
        compare(Object.keys(settings.secretOperations).length, 0)
    }
    function test_missingRequiredSecretKeepFailsAndOptionalDeletionSucceeds() {
        backend.secrets = {apiKey: {present: false}, optionalKey: {present: true}}
        backend.revision++; settings.reload()
        settings.setValue("count", 4)
        verify(!settings.save()); compare(backend.saveCount, 0)
        verify(settings.fieldErrors.apiKey.length > 0)
        backend.secrets = {apiKey: {present: true}, optionalKey: {present: true}}
        backend.revision++; settings.reload()
        settings.editSecret("optionalKey", "delete", "")
        verify(settings.save()); compare(backend.lastOperations.optionalKey.operation, "delete")
    }
    function test_replacementSecretUsesSeparateProtectedOperation() {
        mouseClick(field("addonSecretReplace_apiKey")); wait(10)
        const input = field("addonSecretInput_apiKey")
        verify(input.visible); compare(input.echoMode, TextInput.Password)
        input.forceActiveFocus(); keyClick(Qt.Key_X); keyClick(Qt.Key_Y)
        compare(settings.secretOperations.apiKey.value, "xy")
        verify(settings.save())
        compare(backend.lastOperations.apiKey.operation, "replace")
        compare(backend.lastOperations.apiKey.value, "xy")
        verify(!Contract.own(backend.lastValues, "apiKey"))
        compare(input.text, ""); compare(Object.keys(settings.secretOperations).length, 0)
    }
    function test_sessionOnlyCredentialsRequireExplicitChoice() {
        backend.credentialPersistenceAvailable = false
        settings.editSecret("apiKey", "replace", "session-key", false)
        verify(!settings.save()); compare(backend.saveCount, 0)
        verify(settings.fieldErrors.apiKey.length > 0)
        settings.editSecret("apiKey", "replace", "session-key", true)
        verify(settings.save())
        compare(backend.lastOperations.apiKey.sessionOnly, true)
        verify(!Contract.own(backend.lastValues, "apiKey"))
    }
    function test_sessionOnlyChoiceIsAvailableWithSecureStorage() {
        mouseClick(field("addonSecretReplace_apiKey")); wait(10)
        const input = field("addonSecretInput_apiKey")
        input.forceActiveFocus(); keyClick(Qt.Key_X)
        const choice = findChild(field("addonSecretSessionOnly_apiKey"), "settingSwitch")
        verify(choice.visible)
        mouseClick(choice)
        compare(settings.secretOperations.apiKey.sessionOnly, true)
        verify(settings.save())
        compare(backend.lastOperations.apiKey.sessionOnly, true)
    }
    function test_secretStatusExplainsSessionOnly() {
        verify(field("addonSecretStatus_optionalKey").text.includes("session"))
        settings.editSecret("apiKey", "replace", "")
        verify(!settings.save()); verify(!settings.fieldErrors.apiKey.includes("private"))
    }
    function test_invalidDataAndConcurrentChangesCannotOverwriteSavedValues() {
        settings.setValue("count", 100)
        verify(!settings.save()); compare(backend.saveCount, 0)
        settings.setValue("count", 4)
        backend.values = {enabled: false, count: 6}; backend.revision++
        verify(settings.conflict); verify(!settings.save()); compare(backend.saveCount, 0)
        settings.discard(); compare(settings.draftValues.count, 6); verify(!settings.conflict)
    }
    function test_saveFailureKeepsDraftAndCoreState() {
        backend.acceptSave = false
        settings.setValue("count", 5)
        verify(!settings.save()); compare(backend.values.count, 3)
        compare(settings.draftValues.count, 5); verify(settings.dirty)
        verify(settings.errorMessage.length > 0)
    }
    function test_unknownSchemaAndSecretDefaultsAreRejected() {
        const malicious = Contract.clone(backend.schema)
        malicious.properties.apiKey.default = "must-never-store"
        verify(Contract.validateSettingsSchema(malicious).length > 0)
        delete malicious.properties.apiKey.default
        malicious.properties.count.executableValidator = "anything"
        backend.schema = malicious; settings.reload()
        verify(!settings.validSchema); verify(!field("addonSettingsSave").enabled)
    }
    function test_rendererDispatchesOnlyRegisteredTypedCommands() {
        view.document = viewDocument({type: "Column", children: [
            {type: "Text", id: "label", text: {bind: "state.title"}},
            {type: "Button", id: "play", text: "Play", action: {command: "org.example.radio/play", arguments: {item: {bind: "state.ref"}}}}
        ]})
        view.state = {title: "<b>Untrusted title</b>", ref: "station-1"}; wait(50)
        verify(view.valid, view.errorMessage)
        const label = findChild(view, "addonNode_label")
        compare(label.children[0].item.textFormat, Text.PlainText)
        const node = findChild(view, "addonNode_play")
        mouseClick(node.children[0].item)
        compare(commandSpy.count, 1); compare(commandSpy.signalArguments[0][0], "org.example.radio/play")
        compare(commandSpy.signalArguments[0][1].item, "station-1")
    }
    function test_untrustedButtonAndEnumLabelsUsePlainText() {
        const markup = "<img src='https://example.org/track'>"
        view.document = viewDocument({type: "Button", text: markup, action: {command: "org.example.radio/play"}})
        wait(15)
        verify(view.valid)
        const action = findChild(view, "addonActionLabel")
        verify(action !== null); compare(action.textFormat, Text.PlainText); compare(action.text, markup)
        backend.schema = {type: "object", additionalProperties: false, properties: {choice: {type: "string", enum: [markup, "Safe"]}}}
        backend.values = {choice: markup}; settings.reload(); wait(20)
        const choice = field("addonChoice_choice")
        verify(choice !== null); compare(choice.contentItem.textFormat, Text.PlainText)
        compare(choice.contentItem.text, markup)
        choice.popup.open(); wait(10)
        choice.popup.close()
    }
    function test_rendererRejectsExecutableTypesBindingsAndCommands() {
        const invalid = [
            {type: "Loader", source: "file:///tmp/untrusted.qml"},
            {type: "Text", text: {bind: "state.constructor.name"}},
            {type: "Text", text: {bind: "Qt.openUrlExternally('https://example.org')"}},
            {type: "Button", text: "Bad", action: {command: "org.other/add"}},
            {type: "Text", text: "Bad", sourceComponent: "anything"}
        ]
        for (const root of invalid) {
            view.document = viewDocument(root); wait(5)
            verify(!view.valid); verify(view.errorMessage.length > 0)
        }
        compare(commandSpy.count, 0)
    }
    function test_rendererRejectsDuplicateKeysAndKeepsLastValidState() {
        view.document = viewDocument({type: "List", id: "stations", items: {bind: "state.stations"}, keyField: "ref",
            itemTemplate: {type: "MediaCard", title: {bind: "item.title"}, primaryAction: {command: "org.example.radio/play", arguments: {item: {bind: "item.ref"}}}}})
        view.state = {stations: [{ref: "one", title: "One"}]}; wait(50)
        verify(view.valid, view.errorMessage)
        view.state = {stations: [{ref: "same", title: "One"}, {ref: "same", title: "Two"}]}; wait(50)
        verify(!view.valid); compare(view.acceptedState.stations.length, 1)
        compare(view.acceptedState.stations[0].ref, "one")
    }
    function test_rendererImagesRequireHostAssetHandles() {
        view.assetUrls = {approved: "image://addonassets/cover", remote: "https://example.org/image.png"}
        compare(view.assetFor("https://example.org/image.png"), "")
        compare(view.assetFor("file:///etc/passwd"), "")
        compare(view.assetFor("remote"), "")
        compare(view.assetFor("approved"), "image://addonassets/cover")
    }
    function test_viewBudgetsAndLiveTheme() {
        let node = {type: "Text", text: "Hello"}
        for (let i = 0; i < 12; ++i) node = {type: "Column", children: [node]}
        verify(Contract.validateView(viewDocument(node), []).length > 0)
        view.document = viewDocument({type: "Text", id: "hello", text: "Hello"}); wait(15)
        verify(view.valid)
        const before = view.theme.foreground.toString()
        view.lightTheme = true
        verify(view.theme.foreground.toString() !== before)
    }
    function test_providerSchemaSecretAnnotationsAndLanguageValidation() {
        const schema = {"$schema": "https://json-schema.org/draft/2020-12/schema", type: "object", additionalProperties: false,
            properties: {apiKey: {type: "string", format: "password", writeOnly: true, "x-vlc-secret": true, "x-vlc-label": "settings.key", minLength: 1},
                language: {type: "string", default: "en", minLength: 2, maxLength: 16, pattern: "^[A-Za-z]{2,3}(-[A-Za-z0-9]{2,8})*$"}}, required: ["apiKey"]}
        compare(Contract.validateSettingsSchema(schema), "")
        verify(Contract.fieldSecret(schema.properties.apiKey))
        compare(Contract.fieldError(schema.properties.language, "en-AU", false), "")
        verify(Contract.fieldError(schema.properties.language, "../bad", false).length > 0)
        backend.schema = schema; settings.messages = {"settings.key": "API credential"}; settings.reload(); wait(10)
        compare(field("addonField_apiKey").label, "API credential")
        settings.messages = ({})
    }
    function test_invalidSchemaBoundsAndFlagsCannotCreateEditors() {
        for (const mutation of [{maxLength: -1}, {maxItems: 1.5}, {"x-vlc-secret": "yes"}, {title: {bind: "state.name"}}]) {
            const schema = Contract.clone(backend.schema)
            schema.properties.count = Object.assign({}, schema.properties.count, mutation)
            verify(Contract.validateSettingsSchema(schema).length > 0)
        }
    }
    function test_missingAvailabilityBindingCannotEnableACommand() {
        view.document = viewDocument({type: "Button", id: "guarded", text: "Play", enabled: {bind: "state.available"}, action: {command: "org.example.radio/play"}})
        wait(20)
        verify(view.valid)
        const node = findChild(view, "addonViewRoot")
        verify(!node.enabled)
        view.state = {available: true}; wait(50)
        verify(node.enabled)
        view.registeredCommands = []
        verify(!view.valid)
    }
    function test_providerActionsHaveStableIdentityAndNoImplicitMutations() {
        providers.providers = [
            {id: "org.example.one/search", packageId: "org.example.one", name: "One", enabled: true, configured: true},
            {id: "org.example.two/search", packageId: "org.example.two", name: "Two", enabled: false, configured: false}]
        wait(20)
        verify(!findChild(providers, "addonProviderUp_org.example.one/search").enabled)
        mouseClick(findChild(providers, "addonProviderDown_org.example.one/search"))
        compare(moveSpy.count, 1); compare(moveSpy.signalArguments[0][0], "org.example.one/search"); compare(moveSpy.signalArguments[0][1], 1)
        mouseClick(findChild(providers, "addonProviderConfigure_org.example.two/search"))
        compare(configureSpy.signalArguments[0][0], "org.example.two")
        compare(providers.providers[0].id, "org.example.one/search")
        compare(enabledSpy.count, 0)
    }
}

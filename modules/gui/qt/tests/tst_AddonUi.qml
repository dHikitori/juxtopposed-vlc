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
        property var handles: ({})
        property var session: ({})
        property int pickCount: 0
        signal settingsPathPicked(string packageId, string field, string handle, string name)
        function pickSettingsPath(id, name) { pickCount++; return true }
        function settingsHandleNames(id) { return handles }
        function sessionSettings(id) { return session }
        function setSessionSettings(id, nextValues) {
            if (Object.keys(nextValues).some(name => !Contract.own(schema.properties, name) || Contract.fieldSecret(schema.properties[name]))) return false
            session = Contract.clone(nextValues); return true
        }
        function settingsSchema(id) { return schema }
        function settingsValues(id) { return values }
        function settingsSecretStatus(id) { return secrets }
        // Store writes answer later, as the app's do.
        signal writeFinished(string request, bool success, string error)
        property int requests: 0
        function finishLater(success) {
            const request = String(++requests)
            Qt.callLater(() => writeFinished(request, success, success ? "" : "The save failed."))
            return request
        }
        function requestSaveSettings(id, nextValues, operations) {
            saveCount++; lastValues = nextValues; lastOperations = operations
            if (!acceptSave) return finishLater(false)
            values = Contract.clone(nextValues)
            for (const field of Object.keys(operations)) {
                if (operations[field].operation === "replace") secrets = Object.assign({}, secrets, {[field]: {present: true}})
                else if (operations[field].operation === "delete") secrets = Object.assign({}, secrets, {[field]: {present: false}})
            }
            revision++
            return finishLater(true)
        }
    }
    RedesignAddonSettings { id: settings; width: 450; backend: backend; packageId: "org.example.radio" }
    RedesignAddonView { id: view; x: 460; width: 420 }
    RedesignAddonProviders { id: providers; y: 650; width: 450; feature: "subtitles" }
    Component { id: providerComponent; RedesignAddonProviders { width: 450; feature: "subtitles" } }
    Component { id: spyComponent; SignalSpy {} }
    Component {
        id: displayComponent
        QtObject {
            property bool catalogueReady: true
            property bool catalogueLoading: false
            property int catalogueRevision: 1
            property bool ownerCurrent: true
            property int modeReads: 0
            property int preparations: 0
            property int releases: 0
            property var observations: ({queries:0})
            property var tickets: []
            property var target: null
            property var replacement: null
            property bool hideDuringPrepare: false
            property var afterRelease: null
            signal catalogueChanged()
            signal catalogueFreshnessChanged()
            signal capabilitiesChanged()
            signal retiring()
            function displayCatalogueAttached() { modeReads++; return true }
            function prepareDisplayFeature(feature) {
                const id = String(++preparations); tickets = tickets.concat([id])
                if (hideDuringPrepare) target.active = false
                if (replacement) { const next = replacement; replacement = null; target.backend = next }
                return id
            }
            function releaseDisplayRequest(id) {
                releases++; tickets = tickets.filter(value => value !== id)
                const callback=afterRelease; afterRelease=null; if(callback) callback()
            }
            function displayRequestCurrent(id) { observations.queries++; return ownerCurrent && catalogueReady && tickets.includes(id) }
            function queryCount() { return observations.queries }
            function refreshDisplayCatalogue() { catalogueFreshnessChanged() }
            function retire() { tickets = []; retiring() }
        }
    }
    SignalSpy { id: commandSpy; target: view; signalName: "commandRequested" }
    SignalSpy { id: enabledSpy; target: providers; signalName: "enabledRequested" }
    SignalSpy { id: moveSpy; target: providers; signalName: "moveRequested" }
    SignalSpy { id: configureSpy; target: providers; signalName: "configureRequested" }
    function field(name) { return findChild(settings, name) }
    // The backend answers a save after it returns, as the app's store write does.
    function saveAndWait() { verify(settings.save()); tryVerify(() => !settings.saving) }
    function displayRows() {
        return [{id:"org.example.provider/search",packageId:"org.example.provider",name:"Provider",enabled:true,configured:false,configurationKnown:false}]
    }
    Component {
        id: boundProviderComponent
        RedesignAddonProviders {
            width:450; feature:"subtitles"
            providers: {
                if (!backendReady) return []
                if (displayMode) return testCase.displayRows()
                backend.observations.legacyReads=(backend.observations.legacyReads || 0)+1
                return []
            }
        }
    }
    function test_fixedModeIsPublishedBeforeParentCanUseALegacyGetter() {
        const owner=createTemporaryObject(displayComponent,testCase)
        const panel=createTemporaryObject(boundProviderComponent,testCase,{backend:owner,y:580})
        verify(!panel.backendReady); compare(owner.modeReads,0)
        tryCompare(owner,"preparations",1); verify(panel.displayMode); verify(panel.backendReady)
        compare(owner.observations.legacyReads || 0,0); compare(owner.modeReads,1)
    }
    function test_retirementBeforeQueuedModeCaptureDoesNotQueryOwner() {
        const owner=createTemporaryObject(displayComponent,testCase)
        const panel=createTemporaryObject(providerComponent,testCase,{backend:owner,providers:displayRows(),y:580})
        owner.retire(); verify(!panel.currentForAction()); wait(20)
        compare(owner.modeReads,0); compare(owner.preparations,0); compare(owner.queryCount(),0)
    }
    function test_providerDemandIsVisibleBoundedAndIgnoresCapabilityChanges() {
        const owner=createTemporaryObject(displayComponent,testCase)
        const panel=createTemporaryObject(providerComponent,testCase,{backend:owner,providers:displayRows(),y:580,actionGuard:()=>owner.ownerCurrent})
        owner.target=panel; wait(10); compare(owner.modeReads,1); compare(owner.preparations,1)
        for(let i=0;i<20;i++) { owner.capabilitiesChanged(); owner.catalogueRevision++ }
        compare(owner.preparations,1); compare(owner.modeReads,1)
        panel.visible=false; verify(!panel.currentForAction()); tryCompare(owner,"releases",1); compare(owner.tickets.length,0)
        panel.active=false; panel.visible=true; compare(owner.preparations,1)
        panel.active=true; tryCompare(owner,"preparations",2); compare(owner.tickets.length,1)
    }
    function test_providerActionsReadCurrentAuthorityEvenWithoutANotify() {
        const owner=createTemporaryObject(displayComponent,testCase)
        const panel=createTemporaryObject(providerComponent,testCase,{backend:owner,providers:displayRows(),y:580,actionGuard:()=>owner.ownerCurrent})
        const spy=createTemporaryObject(spyComponent,testCase,{target:panel,signalName:"configureRequested"})
        wait(10); owner.ownerCurrent=false
        mouseClick(findChild(panel,"addonProviderConfigure_org.example.provider/search")); compare(spy.count,0)
        owner.ownerCurrent=true
        mouseClick(findChild(panel,"addonProviderConfigure_org.example.provider/search")); compare(spy.count,1)
        compare(findChild(panel,"addonProviderStatus_org.example.provider/search").text,"Settings not checked")
    }
    function test_providerDemandBalancesInlineHideAndBackendReplacement() {
        const owner=createTemporaryObject(displayComponent,testCase)
        const panel=createTemporaryObject(providerComponent,testCase,{backend:owner,providers:[],y:580})
        owner.target=panel; owner.hideDuringPrepare=true; panel.providers=displayRows()
        tryCompare(owner,"preparations",1); compare(owner.releases,1); compare(owner.tickets.length,0)
        const next=createTemporaryObject(displayComponent,testCase)
        owner.hideDuringPrepare=false; owner.replacement=next; panel.active=true
        tryCompare(next,"tickets",["1"]); compare(owner.releases,2); compare(owner.tickets.length,0)
        panel.active=false; tryCompare(next,"releases",1); compare(next.tickets.length,0)
    }
    function test_providerRetirementStopsQueriesAndDoesNotUseLegacyFallback() {
        const owner=createTemporaryObject(displayComponent,testCase)
        const panel=createTemporaryObject(providerComponent,testCase,{backend:owner,providers:displayRows(),y:580})
        tryCompare(owner,"preparations",1); owner.retire(); const queries=owner.queryCount()
        verify(!panel.currentForAction()); panel.active=false; panel.active=true
        compare(owner.queryCount(),queries); compare(owner.preparations,1); compare(owner.releases,0)
    }
    function test_disabledAndMissingProvidersCanBeManagedWithoutPresenceDemand() {
        const owner=createTemporaryObject(displayComponent,testCase)
        const rows=[{id:"org.example.disabled/search",packageId:"org.example.disabled",name:"Disabled",enabled:false,configured:false,configurationKnown:true},
            {id:"org.example.missing/search",packageId:"org.example.missing",name:"Missing",enabled:true,configured:false,configurationKnown:true}]
        const panel=createTemporaryObject(providerComponent,testCase,{backend:owner,providers:rows,y:450})
        tryCompare(panel,"backendReady",true)
        const configured=createTemporaryObject(spyComponent,testCase,{target:panel,signalName:"configureRequested"})
        const enabled=createTemporaryObject(spyComponent,testCase,{target:panel,signalName:"enabledRequested"})
        compare(owner.preparations,0)
        mouseClick(findChild(panel,"addonProviderConfigure_org.example.missing/search")); compare(configured.count,1)
        mouseClick(findChild(findChild(panel,"addonProviderEnabled_org.example.disabled/search"),"settingSwitch")); compare(enabled.count,1)
        owner.catalogueReady=false
        mouseClick(findChild(panel,"addonProviderConfigure_org.example.missing/search")); compare(configured.count,1)
        compare(owner.preparations,0)
    }
    function test_releaseCallbackCannotReplaceANewerBackendCapture() {
        const original=createTemporaryObject(displayComponent,testCase)
        const intermediate=createTemporaryObject(displayComponent,testCase)
        const latest=createTemporaryObject(displayComponent,testCase)
        const panel=createTemporaryObject(providerComponent,testCase,{backend:original,providers:displayRows(),y:580})
        tryCompare(original,"preparations",1)
        original.afterRelease=()=>panel.backend=latest
        panel.backend=intermediate
        verify(!panel.currentForAction())
        tryCompare(latest,"preparations",1); compare(original.releases,1); compare(intermediate.preparations,0)
        verify(panel.currentForAction()); panel.active=false; tryCompare(latest,"releases",1)
    }
    function viewDocument(root) { return {contract: "ui@1", id: "example", root: root} }
    function init() {
        backend.schema = {type: "object", additionalProperties: false,
            properties: {enabled: {type: "boolean", default: true, title: "Enabled"},
                count: {type: "integer", minimum: 1, maximum: 9, default: 3},
                apiKey: {type: "string", minLength: 1, "x-hakone-secret": true},
                optionalKey: {type: "string", "x-hakone-secret": true}}, required: ["apiKey"]}
        backend.values = {enabled: true, count: 3}
        backend.secrets = {apiKey: {present: true}, optionalKey: {present: true, sessionOnly: true}}
        backend.handles = ({}); backend.session = ({}); backend.pickCount = 0
        backend.saveCount = 0; backend.acceptSave = true; backend.credentialPersistenceAvailable = true; backend.lastOperations = ({}); backend.lastValues = ({})
        settings.width = 450; settings.lightTheme = false; settings.reload()
        view.document = null; view.state = ({}); view.messages = ({}); view.assetUrls = ({})
        view.registeredCommands = ["org.example.radio/play"]
        view.lightTheme = false
        providers.providers = []
        commandSpy.clear(); enabledSpy.clear(); moveSpy.clear(); configureSpy.clear()
        wait(20)
    }
    function accountSchema() {
        backend.schema = {type: "object", additionalProperties: false, properties: {
            account: {type: "string", "x-hakone-secret": true, "x-hakone-control": "account",
                "x-hakone-account": {recipe: "oauth-device", deviceAuthorizationUrl: "https://auth.example.org/device",
                    tokenUrl: "https://auth.example.org/token", clientId: "test"}},
            folder: {type: "string", "x-hakone-control": "folder"},
            count: {type: "integer", minimum: 1, maximum: 9, default: 3}}, required: ["account"]}
        backend.values = {count: 3}
        backend.secrets = {account: {present: true, account: true}}
        backend.revision++; settings.reload()
        verify(settings.validSchema)
    }
    function test_anAccountIsConnectedOrDisconnectedButNeverTyped() {
        accountSchema()
        compare(field("addonAccountStatus_account").text, "The account is connected.")
        verify(!field("addonSecretInput_account").visible)
        verify(field("addonAccountDisconnect_account").visible)
        settings.editSecret("account", "delete", "")
        compare(field("addonAccountStatus_account").text, "When you select Save, the app disconnects this account.")
        verify(field("addonAccountKeep_account").visible)
        saveAndWait(); compare(backend.lastOperations.account.operation, "delete")
        compare(field("addonAccountStatus_account").text, "The account is not connected.")
        settings.setValue("count", 4)
        verify(!settings.save()); compare(settings.fieldErrors.account, "Connect the account.")
    }
    function test_accountConnectionWaitsForSavedEdits() {
        accountSchema()
        settings.setValue("count", 5)
        settings.connectAccount("account")
        compare(settings.errorMessage, "Save or cancel your changes before you connect an account.")
    }
    function test_aPickedFolderShowsItsNameAndSavesTheHandle() {
        accountSchema()
        compare(field("addonPicked_folder").text, "No folder selected.")
        mouseClick(field("addonPick_folder"))
        compare(backend.pickCount, 1)
        backend.settingsPathPicked("org.example.radio", "folder", "handle:abc", "Music")
        compare(settings.draftValues.folder, "handle:abc")
        compare(field("addonPicked_folder").text, "Music")
        backend.settingsPathPicked("org.example.other", "folder", "handle:other", "Other")
        compare(settings.draftValues.folder, "handle:abc")
        verify(settings.save()); compare(backend.lastValues.folder, "handle:abc")
        mouseClick(field("addonPickClear_folder"))
        compare(settings.draftValues.folder, "")
    }
    function test_temporarySettingsApplyOnlyChangedOrdinaryValues() {
        settings.setValue("count", 7)
        verify(settings.applyForSession())
        compare(backend.session.count, 7); verify(!Contract.own(backend.session, "enabled"))
        compare(backend.saveCount, 0); verify(!settings.dirty)
        verify(field("addonSettingsSessionNote").visible)
        mouseClick(field("addonSettingsStopSession"))
        compare(Object.keys(backend.session).length, 0)
        verify(!field("addonSettingsSessionNote").visible)
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
        saveAndWait()
        compare(backend.lastValues.count, 4)
        verify(!Contract.own(backend.lastValues, "apiKey"))
        compare(Object.keys(backend.lastOperations).length, 0)
        compare(backend.saveCount, 1); verify(!settings.dirty)
    }
    function test_requiredSecretDeletionRevokesAndRefreshesStatus() {
        settings.editSecret("apiKey", "delete", "")
        saveAndWait(); compare(backend.saveCount, 1)
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
        saveAndWait()
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
        saveAndWait(); compare(backend.values.count, 3)
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
        view.state = {title: "<b>Untrusted title</b>", ref: "station-1"}
        tryVerify(() => view.valid && JSON.stringify(view.acceptedState) === JSON.stringify(view.state), 5000, view.errorMessage)
        const label = findChild(view, "addonNode_label")
        compare(findChild(label, "addonText").textFormat, Text.PlainText)
        const node = findChild(view, "addonNode_play")
        mouseClick(node.children[0].item)
        compare(commandSpy.count, 1); compare(commandSpy.signalArguments[0][0], "org.example.radio/play")
        compare(commandSpy.signalArguments[0][1].item, "station-1")
    }
    function test_panelComponentsDispatchWithItemContext() {
        const command = name => ({command: "org.example.radio/" + name})
        view.registeredCommands = ["org.example.radio/play", "org.example.radio/season", "org.example.radio/expand",
                                   "org.example.radio/more", "org.example.radio/refresh"]
        view.hostArtwork = {"media:abc": "image://thumbnail/1"}
        view.document = viewDocument({type: "Column", children: [
            {type: "Tabs", id: "seasons", items: {bind: "state.seasons"}, selected: {bind: "state.season"}, text: "Seasons",
             action: Object.assign(command("season"), {arguments: {season: {bind: "item.tab.key"}}})},
            {type: "Menu", id: "options", text: "Episode options", entries: [
                {text: "Refresh", icon: "refresh", action: command("refresh")},
                {text: "Hidden", visible: {bind: "state.hidden"}, action: command("refresh")}]},
            {type: "List", id: "episodes", items: {bind: "state.episodes"}, keyField: "key", scroll: false,
             more: {bind: "state.more"}, moreAction: command("more"),
             itemTemplate: {type: "MediaRow", number: {bind: "item.number"}, title: {bind: "item.title"}, mark: {bind: "item.mark"},
                progress: {bind: "item.progress"}, expanded: {bind: "item.expanded"}, artwork: {bind: "item.artwork"},
                primaryAction: Object.assign(command("expand"), {arguments: {key: {bind: "item.key"}, open: {bind: "item.expanded"}}}),
                children: [
                    {type: "Tabs", id: "versions", items: {bind: "item.versions"}, selected: {bind: "item.version"}, text: "Episode version",
                     action: Object.assign(command("expand"), {arguments: {key: {bind: "item.key"}, version: {bind: "item.tab.key"}}})},
                    {type: "Button", id: "play", text: "Play", action: Object.assign(command("play"), {arguments: {key: {bind: "item.key"}}})}]}}]})
        view.state = {seasons: [{key: 1, label: "Season 1"}, {key: 2, label: "Season 2"}], season: 1, more: true, hidden: false,
            episodes: [{key: "a", number: "1", title: "Pilot", mark: "Now playing", progress: 0.5, expanded: true, artwork: "media:abc",
                        versions: [{key: "v0", label: "Show.S01E01.mkv"}, {key: "v1", label: "Show.S01E01.1080p.mkv"}], version: "v0"},
                       {key: "b", number: "2", title: "Second", mark: "", progress: 0, artwork: "", versions: [], version: ""}]}
        // State is applied on a short throttle timer; wait for it instead of a fixed delay.
        tryVerify(() => view.valid && JSON.stringify(view.acceptedState) === JSON.stringify(view.state), 5000, view.errorMessage)
        tryVerify(() => !!findChild(view, "addonTab_2"))
        mouseClick(findChild(view, "addonTab_2"))
        compare(commandSpy.signalArguments[commandSpy.count - 1][0], "org.example.radio/season")
        compare(commandSpy.signalArguments[commandSpy.count - 1][1].season, 2)
        tryVerify(() => !!findChild(view, "addonMediaRow_b"))
        mouseClick(findChild(view, "addonMediaRow_b"))
        compare(commandSpy.signalArguments[commandSpy.count - 1][1].key, "b")
        // A closed row leaves "expanded" out: the argument is left out, not null.
        verify(!("open" in commandSpy.signalArguments[commandSpy.count - 1][1]))
        tryVerify(() => !!findChild(view, "addonTab_v1"))
        mouseClick(findChild(view, "addonTab_v1"))
        compare(commandSpy.signalArguments[commandSpy.count - 1][1].key, "a")
        compare(commandSpy.signalArguments[commandSpy.count - 1][1].version, "v1")
        const more = findChild(view, "addonListMore_episodes")
        verify(more.visible); mouseClick(more)
        compare(commandSpy.signalArguments[commandSpy.count - 1][0], "org.example.radio/more")
        const menu = findChild(view, "addonMenu_options").parent.actionsMenu
        compare(menu.entries.length, 1)
        menu.actionRequested("0")
        compare(commandSpy.signalArguments[commandSpy.count - 1][0], "org.example.radio/refresh")
        // Host artwork is used only for media: handles that the app supplied.
        compare(view.hostArtworkFor("media:abc"), "image://thumbnail/1")
        compare(view.hostArtworkFor("media:other"), "")
        compare(view.hostArtworkFor("https://example.org/a.png"), "")
        compare(view.assetFor("media:abc"), "")
        // Tab entries need different keys and labels.
        view.state = Object.assign({}, view.acceptedState, {seasons: [{key: 1, label: "One"}, {key: 1, label: "Again"}]})
        tryVerify(() => !view.valid)
        for (const bad of [
                {type: "Tabs", items: [{key: 1, label: "One"}], text: "Seasons", action: command("season")},
                {type: "Tabs", items: {bind: "state.seasons"}, text: "Seasons"},
                {type: "Menu", text: "Options", entries: []},
                {type: "Menu", text: "Options", entries: Array(17).fill({text: "Refresh", action: command("refresh")})},
                {type: "Menu", text: "Options", entries: [{text: "Refresh", action: command("refresh"), url: "x"}]},
                {type: "MediaRow", number: "1"},
                {type: "List", items: {bind: "state.episodes"}, keyField: "key", scroll: "no", itemTemplate: {type: "Text", text: "x"}}])
            verify(Contract.validateView(viewDocument(bad), view.registeredCommands).length > 0, JSON.stringify(bad))
        view.hostArtwork = ({})
    }
    function test_panelLayoutOptions() {
        const command = name => ({command: "org.example.radio/" + name})
        view.registeredCommands = ["org.example.radio/play", "org.example.radio/seasons", "org.example.radio/source"]
        view.document = viewDocument({type: "Column", children: [
            {type: "Row", id: "header", children: [
                {type: "Button", id: "heading", grow: true, text: {bind: "state.show"}, subtitle: {bind: "state.season"}, icon: "chevron-left", action: command("seasons")},
                {type: "IconButton", id: "more", text: "More", icon: "more", action: command("seasons")}]},
            {type: "Text", id: "file", style: "caption", maxLines: 1, text: {bind: "state.file"}},
            {type: "Text", id: "notes", style: "small", maxLines: 3, text: "Notes"},
            {type: "Button", id: "source", intent: "link", text: "Episode source", action: command("source")},
            {type: "Tabs", id: "versions", style: "dropdown", items: {bind: "state.versions"}, text: "Version", action: command("play")},
            {type: "List", id: "seasons", items: {bind: "state.seasons"}, keyField: "key",
             itemTemplate: {type: "MediaRow", title: {bind: "item.label"}, icon: {bind: "item.icon"}, navigates: true,
                selected: {bind: "item.current"}, primaryAction: command("play")}}]})
        view.state = {show: "Show", season: "Season 1", file: "Show.S01E01.1080p.mkv",
            versions: [{key: "a", label: "A"}, {key: "b", label: "B"}],
            seasons: [{key: 1, label: "Season 1", icon: "check", current: true}, {key: 2, label: "Season 2", icon: "", current: false}]}
        // State is applied on a short throttle timer; wait for it instead of a fixed delay.
        tryVerify(() => view.valid && JSON.stringify(view.acceptedState) === JSON.stringify(view.state), 5000, view.errorMessage)
        // A growing child takes the free width on one line.
        const heading = findChild(view, "addonNode_heading"), more = findChild(view, "addonNode_more")
        verify(heading.width > more.width * 3, heading.width + " " + more.width)
        compare(heading.y, more.y)
        compare(findChild(heading, "addonActionSubtitle").text, "Season 1")
        const file = findChild(findChild(view, "addonNode_file"), "addonText")
        compare(file.maximumLineCount, 1); compare(file.elide, Text.ElideMiddle)
        compare(findChild(findChild(view, "addonNode_notes"), "addonText").maximumLineCount, 3)
        // Two versions still use the drop-down.
        verify(findChild(view, "addonTabs_versions").visible)
        // The current season is highlighted though no row is open.
        tryVerify(() => !!findChild(view, "addonMediaRow_1"))
        verify(findChild(view, "addonMediaRow_1").selected)
        verify(!findChild(view, "addonMediaRow_2").selected)
        for (const bad of [
                {type: "Text", text: "x", style: "huge"},
                {type: "Text", text: "x", maxLines: 0},
                {type: "Text", text: "x", grow: "yes"},
                {type: "Button", text: "x", intent: "fancy", action: command("play")},
                {type: "Tabs", items: {bind: "state.versions"}, text: "x", style: "grid", action: command("play")},
                {type: "MediaRow", title: "x", icon: "rocket"},
                {type: "MediaRow", title: "x", navigates: "yes"}])
            verify(Contract.validateView(viewDocument(bad), view.registeredCommands).length > 0, JSON.stringify(bad))
        // A bound media-row selection must be a flag.
        verify(Contract.validateProjection({type: "MediaRow", title: "x", selected: {bind: "state.flag"}}, {flag: "yes"}, null, {}).length > 0)
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
        // State is applied on a short throttle timer; wait for it instead of a fixed delay.
        view.state = {stations: [{ref: "one", title: "One"}]}
        tryVerify(() => view.valid && JSON.stringify(view.acceptedState) === JSON.stringify(view.state), 5000, view.errorMessage)
        view.state = {stations: [{ref: "same", title: "One"}, {ref: "same", title: "Two"}]}
        tryVerify(() => !view.valid); compare(view.acceptedState.stations.length, 1)
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
            properties: {apiKey: {type: "string", format: "password", writeOnly: true, "x-hakone-secret": true, "x-hakone-label": "settings.key", minLength: 1},
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
        for (const mutation of [{maxLength: -1}, {maxItems: 1.5}, {"x-hakone-secret": "yes"}, {title: {bind: "state.name"}}]) {
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
        view.state = {available: true}
        // State is applied on a short throttle timer; wait for it instead of a fixed delay.
        tryVerify(() => node.enabled)
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

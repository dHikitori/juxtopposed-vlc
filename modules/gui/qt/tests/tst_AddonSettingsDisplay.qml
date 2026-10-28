// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "AddonSettingsDisplay"
    width: 700; height: 800; visible: true; when: windowShown
    QtObject {
        id: backend
        property int revision: 0
        property int catalogueRevision: 0
        property bool catalogueReady: true
        property bool catalogueLoading: false
        property string catalogueError: ""
        property string stamp: "release:1:0"
        property string state: "pending"
        property int nextTicket: 0
        property int acquired: 0
        property int released: 0
        property int legacyReads: 0
        property int saves: 0
        property int retries: 0
        property bool refuseTicket: false
        property bool catalogueStaleOnPreparation: true
        property var duringAcquire: null
        property var activeTickets: ({})
        property bool credentialPersistenceAvailable: false
        property var schema: ({type: "object", additionalProperties: false,
            properties: {label: {type: "string", default: "Initial"}, apiKey: {type: "string", "x-hakone-secret": true, minLength: 1}}, required: ["apiKey"]})
        property var values: ({label: "Initial"})
        property var secrets: ({apiKey: {present: true}})
        property var lastOperations: ({})
        signal catalogueChanged()
        signal catalogueFreshnessChanged()
        signal packageWithdrawn(string id)
        signal retiring()
        function displayCatalogueAttached() { return true }
        function displaySettingsStamp(id) { return stamp }
        function displaySettingsState(id) { return state }
        function displaySettingsSchema(id) { return schema }
        function displaySettingsValues(id) { return values }
        function displaySettingsSecretStatus(id) { return secrets }
        function displaySettingsMessages(id) { return {credential: "Saved credential"} }
        function prepareDisplaySettings(id) {
            if (refuseTicket) return ""
            acquired++; const ticket = "ticket-" + (++nextTicket)
            activeTickets = Object.assign({}, activeTickets, {[ticket]: true})
            if (catalogueStaleOnPreparation) { catalogueReady = false; catalogueLoading = true }
            state = "pending"; catalogueFreshnessChanged()
            const action = duringAcquire; if (action) action()
            return ticket
        }
        function releaseDisplayRequest(ticket) { const copy = Object.assign({}, activeTickets); delete copy[ticket]; activeTickets = copy; released++ }
        function displayRequestCurrent(ticket) { return activeTickets[ticket] === true && catalogueReady }
        function refreshDisplayCatalogue() { retries++; catalogueLoading = true; catalogueReady = false; catalogueError = ""; catalogueFreshnessChanged() }
        function saveDisplaySettings(id, expectedStamp, nextValues, operations) {
            if (!catalogueReady || state !== "ready" || expectedStamp !== stamp) return false
            saves++; lastOperations = operations
            catalogueReady = false; catalogueLoading = true; catalogueFreshnessChanged(); packageWithdrawn(id)
            return true
        }
        function settingsSchema(id) { legacyReads++; return schema }
        function settingsValues(id) { legacyReads++; return values }
        function settingsMessages(id) { legacyReads++; return {} }
        function settingsSecretStatus(id) { legacyReads++; return secrets }
        function saveAddonSettings(id, values, operations) { legacyReads++; return false }
        function complete() { state = "ready"; catalogueReady = true; catalogueLoading = false; catalogueRevision++; catalogueChanged(); catalogueFreshnessChanged() }
    }
    Component { id: formFactory; RedesignAddonSettings { width: 550; packageId: "org.example.settings" } }
    Component { id: dialogFactory; RedesignAddonSettingsDialog { packageId: "org.example.settings" } }
    function createForm() {
        const form = createTemporaryObject(formFactory, testCase, {backend: backend})
        verify(form !== null); return form
    }
    function init() {
        backend.catalogueReady = true; backend.catalogueLoading = false; backend.catalogueError = ""; backend.stamp = "release:1:0"; backend.state = "pending"
        backend.acquired = 0; backend.released = 0; backend.legacyReads = 0; backend.saves = 0; backend.retries = 0; backend.refuseTicket = false; backend.catalogueStaleOnPreparation = true; backend.duringAcquire = null; backend.activeTickets = ({})
        backend.values = {label: "Initial"}; backend.secrets = {apiKey: {present: true}}; backend.lastOperations = ({})
    }
    function test_unrelatedManagerRevisionKeepsPrivateDraftWithoutLegacyReads() {
        const form = createForm(); backend.complete(); tryCompare(form, "loaded", true)
        form.setValue("label", "Private draft"); form.editSecret("apiKey", "replace", "fixture-only-draft", true)
        backend.revision++; verify(form.dirty); verify(!form.conflict); compare(form.draftValues.label, "Private draft")
        compare(form.secretOperations.apiKey.value, "fixture-only-draft"); compare(backend.legacyReads, 0)
    }
    function test_successorViewCannotReuseTheFormsOldLease() {
        const form = createForm(); backend.complete(); tryCompare(form, "loaded", true); form.editSecret("apiKey", "replace", "fixture-only-draft", true)
        backend.catalogueReady = false; backend.catalogueFreshnessChanged(); backend.activeTickets = ({})
        backend.values = {label: "Successor"}; backend.complete()
        verify(form.retired); verify(form.conflict); verify(!form.save()); compare(backend.saves, 0)
        compare(form.draftValues.label, "Initial"); compare(form.secretOperations.apiKey.value, "fixture-only-draft"); compare(backend.released, 1)
    }
    function test_nativeStyleDocumentDemandKeepsCatalogueCurrentAndWaitsForSettings() {
        backend.catalogueStaleOnPreparation = false
        const form = createForm(); verify(backend.catalogueReady); verify(!form.loaded); verify(!form.dataReady); verify(!form.save())
        backend.complete(); tryCompare(form, "loaded", true); verify(form.dataReady); compare(backend.legacyReads, 0)
    }
    function test_discardDuringSynchronousAcquisitionReleasesTheReturnedTicket() {
        const form = createForm(); backend.complete(); tryCompare(form, "loaded", true); form.discard(); compare(backend.released, 1)
        backend.duringAcquire = function() { form.discard() }
        form.reload(); compare(form.settingsTicket, ""); compare(backend.acquired, 2); compare(backend.released, 2)
        verify(!form.loaded); verify(!form.preparing); compare(backend.legacyReads, 0)
        backend.duringAcquire = null; backend.complete(); verify(!form.loaded)
    }
    function test_preparationHasNoLegacyReadAndCancelStartsNoNewWork() {
        const form = createForm(); verify(form.displayMode); verify(!form.loaded); verify(!form.dataReady); compare(backend.acquired, 1); compare(backend.legacyReads, 0)
        verify(!form.save()); backend.complete(); tryCompare(form, "loaded", true)
        compare(form.draftValues.label, "Initial"); form.editSecret("apiKey", "replace", "fixture-only-draft", true)
        form.discard(); compare(backend.released, 1); compare(backend.acquired, 1); compare(Object.keys(form.secretOperations).length, 0); compare(backend.legacyReads, 0)
    }
    function test_unrelatedRefreshKeepsProtectedDraftAndUsesFreshFinalGate() {
        const form = createForm(); backend.complete(); tryCompare(form, "loaded", true)
        form.setValue("label", "Private draft"); form.editSecret("apiKey", "replace", "fixture-only-draft", true)
        backend.catalogueReady = false; backend.catalogueLoading = true; backend.catalogueFreshnessChanged()
        verify(!form.dataReady); verify(!form.save()); compare(form.draftValues.label, "Private draft"); compare(form.secretOperations.apiKey.value, "fixture-only-draft"); verify(!form.conflict)
        backend.complete(); verify(!form.conflict); compare(form.draftValues.label, "Private draft"); compare(form.secretOperations.apiKey.value, "fixture-only-draft")
        verify(form.save()); compare(backend.saves, 1); compare(backend.lastOperations.apiKey.value, "fixture-only-draft"); compare(backend.legacyReads, 0)
    }
    function test_selectedPartitionChangeRefusesWithoutDestroyingDraft() {
        const form = createForm(); backend.complete(); tryCompare(form, "loaded", true); form.editSecret("apiKey", "replace", "fixture-only-draft", true)
        backend.catalogueReady = false; backend.catalogueFreshnessChanged(); backend.stamp = "release:2:0"; backend.complete()
        verify(form.conflict); verify(!form.save()); compare(backend.saves, 0); compare(form.secretOperations.apiKey.value, "fixture-only-draft"); compare(backend.legacyReads, 0)
    }
    function test_explicitWithdrawalRetiresOnlyTheSelectedForm() {
        const form = createForm(); backend.complete(); tryCompare(form, "loaded", true); form.setValue("label", "Draft")
        backend.packageWithdrawn("org.example.other"); verify(!form.retired)
        backend.packageWithdrawn("org.example.settings"); verify(form.retired); verify(form.conflict); verify(!form.save()); compare(backend.released, 1); compare(form.draftValues.label, "Draft")
    }
    function test_refusalAndRetryDoNotFallBackOrDiscardPrivateInput() {
        const form = createForm(); backend.complete(); tryCompare(form, "loaded", true); form.editSecret("apiKey", "replace", "fixture-only-draft", true)
        backend.catalogueReady = false; backend.catalogueLoading = false; backend.catalogueError = "Fixture refusal"; backend.catalogueFreshnessChanged()
        form.retryPreparation(); compare(backend.retries, 1); compare(form.secretOperations.apiKey.value, "fixture-only-draft"); compare(backend.acquired, 1); compare(backend.legacyReads, 0)
        backend.complete(); verify(!form.conflict); compare(form.secretOperations.apiKey.value, "fixture-only-draft")
    }
    function test_ticketRefusalIsHonestAndHasNoLegacyReads() {
        backend.refuseTicket = true; const form = createForm(); verify(form.displayMode); verify(!form.loaded); verify(form.errorMessage.length > 0); verify(!form.save()); compare(backend.legacyReads, 0); compare(backend.acquired, 0)
    }
    function test_dialogRetainsPreparedLocaleDuringRefreshAndReleasesOnce() {
        const dialog = createTemporaryObject(dialogFactory, testCase, {backend: backend, parent: testCase})
        verify(dialog !== null); dialog.open(); compare(backend.acquired, 1); backend.complete(); tryCompare(dialog.form, "loaded", true)
        compare(dialog.form.messages.credential, "Saved credential"); backend.catalogueReady = false; backend.catalogueLoading = true; backend.catalogueFreshnessChanged()
        compare(dialog.form.messages.credential, "Saved credential"); dialog.close(); tryCompare(backend, "released", 1); compare(backend.acquired, 1); compare(backend.legacyReads, 0)
    }
}

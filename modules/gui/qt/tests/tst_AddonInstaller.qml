// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "AddonInstaller"
    width: 900; height: 1200; visible: true; when: windowShown
    QtObject {
        id: manager
        property int revision: 0
        property bool busy: false
        property bool credentialPersistenceAvailable: true
        property string errorMessage: ""
        property var pendingPackage: ({})
        property var packages: []
        property int stages: 0
        property int approvals: 0
        property int discards: 0
        property int rollbacks: 0
        property int removals: 0
        property string lastSource: ""
        property string lastTag: ""
        property string lastDirectory: ""
        property string lastPackage: ""
        property bool lastEnable: false
        property bool removedData: false
        function stageRepository(source, tag) { stages++; lastSource = source; lastTag = tag; busy = true; return true }
        function stageLocalPackage(directory) { stages++; lastDirectory = directory; busy = true; return true }
        function complete(developer) {
            busy = false
            pendingPackage = {id: "org.example.radio", name: "<b>Radio</b>", version: "1.0.0", source: lastSource || "local-development", tag: lastTag || "v1.0.0",
                digest: "0123456789abcdef", developerSource: developer, publisherVerified: false, permissions: {"network.http": {origins: ["https://api.example.org"]}}}
            revision++
        }
        function approvePendingPackage(enable) { approvals++; lastEnable = enable; pendingPackage = ({}); revision++; return true }
        function discardPendingPackage() { discards++; pendingPackage = ({}); revision++ }
        function setEnabled(id, enabled) { return true }
        function rollback(id) { rollbacks++; lastPackage = id; return true }
        function uninstall(id, removeData) { removals++; lastPackage = id; removedData = removeData; return true }
        function settingsSchema(id) { return {type: "object", properties: {}, additionalProperties: false} }
        function settingsValues(id) { return {} }
        function settingsSecretStatus(id) { return {} }
    }
    RedesignAddonManagement { id: page; width: 600; backend: manager }
    function item(name) { return findChild(page, name) }
    function init() {
        manager.busy = false; manager.pendingPackage = ({}); manager.packages = []
        manager.stages = 0; manager.approvals = 0; manager.discards = 0; manager.rollbacks = 0; manager.removals = 0
        manager.lastSource = ""; manager.lastTag = ""; manager.lastDirectory = ""; manager.lastEnable = false
        page.developerSource = false; page.enableAfterInstall = false
        item("addonRepositorySource").clear(); item("addonRepositoryTag").clear(); item("addonLocalDirectory").clear()
        manager.revision++; wait(20)
    }
    function cleanup() {
        const dialog = item("addonPackageChangeDialog")
        if (dialog.visible) dialog.close()
        wait(20)
    }
    function stageRepository() {
        item("addonRepositorySource").text = "https://git.example.org/community/radio.git"
        item("addonRepositoryTag").text = "v1.0.0"
        mouseClick(item("addonStagePackage"))
    }
    function test_repositoryStagingNeverInstallsOrGrants() {
        verify(!item("addonStagePackage").enabled)
        stageRepository()
        compare(manager.stages, 1); compare(manager.lastSource, "https://git.example.org/community/radio.git"); compare(manager.lastTag, "v1.0.0")
        compare(manager.approvals, 0); verify(!item("addonStagePackage").enabled)
        manager.complete(false); wait(30)
        verify(item("addonInstallReview").visible)
        compare(item("addonInstallPermissionSummary").textFormat, Text.PlainText)
        verify(item("addonInstallPermissionSummary").text.includes("api.example.org"))
        verify(!item("addonInstallPermissionSummary").text.includes("network.http"))
        verify(!item("addonInstallPermissions").visible)
        mouseClick(item("addonInstallTechnicalDetails"))
        verify(item("addonInstallPermissions").visible)
        compare(item("addonInstallPermissions").textFormat, TextEdit.PlainText)
        verify(item("addonInstallPermissions").text.includes("network.http"))
        compare(manager.approvals, 0)
        mouseClick(item("addonInstallApprove"))
        compare(manager.approvals, 1); verify(!manager.lastEnable)
    }
    function test_enableRequiresSeparateExplicitChoice() {
        stageRepository(); manager.complete(false); wait(30)
        const toggle = findChild(item("addonInstallEnable"), "settingSwitch")
        mouseClick(toggle)
        verify(page.enableAfterInstall)
        mouseClick(item("addonInstallApprove"))
        compare(manager.approvals, 1); verify(manager.lastEnable)
        verify(!page.enableAfterInstall)
    }
    function test_cancelDiscardsCandidateWithoutInstall() {
        stageRepository(); manager.complete(false); wait(30)
        mouseClick(item("addonInstallCancel"))
        compare(manager.discards, 1); compare(manager.approvals, 0)
        verify(!item("addonInstallReview").visible)
    }
    function test_localDevelopmentPathHasSameReviewGate() {
        page.developerSource = true
        item("addonLocalDirectory").text = "/tmp/example-addon"
        mouseClick(item("addonStagePackage"))
        compare(manager.lastDirectory, "/tmp/example-addon"); compare(manager.approvals, 0)
        manager.complete(true); wait(30)
        verify(item("addonInstallOverview").text.includes("Local development package"))
        mouseClick(item("addonInstallApprove"))
        compare(manager.approvals, 1); verify(!manager.lastEnable)
    }
    function test_removeAndRollbackWaitForCoreConfirmation() {
        manager.packages = [{id: "org.example.radio", name: "Radio", version: "2.0.0", configured: true, enabled: true, hasPrevious: true}]
        manager.revision++; wait(30)
        mouseClick(item("addonPackageRollback_org.example.radio"))
        const dialog = item("addonPackageChangeDialog")
        tryCompare(dialog, "opened", true); compare(manager.rollbacks, 0)
        mouseClick(findChild(dialog, "confirmationAccept"))
        compare(manager.rollbacks, 1); compare(manager.lastPackage, "org.example.radio")
        tryCompare(dialog, "visible", false)
        mouseClick(item("addonPackageRemove_org.example.radio")); tryCompare(dialog, "opened", true)
        compare(manager.removals, 0)
        mouseClick(findChild(dialog, "confirmationAccept"))
        compare(manager.removals, 1); verify(!manager.removedData)
    }
    function test_permissionSummaryNamesDataActionsAndBoundDestinations() {
        const summary = page.permissionSummaries({"metadata.context": {fields: ["title", "year"]},
            "network.http": {origins: ["https://api.example.org"]},
            "credentials.use": {bindings: [{field: "apiKey", origin: "https://api.example.org", header: "Authorization"}]},
            "ui.contribute": {hooks: ["home.sections@1"]}, "playback.control": {operations: ["pause"]}})
        verify(summary.includes("selected media")); verify(summary.includes("api.example.org"))
        verify(summary.includes("saved credentials")); verify(summary.includes("Home sections")); verify(summary.includes("playback"))
        verify(!summary.includes("metadata.context")); verify(!summary.includes("Authorization"))
        manager.pendingPackage = {id: "org.example.bad", name: "Bad", permissions: {"universal.access": {}}}
        wait(20)
        verify(!item("addonInstallApprove").enabled)
    }
    function test_busyAndNonHttpsInputsCannotStage() {
        item("addonRepositorySource").text = "file:///tmp/repository"; item("addonRepositoryTag").text = "v1.0.0"
        verify(!item("addonStagePackage").enabled)
        item("addonRepositorySource").text = "https://git.example.org/radio.git"
        manager.busy = true
        verify(!item("addonStagePackage").enabled)
        compare(manager.stages, 0)
    }
    function test_imageRedirectScopeIsVisibleAndUnknownProfileBlocksApproval() {
        manager.pendingPackage = {id: "org.example.images", name: "Images", permissions: {"network.http": {
            origins: ["https://coverartarchive.org"], assetRedirectProfiles: ["internet-archive-images@1"]}}}
        wait(20)
        const summary = item("addonInstallPermissionSummary")
        compare(summary.textFormat, Text.PlainText)
        verify(summary.text.includes("coverartarchive.org"))
        verify(summary.text.includes("archive.org and its subdomains"))
        verify(summary.text.includes("HTTPS on port 443"))
        verify(summary.text.includes("do not use credentials"))
        verify(item("addonInstallApprove").enabled)
        manager.pendingPackage = {id: "org.example.images", name: "Images", permissions: {"network.http": {
            origins: ["https://coverartarchive.org"], assetRedirectProfiles: ["unknown-profile@1"]}}}
        wait(20)
        verify(!item("addonInstallApprove").enabled)
    }
}

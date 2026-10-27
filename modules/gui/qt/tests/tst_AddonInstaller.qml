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
        property bool safeMode: false
        property int recoveries: 0
        property int restores: 0
        property string errorMessage: ""
        property var pendingPackage: ({})
        property var packages: []
        property var updateResult: ({})
        property var capabilities: ({workerAvailable: true, networkHttpAvailable: true, privateStorageAvailable: false, webSocketAvailable: false, coreOperations: []})
        property int updateRequests: 0
        property string lastRange: ""
        property bool lastPrerelease: true
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
        function approvePendingPackage(enable) { approvals++; lastEnable = enable; pendingPackage = ({}); updateResult = ({}); revision++; return true }
        function discardPendingPackage() { discards++; pendingPackage = ({}); busy = false; updateResult = updateResult.packageId ? {packageId: updateResult.packageId, checking: false, canceled: true} : ({}); revision++ }
        function checkRepositoryUpdates(id, range, prerelease) { updateRequests++; lastPackage = id; lastRange = range; lastPrerelease = prerelease; updateResult = {packageId: id, checking: true}; busy = true; revision++; return true }
        function completeUpdate(available) {
            busy = false; updateResult = {packageId: lastPackage, checking: false, available: available, canceled: false}
            if (available) pendingPackage = {id: lastPackage, name: "Radio", version: "1.1.0", source: "https://git.example.org/radio.git", tag: "v1.1.0",
                permissions: {}, hostCapabilities: capabilities, workerRequired: true}
            revision++
        }
        function setEnabled(id, enabled) { return true }
        function setSafeMode(enabled) { safeMode = enabled; revision++; return true }
        function recoverPackage(id) { recoveries++; lastPackage = id; return true }
        function stageDistributionPackage(id) {
            restores++; lastPackage = id
            pendingPackage = {id: id, name: "App package", version: "2.0.0", source: "https://git.example.org/app.git", distributionSource: true, permissions: {}}
            revision++; return true
        }
        function retainedGenerations(id) { return [{version: "1.0.0"}] }
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
        manager.safeMode = false; manager.recoveries = 0; manager.restores = 0
        manager.updateResult = ({}); manager.updateRequests = 0; manager.lastRange = ""; manager.lastPrerelease = true; manager.errorMessage = ""
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
    function test_coreRecoveryControlsAreMounted() {
        const recovery = item("addonManagementRecovery")
        verify(recovery.supported); item("addonSafeMode").clicked(); compare(manager.safeMode, true)
        manager.packages = [{id: "org.example.radio", name: "<b>Radio</b>", recoveryRequired: true, installed: false}]; manager.revision++
        tryVerify(() => item("addonRecover_org.example.radio") !== null)
        verify(item("addonPackage_org.example.radio") === null)
        item("addonRecover_org.example.radio").clicked()
        const confirmation = item("addonRecoveryConfirmation")
        tryVerify(() => confirmation.visible); compare(manager.recoveries, 0)
        confirmation.accept(); compare(manager.recoveries, 1)
    }
    function test_builtinAndRemovedPackagesHaveNoRepositoryActions() {
        manager.packages = [{id: "vlc.builtin.opensubtitles", name: "OpenSubtitles", builtin: true, installed: true},
            {id: "org.example.app", name: "App package", distributionSource: true, suppressed: true, installed: false}]; manager.revision++
        compare(page.managedPackages.length, 0)
        verify(item("addonPackage_vlc.builtin.opensubtitles") === null)
        verify(item("addonPackage_org.example.app") === null)
        tryVerify(() => item("addonRestore_org.example.app") !== null)
    }
    function test_appUpdateAndRestoreUseTheExistingPermissionReview() {
        manager.packages = [{id: "org.example.app", name: "App", version: "1.0.0", distributionSource: true,
            appUpdateAvailable: true, appUpdateVersion: "2.0.0", enabled: true, configured: true}]; manager.revision++
        tryVerify(() => item("addonPackageAppUpdate_org.example.app") !== null)
        item("addonPackageAppUpdate_org.example.app").clicked()
        compare(manager.restores, 1); compare(manager.approvals, 0)
        verify(item("addonInstallReview").visible); verify(item("addonInstallOverview").text.includes("app distribution"))
        item("addonInstallCancel").clicked(); compare(manager.approvals, 0)
        manager.packages = [{id: "org.example.app", name: "App", distributionSource: true, installed: false, suppressed: true}]; manager.revision++
        tryVerify(() => item("addonRestore_org.example.app") !== null)
        item("addonRestore_org.example.app").clicked(); compare(manager.restores, 2); compare(manager.approvals, 0)
        item("addonInstallApprove").clicked(); compare(manager.approvals, 1)
    }
    function test_fullDeletionAndStaleConfirmationHaveDifferentOutcomes() {
        manager.packages = [{id: "org.example.radio", name: "Radio", version: "2.0.0", enabled: true, configured: true}]; manager.revision++
        tryVerify(() => item("addonPackageRemoveData_org.example.radio") !== null)
        item("addonPackageRemoveData_org.example.radio").clicked()
        const dialog = item("addonPackageChangeDialog")
        tryCompare(dialog, "opened", true); verify(dialog.message.includes("saved keys"))
        dialog.reject(); compare(manager.removals, 0)
        verify(page.requestPackageChange("org.example.radio", "removeData")); ++manager.revision
        dialog.accept(); compare(manager.removals, 0); verify(page.actionError.includes("changed"))
        verify(page.requestPackageChange("org.example.radio", "removeData")); dialog.accept()
        compare(manager.removals, 1); verify(manager.removedData)
    }
    function test_removalCannotOfferRepositoryCancel() {
        manager.packages = [{id: "org.example.radio", name: "Radio", installed: true, removing: true}]; manager.revision++
        manager.busy = true
        verify(!item("addonRepositoryCancel").visible)
        verify(item("addonSafeMode").enabled); item("addonSafeMode").clicked(); verify(manager.safeMode)
    }
    function test_reviewUsesLiveSafeModeInsteadOfItsOldCapabilitySnapshot() {
        manager.safeMode = true
        manager.pendingPackage = {id: "org.example.app", name: "App", permissions: {}, hostCapabilities: {safeMode: true, workerAvailable: true}}
        verify(!item("addonInstallEnable").enabled)
        item("addonSafeMode").clicked(); compare(manager.safeMode, false)
        verify(item("addonInstallEnable").enabled)
        page.enableAfterInstall = true
        item("addonSafeMode").clicked(); compare(manager.safeMode, true)
        item("addonInstallApprove").clicked(); compare(manager.approvals, 1); verify(!manager.lastEnable)
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
    function test_streamProfileReview_data() {
        return [
            {tag: "public-v1", scope: {profiles: ["public-https-media@1"]}, valid: true},
            {tag: "missing", scope: {}, valid: false},
            {tag: "empty", scope: {profiles: []}, valid: false},
            {tag: "scalar", scope: {profiles: "public-https-media@1"}, valid: false},
            {tag: "future", scope: {profiles: ["public-https-media@2"]}, valid: false},
            {tag: "duplicate", scope: {profiles: ["public-https-media@1", "public-https-media@1"]}, valid: false},
            {tag: "mixed", scope: {profiles: ["public-https-media@1", "private-media@1"]}, valid: false},
            {tag: "extra", scope: {profiles: ["public-https-media@1"], origins: ["https://example.org"]}, valid: false},
            {tag: "null", scope: null, valid: false}
        ]
    }
    function test_streamProfileReview(data) {
        const permissions = {"media.stream": data.scope}
        manager.pendingPackage = {id: "org.hakone.radio", name: "Radio", permissions: permissions}
        wait(20)
        compare(page.permissionsKnown, data.valid)
        compare(item("addonInstallApprove").enabled, data.valid)
        compare(manager.approvals, 0)
        if (!data.valid) return
        const summary = item("addonInstallPermissionSummary")
        compare(summary.textFormat, Text.PlainText)
        verify(summary.text.includes("public online services"))
        verify(summary.text.includes("Each source also needs your playback approval"))
        verify(!summary.text.includes("public-https-media@1"))
        verify(page.capabilityLimits({}, permissions, false).includes("cannot play media"))
        compare(page.capabilityLimits({mediaStreamAvailable: true}, permissions, false), "")
    }
    function test_updateUsesInstalledSourceAndStableCompatibleRange() {
        manager.packages = [{id: "org.example.radio", name: "Radio", source: "https://git.example.org/radio.git", version: "1.0.0", enabled: true, configured: true}]
        manager.revision++; wait(20)
        mouseClick(item("addonPackageFindUpdates_org.example.radio"))
        compare(manager.updateRequests, 1); compare(manager.lastPackage, "org.example.radio")
        compare(manager.lastRange, "^1.0.0"); verify(!manager.lastPrerelease)
        compare(manager.approvals, 0); verify(manager.packages[0].enabled)
        manager.completeUpdate(true); wait(20)
        verify(item("addonInstallReview").visible); verify(item("addonUpdateStatus").text.includes("approval"))
        compare(item("addonInstallEnable").label, "Enable if disabled")
        verify(!page.enableAfterInstall); compare(manager.approvals, 0)
        mouseClick(item("addonInstallApprove")); compare(manager.approvals, 1); verify(!manager.lastEnable)
        verify(manager.packages[0].enabled)
    }
    function test_updateCancelKeepsInstalledRelease() {
        manager.packages = [{id: "org.example.radio", name: "Radio", source: "https://git.example.org/radio.git", version: "1.0.0", enabled: true}]
        manager.revision++; wait(20)
        mouseClick(item("addonPackageFindUpdates_org.example.radio"))
        mouseClick(item("addonRepositoryCancel"))
        compare(manager.discards, 1); compare(manager.approvals, 0)
        verify(manager.packages[0].enabled); compare(manager.packages[0].version, "1.0.0")
        verify(item("addonUpdateStatus").text.includes("canceled"))
    }
    function test_noUpdateAndHostLimitsUsePlainStatements() {
        manager.lastPackage = "org.example.radio"; manager.completeUpdate(false); wait(20)
        verify(item("addonUpdateStatus").text.includes("No compatible stable update"))
        manager.pendingPackage = {id: "org.example.radio", name: "Radio", version: "1.1.0", workerRequired: true,
            hostCapabilities: {workerAvailable: false, privateStorageAvailable: false, webSocketAvailable: false, coreOperations: []},
            permissions: {"storage.private": {quotaBytes: 1024}, "network.websocket": {origins: ["wss://example.org"]}}}
        wait(20)
        const limits = item("addonInstallCapabilityLimits")
        compare(limits.textFormat, Text.PlainText)
        verify(limits.text.includes("cannot run add-on code")); verify(limits.text.includes("private add-on storage")); verify(limits.text.includes("live service connection"))
        verify(!item("addonInstallEnable").enabled); verify(!page.enableAfterInstall)
        verify(item("addonInstallApprove").enabled)
    }
    function test_localAndAppPackagesDoNotOfferRemoteUpdates() {
        manager.packages = [{id: "org.example.local", name: "Local", source: "https://git.example.org/local.git", version: "1.0.0", developerSource: true},
            {id: "org.example.bundled", name: "App", source: "https://git.example.org/app.git", version: "1.0.0", distributionSource: true}]
        manager.revision++; wait(20)
        verify(!item("addonPackageFindUpdates_org.example.local").visible)
        verify(!item("addonPackageFindUpdates_org.example.bundled").visible)
    }
}

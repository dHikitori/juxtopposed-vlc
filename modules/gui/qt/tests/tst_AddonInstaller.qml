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
        property int crashes: 0
        property bool observing: false
        function health(id) { return {crashes: crashes, retryInMs: 0, observing: observing} }
        property var updateResult: ({})
        property var capabilities: ({workerAvailable: true, networkHttpAvailable: true, privateStorageAvailable: false, webSocketAvailable: false, coreOperations: []})
        property int updateRequests: 0
        property string lastRange: ""
        property bool lastPrerelease: true
        property int settingsSaves: 0
        property bool invalidateSummaryDuringSave: false
        property bool asyncRemoval: false
        property int enableRequests: 0
        property int settingsReads: 0
        property bool draftSchema: false
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
        function setEnabled(id, enabled) { enableRequests++; return true }
        function setSafeMode(enabled) { safeMode = enabled; revision++; return true }
        function recoverPackage(id) { recoveries++; lastPackage = id; return true }
        function stageDistributionPackage(id) {
            restores++; lastPackage = id
            pendingPackage = {id: id, name: "App package", version: "2.0.0", source: "https://git.example.org/app.git", distributionSource: true, permissions: {}}
            revision++; return true
        }
        function retainedGenerations(id) { return [{version: "1.0.0"}] }
        function rollback(id) { rollbacks++; lastPackage = id; return true }
        function uninstall(id, removeData) {
            removals++; lastPackage = id; removedData = removeData
            if (asyncRemoval) { busy = true; summary.available = false; summary.loading = true; summary.rows = [] }
            return true
        }
        function saveAddonSettings(id, values, secrets) {
            settingsSaves++
            if (invalidateSummaryDuringSave) { summary.available = false; summary.loading = true }
            revision++
            return true
        }
        function settingsSchema(id) {
            settingsReads++
            return {type: "object", properties: draftSchema ? {nickname: {type: "string", title: "Name"}} : {}, additionalProperties: false}
        }
        function settingsValues(id) { settingsReads++; return {} }
        function settingsSecretStatus(id) { settingsReads++; return {} }
    }
    QtObject {
        id: summary
        property var rows: []
        property bool loading: false
        property string error: ""
        property bool available: false
        property bool current: false
        property bool autoStart: false
        property bool displayBacked: false
        property bool safeModeKnown: false
        property bool safeMode: false
        property int refreshes: 0
        property int currentCalls: 0
        function isCurrent() { currentCalls++; return current }
        function refresh() {
            refreshes++
            if (autoStart) { rows = []; available = false; loading = true; error = "" }
        }
    }
    RedesignAddonManagement { id: page; width: 600; backend: manager }
    function item(name) {
        let found = findChild(page, name)
        if (found) return found
        for (const popupName of ["addonManagementRecovery", "addonInstallDialog", "addonManagementSettingsDialog"]) {
            const popup = findChild(page, popupName)
            if (popup) { found = findChild(popup, name); if (found) return found }
        }
        return null
    }
    function init() {
        page.closeDialogs(); wait(30)
        page.summaryBackend = null
        summary.rows = []; summary.loading = false; summary.error = ""; summary.available = false; summary.current = false
        summary.autoStart = false; summary.refreshes = 0; summary.currentCalls = 0
        summary.displayBacked = false; summary.safeModeKnown = false; summary.safeMode = false
        manager.enableRequests = 0; manager.settingsSaves = 0; manager.draftSchema = false
        manager.invalidateSummaryDuringSave = false; manager.asyncRemoval = false
        page.actionError = ""; page.removalRequested = false
        page.selectedId = ""; page.advanced = false; page.active = true
        item("addonSearch").clear()
        manager.busy = false; manager.pendingPackage = ({}); manager.packages = []
        manager.safeMode = false; manager.recoveries = 0; manager.restores = 0
        manager.updateResult = ({}); manager.updateRequests = 0; manager.lastRange = ""; manager.lastPrerelease = true; manager.errorMessage = ""
        manager.stages = 0; manager.approvals = 0; manager.discards = 0; manager.rollbacks = 0; manager.removals = 0
        manager.lastSource = ""; manager.lastTag = ""; manager.lastDirectory = ""; manager.lastEnable = false
        page.developerSource = false; page.enableAfterInstall = false
        manager.revision++; wait(20)
    }
    function cleanup() {
        const dialog = item("addonPackageChangeDialog")
        page.closeDialogs()
        wait(20)
    }
    function openInstaller() { page.openInstaller(); wait(30) }
    function selectPackage(id) { page.selectPackage(id); wait(20) }
    function packageAction(action) {
        item("addonMoreActions").clicked(); wait(20)
        const entry = item("libraryAction_" + action)
        verify(entry && entry.enabled); mouseClick(entry)
        wait(20)
    }
    function stageRepository() {
        openInstaller()
        item("addonRepositorySource").text = "https://git.example.org/community/radio.git"
        item("addonRepositoryTag").text = "v1.0.0"
        mouseClick(item("addonStagePackage"))
    }
    function test_coreRecoveryControlsAreMounted() {
        page.openRecovery(); wait(30)
        const recovery = item("addonManagementRecovery")
        verify(recovery.supported); findChild(recovery, "addonSafeMode").clicked(); compare(manager.safeMode, true)
        manager.packages = [{id: "org.example.radio", name: "<b>Radio</b>", recoveryRequired: true, installed: false}]; manager.revision++
        tryVerify(() => item("addonRecover_org.example.radio") !== null)
        verify(item("addonPackage_org.example.radio") === null)
        item("addonRecover_org.example.radio").clicked()
        const confirmation = item("addonRecoveryConfirmation")
        tryVerify(() => confirmation.visible); compare(manager.recoveries, 0)
        confirmation.accept(); compare(manager.recoveries, 1)
    }
    function test_builtinAndRemovedPackagesHaveNoRepositoryActions() {
        manager.packages = [{id: "hakone.builtin.opensubtitles", name: "OpenSubtitles", builtin: true, installed: true},
            {id: "org.example.app", name: "App package", distributionSource: true, suppressed: true, installed: false}]; manager.revision++
        compare(page.managedPackages.length, 0)
        verify(item("addonPackage_hakone.builtin.opensubtitles") === null)
        verify(item("addonPackage_org.example.app") === null)
        page.openRecovery(); wait(30)
        tryVerify(() => item("addonRestore_org.example.app") !== null)
    }
    function test_appUpdateAndRestoreUseTheExistingPermissionReview() {
        manager.packages = [{id: "org.example.app", name: "App", version: "1.0.0", distributionSource: true,
            appUpdateAvailable: true, appUpdateVersion: "2.0.0", enabled: true, configured: true}]; manager.revision++
        // The list row shows the offer without opening the package.
        tryCompare(item("addonPackageStatus_org.example.app"), "text", "1.0.0 · Enabled")
        verify(item("addonPackageUpdate_org.example.app").visible)
        compare(item("addonPackageUpdate_org.example.app").label, "Update to 2.0.0")
        selectPackage("org.example.app")
        tryVerify(() => item("addonPackageAppUpdate_org.example.app") !== null)
        item("addonPackageAppUpdate_org.example.app").clicked()
        compare(manager.restores, 1); compare(manager.approvals, 0)
        verify(item("addonInstallReview").visible); verify(item("addonInstallOverview").text.includes("app distribution"))
        item("addonInstallCancel").clicked(); compare(manager.approvals, 0)
        manager.packages = [{id: "org.example.app", name: "App", distributionSource: true, installed: false, suppressed: true}]; manager.revision++
        page.openRecovery(); wait(30)
        tryVerify(() => item("addonRestore_org.example.app") !== null)
        item("addonRestore_org.example.app").clicked(); compare(manager.restores, 2); compare(manager.approvals, 0)
        item("addonInstallApprove").clicked(); compare(manager.approvals, 1)
    }
    function test_fullDeletionAndStaleConfirmationHaveDifferentOutcomes() {
        manager.packages = [{id: "org.example.radio", name: "Radio", version: "2.0.0", enabled: true, configured: true}]; manager.revision++
        selectPackage("org.example.radio")
        packageAction("removeData")
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
        page.advanced = true
        verify(item("addonSafeMode").enabled); item("addonSafeMode").clicked(); verify(manager.safeMode)
    }
    function test_reviewUsesLiveSafeModeInsteadOfItsOldCapabilitySnapshot() {
        page.advanced = true
        manager.safeMode = true
        manager.pendingPackage = {id: "org.example.app", name: "App", permissions: {}, hostCapabilities: {safeMode: true, workerAvailable: true}}
        verify(!item("addonInstallEnable").enabled)
        page.setPaused(false); compare(manager.safeMode, false)
        verify(item("addonInstallEnable").enabled)
        page.enableAfterInstall = true
        page.setPaused(true); compare(manager.safeMode, true)
        item("addonInstallApprove").clicked(); compare(manager.approvals, 1); verify(!manager.lastEnable)
    }
    function test_repositoryStagingNeverInstallsOrGrants() {
        openInstaller()
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
        tryVerify(() => item("addonInstallReview") === null)
    }
    function test_localDevelopmentPathHasSameReviewGate() {
        openInstaller()
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
        selectPackage("org.example.radio")
        packageAction("rollback")
        const dialog = item("addonPackageChangeDialog")
        tryCompare(dialog, "opened", true); compare(manager.rollbacks, 0)
        mouseClick(findChild(dialog, "confirmationAccept"))
        compare(manager.rollbacks, 1); compare(manager.lastPackage, "org.example.radio")
        tryCompare(dialog, "visible", false)
        packageAction("remove"); tryCompare(dialog, "opened", true)
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
        openInstaller()
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
        selectPackage("org.example.radio")
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
        selectPackage("org.example.radio")
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
        selectPackage("org.example.local")
        verify(!item("addonPackageFindUpdates_org.example.local").visible)
        selectPackage("org.example.bundled")
        verify(!item("addonPackageFindUpdates_org.example.bundled").visible)
    }
    Component { id: managementFactory; RedesignAddonManagement {} }
    function test_initialPageDefersInstallSettingsAndRecovery() {
        compare(item("addonRepositorySource"), null)
        compare(item("addonManagementSettingsDialog"), null)
        compare(item("addonManagementRecovery"), null)
        compare(item("addonInstalledList").height, 252)
        const addButton = item("addonAddExtension")
        addButton.forceActiveFocus(); keyClick(Qt.Key_Space); wait(20)
        verify(item("addonInstallDialog").visible)
        compare(item("addonInstallDialog").contentItem.Accessible.name, "Add extension")
        verify(item("addonRepositorySource").visible)
        compare(item("addonManagementSettingsDialog"), null)
        keyClick(Qt.Key_Escape); wait(20)
        verify(addButton.activeFocus)
    }
    function test_searchAndKeyboardKeepActionsBoundToTheSelectedPackage() {
        manager.packages = [{id: "org.example.one", name: "Alpha", version: "1.0", enabled: true},
            {id: "org.example.two", name: "Beta", version: "2.0", enabled: false, configured: false}]; manager.revision++
        const search = item("addonSearch")
        search.text = "beta"; compare(page.filteredPackages.length, 1)
        search.forceActiveFocus(); keyClick(Qt.Key_Down)
        compare(page.selectedId, "org.example.two")
        verify(item("addonSelectedDetails").visible)
        compare(item("addonPackageConfigure_org.example.two").caption, "Set up…")
        verify(page.stateText(page.selectedPackage).includes("Disabled"))
        keyClick(Qt.Key_Return); wait(20)
        const dialog = item("addonManagementSettingsDialog")
        verify(dialog.visible); compare(dialog.packageId, "org.example.two")
        keyClick(Qt.Key_Escape); wait(20)
        compare(item("addonManagementSettingsDialog"), null)
        verify(item("addonPackageList").currentItem.activeFocus)
        search.text = "missing"; compare(page.filteredPackages.length, 0)
        // Search does not change the subject of an already selected action.
        compare(page.selectedId, "org.example.two")
    }
    function test_selectionRemovalClearsContextualActions() {
        manager.packages = [{id: "org.example.one", name: "One", enabled: true}]; manager.revision++
        selectPackage("org.example.one"); verify(item("addonSelectedDetails").visible)
        manager.packages = []; manager.revision++
        verify(!item("addonSelectedDetails").visible)
        verify(!item("addonSelectedSettings").enabled)
        verify(!page.requestPackageChange("org.example.one", "remove"))
    }
    function test_closedAndInactivePagesKeepThePreparedCandidate() {
        stageRepository(); manager.complete(false); wait(20)
        compare(manager.approvals, 0)
        page.active = false; wait(20)
        compare(item("addonInstallDialog"), null)
        verify(!!manager.pendingPackage.id); compare(manager.discards, 0)
        page.active = true; item("addonAddExtension").clicked(); wait(20)
        verify(item("addonInstallReview").visible); compare(manager.approvals, 0)
    }
    function test_pauseAndRemovingRowsRefuseIndividualEnableChanges() {
        manager.packages = [{id: "org.example.one", name: "One", enabled: true, removing: true}]; manager.revision++
        tryVerify(() => item("addonPackageEnabled_org.example.one") !== null)
        verify(!item("addonPackageEnabled_org.example.one").enabled)
        manager.packages = [{id: "org.example.one", name: "One", enabled: true}]; manager.safeMode = true; manager.revision++
        verify(!item("addonPackageEnabled_org.example.one").enabled)
        compare(item("addonPackageEnabled_org.example.one").checked, false)
        compare(page.stateText(manager.packages[0]), "Temporarily disabled")
        page.setPaused(false); verify(item("addonPackageEnabled_org.example.one").enabled)
    }
    function test_anUpdatedAddonShowsItsTrial() {
        manager.packages = [{id: "org.example.one", name: "One", enabled: true}]; manager.revision++
        manager.observing = true; manager.revision++
        compare(page.stateText(manager.packages[0]), "Enabled · New version on trial")
        manager.observing = false; manager.revision++
        compare(page.stateText(manager.packages[0]), "Enabled")
    }
    function test_anAddonWithoutSettingsOffersNoSettingsButton() {
        manager.packages = [{id: "org.example.one", name: "One", enabled: true, hasSettings: false},
                            {id: "org.example.two", name: "Two", enabled: true}]; manager.revision++
        selectPackage("org.example.one")
        verify(!item("addonSelectedSettings").enabled)
        verify(!item("addonPackageConfigure_org.example.one").visible)
        // Rows without the flag keep the button, as before.
        selectPackage("org.example.two")
        verify(item("addonSelectedSettings").enabled)
        verify(item("addonPackageConfigure_org.example.two").visible)
    }
    function test_anEnabledRowShowsRecentWorkerCrashes() {
        manager.packages = [{id: "org.example.one", name: "One", enabled: true}]; manager.revision++
        compare(page.stateText(manager.packages[0]), "Enabled")
        manager.crashes = 2; manager.revision++
        compare(page.stateText(manager.packages[0]), "Enabled · Stopped unexpectedly · 2 time(s)")
        manager.packages = [{id: "org.example.one", name: "One", enabled: false}]; manager.revision++
        compare(page.stateText(manager.packages[0]), "Disabled")
        manager.crashes = 0
    }
    function test_narrowListAndInstallDialogStayWithinTheWindow() {
        const oldWidth = page.width
        page.width = 280
        openInstaller()
        const dialog = item("addonInstallDialog")
        verify(dialog.width <= testCase.width - 32)
        verify(dialog.height <= testCase.height - 32)
        verify(item("addonMoreActions").width + item("addonAddExtension").width <= page.width)
        verify(item("addonInstalledList").width <= page.width)
        page.width = oldWidth
    }
    function test_packageSnapshotIsSharedAndFormsReadOnlyOnDemand() {
        const counts = {packages: 0, settings: 0, capabilities: 0}
        const proxy = {
            get revision() { return manager.revision },
            get packages() { ++counts.packages; return manager.packages },
            get busy() { return manager.busy },
            get safeMode() { return manager.safeMode },
            get pendingPackage() { return manager.pendingPackage },
            get updateResult() { return manager.updateResult },
            get errorMessage() { return manager.errorMessage },
            get capabilities() { ++counts.capabilities; return manager.capabilities },
            credentialPersistenceAvailable: true,
            setSafeMode: value => manager.setSafeMode(value),
            retainedGenerations: id => [],
            settingsSchema: id => { ++counts.settings; return manager.settingsSchema(id) },
            settingsValues: id => { ++counts.settings; return {} },
            settingsSecretStatus: id => { ++counts.settings; return {} }
        }
        manager.packages = [{id: "org.example.one", name: "One", enabled: true, savedData: true}]
        const probe = createTemporaryObject(managementFactory, testCase, {backend: manager, width: 600})
        verify(probe !== null)
        // Assign the JS object after creation. QVariant initial properties copy getters.
        probe.backend = proxy; wait(20)
        compare(counts.packages, 1); compare(counts.settings, 0); compare(counts.capabilities, 0)
        findChild(probe, "addonSearch").text = "one"; probe.selectPackage("org.example.one"); wait(20)
        compare(counts.packages, 1); compare(counts.settings, 0)
        manager.revision++; wait(20); compare(counts.packages, 2)
        probe.openRecovery(); wait(20)
        compare(counts.packages, 2); compare(counts.settings, 0)
        probe.closeDialogs(); wait(20)
        const oldReads = manager.settingsReads
        probe.backend = manager
        probe.configure("org.example.one"); wait(20)
        verify(manager.settingsReads > oldReads); compare(counts.packages, 2)
        probe.closeDialogs(); wait(20)
    }

    function test_sameCandidateRevisionDoesNotReopenReviewOrResetChoice() {
        stageRepository(); manager.complete(false); wait(20)
        page.enableAfterInstall = true
        page.closeDialogs(); wait(20)
        compare(item("addonInstallDialog"), null)
        manager.safeMode = true
        manager.pendingPackage = Object.assign({}, manager.pendingPackage, {hostCapabilities: {safeMode: true, workerAvailable: true}})
        manager.revision++; wait(20)
        compare(item("addonInstallDialog"), null)
        verify(page.enableAfterInstall)
        item("addonAddExtension").clicked(); wait(20)
        compare(item("addonInstallDialog").title, "Review extension")
        verify(!item("addonInstallEnable").enabled)
        manager.pendingPackage = Object.assign({}, manager.pendingPackage, {version: "2.0.0", digest: "new-digest"})
        wait(20); verify(!page.enableAfterInstall)
        verify(item("addonInstallDialog").visible)
    }

    function summaryRows(rows) {
        summary.rows = rows; summary.available = true; summary.current = true
        page.summaryBackend = summary; wait(20)
    }
    function test_summaryLoadingAndErrorNeverReadSynchronousPackages() {
        const counts = {packages: 0}
        const proxy = {
            get revision() { return manager.revision },
            get packages() { ++counts.packages; return manager.packages },
            get busy() { return manager.busy },
            get safeMode() { return manager.safeMode },
            get pendingPackage() { return manager.pendingPackage },
            get updateResult() { return manager.updateResult },
            get errorMessage() { return manager.errorMessage },
            setSafeMode: value => manager.setSafeMode(value)
        }
        const probe = createTemporaryObject(managementFactory, testCase, {backend: manager, summaryBackend: summary, width: 600})
        verify(probe !== null); probe.backend = proxy; wait(20)
        compare(counts.packages, 0)
        summary.loading = true; wait(20)
        verify(findChild(probe, "addonListState").text.includes("reads"))
        verify(!findChild(probe, "addonListRetry").visible)
        summary.loading = false; summary.error = "<b>Store unavailable</b>"; wait(20)
        compare(findChild(probe, "addonListState").text, summary.error)
        compare(findChild(probe, "addonListState").textFormat, Text.PlainText)
        verify(findChild(probe, "addonListRetry").visible)
        const before = summary.refreshes
        findChild(probe, "addonListRetry").clicked(); compare(summary.refreshes, before + 1)
        summary.error = ""; summary.available = true; summary.current = true
        summary.rows = [{id: "org.example.one", name: "One", enabled: true}]; wait(20)
        compare(probe.packages.length, 1)
        findChild(probe, "addonSearch").text = "one"; probe.selectPackage("org.example.one"); manager.revision++; wait(20)
        compare(counts.packages, 0)
        const activated = summary.refreshes
        probe.active = false; probe.active = true; compare(summary.refreshes, activated + 1)
        probe.closeDialogs()
    }
    function test_summaryGuardRefusesStaleRowActionsBeforeNativeCalls() {
        const row = {id: "org.example.radio", name: "Radio", version: "1.0.0", enabled: true,
            source: "https://git.example.org/radio.git", appUpdateAvailable: true, hasPrevious: true}
        summaryRows([row]); selectPackage(row.id)
        summary.current = false
        verify(!page.setPackageEnabled(row.id, false)); compare(manager.enableRequests, 0)
        verify(!page.findUpdates(row.id)); compare(manager.updateRequests, 0)
        verify(!page.reviewAppUpdate(row.id)); compare(manager.restores, 0)
        const settingsReads = manager.settingsReads
        page.configure(row.id); compare(item("addonManagementSettingsDialog"), null); compare(manager.settingsReads, settingsReads)
        verify(!page.requestPackageChange(row.id, "remove")); compare(manager.removals, 0)
        verify(page.actionError.includes("list changed")); verify(summary.currentCalls >= 5)
        page.setPaused(true); verify(manager.safeMode)
        item("addonAddExtension").clicked(); wait(20); verify(item("addonInstallDialog").visible)
    }
    function test_summaryGuardRunsAgainAtRemovalConfirmation() {
        summaryRows([{id: "org.example.one", name: "One", version: "1.0.0", enabled: true}])
        verify(page.requestPackageChange("org.example.one", "remove"))
        const dialog = item("addonPackageChangeDialog")
        summary.current = false
        dialog.accept(); compare(manager.removals, 0)
        verify(page.actionError.includes("list changed"))
        verify(summary.refreshes >= 2)
    }
    function test_recoverySnapshotUsesTheSameRequestAndConfirmationGuard() {
        summaryRows([{id: "org.example.one", name: "One", recoveryRequired: true, installed: false, savedData: true}])
        page.openRecovery(); wait(20)
        const recovery = item("addonManagementRecovery")
        summary.current = false
        verify(!recovery.request("org.example.one", "recover")); compare(manager.recoveries, 0)
        verify(recovery.actionError.includes("list changed"))
        summary.current = true
        verify(recovery.request("org.example.one", "removeData"))
        const confirmation = item("addonRecoveryConfirmation")
        summary.current = false
        confirmation.accept(); compare(manager.removals, 0)
        verify(recovery.actionError.includes("list changed"))
        findChild(recovery, "addonSafeMode").clicked(); verify(manager.safeMode)
    }
    function test_summaryRefreshClosesSettingsWithoutChangingThePreparedCandidate() {
        summaryRows([{id: "org.example.one", name: "One", configured: true, enabled: true}])
        page.configure("org.example.one"); wait(20); verify(item("addonManagementSettingsDialog").visible)
        summary.available = false; summary.loading = true; wait(20)
        compare(item("addonManagementSettingsDialog"), null)
        verify(page.actionError.includes("Open settings again"))
        manager.pendingPackage = {id: "org.example.new", name: "New", version: "1.0.0", permissions: {}}
        wait(20); verify(item("addonInstallDialog").visible)
        verify(item("addonInstallApprove").enabled)
        item("addonInstallApprove").clicked(); compare(manager.approvals, 1)
    }

    function test_settingsSaveCanSynchronouslyInvalidateSummaryWithoutDestroyingItsForm() {
        summaryRows([{id: "org.example.one", name: "One", configured: true, enabled: true}])
        page.configure("org.example.one"); wait(20)
        const form = findChild(item("addonManagementSettingsDialog"), "addonSettingsDialogForm")
        verify(form !== null)
        manager.invalidateSummaryDuringSave = true
        verify(form.save())
        wait(30)
        compare(manager.settingsSaves, 1)
        compare(item("addonManagementSettingsDialog"), null)
        compare(page.actionError, "")
        verify(summary.loading)
    }
    function test_asyncRemovalKeepsItsOperationStateWhenSummaryRowsClear() {
        summaryRows([{id: "org.example.one", name: "One", configured: true, enabled: true}])
        manager.asyncRemoval = true
        verify(page.requestPackageChange("org.example.one", "remove"))
        item("addonPackageChangeDialog").accept(); wait(20)
        compare(manager.removals, 1); verify(manager.busy); compare(page.packages.length, 0)
        verify(page.removalInProgress); verify(!item("addonRepositoryCancel").visible)
        page.setPaused(true); verify(manager.safeMode)
        manager.busy = false; verify(!page.removalInProgress)
        manager.busy = true; verify(item("addonRepositoryCancel").visible)
    }
    function test_recoveryRemovalStateSurvivesClosingItsDialog() {
        summaryRows([{id: "org.example.one", name: "One", installed: false, savedData: true}])
        manager.asyncRemoval = true
        page.openRecovery(); wait(20)
        verify(item("addonManagementRecovery").request("org.example.one", "removeData"))
        item("addonRecoveryConfirmation").accept(); wait(20)
        verify(page.removalInProgress)
        page.closeDialogs(); wait(20)
        verify(page.removalInProgress); verify(!item("addonRepositoryCancel").visible)
        manager.busy = false; verify(!page.removalInProgress)
    }


    function test_displaySummaryKeepsBasicReadsInMemory() {
        summary.displayBacked = true
        summary.loading = true
        const counts = {packages: 0, safeMode: 0}
        const proxy = {
            get revision() { return manager.revision },
            get packages() { ++counts.packages; return manager.packages },
            get safeMode() { ++counts.safeMode; return manager.safeMode },
            get busy() { return manager.busy },
            get pendingPackage() { return manager.pendingPackage },
            get updateResult() { return manager.updateResult },
            get errorMessage() { return manager.errorMessage },
            setSafeMode: value => manager.setSafeMode(value)
        }
        const probe = createTemporaryObject(managementFactory, testCase,
            {backend: manager, summaryBackend: summary, width: 600})
        verify(probe !== null)
        probe.backend = proxy
        verify(!probe.safeModeKnown); verify(!probe.safeMode)
        for (let i = 0; i < 20; ++i) {
            manager.revision++
            probe.active = false; probe.active = true
            compare(probe.packages.length, 0); verify(!probe.safeMode)
        }
        summary.safeModeKnown = true; summary.safeMode = true
        summary.rows = [{id: "org.example.one", name: "One", enabled: true}]
        summary.available = true; summary.current = true; summary.loading = false
        tryCompare(probe, "safeMode", true)
        compare(probe.packages.length, 1)
        probe.openRecovery()
        const recovery = findChild(probe, "addonManagementRecovery")
        tryVerify(() => recovery !== null && recovery.visible)
        compare(recovery.safeMode, true)
        manager.revision++
        probe.closeDialogs()
        probe.summaryBackend = null
        manager.revision++
        verify(probe.displayBacked); verify(!probe.listReady); verify(!probe.safeModeKnown)
        verify(probe.listError.length > 0)
        compare(probe.packages.length, 0)
        verify(!probe.setPackageEnabled("org.example.one", false))
        compare(counts.packages, 0)
        compare(counts.safeMode, 0)
    }
    function test_displaySummaryRetainsRowsWhileActionsWait() {
        summary.displayBacked = true; summary.safeModeKnown = true
        const row = {id: "org.example.one", name: "One", version: "1.0.0", enabled: true, configured: true}
        summary.rows = [row]; summary.available = true; summary.current = true
        const probe = createTemporaryObject(managementFactory, testCase,
            {backend: manager, summaryBackend: summary, width: 600})
        verify(probe !== null); probe.selectPackage(row.id)
        summary.available = false; summary.current = false; summary.loading = true
        wait(20)
        compare(probe.packages.length, 1); compare(probe.selectedPackage.id, row.id)
        verify(!probe.setPackageEnabled(row.id, false)); compare(manager.enableRequests, 0)
        verify(!findChild(probe, "addonPackageConfigure_" + row.id).enabled)
        summary.available = true; summary.current = true; summary.loading = false
        verify(probe.setPackageEnabled(row.id, false)); compare(manager.enableRequests, 1)
    }


    function test_unknownConfigurationDoesNotClaimSetupIsRequired() {
        summary.displayBacked = true; summary.safeModeKnown = true
        const row = {id: "org.example.one", name: "One", configured: false,
            configurationKnown: false, enabled: true}
        summary.rows = [row]; summary.available = true; summary.current = true
        const probe = createTemporaryObject(managementFactory, testCase,
            {backend: manager, summaryBackend: summary, width: 600})
        verify(probe !== null); probe.selectPackage(row.id)
        const button = findChild(probe, "addonPackageConfigure_" + row.id)
        verify(button !== null); verify(button.enabled)
        compare(probe.stateText(row), "Enabled"); compare(button.caption, "Settings…")
        compare(probe.stateText(Object.assign({}, row, {enabled: false})), "Disabled")
        const known = Object.assign({}, row, {configurationKnown: true})
        summary.rows = [known]; wait(20)
        compare(probe.stateText(known), "Set up first"); compare(button.caption, "Set up…")
        compare(manager.settingsSaves, 0); compare(manager.enableRequests, 0)
    }

    function test_attachedRefreshKeepsTheOpenConfigurationDraft() {
        summary.displayBacked = true; summary.safeModeKnown = true
        summary.rows = [{id: "org.example.one", name: "One", configured: true, enabled: true}]
        summary.available = true; summary.current = true; manager.draftSchema = true
        const probe = createTemporaryObject(managementFactory, testCase,
            {backend: manager, summaryBackend: summary, width: 600})
        verify(probe !== null); probe.configure("org.example.one")
        const dialog = findChild(probe, "addonManagementSettingsDialog")
        tryVerify(() => dialog !== null && dialog.visible)
        dialog.form.setValue("nickname", "Keep this draft")
        summary.available = false; summary.current = false; summary.loading = true
        summary.rows = summary.rows.concat([{id: "org.example.other", name: "Other", configured: true, enabled: true}])
        manager.revision++; wait(30)
        verify(dialog.visible); compare(dialog.form.draftValues.nickname, "Keep this draft")
        compare(manager.settingsSaves, 0)
        summary.available = true; summary.current = true; summary.loading = false
        wait(20)
        verify(dialog.visible); compare(dialog.form.draftValues.nickname, "Keep this draft")
        compare(manager.settingsSaves, 0)
        probe.closeDialogs()
    }

}

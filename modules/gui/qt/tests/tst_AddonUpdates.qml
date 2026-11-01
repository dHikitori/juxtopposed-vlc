// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "AddonUpdates"
    width: 800; height: 700; visible: true; when: windowShown
    QtObject {
        id: manager
        property int revision: 0
        property bool busy: false
        property string errorMessage: ""
        property var pendingPackage: ({})
        property var appUpdates: []
        property string staged: ""
        property var stages: []
        property var approvals: []
        property bool refuseStage: false
        function stageDistributionPackage(id) {
            if (refuseStage) { errorMessage = "The app package changed."; revision++; return false }
            stages = stages.concat([id]); staged = id; busy = true; pendingPackage = ({}); errorMessage = ""; revision++; return true
        }
        // The staged copy arrives later, like the real background read.
        function complete() { busy = false; pendingPackage = {id: staged}; revision++ }
        // Store writes answer later, as the app's do.
        signal writeFinished(string request, bool success, string error)
        property int requests: 0
        function finishLater(success) {
            const request = String(++requests)
            Qt.callLater(() => writeFinished(request, success, success ? "" : errorMessage))
            return request
        }
        function requestApprovePending(enable) {
            approvals = approvals.concat([{id: pendingPackage.id, enable: enable}])
            const id = pendingPackage.id; pendingPackage = ({})
            appUpdates = appUpdates.filter(update => update.id !== id); appUpdatesChanged()
            revision++; return finishLater(true)
        }
    }
    QtObject {
        id: settings
        property var values: ({})
        function settingValue(key, fallback) { return values[key] !== undefined ? values[key] : fallback }
        function setSettingValue(key, value) { const next = Object.assign({}, values); next[key] = value; values = next }
    }
    Component {
        id: promptComponent
        RedesignAddonUpdates { parent: testCase; ctx: settings }
    }
    function updates() {
        return [{id: "org.videolan.episodes", name: "Episodes", installedVersion: "1.0.2", version: "1.0.3", enabled: true, newPermissions: {}},
                {id: "org.videolan.tvmaze", name: "TVMaze", installedVersion: "0.3.0", version: "0.4.0", enabled: false,
                 newPermissions: {"network.http": {origins: ["https://api.tvmaze.com"]}}}]
    }
    function init() {
        manager.appUpdates = updates(); manager.stages = []; manager.approvals = []; manager.pendingPackage = ({})
        manager.busy = false; manager.refuseStage = false; manager.errorMessage = ""; settings.values = ({})
    }
    // A click after the dialog has laid out, as a person would make it.
    function click(item, name) {
        const button = findChild(item.contentItem, name)
        tryVerify(() => button.visible && button.enabled)
        waitForRendering(item.contentItem)
        mouseClick(button)
    }
    function prompt() {
        const item = createTemporaryObject(promptComponent, testCase)
        item.backend = manager
        return item
    }
    function test_updatesWithoutNewPermissionsInstallTogetherOthersNeedApproval() {
        const item = prompt()
        tryVerify(() => item.opened)
        compare(findChild(item.contentItem, "addonUpdateVersions_org.videolan.episodes").text, "1.0.2 → 1.0.3")
        verify(!findChild(item.contentItem, "addonUpdatePermissions_org.videolan.episodes").visible)
        const permissions = findChild(item.contentItem, "addonUpdatePermissions_org.videolan.tvmaze")
        verify(permissions.visible); verify(permissions.text.includes("api.tvmaze.com"))
        compare(findChild(item.contentItem, "addonUpdateApply_org.videolan.tvmaze").caption, "Approve and update")
        // "Update the others" leaves the update with new permissions alone.
        compare(findChild(item.contentItem, "addonUpdatesAll").caption, "Update the others")
        click(item, "addonUpdatesAll")
        compare(manager.stages, ["org.videolan.episodes"])
        compare(findChild(item.contentItem, "addonUpdateStatus_org.videolan.episodes").text, "Updating…")
        manager.complete()
        compare(manager.approvals.length, 1)
        compare(manager.approvals[0].id, "org.videolan.episodes"); compare(manager.approvals[0].enable, true)
        tryCompare(findChild(item.contentItem, "addonUpdateStatus_org.videolan.episodes"), "text", "Updated")
        verify(!findChild(item.contentItem, "addonUpdatesAll").visible)
        // Its own approval; the add-on stays off as it was.
        click(item, "addonUpdateApply_org.videolan.tvmaze")
        compare(manager.stages[1], "org.videolan.tvmaze")
        manager.complete()
        compare(manager.approvals[1].id, "org.videolan.tvmaze"); compare(manager.approvals[1].enable, false)
        tryVerify(() => item.finished)
        verify(findChild(item.contentItem, "addonUpdatesDone").visible)
        click(item, "addonUpdatesDone")
        tryVerify(() => !item.opened)
        compare(settings.values["Redesign/AddonUpdatesDismissed"], undefined)
    }
    function test_laterWaitsForANewerVersion() {
        let item = prompt()
        tryVerify(() => item.opened)
        click(item, "addonUpdatesLater")
        tryVerify(() => !item.opened)
        compare(JSON.parse(settings.values["Redesign/AddonUpdatesDismissed"]), {"org.videolan.episodes": "1.0.3", "org.videolan.tvmaze": "0.4.0"})
        compare(manager.stages.length, 0)
        item = prompt(); wait(50)
        verify(!item.opened)
        // A newer version found later asks again.
        const newer = updates(); newer[0].version = "1.0.4"; manager.appUpdates = newer
        tryVerify(() => item.opened)
        compare(item.rows.length, 1)
        compare(item.rows[0].version, "1.0.4")
    }
    function test_waitsForTheFirstRunWindowAndUpdatesFoundLater() {
        manager.appUpdates = []
        const item = createTemporaryObject(promptComponent, testCase, {blocked: true})
        item.backend = manager; wait(50)
        verify(!item.opened)
        manager.appUpdates = updates(); manager.appUpdatesChanged(); wait(50)
        verify(!item.opened)
        item.blocked = false
        tryVerify(() => item.opened)
    }
    function test_aFailedUpdateSaysWhyAndCanBeRetried() {
        const item = prompt()
        tryVerify(() => item.opened)
        manager.refuseStage = true
        click(item, "addonUpdateApply_org.videolan.episodes")
        tryCompare(findChild(item.contentItem, "addonUpdateStatus_org.videolan.episodes"), "text", "The app package changed.")
        verify(findChild(item.contentItem, "addonUpdateApply_org.videolan.episodes").visible)
        manager.refuseStage = false
        click(item, "addonUpdateApply_org.videolan.episodes")
        manager.complete()
        tryCompare(findChild(item.contentItem, "addonUpdateStatus_org.videolan.episodes"), "text", "Updated")
    }
}

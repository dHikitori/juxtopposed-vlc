// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: test
    name: "AddonRecovery"
    width: 800; height: 650; visible: true; when: windowShown
    QtObject {
        id: manager
        property int revision: 0
        property bool safeMode: false
        property bool busy: false
        property var packages: []
        property var calls: []
        property string failure: ""
        property string errorMessage: ""
        function success(operation) { if (failure !== operation) return true; errorMessage = "<b>Operation failed</b>"; return false }
        function setSafeMode(enabled) { calls = calls.concat([{operation: "safeMode", value: enabled}]); if (!success("safeMode")) return false; safeMode = enabled; ++revision; return true }
        function recoverPackage(id) { calls = calls.concat([{operation: "recover", id: id}]); return success("recover") }
        function restoreDistributionPackage(id) { calls = calls.concat([{operation: "restore", id: id}]); return success("restore") }
        function retainedGenerations(id) { return [{version: "1.0.0"}] }
    }
    Component { id: recoveryComponent; RedesignAddonRecovery { width: 700; backend: manager } }
    function init() {
        manager.safeMode = false; manager.busy = false; manager.calls = []; manager.failure = ""; manager.errorMessage = ""
        manager.packages = [{id: "org.example.failed", name: "<b>Package</b>", recoveryRequired: true, error: "<b>Failure</b>"},
            {id: "org.example.app", name: "App package", suppressed: true, installed: false, distributionSource: true},
            {id: "org.example.other", name: "Other", installed: true}]
        ++manager.revision
    }
    function test_safeModeIsAnExplicitReversibleAction() {
        const view = createTemporaryObject(recoveryComponent, test)
        compare(manager.calls.length, 0)
        findChild(view, "addonSafeMode").clicked()
        compare(manager.safeMode, true); compare(manager.calls[0].operation, "safeMode")
        findChild(view, "addonSafeMode").clicked(); compare(manager.safeMode, false)
        compare(manager.packages.length, 3)
    }
    function test_recoveryNeedsCoreConfirmationAndDoesNotEnable() {
        const view = createTemporaryObject(recoveryComponent, test)
        findChild(view, "addonRecover_org.example.failed").clicked()
        const confirmation = findChild(view, "addonRecoveryConfirmation")
        tryVerify(() => confirmation.visible); compare(manager.calls.length, 0)
        verify(confirmation.message.includes("stay disabled")); confirmation.accept()
        compare(manager.calls.length, 1); compare(manager.calls[0].operation, "recover"); compare(manager.calls[0].id, "org.example.failed")
    }
    function test_cancelKeepsTheRemovedAppPackageRemoved() {
        const view = createTemporaryObject(recoveryComponent, test)
        verify(view.request("org.example.app", "restore"))
        const confirmation = findChild(view, "addonRecoveryConfirmation")
        tryVerify(() => confirmation.visible); verify(confirmation.message.includes("enable"))
        confirmation.reject(); compare(manager.calls.length, 0)
    }
    function test_restoreOnlyUsesHostRegisteredAppPackage() {
        const view = createTemporaryObject(recoveryComponent, test)
        verify(!view.request("org.example.other", "restore")); verify(!view.request("org.example.unknown", "restore"))
        verify(view.request("org.example.app", "restore"))
        findChild(view, "addonRecoveryConfirmation").accept()
        compare(manager.calls.length, 1); compare(manager.calls[0].operation, "restore"); compare(manager.calls[0].id, "org.example.app")
    }
    function test_changedAndBusyPackageCannotApplyAnOldConfirmation() {
        const view = createTemporaryObject(recoveryComponent, test)
        verify(view.request("org.example.failed", "recover")); manager.packages = []; ++manager.revision
        findChild(view, "addonRecoveryConfirmation").accept(); compare(manager.calls.length, 0)
        manager.busy = true; verify(!view.request("org.example.failed", "recover"))
    }
    function test_changedRecoveryStateCannotRestoreThroughAnOldAction() {
        const view = createTemporaryObject(recoveryComponent, test)
        verify(view.request("org.example.app", "restore"))
        manager.packages = [{id: "org.example.app", recoveryRequired: true, installed: true, distributionSource: true}]; ++manager.revision
        findChild(view, "addonRecoveryConfirmation").accept(); compare(manager.calls.length, 0)
    }
    function test_failedActionsShowTheCoreError_data() {
        return [{tag: "safeMode", operation: "safeMode"}, {tag: "recover", operation: "recover"}, {tag: "restore", operation: "restore"}]
    }
    function test_failedActionsShowTheCoreError(data) {
        const view = createTemporaryObject(recoveryComponent, test); manager.failure = data.operation
        if (data.operation === "safeMode") {
            findChild(view, "addonSafeMode").clicked(); compare(manager.safeMode, false)
            compare(findChild(view, "addonSafeMode").caption, "Temporarily disable add-ons")
        } else {
            verify(view.request(data.operation === "restore" ? "org.example.app" : "org.example.failed", data.operation))
            findChild(view, "addonRecoveryConfirmation").accept()
        }
        const error = findChild(view, "addonRecoveryError")
        verify(error.visible); compare(error.text, "<b>Operation failed</b>"); compare(error.textFormat, Text.PlainText)
    }
}

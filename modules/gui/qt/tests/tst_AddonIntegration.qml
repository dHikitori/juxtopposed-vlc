// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "AddonIntegration"
    width: 900; height: 1000; visible: true; when: windowShown
    QtObject {
        id: service
        property bool configured: true
        property bool hasConsumerKey: true
        property bool hasCustomKey: false
        property bool preparing: false
        function setApiKey(value) { hasCustomKey = true; return true }
        function clearApiKey() { hasCustomKey = false; return true }
    }
    QtObject {
        id: manager
        property int revision: 0
        property bool credentialPersistenceAvailable: true
        property string errorMessage: ""
        property var packages: [{id: "org.example.first", name: "First", version: "1.0.0", enabled: true, configured: true},
            {id: "org.example.second", name: "Second", version: "2.0.0", enabled: false, configured: false}]
        property var order: ["org.example.first/search", "org.example.second/subtitles"]
        property var values: ({language: "en"})
        property var secrets: ({apiKey: {present: true}})
        property bool firstBuiltin: false
        property int orderCalls: 0
        property int enableCalls: 0
        property int saveCalls: 0
        property string lastPackage: ""
        property string lastFeature: ""
        function providersForFeature(feature) {
            return order.map(id => {
                const pkg = packages.find(row => id.startsWith(row.id + "/"))
                return {id: id, packageId: pkg.id, name: pkg.name, enabled: pkg.enabled, configured: pkg.configured, builtin: firstBuiltin && pkg.id === "org.example.first"}
            })
        }
        function setProviderOrder(feature, ids) { orderCalls++; lastFeature = feature; order = ids; revision++; return true }
        function setEnabled(id, enabled) {
            enableCalls++; lastPackage = id
            packages = packages.map(row => row.id === id ? Object.assign({}, row, {enabled: enabled}) : row)
            revision++; return true
        }
        function settingsSchema(id) {
            return {type: "object", additionalProperties: false, properties: {
                language: {type: "string", default: "en", minLength: 2, title: "Language"},
                apiKey: {type: "string", "x-hakone-secret": true, minLength: 1, "x-hakone-label": "settings.apiKey"}}, required: ["apiKey"]}
        }
        function settingsMessages(id) { return {"settings.apiKey": "API credential"} }
        function settingsValues(id) { return values }
        function settingsSecretStatus(id) { return secrets }
        function saveAddonSettings(id, next, operations) { saveCalls++; lastPackage = id; values = next; revision++; return true }
    }
    RedesignSubtitleSourcePreferences {
        id: subtitlePreferences
        width: 420
        sourceService: service; addonManager: manager
    }
    RedesignAddonManagement { id: management; x: 440; width: 420; backend: manager }
    function init() {
        manager.order = ["org.example.first/search", "org.example.second/subtitles"]
        manager.packages = [{id: "org.example.first", name: "First", version: "1.0.0", enabled: true, configured: true},
            {id: "org.example.second", name: "Second", version: "2.0.0", enabled: false, configured: false}]
        manager.firstBuiltin = false
        manager.values = {language: "en"}; manager.orderCalls = 0; manager.enableCalls = 0; manager.saveCalls = 0
        service.hasCustomKey = false; subtitlePreferences.addonManager = manager
        manager.revision++
        wait(25)
    }
    function cleanup() {
        for (const name of ["subtitleAddonSettingsDialog", "addonManagementSettingsDialog", "subtitlePreferencesProviderSetup"]) {
            const dialog = findChild(testCase, name)
            if (dialog && dialog.visible) dialog.close()
        }
        wait(20)
    }
    function test_standaloneSubtitlePreferencesCanOmitManager() {
        subtitlePreferences.addonManager = null; wait(10)
        verify(!findChild(subtitlePreferences, "addonProvider_org.example.first/search"))
        verify(findChild(subtitlePreferences, "subtitleProviderConfigure").enabled)
        compare(manager.saveCalls, 0)
    }
    function test_rankedProvidersPersistContributionIdentity() {
        mouseClick(findChild(subtitlePreferences, "addonProviderDown_org.example.first/search")); wait(100)
        compare(manager.orderCalls, 1); compare(manager.lastFeature, "subtitles")
        compare(manager.order[0], "org.example.second/subtitles")
        const enabled = findChild(subtitlePreferences, "addonProviderEnabled_org.example.second/subtitles")
        const toggle = findChild(enabled, "settingSwitch")
        tryVerify(() => toggle.visible && toggle.width > 0 && toggle.height > 0)
        verify(!toggle.checked)
        mouseClick(toggle); wait(10)
        compare(manager.enableCalls, 1); compare(manager.lastPackage, "org.example.second")
        verify(manager.packages[1].enabled)
    }
    function test_providerConfigurationUsesGenericFormAndKeepsDrafts() {
        mouseClick(findChild(subtitlePreferences, "addonProviderConfigure_org.example.second/subtitles"))
        const dialog = findChild(testCase, "subtitleAddonSettingsDialog")
        tryCompare(dialog, "opened", true)
        compare(dialog.packageId, "org.example.second")
        tryVerify(() => findChild(dialog.form, "addonField_apiKey") !== null)
        compare(findChild(dialog.form, "addonField_apiKey").label, "API credential")
        dialog.form.setValue("language", "fr")
        compare(manager.values.language, "en")
        mouseClick(findChild(dialog, "addonSettingsSave"))
        tryCompare(dialog, "visible", false)
        compare(manager.saveCalls, 1); compare(manager.lastPackage, "org.example.second")
        compare(manager.values.language, "fr")
    }
    function test_managementEscapeCancelsAndClearsProtectedInput() {
        mouseClick(findChild(management, "addonPackageConfigure_org.example.first"))
        const dialog = findChild(testCase, "addonManagementSettingsDialog")
        tryCompare(dialog, "opened", true)
        compare(dialog.form.packageId, "org.example.first")
        compare(dialog.form.schemaError, "")
        compare(Object.keys(dialog.form.schema.properties).sort(), ["apiKey", "language"])
        tryVerify(() => findChild(dialog.form, "addonSecretReplace_apiKey") !== null)
        mouseClick(findChild(dialog.form, "addonSecretReplace_apiKey")); wait(10)
        const input = findChild(dialog.form, "addonSecretInput_apiKey")
        input.forceActiveFocus(); keyClick(Qt.Key_X); keyClick(Qt.Key_Y)
        compare(dialog.form.secretOperations.apiKey.value, "xy")
        keyClick(Qt.Key_Escape)
        tryCompare(dialog, "visible", false)
        compare(input.text, ""); compare(Object.keys(dialog.form.secretOperations).length, 0)
        compare(manager.saveCalls, 0)
    }
    function test_builtinProviderRowOpensTheHostSetup() {
        manager.firstBuiltin = true; manager.revision++; wait(25)
        mouseClick(findChild(subtitlePreferences, "addonProviderConfigure_org.example.first/search"))
        const builtin = findChild(subtitlePreferences, "subtitlePreferencesProviderSetup")
        tryCompare(builtin, "opened", true)
        const generic = findChild(testCase, "subtitleAddonSettingsDialog")
        verify(!generic.visible)
        compare(manager.saveCalls, 0)
    }
    function test_builtinProviderSetupRemainsIndependent() {
        mouseClick(findChild(subtitlePreferences, "subtitleProviderConfigure"))
        const dialog = findChild(subtitlePreferences, "subtitlePreferencesProviderSetup")
        tryCompare(dialog, "opened", true)
        findChild(dialog, "subtitleProviderApiKey").text = "builtin-test-key"
        mouseClick(findChild(dialog, "subtitleProviderSaveKey"))
        tryCompare(dialog, "visible", false)
        verify(service.hasCustomKey)
        compare(manager.saveCalls, 0); compare(manager.enableCalls, 0); compare(manager.orderCalls, 0)
    }
}

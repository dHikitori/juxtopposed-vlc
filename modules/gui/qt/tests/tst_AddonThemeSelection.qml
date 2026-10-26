// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    name: "AddonThemeSelection"
    width: 600; height: 500; visible: true; when: windowShown
    QtObject {
        id: service
        property int revision: 0
        property var availableThemes: [{id: "org.example.theme/midnight", name: "<b>Midnight</b>", light: false, dark: true}]
        property string selectedTheme: ""
        property string errorMessage: ""
        property bool usingFallback: false
        property bool highContrast: false
        property bool platformHighContrast: false
        property bool reducedMotion: false
        property bool rejectChange: false
        property int selections: 0
        function selectTheme(id) { selections++; if (rejectChange) return false; selectedTheme = id; revision++; return true }
        function setHighContrast(enabled) { if (rejectChange) return false; highContrast = enabled || platformHighContrast; revision++; return true }
        function setReducedMotion(enabled) { if (rejectChange) return false; reducedMotion = enabled; revision++; return true }
    }
    RedesignAddonThemeSelector { id: page; width: 420; backend: service }
    function item(name) { return findChild(page, name) }
    function init() {
        service.selectedTheme = ""; service.errorMessage = ""; service.usingFallback = false
        service.highContrast = false; service.platformHighContrast = false; service.rejectChange = false; service.selections = 0
        service.reducedMotion = false
        page.lightTheme = false; service.revision++; wait(20)
    }
    function test_selectionIsExplicitAndLabelsArePlain() {
        compare(service.selections, 0)
        const select = item("addonThemeSelection")
        compare(select.currentIndex, 0)
        select.activated(1)
        compare(service.selectedTheme, "org.example.theme/midnight")
        compare(select.contentItem.textFormat, Text.PlainText)
        compare(select.contentItem.text, "<b>Midnight</b>")
        mouseClick(item("addonThemeRestoreDefault"))
        compare(service.selectedTheme, ""); compare(service.selections, 2)
    }
    function test_missingVariantAndRecoveryStayVisible() {
        service.selectedTheme = "org.example.theme/midnight"; page.lightTheme = true
        verify(item("addonThemeFallbackStatus").visible)
        verify(item("addonThemeFallbackStatus").text.includes("built-in"))
        service.usingFallback = true; service.errorMessage = "The selected theme is unavailable."
        compare(item("addonThemeFallbackStatus").text, service.errorMessage)
        verify(item("addonThemeRestoreDefault").visible)
    }
    function test_userAndPlatformHighContrast() {
        const contrast = item("addonThemeHighContrast")
        mouseClick(contrast); verify(service.highContrast)
        service.rejectChange = true
        mouseClick(contrast); verify(service.highContrast); verify(contrast.checked)
        service.platformHighContrast = true
        verify(!contrast.enabled)
    }
    function test_userCanReduceVisualizerMotion() {
        const motion = item("addonThemeReducedMotion")
        mouseClick(motion); verify(service.reducedMotion)
        service.rejectChange = true
        mouseClick(motion); verify(service.reducedMotion); verify(motion.checked)
    }
}

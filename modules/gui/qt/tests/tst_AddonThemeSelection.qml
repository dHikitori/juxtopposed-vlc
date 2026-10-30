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
        select.forceActiveFocus(Qt.TabFocusReason)
        item("addonThemeRestoreDefault").forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Space)
        compare(service.selectedTheme, ""); compare(service.selections, 2)
    }
    function test_resetIsHiddenAfterAMouseChoice() {
        mouseMove(page, page.width + 50, page.height + 50); wait(20)
        page.forceActiveFocus()
        item("addonThemeSelection").forceActiveFocus(Qt.MouseFocusReason)
        verify(!item("addonThemeRestoreDefault").visible)
        page.forceActiveFocus()
        item("addonThemeSelection").forceActiveFocus(Qt.TabFocusReason)
        verify(item("addonThemeRestoreDefault").visible)
    }
    function test_missingVariantAndRecoveryStayVisible() {
        service.selectedTheme = "org.example.theme/midnight"; page.lightTheme = true
        verify(item("addonThemeFallbackStatus").visible)
        verify(item("addonThemeFallbackStatus").text.includes("built-in"))
        service.usingFallback = true; service.errorMessage = "The selected theme is unavailable."
        compare(item("addonThemeFallbackStatus").text, service.errorMessage)
        item("addonThemeSelection").forceActiveFocus(Qt.TabFocusReason)
        verify(item("addonThemeRestoreDefault").visible)
    }
    function test_keyboardSelectionAndRefusal() {
        const select = item("addonThemeSelection")
        select.forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Down)
        compare(service.selectedTheme, "org.example.theme/midnight")
        compare(select.currentIndex, 1)
        service.rejectChange = true
        keyClick(Qt.Key_Up)
        compare(service.selectedTheme, "org.example.theme/midnight")
        compare(select.currentIndex, 1)
    }
}

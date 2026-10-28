// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "GeneralAppearance"
    width: 1000; height: 780; visible: true; when: windowShown
    QtObject {
        id: store
        property int revision: 0
        property string accent: "#185dd3"
        property string lastOption: ""
        property var lastValue
        property int saves: 0
        function describe(name) {
            if (name === "language") return {kind: "string", choices: [{value: "auto", label: "System language"}, {value: "en", label: "English"}]}
            return {kind: "bool"}
        }
        function value(name) { return name === "language" ? "auto" : false }
        function stage(name, value) { lastOption = name; lastValue = value; if (name === "qt-accent-color") accent = value === "default" ? "#9ac6ff" : value; revision++; return true }
        function save() { saves++ }
        function reset(name) { return stage(name, "default") }
        function chooseColor(initial) { return "#123456" }
    }
    QtObject {
        id: schemes
        property int scheme: 2
        function index(value, column) { return value }
        function setData(index, value, role) { scheme = index; return true }
    }
    QtObject {
        id: redesignThemeService
        property int revision: store.revision
        property var availableThemes: [{id: "midnight", name: "Midnight", light: true, dark: true}]
        property string selectedTheme: "midnight"
        property string errorMessage: ""
        property bool usingFallback: false
        property bool highContrast: false
        property bool platformHighContrast: false
        property bool reducedMotion: false
        property bool rejectChange: false
        function tokens(light, accent, contrast, reduced) {
            const base = store.accent
            return {baseAccent: base, highContrast: highContrast, customized: false,
                colors: {"accent.primary": highContrast ? "#8aa8ed" : base},
                shape: {controlRadius: 6, panelRadius: 14, menuRadius: 10}, effects: {panelOpacity: 1}}
        }
        function selectTheme(id) { if (rejectChange) return false; selectedTheme = id; return true }
        function setHighContrast(value) { if (rejectChange) return false; highContrast = value || platformHighContrast; revision++; return true }
        function setReducedMotion(value) { if (rejectChange) return false; reducedMotion = value; revision++; return true }
    }
    RedesignGeneralAppearance {
        id: appearance
        width: 784
        backend: store; colorScheme: schemes; themeBackend: redesignThemeService
        appearanceRows: [["qt-backdrop-blur", "Transparency and blur"], ["qt-compact-menus", "Compact menus"]]
    }
    RedesignAddonVisualizerSelector {
        id: visualizers
        y: 500; width: appearance.width
        controller: visualizerBackend
        scaleFactor: appearance.scaleFactor
    }
    QtObject {
        id: visualizerBackend
        property var availableVisualizers: [{id: "spectrum", name: "Spectrum"}]
        property string selectedVisualizer: ""
        property string errorMessage: ""
        property bool rejectChange: false
        function selectVisualizer(id) { if (rejectChange) return false; selectedVisualizer = id; return true }
    }
    function item(name) { return findChild(appearance, name) }
    function toggle(name) { return findChild(item(name), "settingSwitch") }
    function rect(control) { return control.mapToItem(appearance, 0, 0, control.width, control.height) }
    function init() {
        appearance.width = 784; appearance.scaleFactor = 1; appearance.lightTheme = false
        store.accent = "#185dd3"; store.lastOption = ""; store.lastValue = undefined; store.saves = 0; store.revision++
        redesignThemeService.highContrast = false; redesignThemeService.reducedMotion = false
        redesignThemeService.platformHighContrast = false; redesignThemeService.rejectChange = false
        visualizerBackend.rejectChange = false; visualizerBackend.selectedVisualizer = ""
        schemes.scheme = 2
        wait(10)
    }
    function test_rightEdgesAndEditorWidths_data() { return [{tag:"reference", width:784, scale:1}, {tag:"narrow",width:460,scale:1}, {tag:"scaled",width:920,scale:1.5}] }
    function test_rightEdgesAndEditorWidths(data) {
        appearance.width = data.width; appearance.scaleFactor = data.scale
        wait(20)
        const combo = item("addonThemeSelection")
        const theme = item("settingsThemeChoices")
        const accent = item("settingsAccentChoices")
        const visual = findChild(visualizers, "addonVisualizerSelection")
        const language = findChild(item("settingsInterfaceLanguage"), "preferenceChoiceEditor")
        verify(combo.width > 100)
        verify(language !== null)
        for (const control of [language, combo, theme, accent]) {
            const position = rect(control)
            fuzzyCompare(position.x + position.width, appearance.width, 1)
            fuzzyCompare(position.width, theme.width, 1)
        }
        fuzzyCompare(visual.width, combo.width, 1)
        fuzzyCompare(visual.mapToItem(visualizers, 0, 0).x + visual.width, visualizers.width, 1)
        const label = item("addonThemeLabel"), help = item("addonThemeHelp")
        verify(rect(label).x + label.width <= rect(help).x)
        verify(rect(help).x + help.width <= rect(combo).x)
    }
    function test_accessibilityControlsFollowAccentAndRefuseFailedSave() {
        const accent = rect(item("settingsAccentRow"))
        const high = item("addonThemeHighContrast"), motion = item("addonThemeReducedMotion")
        verify(high.y >= accent.y + accent.height)
        verify(motion.y >= high.y + high.height)
        const contrast = toggle("addonThemeHighContrast")
        contrast.forceActiveFocus(Qt.TabFocusReason); keyClick(Qt.Key_Space)
        verify(redesignThemeService.highContrast); verify(contrast.checked)
        redesignThemeService.rejectChange = true
        keyClick(Qt.Key_Space)
        verify(redesignThemeService.highContrast); verify(contrast.checked)
        redesignThemeService.platformHighContrast = true
        verify(!contrast.enabled)
        redesignThemeService.rejectChange = false
        const reduced = toggle("addonThemeReducedMotion")
        reduced.forceActiveFocus(Qt.TabFocusReason); keyClick(Qt.Key_Space)
        verify(redesignThemeService.reducedMotion)
        redesignThemeService.rejectChange = true; keyClick(Qt.Key_Space)
        verify(redesignThemeService.reducedMotion); verify(reduced.checked)
        fuzzyCompare(rect(contrast).x + contrast.width, appearance.width, 1)
        fuzzyCompare(rect(reduced).x + reduced.width, appearance.width, 1)
    }
    function test_accentResetUsesThemeDefaultAndBaseSwatchSurvivesContrast() {
        const blue = item("settingsAccent185dd3")
        verify(blue.active)
        redesignThemeService.setHighContrast(true)
        verify(blue.active)
        blue.forceActiveFocus(Qt.TabFocusReason)
        const reset = item("settingsAccentReset")
        verify(reset.visible)
        reset.forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Space)
        compare(store.lastOption, "qt-accent-color"); compare(store.lastValue, "default")
        compare(appearance.theme.baseAccent.toString(), "#9ac6ff")
        verify(!blue.active)
    }
    function test_themeModeUsesNativeSelectionAndSave() {
        const light = item("settingsThemeMode1")
        light.forceActiveFocus(Qt.TabFocusReason); keyClick(Qt.Key_Space)
        compare(schemes.scheme, 1); compare(store.saves, 1); verify(light.selected)
    }
    function test_visualizerKeyboardRefusalKeepsSavedSelection() {
        const combo = findChild(visualizers, "addonVisualizerSelection")
        combo.forceActiveFocus(Qt.TabFocusReason); keyClick(Qt.Key_Down)
        compare(visualizerBackend.selectedVisualizer, "spectrum")
        visualizerBackend.rejectChange = true; keyClick(Qt.Key_Up)
        compare(combo.currentIndex, 1); compare(visualizerBackend.selectedVisualizer, "spectrum")
    }
    function test_keyboardTraversalAndHoverReset() {
        const help = item("addonThemeHelp"), combo = item("addonThemeSelection")
        const reset = item("addonThemeRestoreDefault")
        help.forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Tab); verify(reset.activeFocus)
        keyClick(Qt.Key_Tab); verify(combo.activeFocus)
        keyClick(Qt.Key_Backtab); verify(reset.activeFocus)
        keyClick(Qt.Key_Tab); verify(combo.activeFocus)
        const first = item("settingsAccentd37918"), accentReset = item("settingsAccentReset")
        first.forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Backtab); verify(accentReset.activeFocus)
        keyClick(Qt.Key_Tab); verify(first.activeFocus)
        appearance.forceActiveFocus()
        const resetPosition = rect(accentReset)
        mouseMove(appearance, resetPosition.x + resetPosition.width / 2, resetPosition.y + resetPosition.height / 2)
        wait(20); verify(accentReset.visible)
        mouseClick(accentReset)
        compare(store.lastValue, "default")
    }
    function test_helpPopupStaysInsideWindow() {
        appearance.width = 460
        const help = item("addonThemeHelp")
        help.forceActiveFocus(Qt.TabFocusReason); keyClick(Qt.Key_Space)
        wait(20)
        const popup = findChild(help, "addonThemeHelpPopup")
        verify(popup.visible)
        verify(popup.x >= 0); verify(popup.x + popup.width <= testCase.width)
        keyClick(Qt.Key_Escape); verify(!popup.visible)
    }
    function test_nullThemeBackendLeavesNoAccessibilityGap() {
        const old = appearance.height
        appearance.themeBackend = null; wait(20)
        verify(!item("addonThemeHighContrast").visible)
        verify(!item("addonThemeReducedMotion").visible)
        verify(appearance.height <= old - 84)
        appearance.themeBackend = redesignThemeService
    }
}

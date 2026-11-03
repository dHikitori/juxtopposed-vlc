// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    name: "AddonThemeHost"
    width: 600; height: 500
    when: windowShown
    QtObject {
        id: redesignAppearance
        property bool backdropBlurEnabled: true
    }
    QtObject {
        id: redesignThemeService
        property int revision: 0
        property string color: "#9ac6ff"
        property bool contrast: false
        function tokens(light, accent, highContrast, reducedTransparency) {
            return {customized: true, highContrast: contrast, baseAccent: color,
                colors: {"surface.base": light ? "#e9edf5" : "#171923", "surface.panel": "#1e2230",
                    "surface.menu": "#1e2230", "surface.field": "#121520", "surface.hover": "#30394b",
                    "surface.selected": "#36465e", "text.primary": "#edf0f7", "text.secondary": "#b6c0d4",
                    "accent.primary": contrast ? "#ffff00" : color, "focus.ring": "#bbd9ff", "text.onAccent": "#000000"},
                shape: {controlRadius: 8, panelRadius: 12, menuRadius: 12},
                effects: {panelOpacity: reducedTransparency || contrast ? 1 : .96}}
        }
    }
    Component { id: buttonComponent; RedesignButton { caption: "Action"; text: caption; primaryAction: true } }
    Component { id: viewComponent; RedesignAddonView {
        width: 400
        document: ({contract: "ui@1", id: "test", root: {type: "Button", id: "action", text: "Action", action: {command: "org.example.test/action"}}})
        registeredCommands: ["org.example.test/action"]
    } }
    Component { id: dialogComponent; RedesignDialogSurface { width: 400; height: 200 } }
    Component { id: comboComponent; RedesignComboBox { model: ["First", "Second"] } }
    function init() {
        redesignAppearance.backdropBlurEnabled = true
        redesignThemeService.color = "#9ac6ff"; redesignThemeService.contrast = false
        ++redesignThemeService.revision
    }
    function test_openControlUpdatesFromSharedSnapshot() {
        const button = createTemporaryObject(buttonComponent, this)
        compare(button.theme.accent.toString(), "#9ac6ff")
        compare(button.cornerRadius, 8)
        redesignThemeService.color = "#c2a8ff"; ++redesignThemeService.revision
        compare(button.theme.accent.toString(), "#c2a8ff")
        compare(button.foreground.toString(), "#c2a8ff")
    }
    function test_extensionUsesSameTokens() {
        const view = createTemporaryObject(viewComponent, this)
        verify(view.valid)
        tryVerify(() => findChild(view, "addonActionLabel") !== null)
        compare(view.theme.accent.toString(), "#9ac6ff")
        redesignThemeService.color = "#c2a8ff"; ++redesignThemeService.revision
        compare(view.theme.accent.toString(), "#c2a8ff")
    }
    function test_hostScaleAndTransparencyRemainAuthoritative() {
        const dialog = createTemporaryObject(dialogComponent, this)
        compare(dialog.radius, 12)
        dialog.theme.scaleFactor = 2
        compare(dialog.radius, 24)
        compare(dialog.color.a.toFixed(2), "0.96")
        redesignAppearance.backdropBlurEnabled = false
        compare(dialog.color.a, 1)
    }
    function test_highContrastAndMenuUseSnapshot() {
        const combo = createTemporaryObject(comboComponent, this)
        compare(combo.background.color.toString(), "#1e2230")
        redesignThemeService.contrast = true; ++redesignThemeService.revision
        verify(combo.theme.highContrast)
        compare(combo.theme.accent.toString(), "#ffff00")
        compare(combo.theme.baseAccent.toString(), "#9ac6ff")
        compare(combo.theme.floatingOpacity, 1)
    }
}

// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"
import "../player/qml/redesign/AddonTheme.js" as Theme

TestCase {
    name: "AddonTheme"
    RedesignAddonTheme { id: theme }
    function document() {
        return {contract: "theme@1", id: "midnight", variants: {dark: {
            colors: {"surface.base": "#171923", "surface.panel": "#1e2230", "text.primary": "#edf0f7", "text.secondary": "#b6c0d4", "accent.primary": "#9ac6ff", "focus.ring": "#bbd9ff"},
            shape: {controlRadius: 6, panelRadius: 10}, effects: {panelOpacity: 0.96}
        }}}
    }
    function init() {
        theme.document = null; theme.lightTheme = false; theme.userAccent = ""
        theme.highContrast = false; theme.reducedTransparency = false
    }
    function test_defaultsAndPartialVariantFallback() {
        verify(theme.valid)
        compare(theme.snapshot.colors["surface.base"], "#1b1b1b")
        theme.document = document()
        verify(theme.valid, theme.errorMessage)
        compare(theme.snapshot.colors["surface.base"], "#171923")
        compare(theme.snapshot.shape.panelRadius, 10)
        theme.lightTheme = true
        verify(theme.usesFallbackVariant)
        compare(theme.snapshot.colors["surface.base"], "#acacac")
    }
    function test_userAndAccessibilityPreferencesWin() {
        theme.document = document()
        theme.userAccent = "#12bc89"
        compare(theme.snapshot.colors["accent.primary"], "#12bc89")
        theme.reducedTransparency = true
        compare(theme.snapshot.effects.panelOpacity, 1)
        theme.highContrast = true
        compare(theme.snapshot.colors["surface.base"], "#000000")
        compare(theme.snapshot.colors["text.primary"], "#ffffff")
        compare(theme.snapshot.colors["accent.primary"], "#ffff00")
        theme.lightTheme = true
        compare(theme.snapshot.colors["surface.base"], "#ffffff")
        compare(theme.snapshot.colors["text.primary"], "#000000")
    }
    function test_unknownTokensAndExecutablePropertiesFailClosed() {
        const invalid = document()
        invalid.variants.dark.colors["qml.source"] = "#ffffff"
        theme.document = invalid
        verify(!theme.valid)
        compare(theme.snapshot.colors["surface.base"], "#1b1b1b")
        delete invalid.variants.dark.colors["qml.source"]
        invalid.variants.dark.source = "file:///tmp/evil.qml"
        verify(Theme.validate(invalid).length > 0)
    }
    function test_shapeAndTransparencyBounds() {
        for (const radius of [-1, 13, 100000]) {
            const invalid = document(); invalid.variants.dark.shape.controlRadius = radius
            verify(Theme.validate(invalid).length > 0)
        }
        const transparent = document(); transparent.variants.dark.effects.panelOpacity = 0.1
        verify(Theme.validate(transparent).length > 0)
    }
    function test_contrastAndColorValidation() {
        const invisible = document(); invisible.variants.dark.colors["text.primary"] = "#171923"
        verify(Theme.validate(invisible).length > 0)
        const hiddenFocus = document(); hiddenFocus.variants.dark.colors["focus.ring"] = "#171923"
        verify(Theme.validate(hiddenFocus).length > 0)
        const assetUrl = document(); assetUrl.variants.dark.colors["text.primary"] = "url(https://example.org/style)"
        verify(Theme.validate(assetUrl).length > 0)
        verify(Theme.contrast("#000000", "#ffffff") > 20)
    }
}

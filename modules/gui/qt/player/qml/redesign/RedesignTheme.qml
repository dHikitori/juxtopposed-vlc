// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

QtObject {
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    readonly property var themeService: typeof redesignThemeService === "undefined" ? null : redesignThemeService
    readonly property var tokenSnapshot: {
        if (!themeService) return null
        themeService.revision
        return themeService.tokens(lightTheme, "", false, !transparencyEnabled)
    }
    readonly property bool customized: !!tokenSnapshot && tokenSnapshot.customized === true
    readonly property bool highContrast: !!tokenSnapshot && tokenSnapshot.highContrast === true
    readonly property bool reducedMotion: !!themeService && themeService.reducedMotion === true
    readonly property real controlRadius: tokenSnapshot ? tokenSnapshot.shape.controlRadius : 6
    readonly property real panelRadius: tokenSnapshot ? tokenSnapshot.shape.panelRadius : 14
    readonly property real menuRadius: tokenSnapshot ? tokenSnapshot.shape.menuRadius : 10
    readonly property color focusRing: token("focus.ring", lightTheme ? "#785000" : accentColor)
    // Standalone previews use the design default; the app supplies a live preference.
    readonly property bool transparencyEnabled: typeof redesignAppearance === "undefined" || !redesignAppearance
        ? true : redesignAppearance.backdropBlurEnabled
    readonly property bool compactMenus: typeof redesignAppearance !== "undefined" && !!redesignAppearance
        && redesignAppearance.compactMenus === true
    readonly property real menuItemHeight: dp(compactMenus ? 28 : 48)
    readonly property real menuItemPadding: dp(compactMenus ? 8 : 14)
    readonly property real menuPadding: dp(compactMenus ? 2 : 4)
    readonly property real floatingOpacity: tokenSnapshot ? tokenSnapshot.effects.panelOpacity : transparencyEnabled ? 0.90 : 1
    readonly property color background: token("surface.base", lightTheme ? "#acacac" : "#1b1b1b")
    readonly property color panel: token("surface.panel", lightTheme ? "#c3c3c3" : "#0f0f0f")
    readonly property color floatingPanel: withOpacity(token("surface.floating", lightTheme ? "#c3c3c3" : "#0e0e0e"), floatingOpacity)
    readonly property color libraryPanel: token("surface.library", lightTheme ? panel : "#0e0e0e")
    readonly property color libraryOverlay: withOpacity(token("surface.libraryOverlay", lightTheme ? "#e7e7e7" : "#0e0e0e"), floatingOpacity)
    readonly property color libraryNavigation: token("surface.navigation", lightTheme ? background : "#191919")
    readonly property color libraryNavigationOverlay: withOpacity(token("surface.navigationOverlay", lightTheme ? "#d5d5d5" : "#191919"), customized ? floatingOpacity : lightTheme && transparencyEnabled ? 0.75 : 1)
    readonly property color field: token("surface.field", lightTheme ? "#dedede" : "#090909")
    readonly property color border: token("border.default", lightTheme ? "#969696" : "#292929")
    readonly property color foreground: token("text.secondary", lightTheme ? "#404040" : "#8f8f8f")
    readonly property color bright: token("text.primary", lightTheme ? "#2e2e2e" : "#e5e5e5")
    readonly property color settingsHeading: token("text.heading", lightTheme ? bright : "#c6c6c6")
    readonly property color playerHeading: token("text.playerHeading", lightTheme ? bright : "#969696")
    readonly property color playerLabel: token("text.muted", lightTheme ? foreground : "#6b6b6b")
    readonly property color hover: token("surface.hover", lightTheme ? "#d5d5d5" : "#292929")
    readonly property color selected: token("surface.selected", lightTheme ? "#b8b8b8" : "#333333")
    readonly property color menuPanel: token("surface.menu", lightTheme ? background : "#191919")
    readonly property color menuHover: token("surface.menuHover", lightTheme ? hover : "#1f1f1f")
    readonly property color accent: token("accent.primary", accentColor)
    readonly property color onAccent: token("text.onAccent", "#161616")
    readonly property color menuAccent: accent.toString() === "#d37918" ? "#ab6314" : Qt.darker(accent, 1.23)
    // Keep the original orange palette; other accents use the Figma fill opacity.
    readonly property bool originalAccent: !customized && accent.toString() === "#d37918"
    readonly property real accentFillOpacity: lightTheme ? 0.35 : accent.toString() === "#185dd3" ? 0.30 : 0.24
    readonly property color progress: originalAccent ? lightTheme ? "#d8ba9a" : "#371f06" : tintAccent(field, accentFillOpacity)
    readonly property color sliderTick: lightTheme ? Qt.darker(field, 1.12) : originalAccent ? "#1a1611" : tintAccent(field, 0.1)
    readonly property color subtitleCue: token("subtitle.cue", lightTheme ? "#a3a3a3" : "#202020")
    readonly property color subtitleCueText: token("subtitle.cueText", lightTheme ? "#161616" : bright)
    readonly property color subtitleCueProgress: originalAccent ? lightTheme ? "#a27b53" : "#4e3823" : tintAccent(subtitleCue, accentFillOpacity)
    readonly property color danger: token("status.danger", "#d33718")
    readonly property color dangerBackground: token("status.dangerBackground", lightTheme ? "#f8e2d9" : "#1a1111")

    function token(name: string, fallback: color): color { return tokenSnapshot && tokenSnapshot.colors[name] !== undefined ? tokenSnapshot.colors[name] : fallback }
    function withOpacity(value: color, opacity: real): color { return Qt.rgba(value.r, value.g, value.b, opacity) }
    function dp(value: real): real { return value * scaleFactor }
    function tintAccent(background: color, opacity: real): color {
        return Qt.tint(background, Qt.rgba(accent.r, accent.g, accent.b, opacity))
    }
}

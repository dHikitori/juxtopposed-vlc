// SPDX-License-Identifier: GPL-2.0-or-later
.pragma library
.import "AddonUi.js" as Contract

var colorNames = ["surface.base", "surface.panel", "surface.field", "surface.hover", "surface.selected", "text.primary", "text.secondary", "accent.primary", "focus.ring", "border.default", "status.danger"]
function defaults(light) {
    return {
        colors: {
            "surface.base": light ? "#acacac" : "#1b1b1b",
            "surface.panel": light ? "#c3c3c3" : "#0f0f0f",
            "surface.field": light ? "#dedede" : "#090909",
            "surface.hover": light ? "#d5d5d5" : "#292929",
            "surface.selected": light ? "#b8b8b8" : "#333333",
            "text.primary": light ? "#2e2e2e" : "#e5e5e5",
            "text.secondary": light ? "#404040" : "#8f8f8f",
            "accent.primary": "#d37918",
            "focus.ring": light ? "#785000" : "#d37918",
            "border.default": light ? "#969696" : "#292929",
            "status.danger": "#d33718"
        },
        shape: {controlRadius: 6, panelRadius: 14},
        effects: {panelOpacity: 0.90}
    }
}
function isColor(value) { return typeof value === "string" && /^#[0-9a-fA-F]{6}$/.test(value) }
function luminance(value) {
    var channels = [1, 3, 5].map(index => parseInt(value.slice(index, index + 2), 16) / 255)
    channels = channels.map(channel => channel <= 0.04045 ? channel / 12.92 : Math.pow((channel + 0.055) / 1.055, 2.4))
    return channels[0] * 0.2126 + channels[1] * 0.7152 + channels[2] * 0.0722
}
function contrast(a, b) {
    var first = luminance(a), second = luminance(b)
    return (Math.max(first, second) + 0.05) / (Math.min(first, second) + 0.05)
}
function merge(variant, light) {
    var base = defaults(light)
    return {colors: Object.assign({}, base.colors, variant.colors || {}),
        shape: Object.assign({}, base.shape, variant.shape || {}),
        effects: Object.assign({}, base.effects, variant.effects || {})}
}
function validate(document) {
    var error = Contract.validateData(document)
    if (error) return error
    if (!Contract.record(document) || document.contract !== "theme@1" || !Contract.safeKey(document.id)
            || !Contract.record(document.variants) || Object.keys(document).some(key => !["contract", "id", "variants"].includes(key))) return "The theme contract is not correct."
    if (!Object.keys(document.variants).length || Object.keys(document.variants).some(key => !["light", "dark"].includes(key))) return "The theme variants are not correct."
    for (var name of Object.keys(document.variants)) {
        var variant = document.variants[name]
        if (!Contract.record(variant) || Object.keys(variant).some(key => !["colors", "shape", "effects"].includes(key))) return "The app cannot use this theme group."
        if (variant.colors !== undefined) {
            if (!Contract.record(variant.colors)) return "The theme colors are not correct."
            for (var color of Object.keys(variant.colors)) if (!colorNames.includes(color) || !isColor(variant.colors[color])) return "The app cannot use this theme color token."
        }
        if (variant.shape !== undefined) {
            if (!Contract.record(variant.shape)) return "The theme shape is not correct."
            for (var shape of Object.keys(variant.shape)) {
                var value = variant.shape[shape]
                if (!["controlRadius", "panelRadius"].includes(shape) || typeof value !== "number" || !Number.isFinite(value)
                        || value < 0 || value > (shape === "controlRadius" ? 12 : 20)) return "A theme shape token is not within the permitted limits."
            }
        }
        if (variant.effects !== undefined && (!Contract.record(variant.effects)
                || Object.keys(variant.effects).some(key => key !== "panelOpacity")
                || variant.effects.panelOpacity !== undefined && (typeof variant.effects.panelOpacity !== "number"
                    || variant.effects.panelOpacity < 0.85 || variant.effects.panelOpacity > 1))) return "A theme effect token is not within the permitted limits."
        var colors = merge(variant, name === "light").colors
        for (var surface of ["surface.base", "surface.panel", "surface.field"]) {
            for (var foreground of ["text.primary", "text.secondary"]) if (contrast(colors[foreground], colors[surface]) < 4.5) return "The theme text contrast is less than the minimum."
            if (contrast(colors["focus.ring"], colors[surface]) < 3) return "The theme focus indicator does not have enough contrast."
        }
    }
    return ""
}
function resolve(document, light, userAccent, accessibility) {
    var valid = document !== null && !validate(document)
    var selected = valid ? document.variants[light ? "light" : "dark"] : null
    var tokens = merge(selected || {}, light)
    if (isColor(userAccent)) tokens.colors["accent.primary"] = userAccent
    accessibility = accessibility || {}
    if (accessibility.highContrast === true) {
        var background = light ? "#ffffff" : "#000000", foreground = light ? "#000000" : "#ffffff"
        for (var surface of ["surface.base", "surface.panel", "surface.field"]) tokens.colors[surface] = background
        tokens.colors["surface.hover"] = light ? "#dddddd" : "#333333"
        tokens.colors["surface.selected"] = light ? "#cccccc" : "#444444"
        tokens.colors["text.primary"] = foreground; tokens.colors["text.secondary"] = foreground
        tokens.colors["accent.primary"] = light ? "#005fcc" : "#ffff00"
        tokens.colors["focus.ring"] = tokens.colors["accent.primary"]
        tokens.colors["border.default"] = foreground
        tokens.effects.panelOpacity = 1
    }
    if (accessibility.reducedTransparency === true) tokens.effects.panelOpacity = 1
    return tokens
}

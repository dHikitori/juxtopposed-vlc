// SPDX-License-Identifier: GPL-2.0-or-later
.pragma library

function object(value) { return value !== null && typeof value === "object" && !Array.isArray(value) }
function keys(value, permitted) { return object(value) && Object.keys(value).every(key => permitted.includes(key)) }
function text(value, limit, empty) {
    if (typeof value !== "string" || value.length > limit || (!empty && !value.length)) return false
    for (let i = 0; i < value.length; ++i) {
        const code = value.charCodeAt(i)
        if (code < 32 || (code >= 127 && code <= 159) || code === 0x2028 || code === 0x2029 || (code >= 0xdc00 && code <= 0xdfff)) return false
        if (code >= 0xd800 && code <= 0xdbff) {
            const low = value.charCodeAt(++i)
            if (!(low >= 0xdc00 && low <= 0xdfff)) return false
        }
    }
    return true
}

// The host keeps this object. Only publicData enters the package session.
function snapshot(kind, choiceSet, targets, assets) {
    if (!["playlist", "subtitles"].includes(kind) || !text(choiceSet, 128, false)
        || !Array.isArray(targets) || targets.length > 200 || !Array.isArray(assets) || assets.length > 200) return null
    const result = {kind: kind, choiceSet: choiceSet, targetBindings: ({}), assetBindings: ({}),
        publicData: {choiceSet: choiceSet, targets: [], assets: []}}
    function copy(rows, prefix, bindings, output) {
        const seen = []
        for (let index = 0; index < rows.length; ++index) {
            const row = rows[index]
            if (!object(row) || !text(row.key, 8192, false) || !text(row.title, 512, true)
                || (row.reviewTitle !== undefined && !text(row.reviewTitle, 512, true))
                || seen.includes(row.key) || (row.enabled !== undefined && typeof row.enabled !== "boolean")) return false
            const ref = prefix + index
            seen.push(row.key)
            bindings[ref] = {key: row.key, enabled: row.enabled !== false, reviewTitle: row.reviewTitle === undefined ? row.title : row.reviewTitle}
            output.push({ref: ref, title: row.title, enabled: row.enabled !== false})
        }
        return true
    }
    if (!copy(targets, "target:", result.targetBindings, result.publicData.targets)
        || !copy(assets, "asset:", result.assetBindings, result.publicData.assets)) return null
    return result
}

function playlistResult(current, draft) {
    if (!current || current.kind !== "playlist" || !keys(draft, ["choiceSet", "selected", "newName"])
        || draft.choiceSet !== current.choiceSet || !Array.isArray(draft.selected) || draft.selected.length > 1
        || (draft.newName !== undefined && !text(draft.newName, 256, true))) return null
    const name = (draft.newName || "").trim()
    if (draft.selected.length) {
        const ref = draft.selected[0]
        const binding = typeof ref === "string" && Object.prototype.hasOwnProperty.call(current.targetBindings, ref) ? current.targetBindings[ref] : null
        if (!binding || !binding.enabled || name.length) return null
        return {key: binding.key, name: ""}
    }
    return name.length ? {key: "", name: name} : null
}

function subtitleResult(current, draft) {
    if (!current || current.kind !== "subtitles" || !keys(draft, ["choiceSet", "assignments"])
        || draft.choiceSet !== current.choiceSet || !Array.isArray(draft.assignments)
        || !draft.assignments.length || draft.assignments.length > 200) return null
    const targets = [], result = []
    let total = 0
    for (const assignment of draft.assignments) {
        if (!keys(assignment, ["targetRef", "assetRefs"]) || typeof assignment.targetRef !== "string"
            || !Object.prototype.hasOwnProperty.call(current.targetBindings, assignment.targetRef)
            || targets.includes(assignment.targetRef) || !Array.isArray(assignment.assetRefs)
            || assignment.assetRefs.length > 200) return null
        const target = current.targetBindings[assignment.targetRef]
        if (!target.enabled || (total += assignment.assetRefs.length) > 1000) return null
        targets.push(assignment.targetRef)
        const seen = [], assets = []
        for (const ref of assignment.assetRefs) {
            const binding = typeof ref === "string" && Object.prototype.hasOwnProperty.call(current.assetBindings, ref) ? current.assetBindings[ref] : null
            if (!binding || !binding.enabled || seen.includes(ref)) return null
            seen.push(ref); assets.push(binding.key)
        }
        result.push({targetKey: target.key, assetKeys: assets})
    }
    return result
}

// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

QtObject {
    id: root
    property string savedLayout: ""
    property bool editing: false
    // Enabled player panels from the add-on registry. A panel that asks for a
    // zone appears there until the user removes it in the editor.
    property var panels: []
    // The bundled Episodes panel replaces the built-in "episodes" control.
    readonly property string episodesPanel: "addon:org.videolan.episodes/episodes"
    property var stored: defaults()
    property var draft: defaults()
    readonly property var layout: editing ? draft : placed(stored)
    readonly property bool dirty: editing && JSON.stringify(withDismissed(draft)) !== JSON.stringify(placed(stored))
    readonly property var zones: ["headerLeft", "headerCenter", "headerRight", "footerLeft", "footerCenter", "footerRight", "above"]
    readonly property var controls: ["menu", "library", "folder", "title", "minimize", "maximize", "fullscreen", "close",
        "previous", "play", "next", "timeline", "mute", "volume", "audio", "subtitles", "settings", "queue",
        "speed", "forward10", "backward10", "forward15", "backward15", "stop", "repeat", "shuffle", "record",
        "snapshot", "pip", "hdr", "zoom", "rightSidebar", "quit", "preferences", "home", "thumbnail",
        "fileName", "artist", "folderName", "playlistName", "properties", "replay", "chapters", "favorite",
        "bookmark", "bookmarks", "playlists"]

    function defaults() {
        return {version: 1, zones: {
            headerLeft: ["menu", "library", "divider:1", "folder"],
            headerCenter: ["title"], headerRight: ["minimize", "maximize", "fullscreen", "close"],
            footerLeft: ["previous", "play", "next"], footerCenter: ["timeline"],
            footerRight: ["mute", "volume", "audio", "subtitles", "settings", episodesPanel, "queue"], above: []
        }}
    }
    function clone(value) { return JSON.parse(JSON.stringify(value)) }
    function panelId(id) { return typeof id === "string" && /^addon:[a-z][a-z0-9.-]{1,127}\/[a-z][a-z0-9.-]{0,63}$/.test(id) }
    function validId(id) {
        return typeof id === "string" && (controls.includes(id) || panelId(id) || /^(spacer|divider):[1-9][0-9]{0,5}$/.test(id))
    }
    function valid(value) {
        if (!value || value.version !== 1 || !value.zones || typeof value.zones !== "object"
            || Array.isArray(value.zones) || Object.keys(value.zones).length !== zones.length) return false
        const seen = new Set()
        for (const zone of zones) {
            const items = value.zones[zone]
            if (!Array.isArray(items) || items.length > 64) return false
            for (const id of items) {
                if (!validId(id) || seen.has(id) || seen.size >= 96) return false
                seen.add(id)
            }
        }
        if (value.dismissed !== undefined && (!Array.isArray(value.dismissed) || value.dismissed.length > 64
            || value.dismissed.some(id => !panelId(id)))) return false
        return true
    }
    function decode(text) {
        if (typeof text !== "string" || text.length > 65536) return null
        try {
            const value = JSON.parse(text)
            // Layouts saved before Episodes became an add-on name its control.
            if (value && value.zones && typeof value.zones === "object")
                for (const zone of zones)
                    if (Array.isArray(value.zones[zone]))
                        value.zones[zone] = value.zones[zone].map(id => id === "episodes" ? episodesPanel : id)
            if (!valid(value)) return null
            const normalized = {version: 1, zones: {}}
            for (const zone of zones) normalized.zones[zone] = value.zones[zone].slice()
            if (Array.isArray(value.dismissed) && value.dismissed.length) normalized.dismissed = value.dismissed.slice()
            return normalized
        } catch (error) { return null }
    }
    function reload() {
        editing = false
        stored = decode(savedLayout) || defaults()
        draft = placed(stored)
    }
    // The layout with each requested panel zone that the user has not
    // dismissed. It is drawn, never saved by itself.
    function placed(value) {
        const next = clone(value)
        const used = new Set()
        for (const zone of zones) for (const id of next.zones[zone]) used.add(id)
        for (const panel of panels || []) {
            const id = "addon:" + panel.id
            if (!panelId(id) || used.has(id) || (next.dismissed || []).includes(id) || !zones.includes(panel.placement)
                || panel.placement === "headerCenter" || panel.placement === "footerCenter"
                || next.zones[panel.placement].length >= 64 || used.size >= 96) continue
            next.zones[panel.placement].push(id)
            used.add(id)
        }
        return next
    }
    // Panels that the user removed from the drawn layout stay removed.
    function withDismissed(value) {
        const next = clone(value)
        const used = new Set()
        for (const zone of zones) for (const id of next.zones[zone]) used.add(id)
        const shown = placed(stored)
        const dismissed = new Set((next.dismissed || []).filter(id => !used.has(id)))
        for (const zone of zones)
            for (const id of shown.zones[zone]) if (panelId(id) && !used.has(id)) dismissed.add(id)
        if (dismissed.size) next.dismissed = Array.from(dismissed).slice(-64)
        else delete next.dismissed
        return next
    }
    function begin() {
        if (editing) return
        draft = placed(stored)
        editing = true
    }
    function cancel() {
        editing = false
        draft = placed(stored)
    }
    // The caller writes the serialized draft in the same transaction as the
    // active player preset. A failed write must leave the editor open.
    function finish(success) {
        if (!editing || !success) return false
        stored = withDismissed(draft)
        editing = false
        return true
    }
    function serialize() { return JSON.stringify(withDismissed(draft)) }
    function reset() { if (editing) draft = defaults() }
    function location(id) {
        for (const zone of zones) {
            const index = layout.zones[zone].indexOf(id)
            if (index >= 0) return {zone: zone, index: index}
        }
        return null
    }
    function instanceId(kind, value) {
        const used = new Set()
        for (const zone of zones)
            for (const id of value.zones[zone]) used.add(id)
        for (let n = 1; n <= 96; ++n) {
            const id = kind + ":" + n
            if (!used.has(id)) return id
        }
        return ""
    }
    // index is an insertion boundary before removal, like VLC's queue moves.
    function move(id, zone, index) {
        if (id === "spacer" || id === "divider") id = instanceId(id, draft)
        return moveGroup([id], zone, index)
    }
    function moveGroup(ids, zone, index) {
        if (!editing || !zones.includes(zone) || !Number.isInteger(index)
            || index < 0 || index > draft.zones[zone].length
            || !Array.isArray(ids) || !ids.length || ids.some(id => !validId(id))
            || new Set(ids).size !== ids.length) return false
        const next = clone(draft)
        for (const source of zones) {
            // Count against the original boundary before removing any items.
            if (source === zone)
                index -= next.zones[source].slice(0, index).filter(id => ids.includes(id)).length
            next.zones[source] = next.zones[source].filter(id => !ids.includes(id))
        }
        next.zones[zone].splice(index, 0, ...ids)
        if (!valid(next)) return false
        draft = next
        return true
    }
    function remove(id) {
        return removeGroup([id])
    }
    function removeGroup(ids) {
        if (!editing || !Array.isArray(ids) || !ids.length
            || new Set(ids).size !== ids.length || ids.some(id => !location(id))) return false
        const next = clone(draft)
        for (const zone of zones)
            next.zones[zone] = next.zones[zone].filter(id => !ids.includes(id))
        draft = next
        return true
    }
    onSavedLayoutChanged: reload()
    Component.onCompleted: reload()
}

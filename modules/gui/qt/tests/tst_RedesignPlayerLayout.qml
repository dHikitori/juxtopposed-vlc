// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    name: "JuxtopposedPlayerLayout"
    RedesignPlayerLayout { id: layout }
    Component { id: layoutComponent; RedesignPlayerLayout { } }
    function init() { layout.panels = []; layout.savedLayout = ""; layout.reload() }
    function test_legacyEpisodesControlBecomesThePanel() {
        const saved = layout.defaults()
        saved.zones.footerRight = ["mute", "volume", "episodes", "queue"]
        layout.savedLayout = JSON.stringify(saved).split(layout.episodesPanel).join("x")
        layout.reload()
        compare(layout.layout.zones.footerRight, ["mute", "volume", layout.episodesPanel, "queue"])
        verify(!layout.validId("episodes"))
        verify(layout.validId("addon:org.example.panels/lyrics"))
        verify(!layout.validId("addon:Org.Example/x") && !layout.validId("addon:org.example/") && !layout.validId("addon:/x"))
    }
    function test_requestedPanelZoneUntilDismissed() {
        layout.panels = [{id: "org.example.panels/lyrics", name: "Lyrics", placement: "footerLeft"},
                         {id: "org.example.panels/notes", name: "Notes"},
                         {id: "org.example.panels/center", name: "Center", placement: "footerCenter"}]
        // The requested zone is drawn, not saved.
        compare(layout.layout.zones.footerLeft, ["previous", "play", "next", "addon:org.example.panels/lyrics"])
        compare(layout.location("addon:org.example.panels/notes"), null)
        compare(layout.location("addon:org.example.panels/center"), null)
        verify(!layout.dirty)
        layout.begin()
        verify(!layout.dirty)
        verify(layout.remove("addon:org.example.panels/lyrics"))
        verify(layout.dirty)
        const saved = JSON.parse(layout.serialize())
        compare(saved.dismissed, ["addon:org.example.panels/lyrics"])
        verify(layout.finish(true))
        compare(layout.location("addon:org.example.panels/lyrics"), null)
        // A dismissed panel does not come back after a restart either.
        layout.savedLayout = JSON.stringify(saved); layout.reload()
        compare(layout.location("addon:org.example.panels/lyrics"), null)
        // Moving it back in the editor clears the dismissal.
        layout.begin()
        verify(layout.move("addon:org.example.panels/lyrics", "headerRight", 0))
        const again = JSON.parse(layout.serialize())
        compare(again.dismissed, undefined)
        compare(again.zones.headerRight[0], "addon:org.example.panels/lyrics")
        layout.cancel()
    }
    function test_originalLayout() {
        verify(!layout.editing); verify(!layout.dirty)
        compare(layout.layout, layout.defaults())
        compare(layout.location("menu"), {zone: "headerLeft", index: 0})
        compare(layout.location("timeline"), {zone: "footerCenter", index: 0})
        compare(layout.location("favorite"), null)
    }
    function test_referenceAlternateLayoutAndCancel() {
        layout.begin()
        verify(layout.move("timeline", "above", 0))
        for (const id of ["previous", "play", "next"])
            verify(layout.move(id, "footerCenter", layout.layout.zones.footerCenter.length))
        for (const id of ["menu", "library", "divider:1", "folder", "title"])
            verify(layout.move(id, "footerLeft", layout.layout.zones.footerLeft.length))
        verify(layout.move("favorite", "footerLeft", 5))
        verify(layout.move("fullscreen", "footerRight", layout.layout.zones.footerRight.length))
        compare(layout.layout.zones.above, ["timeline"])
        compare(layout.layout.zones.footerCenter, ["previous", "play", "next"])
        compare(layout.layout.zones.footerLeft, ["menu", "library", "divider:1", "folder", "title", "favorite"])
        verify(layout.dirty)
        layout.cancel()
        compare(layout.layout, layout.defaults()); verify(!layout.dirty)
    }
    function test_reorderInsertionBoundaries() {
        layout.begin()
        verify(layout.move("previous", "footerLeft", 3))
        compare(layout.layout.zones.footerLeft, ["play", "next", "previous"])
        verify(layout.move("previous", "footerLeft", 0))
        compare(layout.layout.zones.footerLeft, ["previous", "play", "next"])
        verify(!layout.dirty)
    }
    function test_removeAndRestoreControl() {
        layout.begin()
        verify(layout.remove("volume"))
        compare(layout.location("volume"), null)
        verify(!layout.remove("volume"))
        verify(layout.move("volume", "headerLeft", 0))
        compare(layout.location("volume"), {zone: "headerLeft", index: 0})
        compare(layout.layout.zones.footerRight.includes("volume"), false)
    }
    function test_groupMovesUseOriginalInsertionBoundary() {
        layout.begin()
        verify(layout.moveGroup(["mute", "volume"], "footerRight", 7))
        compare(layout.layout.zones.footerRight, ["audio", "subtitles", "settings", "addon:org.hakone.episodes/episodes", "queue", "mute", "volume"])
        verify(layout.moveGroup(["mute", "volume"], "footerRight", 0))
        compare(layout.layout, layout.defaults())
        verify(layout.moveGroup(["mute", "volume"], "above", 0))
        compare(layout.layout.zones.above, ["mute", "volume"])
        verify(layout.removeGroup(["mute", "volume"]))
        compare(layout.location("mute"), null); compare(layout.location("volume"), null)
        verify(layout.moveGroup(["mute", "volume"], "headerLeft", 2))
        const saved = layout.serialize()
        verify(layout.finish(true))
        const fresh = createTemporaryObject(layoutComponent, this, {savedLayout: saved})
        compare(fresh.layout, layout.layout)
    }
    function test_invalidGroupOperationsAreAtomic() {
        layout.begin()
        const original = layout.serialize()
        for (const ids of [[], ["mute", "mute"], ["mute", "unknown"], null]) {
            verify(!layout.moveGroup(ids, "above", 0))
            verify(!layout.removeGroup(ids))
            compare(layout.serialize(), original)
        }
        verify(!layout.removeGroup(["mute", "favorite"]))
        compare(layout.serialize(), original)
        layout.cancel()
        verify(!layout.moveGroup(["mute", "volume"], "above", 0))
        verify(!layout.removeGroup(["mute", "volume"]))
    }
    function test_repeatableSeparators() {
        layout.begin()
        verify(layout.move("divider", "footerLeft", 0))
        verify(layout.move("divider", "footerLeft", 1))
        verify(layout.move("spacer", "footerLeft", 2))
        verify(layout.move("spacer", "footerLeft", 3))
        compare(layout.layout.zones.footerLeft.slice(0, 4), ["divider:2", "divider:3", "spacer:1", "spacer:2"])
        verify(layout.valid(layout.layout))
    }
    function test_saveFailureKeepsDraft() {
        layout.begin(); layout.move("timeline", "above", 0)
        verify(!layout.finish(false))
        verify(layout.editing); verify(layout.dirty)
        compare(layout.stored, layout.defaults())
        compare(layout.layout.zones.above, ["timeline"])
    }
    function test_equivalentSavedLayoutHasNoPendingChanges() {
        const value = layout.defaults(), reversed = {zones: {}, version: 1}
        for (const zone of layout.zones.slice().reverse()) reversed.zones[zone] = value.zones[zone]
        layout.savedLayout = JSON.stringify(reversed, null, 2)
        layout.begin(); verify(!layout.dirty)
        layout.reset(); verify(!layout.dirty)
        compare(layout.layout, layout.defaults())
    }
    function test_saveRoundTripAndRestart() {
        layout.begin(); layout.move("timeline", "above", 0); layout.remove("audio")
        const text = layout.serialize()
        verify(layout.finish(true)); verify(!layout.editing); verify(!layout.dirty)
        const fresh = createTemporaryObject(layoutComponent, this, {savedLayout: text})
        verify(fresh !== null)
        compare(fresh.layout, layout.layout)
        fresh.begin(); fresh.reset(); verify(fresh.dirty); fresh.cancel()
        compare(fresh.layout, layout.layout)
    }
    function test_externalProfileChangeDiscardsDraft() {
        layout.begin(); layout.move("timeline", "above", 0)
        const other = layout.defaults(); other.zones.footerRight = ["queue"]
        layout.savedLayout = JSON.stringify(other)
        verify(!layout.editing); verify(!layout.dirty)
        compare(layout.layout, other)
        layout.begin(); compare(layout.draft, other)
    }
    function test_unknownOrDamagedSettings_data() {
        const duplicate = layout.defaults(); duplicate.zones.above = ["timeline"]
        const unknown = layout.defaults(); unknown.zones.headerLeft.push("unrecognized")
        const extra = layout.defaults(); extra.zones.extra = []
        const missing = layout.defaults(); delete missing.zones.headerLeft
        const badId = layout.defaults(); badId.zones.above = ["spacer:0"]
        return ["", "broken", "[]", "null", '{"version":2}', " ".repeat(65537),
                JSON.stringify(duplicate), JSON.stringify(unknown), JSON.stringify(extra),
                JSON.stringify(missing), JSON.stringify(badId)].map((text, n) => ({tag: String(n), text: text}))
    }
    function test_unknownOrDamagedSettings(data) {
        layout.savedLayout = data.text
        compare(layout.layout, layout.defaults())
        compare(layout.savedLayout, data.text)
    }
    function test_invalidMovesLeaveDraftUntouched() {
        layout.begin()
        const original = layout.serialize()
        for (const args of [["unknown", "above", 0], ["play", "unknown", 0],
                            ["play", "above", -1], ["play", "above", 2], ["play", "above", 0.5]])
            verify(!layout.move(...args))
        compare(layout.serialize(), original)
        layout.cancel()
        verify(!layout.move("play", "above", 0)); verify(!layout.remove("play"))
    }
    function test_boundedNumberOfControls() {
        layout.begin()
        for (let n = 0; n < 64; ++n) verify(layout.move("spacer", "above", n))
        const original = layout.serialize()
        verify(!layout.move("spacer", "above", 64))
        compare(layout.serialize(), original)
    }
}

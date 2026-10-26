// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign/AddonSelectionContracts.js" as Selections

TestCase {
    name: "AddonSelections"
    function playlist() { return Selections.snapshot("playlist", "session-1", [
        {key: "playlist-17", title: "Films"}, {key: "playlist-23", title: "Films"}, {key: "playlist-31", title: "Imported", enabled: false}], []) }
    function subtitles() { return Selections.snapshot("subtitles", "session-2", [
        {key: "file:///private/video.mkv", title: "Selected media"}], [
        {key: "addon-subtitle:host-handle", title: "Subtitle"}, {key: "opensubtitles:host-handle", title: "Other subtitle"}]) }
    function test_packageProjectionContainsNoNativeKeys() {
        const current = subtitles(), data = JSON.stringify(current.publicData)
        verify(!data.includes("file:")); verify(!data.includes("addon-subtitle:")); verify(!data.includes("opensubtitles:"))
        compare(current.publicData.targets[0].ref, "target:0"); compare(current.publicData.assets[0].ref, "asset:0")
    }
    function test_duplicateTitlesKeepExactPlaylistIdentity() {
        compare(Selections.playlistResult(playlist(), {choiceSet: "session-1", selected: ["target:1"]}).key, "playlist-23")
    }
    function test_unknownReadOnlyAndStalePlaylistResultsFail() {
        const current = playlist()
        for (const ref of ["playlist-17", "target:2", "target:7", "__proto__", "constructor"])
            compare(Selections.playlistResult(current, {choiceSet: "session-1", selected: [ref]}), null)
        compare(Selections.playlistResult(current, {choiceSet: "old-session", selected: ["target:0"]}), null)
        compare(Selections.playlistResult(current, {choiceSet: "session-1", selected: ["target:0"], url: "file:///private"}), null)
    }
    function test_newPlaylistNameNeedsExplicitExclusiveResult() {
        const current = playlist()
        const result = Selections.playlistResult(current, {choiceSet: "session-1", selected: [], newName: " New playlist "})
        compare(result.key, ""); compare(result.name, "New playlist")
        for (const name of ["", " ", "x".repeat(257), "Name\nURL", "Name\u0085URL", "Name\u2028URL", "\ud800"])
            compare(Selections.playlistResult(current, {choiceSet: "session-1", selected: [], newName: name}), null)
        compare(Selections.playlistResult(current, {choiceSet: "session-1", selected: ["target:0"], newName: "New"}), null)
    }
    function test_subtitleResultUsesOnlyOfferedHandles() {
        const result = Selections.subtitleResult(subtitles(), {choiceSet: "session-2", assignments: [{targetRef: "target:0", assetRefs: ["asset:1", "asset:0"]}]})
        compare(result.length, 1); compare(result[0].targetKey, "file:///private/video.mkv")
        compare(result[0].assetKeys[0], "opensubtitles:host-handle"); compare(result[0].assetKeys[1], "addon-subtitle:host-handle")
    }
    function test_subtitleForgedDuplicateAndStaleResultsFail() {
        const current = subtitles()
        for (const refs of [["https://example.org/subtitle.srt"], ["asset:9"], ["asset:0", "asset:0"]])
            compare(Selections.subtitleResult(current, {choiceSet: "session-2", assignments: [{targetRef: "target:0", assetRefs: refs}]}), null)
        compare(Selections.subtitleResult(current, {choiceSet: "old-session", assignments: [{targetRef: "target:0", assetRefs: ["asset:0"]}]}), null)
        compare(Selections.subtitleResult(current, {choiceSet: "session-2", assignments: [{targetRef: "target:0", assetRefs: ["asset:0"]}, {targetRef: "target:0", assetRefs: ["asset:1"]}]}), null)
    }
    function test_explicitEmptySubtitleSelectionCanClearAKnownTarget() {
        const result = Selections.subtitleResult(subtitles(), {choiceSet: "session-2", assignments: [{targetRef: "target:0", assetRefs: []}]})
        compare(result.length, 1); compare(result[0].targetKey, "file:///private/video.mkv"); compare(result[0].assetKeys.length, 0)
        compare(Selections.subtitleResult(subtitles(), {choiceSet: "session-2", assignments: []}), null)
    }
    function test_hostSnapshotCopiesValuesAndHasBounds() {
        const targets = [{key: "playlist-17", title: "Films"}]
        const current = Selections.snapshot("playlist", "one", targets, [])
        targets[0].key = "playlist-23"
        compare(Selections.playlistResult(current, {choiceSet: "one", selected: ["target:0"]}).key, "playlist-17")
        compare(Selections.snapshot("playlist", "one", Array.from({length: 201}, (_, i) => ({key: String(i), title: "Item"})), []), null)
        compare(Selections.snapshot("playlist", "one", [{key: "a", title: "One"}, {key: "a", title: "Two"}], []), null)
    }
}

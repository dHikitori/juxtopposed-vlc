// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: test
    name: "AddonPlainText"
    width: 1280; height: 800; visible: true; when: windowShown
    property int serial: 0
    Component { id: textComponent; Text { width: 1200; height: 100; font.pixelSize: 14 } }
    Component { id: playerComponent; RedesignPlayerView { width: 1280; height: 720; started: true } }
    Component { id: miniComponent; RedesignMiniPlayer { width: 391; x: 0; y: 0 } }
    Component { id: panelViewComponent; RedesignAddonView { width: 480 } }
    Component { id: tracksComponent; RedesignTracks { width: 480; heading: "Subtitles" } }
    Component { id: libraryComponent; RedesignLibraryView { width: 480; height: 740; category: 1; showAddRow: false } }
    ListModel { id: tracks }
    ListModel { id: libraryItems }

    function url(kind) {
        const key = String(++serial)
        return kind === "image" ? "image://addon-text-probe/" + key
            : kind === "network" ? "https://addon-text-probe.invalid/" + key + ".png"
            : testTextResources.fileUrl + "?probe=" + key
    }
    function markup() { return ["image", "network", "file"].map(kind => '<img src="' + url(kind) + '">').join(" ") + " Provider title" }
    function collectText(item, value) {
        let result = item.text === value && item.textFormat !== undefined ? [item] : []
        if (item.children) for (const child of item.children) result = result.concat(collectText(child, value))
        return result
    }
    function plain(item, value, minimum) {
        const labels = collectText(item, value)
        verify(labels.length >= minimum)
        for (const label of labels) compare(label.textFormat, Text.PlainText)
    }
    function tooltip(button) {
        compare(button.tooltipTextFormat, Text.PlainText)
        const tip = findChild(button, "redesignToolTip")
        tip.open()
        tryVerify(() => tip.visible)
        compare(tip.contentItem.text, button.text); compare(tip.contentItem.textFormat, Text.PlainText)
        tip.close()
    }
    function noLoads() {
        waitForRendering(test)
        compare(testTextResources.imageRequests, 0)
        compare(testTextResources.networkRequests, 0)
        if (testTextResources.fileObservationAvailable) compare(testTextResources.fileRequests, 0)
    }
    function init() { tracks.clear(); libraryItems.clear(); testTextResources.reset() }

    function test_positiveResourceControls_data() {
        const kinds = ["image", "network"]
        if (testTextResources.fileObservationAvailable) kinds.push("file")
        return kinds.reduce((rows, kind) => rows.concat([{tag: kind + "-auto", kind: kind, format: Text.AutoText},
            {tag: kind + "-rich", kind: kind, format: Text.RichText}]), [])
    }
    function test_positiveResourceControls(data) {
        const label = createTemporaryObject(textComponent, test, {textFormat: data.format, text: '<img src="' + url(data.kind) + '"> Control'})
        verify(label !== null)
        tryVerify(() => data.kind === "image" ? testTextResources.imageRequests > 0
            : data.kind === "network" ? testTextResources.networkRequests > 0 : testTextResources.fileRequests > 0)
    }
    function test_playerTitleAndArtistStayLiteral() {
        const title = markup(), artist = markup()
        const player = createTemporaryObject(playerComponent, test, {mediaTitle: title, artist: artist})
        verify(player !== null)
        plain(player, title, 2); plain(player, artist, 1); noLoads()
    }
    function test_miniPlayerTitleArtistAndFolderStayLiteral() {
        const title = markup(), artist = markup(), folder = markup()
        const mini = createTemporaryObject(miniComponent, test, {mediaTitle: title, artist: artist, folderTitle: folder})
        verify(mini !== null)
        plain(mini, title, 1); plain(mini, artist, 1); plain(mini, folder, 1); noLoads()
    }
    // The Episodes panel is an add-on view: show, episode and file names
    // from the package state stay literal in rows, tabs and tooltips.
    function test_panelTitlesChoicesAndTooltipsStayLiteral() {
        const title = markup(), show = markup(), filename = markup()
        const expand = {command: "org.hakone.episodes/expand"}
        const view = createTemporaryObject(panelViewComponent, test, {registeredCommands: [expand.command]})
        verify(view !== null)
        view.document = {contract: "ui@1", id: "episodes", root: {type: "Page", children: [
            {type: "Heading", text: {bind: "state.show"}},
            {type: "List", id: "episodes", items: {bind: "state.episodes"}, keyField: "key", scroll: false,
             itemTemplate: {type: "MediaRow", number: {bind: "item.number"}, title: {bind: "item.title"}, expanded: {bind: "item.expanded"},
                primaryAction: Object.assign({arguments: {key: {bind: "item.key"}}}, expand),
                children: [{type: "Tabs", id: "versions", items: {bind: "item.versions"}, selected: {bind: "item.version"}, text: "Episode version",
                            action: Object.assign({arguments: {key: {bind: "item.key"}, version: {bind: "item.tab.key"}}}, expand)}]}}]}}
        view.state = {show: show, episodes: [{key: "a", number: "1", title: title, expanded: true,
            versions: [{key: "a", label: filename + ".mkv"}, {key: "b", label: "Other.mkv"}], version: "a"}]}
        tryVerify(() => view.valid && findChild(view, "addonMediaRow_a") !== null && findChild(view, "addonTab_a") !== null)
        tooltip(findChild(view, "addonMediaRow_a")); tooltip(findChild(view, "addonTab_a"))
        plain(view, show, 1); plain(view, title, 1); plain(view, filename + ".mkv", 1); noLoads()
    }
    function test_trackNamesAndTooltipsStayLiteral() {
        const name = markup()
        tracks.append({display: name, checked: false, external: true, sourceName: name, sourceUri: "file:///Source.srt", displayLabel: name})
        const view = createTemporaryObject(tracksComponent, test, {trackModel: tracks})
        verify(view !== null)
        tryVerify(() => findChild(view, "trackChoice_0") !== null)
        const choice = findChild(view, "trackChoice_0"), tip = findChild(choice, "redesignToolTip")
        plain(choice, name, 1)
        choice.forceActiveFocus(Qt.TabFocusReason); tryVerify(() => tip.visible)
        compare(tip.contentItem.text, name + "\nfile:///Source.srt"); compare(tip.contentItem.textFormat, Text.PlainText)
        noLoads()
    }
    function test_libraryTitlesAndTooltipsStayLiteral() {
        const title = markup()
        libraryItems.append({title: title, main_artist: "Artist", fileIsDir: false})
        const view = createTemporaryObject(libraryComponent, test, {mediaModel: libraryItems})
        verify(view !== null)
        tryVerify(() => findChild(view, "libraryMediaItem0") !== null)
        const button = findChild(view, "libraryMediaItem0")
        verify(button.text.includes(title)); tooltip(button); plain(button, button.text, 1); noLoads()
    }
}

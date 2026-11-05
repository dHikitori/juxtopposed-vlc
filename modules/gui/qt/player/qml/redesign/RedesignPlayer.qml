// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Window
import VLC.MainInterface
import VLC.Player
import VLC.Playlist
import VLC.Style
import VLC.Util
import VLC.Dialogs
import VLC.Network
import VLC.Widgets as Widgets

FocusScope {
    id: rootPlayer
    property var pagePrefix: []
    readonly property real positionSliderY: view.positionSliderY
    readonly property ColorContext colorContext: ColorContext { palette: VLCStyle.palette; colorSet: ColorContext.Window }
    readonly property bool resumeAvailable: Player.canRestorePlayback
    property int queueTab: 0
    property string subtitleReplacementTicket: ""
    function openTrackDialog(page) {
        if (page === "audio") { DialogsProvider.loadAudioFile(); return }
        if (subtitleReplacementTicket.length) return
        const registry = typeof AddonUiRegistry === "undefined" ? null : AddonUiRegistry
        subtitleReplacementTicket = registry ? registry.executeReplacement("actions.subtitles.open@1", {}) : ""
        if (!subtitleReplacementTicket.length) DialogsProvider.loadSubtitlesFile()
    }
    Connections {
        target: typeof AddonUiRegistry === "undefined" ? null : AddonUiRegistry
        function onReplacementFinished(ticket, success) {
            if (ticket !== rootPlayer.subtitleReplacementTicket) return
            rootPlayer.subtitleReplacementTicket = ""
            if (!success) DialogsProvider.loadSubtitlesFile()
        }
    }
    RedesignAddonWindow {
        id: addonWindow
        registry: typeof AddonUiRegistry === "undefined" ? null : AddonUiRegistry
        scaleFactor: view.scaleFactor; accentColor: view.accentColor; lightTheme: view.lightTheme
    }
    RedesignAddonDiscoveryWindow {
        id: discoveryWindow
        backend: typeof AddonDiscovery === "undefined" ? null : AddonDiscovery
        settingsBackend: typeof AddonManager === "undefined" ? null : AddonManager
        scaleFactor: view.scaleFactor; accentColor: view.accentColor; lightTheme: view.lightTheme
        onManageRequested: DialogsProvider.prefsPageDialog("extensions")
    }
    // The projection retains folder state while its lazy source is unloaded.
    PlaylistTreeModel { id: queueTreeModel; treeEnabled: MainCtx.playlistTree }
    property int requestedLibraryCategory: -1
    property var requestedNetworkPlaylist: null
    // The app owns the series model, so add-on panels can read it through
    // media.siblings. A test without the app uses a local one.
    SeriesModel { id: localSeriesModel; objectName: "seriesEpisodeModel" }
    readonly property var seriesModel: typeof redesignSeriesModel !== "undefined" && redesignSeriesModel ? redesignSeriesModel : localSeriesModel
    readonly property var addonPanelRegistry: typeof AddonUiRegistry === "undefined" ? null : AddonUiRegistry
    readonly property var mediaSiblings: typeof redesignMediaSiblings === "undefined" ? null : redesignMediaSiblings
    // The series is found only while an enabled panel can read it.
    readonly property bool seriesWanted: !!addonPanelRegistry && addonPanelRegistry.siblingsWanted
    onSeriesWantedChanged: seriesContextTimer.restart()
    property string panelMediaUrl: ""
    EpisodeMetadata {
        id: episodeMetadata
        objectName: "seriesEpisodeMetadata"
        ctx: MainCtx
        onTitleReady: (url, title, source) => seriesModel.setEpisodeTitle(url, title, source)
    }
    Connections {
        target: seriesModel
        function onWatchStatesRequested(urls, serial) { episodeMetadata.readCached(urls) }
    }
    Timer {
        id: seriesContextTimer; interval: 0
        onTriggered: {
            if (rootPlayer.seriesWanted) {
                episodeMetadata.request(Player.url, Player.seriesContext)
                seriesModel.setCurrent(Player.url, Player.seriesContext)
            }
            if (!rootPlayer.addonPanelRegistry) return
            // A new media gets one context call for each panel; a metadata
            // change only refreshes the open panels.
            const url = String(Player.url || "")
            if (url !== rootPlayer.panelMediaUrl) {
                rootPlayer.panelMediaUrl = url
                rootPlayer.addonPanelRegistry.setPanelMedia({
                    kind: !url.length ? "none" : Player.videoTracks.count > 0 ? "video" : Player.audioTracks.count > 0 ? "audio" : "none",
                    durationMs: Player.length.valid() ? Player.length.toSeconds() * 1000 : 0})
            } else rootPlayer.addonPanelRegistry.refreshPanels()
        }
    }
    Connections {
        target: rootPlayer.addonPanelRegistry
        ignoreUnknownSignals: true
        // A queue button (queue.actions@1) opens its package's player panel
        // in place of the queue.
        function onPanelRequested(panel) { if (view.panelOpen !== panel) view.togglePanel(panel) }
    }
    property string seriesFeedUrl: seriesModel.feed.toString()
    onSeriesFeedUrlChanged: seriesFeed.setSources(seriesFeedUrl ? [{mrl: seriesFeedUrl}] : [])
    NetworkPlaylistModel {
        id: seriesFeed
        ctx: MainCtx
        active: rootPlayer.seriesFeedUrl.length > 0
        onModelReset: seriesFeedUpdate.restart()
        onDataChanged: seriesFeedUpdate.restart()
        onLoadingChanged: seriesFeedUpdate.restart()
    }
    Timer {
        id: seriesFeedUpdate
        interval: 0
        onTriggered: {
            if (!rootPlayer.seriesFeedUrl) return
            const rows = []
            for (let i = 0; i < seriesFeed.count; ++i) {
                const row = seriesFeed.getDataAt(i)
                row.sourceIndex = i
                rows.push(row)
            }
            seriesModel.setFeedEpisodes(Qt.resolvedUrl(rootPlayer.seriesFeedUrl), rows)
            seriesModel.setFeedStatus(seriesFeed.loading, !seriesFeed.loading && !seriesFeed.expanded)
            seriesModel.setProgress(Player.url, Player.position)
        }
    }
    Loader {
        active: MainCtx.mediaLibraryAvailable
        source: active ? "qrc:///qt/qml/VLC/Player/RedesignSeriesMetadata.qml" : ""
        onLoaded: { item.seriesModel = seriesModel; seriesModel.loadStates() }
    }
    // Effects that a click in an add-on panel asked for. The siblings
    // projection checks them; the app runs them on its own paths.
    Connections {
        target: rootPlayer.addonPanelRegistry
        ignoreUnknownSignals: true
        function onPanelEffect(sessionId, packageId, effect, value) {
            if (rootPlayer.mediaSiblings) rootPlayer.mediaSiblings.runEffect(packageId, effect, value)
        }
    }
    Connections {
        target: rootPlayer.mediaSiblings
        ignoreUnknownSignals: true
        function onChanged() { if (rootPlayer.addonPanelRegistry) rootPlayer.addonPanelRegistry.refreshPanels() }
        function onPlayRequested(url, feedIndex, version, fromStart) {
            if (feedIndex >= 0) seriesFeed.playEpisode(feedIndex, version > 0, fromStart)
            else Player.playSeriesEpisode(url, fromStart)
        }
        function onRescanRequested(chooseFolder) {
            if (rootPlayer.seriesFeedUrl) { seriesFeed.setSources([]); seriesFeed.setSources([{mrl: rootPlayer.seriesFeedUrl}]); return }
            if (!chooseFolder) { seriesModel.refresh(); return }
            const folder = DialogsProvider.selectLibraryFolder(seriesModel.folder)
            if (folder.toString()) seriesModel.refresh(folder)
        }
        function onIdentifyRequested() { identityDialog.begin() }
        function onSourceRequested(url) { Qt.openUrlExternally(url) }
    }
    Connections {
        target: DialogsProvider
        function onShowRedesignNetworkPlaylist(urls, name) {
            rootPlayer.requestedNetworkPlaylist = {urls: Array.from(urls), name: name}
            rootPlayer.openLibraryCategory(3)
        }
    }
    function beginCustomization() {
        if (MainCtx.minimalView) MainCtx.minimalView = false
        mediaMenu.dismiss(); tracksPopup.close(); customChoice.close()
        view.settingsPage = ""; view.queueOpen = false; view.panelOpen = ""
        view.libraryOpen = false; view.libraryExpanded = false
        customizationPreferences.discard()
        customizationEditor.begin()
    }
    function openLibraryCategory(category) {
        if (category !== 3) requestedNetworkPlaylist = null
        requestedLibraryCategory = category
        view.libraryOpen = true
        Qt.callLater(applyLibraryCategory)
    }
    function applyLibraryCategory() {
        const library = view.libraryItem ? view.libraryItem.contentItem : null
        if (requestedLibraryCategory < 0 || !library) return
        if (requestedLibraryCategory === 3 && requestedNetworkPlaylist && typeof library.openNetworkPlaylist === "function") {
            library.openNetworkPlaylist(requestedNetworkPlaylist.urls, requestedNetworkPlaylist.name)
            requestedNetworkPlaylist = null
        } else library.resetLocation(requestedLibraryCategory)
        requestedLibraryCategory = -1
    }
    function showCustomChoice(action, anchor) {
        customChoice.mode = action
        if (action === "speed") {
            customChoice.entries = [.25, .5, .75, 1, 1.25, 1.5, 2].map(value => ({action: String(value), label: value + "×"}))
                .concat([{action: "custom", label: qsTr("Custom speed…")}])
        } else if (action === "zoom") {
            customChoice.entries = [{action: "0.25", label: qsTr("1:4 Quarter")}, {action: "0.5", label: qsTr("1:2 Half")},
                {action: "1", label: qsTr("1:1 Original")}, {action: "2", label: qsTr("2:1 Double")},
                {action: "-1", label: qsTr("Fit Inside Window")}, {action: "-2", label: qsTr("Fill Window")},
                {action: "-3", label: qsTr("Fit Window Width")}, {action: "-4", label: qsTr("Fit Window Height")}]
        } else {
            const supported = playerPreferences.describe("d3d11-hdr-mode").choices || []
            customChoice.entries = [{action: "auto", label: qsTr("Auto")}, {action: "never", label: qsTr("Off")},
                {action: "always", label: "HDR10"}, {action: "hdr10plus", label: "HDR10+"},
                {action: "dolbyvision", label: "Dolby Vision"}, {action: "hlg", label: "HLG"}]
                .map(row => Object.assign({}, row, {enabled: supported.some(choice => choice.value === row.action)}))
        }
        customChoice.toggleFrom(anchor)
    }
    function runCustomAction(action, anchor) {
        if (["speed", "hdr", "zoom"].includes(action)) { showCustomChoice(action, anchor); return }
        if (["forward10", "backward10", "forward15", "backward15"].includes(action)) {
            if (!view.seekable) return
            const seconds = Number(action.endsWith("10") ? 10 : 15) * (action.startsWith("backward") ? -1 : 1)
            Player.jumpSeconds(seconds)
        } else if (action === "stop") MainPlaylistController.stop()
        else if (action === "repeat") MainPlaylistController.toggleRepeatMode()
        else if (action === "shuffle") MainPlaylistController.toggleRandom()
        else if (action === "record") Player.toggleRecord()
        else if (action === "snapshot") Player.snapshot()
        else if (action === "pip") { view.libraryOpen = true; view.libraryExpanded = true; view.miniPlayerVisible = true }
        else if (action === "rightSidebar") { view.settingsPage = ""; view.queueOpen = !view.queueOpen }
        else if (action === "quit") DialogsProvider.quit()
        else if (action === "preferences") DialogsProvider.prefsDialog()
        else if (action === "home" || action === "playlists") openLibraryCategory(action === "home" ? 0 : 4)
        else if (action === "properties") DialogsProvider.mediaInfoDialog()
        else if (action === "replay") Player.replay()
        else if (action === "episodes") view.togglePanel(view.layoutModel.episodesPanel.slice(6))
        else if (action === "chapters" || action === "bookmarks") {
            rootPlayer.queueTab = action === "chapters" ? 2 : 1
            view.settingsPage = ""; view.queueOpen = true
            Qt.callLater(() => { if (view.queueItem) view.queueItem.tab = rootPlayer.queueTab })
        } else if (action === "bookmark") DialogsProvider.addBookmark()
        else if (action === "favorite" && favoriteLoader.item) favoriteLoader.item.toggle()
    }
    readonly property string mediaFolderUrl: {
        const url = String(Player.url || "")
        return url.startsWith("file:") ? url.substring(0, url.lastIndexOf("/") + 1) : ""
    }
    function mediaFolderName() {
        const name = mediaFolderUrl.replace(/\/+$/, "").split("/").pop()
        try { return decodeURIComponent(name) } catch (error) { return name }
    }
    Connections {
        target: Player
        function onInputChanged() { view.refreshSeekPreview(); view.cancelTimeEdit(); gotoTime.reject(); seriesContextTimer.restart() }
        function onCurrentMetaChanged() { seriesContextTimer.restart() }
        function onPositionChanged() { seriesModel.setProgress(Player.url, Player.position) }
    }
    Connections {
        target: DialogsProvider
        function onShowToolbarEditorDialog() { rootPlayer.beginCustomization() }
        function onShowRedesignGotoTime() {
            if (gotoTime.visible) gotoTime.reject()
            else {
                mediaMenu.dismiss(); tracksPopup.close(); customChoice.close()
                gotoTime.begin(Player.time.toSeconds())
            }
        }
        function onShowRedesignBookmarks() {
            rootPlayer.queueTab = 1
            view.settingsPage = ""; view.queueOpen = true
            Qt.callLater(() => { if (view.queueItem) view.queueItem.tab = 1 })
        }
    }
    function clockText(tick) {
        if (!tick.valid()) return "00:00:00"
        const parts = tick.formatHMS(0).split(":")
        return parts.length === 2 ? "00:" + parts.join(":") : parts.join(":")
    }
    WheelToVLCConverter {
        id: volumeWheel
        function adjust(steps) {
            if (steps === 0) return
            Player.muted = false
            if (steps > 0) Player.setVolumeUp(steps)
            else Player.setVolumeDown(-steps)
        }
        onWheelUpDown: steps => adjust(steps)
        onWheelLeftRight: steps => adjust(steps)
    }
    // Each wheel step over the seek bar is a short jump (short-jump-size).
    WheelToVLCConverter {
        id: seekWheel
        function jump(steps) {
            for (let i = 0; i < Math.abs(steps); ++i) {
                if (steps > 0) Player.jumpFwd()
                else Player.jumpBwd()
            }
        }
        onWheelUpDown: steps => jump(steps)
        onWheelLeftRight: steps => jump(-steps)
    }
    RedesignSleepTimer { id: sleepTimer; onTriggered: MainPlaylistController.pause() }
    RedesignIdentityDialog {
        id: identityDialog
        objectName: "identityDialog"
        parent: view
        scaleFactor: view.scaleFactor; lightTheme: view.lightTheme; accentColor: view.accentColor
        videoBackdropContent: rootPlayer.menuBackdropContent
        backend: typeof AddonIdentityChoice !== "undefined" ? AddonIdentityChoice : null
    }
    RedesignGotoTimeDialog {
        id: gotoTime
        objectName: "gotoTimeDialog"
        parent: view
        scaleFactor: view.scaleFactor; lightTheme: view.lightTheme; accentColor: view.accentColor
        videoBackdropContent: rootPlayer.menuBackdropContent
        seekable: Player.isStarted && Player.seekable
        durationSeconds: Player.length.valid() ? Player.length.toSeconds() : 0
        onVisibleChanged: DialogsProvider.setRedesignGotoTimeVisible(visible)
        Component.onDestruction: DialogsProvider.setRedesignGotoTimeVisible(false)
        onSeekRequested: seconds => Player.jumpToSeconds(seconds)
    }
    component SurfaceBackdrop: RedesignSurfaceBackdrop {
        sourceItem: view.backdropSource
        scaleFactor: view.scaleFactor
    }
    // Menus and track popovers are outside view's visual tree. Include the
    // player panels below them without including the menu in its own capture.
    property Component menuBackdropContent: Component { SurfaceBackdrop { sourceItem: view } }
    RedesignPreferences { id: playerPreferences; ctx: MainCtx; autoSave: true; editPlayerPreset: true }
    Loader {
        id: favoriteLoader
        active: MainCtx.mediaLibraryAvailable && !!view.layoutModel.location("favorite")
        source: active ? "qrc:///qt/qml/VLC/Player/RedesignPlayerFavorite.qml" : ""
        onLoaded: {
            item.mediaUrl = Qt.binding(() => Player.url)
            item.errorOccurred.connect(message => DialogsProvider.messageDialog(message, qsTr("Hakone Media Player")))
        }
    }
    RedesignActionMenu {
        id: customChoice
        property string mode: ""
        parent: view
        videoBackdropContent: rootPlayer.menuBackdropContent
        scaleFactor: view.scaleFactor; lightTheme: view.lightTheme; accentColor: view.accentColor
        checkedAction: {
            playerPreferences.revision
            if (mode === "speed") return String(Math.abs(Player.rate))
            if (mode === "hdr") return String(playerPreferences.value("d3d11-hdr-mode"))
            return String(playerPreferences.value("autoscale") ? -Math.max(1, Number(playerPreferences.value("fit")) || 1) : playerPreferences.value("zoom"))
        }
        onActionRequested: action => {
            if (mode === "speed") {
                if (action === "custom") { view.queueOpen = false; view.settingsPage = "playback" }
                else Player.rate = (Player.rate < 0 ? -1 : 1) * Number(action)
            } else if (mode === "hdr") playerPreferences.stage("d3d11-hdr-mode", action)
            else {
                const value = Number(action)
                if (value < 0) { playerPreferences.stage("fit", -value); playerPreferences.stage("autoscale", true) }
                else { playerPreferences.stage("zoom", value); playerPreferences.stage("fit", 0); playerPreferences.stage("autoscale", false) }
            }
        }
    }
    Binding {
        target: MainCtx; property: "useGlobalShortcuts"; value: false
        when: customChoice.visible
        restoreMode: Binding.RestoreBindingOrValue
    }
    Shortcut {
        sequences: ["Menu", "Shift+F10"]
        context: Qt.WindowShortcut
        enabled: {
            const window = rootPlayer.Window.window
            const item = window ? window.activeFocusItem : null
            return MainCtx.useGlobalShortcuts && MainCtx.preferHotkeys && !(item instanceof TextInput) && !(item instanceof TextEdit)
        }
        onActivated: {
            mediaMenu.dismiss()
            tracksPopup.close()
            const item = rootPlayer.Window.window.activeFocusItem || view
            DialogsProvider.popupPlayerMenu(item.mapToGlobal(item.width / 2, item.height / 2))
        }
    }
    Shortcut {
        sequences: { playerPreferences.revision; return String(playerPreferences.value("key-toggle-library") || "").split("\t").filter(sequence => sequence.length > 0) }
        context: Qt.WindowShortcut
        enabled: MainCtx.useGlobalShortcuts
        onActivated: { view.libraryOpen = !view.libraryOpen; view.libraryExpanded = false }
    }
    Binding {
        target: MainCtx
        property: "preferHotkeys"
        value: MainCtx.useGlobalShortcuts && !view.controlsLocked && !tracksPopup.visible && !mediaMenu.visible && !shortcutEditor.visible && !addonWindow.visible && !discoveryWindow.visible
        restoreMode: Binding.RestoreBindingOrValue
    }
    Binding { target: Player; property: "seekPreviewEnabled"; value: MainCtx.redesignPlayerPreferences.seekPreview }
    Connections {
        target: Player.audioVisualization
        function onDataChanged() { reconcileVisualization.restart() }
    }
    Timer {
        id: reconcileVisualization
        interval: 0
        // Native variable callbacks can run inside a preference save. Wait for
        // that transaction to finish before reconciling an external choice.
        onTriggered: {
            if (!Player.audioWaveformEnabled) return
            const model = Player.audioVisualization
            for (let i = 0; i < model.rowCount(); ++i) {
                const value = String(model.choiceValue(i))
                if (!value || !model.data(model.index(i, 0), Qt.CheckStateRole)) continue
                const effect = ["scope", "spectrum", "spectrometer", "vuMeter", "dummy"].includes(value)
                if (!playerPreferences.stage("qt-audio-waveform", false)) return
                if (effect && !playerPreferences.stage("effect-list", value)) return
                if (playerPreferences.stage("audio-visual", effect ? "visual" : value)) playerPreferences.save()
                return
            }
        }
    }
    Binding {
        target: MainCtx.videoSurfaceProvider
        property: "subtitleArea"
        value: view.subtitleArea
        when: !!MainCtx.videoSurfaceProvider
    }
    Binding { target: Player; property: "audioWaveformEnabled"; value: !!MainCtx.redesignPlayerPreferences.audioWaveform }
    Binding { target: Player; property: "subtitleTimelineEnabled"; value: MainCtx.redesignPlayerPreferences.subtitleTimeline }

    Component.onCompleted: {
        seriesContextTimer.restart()
        MainCtx.playqueuePanel.docked = true
        view.queueOpen = MainCtx.playqueuePanel.visible
    }

    RedesignPlayerView {
        id: view
        objectName: "redesignPlayer"
        anchors.fill: parent
        focus: true
        scaleFactor: VLCStyle.scale
        lightTheme: !VLCStyle.palette.isDark
        accentColor: MainCtx.redesignAccent
        playerLayout: MainCtx.redesignPlayerPreferences.layout
        customMenuOpen: customChoice.visible || mediaMenu.visible
        addonPanels: { const registry = rootPlayer.addonPanelRegistry; return registry ? registry.panels : [] }
        customControlStates: ({
            speed: {enabled: Player.ratechangable},
            forward10: {enabled: view.seekable}, backward10: {enabled: view.seekable},
            forward15: {enabled: view.seekable}, backward15: {enabled: view.seekable},
            stop: {enabled: Player.isStarted},
            repeat: {checked: MainPlaylistController.repeatMode !== PlaylistController.PLAYBACK_REPEAT_NONE,
                badge: MainPlaylistController.repeatMode === PlaylistController.PLAYBACK_REPEAT_CURRENT ? "1" : "",
                description: MainPlaylistController.repeatMode === PlaylistController.PLAYBACK_REPEAT_CURRENT ? qsTr("Repeat this item")
                    : MainPlaylistController.repeatMode === PlaylistController.PLAYBACK_REPEAT_NONE ? qsTr("Repeat off") : qsTr("Repeat all")},
            shuffle: {checked: MainPlaylistController.random}, record: {enabled: Player.isStarted, checked: Player.recording},
            snapshot: {enabled: Player.isStarted && view.hasVideo}, pip: {enabled: Player.isStarted && view.hasVideo},
            hdr: {enabled: Player.hasVideoOutput && playerPreferences.describe("d3d11-hdr-mode").kind !== undefined},
            zoom: {enabled: Player.hasVideoOutput}, home: {enabled: MainCtx.mediaLibraryAvailable},
            playlists: {enabled: MainCtx.mediaLibraryAvailable},
            favorite: {enabled: Player.isStarted && !!favoriteLoader.item && !favoriteLoader.item.busy,
                checked: !!favoriteLoader.item && favoriteLoader.item.favorite,
                icon: favoriteLoader.item && favoriteLoader.item.favorite ? "favorite-active" : "favorite"},
            bookmark: {enabled: Player.isStarted && MainCtx.mediaLibraryAvailable}, bookmarks: {enabled: MainCtx.mediaLibraryAvailable},
            chapters: {enabled: Player.isStarted && Player.hasChapters}, properties: {enabled: Player.isStarted}, replay: {enabled: view.seekable},
            thumbnail: {artwork: Player.artwork}, fileName: {value: Player.name}, artist: {value: Player.artist},
            folderName: {value: rootPlayer.mediaFolderName()}, playlistName: {value: MainPlaylistController.currentItem.playlistName}
        })
        onCustomActionRequested: (action, anchor) => rootPlayer.runCustomAction(action, anchor)
        playing: Player.playingState === Player.PLAYING_STATE_PLAYING
        started: Player.isStarted
        hasVideo: MainCtx.hasEmbededVideo
        audioOnly: Player.audioTracks.count > 0 && Player.videoTracks.count === 0
        seekable: started && Player.seekable
        pausable: Player.pausable
        hasPrevious: MainPlaylistController.hasPrev
        hasNext: MainPlaylistController.hasNext
        muted: Player.muted
        fullscreen: MainCtx.intfMainWindow.visibility === Window.FullScreen
        minimalView: MainCtx.minimalView
        autoHideMode: MainCtx.redesignPlayerPreferences.autoHide
        controlsHideTimeout: MainCtx.redesignPlayerPreferences.hideTimeout
        cursorHideTimeout: MainCtx.mouseHideTimeout
        showFullscreenControls: MainCtx.redesignPlayerPreferences.fullscreenControls
        fullscreenControlsOpacity: MainCtx.pinOpacity
        fullscreenControlsSensitivity: MainCtx.redesignPlayerPreferences.fullscreenSensitivity
        showSubtitleTimeline: MainCtx.redesignPlayerPreferences.subtitleTimeline
        showSeekPreview: MainCtx.redesignPlayerPreferences.seekPreview
        audioWaveformPeaks: Player.audioWaveformPeaks
        audioWaveformEnabled: Player.audioWaveformEnabled || Player.audioWaveformAutomatic
        subtitleTimelineCues: Player.subtitleTimelineCues
        subtitleTimelineState: Player.subtitleTimelineState
        onPrecisePositionUpdatesRequested: requested => {
            if (requested) Player.requestAddPrecisePositionTimer()
            else Player.requestRemovePrecisePositionTimer()
        }
        subtitleDelayMS: Player.subtitleDelayMS
        durationSeconds: started ? Player.lengthSeconds : 0
        onSubtitleDelayRequested: milliseconds => playerPreferences.stage("redesign-subtitle-delay", milliseconds)
        maximized: MainCtx.intfMainWindow.visibility === Window.Maximized
        showWindowButtons: MainCtx.clientSideDecoration && !fullscreen
        position: started ? Player.position : 0
        volume: Player.volume
        maxVolume: MainCtx.maxVolume / 100
        buffering: Player.buffering
        statistics: Player.playbackStatistics
        mediaTitle: Player.title || Player.name || qsTr("Hakone Media Player")
        locationTitle: view.libraryItem ? view.libraryItem.locationTitle : qsTr("Home")
        timeText: started ? rootPlayer.clockText(Player.time) : "00:00:00"
        lengthText: started ? rootPlayer.clockText(Player.length) : "00:00:00"
        artist: Player.artist
        mediaFolderTitle: rootPlayer.mediaFolderName()
        canBrowseMedia: MainCtx.mediaLibraryAvailable
        artwork: Player.artwork
        resumeAvailable: rootPlayer.resumeAvailable
        resumeTimeText: Player.playbackRestorePosition >= 0 && Player.lengthSeconds > 0
            ? rootPlayer.clockText(Player.length.scale(Player.playbackRestorePosition)) : ""
        timeForPosition: value => rootPlayer.clockText(Player.length.scale(value))
        seekPreviewImage: Player.seekPreview
        videoBackdropContent: Component { SurfaceBackdrop { } }
        artworkToolbarBackdropContent: Component {
            SurfaceBackdrop {
                sourceItem: view.artworkBackdropSource
                nativeVideo: false
                cornerRadius: 0
            }
        }
        seekThumbnailContent: GraphicsInfo.shaderType === GraphicsInfo.RhiShader ? shaderSeekThumbnail : softwareSeekThumbnail
        Component {
            id: shaderSeekThumbnail
            Widgets.ImageExt {
                objectName: "seekThumbnailImage"
                readonly property bool preparedFrame: false
                source: view.seekPreviewImage; cache: false
                // Keep the shader's soft edge under the clip, without a dark outline.
                anchors.fill: parent; anchors.margins: -0.5
                radius: view.scaleFactor * 4 + 0.5
                // Thumbnail export can round the fitted height down by one pixel.
                fillMode: sourceTextureProviderItem.implicitHeight > 0 && parent && parent.width > 0
                    && Math.abs(sourceTextureProviderItem.implicitHeight - sourceTextureProviderItem.implicitWidth * parent.height / parent.width) <= 1.01
                    ? Image.Stretch : Image.PreserveAspectFit
            }
        }
        Component {
            id: softwareSeekThumbnail
            RedesignSeekThumbnail {
                frameSource: view.seekPreviewImage
                scaleFactor: view.scaleFactor
            }
        }
        seekPreviewImagePosition: Player.seekPreviewPosition
        onSeekPreviewRequested: value => Player.requestSeekPreview(value)
        onSeekPreviewEnded: Player.cancelSeekPreview()
        onTogglePlayback: MainPlaylistController.empty ? DialogsProvider.simpleOpenDialog() : MainPlaylistController.togglePlayPause()
        onPrevious: MainPlaylistController.prevOrReset()
        onNext: MainPlaylistController.next()
        onSeekRequested: value => Player.jumpToPos(value)
        onSeekToSecondsRequested: seconds => Player.jumpToSeconds(seconds)
        onSeekDragStarted: Player.beginSeekDrag()
        onSeekDragFinished: (value, commit) => Player.finishSeekDrag(value, commit)
        onVolumeRequested: value => { Player.muted = false; Player.volume = value }
        onVolumeWheelRequested: event => {
            // Match the standard volume widget's continuous touchpad scrolling.
            const pixels = Math.abs(event.pixelDelta.x) > Math.abs(event.pixelDelta.y)
                         ? event.pixelDelta.x : event.pixelDelta.y
            if (Math.abs(pixels) % 120 > 0) {
                Player.muted = false
                Player.volume = Math.max(0, Math.min(2, Player.volume + (event.inverted ? -pixels : pixels) * 0.001))
            } else {
                volumeWheel.qmlWheelEvent(event)
            }
        }
        onSeekWheelRequested: event => seekWheel.qmlWheelEvent(event)
        onMuteRequested: Player.muted = !Player.muted
        onFullscreenRequested: MainCtx.toggleInterfaceFullScreen()
        onMinimizeRequested: MainCtx.requestInterfaceMinimized()
        onMaximizeRequested: maximized ? MainCtx.requestInterfaceNormal() : MainCtx.requestInterfaceMaximized()
        onCloseRequested: DialogsProvider.quit()
        onOpenRequested: DialogsProvider.simpleOpenDialog()
        onPreferencesRequested: DialogsProvider.prefsDialog()
        onFullLibraryRequested: { libraryOpen = true; libraryExpanded = !libraryExpanded }
        onMediaFolderRequested: {
            libraryOpen = true
            Qt.callLater(() => {
                const library = view.libraryItem ? view.libraryItem.item : null
                if (library && rootPlayer.mediaFolderUrl) library.openFolder(rootPlayer.mediaFolderUrl, Player.videoTracks.count > 0 ? 1 : 2)
            })
        }
        onArtistRequested: {
            const library = view.libraryItem ? view.libraryItem.item : null
            if (library && Player.artist) library.openArtistByName(Player.artist)
        }
        trackPopup: tracksPopup
        addonVisualizerContent: Component {
            AddonVisualizerSurface {
                controller: typeof AddonVisualizers === "undefined" ? null : AddonVisualizers
                backgroundColor: view.theme.background
                foregroundColor: view.theme.bright
                accentColor: view.theme.accent
                reducedMotion: view.theme.reducedMotion
            }
        }
        addonVisualizerActive: typeof AddonVisualizers !== "undefined" && AddonVisualizers.active
        onMenuRequested: anchor => mediaMenu.toggleFrom(anchor, view)
        onContextMenuRequested: globalPosition => audioContextMenu.popup(globalPosition)
        onResumeRequested: resume => {
            if (resume) Player.restorePlaybackPos()
            else Player.acknowledgeRestoreCallback()
        }
        onKeyPressed: event => {
            if (!event.accepted && event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab)
                MainCtx.sendHotkey(event.key, event.modifiers)
        }
        titleBarDragContent: Component {
            Loader {
                active: MainCtx.clientSideDecoration && !view.fullscreen
                source: "qrc:///qt/qml/VLC/Widgets/CSDTitlebarTapNDrapHandler.qml"
            }
        }
        videoContent: Component {
            VideoSurface {
                id: video
                videoSurfaceProvider: MainCtx.videoSurfaceProvider
                // Clear the chrome background where the compositor displays video.
                renderingEnabled: true
                visible: MainCtx.hasEmbededVideo
                focus: visible
                cursorShape: view.videoCursorVisible ? Qt.ArrowCursor : Qt.BlankCursor
                onMouseMoved: view.revealCursor()
                onMousePressed: { view.cancelTimeEdit(); view.revealCursor() }
                onMouseReleased: view.revealCursor()
            }
        }
        Connections {
            target: MainCtx.playqueuePanel
            function onVisibleChanged() { view.queueOpen = MainCtx.playqueuePanel.visible && MainCtx.playqueuePanel.docked }
        }
        onQueueOpenChanged: if (MainCtx.playqueuePanel.docked) MainCtx.playqueuePanel.visible = queueOpen
        libraryContent: Component {
            Loader {
                asynchronous: true
                source: MainCtx.mediaLibraryAvailable ? "qrc:///qt/qml/VLC/Player/RedesignLibrary.qml" : ""
                onLoaded: {
                    item.scaleFactor = Qt.binding(() => view.scaleFactor)
                    item.lightTheme = Qt.binding(() => view.lightTheme)
                    item.accentColor = Qt.binding(() => view.accentColor)
                    item.videoBackdropContent = rootPlayer.menuBackdropContent
                    item.expanded = Qt.binding(() => view.libraryExpanded)
                    item.fullLibraryRequested.connect(() => { view.libraryExpanded = !view.libraryExpanded })
                    item.discoveryRequested.connect(source => discoveryWindow.openSource(source))
                    rootPlayer.applyLibraryCategory()
                }
                Loader {
                    id: unavailableLibrary
                    anchors.fill: parent
                    active: !MainCtx.mediaLibraryAvailable
                    asynchronous: true
                    sourceComponent: RedesignLibraryUnavailable {
                        videoBackdropContent: rootPlayer.menuBackdropContent
                        anchors.fill: parent
                        expanded: view.libraryExpanded
                        scaleFactor: view.scaleFactor; lightTheme: view.lightTheme; accentColor: view.accentColor
                        onAddRequested: category === 3 ? DialogsProvider.openNetDialog() : DialogsProvider.simpleOpenDialog()
                        onFilesRequested: DialogsProvider.simpleOpenDialog()
                        onFolderRequested: DialogsProvider.PLOpenDir()
                        onFullLibraryRequested: view.libraryExpanded = !view.libraryExpanded
                        onPreferencesRequested: DialogsProvider.prefsPageDialog("library")
                        onDiscoveryRequested: source => discoveryWindow.openSource(source)
                    }
                    onLoaded: rootPlayer.applyLibraryCategory()
                }
                readonly property var contentItem: item || unavailableLibrary.item
                property string heading: contentItem ? contentItem.heading : qsTr("Home")
                property string locationTitle: contentItem && contentItem.breadcrumbs.length ? contentItem.breadcrumbs[contentItem.breadcrumbs.length - 1].label : qsTr("Home")
            }
        }
        queueContent: Component {
            RedesignQueuePanel {
                videoBackdropContent: rootPlayer.menuBackdropContent
                tab: rootPlayer.queueTab
                onTabChanged: rootPlayer.queueTab = tab
                scaleFactor: view.scaleFactor; lightTheme: view.lightTheme; accentColor: view.accentColor
                onCloseRequested: view.queueOpen = false
                queueContent: Component { RedesignQueue {
                    singleClickActivate: !!MainCtx.redesignPlayerPreferences.singleClickActivate
                videoBackdropContent: rootPlayer.menuBackdropContent
                scaleFactor: view.scaleFactor; lightTheme: view.lightTheme; accentColor: view.accentColor
                queueModel: PlaylistListModel { id: queueModel; playlist: MainPlaylistController.playlist }
                hierarchyModel: queueTreeModel
                Component.onCompleted: queueTreeModel.sourceModel = queueModel
                itemCount: queueModel.count
                currentIndex: queueModel.currentIndex
                totalDurationText: summaryDuration(queueModel.duration)
                sortChoices: MainPlaylistController.sortKeyTitleList
                sortKey: MainPlaylistController.sortKey
                sortDescending: MainPlaylistController.sortOrder === PlaylistController.SORT_ORDER_DESC
                random: MainPlaylistController.random
                repeating: MainPlaylistController.repeatMode !== PlaylistController.PLAYBACK_REPEAT_NONE
                onCloseRequested: view.queueOpen = false
                onAddRequested: DialogsProvider.simpleOpenDialog(false)
                onSaveRequested: DialogsProvider.savePlayingToPlaylist()
                onClearRequested: MainPlaylistController.clear()
                onShuffleRequested: MainPlaylistController.toggleRandom()
                onRepeatRequested: MainPlaylistController.toggleRepeatMode()
                onSortRequested: (key, descending) => MainPlaylistController.sort(key, descending ? PlaylistController.SORT_ORDER_DESC : PlaylistController.SORT_ORDER_ASC)
                onPlayRequested: index => MainPlaylistController.goTo(index, true)
                onRemoveRequested: index => queueModel.removeItems([index])
                onMoveRequested: (from, to) => queueModel.moveItemsPre([from], to)
                onLibraryDropRequested: source => source.libraryView.enqueueRequested(source.mediaIndex)
                onFilesDropped: urls => MainPlaylistController.append(Array.from(urls), false)
                } }
            }
        }
        settingsContent: Component {
            RedesignSettings {
                videoBackdropContent: rootPlayer.menuBackdropContent
                preferenceStore: playerPreferences
                Component.onCompleted: playerPreferences.refresh()
                scaleFactor: view.scaleFactor; lightTheme: view.lightTheme; accentColor: view.accentColor
                page: view.settingsPage
                showStatistics: view.showStatistics
                showSubtitleTimeline: view.showSubtitleTimeline
                subtitleTimelineState: view.subtitleTimelineState
                sleepTimerMinutes: sleepTimer.minutes
                onStatisticsRequested: enabled => view.showStatistics = enabled
                onPageRequested: page => view.settingsPage = page
                onCloseRequested: view.settingsPage = ""
                onSleepTimerRequested: minutes => sleepTimer.start(minutes)
            }
        }
        panelContent: Component {
            RedesignAddonPanel {
                registry: rootPlayer.addonPanelRegistry
                panelId: view.panelOpen
                entry: view.panel(view.panelOpen)
                hostArtwork: {
                    const siblings = rootPlayer.mediaSiblings
                    if (!siblings || !entry) return ({})
                    siblings.artworkRevision
                    return siblings.artwork(entry.packageId)
                }
                scaleFactor: view.scaleFactor; lightTheme: view.lightTheme; accentColor: view.accentColor
                onCloseRequested: view.panelOpen = ""
                onManageRequested: DialogsProvider.prefsPageDialog("extensions")
            }
        }
    }

    RedesignTrackPopup {
        id: tracksPopup
        videoBackdropContent: rootPlayer.menuBackdropContent
        parent: view
        placementArea: view
        scaleFactor: view.scaleFactor; lightTheme: view.lightTheme; accentColor: view.accentColor
        videoTracks: Player.videoTracks
        audioTracks: Player.audioTracks
        subtitleTracks: Player.subtitleTracks
        audioDevices: Player.audioDevices
        onSelectionRequested: (model, index, selected) => model.setData(model.index(index, 0), selected, Qt.CheckStateRole)
        onDisableRequested: model => {
            for (let i = 0; i < model.count; ++i)
                model.setData(model.index(i, 0), false, Qt.CheckStateRole)
        }
        canAddTracks: Player.isStarted
        onAddTracksRequested: page => rootPlayer.openTrackDialog(page)
        onEditTracksRequested: page => rootPlayer.openTrackDialog(page)
        onPreferencesRequested: page => view.settingsPage = page
    }

    Loader {
        id: recentMenuModel
        active: MainCtx.mediaLibraryAvailable && mediaMenu.recentFilesEnabled
        source: active ? "qrc:///qt/qml/VLC/Player/RedesignRecentMenuModel.qml" : ""
    }
    RedesignPreferences { id: customizationPreferences; ctx: MainCtx; editPlayerPreset: true }
    RedesignCustomizationEditor {
        id: customizationEditor
        presetId: { playerPreferences.revision; return playerPreferences.selectedPlayerPreset }
        z: 50
        player: view
        videoBackdropContent: rootPlayer.menuBackdropContent
        onSaveRequested: layout => {
            const staged = customizationPreferences.stage("qt-player-layout", layout)
            const saved = staged && customizationPreferences.save()
            completeSave(saved, customizationPreferences.errorMessage)
        }
        onFinished: customizationPreferences.discard()
    }
    Binding {
        target: MainCtx; property: "useGlobalShortcuts"; value: false
        when: customizationEditor.opened
        restoreMode: Binding.RestoreBindingOrValue
    }
    RedesignShortcuts {
        id: shortcutEditor
        videoBackdropContent: rootPlayer.menuBackdropContent
        parent: view
        scaleFactor: view.scaleFactor; lightTheme: view.lightTheme; accentColor: view.accentColor
        onSaved: playerPreferences.discard()
    }
    RedesignPreferences { id: applicationProfiles; ctx: MainCtx; applicationProfile: true }
    RedesignPlayerPreset {
        id: profileEditor
        objectName: "menuPlayerProfileEditor"
        backend: applicationProfiles
        applicationProfile: true
        selectorVisible: false
        videoBackdropContent: rootPlayer.menuBackdropContent
        scaleFactor: view.scaleFactor; lightTheme: view.lightTheme; accentColor: view.accentColor
    }
    RedesignMenu {
        id: mediaMenu
        parent: view
        triggerItem: view.mainMenuTrigger
        videoBackdropContent: rootPlayer.menuBackdropContent
        scaleFactor: view.scaleFactor; lightTheme: view.lightTheme; accentColor: view.accentColor
        recentModel: recentMenuModel.item
        recentFilesAvailable: MainCtx.mediaLibraryAvailable
        recentFilesEnabled: { playerPreferences.revision; return !!playerPreferences.value("save-recentplay") }
        profiles: { applicationProfiles.revision; return applicationProfiles.playerPresets }
        selectedProfile: { applicationProfiles.revision; return applicationProfiles.selectedPlayerPreset }
        onDiscoveryRequested: discoveryWindow.openSource("")
        onRecentRequested: mediaId => { if (recentMenuModel.item) recentMenuModel.item.playMedia(mediaId) }
        onClearRecentRequested: if (recentMenuModel.item) recentMenuModel.item.clearHistory()
        onProfileActionsRequested: (profileId, name, x, y) => profileEditor.openProfileActions(profileId, name, x, y)
        onProfileRequested: profileId => {
            if (!applicationProfiles.selectPlayerPreset(profileId))
                profileEditor.retrySelection(profileId)
        }
        onActionRequested: action => {
            switch (action) {
            case "file": DialogsProvider.simpleOpenDialog(); break
            case "directory": DialogsProvider.PLOpenDir(); break
            case "disc": DialogsProvider.openDiscDialog(); break
            case "network": DialogsProvider.openNetDialog(); break
            case "capture": DialogsProvider.openCaptureDialog(); break
            case "convert": DialogsProvider.openAndTranscodingDialogs(); break
            case "customize": rootPlayer.beginCustomization(); break
            case "new-profile": profileEditor.namePreset("create", "", ""); break
            case "shortcuts": shortcutEditor.open(); break
            case "settings": DialogsProvider.prefsDialog(); break
            case "about": DialogsProvider.aboutDialog(); break
            }
        }
    }
    // VideoSurface continues to provide VLC's native context menu and hotkeys.
    QmlAudioContextMenu { id: audioContextMenu; ctx: MainCtx }
    Connections {
        target: MainCtx
        function onAskShow() { view.revealControls() }
        function onMainInterfaceModesChanged() {
            if (!MainCtx.minimalView && !MainCtx.playerView && Player.isStarted) { view.libraryOpen = true; view.libraryExpanded = true }
        }
    }
    Dialogs {
        z: 10
        bgContent: rootPlayer
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right; bottomMargin: view.controlsHeight }
    }
    Widgets.FloatingNotification {
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right; margins: view.theme.dp(20); bottomMargin: view.controlsHeight + view.theme.dp(20) }
    }
}

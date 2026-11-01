// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

Item {
    id: root
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    property Component videoBackdropContent
    property var mediaModel
    property url currentMediaUrl
    readonly property string currentMediaKey: mediaIdentity(currentMediaUrl)
    readonly property int libraryMediaCount: mediaView.count
    property int itemCount: libraryMediaCount
    property bool loading: false
    property bool homeSectionsVisible: category === 0
    property var homeSections: []
    property int category: 1
    property string heading: qsTr("Videos")
    property var breadcrumbs: []
    property var pins: []
    property string selectedPinId: ""
    property var locationPinTarget
    property bool locationPinned: false
    property var menuForItem: (index, section) => null
    property string searchText: ""
    property alias grid: viewMode.grid
    property bool expanded: false
    property bool playlistOpen: false
    property bool playlistEditing: false
    property bool playlistBusy: false
    property string playlistKey: ""
    property string playlistEditMessage: ""
    property int dragDestination: -1
    readonly property bool wideToolbar: expanded && width >= theme.dp(1000)
    property bool podcastView: false
    readonly property bool squareCards: category === 2 || showingFolders || podcastView
    property var settingsContext
    property string sortScope: "media"
    property var sortFields: ["played", "insertion", "title", "main_artist", "album_title", "track_number", "uri", "disc_number"]
    property string defaultSortChoice: "unwatched"
    property var extraSortOptions: []
    readonly property alias sortState: sorting
    readonly property bool descending: sorting.selection.descending
    readonly property string sortField: sorting.selection.field
    property bool canGoBack: false
    property bool canGoForward: false
    property bool canEnqueue: true
    property string unnamedItemTitle: ""
    property string defaultItemIcon: category === 4 ? "queue" : category === 2 ? "music" : category === 3 ? "network" : "video"
    property bool singleClickActivate: false
    property bool pointerSelection: false
    function viewState() { return {index: mediaView.currentIndex, y: mediaView.contentY, pointer: pointerSelection} }
    function restoreViewState(state) {
        if (!state) return
        mediaView.forceLayout()
        mediaView.currentIndex = Math.min(state.index, mediaView.count - 1)
        mediaView.contentY = Math.max(0, Math.min(state.y, mediaView.contentHeight - mediaView.height))
        pointerSelection = state.pointer
    }
    onMediaModelChanged: pointerSelection = false
    property var artworkForItem: model => model.thumbnail || model.small_cover || model.cover || ""
    property var artworkIconForItem: model => model.fileIsDir || root.showingFolders ? "folder" : model.iconName || root.defaultItemIcon
    property bool showingFolders: false
    property bool showAddRow: category > 0 && category < 5
    property string addLabel: playlistOpen ? qsTr("Add files") : category === 4 ? qsTr("Create playlist") : category === 3 ? qsTr("Add Network Stream") : qsTr("Add")
    property string emptyText: qsTr("Add a folder to your library to see your media here.")
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    RedesignLibraryPath { id: mediaPaths }
    function mediaIdentity(value) {
        const text = String(value || "")
        const path = mediaPaths.split(text)
        if (!path || !path.prefix.startsWith("file://")) return text
        const normalized = mediaPaths.urlFor(path, path.parts.length)
        return path.windows ? normalized.toLowerCase() : normalized
    }
    function episodeSummary(model) {
        const podcast = model.podcast
        if (!podcast || !podcast.feed) return ""
        const parts = []
        if (podcast.season) parts.push(qsTr("Season %1").arg(podcast.season))
        if (podcast.episode) parts.push(qsTr("Episode %1").arg(podcast.episode))
        const duration = model.duration || podcast.duration
        if (duration && duration.valid && duration.valid()) parts.push(duration.formatHMS(0))
        const progress = Number(model.progress || 0)
        if (progress > 0 && duration && duration.valid && duration.valid())
            parts.push(qsTr("%1 left").arg(duration.scale(1 - progress).formatHMS(0)))
        else if (model.played || Number(model.playcount || 0) > 0) parts.push(qsTr("Played"))
        else parts.push(qsTr("Not played"))
        return parts.join(" · ")
    }
    signal homeActivated(string section, int index)
    signal homeEnqueueRequested(string section, int index)
    signal homeSectionRequested(string section)
    signal categoryRequested(int category)
    signal discoveryRequested(string source)
    signal activated(int index)
    signal enqueueRequested(int index)
    signal addRequested()
    signal filesRequested()
    signal folderRequested()
    signal fullLibraryRequested()
    signal backRequested()
    signal forwardRequested()
    signal newPlaylistRequested()
    signal preferencesRequested()
    signal breadcrumbActivated(int index)
    signal pinActivated(var pin)
    signal pinRemoved(var pin)
    signal pinEnqueueRequested(var pin)
    signal pinLocationRequested()
    signal itemActionRequested(var target, string action)
    signal playlistEditRequested()
    signal playlistMoveRequested(int from, int to, var id)
    signal playlistRemoveRequested(int row, var id)
    signal playlistFilesDropped(var urls)

    QtObject {
        id: viewMode
        property var ctx: root.settingsContext
        readonly property string mode: root.expanded ? "expanded" : "compact"
        readonly property string storageKey: "Juxtopposed/library-view"
        property var saved: ({})
        property bool grid: false
        property bool ready: false
        property bool applying: false
        function apply() {
            applying = true
            grid = typeof saved[mode] === "boolean" ? saved[mode] : root.expanded
            applying = false
        }
        function load() {
            try {
                const value = ctx ? JSON.parse(ctx.settingValue(storageKey, "{}")) : {}
                saved = value && !Array.isArray(value) && typeof value === "object" ? value : {}
            } catch (error) { saved = {} }
            apply()
        }
        onGridChanged: {
            if (!ready || applying) return
            saved = Object.assign({}, saved, {[mode]: grid})
            if (ctx) ctx.setSettingValue(storageKey, JSON.stringify(saved))
        }
        onModeChanged: if (ready) apply()
        property Connections settingsObserver: Connections {
            target: viewMode.ctx || null
            ignoreUnknownSignals: true
            function onSettingsReloaded() { viewMode.load() }
        }
        onCtxChanged: if (ready) load()
        Component.onCompleted: { load(); ready = true }
    }

    function nameFromUrl(url) {
        const value = url.toString()
        const name = value.replace(/\\/g, "/").replace(/\/+$/, "").split("/").pop()
        if (!name || name === "file:") return "/"
        if (!/^[a-z][a-z0-9+.-]*:\/\//i.test(value)) return name
        try { return decodeURIComponent(name) } catch (error) { return name }
    }
    function addMedia() {
        if (playlistOpen && playlistBusy) return
        if (locationPinTarget?.kind === "collection") filesRequested()
        else if (category === 4 && !playlistOpen) newPlaylistRequested()
        else addRequested()
    }
    RedesignLibrarySort {
        id: sorting
        ctx: root.settingsContext; scope: root.sortScope
        fields: root.sortFields; defaultChoice: root.defaultSortChoice; extraOptions: root.extraSortOptions
    }
    property var sortPopup: null
    Component {
        id: sortMenuComponent
        RedesignActionMenu {
            id: menu
            videoBackdropContent: root.videoBackdropContent
            objectName: "librarySortMenu"
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            checkedAction: sorting.selection.id
            onActionRequested: action => sorting.select(action)
            onOpened: currentIndex = sorting.options.findIndex(option => option.id === sorting.selection.id)
            onClosed: {
                if (root.sortPopup === menu) root.sortPopup = null
                destroy()
            }
        }
    }
    Connections {
        target: sorting
        function onOptionsChanged() { if (root.sortPopup) root.sortPopup.close() }
    }
    function openSortMenu(anchor) {
        if (sortPopup) { sortPopup.close(); return }
        sortPopup = sortMenuComponent.createObject(root, {
            entries: sorting.options.map(option => ({action: option.id, label: option.label}))
        })
        sortPopup.toggleFrom(anchor)
    }
    property int menuRequest: 0
    onBreadcrumbsChanged: { ++menuRequest; itemMenu.close() }
    function openItemMenu(anchor, index, point, section) {
        const request = ++menuRequest
        const context = menuForItem(index, section || "")
        function show(context) {
            if (request !== menuRequest || !anchor || !root.visible || !context || !context.target || !context.entries.length) return
            itemMenu.focusOrigin = anchor.activeFocus ? anchor : null
            itemMenu.target = context.target
            itemMenu.entries = context.entries
            itemMenu.popup(anchor, point)
        }
        if (context && typeof context.then === "function") context.then(show)
        else show(context)
    }
    RedesignActionMenu {
        id: itemMenu
        videoBackdropContent: root.videoBackdropContent
        objectName: "libraryItemMenu"
        property var target
        property Item focusOrigin
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        onClosed: {
            const origin = focusOrigin
            focusOrigin = null
            const window = root.Window.window
            if (origin && origin.visible && root.visible && window && window.activeFocusItem === window.contentItem)
                origin.forceActiveFocus(Qt.TabFocusReason)
        }
        onActionRequested: action => { itemMenu.close(); root.itemActionRequested(target, action) }
    }
    RedesignActionMenu {
        id: moreMenu
        videoBackdropContent: root.videoBackdropContent
        objectName: "libraryMoreMenu"
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        entries: (root.locationPinTarget && root.locationPinTarget.kind === "folder" ? [
            {action: "add-files", label: qsTr("Add File"), icon: "add"},
            {action: "add-folder", label: qsTr("Add Folder"), icon: "new-folder-two-tone", accent: true},
            {action: "subtitle-tracks", label: qsTr("Edit Subtitle Tracks"), icon: "subtitles-two-tone", accent: true},
            {action: "audio-tracks", label: qsTr("Edit Audio Tracks"), icon: "audio-two-tone", accent: true},
            {action: "resync", label: qsTr("Resync"), icon: "refresh-two-tone", accent: true},
            {action: "choose-artwork", label: qsTr("Choose artwork…"), icon: "image"},
            {action: "reset-artwork", label: qsTr("Use automatic artwork"), icon: "refresh-two-tone"},
            {action: "folder-info", label: qsTr("Info"), icon: "info-two-tone", accent: true}
        ] : ([1,2].includes(root.category) && !root.playlistOpen ? [
            {action: "add-files", label: qsTr("Add File"), icon: "add"},
            {action: "add-folder", label: qsTr("Add Folder"), icon: "new-folder-two-tone", accent: true}
        ] : [{action: "add", label: root.addLabel, icon: "add", enabled: !root.playlistOpen || !root.playlistBusy}])
            .concat([{action: "settings", label: qsTr("Library settings"), icon: "settings"}])
            .concat(root.playlistOpen ? [{action: "edit-playlist", label: root.playlistEditing ? qsTr("Done") : qsTr("Edit playlist"), icon: root.playlistEditing ? "check" : "drag", enabled: !root.playlistBusy}] : [])
            .concat(root.playlistOpen && root.locationPinTarget ? [
                {action: "add-playlist", label: qsTr("Add to Playlist"), icon: "playlist", accent: true},
                {action: "properties", label: qsTr("Properties & Metadata"), icon: "info-two-tone", accent: true}] : [])
            .concat(root.locationPinTarget ? [{action: "pin", label: root.locationPinned ? qsTr("Remove from sidebar") : qsTr("Add to sidebar"), icon: root.locationPinned ? "favorite-active" : "favorite", accent: true}] : []))
            .concat(root.expanded ? [{action: "compact", label: qsTr("Open compact library"), icon: "sidebar"}] : [])
        onActionRequested: action => {
            moreMenu.close()
            if (action === "add") root.addMedia()
            else if (action === "add-files") root.filesRequested()
            else if (action === "add-folder") root.folderRequested()
            else if (action === "edit-playlist") root.playlistEditRequested()
            else if (action === "pin") root.pinLocationRequested()
            else if (["add-playlist", "properties", "resync", "folder-info", "subtitle-tracks", "audio-tracks", "choose-artwork", "reset-artwork"].includes(action)) root.itemActionRequested(root.locationPinTarget, action)
            else if (action === "compact") root.fullLibraryRequested()
            else root.preferencesRequested()
        }
    }
    RedesignActionMenu {
        id: viewMenu
        videoBackdropContent: root.videoBackdropContent
        objectName: "libraryViewMenu"
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        checkedAction: root.grid ? "grid" : "list"
        entries: [
            { action: "grid", label: qsTr("Grid View"), icon: "grid" },
            { action: "list", label: qsTr("List View"), icon: "list-view" }
        ]
        onActionRequested: action => root.grid = action === "grid"
    }

    RedesignLibraryNavigation {
        id: rail
        videoBackdropContent: root.videoBackdropContent
        objectName: "libraryNavigation"
        x: root.expanded ? theme.dp(8) : 0
        anchors.top: parent.top; anchors.bottom: parent.bottom
        anchors.topMargin: root.expanded ? theme.dp(8) : 0
        anchors.bottomMargin: root.expanded ? theme.dp(8) : 0
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        expanded: root.expanded; category: root.category
        atCategoryRoot: root.breadcrumbs.length <= 1
        pins: root.pins; selectedPinId: root.selectedPinId
        onCategoryRequested: category => root.categoryRequested(category)
        onDiscoveryRequested: source => root.discoveryRequested(source)
        onAddRequested: root.addRequested()
        onPinActivated: pin => root.pinActivated(pin)
        onPinRemoved: pin => root.pinRemoved(pin)
        onPinEnqueueRequested: pin => root.pinEnqueueRequested(pin)
    }
    DropArea {
        id: playlistDrop
        anchors.fill: parent
        enabled: root.playlistOpen && !root.playlistBusy
        onEntered: drag => drag.accepted = drag.hasUrls
        onDropped: drop => { if (drop.hasUrls) { root.playlistFilesDropped(drop.urls); drop.acceptProposedAction() } }
    }
    Rectangle {
        anchors.fill: parent; anchors.leftMargin: rail.width
        color: "transparent"; border.color: root.theme.accent; border.width: root.theme.dp(2)
        visible: playlistDrop.containsDrag
    }
    property bool searchOpen: false
    component LibraryButton: RedesignButton {
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
    }
    component BackButton: LibraryButton {
        objectName: "libraryBackButton"
        iconName: "back"; text: qsTr("Back"); enabled: root.canGoBack
        onClicked: root.backRequested()
    }
    component ForwardButton: LibraryButton {
        objectName: "libraryForwardButton"
        iconName: "forward"; text: qsTr("Forward"); enabled: root.canGoForward
        onClicked: root.forwardRequested()
    }
    component MoreButton: LibraryButton {
        id: button
        objectName: "libraryMoreButton"
        iconName: "more"; text: qsTr("More library actions")
        foreground: root.theme.playerLabel
        onClicked: moreMenu.toggleFrom(button)
    }
    component SortButton: LibraryButton {
        id: button
        objectName: "librarySortButton"
        enabled: sorting.options.length > 0
        iconName: enabled && (root.sortPopup || sorting.selection.id !== root.defaultSortChoice) ? "sort-active" : "sort"
        iconDetailColor: root.theme.accent
        foreground: root.theme.playerLabel
        text: qsTr("Sort options")
        onClicked: root.openSortMenu(button)
    }
    component ViewButton: LibraryButton {
        id: button
        objectName: "libraryViewButton"
        iconName: root.grid ? "grid" : "list-view"; text: qsTr("View options")
        foreground: root.theme.playerLabel
        onClicked: viewMenu.toggleFrom(button)
    }
    component ItemCount: Text {
        text: root.loading ? qsTr("Loading…") : qsTr("%n item", "%n items", root.itemCount)
        color: root.theme.playerLabel
        font.pixelSize: root.theme.dp(14)
    }
    component Location: RedesignBreadcrumbs {
        videoBackdropContent: root.videoBackdropContent
        objectName: "libraryLocation"
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        boldCurrent: root.expanded && root.breadcrumbs.length > 1
        entries: root.breadcrumbs.length ? root.breadcrumbs : [{label: root.heading}]
        onActivated: index => root.breadcrumbActivated(index)
    }
    component SearchField: C.TextField {
        objectName: "librarySearchField"
        placeholderText: qsTr("Search")
        placeholderTextColor: root.wideToolbar && !root.lightTheme ? "#404040" : root.theme.foreground
        text: root.searchText
        onTextEdited: root.searchText = text
        color: root.theme.bright
        font.pixelSize: root.theme.dp(14)
        implicitHeight: root.theme.dp(34)
        leftPadding: root.expanded ? root.theme.dp(42) : root.theme.dp(10)
        topPadding: root.wideToolbar ? root.theme.dp(6.5) : padding
        bottomPadding: root.wideToolbar ? root.theme.dp(5.5) : padding
        background: Rectangle { color: root.wideToolbar && !root.lightTheme ? "#151515" : root.theme.field; radius: root.theme.dp(6); border.width: parent.activeFocus ? 1 : 0; border.color: root.theme.accent }
        RedesignIcon { visible: root.expanded; x: root.theme.dp(8); anchors.verticalCenter: parent.verticalCenter; width: root.theme.dp(24); height: width; name: "search"; color: root.theme.playerLabel }
    }
    Component {
        id: compactToolbar
        ColumnLayout {
            spacing: root.theme.dp(8)
            RowLayout {
                Layout.fillWidth: true; Layout.leftMargin: -root.theme.dp(6); Layout.rightMargin: -root.theme.dp(6)
                spacing: 0
                BackButton {}
                ForwardButton {}
                Location { Layout.fillWidth: true; Layout.leftMargin: root.theme.dp(4) }
                MoreButton {}
                LibraryButton {
                    objectName: "expandLibraryButton"
                    iconName: root.expanded ? "sidebar" : "expand"
                    text: root.expanded ? qsTr("Open compact library") : qsTr("Open full library")
                    foreground: root.theme.playerLabel
                    onClicked: root.fullLibraryRequested()
                }
            }
            RowLayout {
                Layout.fillWidth: true; Layout.rightMargin: -root.theme.dp(6)
                spacing: 0
                ItemCount { Layout.fillWidth: true }
                SortButton {}
                ViewButton {}
                LibraryButton {
                    objectName: "librarySearchButton"
                    iconName: "search"; text: qsTr("Search library"); selected: root.searchOpen
                    foreground: root.theme.playerLabel
                    onClicked: {
                        root.searchOpen = !root.searchOpen
                        if (root.searchOpen) compactSearch.forceActiveFocus()
                        else root.searchText = ""
                    }
                }
            }
            SearchField { id: compactSearch; visible: root.searchOpen || root.searchText.length > 0; Layout.fillWidth: true }
        }
    }
    Component {
        id: expandedToolbar
        Item {
            implicitHeight: root.theme.dp(34)
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: -root.theme.dp(6); anchors.rightMargin: -root.theme.dp(6)
                anchors.topMargin: -root.theme.dp(1); anchors.bottomMargin: root.theme.dp(1)
                spacing: 0
                BackButton {}
                ForwardButton {}
                Location { Layout.fillWidth: true; Layout.leftMargin: root.theme.dp(4) }
                ItemCount { Layout.rightMargin: root.theme.dp(14) }
                SortButton { Layout.rightMargin: root.theme.dp(8) }
                ViewButton {}
                SearchField { Layout.preferredWidth: Math.min(root.theme.dp(325), root.width * 0.24); Layout.leftMargin: root.theme.dp(14); Layout.rightMargin: root.theme.dp(14) }
                MoreButton {}
            }
        }
    }
    ColumnLayout {
        anchors { top: parent.top; bottom: parent.bottom; left: rail.right; right: parent.right; margins: theme.dp(20); leftMargin: theme.dp(root.expanded ? 28 : 20); topMargin: theme.dp(root.wideToolbar ? 20 : 14) }
        spacing: theme.dp(root.wideToolbar ? 16 : 10)
        Loader {
            Layout.fillWidth: true
            Layout.preferredHeight: root.wideToolbar ? theme.dp(34) : implicitHeight
            sourceComponent: root.wideToolbar ? expandedToolbar : compactToolbar
        }
        Rectangle {
            objectName: "libraryToolbarDivider"
            Layout.fillWidth: true; height: theme.dp(1)
            color: !root.expanded && !root.lightTheme ? "#222222" : theme.border
            antialiasing: !root.expanded
            transform: Translate { y: root.expanded ? 0 : root.theme.dp(0.5) }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.playlistEditing || root.playlistEditMessage.length > 0
            Text {
                Layout.fillWidth: true
                text: root.playlistEditMessage || (root.playlistBusy ? qsTr("Saving…") : qsTr("Changes are saved automatically."))
                color: root.playlistEditMessage.length ? root.theme.danger : root.theme.foreground
                font.pixelSize: root.theme.dp(14); wrapMode: Text.WordWrap
            }
            LibraryButton {
                objectName: "playlistDoneButton"
                visible: root.playlistEditing
                caption: qsTr("Done"); text: caption; enabled: !root.playlistBusy
                onClicked: root.playlistEditRequested()
            }
        }
        RedesignButton {
            objectName: "libraryAddRow"
            visible: root.showAddRow && !root.expanded
            Layout.topMargin: theme.dp(5)
            Layout.fillWidth: true
            implicitHeight: theme.dp(42)
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            text: root.addLabel
            enabled: !root.playlistOpen || !root.playlistBusy
            onClicked: root.addMedia()
            contentItem: Row {
                spacing: theme.dp(10)
                Item {
                    width: theme.dp(32); height: theme.dp(42)
                    RedesignIcon { anchors.centerIn: parent; width: theme.dp(24); height: width; name: "add"; color: theme.playerLabel }
                }
                Text { anchors.verticalCenter: parent.verticalCenter; anchors.verticalCenterOffset: theme.dp(1); text: root.addLabel; color: theme.playerLabel; font.pixelSize: theme.dp(14) }
            }
        }
        RedesignAddonSurface {
            hook: "home.content@1"
            hostContext: ({surface: "home.content@1", search: root.searchText, layout: root.expanded ? "expanded" : "compact"})
            visible: root.homeSectionsVisible
            Layout.topMargin: theme.dp(root.expanded ? 25 : 10)
            Layout.fillWidth: true; Layout.fillHeight: true
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            builtIn: Component { RedesignHomeView {
            singleClickActivate: root.singleClickActivate
            artworkForItem: root.artworkForItem
            objectName: "libraryHomeView"
            visible: root.homeSectionsVisible
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            expanded: root.expanded
            searchText: root.searchText
            sections: root.homeSectionsVisible ? root.homeSections : []
            onActivated: (section, index) => root.homeActivated(section, index)
            onEnqueueRequested: (section, index) => root.homeEnqueueRequested(section, index)
            onSectionRequested: section => root.homeSectionRequested(section)
            onContextRequested: (section, index, anchor, point) => root.openItemMenu(anchor, index, point, section)
            } }
        }
        GridView {
            id: mediaView
            objectName: "libraryMediaView"
            Layout.topMargin: root.expanded ? theme.dp(root.grid ? 4 : 9) : root.showAddRow ? -theme.dp(10) : theme.dp(5)
            Layout.rightMargin: root.expanded ? -theme.dp(20) : 0
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            model: root.mediaModel
            // Keep a screen of tiles built beyond the view, so scrolling back
            // shows their artwork at once instead of loading it again.
            cacheBuffer: Math.max(0, height)
            visible: !root.homeSectionsVisible && root.itemCount > 0
            cellWidth: root.expanded ? Math.min(width, theme.dp(root.grid ? root.squareCards ? 220 : 336 : 416)) : root.grid ? width / Math.max(1, Math.floor(width / theme.dp(130))) : width
            cellHeight: root.grid ? root.expanded ? (cellWidth - theme.dp(20)) * (root.squareCards ? 1 : 9 / 16) + theme.dp(root.category === 2 ? 88 : 79) : theme.dp(142) : theme.dp(root.podcastView ? 62 : 42)
            keyNavigationEnabled: true
            currentIndex: -1
            function moveFocus(key) {
                if (key === Qt.Key_Left) moveCurrentIndexLeft()
                else if (key === Qt.Key_Right) moveCurrentIndexRight()
                else if (key === Qt.Key_Up) moveCurrentIndexUp()
                else if (key === Qt.Key_Down) moveCurrentIndexDown()
                else return false
                if (currentItem) currentItem.focusMediaItem()
                return true
            }
            // Navigation can replace the model while focus stays on the view.
            Keys.onPressed: event => { event.accepted = moveFocus(event.key) }
            C.ScrollBar.vertical: C.ScrollBar { policy: C.ScrollBar.AsNeeded }
            delegate: Item {
                id: delegate
                required property int index
                required property var model
                width: mediaView.cellWidth - (root.expanded ? theme.dp(20) : 0)
                height: mediaView.cellHeight
                readonly property string itemTitle: model.title || model.name || model.fileName || (model.url ? root.nameFromUrl(model.url) : "") || root.unnamedItemTitle
                readonly property string episodeInfo: root.episodeSummary(model)
                readonly property string itemArtist: episodeInfo || model.main_artist || ""
                readonly property string displayTitle: itemTitle + ((!root.expanded || !root.grid) && !episodeInfo && itemArtist ? " - " + itemArtist : "")
                readonly property string itemArtwork: root.artworkForItem(model)
                readonly property url itemMediaUrl: model.fileUrl || model.mrl || model.url || ""
                readonly property string mediaKey: root.mediaIdentity(itemMediaUrl)
                readonly property bool isMedia: mediaKey.length > 0 && !model.fileIsDir && !root.showingFolders
                readonly property bool isCurrent: isMedia && root.currentMediaKey.length > 0 && mediaKey === root.currentMediaKey
                readonly property bool played: isMedia && (model.played === true || Number(model.playcount || 0) > 0)
                function focusMediaItem() { mediaButton.forceActiveFocus(Qt.TabFocusReason) }
                RedesignButton {
                    id: grip
                    objectName: "playlistGrip" + delegate.index
                    anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                    implicitWidth: theme.dp(24); implicitHeight: theme.dp(36)
                    visible: root.playlistEditing && !root.grid; enabled: !root.playlistBusy
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    iconName: "drag"; text: qsTr("Move playlist item")
                    onClicked: { mediaView.currentIndex = delegate.index; mediaButton.forceActiveFocus() }
                    DragHandler {
                        id: rowMover
                        target: null; enabled: grip.enabled && grip.visible
                        property int from: -1
                        property int to: -1
                        property var originId
                        property string originPlaylist: ""
                        onActiveChanged: {
                            if (active) { from = delegate.index; to = from; originId = delegate.model.id; originPlaylist = root.playlistKey; mediaView.currentIndex = from }
                            else if (from >= 0) {
                                if (root.visible && root.playlistEditing && originPlaylist === root.playlistKey && from !== to)
                                    root.playlistMoveRequested(from, to, originId)
                                from = -1; root.dragDestination = -1
                            }
                        }
                        onCentroidChanged: if (active) {
                            const point = grip.mapToItem(mediaView, centroid.position.x, centroid.position.y)
                            const index = mediaView.indexAt(point.x + mediaView.contentX, point.y + mediaView.contentY)
                            to = index >= 0 ? index : point.y < 0 ? 0 : root.itemCount - 1
                            root.dragDestination = to
                        }
                        onCanceled: { from = -1; root.dragDestination = -1 }
                    }
                }
                RedesignButton {
                    id: mediaButton
                    objectName: "libraryMediaItem" + delegate.index
                    anchors.fill: parent
                    anchors.margins: 0
                    anchors.leftMargin: root.playlistEditing && !root.grid ? theme.dp(28) : 0
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    text: delegate.displayTitle
                    tooltipTextFormat: Text.PlainText
                    // Mouse focus left after navigation is not a keyboard selection.
                    selected: mediaView.currentIndex === delegate.index && (root.pointerSelection || root.playlistEditing || mediaButton.visualFocus)
                    onActiveFocusChanged: if (activeFocus) mediaView.currentIndex = delegate.index
                    handleMediaKeys: false
                    mediaItem: !root.playlistEditing
                    singleClickActivate: root.singleClickActivate
                    onClicked: { mediaView.currentIndex = delegate.index; root.pointerSelection = true; forceActiveFocus(Qt.MouseFocusReason) }
                    onActivated: root.activated(delegate.index)
                    Keys.onMenuPressed: root.openItemMenu(this, delegate.index, Qt.point(0, height), "")
                    Keys.onPressed: event => {
                        if (root.playlistEditing && !root.playlistBusy && event.key === Qt.Key_Delete) {
                            root.playlistRemoveRequested(delegate.index, delegate.model.id); event.accepted = true
                        } else if (root.playlistEditing && !root.playlistBusy && event.modifiers & Qt.AltModifier && (event.key === Qt.Key_Up || event.key === Qt.Key_Down)) {
                            const target = delegate.index + (event.key === Qt.Key_Up ? -1 : 1)
                            if (target >= 0 && target < root.itemCount) root.playlistMoveRequested(delegate.index, target, delegate.model.id)
                            event.accepted = true
                        } else if (event.key === Qt.Key_F10 && event.modifiers & Qt.ShiftModifier) {
                            root.openItemMenu(this, delegate.index, Qt.point(0, height), ""); event.accepted = true
                        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                            if (!root.playlistEditing && !event.isAutoRepeat) root.activated(delegate.index); event.accepted = true
                        } else if (event.key === Qt.Key_Space && !root.playlistEditing) {
                            if (!event.isAutoRepeat) root.activated(delegate.index); event.accepted = true
                        } else if (mediaView.moveFocus(event.key)) {
                            event.accepted = true
                        } else event.accepted = false
                    }
                    TapHandler { acceptedButtons: Qt.RightButton; onTapped: event => root.openItemMenu(mediaButton, delegate.index, event.position, "") }
                    contentItem: Item {
                        RedesignLibraryArtwork {
                            id: thumbnail
                            objectName: "libraryArtwork" + delegate.index
                            x: root.grid && !root.expanded ? theme.dp(4) : 0
                            y: root.grid ? 0 : (parent.height - height) / 2
                            width: root.grid ? parent.width - (root.expanded ? 0 : theme.dp(8)) : theme.dp(32)
                            height: root.grid ? root.expanded ? width * (root.squareCards ? 1 : 9 / 16) : theme.dp(90) : theme.dp(32)
                            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                            cornerRadius: root.grid && root.expanded ? 12 : 2
                            iconName: root.artworkIconForItem(model)
                            iconSize: root.grid && root.expanded ? 48 : 24
                            showPlaceholderBackground: root.grid
                            source: delegate.itemArtwork
                            progress: Number(model.progress || 0)
                            progressHeight: root.grid ? 4 : 3
                            roundProgress: root.grid
                            surroundColor: mediaButton.down || mediaButton.selected ? theme.selected : mediaButton.hovered ? theme.hover : theme.panel
                        }
                        Text {
                            objectName: "libraryTitle" + delegate.index
                            x: root.grid ? root.expanded ? 0 : theme.dp(4) : thumbnail.width + theme.dp(10)
                            y: root.grid ? thumbnail.y + thumbnail.height + (root.expanded ? theme.dp(35) - baselineOffset : theme.dp(6))
                                : parent.height / 2 + theme.dp(delegate.episodeInfo ? -4 : 5.5) - baselineOffset
                            width: root.grid ? parent.width - (root.expanded ? 0 : theme.dp(8)) : Math.max(0, parent.width - x - theme.dp(26))
                            text: delegate.displayTitle; textFormat: Text.PlainText; elide: Text.ElideRight
                            color: delegate.isCurrent && root.expanded && !root.grid ? theme.accent
                                : mediaButton.hovered || mediaButton.visualFocus ? theme.bright
                                : delegate.isCurrent ? (root.expanded ? theme.playerHeading : theme.foreground)
                                : delegate.played ? theme.playerLabel
                                : root.expanded || delegate.isMedia ? theme.playerHeading : theme.playerLabel
                            font.pixelSize: theme.dp(root.expanded && root.grid ? 16 : 14)
                        }
                        Text {
                            objectName: "libraryArtist" + delegate.index
                            visible: delegate.episodeInfo.length > 0 || root.expanded && root.grid && delegate.itemArtist.length > 0
                            x: root.grid ? 0 : thumbnail.width + theme.dp(10)
                            y: root.grid ? thumbnail.height + theme.dp(root.expanded ? 64 : 39) - baselineOffset : parent.height / 2 + theme.dp(14) - baselineOffset
                            width: parent.width - x - (root.grid ? 0 : theme.dp(26))
                            text: delegate.itemArtist; textFormat: Text.PlainText; elide: Text.ElideRight
                            color: theme.playerLabel; font.pixelSize: theme.dp(delegate.episodeInfo ? root.grid && root.expanded ? 14 : 12 : 16)
                        }
                    }
                    DragHandler {
                        id: dragHandler
                        target: null
                        enabled: root.canEnqueue && !root.playlistEditing
                    }
                    RedesignLibraryDrag {
                        handler: dragHandler
                        libraryView: root
                        mediaIndex: delegate.index
                    }
                }
                RedesignButton {
                    objectName: "libraryRowAction" + delegate.index
                    visible: (root.playlistEditing || root.canEnqueue) && !root.grid && (root.playlistEditing || mediaButton.hovered || hovered || mediaButton.visualFocus)
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    implicitWidth: theme.dp(28)
                    iconName: root.playlistEditing ? "remove" : "add"; text: root.playlistEditing ? qsTr("Remove from playlist") : qsTr("Add to queue")
                    enabled: !root.playlistEditing || !root.playlistBusy
                    onClicked: root.playlistEditing ? root.playlistRemoveRequested(delegate.index, delegate.model.id) : root.enqueueRequested(delegate.index)
                }
                Rectangle { anchors.fill: parent; color: "transparent"; border.color: theme.accent; border.width: theme.dp(2); visible: root.playlistEditing && root.dragDestination === delegate.index }
            }
        }
        Text {
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Kept while a list loads, without text: it takes the spare
            // height, so the toolbar stays at the top.
            visible: !root.homeSectionsVisible && root.itemCount === 0
            objectName: "libraryEmptyMessage"
            text: root.loading ? "" : root.searchText.length > 0 ? qsTr("No media matches your search.") : root.emptyText
            wrapMode: Text.WordWrap; color: theme.foreground; font.pixelSize: theme.dp(14)
            verticalAlignment: Text.AlignTop
        }
    }
}

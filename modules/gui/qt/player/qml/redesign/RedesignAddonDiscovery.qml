// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

FocusScope {
    id: root
    required property var backend
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    property bool playlistActionsAvailable: viewState.playlistAvailable === true
    property bool playlistPickerOpen: false
    property string playlistChoice: ""
    property string viewId: ""
    property string sourceId: ""
    property var viewState: ({})
    property var selected: []
    property string actionError: ""
    property bool ready: false
    property bool displayActive: visible
    property var viewOwner: null
    property var displayOwner: null
    property string displayRequest: ""
    property int displayGeneration: 0
    property bool pendingOpen: false
    property bool playbackReviewAvailable: false
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme }
    readonly property var sources: { if (!backend) return []; backend.revision; return backend.sources || [] }
    readonly property var source: sources.find(entry => entry.id === sourceId) || ({})
    readonly property var sourceChoices: [{value: "", label: qsTr("Select source")}].concat(sources.map(entry => ({value: entry.id, label: entry.name || entry.id})))
    readonly property bool canSearch: (source.operations || []).includes("search")
    readonly property bool displayAvailable: !!backend && backend.available !== false
    readonly property bool displayLoading: !!backend && backend.loading === true
    readonly property bool sourceChecking: sourceId.length > 0 && source.configurationKnown === false
    readonly property bool actionable: displayAvailable && !sourceChecking
    // The app's queue-fill session; its source can differ from the open one.
    readonly property var queueFill: backend && backend.queueFill ? backend.queueFill : ({})
    signal configureRequested(string packageId)
    signal playlistRequested(var itemReferences)
    signal playbackReviewRequested(string source)
    function closeView() {
        const owner = viewOwner, id = viewId
        viewId = ""; viewOwner = null; pendingOpen = false; viewState = ({}); rows.clear(); selected = []; playlistPickerOpen = false; playlistChoice = ""
        if (owner && id.length) owner.closeView(id)
    }
    function releaseDisplay() {
        ++displayGeneration
        const owner = displayOwner, request = displayRequest
        displayOwner = null; displayRequest = ""
        if (owner && request.length && owner.releaseDisplay) owner.releaseDisplay(request)
    }
    function requestDisplay() {
        releaseDisplay()
        if (!ready || !displayActive || !backend || !backend.acquireDisplay) return
        // The backend can change visibility or binding inputs. Run it after
        // the property binding that requested preparation has returned.
        Qt.callLater(root.finishDisplayRequest, backend, displayGeneration)
    }
    function finishDisplayRequest(owner, generation) {
        if (!owner || generation !== displayGeneration || !ready || !displayActive || backend !== owner) return
        const request = owner.acquireDisplay()
        if (generation !== displayGeneration || !ready || !displayActive || backend !== owner) {
            if (request.length && owner.releaseDisplay) owner.releaseDisplay(request)
            return
        }
        displayOwner = owner; displayRequest = request
    }
    function retryPendingOpen() {
        if (ready && displayActive && pendingOpen && actionable) openSource(sourceId)
    }
    function openSource(id) {
        closeView(); sourceId = id; actionError = ""; query.text = ""
        if (!id.length || !backend || !displayActive) return
        if (!actionable) { pendingOpen = true; return }
        const owner = backend
        viewId = owner.openSource(id)
        if (viewId.length) viewOwner = owner
        if (viewId.length) reload()
        else if (!actionable) pendingOpen = true
        else actionError = qsTr("Set up and enable this discovery source.")
    }
    function reload() {
        if (!viewId.length) return
        viewState = backend.view(viewId)
        const items = viewState.items || []
        const wanted = new Set(items.map(entry => entry.ref))
        for (let i = rows.count - 1; i >= 0; --i) if (!wanted.has(rows.get(i).ref)) rows.remove(i)
        for (let index = 0; index < items.length; ++index) {
            const item = items[index]
            const value = {ref: item.ref, title: item.title || "", kind: item.kind, available: item.available !== false}
            let existing = -1
            for (let i = 0; i < rows.count; ++i) if (rows.get(i).ref === item.ref) { existing = i; break }
            if (existing < 0) rows.insert(index, value)
            else { rows.set(existing, value); if (existing !== index) rows.move(existing, index, 1) }
        }
        selected = selected.filter(ref => wanted.has(ref))
    }
    function select(ref) {
        if (playlistPickerOpen) cancelPlaylist()
        selected = selected.includes(ref) ? selected.filter(value => value !== ref) : selected.concat([ref])
    }
    function cancelPlaylist() { if (backend && viewId.length && backend.clearPlaylist) backend.clearPlaylist(viewId); playlistPickerOpen = false; playlistChoice = "" }
    function addToPlaylist() {
        const target = viewState.playlistTarget || ""
        if (!target.length || !backend.enqueue(viewId, selected, target)) actionError = qsTr("The app cannot add this selection to the playlist.")
        else { playlistPickerOpen = false; playlistChoice = "" }
    }
    function search() { if (actionable && viewId.length) { selected = []; actionError = ""; backend.search(viewId, query.text) } }
    function enqueue() {
        actionError = ""
        if (!backend.enqueue(viewId, selected)) actionError = qsTr("The app cannot add this selection to the queue.")
    }
    function play() {
        actionError = ""
        if (!backend.play(viewId, selected[0])) actionError = qsTr("The app cannot play this item.")
    }
    Component.onCompleted: { ready = true; requestDisplay() }
    Component.onDestruction: { ready = false; closeView(); releaseDisplay() }
    onDisplayActiveChanged: if (ready) {
        if (!displayActive) { closeView(); releaseDisplay() }
        else requestDisplay()
    }
    onBackendChanged: if (ready) { closeView(); requestDisplay() }
    onSourcesChanged: retryPendingOpen()
    Connections {
        target: root.backend
        function onChanged() { root.retryPendingOpen(); root.reload() }
        function onAvailabilityChanged() { root.retryPendingOpen(); root.reload() }
        function onViewChanged(id) { if (id === root.viewId) root.reload() }
        function onViewClosed(id) {
            if (id !== root.viewId) return
            root.viewId = ""; root.viewOwner = null; root.viewState = ({}); rows.clear(); root.selected = []; root.playlistPickerOpen = false; root.playlistChoice = ""
            root.actionError = qsTr("The source changed. Open it again.")
        }
        function onActionFinished(id, success, error) { if (id === root.viewId) root.actionError = success ? "" : error }
    }
    ListModel { id: rows }
    ColumnLayout {
        anchors.fill: parent; spacing: root.theme.dp(10)
        RowLayout {
            Layout.fillWidth: true
            RedesignComboBox {
                objectName: "addonDiscoverySources"
                Layout.fillWidth: true
                model: root.sourceChoices; textRole: "label"; valueRole: "value"; labelTextFormat: Text.PlainText
                currentIndex: Math.max(0, root.sourceChoices.findIndex(entry => entry.value === root.sourceId))
                Accessible.name: qsTr("Discovery source")
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onActivated: index => root.openSource(root.sourceChoices[index].value)
            }
            RedesignButton {
                objectName: "addonDiscoveryOpen"
                caption: qsTr("Open source"); text: caption; enabled: root.sourceId.length > 0 && root.actionable
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.openSource(root.sourceId)
            }
            RedesignButton {
                objectName: "addonDiscoveryConfigure"
                caption: qsTr("Settings"); text: caption; enabled: root.sourceId.length > 0 && root.displayAvailable
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.configureRequested(root.sourceId.split("/")[0])
            }
            RedesignButton {
                objectName: "addonDiscoveryPlaybackReview"
                visible: root.playbackReviewAvailable
                enabled: root.actionable && root.sourceId.length > 0 && (root.source.operations || []).includes("resolve")
                caption: qsTr("Playback access"); text: caption; tooltipTextFormat: Text.PlainText
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.playbackReviewRequested(root.sourceId)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            RedesignButton {
                objectName: "addonDiscoveryBack"
                iconName: "back"; text: qsTr("Back"); enabled: root.actionable && root.viewState.canGoBack === true
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.backend.back(root.viewId)
            }
            C.TextField {
                id: query
                objectName: "addonDiscoveryQuery"
                Layout.fillWidth: true; enabled: root.actionable && root.viewId.length > 0 && root.canSearch
                placeholderText: qsTr("Search source"); Accessible.name: placeholderText
                color: root.theme.bright; font.pixelSize: root.theme.dp(14); selectByMouse: true
                maximumLength: 512
                background: Rectangle { color: root.theme.field; radius: root.theme.dp(root.theme.controlRadius); border.width: query.activeFocus ? root.theme.dp(2) : 1; border.color: query.activeFocus ? root.theme.focusRing : root.theme.border }
                onAccepted: root.search()
            }
            RedesignButton {
                objectName: "addonDiscoverySearch"
                caption: qsTr("Search"); text: caption; enabled: query.enabled
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.search()
            }
        }
        Text {
            objectName: "addonDiscoveryLoading"
            Layout.fillWidth: true; visible: root.displayLoading || !root.displayAvailable || root.sourceChecking
            text: (root.backend && root.backend.preparationError) || (root.displayLoading ? qsTr("The app updates the sources…") : root.sourceChecking ? qsTr("The app checks the source settings…") : qsTr("Sources are not available yet."))
            textFormat: Text.PlainText; wrapMode: Text.Wrap
            color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
        }
        Text {
            Layout.fillWidth: true; visible: root.viewState.busy === true
            text: qsTr("Source request in progress."); textFormat: Text.PlainText
            color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
        }
        ListView {
            id: list
            objectName: "addonDiscoveryItems"
            Layout.fillWidth: true; Layout.fillHeight: true
            model: rows; clip: true; boundsBehavior: Flickable.StopAtBounds
            C.ScrollBar.vertical: C.ScrollBar { }
            delegate: RedesignButton {
                required property string ref
                required property string title
                required property string kind
                required property bool available
                objectName: "addonDiscoveryItem_" + ref
                width: ListView.view.width; height: root.theme.dp(44)
                text: title; caption: title; captionTextFormat: Text.PlainText; tooltipTextFormat: Text.PlainText
                captionMaximumWidth: Math.max(0, width - root.theme.dp(80))
                iconName: kind === "node" ? "folder" : "music"; leftAligned: true
                enabled: available && root.actionable
                selected: root.selected.includes(ref)
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: kind === "node" ? root.backend.browse(root.viewId, ref) : root.select(ref)
            }
            footer: RedesignButton {
                objectName: "addonDiscoveryMore"
                visible: root.viewState.hasMore === true
                height: visible ? implicitHeight : 0
                caption: qsTr("More items"); text: caption; enabled: root.actionable && root.viewState.busy !== true
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.backend.loadMore(root.viewId)
            }
        }
        Text {
            Layout.fillWidth: true
            visible: root.actionable && rows.count === 0 && root.viewState.busy !== true && !root.viewState.error && !root.actionError
            text: !root.viewId.length ? qsTr("Select a source.") : root.canSearch ? qsTr("Use search to find media.") : qsTr("This source has no items.")
            textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
        }
        Text {
            Layout.fillWidth: true; visible: text.length > 0
            text: root.actionError || root.viewState.error || ""; textFormat: Text.PlainText; wrapMode: Text.Wrap
            color: root.theme.danger; font.pixelSize: root.theme.dp(13)
            Accessible.role: Accessible.AlertMessage
        }
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.playlistPickerOpen && root.playlistActionsAvailable
            RowLayout {
                Layout.fillWidth: true
                C.TextField {
                    id: playlistQuery
                    objectName: "addonDiscoveryPlaylistQuery"
                    Layout.fillWidth: true; maximumLength: 256
                    placeholderText: qsTr("Search playlists"); Accessible.name: placeholderText
                    color: root.theme.bright; font.pixelSize: root.theme.dp(14)
                    background: Rectangle { color: root.theme.field; radius: root.theme.dp(root.theme.controlRadius); border.width: 1; border.color: root.theme.border }
                    onAccepted: { root.playlistChoice = ""; root.backend.loadPlaylists(root.viewId, text) }
                }
                RedesignButton {
                    objectName: "addonDiscoveryPlaylistSearch"
                    caption: qsTr("Search"); text: caption; enabled: root.actionable && root.viewState.playlistBusy !== true
                    scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                    onClicked: { root.playlistChoice = ""; root.backend.loadPlaylists(root.viewId, playlistQuery.text) }
                }
            }
            RedesignComboBox {
                objectName: "addonDiscoveryPlaylistChoices"
                Layout.fillWidth: true
                readonly property var choices: [{ref: "", name: qsTr("Select playlist")}].concat(root.viewState.playlists || [])
                model: choices; textRole: "name"; valueRole: "ref"; labelTextFormat: Text.PlainText
                currentIndex: Math.max(0, choices.findIndex(entry => entry.ref === root.playlistChoice))
                enabled: root.actionable && root.viewState.playlistBusy !== true
                Accessible.name: qsTr("Playlist")
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onActivated: index => {
                    root.playlistChoice = choices[index].ref
                    if (root.playlistChoice.length) root.backend.choosePlaylist(root.viewId, root.playlistChoice)
                    else root.backend.clearPlaylist(root.viewId)
                }
            }
            Text {
                Layout.fillWidth: true; textFormat: Text.PlainText; wrapMode: Text.Wrap
                text: root.viewState.playlistError || (root.viewState.playlistBusy ? qsTr("The app must prepare the playlist.")
                    : root.viewState.morePlaylists ? qsTr("Use search to find other playlists.")
                    : !(root.viewState.playlists || []).length ? qsTr("No saved playlists match this search.") : "")
                visible: text.length > 0; color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
            }
            RowLayout {
                RedesignButton {
                    objectName: "addonDiscoveryPlaylistCancel"
                    caption: qsTr("Cancel"); text: caption
                    scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                    onClicked: root.cancelPlaylist()
                }
                RedesignButton {
                    objectName: "addonDiscoveryPlaylistConfirm"
                    caption: qsTr("Add"); text: caption
                    enabled: root.actionable && root.selected.length > 0 && root.viewState.canAddToPlaylist === true && root.viewState.playlistBusy !== true
                    scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                    onClicked: root.addToPlaylist()
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            RedesignButton {
                objectName: "addonDiscoveryPlay"
                caption: qsTr("Play"); text: caption; enabled: root.actionable && root.selected.length === 1 && root.viewState.canPlay === true
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.play()
            }
            RedesignButton {
                objectName: "addonDiscoveryEnqueue"
                caption: qsTr("Add to queue"); text: caption; enabled: root.actionable && root.selected.length > 0 && root.viewState.canEnqueue === true
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.enqueue()
            }
            RedesignButton {
                objectName: "addonDiscoveryPlaylist"
                visible: root.playlistActionsAvailable
                caption: qsTr("Add to playlist"); text: caption; enabled: root.actionable && root.selected.length > 0
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: {
                    root.playlistPickerOpen = true; root.playlistChoice = ""
                    root.backend.loadPlaylists(root.viewId, playlistQuery.text)
                }
            }
            RedesignButton {
                objectName: "addonDiscoveryFill"
                visible: root.viewState.canFillQueue === true && root.queueFill.active !== true
                caption: qsTr("Keep the queue filled"); enabled: root.actionable; tooltipTextFormat: Text.PlainText
                text: qsTr("Keep the queue filled: add suggestions from this source to the end of the queue as it plays. The app keeps up to five ahead and changes nothing else in the queue.")
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.backend.startQueueFill(root.viewId, 5)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.queueFill.active === true || (root.queueFill.error || "").length > 0
            Text {
                objectName: "addonDiscoveryFillStatus"
                Layout.fillWidth: true; textFormat: Text.PlainText; wrapMode: Text.Wrap
                color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
                text: root.queueFill.active !== true ? root.queueFill.error || ""
                    : root.queueFill.paused === true ? qsTr("Paused: %1 adds no items to the queue.").arg(root.queueFill.sourceName || qsTr("a source"))
                    : qsTr("%1 keeps the queue filled: %2 added.").arg(root.queueFill.sourceName || qsTr("A source")).arg(root.queueFill.added || 0)
            }
            RedesignButton {
                objectName: "addonDiscoveryFillPause"
                visible: root.queueFill.active === true
                caption: root.queueFill.paused === true ? qsTr("Resume") : qsTr("Pause"); text: caption
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.backend.pauseQueueFill(root.queueFill.paused !== true)
            }
            RedesignButton {
                objectName: "addonDiscoveryFillStop"
                visible: root.queueFill.active === true
                caption: qsTr("Stop"); text: caption
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.backend.stopQueueFill()
            }
        }
    }
}

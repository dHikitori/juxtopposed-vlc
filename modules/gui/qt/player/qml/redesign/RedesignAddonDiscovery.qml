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
    property bool playlistActionsAvailable: false
    property string viewId: ""
    property string sourceId: ""
    property var viewState: ({})
    property var selected: []
    property string actionError: ""
    property bool ready: false
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme }
    readonly property var sources: { if (!backend) return []; backend.revision; return backend.sources || [] }
    readonly property var source: sources.find(entry => entry.id === sourceId) || ({})
    readonly property var sourceChoices: [{value: "", label: qsTr("Select source")}].concat(sources.map(entry => ({value: entry.id, label: entry.name || entry.id})))
    readonly property bool canSearch: (source.operations || []).includes("search")
    signal configureRequested(string packageId)
    signal playlistRequested(var itemReferences)
    function closeView() { if (backend && viewId.length) backend.closeView(viewId); viewId = ""; viewState = ({}); rows.clear(); selected = [] }
    function openSource(id) {
        closeView(); sourceId = id; actionError = ""; query.text = ""
        if (!id.length) return
        viewId = backend.openSource(id)
        if (viewId.length) reload()
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
    function select(ref) { selected = selected.includes(ref) ? selected.filter(value => value !== ref) : selected.concat([ref]) }
    function search() { if (viewId.length) { selected = []; actionError = ""; backend.search(viewId, query.text) } }
    function enqueue() {
        actionError = ""
        if (!backend.enqueue(viewId, selected)) actionError = qsTr("The app cannot add this selection to the queue.")
    }
    function play() {
        actionError = ""
        if (!backend.play(viewId, selected[0])) actionError = qsTr("The app cannot play this item.")
    }
    Component.onCompleted: ready = true
    Component.onDestruction: closeView()
    onVisibleChanged: if (ready && !visible) closeView()
    Connections {
        target: root.backend
        function onViewChanged(id) { if (id === root.viewId) root.reload() }
        function onViewClosed(id) {
            if (id !== root.viewId) return
            root.viewId = ""; root.viewState = ({}); rows.clear(); root.selected = []
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
                caption: qsTr("Open source"); text: caption; enabled: root.sourceId.length > 0
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.openSource(root.sourceId)
            }
            RedesignButton {
                objectName: "addonDiscoveryConfigure"
                caption: qsTr("Settings"); text: caption; enabled: root.sourceId.length > 0
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.configureRequested(root.sourceId.split("/")[0])
            }
        }
        RowLayout {
            Layout.fillWidth: true
            RedesignButton {
                objectName: "addonDiscoveryBack"
                iconName: "back"; text: qsTr("Back"); enabled: root.viewState.canGoBack === true
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.backend.back(root.viewId)
            }
            C.TextField {
                id: query
                objectName: "addonDiscoveryQuery"
                Layout.fillWidth: true; enabled: root.viewId.length > 0 && root.canSearch
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
                enabled: available
                selected: root.selected.includes(ref)
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: kind === "node" ? root.backend.browse(root.viewId, ref) : root.select(ref)
            }
            footer: RedesignButton {
                objectName: "addonDiscoveryMore"
                visible: root.viewState.hasMore === true
                height: visible ? implicitHeight : 0
                caption: qsTr("More items"); text: caption; enabled: root.viewState.busy !== true
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.backend.loadMore(root.viewId)
            }
        }
        Text {
            Layout.fillWidth: true
            visible: rows.count === 0 && root.viewState.busy !== true && !root.viewState.error && !root.actionError
            text: !root.viewId.length ? qsTr("Select a source.") : root.canSearch ? qsTr("Use search to find media.") : qsTr("This source has no items.")
            textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
        }
        Text {
            Layout.fillWidth: true; visible: text.length > 0
            text: root.actionError || root.viewState.error || ""; textFormat: Text.PlainText; wrapMode: Text.Wrap
            color: root.theme.danger; font.pixelSize: root.theme.dp(13)
            Accessible.role: Accessible.AlertMessage
        }
        RowLayout {
            Layout.fillWidth: true
            RedesignButton {
                objectName: "addonDiscoveryPlay"
                caption: qsTr("Play"); text: caption; enabled: root.selected.length === 1 && root.viewState.canPlay === true
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.play()
            }
            RedesignButton {
                objectName: "addonDiscoveryEnqueue"
                caption: qsTr("Add to queue"); text: caption; enabled: root.selected.length > 0 && root.viewState.canEnqueue === true
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.enqueue()
            }
            RedesignButton {
                objectName: "addonDiscoveryPlaylist"
                visible: root.playlistActionsAvailable
                caption: qsTr("Add to playlist"); text: caption; enabled: root.selected.length > 0
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.playlistRequested(root.selected.slice())
            }
        }
    }
}

// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

C.Dialog {
    id: root
    popupType: C.Popup.Item
    property real scaleFactor: 1
    property bool lightTheme: false
    property color accentColor: "#d37918"
    property Component videoBackdropContent
    property var playlistModel
    property var addonRegistry: typeof redesignAddonUi === "undefined" ? null : redesignAddonUi
    property int addonDialogGeneration: 0
    property int addonPlaylistRevision: 0
    property int addonDataRequest: 0
    property bool addonChoicesReady: false
    property var addonTargets: []
    property bool addonFallback: false
    readonly property var addonReplacement: { if (!addonRegistry) return ({}); addonRegistry.revision; return addonRegistry.replacement("dialog.add-to-playlist.content@1") }
    readonly property string addonSelectionId: addonReplacement.id || ""
    readonly property bool addonWanted: addonSelectionId.length > 0 && !addonFallback
    property Item scrollFocusItem
    property int itemCount: 0
    property bool busy: false
    property string errorMessage: ""
    property string selectedKey: ""
    property int selectedIndex: -1
    property int selectedRevision: -1
    property int selectedGeneration: -1
    property int targetRequest: 0
    property bool checkingTarget: false
    readonly property bool currentSelection: selectedKey.length > 0 && selectedIndex >= 0
        && selectedRevision === addonPlaylistRevision && selectedGeneration === addonDialogGeneration
    property alias newName: nameField.text
    property alias filterText: searchField.text
    readonly property bool valid: itemCount > 0 && !checkingTarget
        && (addonWanted ? addonPicker.prepared && addonPicker.usingReplacement && !!addonPicker.preview : currentSelection || newName.trim().length > 0)
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    signal saveRequested(string key, string name)
    title: qsTr("Add to Playlist")
    anchors.centerIn: parent
    width: Math.min(theme.dp(713), parent ? parent.width - theme.dp(32) : theme.dp(713))
    height: Math.min(theme.dp(589), parent ? parent.height - theme.dp(32) : theme.dp(589))
    padding: 0; spacing: 0
    modal: true; dim: true; focus: true
    closePolicy: busy ? C.Popup.NoAutoClose : C.Popup.CloseOnEscape
    background: RedesignDialogSurface { theme: root.theme; videoBackdropContent: root.videoBackdropContent }
    C.Overlay.modal: Rectangle { color: "#80000000" }
    function begin(count) {
        if (visible) return false
        clearSelection(); itemCount = count; newName = ""; filterText = ""; errorMessage = ""
        addonFallback = false; ++addonDialogGeneration
        open()
        return true
    }
    function submit() {
        if (!valid || busy) return
        if (addonWanted) { if (addonPicker.prepared && addonPicker.usingReplacement) addonPicker.confirm(); return }
        submitCore()
    }
    function submitCore() {
        if (!visible || busy || checkingTarget || itemCount <= 0) return
        const name = newName.trim()
        if (name.length) { busy = true; errorMessage = ""; saveRequested("", name); return }
        if (!currentSelection) return
        const key = selectedKey, index = selectedIndex, revision = selectedRevision, generation = selectedGeneration
        readCurrentTarget(index, key, revision, generation, () => {
            if (!currentSelection || selectedKey !== key || newName.trim().length) return
            busy = true; errorMessage = ""; saveRequested(key, "")
        })
    }
    function clearSelection() {
        ++targetRequest; checkingTarget = false
        selectedKey = ""; selectedIndex = -1; selectedRevision = -1; selectedGeneration = -1
    }
    function readCurrentTarget(index, key, revision, generation, complete) {
        const model = playlistModel, request = ++targetRequest, owner = root
        checkingTarget = true
        function received(data) {
            if (!Qt.isQtObject(owner) || request !== owner.targetRequest) return
            owner.checkingTarget = false
            if (!owner.visible || owner.busy || model !== owner.playlistModel
                || revision !== owner.addonPlaylistRevision || generation !== owner.addonDialogGeneration) return
            const row = Array.isArray(data) && data.length === 1 ? data[0] : null
            if (!row || row.id === undefined || row.id === null || row.id.toString() !== key || row.readOnly === true) {
                owner.invalidateAddonChoices(); owner.errorMessage = qsTr("The playlist changed. Select it again."); return
            }
            complete()
        }
        if (!model || !Number.isInteger(index) || index < 0 || index >= Number(model.count)) { received([]); return }
        if (typeof model.getDataFlat === "function") model.getDataFlat([index], received)
        else if (typeof model.get === "function") received([model.get(index)])
        else received([])
    }
    function chooseBuiltinTarget(index, key) {
        if (!visible || busy || addonWanted) return
        clearSelection(); newName = ""; errorMessage = ""
        const revision = addonPlaylistRevision, generation = addonDialogGeneration
        readCurrentTarget(index, key, revision, generation, () => {
            if (addonWanted) return
            selectedIndex = index; selectedRevision = revision; selectedGeneration = generation; selectedKey = key
        })
    }
    function acceptAddonSelection(selection) {
        if (!visible || busy || !addonWanted || !addonChoicesReady) return
        clearSelection(); newName = selection.name
        if (selection.key.length) {
            const index = addonTargets.findIndex(row => row.key === selection.key && row.enabled)
            if (index < 0) return
            selectedIndex = index; selectedRevision = addonPlaylistRevision
            selectedGeneration = addonDialogGeneration; selectedKey = selection.key
        }
        submitCore()
    }
    function finish(success, message) {
        busy = false
        if (success) accept()
        else { errorMessage = message || qsTr("VLC cannot save this selection. Try again."); addonPicker.begin() }
    }
    function invalidateAddonChoices() {
        clearSelection()
        ++addonPlaylistRevision; ++addonDataRequest; addonChoicesReady = false
        Qt.callLater(reloadAddonChoices)
    }
    function reloadAddonChoices() {
        const request = ++addonDataRequest
        addonChoicesReady = false
        if (!visible || !addonRegistry || !addonWanted || !playlistModel || busy) return
        const count = Number(playlistModel.count)
        if (!Number.isInteger(count) || count < 0 || count > 200) { addonFallback = true; return }
        const owner = root
        function received(data) {
            if (!Qt.isQtObject(owner) || !owner.visible || request !== owner.addonDataRequest || !Array.isArray(data) || data.length !== count) return
            const choices = []
            for (let index = 0; index < data.length; ++index) {
                const row = data[index]
                if (!row || row.id === undefined || row.id === null) { owner.addonFallback = true; return }
                const key = row.id.toString()
                if (!key.length) { owner.addonFallback = true; return }
                choices.push({key: key, title: String(row.name || qsTr("Playlist %1").arg(index + 1)).slice(0, 512), enabled: row.readOnly !== true})
            }
            owner.addonTargets = choices; owner.addonChoicesReady = true
        }
        if (typeof playlistModel.getDataFlat === "function") playlistModel.getDataFlat(Array.from({length: count}, (_, index) => index), received)
        else if (typeof playlistModel.get === "function") received(Array.from({length: count}, (_, index) => playlistModel.get(index)))
    }
    onAddonReplacementChanged: invalidateAddonChoices()
    onAddonSelectionIdChanged: addonFallback = false
    onPlaylistModelChanged: invalidateAddonChoices()
    onVisibleChanged: {
        if (visible) Qt.callLater(reloadAddonChoices)
        else { clearSelection(); ++addonDataRequest; addonChoicesReady = false; addonTargets = [] }
    }
    Connections {
        target: root.playlistModel || null
        ignoreUnknownSignals: true
        function onCountChanged() { root.invalidateAddonChoices() }
        function onModelReset() { root.invalidateAddonChoices() }
        function onRowsInserted() { root.invalidateAddonChoices() }
        function onRowsRemoved() { root.invalidateAddonChoices() }
        function onDataChanged() { root.invalidateAddonChoices() }
        function onLayoutChanged() { root.invalidateAddonChoices() }
    }
    function revealOnFocus(item) {
        scrollFocusItem = item
        revealFocusedItem()
    }
    function revealFocusedItem() { Qt.callLater(revealFocusedItemNow) }
    function revealFocusedItemNow() {
        const item = scrollFocusItem
        if (!root.visible || !item || !item.activeFocus) return
        const viewport = scroll.contentItem
        const point = item.mapToItem(viewport.contentItem, 0, 0)
        let target = viewport.contentY
        if (point.y < target || item.height >= viewport.height) target = point.y - root.theme.dp(8)
        else if (point.y + item.height > target + viewport.height)
            target = point.y + item.height + root.theme.dp(8) - viewport.height
        viewport.contentY = Math.max(0, Math.min(target, viewport.contentHeight - viewport.height))
    }
    function moveDestinationFocus(index, direction) {
        for (let next = index + direction; next >= 0 && next < destinations.count; next += direction) {
            destinations.positionViewAtIndex(next, ListView.Contain)
            destinations.forceLayout()
            const item = destinations.itemAtIndex(next)
            if (item && item.enabled) {
                item.forceActiveFocus(Qt.TabFocusReason)
                return
            }
        }
        destinations.positionViewAtIndex(destinations.currentIndex, ListView.Contain)
        revealFocusedItem()
    }
    component PlaylistField: C.TextField {
        enabled: !root.busy; selectByMouse: true
        color: root.theme.settingsHeading; placeholderTextColor: root.lightTheme ? root.theme.foreground : "#404040"
        selectionColor: root.theme.accent; selectedTextColor: root.theme.field
        font.pixelSize: root.theme.dp(14); leftPadding: root.theme.dp(14)
        onActiveFocusChanged: if (activeFocus) root.revealOnFocus(this)
        background: Rectangle {
            color: root.theme.field; radius: root.theme.dp(6); border.width: root.theme.dp(2)
            border.color: parent.activeFocus ? root.theme.accent : root.lightTheme ? root.theme.border : "#191919"
        }
        onAccepted: root.submit()
    }
    onNewNameChanged: { if (newName.length) clearSelection(); errorMessage = "" }
    onFilterTextChanged: { clearSelection(); errorMessage = "" }
    onOpened: searchField.forceActiveFocus()
    Shortcut { sequence: "Ctrl+Return"; enabled: root.visible && root.valid && !root.busy; onActivated: root.submit() }
    Shortcut { sequence: "Ctrl+Enter"; enabled: root.visible && root.valid && !root.busy; onActivated: root.submit() }
    header: Item {
        implicitHeight: root.theme.dp(50)
        Rectangle {
            anchors.fill: parent; radius: root.theme.dp(14); color: root.theme.field
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: parent.height / 2; color: parent.color }
        }
        Text {
            anchors.left: parent.left; anchors.leftMargin: root.theme.dp(20); anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, parent.width - root.theme.dp(root.addonReplacement.id ? 270 : 72))
            text: root.title; textFormat: Text.PlainText; elide: Text.ElideRight; color: root.theme.foreground; font.pixelSize: root.theme.dp(14)
        }
        RedesignButton {
            objectName: "addonPlaylistRecovery"
            anchors.right: parent.right; anchors.rightMargin: root.theme.dp(52); anchors.verticalCenter: parent.verticalCenter
            caption: root.width >= root.theme.dp(500) ? qsTr("Use built-in view") : ""; iconName: "back"; text: qsTr("Use built-in view")
            tooltipTextFormat: Text.PlainText; visible: !!root.addonReplacement.id; enabled: !root.busy
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            onClicked: addonPicker.useBuiltin()
        }
        RedesignButton {
            objectName: "addToPlaylistClose"
            anchors.right: parent.right; anchors.rightMargin: root.theme.dp(7); anchors.verticalCenter: parent.verticalCenter
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            iconName: "close"; text: qsTr("Close"); enabled: !root.busy
            onClicked: root.reject()
        }
    }
    contentItem: Item {
      C.ScrollView {
        id: scroll
        anchors.fill: parent
        visible: !addonPicker.usingReplacement
        enabled: !root.addonWanted
        contentWidth: availableWidth; clip: true
        C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
        onAvailableHeightChanged: if (root.opened) root.revealFocusedItem()
        onContentHeightChanged: if (root.opened) root.revealFocusedItem()
        Column {
            width: scroll.availableWidth
            Item {
                width: parent.width
                height: Math.max(root.theme.dp(100), help.y + help.height + root.theme.dp(23))
                Text {
                    id: heading
                    x: root.theme.dp(20); width: parent.width - root.theme.dp(40)
                    y: root.theme.dp(42) - baselineOffset
                    text: qsTr("Select a Playlist"); color: root.theme.settingsHeading
                    font.pixelSize: root.theme.dp(16); wrapMode: Text.WordWrap
                }
                Text {
                    id: help
                    x: root.theme.dp(20); width: parent.width - root.theme.dp(40)
                    y: Math.max(root.theme.dp(73) - baselineOffset, heading.y + heading.height + root.theme.dp(10))
                    text: qsTr("Add the selected media to a saved playlist, or create a new playlist.")
                    color: root.theme.playerLabel; font.pixelSize: root.theme.dp(14); wrapMode: Text.WordWrap
                }
            }
            PlaylistField {
                id: searchField
                objectName: "addToPlaylistSearch"
                x: root.theme.dp(20); width: parent.width - root.theme.dp(40); height: root.theme.dp(37)
                placeholderText: qsTr("Search playlists"); Accessible.name: placeholderText
                leftPadding: root.theme.dp(42)
                RedesignIcon {
                    x: root.theme.dp(8); y: (parent.height - height) / 2
                    width: root.theme.dp(24); height: width
                    name: "search"; color: root.theme.foreground
                }
            }
            Item { width: 1; height: root.theme.dp(12) }
            ListView {
                id: destinations
                objectName: "playlistDestinations"
                x: root.theme.dp(20); width: parent.width - root.theme.dp(40); height: root.theme.dp(180)
                clip: true; model: root.playlistModel
                C.ScrollBar.vertical: C.ScrollBar { }
                delegate: C.ItemDelegate {
                    id: destination
                    required property var model
                    required property int index
                    readonly property string key: model.id.toString()
                    objectName: "playlistDestination" + index
                    width: destinations.width; height: root.theme.dp(48)
                    padding: root.theme.dp(8)
                    Accessible.name: model.name
                    Accessible.selected: highlighted
                    Keys.onDownPressed: root.moveDestinationFocus(index, 1)
                    Keys.onUpPressed: root.moveDestinationFocus(index, -1)
                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_Home) root.moveDestinationFocus(-1, 1)
                        else if (event.key === Qt.Key_End) root.moveDestinationFocus(destinations.count, -1)
                        else return
                        event.accepted = true
                    }
                    onActiveFocusChanged: if (activeFocus && root && root.visible && ListView.view) {
                        const view = ListView.view
                        const position = index
                        root.revealOnFocus(destination)
                        view.currentIndex = position
                        if (Qt.isQtObject(view)) view.positionViewAtIndex(position, ListView.Contain)
                    }
                    enabled: !root.busy && model.readOnly !== true
                    highlighted: root.selectedKey === key
                    onClicked: root.chooseBuiltinTarget(index, key)
                    background: Rectangle {
                        radius: root.theme.dp(6)
                        color: destination.highlighted ? root.theme.field : destination.hovered ? root.theme.hover : "transparent"
                        border.color: destination.visualFocus ? root.theme.accent : "transparent"
                    }
                    contentItem: RowLayout {
                        spacing: root.theme.dp(12); opacity: destination.enabled ? 1 : 0.35
                        RedesignLibraryArtwork {
                            Layout.preferredWidth: root.theme.dp(32); Layout.preferredHeight: root.theme.dp(32)
                            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                            source: destination.model.thumbnail || ""; iconName: "playlist"; iconSize: 24; cornerRadius: 2
                        }
                        Text {
                            objectName: "playlistDestinationLabel"
                            Layout.fillWidth: true; text: destination.model.name; elide: Text.ElideRight
                            textFormat: Text.PlainText
                            color: root.theme.playerHeading; font.pixelSize: root.theme.dp(14)
                        }
                        RedesignIcon {
                            Layout.preferredWidth: root.theme.dp(24); Layout.preferredHeight: root.theme.dp(24)
                            name: "check-active"; visible: destination.highlighted; color: root.theme.accent
                        }
                        Text {
                            visible: destination.model.readOnly === true
                            text: qsTr("Read only"); color: root.theme.playerLabel; font.pixelSize: root.theme.dp(12)
                        }
                    }
                }
                Text {
                    anchors.fill: parent; anchors.margins: root.theme.dp(20)
                    visible: destinations.count === 0
                    text: root.filterText.length ? qsTr("No playlists match your search.") : qsTr("VLC has no saved playlists.")
                    color: root.theme.playerLabel; font.pixelSize: root.theme.dp(14); wrapMode: Text.WordWrap
                    verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter
                }
            }
            Item { width: 1; height: root.theme.dp(20) }
            Rectangle {
                x: root.theme.dp(20); width: parent.width - root.theme.dp(40)
                height: root.theme.dp(1); color: root.lightTheme ? root.theme.border : "#222222"
            }
            Item {
                width: parent.width; height: Math.max(root.theme.dp(57), createHeading.y + createHeading.height + root.theme.dp(23))
                Text {
                    id: createHeading
                    x: root.theme.dp(20); width: parent.width - root.theme.dp(40)
                    y: root.theme.dp(27) - baselineOffset
                    text: qsTr("Or create a new playlist"); color: root.theme.settingsHeading
                    font.pixelSize: root.theme.dp(16); wrapMode: Text.WordWrap
                }
            }
            PlaylistField {
                id: nameField
                objectName: "addToPlaylistName"
                x: root.theme.dp(20); width: parent.width - root.theme.dp(40); height: root.theme.dp(37)
                placeholderText: qsTr("Playlist name"); Accessible.name: placeholderText
            }
            Item { width: 1; height: root.theme.dp(20) }
        }
      }
      RedesignAddonSelection {
        id: addonPicker
        objectName: "addonPlaylistSelection"
        anchors.fill: parent; anchors.margins: root.theme.dp(20)
        registry: root.addonRegistry; hook: "dialog.add-to-playlist.content@1"; kind: "playlist"
        choiceSet: "playlist:" + root.addonDialogGeneration + ":" + root.addonPlaylistRevision + ":" + root.itemCount
        targets: root.addonTargets; active: root.visible && root.addonChoicesReady && root.addonWanted
        visible: resolved; busy: root.busy || root.checkingTarget; showActions: false; showRecovery: false
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        onConfirmed: selection => root.acceptAddonSelection(selection)
        onFallbackRequested: root.addonFallback = true
      }
    }
    footer: Item {
        implicitHeight: root.theme.dp(74) + (errorText.visible ? errorText.implicitHeight + root.theme.dp(12) : 0)
        Text {
            id: errorText
            objectName: "addToPlaylistError"
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.leftMargin: root.theme.dp(20); anchors.rightMargin: root.theme.dp(20); anchors.topMargin: root.theme.dp(8)
            visible: root.errorMessage.length > 0
            text: root.errorMessage; textFormat: Text.PlainText; color: root.theme.danger; font.pixelSize: root.theme.dp(14); wrapMode: Text.WordWrap
            Accessible.role: Accessible.AlertMessage
        }
        RowLayout {
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            anchors.margins: root.theme.dp(20)
            RedesignButton {
                objectName: "addToPlaylistCancel"
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                implicitHeight: root.theme.dp(32); implicitWidth: root.theme.dp(100)
                caption: qsTr("Cancel"); captionFontWeight: Font.Medium; text: caption; enabled: !root.busy
                background: Rectangle { color: parent.hovered ? root.theme.hover : root.theme.background; radius: root.theme.dp(6); border.color: parent.visualFocus ? root.theme.accent : "transparent" }
                onClicked: root.reject()
            }
            Item { Layout.fillWidth: true }
            RedesignButton {
                objectName: "addToPlaylistSave"
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                implicitHeight: root.theme.dp(32); implicitWidth: root.theme.dp(100)
                caption: root.busy ? qsTr("Saving…") : qsTr("Add"); text: caption
                primaryAction: true
                enabled: root.valid && !root.busy
                onClicked: root.submit()
            }
        }
    }
}

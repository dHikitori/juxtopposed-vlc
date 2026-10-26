// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

// Presentation shared by the VLC integration and the standalone QML checks.
FocusScope {
    id: root
    readonly property RedesignTime timeParser: RedesignTime {}
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    property bool presentationOnly: false
    property string playerLayout: ""
    readonly property alias layoutModel: chromeLayout
    readonly property alias mainMenuTrigger: mainMenuButton
    readonly property real controlsHeight: chrome.controlsHeight
    readonly property bool layoutReady: !chrome.applying && JSON.stringify(chrome.renderedLayout) === JSON.stringify(chromeLayout.layout)
    readonly property var chromeItems: chrome.allControls
    property var customControlStates: ({})
    property bool customMenuOpen: false
    signal customActionRequested(string action, Item anchor)
    property bool playing: false
    property bool started: false
    property bool hasVideo: false
    property bool audioOnly: false
    property bool seekable: false
    property bool pausable: true
    property bool hasPrevious: false
    property bool hasNext: false
    property bool muted: false
    property bool fullscreen: false
    property int autoHideMode: 1
    property int controlsHideTimeout: 1800
    property int cursorHideTimeout: 1000
    readonly property bool videoCursorVisible: !fullscreen || cursorHideTimer.running
    property bool showFullscreenControls: true
    property real fullscreenControlsOpacity: 1
    property int fullscreenControlsSensitivity: 3
    readonly property real chromeOpacity: fullscreen ? Math.max(0.1, Math.min(1, fullscreenControlsOpacity)) : 1
    readonly property color chromeBackground: Qt.rgba(theme.background.r, theme.background.g, theme.background.b, chromeOpacity)
    property point _lastControlMotion: Qt.point(-1, -1)
    property bool _headerRevealed: false
    property bool _toolbarRevealed: false
    property bool showStatistics: false
    property bool audioWaveformEnabled: false
    property var audioWaveformPeaks: []
    property bool showSubtitleTimeline: false
    property bool showSeekPreview: true
    property var subtitleTimelineCues: []
    property string subtitleTimelineState: "off"
    property real durationSeconds: 0
    property int subtitleDelayMS: 0
    signal subtitleDelayRequested(int milliseconds)
    property var statistics: ({})
    readonly property bool autoHideActive: started && hasVideo && (autoHideMode === 2 || (fullscreen && autoHideMode === 1))
    property bool maximized: false
    property bool minimalView: false
    onMinimalViewChanged: {
        if (minimalView) {
            libraryOpen = false
            queueOpen = false
            episodesOpen = false
            settingsPage = ""
            forceActiveFocus()
        }
    }
    property bool showWindowButtons: true
    property bool libraryOpen: false
    property bool libraryExpanded: false
    property bool miniPlayerVisible: true
    property string mediaFolderTitle: ""
    property bool canBrowseMedia: true
    onLibraryExpandedChanged: if (libraryExpanded) miniPlayerVisible = true
    property bool libraryInitialized: false
    onLibraryOpenChanged: if (!libraryOpen) libraryExpanded = false
    property bool queueOpen: false
    onQueueOpenChanged: if (queueOpen) episodesOpen = false
    property bool episodesOpen: false
    property bool episodesAvailable: false
    onEpisodesAvailableChanged: if (!episodesAvailable) episodesOpen = false
    readonly property bool floatingQueue: queueOpen && miniPlayer.visible
    property string settingsPage: ""
    onSettingsPageChanged: if (settingsPage.length > 0) episodesOpen = false
    property real position: 0
    property real volume: 1
    property real maxVolume: 1.25
    property real buffering: 1
    property string mediaTitle: qsTr("VLC Media Player")
    property string locationTitle: qsTr("Home")
    property string timeText: "00:00:00"
    property real timeLabelFontSize: 15
    property string lengthText: "00:00:00"
    property string artist: ""
    property url artwork
    property bool resumeAvailable: false
    property string resumeTimeText: ""
    property var timeForPosition: function(position) { return "" }
    property url seekPreviewImage
    property Component seekThumbnailContent
    property real seekPreviewImagePosition: -1
    signal seekPreviewRequested(real position)
    signal seekPreviewEnded()
    signal precisePositionUpdatesRequested(bool requested)
    function refreshSeekPreview() { if (libraryExpanded) miniPlayer.refreshPreview(); else seek.refreshPreview() }
    property Component titleBarDragContent
    property Component videoContent
    property Component videoBackdropContent
    property Component artworkToolbarBackdropContent
    property Component addonVisualizerContent
    property bool addonVisualizerActive: false
    readonly property alias backdropSource: backdropScene
    readonly property alias artworkBackdropSource: viewport
    property Component libraryContent
    property Component queueContent
    property Component settingsContent
    property Component episodesContent
    property QtObject trackPopup
    readonly property alias videoItem: videoLoader.item
    readonly property rect subtitleArea: videoCanvas.width > 0 && videoCanvas.height > 0
        ? Qt.rect(root.libraryExpanded ? 0 : viewport.x / videoCanvas.width,
                  root.libraryExpanded ? 0 : viewport.y / videoCanvas.height,
                  viewport.width / videoCanvas.width, viewport.height / videoCanvas.height)
        : Qt.rect(0, 0, 1, 1)
    readonly property alias libraryItem: libraryLoader.item
    readonly property alias settingsItem: settingsLoader.item
    readonly property alias queueItem: queueLoader.item
    readonly property real positionSliderY: {
        if (libraryExpanded) return miniPlayer.y + miniPlayer.seekY
        let y = 0
        for (let item = seek; item && item !== root; item = item.parent) y += item.y
        return y
    }
    readonly property bool controlsLocked: elapsedTime.editing || chromeLayout.editing || customMenuOpen || queueOpen || episodesOpen || libraryOpen || settingsPage.length > 0 || resumeAvailable || !!trackPopup && trackPopup.visible
    readonly property alias topVisible: header.visible
    readonly property alias bottomVisible: toolbar.visible
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }

    signal togglePlayback()
    signal previous()
    signal next()
    signal seekRequested(real value)
    signal seekToSecondsRequested(int seconds)
    function cancelTimeEdit() { elapsedTime.cancel() }
    signal seekDragStarted()
    signal seekDragFinished(real value, bool commit)
    signal volumeRequested(real value)
    signal volumeWheelRequested(var event)
    signal muteRequested()
    signal fullscreenRequested()
    signal minimizeRequested()
    signal maximizeRequested()
    signal closeRequested()
    signal audioTracksRequested(Item anchor)
    signal subtitleTracksRequested(Item anchor)
    signal menuRequested(Item anchor)
    signal contextMenuRequested(point globalPosition)
    signal openRequested()
    signal preferencesRequested()
    signal fullLibraryRequested()
    signal mediaFolderRequested()
    signal artistRequested()
    signal resumeRequested(bool resume)
    signal keyPressed(var event)

    Component.onDestruction: if (seek.dragActive) root.seekDragFinished(seek.dragPosition, false)

    component ChromeButton: RedesignButton {
        highlightSelection: false
        iconForeground: lightTheme ? theme.bright : theme.foreground
        focusPolicy: root.presentationOnly ? Qt.NoFocus : Qt.StrongFocus
    }

    // Preview the chrome without making its controls dim or focusable.
    MouseArea {
        anchors.fill: parent; z: 100
        enabled: root.presentationOnly; visible: enabled
        acceptedButtons: Qt.AllButtons
        onClicked: mouse => {
            if (mouse.button !== Qt.LeftButton) return
            const point = closeButton.mapFromItem(root, mouse.x, mouse.y)
            if (closeButton.visible && point.x >= 0 && point.y >= 0
                && point.x < closeButton.width && point.y < closeButton.height)
                root.closeRequested()
        }
        onWheel: wheel => wheel.accepted = true
    }

    // Panels consume right-clicks before they reach this background fallback.
    // A passive handler on the player would also observe their context clicks.
    MouseArea {
        anchors.fill: parent
        z: -1
        enabled: !root.hasVideo
        acceptedButtons: Qt.RightButton
        onClicked: mouse => root.contextMenuRequested(mapToGlobal(mouse.x, mouse.y))
    }

    // Observe outside presses without taking the click from its normal control.
    TapHandler {
        enabled: elapsedTime.editing
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressedChanged: {
            if (pressed && !timeEditor.contains(timeEditor.mapFromItem(root, point.position)))
                elapsedTime.cancel()
        }
    }

    function revealControls() { revealTimer.restart() }
    function revealCursor() { if (fullscreen) cursorHideTimer.restart() }
    function toggleSettings(page: string) {
        episodesOpen = false
        if (trackPopup) trackPopup.close()
        queueOpen = false
        settingsPage = settingsPage.length > 0 ? "" : page
    }
    function toggleTracks(anchor, page) {
        episodesOpen = false
        if (!trackPopup) return
        if (settingsPage === page) {
            settingsPage = ""
            trackPopup.close()
            return
        }
        settingsPage = ""
        queueOpen = false
        trackPopup.toggleFrom(anchor, page)
    }
    function dismissPanels() {
        episodesOpen = false
        settingsPage = ""
        queueOpen = false
        libraryOpen = false
    }

    Keys.onEscapePressed: event => {
        if (queueOpen || episodesOpen || libraryOpen || settingsPage.length > 0)
            dismissPanels()
        else if (fullscreen)
            fullscreenRequested()
        else
            event.accepted = false
    }
    function toggleEpisodes() {
        if (!episodesAvailable && !presentationOnly) return
        if (trackPopup) trackPopup.close()
        queueOpen = false; settingsPage = ""
        episodesOpen = !episodesOpen
    }
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab)
            revealControls()
        keyPressed(event)
    }
    Timer { id: revealTimer; interval: root.controlsHideTimeout; running: root.fullscreen }
    Timer { id: cursorHideTimer; interval: Math.max(0, root.cursorHideTimeout) }
    onFullscreenChanged: {
        _lastControlMotion = Qt.point(-1, -1)
        _headerRevealed = false
        _toolbarRevealed = false
        revealControls()
        if (fullscreen) revealCursor()
        else cursorHideTimer.stop()
    }
    function observeControlMotion(position: point) {
        const first = _lastControlMotion.x < 0
        const distance = Math.max(Math.abs(position.x - _lastControlMotion.x), Math.abs(position.y - _lastControlMotion.y))
        const moved = first || distance > Math.max(0, fullscreenControlsSensitivity)
        const atTop = position.y >= 0 && position.y < headerEdge.height
        const atBottom = position.y <= height && position.y >= height - bottomEdge.height
        if (!atTop) _headerRevealed = false
        else if (moved || header.visible) _headerRevealed = true
        if (!atBottom) _toolbarRevealed = false
        else if (moved || toolbar.visible) _toolbarRevealed = true
        if (moved) _lastControlMotion = position
    }
    HoverHandler {
        enabled: root.fullscreen
        blocking: false
        onPointChanged: if (hovered) {
            root.revealCursor()
            root.observeControlMotion(point.position)
        }
        onHoveredChanged: {
            if (hovered) {
                root.revealCursor()
                root.observeControlMotion(point.position)
            }
            else {
                root._lastControlMotion = Qt.point(-1, -1)
                root._headerRevealed = false
                root._toolbarRevealed = false
            }
        }
    }
    Item {
        id: backdropScene
        anchors.fill: parent
        Rectangle { anchors.fill: parent; z: -1; color: theme.background }
    }

    // Side panels cover the picture. Their state must not change the video
    // surface geometry or trigger a new video size/aspect ratio.
    Item {
        id: viewport
        parent: root.libraryExpanded ? miniPlayer.videoArea : backdropScene
        objectName: "redesignViewport"
        visible: !root.libraryExpanded || (root.started && miniPlayer.visible)
        z: 0
        anchors {
            top: parent.top
            bottom: parent.bottom
            left: parent.left
            right: parent.right
            topMargin: root.libraryExpanded || root.fullscreen ? 0 : root.minimalView ? theme.dp(10) : theme.dp(40)
            bottomMargin: root.libraryExpanded || root.fullscreen ? 0 : root.minimalView ? theme.dp(10) : root.controlsHeight
            leftMargin: root.libraryExpanded || root.fullscreen ? 0 : theme.dp(10)
            rightMargin: root.libraryExpanded || root.fullscreen ? 0 : theme.dp(10)
        }
        clip: true
        Rectangle { anchors.fill: parent; color: audioArtwork.ready && audioArtwork.visible ? "transparent" : "black"; radius: theme.dp(10) }
        Item {
            id: videoCanvas
            objectName: "redesignVideoCanvas"
            // Fit the picture to the client area, then mask it with the frame.
            // The mini player retains its own picture plane and aspect ratio.
            x: root.libraryExpanded ? 0 : -viewport.x
            y: root.libraryExpanded ? 0 : -viewport.y
            width: root.libraryExpanded ? viewport.width : root.width
            height: root.libraryExpanded ? viewport.height : root.height
            Loader { id: videoLoader; anchors.fill: parent; sourceComponent: root.videoContent }
        }
        Rectangle {
            // The outer border leaves a rounded opening over the native video layer.
            anchors.fill: parent
            anchors.margins: -theme.dp(root.libraryExpanded ? 5 : 10)
            color: "transparent"
            radius: theme.dp(root.libraryExpanded ? 10 : 19)
            border.width: theme.dp(root.libraryExpanded ? 5 : 10)
            border.color: theme.background
            visible: (root.libraryExpanded || !root.fullscreen) && !(audioArtwork.ready && audioArtwork.visible)
        }


        // An honest empty state; the supplied cat is reference footage only.
        Column {
            anchors.centerIn: parent
            width: Math.min(parent.width - theme.dp(40), theme.dp(420))
            spacing: theme.dp(18)
            visible: !root.started && !root.hasVideo
            RedesignIcon {
                anchors.horizontalCenter: parent.horizontalCenter
                width: theme.dp(48); height: width
                name: "cone"; color: theme.accent
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Drop a file to start playing")
                color: "#cccccc"
                font.pixelSize: theme.dp(18)
            }
            ChromeButton {
                objectName: "openFileButton"
                anchors.horizontalCenter: parent.horizontalCenter
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                iconName: "folder"; text: qsTr("Open file"); caption: text
                accent: true
                onClicked: root.openRequested()
            }
        }
        RedesignAudioArtwork {
            id: audioArtwork
            objectName: "audioArtworkView"
            anchors.fill: parent
            scaleFactor: root.scaleFactor
            source: visible ? root.artwork : ""
            title: root.mediaTitle; artist: root.artist
            showTitle: !root.libraryExpanded
            visible: root.started && !root.hasVideo
        }
        Loader {
            objectName: "addonVisualizerLoader"
            anchors.fill: parent
            active: root.started && !root.hasVideo && !!root.addonVisualizerContent
            visible: active
            sourceComponent: root.addonVisualizerContent
        }
        Column {
            anchors.centerIn: parent
            spacing: theme.dp(14)
            objectName: "audioArtworkFallback"
            visible: root.started && !root.hasVideo && !audioArtwork.ready && !root.addonVisualizerActive
            RedesignIcon {
                anchors.horizontalCenter: parent.horizontalCenter
                width: theme.dp(96); height: width
                name: "music"; color: theme.accent
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.max(0, viewport.width - theme.dp(40))
                text: root.mediaTitle
                visible: !root.libraryExpanded
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                color: "#e5e5e5"
                font.pixelSize: theme.dp(22)
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.artist; color: "#8f8f8f"; font.pixelSize: theme.dp(14)
                visible: !root.libraryExpanded
            }
        }
        Text {
            anchors.centerIn: parent
            visible: root.started && root.buffering < 1
            text: qsTr("Buffering %1%").arg(Math.round(root.buffering * 100))
            color: "white"
            style: Text.Outline; styleColor: "black"
            font.pixelSize: theme.dp(16)
        }
        RedesignStatistics {
            objectName: "playbackStatisticsOverlay"
            anchors.top: parent.top; anchors.left: parent.left
            anchors.margins: theme.dp(12)
            width: Math.max(0, Math.min(implicitWidth, parent.width - theme.dp(24)))
            height: Math.max(0, Math.min(implicitHeight, parent.height - theme.dp(24)))
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            statistics: root.statistics
            visible: root.showStatistics && root.started && !root.minimalView
        }
        Rectangle {
            objectName: "resumePlaybackPrompt"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: theme.dp(24)
            width: Math.max(0, Math.min(parent.width - theme.dp(24), theme.dp(550)))
            height: Math.max(theme.dp(52), resumeRow.implicitHeight + theme.dp(20))
            visible: root.resumeAvailable && !root.minimalView
            Accessible.role: Accessible.Dialog
            Accessible.name: resumeContinueButton.caption
            color: theme.lightTheme ? theme.panel : "#161616"
            radius: theme.dp(16)
            RowLayout {
                id: resumeRow
                anchors.fill: parent; anchors.margins: theme.dp(10)
                anchors.leftMargin: theme.dp(20)
                spacing: theme.dp(10)
                Text {
                    objectName: "resumePlaybackLabel"
                    readonly property string escapedTime: root.resumeTimeText.replace(/&/g, "&amp;")
                        .replace(/</g, "&lt;").replace(/>/g, "&gt;")
                    Layout.fillWidth: true
                    text: root.resumeTimeText.length ? qsTr("Continue from %1").arg("<b>" + escapedTime + "</b>")
                        : qsTr("Continue where you left off?")
                    textFormat: Text.StyledText
                    color: theme.bright; font.pixelSize: theme.dp(16)
                    font.weight: Font.Normal
                    wrapMode: Text.WordWrap
                }
                ChromeButton {
                    id: resumeContinueButton
                    objectName: "resumeContinueButton"
                    implicitHeight: theme.dp(32)
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    caption: root.audioOnly ? qsTr("Continue listening") : qsTr("Continue Watching")
                    text: caption; accent: true
                    onClicked: root.resumeRequested(true)
                }
                ChromeButton {
                    objectName: "resumeDismissButton"
                    Layout.preferredWidth: theme.dp(44)
                    implicitHeight: theme.dp(32)
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    filled: true; iconSize: theme.dp(18)
                    iconName: "close"; text: qsTr("Close")
                    onClicked: root.resumeRequested(false)
                }
            }
        }
    }

    Rectangle {
        id: library
        parent: backdropScene
        objectName: "redesignLibraryPanel"
        z: 1
        property real reveal: root.libraryOpen ? 1 : 0
        Behavior on reveal { NumberAnimation { duration: 150; easing.type: Easing.OutExpo } }
        visible: reveal > 0
        width: root.libraryExpanded ? root.width : Math.min(theme.dp(427), root.width * (root.width < theme.dp(900) ? 0.48 : 0.38))
        x: (reveal - 1) * width
        anchors.top: parent.top; anchors.bottom: parent.bottom
        anchors.topMargin: header.height; anchors.bottomMargin: root.libraryExpanded ? 0 : toolbar.height
        color: root.libraryExpanded ? theme.libraryPanel : "transparent"
        clip: true
        Rectangle {
            objectName: "redesignLibraryBody"
            visible: !root.libraryExpanded
            x: theme.dp(50)
            width: Math.max(0, parent.width - x)
            height: parent.height
            color: theme.libraryOverlay
        }
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: wheel => wheel.accepted = true }
        Loader {
            id: libraryLoader
            anchors.fill: parent
            sourceComponent: root.libraryContent
            active: root.libraryOpen || root.libraryInitialized
            enabled: root.libraryOpen
            onLoaded: root.libraryInitialized = true
        }
    }
    RedesignMiniPlayer {
        id: miniPlayer
        parent: backdropScene
        objectName: "redesignMiniPlayer"
        z: 2
        visible: root.libraryExpanded && root.libraryOpen && root.started && root.miniPlayerVisible
        width: Math.max(theme.dp(160), Math.min(theme.dp(391), root.width - theme.dp(12), (root.height - theme.dp(182)) * 16 / 9 + theme.dp(20)))
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        playing: root.playing; started: root.started; seekable: root.seekable; pausable: root.pausable
        hasPrevious: root.hasPrevious; hasNext: root.hasNext; hasVideo: root.hasVideo
        position: root.position; mediaTitle: root.mediaTitle; folderTitle: root.mediaFolderTitle; artist: root.artist
        canBrowseMedia: root.canBrowseMedia
        minimumY: Math.min(root.height - height - theme.dp(6), theme.dp(46) + (root.floatingQueue ? theme.dp(356) : 0))
        previewEnabled: root.showSeekPreview && !root.presentationOnly
        audioWaveformEnabled: root.audioWaveformEnabled
        audioWaveformPeaks: root.audioWaveformPeaks
        thumbnailContent: root.seekThumbnailContent
        videoBackdropContent: root.videoBackdropContent
        previewImage: root.seekPreviewImage; previewImagePosition: root.seekPreviewImagePosition
        timeForPosition: root.timeForPosition
        onTogglePlayback: root.togglePlayback()
        onPrevious: root.previous()
        onNext: root.next()
        onSeekRequested: position => root.seekRequested(position)
        onSeekDragStarted: root.seekDragStarted()
        onSeekDragFinished: (position, commit) => root.seekDragFinished(position, commit)
        onPreviewRequested: position => root.seekPreviewRequested(position)
        onPreviewEnded: root.seekPreviewEnded()
        onOpenPlayerRequested: { root.libraryExpanded = false; root.libraryOpen = false }
        onCloseRequested: { root.queueOpen = false; root.miniPlayerVisible = false }
        onQueueRequested: { root.settingsPage = ""; root.queueOpen = !root.queueOpen }
        onFolderRequested: root.mediaFolderRequested()
        onArtistRequested: root.artistRequested()
    }
    Rectangle {
        id: floatingQueuePanel
        objectName: "redesignFloatingQueue"
        z: 1.5
        visible: root.floatingQueue
        x: miniPlayer.x
        y: Math.max(theme.dp(46), miniPlayer.y - height)
        width: miniPlayer.width
        height: Math.min(theme.dp(356), Math.max(0, miniPlayer.y - theme.dp(46)))
        radius: theme.dp(14)
        color: theme.background
        // Continue the card behind the mini player without moving its video.
        Rectangle { y: parent.height - theme.dp(14); width: parent.width; height: theme.dp(28); color: parent.color }
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: wheel => wheel.accepted = true }
    }
    Rectangle {
        id: rightPanel
        objectName: "redesignRightPanel"
        z: 3
        visible: root.episodesOpen || (root.queueOpen && !root.floatingQueue) || root.settingsPage.length > 0
        width: Math.min(theme.dp(400), root.width * 0.48)
        anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.right: parent.right
        anchors.topMargin: header.height + theme.dp(20); anchors.bottomMargin: toolbar.height + theme.dp(20)
        anchors.rightMargin: theme.dp(20)
        color: theme.floatingPanel
        radius: theme.dp(10)
        border.width: 0
        Loader {
            anchors.fill: parent
            z: -1
            sourceComponent: root.videoBackdropContent
            active: root.episodesOpen || (root.queueOpen && !root.floatingQueue) || root.settingsPage.length > 0
        }
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: wheel => wheel.accepted = true }
        Loader {
            id: queueLoader
            parent: root.floatingQueue ? floatingQueuePanel : rightPanel
            anchors.fill: parent
            sourceComponent: root.queueContent
            active: root.queueOpen
            visible: active
            onLoaded: if (item && item.floating !== undefined) item.floating = Qt.binding(() => root.floatingQueue)
        }
        Loader {
            id: settingsLoader
            anchors.fill: parent
            sourceComponent: root.settingsContent
            active: root.settingsPage.length > 0
            visible: active
        }
        Loader {
            anchors.fill: parent
            sourceComponent: root.episodesContent
            active: root.episodesOpen
            visible: active
        }
    }

    // Fullscreen edges reveal independently; motion in the video stays unobtrusive.
    // Each detector contains the chrome it reveals. Hover on a child control
    // must not hide the toolbar and then expose the detector again.
    Item {
        id: headerEdge
        parent: backdropScene
        z: 4
        anchors.top: parent.top; width: parent.width; height: theme.dp(48)
        HoverHandler { id: headerHover; blocking: false }
    }
    Item {
        id: bottomEdge
        parent: backdropScene
        z: 4
        anchors.bottom: parent.bottom; width: parent.width; height: root.controlsHeight + theme.dp(10)
        HoverHandler { id: bottomHover; blocking: false }
    }

    Rectangle {
        id: header
        parent: headerEdge
        objectName: "redesignHeader"
        anchors.top: parent.top; width: parent.width; height: theme.dp(40)
        color: root.chromeBackground
        visible: !root.minimalView && (!root.fullscreen || (headerHover.hovered && root._headerRevealed) || header.activeFocus || revealTimer.running || root.controlsLocked)
        Loader { anchors.fill: parent; active: !chromeLayout.editing; sourceComponent: root.titleBarDragContent }
        RowLayout {
            id: headerLeft
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0
            ChromeButton {
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                implicitWidth: theme.dp(50)
                iconName: "cone"; text: qsTr("Open file"); foreground: theme.accent
                onClicked: root.openRequested()
            }
            ChromeButton {
                implicitWidth: theme.dp(30)
                id: mainMenuButton
                objectName: "mainMenuButton"
                visible: !root.presentationOnly
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                iconName: "more"; text: qsTr("Menu")
                onClicked: root.menuRequested(this)
            }
            ChromeButton {
                implicitWidth: {
                    const slot = chromeLayout.location("library")
                    return theme.dp(!slot || slot.zone.startsWith("header") ? 46 : 30)
                }
                id: libraryToggleButton
                objectName: "libraryToggleButton"
                visible: !root.presentationOnly && !root.libraryExpanded
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                iconName: "sidebar"; text: root.libraryOpen ? qsTr("Hide library") : qsTr("Show library")
                iconReveal: library.reveal
                Accessible.checkable: true; Accessible.checked: root.libraryOpen
                onClicked: root.libraryOpen = !root.libraryOpen
            }
            Rectangle {
                id: headerDivider
                readonly property bool inFooter: !!parent && parent.objectName.startsWith("customFooter")
                visible: !root.presentationOnly && !root.libraryExpanded
                // The lower toolbar already adds spacing between controls.
                // Retain the reference gap without the header's extra padding.
                Layout.leftMargin: theme.dp(inFooter ? 1 : 2)
                Layout.rightMargin: theme.dp(inFooter ? 0 : 6)
                width: theme.dp(1); height: theme.dp(18); color: theme.border
            }
            ChromeButton {
                id: mediaFolderButton
                objectName: "mediaFolderButton"
                captionVerticalOffset: 1
                implicitWidth: Math.max(theme.dp(36), contentItem.implicitWidth + theme.dp(15))
                property bool hasMediaFolder: root.started && root.canBrowseMedia && root.mediaFolderTitle.length > 0
                visible: !root.presentationOnly && !root.libraryExpanded && root.width > theme.dp(700)
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                iconName: "folder"
                caption: hasMediaFolder ? root.mediaFolderTitle : root.locationTitle
                captionMaximumWidth: {
                    const slot = chromeLayout.location("folder")
                    return theme.dp(!slot || slot.zone.startsWith("header") ? 220 : 100)
                }
                text: hasMediaFolder ? qsTr("Open media folder") : qsTr("Browse library")
                onClicked: {
                    if (hasMediaFolder) {
                        root.libraryOpen = true
                        root.mediaFolderRequested()
                    } else root.libraryOpen = !root.libraryOpen
                }
            }
        }
        Item {
            id: titleControl
            objectName: "mediaTitleControl"
            readonly property bool inHeader: parent === header || !!parent && parent.objectName.startsWith("customHeader")
            visible: inHeader || root.width > theme.dp(1200)
            anchors.centerIn: parent === header ? header : undefined
            width: parent === header ? Math.max(0, header.width - Math.max(headerLeft.width,
                windowControls.width - (closeButton.flushToCorner ? theme.dp(6) : 0)) * 2 - theme.dp(32)) : implicitWidth
            implicitWidth: Math.min(theme.dp(220), titleLabel.implicitWidth - titleLabel.leftPadding + theme.dp(14))
            implicitHeight: theme.dp(36)
            Layout.fillWidth: true
            Text {
                id: titleLabel
                anchors.fill: parent
                horizontalAlignment: titleControl.inHeader ? Text.AlignHCenter : Text.AlignLeft
                leftPadding: titleControl.inHeader ? 0 : theme.dp(6)
                verticalAlignment: Text.AlignVCenter
                text: root.mediaTitle
                font.pixelSize: theme.dp(14)
                elide: Text.ElideRight; color: theme.foreground
            }
        }
        Instantiator {
            model: [minimizeButton, maximizeButton, fullscreenButton, closeButton]
            delegate: QtObject {
                id: windowSize
                required property var modelData
                property Binding widthRule: Binding {
                    target: windowSize.modelData; property: "width"
                    value: windowSize.modelData.implicitWidth
                    when: windowSize.modelData.parent === windowControls
                    restoreMode: Binding.RestoreBindingOrValue
                }
                property Binding heightRule: Binding {
                    target: windowSize.modelData; property: "height"
                    value: windowSize.modelData.implicitHeight
                    when: windowSize.modelData.parent === windowControls
                    restoreMode: Binding.RestoreBindingOrValue
                }
            }
        }
        Row {
            id: windowControls
            anchors.right: parent.right; anchors.rightMargin: closeButton.flushToCorner ? 0 : theme.dp(6)
            anchors.verticalCenter: parent.verticalCenter
            ChromeButton {
                id: minimizeButton
                anchors.verticalCenter: parent === windowControls ? parent.verticalCenter : undefined
                visible: root.showWindowButtons
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                iconName: "minimize"; text: qsTr("Minimize")
                onClicked: root.minimizeRequested()
            }
            ChromeButton {
                id: maximizeButton
                anchors.verticalCenter: parent === windowControls ? parent.verticalCenter : undefined
                visible: root.showWindowButtons
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                iconName: "maximize"; text: root.maximized ? qsTr("Restore window") : qsTr("Maximize")
                onClicked: root.maximizeRequested()
            }
            ChromeButton {
                id: fullscreenButton
                anchors.verticalCenter: parent === windowControls ? parent.verticalCenter : undefined
                objectName: "fullscreenButton"
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                iconName: root.fullscreen ? "fullscreen-active" : "fullscreen"; text: root.fullscreen ? qsTr("Exit fullscreen") : qsTr("Fullscreen")
                onClicked: root.fullscreenRequested()
            }
            ChromeButton {
                id: closeButton
                objectName: "windowCloseButton"
                readonly property bool flushToCorner: root.showWindowButtons && (root.maximized || root.fullscreen) && !chromeLayout.editing
                    && (parent === windowControls || !!parent && parent.objectName === "customHeaderRight"
                        && chrome.renderedLayout && chrome.renderedLayout.zones.headerRight.slice(-1)[0] === "close")
                implicitWidth: theme.dp(flushToCorner ? 42 : 36)
                implicitHeight: theme.dp(flushToCorner ? 40 : 36)
                rightPadding: flushToCorner ? theme.dp(6) : 0
                visible: root.showWindowButtons
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                iconName: "close"; text: qsTr("Close")
                onClicked: root.closeRequested()
            }
        }
    }

    Rectangle {
        id: toolbar
        parent: bottomEdge
        objectName: "redesignToolbar"
        anchors.bottom: parent.bottom; width: parent.width; height: root.controlsHeight
        readonly property bool artworkVisible: audioArtwork.visible && audioArtwork.ready
        readonly property color artworkFill: root.lightTheme ? theme.background : "#191919"
        color: artworkVisible ? Qt.rgba(artworkFill.r, artworkFill.g, artworkFill.b,
            root.chromeOpacity * (theme.transparencyEnabled ? 0.95 : 1))
            : chromeLayout.editing || chrome.custom
                ? Qt.rgba(artworkFill.r, artworkFill.g, artworkFill.b, root.chromeOpacity) : root.chromeBackground
        visible: !root.minimalView && !root.libraryExpanded && (!root.fullscreen || root.showFullscreenControls || chromeLayout.editing)
            && (!root.autoHideActive || (bottomHover.hovered && (!root.fullscreen || root._toolbarRevealed)) || toolbar.activeFocus || revealTimer.running || root.controlsLocked)
        Loader {
            objectName: "artworkToolbarBackdropLoader"
            anchors.fill: parent
            z: -1
            active: toolbar.visible && toolbar.artworkVisible && theme.transparencyEnabled
            sourceComponent: root.artworkToolbarBackdropContent
        }
        Rectangle {
            width: parent.width; height: theme.dp(1)
            color: root.lightTheme ? theme.border : "#222222"
            visible: toolbar.artworkVisible || root.controlsHeight > theme.dp(54)
        }
        Rectangle {
            y: parent.height - theme.dp(54)
            width: parent.width; height: theme.dp(1)
            color: root.lightTheme ? theme.border : "#222222"
            visible: root.controlsHeight > theme.dp(54)
        }
        RowLayout {
            id: originalToolbar
            visible: !chrome.custom && !chrome.applying
            anchors.fill: parent
            anchors.topMargin: root.controlsHeight - theme.dp(54)
            anchors.leftMargin: theme.dp(20); anchors.rightMargin: theme.dp(17)
            spacing: theme.dp(6)
            ChromeButton {
                id: previousButton
                Layout.rightMargin: parent === originalToolbar ? theme.dp(2) : 0
                objectName: "previousButton"
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                implicitWidth: theme.dp(30); implicitHeight: theme.dp(28)
                iconName: "previous"; text: qsTr("Previous"); enabled: root.hasPrevious
                onClicked: root.previous()
            }
            ChromeButton {
                id: playButton
                Layout.rightMargin: parent === originalToolbar ? theme.dp(2) : 0
                objectName: "playButton"
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                implicitWidth: theme.dp(54); implicitHeight: theme.dp(28)
                iconName: root.playing ? "pause" : "play"
                text: root.playing ? qsTr("Pause") : qsTr("Play")
                accent: true; enabled: !root.started || root.pausable
                onClicked: root.togglePlayback()
            }
            ChromeButton {
                id: nextButton
                objectName: "nextButton"
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                implicitWidth: theme.dp(30); implicitHeight: theme.dp(28)
                iconName: "next"; text: qsTr("Next"); enabled: root.hasNext
                onClicked: root.next()
            }
            RowLayout {
                id: timelineControl
                Layout.leftMargin: parent === originalToolbar ? theme.dp(6) : 0
                Layout.rightMargin: parent === originalToolbar ? theme.dp(4) : 0
                objectName: "timelineControl"
                Layout.fillWidth: true
                spacing: theme.dp(10)
                Text {
                    id: elapsedTime
                    objectName: "elapsedTimeLabel"
                    visible: root.width > theme.dp(650)
                    Layout.preferredWidth: Math.max(theme.dp(chrome.hasTimeline("above") ? 74 : chrome.audioRight ? 84 : 82), implicitWidth)
                    leftPadding: chrome.hasTimeline("above") ? 0 : theme.dp(8)
                    horizontalAlignment: Text.AlignHCenter
                    text: root.timeText
                    textFormat: Text.PlainText
                    color: editing ? "transparent" : theme.foreground
                    font.pixelSize: theme.dp(root.timeLabelFontSize)
                    readonly property bool canEdit: root.started && root.seekable && !root.presentationOnly
                    readonly property bool editing: timeEditor.visible
                    readonly property int parsedSeconds: timeParser.parseTime(timeEditor.text)
                    readonly property bool valid: canEdit && parsedSeconds >= 0
                        && (!(root.durationSeconds > 0) || parsedSeconds <= root.durationSeconds)
                    readonly property string helpText: parsedSeconds >= 0 && root.durationSeconds > 0 && parsedSeconds > root.durationSeconds
                        ? qsTr("Time must be between %1 and %2.").arg(timeParser.formatTime(0)).arg(timeParser.formatTime(root.durationSeconds))
                        : qsTr("Use seconds, mm:ss, or hh:mm:ss.")
                    activeFocusOnTab: canEdit
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Go to time")
                    Accessible.description: root.timeText
                    Accessible.onPressAction: begin()
                    onCanEditChanged: if (!canEdit) cancel()
                    onVisibleChanged: if (!visible) cancel()
                    function begin() {
                        if (!canEdit || editing) return
                        timeEditor.text = root.timeText
                        timeEditor.visible = true
                        timeEditor.forceActiveFocus()
                        timeEditor.selectAll()
                    }
                    function cancel() {
                        timeEditor.visible = false
                        timeEditor.deselect()
                        timeEditor.focus = false
                        focus = false
                    }
                    function submit() {
                        if (!editing || !valid) return
                        const seconds = parsedSeconds
                        cancel()
                        root.seekToSecondsRequested(seconds)
                    }
                    Keys.onReturnPressed: begin()
                    Keys.onEnterPressed: begin()
                    Keys.onSpacePressed: begin()
                    Rectangle {
                        anchors.fill: parent; anchors.margins: -theme.dp(2)
                        visible: elapsedTime.activeFocus && !elapsedTime.editing
                        color: "transparent"; radius: theme.dp(4)
                        border.color: theme.accent; border.width: theme.dp(1)
                    }
                    MouseArea {
                        anchors.fill: parent; anchors.topMargin: -theme.dp(5); anchors.bottomMargin: -theme.dp(5)
                        enabled: elapsedTime.canEdit && !elapsedTime.editing
                        cursorShape: Qt.PointingHandCursor
                        onClicked: elapsedTime.begin()
                    }
                    C.TextField {
                        id: timeEditor
                        objectName: "elapsedTimeEditor"
                        visible: false
                        width: parent.width; height: Math.max(parent.height, theme.dp(28))
                        anchors.verticalCenter: parent.verticalCenter
                        leftPadding: parent.leftPadding; rightPadding: 0
                        topPadding: 0; bottomPadding: 0
                        horizontalAlignment: TextInput.AlignHCenter
                        font: elapsedTime.font
                        color: theme.playerHeading
                        selectionColor: theme.accent; selectedTextColor: theme.field
                        selectByMouse: true
                        inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                        Accessible.name: qsTr("Time")
                        Accessible.description: elapsedTime.helpText
                        background: Rectangle {
                            color: theme.field; radius: theme.dp(4)
                            border.width: theme.dp(1)
                            border.color: timeEditor.text.trim().length && !elapsedTime.valid ? theme.danger : theme.accent
                        }
                        Keys.onReturnPressed: elapsedTime.submit()
                        Keys.onEnterPressed: elapsedTime.submit()
                        Keys.onEscapePressed: elapsedTime.cancel()
                        onActiveFocusChanged: if (visible && !activeFocus) elapsedTime.cancel()
                        RedesignToolTip {
                            theme: root.theme
                            text: elapsedTime.helpText
                            contentTextFormat: Text.PlainText
                            visible: timeEditor.visible && timeEditor.activeFocus && timeEditor.text.trim().length && !elapsedTime.valid
                            delay: 400
                        }
                    }
                }
                RedesignPlayerSeekBar {
                    focusPolicy: root.presentationOnly ? Qt.NoFocus : Qt.StrongFocus
                    id: seek
                    objectName: "seekBar"
                    mediaStarted: root.started
                    playerPosition: root.position
                    onDragStarted: root.seekDragStarted()
                    onDragFinished: (position, commit) => root.seekDragFinished(position, commit)
                    onSeekRequested: position => root.seekRequested(position)
                    Layout.fillWidth: true
                    Layout.minimumWidth: theme.dp(40)
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    enabled: root.seekable
                    timeForPosition: root.timeForPosition
                    previewEnabled: root.showSeekPreview && root.started && root.hasVideo && !root.presentationOnly
                    thumbnailContent: root.seekThumbnailContent
                    videoBackdropContent: root.videoBackdropContent
                    previewImage: root.seekPreviewImage
                    previewImagePosition: root.seekPreviewImagePosition
                    waveformEnabled: root.audioWaveformEnabled
                    waveformPeaks: root.audioWaveformPeaks
                    timelineEnabled: root.showSubtitleTimeline
                    timelineCues: root.subtitleTimelineCues
                    timelineState: root.subtitleTimelineState
                    durationSeconds: root.durationSeconds
                    subtitleDelayMS: root.subtitleDelayMS
                    onSubtitleDelayRequested: milliseconds => root.subtitleDelayRequested(milliseconds)
                    onPrecisePositionUpdatesRequested: requested => root.precisePositionUpdatesRequested(requested)
                    onPreviewRequested: position => root.seekPreviewRequested(position)
                    onPreviewEnded: root.seekPreviewEnded()
                }
                Text {
                    objectName: "durationTimeLabel"
                    visible: root.width > theme.dp(800)
                    Layout.preferredWidth: Math.max(theme.dp(chrome.hasTimeline("above") ? 74 : 78), implicitWidth)
                    rightPadding: chrome.hasTimeline("above") ? 0 : theme.dp(6)
                    horizontalAlignment: Text.AlignHCenter
                    text: root.lengthText
                    color: theme.foreground; font.pixelSize: theme.dp(root.timeLabelFontSize)
                }
            }
            ChromeButton {
                id: muteButton
                Layout.rightMargin: parent === originalToolbar ? theme.dp(4) : 0
                objectName: "muteButton"
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                implicitWidth: theme.dp(28)
                iconName: root.muted || root.volume === 0 ? "muted"
                    : !Number.isFinite(root.volume) || root.volume < 0 ? "volume"
                    : root.volume < 0.33 ? "volume-low"
                    : root.volume <= 0.66 ? "volume-medium" : "volume"
                text: root.muted ? qsTr("Unmute") : qsTr("Mute")
                onClicked: root.muteRequested()
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.NoButton
                    onWheel: event => {
                        root.volumeWheelRequested(event)
                        event.accepted = true
                    }
                }
            }
            RedesignVolume {
                focusPolicy: root.presentationOnly ? Qt.NoFocus : Qt.StrongFocus
                id: volumeSlider
                Layout.rightMargin: parent === originalToolbar ? theme.dp(9) : 0
                objectName: "volumeSlider"
                visible: root.width > theme.dp(750)
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                muted: root.muted; to: root.maxVolume
                currentVolume: root.volume
                onWheelRequested: event => root.volumeWheelRequested(event)
                Binding on value { when: !volumeSlider.pressed; value: root.volume; restoreMode: Binding.RestoreBindingOrValue }
                onMoved: root.volumeRequested(value)
            }
            ChromeButton {
                id: audioButton
                objectName: "audioButton"
                visible: root.width > theme.dp(900)
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                iconName: "audio"; selectedIconName: "nav-audio-active"; text: qsTr("Audio tracks and output")
                selected: !!root.trackPopup && root.trackPopup.visible && root.trackPopup.page === "audio"
                onClicked: { root.toggleTracks(this, "audio"); root.audioTracksRequested(this) }
            }
            ChromeButton {
                id: subtitleButton
                objectName: "subtitleButton"
                // A centered playback group needs room on narrow custom bars.
                // Subtitle settings remain available in the settings panel.
                visible: !(episodesButton.visible && chrome.custom && root.width <= theme.dp(1000))
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                iconName: "subtitles"; text: qsTr("Subtitle tracks")
                selectedIconName: "subtitles-active"
                selected: !!root.trackPopup && root.trackPopup.visible && root.trackPopup.page === "subtitles"
                onClicked: { root.toggleTracks(this, "subtitles"); root.subtitleTracksRequested(this) }
            }
            ChromeButton {
                id: playbackSettingsButton
                rightPadding: selected ? 0 : theme.dp(2)
                Layout.rightMargin: parent === originalToolbar ? theme.dp(8) : 0
                objectName: "playbackSettingsButton"
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                iconName: "playback-settings"; text: qsTr("Playback settings")
                selectedIconName: "playback-settings-active"
                selected: root.settingsPage.length > 0
                onClicked: root.toggleSettings("playback")
            }
            ChromeButton {
                id: episodesButton
                objectName: "episodesButton"
                visible: root.episodesAvailable || root.presentationOnly || chromeLayout.editing
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                iconName: "episodes"; text: qsTr("Episodes"); selected: root.episodesOpen
                onClicked: root.toggleEpisodes()
            }
            ChromeButton {
                id: queueButton
                objectName: "queueButton"
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                iconName: "lists"; selectedIconName: "lists-active"; text: qsTr("Play queue"); selected: root.queueOpen
                onClicked: { root.episodesOpen = false; root.settingsPage = ""; root.queueOpen = !root.queueOpen }
            }
        }
    }
    RedesignPlayerLayout { id: chromeLayout; savedLayout: root.playerLayout }
    RedesignCustomizationCatalog { id: customizationCatalog }
    RedesignPlayerChrome {
        id: chrome
        theme: root.theme; model: chromeLayout
        // Keep room for the palette on short windows and large display scales.
        reserveEditingRow: root.height >= theme.dp(600)
        headerHost: header; footerHost: toolbar
        extraControlComponent: Component {
            RedesignExtraControl {
                theme: root.theme
                specification: customizationCatalog.definition(controlId)
                controlState: root.customControlStates[controlId] || ({})
                onTriggered: anchor => root.customActionRequested(controlId, anchor)
            }
        }
        controls: ({menu: mainMenuButton, library: libraryToggleButton, "divider:1": headerDivider, folder: mediaFolderButton,
            title: titleControl, minimize: minimizeButton, maximize: maximizeButton, fullscreen: fullscreenButton, close: closeButton,
            previous: previousButton, play: playButton, next: nextButton, timeline: timelineControl,
            mute: muteButton, volume: volumeSlider, audio: audioButton, subtitles: subtitleButton,
            settings: playbackSettingsButton, episodes: episodesButton, queue: queueButton})
    }
}

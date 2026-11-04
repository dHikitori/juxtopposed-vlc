// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    property bool playing: false
    property bool started: false
    property bool seekable: false
    property bool pausable: true
    property bool hasPrevious: false
    property bool hasNext: false
    property bool hasVideo: false
    property bool audioWaveformEnabled: false
    property var audioWaveformPeaks: []
    property bool previewEnabled: false
    property real position: 0
    property string mediaTitle: ""
    property string folderTitle: ""
    property string artist: ""
    property bool canBrowseMedia: true
    property url previewImage
    property Component thumbnailContent
    property Component videoBackdropContent
    property real previewImagePosition: -1
    property var timeForPosition: function(position) { return "" }
    property bool userMoved: false
    property real draggedX: 0
    property real draggedY: 0
    property real minimumY: theme.dp(46)
    readonly property color metadataColor: lightTheme ? theme.foreground : "#969696"
    readonly property alias videoArea: videoHost
    readonly property real seekY: seek.y
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    signal togglePlayback()
    signal previous()
    signal next()
    signal seekRequested(real position)
    signal seekWheelRequested(var event)
    signal seekDragStarted()
    signal seekDragFinished(real position, bool commit)
    signal previewRequested(real position)
    signal previewEnded()
    signal openPlayerRequested()
    signal closeRequested()
    signal queueRequested()
    signal folderRequested()
    signal artistRequested()

    implicitWidth: theme.dp(391)
    height: Math.ceil(videoHost.height + theme.dp(130))
    x: userMoved ? draggedX : parent ? parent.width - width - theme.dp(6) : 0
    y: userMoved ? draggedY : parent ? parent.height - height - theme.dp(6) : 0
    color: theme.background; radius: theme.dp(14)
    HoverHandler { id: hover; blocking: false }

    function refreshPreview() { seek.refreshPreview() }
    function constrainPosition() {
        if (!parent || !userMoved) return
        draggedX = Math.max(theme.dp(6), Math.min(draggedX, parent.width - width - theme.dp(6)))
        draggedY = Math.max(minimumY, Math.min(draggedY, parent.height - height - theme.dp(6)))
    }
    onWidthChanged: constrainPosition()
    onHeightChanged: constrainPosition()
    Connections {
        target: root.parent
        function onWidthChanged() { root.constrainPosition() }
        function onHeightChanged() { root.constrainPosition() }
    }
    component MoveHandler: DragHandler {
        target: null
        property real originX: 0
        property real originY: 0
        onActiveChanged: if (active) {
            originX = root.x; originY = root.y
            root.draggedX = originX; root.draggedY = originY
            root.userMoved = true
        }
        onActiveTranslationChanged: if (active) {
            root.draggedX = originX + activeTranslation.x
            root.draggedY = originY + activeTranslation.y
            root.constrainPosition()
        }
    }
    component MiniButton: RedesignButton {
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
    }
    component MetadataButton: MiniButton {
        id: metadataButton
        property string label
        Layout.fillWidth: true
        Layout.minimumWidth: Math.min(implicitWidth, metadataRow.labelSpace / (artistButton.visible ? 2 : 1))
        Layout.maximumWidth: implicitWidth
        implicitWidth: Math.round(root.theme.dp(48) + labelMetrics.advanceWidth)
        caption: label; captionFontSize: 16; captionSpacing: 8
        captionTextFormat: Text.PlainText
        captionMaximumWidth: Math.max(0, width - root.theme.dp(48))
        leftAligned: true; contentLeftMargin: 8
        foreground: root.metadataColor; iconForeground: root.theme.foreground
        TextMetrics {
            id: labelMetrics
            text: metadataButton.label
            font.family: metadataButton.font.family
            font.pixelSize: root.theme.dp(16)
        }
    }
    Item {
        id: videoHost
        objectName: "miniPlayerVideoHost"
        x: theme.dp(10); y: theme.dp(10)
        width: parent.width - theme.dp(20); height: width * 9 / 16
        MoveHandler {}
        Rectangle {
            objectName: "miniPlayerVideoBorder"
            // Paint the reference border outside the top and sides of the video.
            x: -root.theme.dp(2); y: -root.theme.dp(2)
            width: parent.width + root.theme.dp(4)
            height: Math.ceil(parent.height + root.theme.dp(2))
            z: 3; color: "transparent"; radius: root.theme.dp(6)
            border.color: root.theme.field; border.width: root.theme.dp(2)
        }
        Item {
            anchors.fill: parent; z: 4
            HoverHandler { id: videoHover; blocking: false }
            FocusScope {
                id: controlsScope
                anchors.fill: parent
                visible: hover.hovered || videoHover.hovered || activeFocus || seek.activeFocus || folder.activeFocus || artistButton.activeFocus || queueButton.activeFocus
                RowLayout {
                    anchors.top: parent.top; anchors.right: parent.right
                    anchors.margins: root.theme.dp(4); spacing: 0
                    MiniButton { objectName: "miniPlayerExpandButton"; iconName: "expand"; text: qsTr("Open player"); onClicked: root.openPlayerRequested() }
                    MiniButton { objectName: "miniPlayerCloseButton"; iconName: "close"; text: qsTr("Hide mini player"); onClicked: root.closeRequested() }
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: controls.implicitWidth; height: root.theme.dp(40)
                    radius: root.theme.dp(6); color: root.theme.panel; opacity: 0.9
                }
                RowLayout {
                    id: controls
                    anchors.centerIn: parent; spacing: root.theme.dp(4)
                    MiniButton { objectName: "miniPlayerPreviousButton"; iconName: "previous"; text: qsTr("Previous"); enabled: root.hasPrevious; onClicked: root.previous() }
                    MiniButton { objectName: "miniPlayerPlayButton"; iconName: root.playing ? "pause" : "play"; text: root.playing ? qsTr("Pause") : qsTr("Play"); enabled: !root.started || root.pausable; onClicked: root.togglePlayback() }
                    MiniButton { objectName: "miniPlayerNextButton"; iconName: "next"; text: qsTr("Next"); enabled: root.hasNext; onClicked: root.next() }
                }
            }
        }
    }
    RedesignPlayerSeekBar {
        id: seek
        objectName: "miniPlayerSeekBar"
        x: theme.dp(8); y: Math.ceil(videoHost.y + videoHost.height) + theme.dp(4)
        width: parent.width - theme.dp(16); height: theme.dp(16)
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        enabled: root.seekable
        mediaStarted: root.started; playerPosition: root.position
        waveformEnabled: root.audioWaveformEnabled
        waveformPeaks: root.audioWaveformPeaks
        previewEnabled: root.previewEnabled && root.hasVideo && root.started
        thumbnailContent: root.thumbnailContent
        videoBackdropContent: root.videoBackdropContent
        previewImage: root.previewImage; previewImagePosition: root.previewImagePosition
        timeForPosition: root.timeForPosition
        onSeekRequested: position => root.seekRequested(position)
        onWheelSeekRequested: event => root.seekWheelRequested(event)
        onDragStarted: root.seekDragStarted()
        onDragFinished: (position, commit) => root.seekDragFinished(position, commit)
        onPreviewRequested: position => root.previewRequested(position)
        onPreviewEnded: root.previewEnded()
    }
    Text {
        objectName: "miniPlayerTitle"
        x: theme.dp(16); y: parent.height - theme.dp(62) - baselineOffset
        width: parent.width - theme.dp(32)
        text: root.mediaTitle; textFormat: Text.PlainText; elide: Text.ElideRight
        color: root.metadataColor; font.pixelSize: theme.dp(16)
        MoveHandler {}
    }
    RowLayout {
        id: metadataRow
        readonly property real labelSpace: Math.max(0, width - queueButton.width - spacing * (artistButton.visible ? 3 : 2))
        anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: theme.dp(8); anchors.rightMargin: theme.dp(13)
        y: parent.height - theme.dp(46)
        height: theme.dp(36); spacing: theme.dp(4)
        MetadataButton {
            id: folder
            objectName: "miniPlayerFolderButton"
            iconName: "folder"; label: root.folderTitle
            text: qsTr("Open media folder"); enabled: root.canBrowseMedia && root.folderTitle.length > 0
            onClicked: root.folderRequested()
        }
        MetadataButton {
            id: artistButton
            objectName: "miniPlayerArtistButton"
            iconName: "artist"; label: root.artist
            text: qsTr("Find artist"); visible: root.artist.length > 0
            enabled: root.canBrowseMedia
            onClicked: root.artistRequested()
        }
        Item { Layout.fillWidth: true }
        MiniButton { id: queueButton; objectName: "miniPlayerQueueButton"; iconName: "lists"; text: qsTr("Play queue"); onClicked: root.queueRequested() }
    }
}

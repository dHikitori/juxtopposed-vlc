// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

FocusScope {
    id: root
    property real scaleFactor: 1
    property bool lightTheme: false
    property color accentColor: "#d37918"
    property var episodesModel
    property var seasons: []
    property int selectedSeason: -1
    property int currentSeason: -1
    property string showTitle: ""
    property bool loading: false
    property bool limited: false
    property bool podcast: false
    property string message: ""
    property bool showingSeasons: false
    property string expandedKey: ""
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    signal seasonRequested(int season)
    signal episodeRequested(int row, int version, bool fromStart)
    signal detailsRequested(int row)
    signal closeRequested()
    signal refreshRequested()
    signal folderRequested()
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Episodes")

    function seasonLabel(season) { return season === 0 ? qsTr("Specials") : season < 0 ? qsTr("Episodes") : qsTr("Season %1").arg(season) }
    function fileLabel(url) {
        const name = String(url).split("/").pop()
        try { return decodeURIComponent(name) } catch (error) { return name }
    }
    function followCurrentEpisode() {
        if (!episodesModel || episodes.count === 0 || expandedKey || showingSeasons) return
        let index = 0
        for (let i = 0; i < episodes.count; ++i) {
            const row = episodesModel.get ? episodesModel.get(i) : episodesModel[i]
            if (row && row.current) { index = i; break }
        }
        episodes.forceLayout()
        episodes.currentIndex = index
        episodes.positionViewAtIndex(index, ListView.Beginning)
    }
    function followCurrentSeason() {
        if (!showingSeasons || seasons.length === 0) return
        let index = seasons.indexOf(currentSeason)
        if (index < 0) index = Math.max(0, seasons.indexOf(selectedSeason))
        seasonList.forceLayout()
        seasonList.currentIndex = index
        seasonList.positionViewAtIndex(index, ListView.Beginning)
    }
    function positionCurrentEntry() {
        if (!visible) return
        if (showingSeasons) followCurrentSeason()
        else followCurrentEpisode()
    }
    function enterList() {
        if (!visible) return
        if (showingSeasons) seasonList.forceActiveFocus()
        else episodes.forceActiveFocus()
        // Wait for the layout and newly created, expanded episode delegates.
        Qt.callLater(root.positionCurrentEntry)
    }
    Component.onCompleted: {
        if (selectedSeason !== currentSeason && seasons.indexOf(currentSeason) >= 0)
            seasonRequested(currentSeason)
        Qt.callLater(root.enterList)
    }
    onVisibleChanged: if (visible) Qt.callLater(root.enterList)
    onShowingSeasonsChanged: Qt.callLater(root.enterList)
    onSeasonsChanged: if (showingSeasons) Qt.callLater(root.positionCurrentEntry)
    Connections {
        target: root.episodesModel && typeof root.episodesModel.modelReset === "function" ? root.episodesModel : null
        function onModelReset() { Qt.callLater(root.positionCurrentEntry) }
    }
    Keys.onEscapePressed: event => {
        if (showingSeasons) showingSeasons = false
        else { closeRequested(); event.accepted = true }
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true; Layout.margins: theme.dp(10)
            spacing: theme.dp(4)
            RedesignButton {
                objectName: "episodeSeasonsButton"
                Layout.fillWidth: true; Layout.preferredHeight: theme.dp(44)
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                text: [root.showTitle, root.seasonLabel(root.selectedSeason), qsTr("Seasons")].filter(value => !!value).join(", ")
                enabled: root.seasons.length > 1
                onClicked: root.showingSeasons = !root.showingSeasons
                contentItem: RowLayout {
                    spacing: theme.dp(8)
                    RedesignIcon {
                        Layout.leftMargin: theme.dp(4)
                        Layout.preferredWidth: theme.dp(24); Layout.preferredHeight: theme.dp(24)
                        visible: root.seasons.length > 1
                        name: root.showingSeasons ? "chevron-down" : LayoutMirroring.enabled ? "chevron-right" : "chevron-left"
                        color: theme.foreground
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: theme.dp(3)
                        Text {
                            objectName: "episodeShowTitle"
                            Layout.fillWidth: true
                            text: root.showTitle || qsTr("Episodes")
                            textFormat: Text.PlainText; elide: Text.ElideRight
                            color: theme.foreground; font.pixelSize: theme.dp(16); font.weight: Font.Medium
                        }
                        Text {
                            objectName: "episodeSeasonLabel"
                            Layout.fillWidth: true; visible: !root.showingSeasons
                            text: root.seasonLabel(root.selectedSeason)
                            color: theme.playerLabel; font.pixelSize: theme.dp(12)
                        }
                    }
                }
            }
            RedesignButton {
                objectName: "episodeOptionsButton"
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                iconName: "more"; text: qsTr("Episode options")
                onClicked: options.toggleFrom(this)
                RedesignActionMenu {
                    id: options
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    entries: [{action: "refresh", label: qsTr("Refresh"), icon: "repeat"}].concat(root.podcast ? []
                        : [{action: "folder", label: qsTr("Choose series folder…"), icon: "folder"}])
                    onActionRequested: action => action === "folder" ? root.folderRequested() : root.refreshRequested()
                }
            }
            RedesignButton {
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                iconName: "close"; text: qsTr("Close episodes")
                onClicked: root.closeRequested()
            }
        }
        Rectangle { Layout.fillWidth: true; height: theme.dp(1); color: theme.border }
        Text {
            Layout.fillWidth: true; Layout.margins: theme.dp(12)
            visible: root.loading || root.message.length > 0 || root.limited
            text: root.message || (root.loading ? qsTr("Finding episodes…") : qsTr("Some folders could not be fully searched. Refresh to try again, or choose the series folder."))
            color: theme.playerLabel; font.pixelSize: theme.dp(12); wrapMode: Text.WordWrap
            textFormat: Text.PlainText
        }
        ListView {
            id: seasonList
            objectName: "episodeSeasonList"
            Layout.fillWidth: true; Layout.fillHeight: true
            visible: root.showingSeasons; clip: true
            model: root.seasons
            keyNavigationEnabled: true
            C.ScrollBar.vertical: C.ScrollBar {}
            delegate: RedesignButton {
                required property int modelData
                required property int index
                objectName: "episodeSeason_" + modelData
                width: seasonList.width; height: theme.dp(48)
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                cornerRadius: 0
                text: root.seasonLabel(modelData)
                selected: modelData === root.currentSeason
                onClicked: {
                    root.expandedKey = ""
                    root.seasonRequested(modelData)
                    root.showingSeasons = false
                }
                contentItem: RowLayout {
                    spacing: theme.dp(10)
                    RedesignIcon { Layout.leftMargin: theme.dp(12); Layout.preferredWidth: theme.dp(20); Layout.preferredHeight: theme.dp(20); name: modelData === root.currentSeason ? "check" : ""; color: theme.accent }
                    Text { Layout.fillWidth: true; text: root.seasonLabel(modelData); color: theme.foreground; font.pixelSize: theme.dp(14) }
                    RedesignIcon { Layout.rightMargin: theme.dp(12); Layout.preferredWidth: theme.dp(20); Layout.preferredHeight: theme.dp(20); name: "chevron-right"; color: theme.playerLabel }
                }
            }
        }
        ListView {
            id: episodes
            objectName: "episodeList"
            Layout.fillWidth: true; Layout.fillHeight: true
            visible: !root.showingSeasons; clip: true
            model: root.episodesModel
            onCountChanged: Qt.callLater(root.positionCurrentEntry)
            keyNavigationEnabled: true
            Keys.onReturnPressed: if (currentItem) currentItem.toggle()
            Keys.onEnterPressed: if (currentItem) currentItem.toggle()
            Keys.onSpacePressed: if (currentItem) currentItem.toggle()
            C.ScrollBar.vertical: C.ScrollBar {}
            delegate: ColumnLayout {
                id: row
                required property int index
                required property var model
                readonly property var entry: model.modelData || model
                readonly property bool expanded: root.expandedKey === String(entry.urls[0]) || (!root.expandedKey && entry.current)
                property int version: Number(entry.currentVersion || 0)
                objectName: "episodeRow_" + index
                function toggle() {
                    root.expandedKey = expanded ? "collapsed" : String(entry.urls[0])
                    episodes.currentIndex = index
                }
                width: episodes.width
                spacing: 0
                onExpandedChanged: if (expanded) root.detailsRequested(index)
                Component.onCompleted: if (expanded) root.detailsRequested(index)
                RedesignButton {
                    id: episodeButton
                    objectName: "episodeTitle_" + row.index
                    Layout.fillWidth: true; Layout.preferredHeight: theme.dp(60)
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    cornerRadius: 0
                    text: row.entry.title + (row.entry.current ? ", " + qsTr("Now playing") : "")
                    selected: row.entry.current
                    onClicked: row.toggle()
                    contentItem: RowLayout {
                        spacing: theme.dp(10)
                        Text { Layout.leftMargin: theme.dp(12); visible: !!row.entry.number; text: row.entry.number; color: theme.accent; font.pixelSize: theme.dp(14) }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: theme.dp(4)
                            Text { Layout.fillWidth: true; text: row.entry.title; textFormat: Text.PlainText; color: theme.foreground; font.pixelSize: theme.dp(14); elide: Text.ElideRight }
                            Text {
                                visible: row.entry.current || row.entry.played
                                text: row.entry.current ? qsTr("Now playing") : root.podcast ? qsTr("Listened") : qsTr("Watched")
                                color: row.entry.current ? theme.accent : theme.playerLabel; font.pixelSize: theme.dp(11)
                            }
                        }
                        RedesignIcon { Layout.rightMargin: theme.dp(12); Layout.preferredWidth: theme.dp(20); Layout.preferredHeight: theme.dp(20); name: row.expanded ? "chevron-up" : "chevron-down"; color: theme.playerLabel }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true; Layout.leftMargin: theme.dp(12); Layout.rightMargin: theme.dp(12)
                    visible: row.entry.progress > 0
                    height: theme.dp(2); color: "transparent"
                    Rectangle { width: parent.width * Math.max(0, Math.min(1, row.entry.progress)); height: parent.height; color: theme.accent }
                }
                ColumnLayout {
                    Layout.fillWidth: true; Layout.margins: theme.dp(12)
                    visible: row.expanded; spacing: theme.dp(12)
                    RedesignLibraryArtwork {
                        Layout.fillWidth: true; Layout.preferredHeight: Math.min(width * 9 / 16, theme.dp(155))
                        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                        source: row.expanded ? row.entry.thumbnail : ""
                        iconName: root.podcast && !row.entry.videoEpisode ? "music" : "video"
                        surroundColor: theme.floatingPanel; cornerRadius: 6
                    }
                    Text {
                        objectName: "episodeDescription_" + row.index
                        Layout.fillWidth: true; visible: !!row.entry.description
                        text: row.entry.description; textFormat: Text.PlainText; wrapMode: Text.Wrap
                        maximumLineCount: 3; elide: Text.ElideRight
                        color: theme.foreground; font.pixelSize: theme.dp(13)
                    }
                    RedesignButton {
                        objectName: "episodeTitleSource_" + row.index
                        visible: !!row.entry.titleSource && String(row.entry.titleSource).length > 0
                        Layout.fillWidth: true
                        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                        caption: qsTr("Episode source"); text: caption
                        captionFontSize: 11; leftAligned: true
                        foreground: theme.playerLabel
                        contentLeftMargin: 0
                        onClicked: Qt.openUrlExternally(row.entry.titleSource)
                    }
                    RedesignComboBox {
                        objectName: "episodeVersion_" + row.index
                        Layout.fillWidth: true; visible: !root.podcast && row.entry.urls.length > 1
                        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                        model: row.entry.urls.map(url => root.fileLabel(url))
                        currentIndex: row.version
                        onActivated: index => row.version = index
                        Accessible.name: qsTr("Episode version")
                    }
                    Text {
                        Layout.fillWidth: true; visible: !root.podcast
                        text: root.fileLabel(row.entry.urls[Math.min(row.version, row.entry.urls.length - 1)])
                        textFormat: Text.PlainText; elide: Text.ElideMiddle; color: theme.playerLabel; font.pixelSize: theme.dp(11)
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        RedesignButton {
                            objectName: "episodePlay_" + row.index
                            Layout.fillWidth: true
                            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                            iconName: "play-outline"
                            caption: row.entry.current && row.version === Number(row.entry.currentVersion || 0) ? qsTr("Playing") : row.entry.progress > 0 ? qsTr("Resume") : qsTr("Play")
                            text: caption; enabled: !row.entry.current || row.version !== Number(row.entry.currentVersion || 0)
                            onClicked: root.episodeRequested(row.index, row.version, false)
                        }
                        RedesignButton {
                            objectName: "episodeRestart_" + row.index
                            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                            iconName: "replay"; text: qsTr("Play from beginning")
                            onClicked: root.episodeRequested(row.index, row.version, true)
                        }
                    }
                    RedesignButton {
                        visible: root.podcast && row.entry.urls.length > 1
                        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                        iconName: "video"; caption: qsTr("Play video"); text: caption
                        onClicked: root.episodeRequested(row.index, 1, false)
                    }
                }
            }
            Text {
                anchors.fill: parent; anchors.margins: theme.dp(16)
                visible: episodes.count === 0 && !root.loading && !root.message
                text: qsTr("No episodes found in this season.")
                color: theme.playerLabel; font.pixelSize: theme.dp(14); wrapMode: Text.WordWrap
            }
        }
    }
}

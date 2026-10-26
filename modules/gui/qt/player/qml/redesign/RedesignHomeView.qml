// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Controls.Basic as C

Flickable {
    id: root
    property var artworkForItem: model => model.thumbnail || model.small_cover || model.cover || ""
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    property var sections: []
    property bool expanded: false
    property bool singleClickActivate: false
    property string selectedKey: ""
    property string searchText: ""
    signal activated(string section, int index)
    signal enqueueRequested(string section, int index)
    signal sectionRequested(string section)
    signal contextRequested(string section, int index, Item anchor, point point)
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    contentWidth: width
    contentHeight: groups.height
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    C.ScrollBar.vertical: C.ScrollBar { policy: C.ScrollBar.AsNeeded }
    Column {
        id: groups
        width: root.width
        spacing: root.theme.dp(root.expanded ? 80 : 25)
        Repeater {
            model: root.sections
            Column {
                id: section
                required property var modelData
                width: groups.width
                spacing: root.theme.dp(root.expanded ? 16 : 10)
                RedesignButton {
                    objectName: "homeSection_" + section.modelData.key
                    width: root.expanded ? Math.min(parent.width, sectionTitle.implicitWidth + root.theme.dp(32)) : parent.width
                    height: root.theme.dp(root.expanded ? 24 : 32)
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    text: section.modelData.title
                    onClicked: root.sectionRequested(section.modelData.key)
                    contentItem: Item {
                        // The expanded reference heading has a 15 px baseline.
                        baselineOffset: root.theme.dp(15)
                        Text {
                            id: sectionTitle
                            objectName: "homeSectionTitle_" + section.modelData.key
                            anchors.left: parent.left; anchors.right: arrow.left
                            anchors.verticalCenter: parent.verticalCenter
                            states: State {
                                name: "expanded"; when: root.expanded
                                AnchorChanges {
                                    target: sectionTitle
                                    anchors.verticalCenter: undefined
                                    anchors.baseline: sectionTitle.parent.baseline
                                }
                            }
                            text: section.modelData.title; elide: Text.ElideRight
                            color: root.expanded ? root.theme.foreground : root.theme.playerHeading; font.pixelSize: root.theme.dp(root.expanded ? 20 : 16)
                        }
                        RedesignIcon { id: arrow; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; width: root.theme.dp(24); height: width; name: (LayoutMirroring.enabled ? "chevron-left" : "chevron-right"); color: root.theme.foreground }
                    }
                }
                Flickable {
                    id: row
                    width: parent.width
                    height: sectionItems.count > 0 ? entries.implicitHeight : root.theme.dp(42)
                    contentWidth: root.expanded ? entries.implicitWidth : width
                    contentHeight: height
                    clip: true
                    interactive: root.expanded
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    C.ScrollBar.horizontal: C.ScrollBar { policy: root.expanded ? C.ScrollBar.AsNeeded : C.ScrollBar.AlwaysOff }
                    Grid {
                        id: entries
                        columns: root.expanded ? 6 : 1
                        columnSpacing: root.expanded ? root.theme.dp(20) : 0
                        width: root.expanded ? implicitWidth : row.width
                        Repeater {
                            id: sectionItems
                            model: section.modelData.model
                            delegate: Item {
                                id: entry
                                required property int index
                                required property var model
                                width: root.expanded ? root.theme.dp(section.modelData.key === "watching" ? 316 : 200) : section.width
                                height: !visible ? 0 : !root.expanded ? root.theme.dp(42) : coverHeight + root.theme.dp(section.modelData.key === "listening" ? 69 : 40)
                                visible: index < (root.expanded ? 6 : 3)
                                readonly property real coverHeight: width * (section.modelData.key === "watching" ? 9 / 16 : 1)
                                readonly property string title: model.title || model.name || ""
                                readonly property string displayTitle: title + (!root.expanded && section.modelData.key === "listening" && model.main_artist ? " - " + model.main_artist : "")
                                readonly property string artwork: root.artworkForItem(model)
                                RedesignButton {
                                    id: button
                                    objectName: "homeItem_" + section.modelData.key + "_" + entry.index
                                    anchors.fill: parent
                                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                                    text: entry.displayTitle
                                    handleMediaKeys: false
                                    mediaItem: true; singleClickActivate: root.singleClickActivate
                                    selected: root.selectedKey === section.modelData.key + "/" + entry.index
                                    onClicked: root.selectedKey = section.modelData.key + "/" + entry.index
                                    onActivated: root.activated(section.modelData.key, entry.index)
                                    Keys.onMenuPressed: root.contextRequested(section.modelData.key, entry.index, this, Qt.point(0, height))
                                    Keys.onPressed: event => {
                                        if (event.key === Qt.Key_F10 && event.modifiers & Qt.ShiftModifier) {
                                            root.contextRequested(section.modelData.key, entry.index, this, Qt.point(0, height)); event.accepted = true
                                        } else if ([Qt.Key_Return, Qt.Key_Enter, Qt.Key_Space].includes(event.key)) {
                                            if (!event.isAutoRepeat) root.activated(section.modelData.key, entry.index)
                                            event.accepted = true
                                        } else event.accepted = false
                                    }
                                    TapHandler { acceptedButtons: Qt.RightButton; onTapped: event => root.contextRequested(section.modelData.key, entry.index, button, event.position) }
                                    property int mediaIndex: entry.index
                                    property QtObject libraryView: QtObject {
                                        function enqueueRequested(index) { root.enqueueRequested(section.modelData.key, index) }
                                    }
                                    DragHandler { id: dragHandler; target: null }
                                    RedesignLibraryDrag {
                                        handler: dragHandler
                                        libraryView: button.libraryView
                                        mediaIndex: entry.index
                                    }
                                    contentItem: Item {
                                        RedesignLibraryArtwork {
                                            id: cover
                                            objectName: "homeArtwork_" + section.modelData.key + "_" + entry.index
                                            width: root.expanded ? parent.width : root.theme.dp(32)
                                            height: root.expanded ? entry.coverHeight : width
                                            y: root.expanded ? 0 : (parent.height - height) / 2
                                            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                                            source: entry.artwork
                                            iconName: section.modelData.icon
                                            iconSize: root.expanded ? 48 : 24
                                            showPlaceholderBackground: root.expanded
                                            placeholderColor: root.lightTheme ? root.theme.field : section.modelData.key === "folders" ? "#1e1e1e" : "#171717"
                                            cornerRadius: root.expanded ? 12 : 2
                                            progress: ["watching", "listening"].includes(section.modelData.key) ? Number(entry.model.progress || 0) : 0
                                            surroundColor: button.down || button.selected ? root.theme.selected : button.hovered ? root.theme.hover : root.theme.panel
                                        }
                                        Text {
                                            objectName: "homeTitle_" + section.modelData.key + "_" + entry.index
                                            x: root.expanded ? 0 : cover.width + root.theme.dp(10)
                                            y: root.expanded ? cover.height + root.theme.dp(35) - baselineOffset : (parent.height - height) / 2
                                            width: Math.max(0, parent.width - x - (root.expanded ? 0 : root.theme.dp(28)))
                                            text: entry.displayTitle; elide: Text.ElideRight
                                            color: button.hovered || button.visualFocus ? root.theme.bright : root.expanded ? root.theme.playerHeading : root.theme.playerLabel
                                            font.pixelSize: root.theme.dp(root.expanded ? 16 : 14)
                                        }
                                        Text {
                                            objectName: "homeArtist_" + section.modelData.key + "_" + entry.index
                                            visible: root.expanded && section.modelData.key === "listening" && !!entry.model.main_artist
                                            x: 0; y: cover.height + root.theme.dp(64) - baselineOffset; width: parent.width
                                            text: entry.model.main_artist || ""; elide: Text.ElideRight
                                            color: root.theme.playerLabel; font.pixelSize: root.theme.dp(16)
                                        }
                                    }
                                }
                                RedesignButton {
                                    visible: button.hovered || hovered || button.visualFocus
                                    anchors.right: parent.right
                                    anchors.rightMargin: root.expanded ? root.theme.dp(8) : 0
                                    y: root.expanded ? root.theme.dp(8) : (parent.height - height) / 2
                                    implicitWidth: root.theme.dp(28)
                                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                                    iconName: "add"; text: qsTr("Add to queue")
                                    onClicked: root.enqueueRequested(section.modelData.key, entry.index)
                                }
                            }
                        }
                    }
                    Text {
                        visible: sectionItems.count === 0
                        width: parent.width; height: root.theme.dp(42)
                        verticalAlignment: Text.AlignVCenter
                        text: root.searchText.length > 0 ? qsTr("No media matches your search.") : section.modelData.emptyText
                        wrapMode: Text.WordWrap; color: root.theme.foreground; font.pixelSize: root.theme.dp(14)
                    }
                }
            }
        }
        RedesignAddonSections {
            width: parent.width
            hook: "home.sections@1"
            hostContext: ({surface: "home.sections@1", search: root.searchText, layout: root.expanded ? "expanded" : "compact"})
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        }
    }
}

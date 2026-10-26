// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Controls.Basic as C

Rectangle {
    id: root
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    property Component videoBackdropContent
    property bool expanded: false
    property int category: 1
    property bool atCategoryRoot: true
    property var pins: []
    property string selectedPinId: ""
    property var addonRegistry: typeof redesignAddonUi === "undefined" ? null : redesignAddonUi
    readonly property var addonRoutes: { if (!addonRegistry) return []; addonRegistry.revision; return addonRegistry.routes().slice(0, 16) }
    signal categoryRequested(int category)
    signal addRequested()
    signal pinActivated(var pin)
    signal pinRemoved(var pin)
    signal pinEnqueueRequested(var pin)
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    width: theme.dp(expanded ? 201 : 50)
    radius: expanded ? theme.dp(6) : 0
    color: expanded ? theme.libraryNavigation : theme.libraryNavigationOverlay
    function reveal(button) {
        const y = button.mapToItem(contents, 0, 0).y + contents.y
        if (y < scroll.contentY) scroll.contentY = y
        else if (y + button.height > scroll.contentY + scroll.height) scroll.contentY = y + button.height - scroll.height
        scroll.contentY = Math.max(0, Math.min(scroll.contentY, scroll.contentHeight - scroll.height))
    }
    function openPinMenu(button, pin, point) {
        pinMenu.pin = pin
        pinMenu.popup(button, point)
    }
    RedesignActionMenu {
        id: pinMenu
        videoBackdropContent: root.videoBackdropContent
        objectName: "libraryPinMenu"
        property var pin
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        entries: pin ? [
            {action: "open", label: ["file", "media"].includes(pin.kind) ? qsTr("Play") : qsTr("Open"), icon: "play"},
            {action: "queue", label: qsTr("Add to queue"), icon: "queue", enabled: !["section", "home", "network-source"].includes(pin.kind)},
            {action: "remove", label: qsTr("Remove from sidebar"), icon: "remove"}
        ] : []
        onActionRequested: action => {
            if (action === "remove") root.pinRemoved(pin)
            else if (action === "queue") root.pinEnqueueRequested(pin)
            else root.pinActivated(pin)
        }
    }
    component NavigationButton: RedesignButton {
        id: button
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        width: contents.width
        leftAligned: root.expanded
        captionSpacing: 10
        captionMaximumWidth: root.theme.dp(130)
        selectedForeground: root.theme.bright
        Keys.onReturnPressed: clicked()
        Keys.onEnterPressed: clicked()
        onActiveFocusChanged: if (activeFocus) root.reveal(this)
        background: Rectangle {
            radius: root.theme.dp(6)
            color: button.down ? root.theme.selected : button.hovered ? root.theme.hover : "transparent"
            border.width: button.visualFocus ? root.theme.dp(2) : 0
            border.color: root.theme.accent
        }
    }
    Flickable {
        id: scroll
        objectName: "libraryNavigationScroll"
        anchors.fill: parent
        contentWidth: width
        contentHeight: contents.height + root.theme.dp(root.expanded ? 20 : 12)
        onContentHeightChanged: returnToBounds()
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        C.ScrollBar.vertical: C.ScrollBar { policy: C.ScrollBar.AsNeeded }
        Column {
            id: contents
            y: root.theme.dp(root.expanded ? 10 : 6)
            anchors.horizontalCenter: parent.horizontalCenter
            width: root.expanded ? root.width - root.theme.dp(16) : root.theme.dp(36)
            spacing: root.theme.dp(10)
            Repeater {
                model: [qsTr("Home"), qsTr("Videos"), qsTr("Music"), qsTr("Network streams"), qsTr("Playlists"), qsTr("Recent files")]
                NavigationButton {
                    required property int index
                    required property string modelData
                    readonly property bool ancestorCategory: root.category === index
                        && (!root.atCategoryRoot || root.selectedPinId.length > 0)
                    readonly property string normalIcon: ["home", "videos", "library-songs", "network", "playlist", "history"][index]
                    readonly property string activeIcon: ["home-active", "videos-active", "library-songs-active", "nav-streaming-active", "playlist-active", "history"][index]
                    objectName: "libraryCategory" + index
                    iconName: ancestorCategory ? activeIcon : normalIcon
                    selectedIconName: activeIcon
                    text: modelData; selected: !ancestorCategory && root.category === index
                    caption: root.expanded ? [qsTr("Home"), qsTr("Videos"), qsTr("Music"), qsTr("Network"), qsTr("Playlists"), qsTr("Recent")][index] : ""
                    onClicked: root.categoryRequested(index)
                }
            }
            Repeater {
                model: root.addonRoutes
                NavigationButton {
                    required property var modelData
                    objectName: "addonNavigation_" + modelData.id
                    text: modelData.name || modelData.id; tooltipTextFormat: Text.PlainText
                    caption: root.expanded ? text : ""; captionTextFormat: Text.PlainText
                    iconName: "info"
                    onClicked: root.addonRegistry.openRoute(modelData.route, {})
                }
            }
            Repeater {
                model: root.pins
                NavigationButton {
                    id: shortcut
                    required property var modelData
                    required property int index
                    objectName: "libraryPin" + index
                    text: modelData.name
                    selected: modelData.id === root.selectedPinId
                    onClicked: root.pinActivated(modelData)
                    Keys.onMenuPressed: root.openPinMenu(this, modelData, Qt.point(0, height))
                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_F10 && event.modifiers & Qt.ShiftModifier) {
                            root.openPinMenu(this, modelData, Qt.point(0, height)); event.accepted = true
                        } else event.accepted = false
                    }
                    TapHandler { acceptedButtons: Qt.RightButton; onTapped: event => root.openPinMenu(shortcut, shortcut.modelData, event.position) }
                    contentItem: Item {
                        Item {
                            id: picture
                            objectName: "libraryPinPicture" + shortcut.index
                            anchors.verticalCenter: parent.verticalCenter
                            x: root.expanded ? root.theme.dp(12) : (parent.width - width) / 2
                            width: root.theme.dp(24); height: width
                            RedesignLibraryArtwork {
                                anchors.centerIn: parent
                                width: root.theme.dp(28); height: width
                                visible: !!shortcut.modelData.artwork
                                source: shortcut.modelData.artwork || ""
                                iconName: shortcut.modelData.icon
                                surroundColor: shortcut.down ? root.theme.selected : shortcut.hovered ? root.theme.hover : root.color
                                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                                cornerRadius: 4
                            }
                            RedesignIcon {
                                anchors.fill: parent
                                visible: !shortcut.modelData.artwork
                                name: shortcut.modelData.icon
                                color: shortcut.selected ? root.theme.accent : root.theme.foreground
                            }
                        }
                        Text {
                            objectName: "libraryPinLabel" + shortcut.index
                            visible: root.expanded
                            anchors.left: picture.right; anchors.leftMargin: root.theme.dp(10)
                            anchors.right: parent.right; anchors.rightMargin: root.theme.dp(8)
                            anchors.verticalCenter: parent.verticalCenter
                            text: shortcut.text; textFormat: Text.PlainText; elide: Text.ElideRight
                            font.pixelSize: root.theme.dp(14)
                            color: shortcut.selected ? root.theme.bright : root.theme.foreground
                        }
                    }
                }
            }
            NavigationButton {
                objectName: "libraryCategory6"
                iconName: "add"; text: qsTr("Add media")
                caption: root.expanded ? qsTr("Add item") : ""
                onClicked: root.addRequested()
            }
        }
    }
}

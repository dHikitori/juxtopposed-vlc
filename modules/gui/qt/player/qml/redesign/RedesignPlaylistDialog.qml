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
    property bool busy: false
    property string errorMessage: ""
    property alias playlistName: nameField.text
    property url cover: ""
    readonly property int fileCount: files.count
    readonly property var urls: { filesRevision; return Array.from({length: files.count}, (_, i) => files.get(i).url) }
    property Item scrollFocusItem
    property int filesRevision: 0
    readonly property bool valid: playlistName.trim().length > 0
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    signal filesRequested()
    signal coverRequested()
    signal saveRequested(string name, var urls, url cover)
    title: qsTr("New Playlist")
    readonly property real pixelRatio: parent ? parent.Screen.devicePixelRatio : 1
    x: parent ? Math.round((parent.width - width) * pixelRatio / 2) / pixelRatio : 0
    y: parent ? Math.round((parent.height - height) * pixelRatio / 2) / pixelRatio : 0
    width: Math.min(theme.dp(713), parent ? parent.width - theme.dp(32) : theme.dp(713))
    height: Math.min(theme.dp(589), parent ? parent.height - theme.dp(32) : theme.dp(589))
    padding: 0; spacing: 0
    modal: true; dim: true; focus: true
    closePolicy: busy ? C.Popup.NoAutoClose : C.Popup.CloseOnEscape
    background: RedesignDialogSurface { theme: root.theme; videoBackdropContent: root.videoBackdropContent }
    C.Overlay.modal: Rectangle { color: "#80000000" }
    ListModel { id: files }
    function displayName(url) {
        const part = String(url).replace(/\\/g, "/").split("/").pop()
        try { return decodeURIComponent(part) } catch (error) { return part }
    }
    function addFiles(values) {
        if (busy) return
        for (const value of values) {
            const url = String(value)
            if (!/^[a-z][a-z0-9+.-]*:/i.test(url)) continue
            files.append({url: url, name: displayName(url)})
        }
        ++filesRevision
        errorMessage = ""
    }
    function removeFile(index) {
        if (busy || index < 0 || index >= files.count) return
        files.remove(index); ++filesRevision
    }
    function begin() {
        if (!visible) { playlistName = ""; cover = ""; errorMessage = ""; files.clear(); ++filesRevision; open() }
        nameField.forceActiveFocus()
    }
    function submit() {
        if (!valid || busy) return
        errorMessage = ""; busy = true
        saveRequested(playlistName.trim(), urls, cover)
    }
    function finish(success, message) {
        busy = false
        if (success) accept()
        else { errorMessage = message || qsTr("Hakone cannot save this playlist. Try again."); nameField.forceActiveFocus() }
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
    onOpened: nameField.forceActiveFocus()
    onPlaylistNameChanged: errorMessage = ""
    onCoverChanged: errorMessage = ""
    Shortcut { sequence: "Ctrl+Return"; enabled: root.visible && root.valid && !root.busy; onActivated: root.submit() }
    Shortcut { sequence: "Ctrl+Enter"; enabled: root.visible && root.valid && !root.busy; onActivated: root.submit() }
    header: Item {
        implicitHeight: root.theme.dp(50)
        Rectangle {
            anchors.fill: parent; radius: root.theme.dp(14); color: root.theme.field
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: parent.height / 2; color: parent.color }
        }
        Text {
            anchors.left: parent.left; anchors.leftMargin: root.theme.dp(19); anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: root.theme.dp(1)
            text: root.title; color: root.theme.foreground; font.pixelSize: root.theme.dp(14)
        }
        RedesignButton {
            objectName: "playlistCloseButton"
            anchors.right: parent.right; anchors.rightMargin: root.theme.dp(7); anchors.verticalCenter: parent.verticalCenter
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            iconName: "close"; text: qsTr("Close"); enabled: !root.busy
            onClicked: root.reject()
        }
    }
    contentItem: C.ScrollView {
        id: scroll
        clip: true; contentWidth: availableWidth
        C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
        onAvailableHeightChanged: if (root.opened) root.revealFocusedItem()
        onContentHeightChanged: if (root.opened) root.revealFocusedItem()
        Column {
            width: scroll.availableWidth
            Item {
                width: parent.width
                height: Math.max(root.theme.dp(161), nameField.y + nameField.height + root.theme.dp(22), coverBox.y + coverBox.height + root.theme.dp(20))
                Item {
                    x: root.theme.dp(20); width: parent.width - root.theme.dp(172)
                    Text {
                        id: detailsHeading
                        width: parent.width; y: root.theme.dp(42) - baselineOffset
                        text: qsTr("Playlist Details"); color: root.theme.settingsHeading
                        font.pixelSize: root.theme.dp(16); wrapMode: Text.WordWrap
                    }
                    Text {
                        id: detailsHelp
                        width: parent.width
                        y: Math.max(root.theme.dp(73) - baselineOffset, detailsHeading.y + detailsHeading.height + root.theme.dp(10))
                        text: qsTr("Save the playlist with this name and thumbnail."); color: root.theme.playerLabel
                        font.pixelSize: root.theme.dp(14); wrapMode: Text.WordWrap
                    }
                    C.TextField {
                        id: nameField
                        objectName: "playlistNameField"
                        width: parent.width; height: root.theme.dp(37)
                        y: Math.max(root.theme.dp(102), detailsHelp.y + detailsHelp.height + root.theme.dp(23))
                        placeholderText: qsTr("e.g., Favorite Videos")
                        color: root.theme.settingsHeading; placeholderTextColor: root.lightTheme ? root.theme.foreground : "#404040"
                        selectionColor: root.theme.accent; selectedTextColor: root.theme.field
                        font.pixelSize: root.theme.dp(14); leftPadding: root.theme.dp(14)
                        topPadding: root.theme.dp(6.5); bottomPadding: root.theme.dp(5.5)
                        readOnly: root.busy; selectByMouse: true
                        Accessible.name: qsTr("Playlist name")
                        onActiveFocusChanged: if (activeFocus) root.revealOnFocus(this)
                        background: Rectangle { color: root.theme.field; radius: root.theme.dp(6); border.width: root.theme.dp(2); border.color: nameField.activeFocus ? root.theme.accent : root.lightTheme ? root.theme.border : "#191919" }
                        onAccepted: root.submit()
                    }
                }
                Item {
                    id: coverBox
                    objectName: "playlistCoverBox"
                    x: parent.width - root.theme.dp(132); y: root.theme.dp(29)
                    width: root.theme.dp(112); height: root.theme.dp(112)
                    Rectangle { anchors.fill: parent; radius: root.theme.dp(6); color: root.theme.field; border.width: root.theme.dp(2); border.color: root.lightTheme ? root.theme.border : "#191919" }
                    Image {
                        anchors.fill: parent; anchors.margins: root.theme.dp(4)
                        source: root.cover; asynchronous: true; fillMode: Image.PreserveAspectCrop
                        sourceSize: Qt.size(root.theme.dp(224), root.theme.dp(224))
                    }
                    RedesignButton {
                        objectName: "playlistCoverButton"
                        x: (parent.width - width) / 2; y: (parent.height - height) / 2
                        implicitWidth: root.theme.dp(30); implicitHeight: root.theme.dp(30)
                        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                        iconName: "add"; text: root.cover.toString().length ? qsTr("Change thumbnail") : qsTr("Add thumbnail")
                        enabled: !root.busy
                        onActiveFocusChanged: if (activeFocus) root.revealOnFocus(this)
                        background: Rectangle { color: parent.hovered ? root.theme.hover : root.lightTheme ? root.theme.panel : "#161616"; radius: root.theme.dp(5); border.color: parent.visualFocus ? root.theme.accent : "transparent" }
                        onClicked: root.coverRequested()
                    }
                    RedesignButton {
                        objectName: "playlistRemoveCoverButton"
                        anchors.right: parent.right; anchors.top: parent.top
                        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                        implicitWidth: root.theme.dp(28); implicitHeight: root.theme.dp(28); iconSize: root.theme.dp(18)
                        iconName: "close"; text: qsTr("Remove thumbnail"); visible: root.cover.toString().length > 0; enabled: !root.busy
                        onActiveFocusChanged: if (activeFocus) root.revealOnFocus(this)
                        onClicked: root.cover = ""
                    }
                }
            }
            RedesignSeparator {
                x: root.theme.dp(20); width: parent.width - root.theme.dp(40)
                scaleFactor: root.scaleFactor
                color: root.lightTheme ? root.theme.border : "#222222"
            }
            Item {
                width: parent.width
                height: Math.max(root.theme.dp(97), filesHelp.y + filesHelp.height + root.theme.dp(23))
                Text {
                    id: filesHeading
                    x: root.theme.dp(20); width: parent.width - root.theme.dp(40)
                    y: root.theme.dp(39) - baselineOffset
                    text: qsTr("Select Files"); color: root.theme.settingsHeading
                    font.pixelSize: root.theme.dp(16); wrapMode: Text.WordWrap
                }
                Text {
                    id: filesHelp
                    x: root.theme.dp(20); width: parent.width - root.theme.dp(40)
                    y: Math.max(root.theme.dp(70) - baselineOffset, filesHeading.y + filesHeading.height + root.theme.dp(10))
                    text: qsTr("These files will be added to the playlist."); color: root.theme.playerLabel
                    font.pixelSize: root.theme.dp(14); wrapMode: Text.WordWrap
                }
            }
            Item {
                id: dropBox
                objectName: "playlistDropBox"
                x: root.theme.dp(20); width: parent.width - root.theme.dp(40); height: root.theme.dp(186)
                Rectangle { anchors.fill: parent; radius: root.theme.dp(6); color: root.theme.field }
                RedesignSvg {
                    anchors.fill: parent
                    svg: '<svg xmlns="http://www.w3.org/2000/svg" width="' + width + '" height="' + height
                        + '" viewBox="0 0 ' + width / root.scaleFactor + ' ' + height / root.scaleFactor
                        + '"><rect x="1" y="1" width="' + Math.max(0, width / root.scaleFactor - 2)
                        + '" height="' + Math.max(0, height / root.scaleFactor - 2)
                        + '" rx="5" fill="none" stroke="' + (fileDrop.containsDrag ? root.theme.accent : root.lightTheme ? root.theme.border : "#2d2d2d") + '" stroke-width="2" stroke-dasharray="4 4"/></svg>'
                }
                DropArea {
                    id: fileDrop
                    objectName: "playlistFileDropArea"
                    anchors.fill: parent; enabled: !root.busy
                    onEntered: drag => drag.accepted = drag.hasUrls
                    onDropped: drop => { if (drop.hasUrls) { root.addFiles(drop.urls); drop.acceptProposedAction() } }
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: !root.busy && files.count === 0
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.filesRequested()
                }
                Item {
                    anchors.fill: parent; visible: files.count === 0
                    Text {
                        width: parent.width - root.theme.dp(32)
                        anchors.horizontalCenter: parent.horizontalCenter; anchors.horizontalCenterOffset: -root.theme.dp(3)
                        y: Math.max(root.theme.dp(73.5) - baselineOffset, root.theme.dp(16))
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("Drop files here or select files."); color: root.lightTheme ? root.theme.foreground : "#404040"
                        font.pixelSize: root.theme.dp(14); wrapMode: Text.WordWrap
                    }
                    RedesignButton {
                        objectName: "playlistSelectFilesButton"
                        x: (parent.width - width) / 2 - root.theme.dp(3)
                        y: root.theme.dp(96.5)
                        implicitWidth: root.theme.dp(84); implicitHeight: root.theme.dp(30)
                        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                        iconName: "add"; text: qsTr("Select files"); enabled: !root.busy
                        onActiveFocusChanged: if (activeFocus) root.revealOnFocus(this)
                        background: Rectangle { color: parent.hovered ? root.theme.hover : root.lightTheme ? root.theme.panel : "#161616"; radius: root.theme.dp(5); border.color: parent.visualFocus ? root.theme.accent : "transparent" }
                        onClicked: root.filesRequested()
                    }
                }
                ListView {
                    id: fileList
                    objectName: "playlistSelectedFiles"
                    anchors.fill: parent; anchors.margins: root.theme.dp(8); anchors.bottomMargin: root.theme.dp(44)
                    visible: files.count > 0; clip: true
                    model: files
                    C.ScrollBar.vertical: C.ScrollBar { }
                    delegate: RowLayout {
                        required property int index
                        required property string name
                        required property string url
                        width: fileList.width; height: root.theme.dp(36)
                        Text { Layout.fillWidth: true; text: name; elide: Text.ElideRight; color: root.theme.foreground; font.pixelSize: root.theme.dp(14) }
                        RedesignButton {
                            objectName: "playlistRemoveFile" + index
                            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                            iconName: "remove"; text: qsTr("Remove %1").arg(name); enabled: !root.busy
                            onActiveFocusChanged: if (activeFocus) root.revealOnFocus(this)
                            onClicked: root.removeFile(index)
                        }
                    }
                }
                RedesignButton {
                    objectName: "playlistAddMoreFilesButton"
                    anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: root.theme.dp(8)
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    iconName: "add"; caption: qsTr("Add files"); text: caption
                    visible: files.count > 0; enabled: !root.busy
                    onActiveFocusChanged: if (activeFocus) root.revealOnFocus(this)
                    onClicked: root.filesRequested()
                }
            }
            Item { width: 1; height: root.theme.dp(20) }
        }
    }
    footer: Item {
        implicitHeight: root.theme.dp(74) + (errorText.visible ? errorText.implicitHeight + root.theme.dp(12) : 0)
        Text {
            id: errorText
            objectName: "playlistErrorMessage"
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.leftMargin: root.theme.dp(20); anchors.rightMargin: root.theme.dp(20); anchors.topMargin: root.theme.dp(8)
            visible: root.errorMessage.length > 0
            text: root.errorMessage; color: root.theme.danger; font.pixelSize: root.theme.dp(14); wrapMode: Text.WordWrap
            Accessible.role: Accessible.AlertMessage
        }
        RedesignButton {
            objectName: "playlistCancelButton"
            x: root.theme.dp(20); anchors.bottom: parent.bottom; anchors.bottomMargin: root.theme.dp(20)
            implicitWidth: root.theme.dp(100); implicitHeight: root.theme.dp(32)
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            caption: qsTr("Cancel"); captionFontWeight: Font.DemiBold; captionVerticalOffset: 1; text: caption; enabled: !root.busy
            background: Rectangle { color: parent.hovered ? root.theme.hover : root.theme.background; radius: root.theme.dp(6); border.color: parent.visualFocus ? root.theme.accent : "transparent" }
            onClicked: root.reject()
        }
        RedesignButton {
            objectName: "playlistSaveButton"
            anchors.right: parent.right; anchors.rightMargin: root.theme.dp(20); anchors.bottom: parent.bottom; anchors.bottomMargin: root.theme.dp(20)
            implicitWidth: root.theme.dp(100); implicitHeight: root.theme.dp(32)
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            caption: root.busy ? qsTr("Saving…") : qsTr("Save"); text: caption
            enabled: root.valid && !root.busy; primaryAction: true; captionFontWeight: Font.DemiBold; captionVerticalOffset: 1
            onClicked: root.submit()
        }
    }
}

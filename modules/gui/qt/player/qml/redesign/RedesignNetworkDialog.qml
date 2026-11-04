// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Controls.Basic as C

C.Dialog {
    id: root
    popupType: C.Popup.Item
    property real scaleFactor: 1
    property bool lightTheme: false
    property color accentColor: "#d37918"
    property Component videoBackdropContent
    property bool busy: false
    property bool canSavePlaylist: true
    property string errorMessage: ""
    property alias urlsText: urlsField.text
    property alias playlistName: nameField.text
    readonly property var urls: urlsText.split(/\r?\n/).map(line => line.trim()).filter(line => line.length > 0)
    readonly property bool valid: urls.length > 0 && urls.every(url => /^[a-z][a-z0-9+.-]*:\/\/\S+$/i.test(url))
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    signal saveRequested(var urls, string name)

    title: qsTr("Open Network Stream")
    // Keep the centered surface on physical pixels for sharp text and borders.
    readonly property real pixelRatio: parent ? parent.Screen.devicePixelRatio : 1
    x: parent ? Math.round((parent.width - width) * pixelRatio / 2) / pixelRatio : 0
    y: parent ? Math.round((parent.height - height) * pixelRatio / 2) / pixelRatio : 0
    width: Math.min(theme.dp(1048), parent ? parent.width - theme.dp(32) : theme.dp(1048))
    height: Math.min(theme.dp(631), parent ? parent.height - theme.dp(32) : theme.dp(631))
    padding: 0; spacing: 0
    modal: true; dim: true; focus: true
    closePolicy: busy ? C.Popup.NoAutoClose : C.Popup.CloseOnEscape
    background: RedesignDialogSurface { theme: root.theme; videoBackdropContent: root.videoBackdropContent }
    C.Overlay.modal: Rectangle { color: "#80000000" }

    function begin() {
        if (!visible) { urlsText = ""; playlistName = ""; errorMessage = ""; open() }
        urlsField.forceActiveFocus()
    }
    function submit() {
        if (!valid || busy) return
        errorMessage = ""
        busy = true
        saveRequested(urls, canSavePlaylist ? playlistName.trim() : "")
    }
    function finish(success) {
        busy = false
        if (success) accept()
        else { errorMessage = qsTr("Hakone cannot open these streams. Make sure that the links are correct. Try again."); urlsField.forceActiveFocus() }
    }
    function revealFocusedField() { Qt.callLater(revealFocusedFieldNow) }
    function revealFocusedFieldNow() {
        const field = urlsField.activeFocus ? urlsField : nameField.activeFocus ? nameField : null
        if (!root.visible || !field) return
        const viewport = scroll.contentItem
        const point = field.mapToItem(viewport.contentItem, 0, 0)
        let target = viewport.contentY
        if (point.y < target || field.height >= viewport.height) target = point.y - root.theme.dp(8)
        else if (point.y + field.height > target + viewport.height)
            target = point.y + field.height + root.theme.dp(8) - viewport.height
        viewport.contentY = Math.max(0, Math.min(target, viewport.contentHeight - viewport.height))
    }
    onOpened: urlsField.forceActiveFocus()
    onUrlsTextChanged: errorMessage = ""
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
            objectName: "networkCloseButton"
            anchors.right: parent.right; anchors.rightMargin: root.theme.dp(7); anchors.verticalCenter: parent.verticalCenter
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            iconName: "close"; text: qsTr("Close"); enabled: !root.busy
            onClicked: root.reject()
        }
    }
    contentItem: C.ScrollView {
        id: scroll
        clip: true
        contentWidth: availableWidth
        onAvailableHeightChanged: if (root.opened) root.revealFocusedField()
        C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
        Column {
            width: scroll.availableWidth
            Item {
                width: parent.width; height: Math.max(root.theme.dp(100), urlHelp.y + urlHelp.height + root.theme.dp(23))
                Text {
                    id: urlHeading
                    x: root.theme.dp(20); width: parent.width - root.theme.dp(40)
                    y: root.theme.dp(42) - baselineOffset
                    text: qsTr("Network URL"); color: root.theme.settingsHeading; font.pixelSize: root.theme.dp(16)
                    wrapMode: Text.WordWrap
                }
                Text {
                    id: urlHelp
                    x: root.theme.dp(20); width: parent.width - root.theme.dp(40)
                    y: Math.max(root.theme.dp(73) - baselineOffset, urlHeading.y + urlHeading.height + root.theme.dp(10))
                    text: qsTr("Enter one link on each line."); color: root.theme.playerLabel; font.pixelSize: root.theme.dp(14)
                    wrapMode: Text.WordWrap
                }
            }
            C.ScrollView {
                x: root.theme.dp(20); width: parent.width - root.theme.dp(40); height: root.theme.dp(194)
                clip: true
                C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
                C.TextArea {
                    id: urlsField
                    objectName: "networkUrlsField"
                    placeholderText: qsTr("e.g., https://www.example.com/stream.avi")
                    color: root.theme.settingsHeading; placeholderTextColor: root.lightTheme ? root.theme.foreground : "#404040"
                    selectionColor: root.theme.accent; selectedTextColor: root.theme.field
                    font.pixelSize: root.theme.dp(14); padding: root.theme.dp(14)
                    leftPadding: root.theme.dp(14); topPadding: root.theme.dp(10.5)
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true; readOnly: root.busy
                    Accessible.name: qsTr("Network URLs")
                    onActiveFocusChanged: if (activeFocus) root.revealFocusedField()
                    background: Rectangle { color: root.theme.field; radius: root.theme.dp(6); border.width: root.theme.dp(2); border.color: urlsField.activeFocus ? root.theme.accent : root.lightTheme ? root.theme.border : "#191919" }
                }
            }
            Text {
                x: root.theme.dp(20); width: parent.width - root.theme.dp(40)
                topPadding: root.theme.dp(12)
                visible: root.errorMessage.length > 0
                text: root.errorMessage; color: root.theme.danger; font.pixelSize: root.theme.dp(14); wrapMode: Text.WordWrap
                Accessible.role: Accessible.AlertMessage
            }
            Item { width: 1; height: root.theme.dp(20) }
            RedesignSeparator {
                x: root.theme.dp(20); width: parent.width - root.theme.dp(40)
                scaleFactor: root.scaleFactor
                color: root.lightTheme ? root.theme.border : "#222222"
            }
            Item {
                width: parent.width; height: Math.max(root.theme.dp(99), nameHelp.y + nameHelp.height + root.theme.dp(23))
                Text {
                    id: nameHeading
                    x: root.theme.dp(20); width: parent.width - root.theme.dp(40)
                    y: root.theme.dp(39) - baselineOffset
                    readonly property string headingText: qsTr("Playlist Name (optional)")
                    text: {
                        const label = headingText.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
                        return label.replace(/(\([^)]*\)|（[^）]*）)(\s*)$/, "<font color='" + root.theme.playerLabel + "'>$1</font>$2")
                    }
                    textFormat: Text.RichText
                    color: root.theme.settingsHeading; font.pixelSize: root.theme.dp(16)
                    wrapMode: Text.WordWrap
                }
                Text {
                    id: nameHelp
                    x: root.theme.dp(20); width: parent.width - root.theme.dp(40)
                    y: Math.max(root.theme.dp(70) - baselineOffset, nameHeading.y + nameHeading.height + root.theme.dp(10))
                    text: root.canSavePlaylist ? qsTr("Save these links in a playlist with this name.")
                                              : qsTr("To save a playlist, turn on the media library in Settings. Close Hakone. Start Hakone again.")
                    color: root.theme.playerLabel; font.pixelSize: root.theme.dp(14)
                    wrapMode: Text.WordWrap
                }
            }
            C.TextField {
                id: nameField
                objectName: "networkPlaylistNameField"
                x: root.theme.dp(20); width: parent.width - root.theme.dp(40); height: root.theme.dp(37)
                placeholderText: qsTr("e.g., Favorite TV Channels")
                color: root.theme.settingsHeading; placeholderTextColor: root.lightTheme ? root.theme.foreground : "#404040"
                selectionColor: root.theme.accent; selectedTextColor: root.theme.field
                font.pixelSize: root.theme.dp(14); leftPadding: root.theme.dp(14)
                topPadding: root.theme.dp(6.5); bottomPadding: root.theme.dp(5.5)
                enabled: root.canSavePlaylist
                opacity: enabled ? 1 : .5
                readOnly: root.busy; selectByMouse: true
                Accessible.name: qsTr("Playlist name")
                onActiveFocusChanged: if (activeFocus) root.revealFocusedField()
                background: Rectangle { color: root.theme.field; radius: root.theme.dp(6); border.width: root.theme.dp(2); border.color: nameField.activeFocus ? root.theme.accent : root.lightTheme ? root.theme.border : "#191919" }
                onAccepted: root.submit()
            }
            Item { width: 1; height: root.theme.dp(20) }
        }
    }
    footer: Item {
        implicitHeight: root.theme.dp(72)
        RedesignButton {
            objectName: "networkCancelButton"
            x: root.theme.dp(20); anchors.verticalCenter: parent.verticalCenter
            implicitWidth: root.theme.dp(100); implicitHeight: root.theme.dp(32)
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            caption: qsTr("Cancel"); captionFontWeight: Font.DemiBold; captionVerticalOffset: 1; text: caption; enabled: !root.busy
            background: Rectangle { color: parent.hovered ? root.theme.hover : root.theme.background; radius: root.theme.dp(6); border.color: parent.visualFocus ? root.theme.accent : "transparent" }
            onClicked: root.reject()
        }
        RedesignButton {
            objectName: "networkSaveButton"
            anchors.right: parent.right; anchors.rightMargin: root.theme.dp(20); anchors.verticalCenter: parent.verticalCenter
            implicitWidth: root.theme.dp(100); implicitHeight: root.theme.dp(32)
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            caption: root.canSavePlaylist ? (root.busy ? qsTr("Saving…") : qsTr("Save"))
                                          : (root.busy ? qsTr("Opening…") : qsTr("Open"))
            text: caption
            enabled: root.valid && !root.busy; primaryAction: true; captionFontWeight: Font.DemiBold; captionVerticalOffset: 1
            onClicked: root.submit()
        }
    }
}

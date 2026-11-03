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
    property bool hasCustomKey: false
    property bool hasConsumerKey: false
    property bool busy: false
    property string errorMessage: ""
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    readonly property real pixelRatio: parent ? parent.Screen.devicePixelRatio : 1
    readonly property bool stackedActions: availableWidth < Math.max(theme.dp(100), cancelMetrics.advanceWidth + theme.dp(16)) + Math.max(theme.dp(100), saveMetrics.advanceWidth + theme.dp(16)) + theme.dp(8)
    signal saveRequested(string key)
    signal removeRequested()
    title: qsTr("OpenSubtitles setup")
    x: parent ? Math.round((parent.width - width) * pixelRatio / 2) / pixelRatio : 0
    y: parent ? Math.round((parent.height - height) * pixelRatio / 2) / pixelRatio : 0
    width: Math.max(0, Math.min(theme.dp(560), parent ? parent.width - theme.dp(32) : theme.dp(560)))
    height: Math.min(implicitHeight, parent ? Math.max(0, parent.height - theme.dp(32)) : implicitHeight)
    padding: theme.dp(24); modal: true; dim: true; focus: true
    closePolicy: C.Popup.CloseOnEscape
    header: null; footer: null
    function submit() {
        if (!busy && apiKeyField.text.trim().length) saveRequested(apiKeyField.text)
    }
    function revealKeyField() {
        const flick = bodyScroll.contentItem
        if (!opened || !apiKeyField.activeFocus || !flick || bodyScroll.availableHeight <= 0) return
        const top = apiKeyField.mapToItem(body, 0, 0).y
        const bottom = top + apiKeyField.height
        if (top < flick.contentY) flick.contentY = top
        else if (bottom > flick.contentY + bodyScroll.availableHeight)
            flick.contentY = Math.max(0, bottom - bodyScroll.availableHeight)
    }
    function finish(success, error = "") {
        if (success) close()
        else errorMessage = error || qsTr("VLC cannot save this change. Make sure that the key is correct. Try again.")
    }
    onOpened: { errorMessage = ""; apiKeyField.forceActiveFocus(); Qt.callLater(revealKeyField) }
    onClosed: apiKeyField.text = ""
    background: RedesignDialogSurface { theme: root.theme; videoBackdropContent: root.videoBackdropContent }
    C.Overlay.modal: Rectangle { color: "#80000000" }
    TextMetrics { id: cancelMetrics; text: cancelButton.caption; font.pixelSize: root.theme.dp(14); font.weight: Font.DemiBold }
    TextMetrics { id: saveMetrics; text: saveButton.caption; font.pixelSize: root.theme.dp(14); font.weight: Font.DemiBold }
    TextMetrics { id: helpMetrics; text: helpButton.caption; font.pixelSize: root.theme.dp(14) }
    TextMetrics { id: removeMetrics; text: removeButton.caption; font.pixelSize: root.theme.dp(14) }
    contentItem: ColumnLayout {
        Accessible.role: Accessible.Dialog
        Accessible.name: root.title
        spacing: root.theme.dp(20)
        C.ScrollView {
            id: bodyScroll
            objectName: "subtitleProviderBody"
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 0
            implicitHeight: body.implicitHeight
            contentWidth: availableWidth
            clip: true
            onHeightChanged: if (root.opened) Qt.callLater(root.revealKeyField)
            C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
            ColumnLayout {
                id: body
                width: bodyScroll.availableWidth
                spacing: root.theme.dp(16)
                Text {
                    objectName: "subtitleProviderTitle"
                    Layout.fillWidth: true
                    text: root.title; textFormat: Text.PlainText
                    font.pixelSize: root.theme.dp(20); color: root.theme.bright; wrapMode: Text.Wrap
                }
                Text {
                    Layout.fillWidth: true
                    text: root.hasConsumerKey
                        ? qsTr("This app can use OpenSubtitles without a personal key. You can add your own application API key here. Hakone stores it in your profile.")
                        : qsTr("Add your OpenSubtitles application API key. Hakone stores it in your profile.")
                    textFormat: Text.PlainText; wrapMode: Text.WordWrap
                    font.pixelSize: root.theme.dp(14); color: root.theme.foreground
                }
                C.TextField {
                    id: apiKeyField
                    objectName: "subtitleProviderApiKey"
                    Layout.fillWidth: true; Layout.preferredHeight: root.theme.dp(36)
                    leftPadding: root.theme.dp(10); rightPadding: root.theme.dp(10)
                    echoMode: TextInput.Password; placeholderText: qsTr("API key"); Accessible.name: placeholderText
                    color: root.theme.foreground; font.pixelSize: root.theme.dp(14); selectByMouse: true
                    enabled: !root.busy
                    onYChanged: if (root.opened && activeFocus) Qt.callLater(root.revealKeyField)
                    background: Rectangle { color: root.theme.field; radius: root.theme.dp(6); border.color: parent.visualFocus ? root.theme.accent : "transparent" }
                    onAccepted: root.submit()
                }
                RedesignButton {
                    id: helpButton
                    objectName: "subtitleProviderHelp"
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    caption: qsTr("Get an API key"); text: caption; iconName: "link"
                    Layout.maximumWidth: body.width; Layout.preferredWidth: helpMetrics.advanceWidth + root.theme.dp(46)
                    captionMaximumWidth: Math.max(0, width - root.theme.dp(46))
                    onClicked: root.helpRequested()
                }
                Text {
                    objectName: "subtitleProviderError"
                    Layout.fillWidth: true; visible: text.length > 0
                    text: root.errorMessage; textFormat: Text.PlainText; wrapMode: Text.WordWrap
                    font.pixelSize: root.theme.dp(14); color: root.theme.danger
                    Accessible.role: Accessible.AlertMessage
                }
                RedesignButton {
                    id: removeButton
                    objectName: "subtitleProviderRemoveKey"
                    visible: root.hasCustomKey; enabled: !root.busy
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    caption: root.hasConsumerKey ? qsTr("Use app key") : qsTr("Remove key"); text: caption
                    Layout.maximumWidth: body.width; Layout.preferredWidth: Math.max(root.theme.dp(36), removeMetrics.advanceWidth + root.theme.dp(16))
                    captionMaximumWidth: Math.max(0, width - root.theme.dp(16))
                    foreground: root.hasConsumerKey ? root.theme.foreground : root.theme.danger
                    background: Rectangle { color: parent.hovered ? root.theme.hover : root.hasConsumerKey ? root.theme.background : root.theme.dangerBackground; radius: root.theme.dp(6); border.width: parent.visualFocus ? root.theme.dp(1) : 0; border.color: parent.foreground }
                    onClicked: root.removeRequested()
                }
            }
        }
        GridLayout {
            Layout.alignment: Qt.AlignRight; Layout.fillWidth: root.stackedActions
            columns: root.stackedActions ? 1 : 2
            columnSpacing: root.theme.dp(8); rowSpacing: root.theme.dp(8)
            RedesignButton {
                id: cancelButton
                objectName: "subtitleProviderCancel"
                Layout.fillWidth: root.stackedActions
                Layout.preferredWidth: Math.max(root.theme.dp(100), cancelMetrics.advanceWidth + root.theme.dp(16))
                Layout.preferredHeight: root.theme.dp(32)
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                caption: qsTr("Cancel"); captionFontWeight: Font.DemiBold; text: caption
                captionMaximumWidth: Math.max(0, width - root.theme.dp(16)); captionVerticalOffset: 1
                filled: true; onClicked: root.close()
                Keys.onReturnPressed: root.close()
                Keys.onEnterPressed: root.close()
            }
            RedesignButton {
                id: saveButton
                objectName: "subtitleProviderSaveKey"
                Layout.fillWidth: root.stackedActions
                Layout.preferredWidth: Math.max(root.theme.dp(100), saveMetrics.advanceWidth + root.theme.dp(16))
                Layout.preferredHeight: root.theme.dp(32)
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                caption: qsTr("Save"); captionFontWeight: Font.DemiBold; text: caption
                captionMaximumWidth: Math.max(0, width - root.theme.dp(16)); captionVerticalOffset: 1
                primaryAction: true; enabled: !root.busy && apiKeyField.text.trim().length > 0
                onClicked: root.submit()
                Keys.onReturnPressed: root.submit()
                Keys.onEnterPressed: root.submit()
            }
        }
    }
}

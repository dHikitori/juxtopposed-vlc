// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

C.Popup {
    id: root
    required property var backend
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    readonly property RedesignTheme theme: RedesignTheme {
        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
    }
    readonly property var review: backend ? backend.review || ({}) : ({})
    readonly property bool busy: !!backend && (backend.preparing === true || backend.acting === true)
    function reviewSource(contribution) {
        if (!backend) return
        open()
        backend.prepare(contribution)
    }
    function accountValue(value) {
        return typeof value === "string" || typeof value === "number" || typeof value === "boolean"
            ? String(value).slice(0, 256) : ""
    }
    modal: true
    dim: true
    popupType: C.Popup.Item
    parent: C.Overlay.overlay
    width: Math.min(theme.dp(640), parent.width - theme.dp(24))
    height: Math.min(theme.dp(660), parent.height - theme.dp(24))
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    padding: theme.dp(16)
    focus: true
    closePolicy: C.Popup.CloseOnEscape
    onClosed: if (backend && typeof backend.viewClosed === "function") backend.viewClosed()
    Component.onDestruction: if (backend && typeof backend.viewClosed === "function") backend.viewClosed()
    background: RedesignDialogSurface { theme: root.theme }
    contentItem: ColumnLayout {
        spacing: root.theme.dp(12)
        Text {
            Layout.fillWidth: true
            text: qsTr("Source playback access")
            textFormat: Text.PlainText
            color: root.theme.bright
            font.pixelSize: root.theme.dp(18)
        }
        C.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            visible: !!root.review.name
            ColumnLayout {
                width: parent.width
                spacing: root.theme.dp(10)
                Text {
                    objectName: "sourceConsentName"
                    Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
                    text: root.review.name || ""
                    color: root.theme.settingsHeading; font.pixelSize: root.theme.dp(17)
                }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
                    text: qsTr("Permit this add-on and account to play media from public HTTPS servers.")
                    color: root.theme.foreground; font.pixelSize: root.theme.dp(14)
                }
                Text {
                    objectName: "sourceConsentRepository"
                    Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
                    text: qsTr("Add-on source: %1").arg(root.review.source || "")
                    color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
                }
                Text {
                    objectName: "sourceConsentAccount"
                    Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
                    text: root.review.accountLabel || qsTr("This account")
                    color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
                }
                Repeater {
                    model: Object.keys(root.review.accountFields || {}).slice(0, 32)
                    Text {
                        required property string modelData
                        Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
                        text: modelData.slice(0, 128) + ": " + root.accountValue(root.review.accountFields[modelData])
                        color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
                    }
                }
                Text {
                    objectName: "sourceConsentPublisher"
                    Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
                    visible: root.review.publisherVerified === false
                    text: qsTr("The app did not verify the publisher.")
                    color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
                }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
                    text: root.review.approved === true
                        ? qsTr("Playback is permitted for this source and account. You can remove this approval.")
                        : qsTr("This approval stays when the panel closes. A source or account change needs a new review.")
                    color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
                }
            }
        }
        Text {
            objectName: "sourceConsentStatus"
            Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
            text: root.backend ? root.backend.status || (root.backend.preparing ? qsTr("The app prepares the source review.") : "") : ""
            visible: text.length > 0
            color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
        }
        Text {
            objectName: "sourceConsentError"
            Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
            text: root.backend ? root.backend.errorMessage || "" : ""
            visible: text.length > 0
            color: root.theme.danger; font.pixelSize: root.theme.dp(13)
            Accessible.role: Accessible.AlertMessage
        }
        RowLayout {
            Layout.fillWidth: true
            RedesignButton {
                objectName: "sourceConsentApprove"
                visible: !!root.review.name && root.review.approved === false
                enabled: !root.busy
                caption: qsTr("Allow source playback"); text: caption; tooltipTextFormat: Text.PlainText
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.backend.approve()
            }
            RedesignButton {
                objectName: "sourceConsentRevoke"
                visible: !!root.review.name && root.review.approved === true
                enabled: !root.busy
                caption: qsTr("Remove playback approval"); text: caption; tooltipTextFormat: Text.PlainText
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.backend.revoke()
            }
            Item { Layout.fillWidth: true }
            RedesignButton {
                objectName: "sourceConsentClose"
                caption: qsTr("Back"); text: caption; tooltipTextFormat: Text.PlainText
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.close()
            }
        }
    }
}

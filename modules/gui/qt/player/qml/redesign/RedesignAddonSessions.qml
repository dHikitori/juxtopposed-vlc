// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

ColumnLayout {
    id: root
    required property var backend
    property bool active: true
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    property var selectedOperations: []
    property var displayOwner: null
    property bool displayMounted: false
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme }
    readonly property var controllers: backend ? backend.controllers || [] : []
    readonly property var review: backend ? backend.review || ({}) : ({})
    readonly property string reviewSignature: JSON.stringify(review)
    readonly property var selectedController: controllers[controller.currentIndex] || ({})
    readonly property bool busy: !!backend && (backend.preparing === true || backend.starting === true)
    readonly property bool displayCurrent: !backend || backend.displayCurrent === undefined || backend.displayCurrent === true
    readonly property bool displayLoading: !!backend && backend.displayLoading === true
    readonly property string displayError: backend ? backend.displayError || "" : ""
    readonly property bool controllerReady: displayCurrent && selectedController.ready !== false
    spacing: theme.dp(10)
    onReviewSignatureChanged: selectedOperations = []
    onActiveChanged: syncDisplay()
    onVisibleChanged: syncDisplay()
    onBackendChanged: syncDisplay()
    Component.onCompleted: { displayMounted = true; syncDisplay() }
    Component.onDestruction: releaseDisplay(displayOwner)
    function releaseDisplay(owner) {
        if (!owner) return
        if (typeof owner.setDisplayActive === "function") owner.setDisplayActive(false)
        else if (typeof owner.viewClosed === "function") owner.viewClosed()
    }
    function syncDisplay() {
        if (!displayMounted) return
        if (displayOwner !== backend) {
            const retired = displayOwner
            displayOwner = backend
            releaseDisplay(retired)
        }
        if (!displayOwner) return
        if (typeof displayOwner.setDisplayActive === "function") displayOwner.setDisplayActive(active && visible)
        else if (!active || !visible) releaseDisplay(displayOwner)
    }
    function operationLabel(value) {
        return ({play: qsTr("Play"), pause: qsTr("Pause"), seek: qsTr("Seek"), setRate: qsTr("Temporary speed correction")})[value] || value
    }
    function fieldLabel(value) {
        return ({mediaEpoch: qsTr("Playback identity"), transportRevision: qsTr("Playback state revision"), observedAtUs: qsTr("Observation time"),
            origin: qsTr("Control source"), originSequence: qsTr("Control sequence"), originOperation: qsTr("Control action"), suspended: qsTr("Control suspension"),
            positionUs: qsTr("Position"), durationUs: qsTr("Duration"), rate: qsTr("Speed"), paused: qsTr("Pause state"), pausable: qsTr("Pause support"),
            seekable: qsTr("Seek support"), rateChange: qsTr("Speed support"), buffering: qsTr("Buffer state"), live: qsTr("Live state"),
            liveStartUs: qsTr("Live start"), liveEndUs: qsTr("Live end"), outputLatencyUs: qsTr("Output delay"), outputLatencyUncertaintyUs: qsTr("Output delay uncertainty")})[value] || value
    }
    function toggleOperation(value, enabled) {
        selectedOperations = enabled ? [...new Set(selectedOperations.concat([value]))] : selectedOperations.filter(item => item !== value)
    }
    Text {
        Layout.fillWidth: true; text: qsTr("Shared playback sessions"); textFormat: Text.PlainText
        color: root.theme.settingsHeading; font.pixelSize: root.theme.dp(17)
    }
    Text {
        Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
        objectName: "addonSessionIntroduction"
        text: root.displayLoading ? qsTr("The app checks session controllers.")
            : !root.displayCurrent ? qsTr("The controller list is not ready. Try again.")
            : root.controllers.length ? qsTr("Select an add-on, relay address, and room. Review what the session can share before you join.")
            : qsTr("No session controller is installed. Add a controller first.")
        color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
    }
    RedesignComboBox {
        id: controller
        objectName: "addonSessionController"
        Layout.fillWidth: true; model: root.controllers; textRole: "name"; valueRole: "id"
        enabled: !root.busy && root.controllers.length > 0
        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
        Accessible.name: qsTr("Session controller")
        onActivated: { root.backend.cancelReview(); relay.text = (root.selectedController.origins || [])[0] || "" }
        contentItem: Text { text: controller.displayText; textFormat: Text.PlainText; elide: Text.ElideRight; color: root.theme.foreground; font.pixelSize: root.theme.dp(14); verticalAlignment: Text.AlignVCenter }
        delegate: C.ItemDelegate {
            required property int index
            width: ListView.view.width; implicitHeight: root.theme.dp(36)
            contentItem: Text {
                readonly property var entry: root.controllers[index] || ({})
                text: (entry.name || "") + (entry.state ? " — " + entry.state : "")
                textFormat: Text.PlainText; elide: Text.ElideRight; color: root.theme.foreground; font.pixelSize: root.theme.dp(14)
            }
            background: Rectangle { color: hovered ? root.theme.hover : root.theme.panel }
        }
    }
    Text {
        objectName: "addonSessionControllerState"; Layout.fillWidth: true
        text: root.selectedController.state || ""; visible: text.length > 0
        textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
    }
    Text {
        objectName: "addonSessionDisplayError"; Layout.fillWidth: true
        text: root.displayError; visible: text.length > 0; textFormat: Text.PlainText
        wrapMode: Text.Wrap; color: root.theme.danger; font.pixelSize: root.theme.dp(13); Accessible.role: Accessible.AlertMessage
    }
    RedesignButton {
        objectName: "addonSessionRetry"
        visible: !root.displayLoading && (!root.displayCurrent || root.displayError.length > 0)
        caption: qsTr("Try again"); text: caption; tooltipTextFormat: Text.PlainText
        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
        onClicked: if (root.backend && typeof root.backend.retryDisplay === "function") root.backend.retryDisplay()
    }
    SessionInput {
        id: relay; objectName: "addonSessionRelay"; Layout.fillWidth: true
        maximumLength: 2048; placeholderText: qsTr("Relay address: wss://relay.example/room")
        enabled: !root.busy && root.controllerReady && root.controllers.length > 0; Accessible.name: qsTr("Relay address")
        onTextEdited: root.backend.cancelReview()
    }
    SessionInput {
        id: room; objectName: "addonSessionRoom"; Layout.fillWidth: true
        maximumLength: 256; placeholderText: qsTr("Room name"); enabled: !root.busy && root.controllerReady && root.controllers.length > 0
        Accessible.name: qsTr("Room name"); onTextEdited: root.backend.cancelReview()
    }
    RedesignButton {
        objectName: "addonSessionPrepare"
        caption: qsTr("Review session"); text: caption; tooltipTextFormat: Text.PlainText
        enabled: !root.busy && root.controllerReady && !!root.selectedController.id && relay.text.length > 0 && room.text.length > 0
        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
        onClicked: root.backend.prepare(root.selectedController.id, relay.text, room.text)
    }
    ColumnLayout {
        objectName: "addonSessionReview"
        Layout.fillWidth: true; visible: !!root.review.name; spacing: root.theme.dp(8)
        Text { objectName: "addonSessionReviewName"; Layout.fillWidth: true; text: root.review.name || ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.settingsHeading; font.pixelSize: root.theme.dp(16) }
        Text { objectName: "addonSessionReviewRelay"; Layout.fillWidth: true; text: qsTr("Relay: %1").arg(root.review.relay || ""); textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.foreground; font.pixelSize: root.theme.dp(13) }
        Text { objectName: "addonSessionReviewRoom"; Layout.fillWidth: true; text: qsTr("Room: %1").arg(root.review.room || ""); textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.foreground; font.pixelSize: root.theme.dp(13) }
        Text {
            objectName: "addonSessionReviewFields"; Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
            text: qsTr("Share: %1").arg((root.review.observationFields || []).map(root.fieldLabel).join(", "))
            color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
        }
        Text {
            objectName: "addonSessionBackground"; Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
            text: root.review.continuesAfterViewClose === true ? qsTr("This session continues when this panel closes. Use End session to stop it.") : qsTr("This session ends when its view closes.")
            color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
        }
        Text { Layout.fillWidth: true; text: qsTr("Observation only is the default. Select any playback controls that you want to permit."); textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.foreground; font.pixelSize: root.theme.dp(13) }
        Repeater {
            model: root.review.operations || []
            RedesignButton {
                required property string modelData
                objectName: "addonSessionAllow_" + modelData
                checkable: true; checked: root.selectedOperations.includes(modelData); selected: checked
                caption: qsTr("Allow %1").arg(root.operationLabel(modelData)); text: caption; tooltipTextFormat: Text.PlainText
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: { root.toggleOperation(modelData, checked); checked = Qt.binding(() => root.selectedOperations.includes(modelData)) }
            }
        }
        RowLayout {
            RedesignButton {
                objectName: "addonSessionAccept"
                caption: root.selectedOperations.length ? qsTr("Join with selected controls") : qsTr("Join with observation only"); text: caption; tooltipTextFormat: Text.PlainText
                enabled: !root.busy && root.displayCurrent && root.controllerReady; scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.backend.accept(root.selectedOperations)
            }
            RedesignButton {
                objectName: "addonSessionCancel"
                caption: qsTr("Cancel review"); text: caption; tooltipTextFormat: Text.PlainText
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.backend.cancelReview()
            }
        }
    }
    Repeater {
        model: root.backend ? root.backend.sessions || [] : []
        ColumnLayout {
            id: row
            required property var modelData
            Layout.fillWidth: true
            Text { objectName: "addonSessionActiveLabel"; Layout.fillWidth: true; text: (row.modelData.name || "") + " — " + (row.modelData.room || ""); textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.settingsHeading; font.pixelSize: root.theme.dp(15) }
            Text { Layout.fillWidth: true; text: row.modelData.starting === true ? qsTr("The app checks playback before the session starts.") : row.modelData.suspended === true ? qsTr("Local playback control has priority. Use Resume to permit session control again.") : qsTr("The session is active."); textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.foreground; font.pixelSize: root.theme.dp(13) }
            RowLayout {
                RedesignButton {
                    objectName: "addonSessionResume_" + row.modelData.id
                    visible: row.modelData.suspended === true
                    enabled: row.modelData.connected === true && row.modelData.starting !== true
                    caption: qsTr("Resume session control"); text: caption; tooltipTextFormat: Text.PlainText
                    scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                    onClicked: root.backend.resume(row.modelData.id)
                }
                RedesignButton {
                    objectName: "addonSessionEnd_" + row.modelData.id
                    caption: qsTr("End session"); text: caption; tooltipTextFormat: Text.PlainText
                    scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                    onClicked: root.backend.end(row.modelData.id)
                }
            }
        }
    }
    Text { objectName: "addonSessionStatus"; Layout.fillWidth: true; text: root.backend ? root.backend.status || (root.backend.preparing ? qsTr("The app prepares the review.") : "") : ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; visible: text.length > 0; color: root.theme.foreground; font.pixelSize: root.theme.dp(13) }
    Text { objectName: "addonSessionError"; Layout.fillWidth: true; text: root.backend ? root.backend.errorMessage || "" : ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; visible: text.length > 0; color: root.theme.danger; font.pixelSize: root.theme.dp(13); Accessible.role: Accessible.AlertMessage }
    component SessionInput: C.TextField {
        color: root.theme.foreground; placeholderTextColor: root.theme.foreground; font.pixelSize: root.theme.dp(14)
        leftPadding: root.theme.dp(10); rightPadding: root.theme.dp(10); implicitHeight: root.theme.dp(38)
        background: Rectangle { color: root.theme.field; radius: root.theme.dp(root.theme.controlRadius); border.color: parent.activeFocus ? root.theme.accent : root.theme.border }
    }
}

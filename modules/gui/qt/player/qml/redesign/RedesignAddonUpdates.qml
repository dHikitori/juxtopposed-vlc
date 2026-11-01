// SPDX-License-Identifier: GPL-2.0-or-later
// The startup prompt for add-on updates that come with the app. The startup
// package check lists them (AddonManager.appUpdates); this dialog shows each
// version change and any new permissions, and installs an update through the
// same review path as Extensions: stage the app's copy, then approve it.
// An update that asks for new permissions needs its own click.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

C.Dialog {
    id: root
    popupType: C.Popup.Item
    property var backend: null
    property var ctx: null
    // The prompt waits while another first-run window is up.
    property bool blocked: false
    property real scaleFactor: 1
    property bool lightTheme: false
    property color accentColor: "#d37918"
    property Component videoBackdropContent
    readonly property string dismissedKey: "Redesign/AddonUpdatesDismissed"
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    readonly property real pixelRatio: parent ? parent.Screen.devicePixelRatio : 1
    // The rows shown, copied when the prompt opens; an approved update stays
    // in the list as "Updated".
    property var rows: []
    // id -> {state: "waiting" | "working" | "done" | "failed", error}
    property var progress: ({})
    property var queue: []
    property string current: ""
    property bool approving: false
    property string approval: ""
    property bool shownThisRun: false
    readonly property bool working: current.length > 0 || queue.length > 0
    readonly property var simple: rows.filter(row => !needsApproval(row) && stateOf(row.id) !== "done")
    readonly property bool finished: rows.length > 0 && rows.every(row => stateOf(row.id) === "done")
    readonly property bool approvalLeft: rows.some(row => needsApproval(row) && stateOf(row.id) !== "done")
    title: qsTr("Add-on updates")
    x: parent ? Math.round((parent.width - width) * pixelRatio / 2) / pixelRatio : 0
    y: parent ? Math.round((parent.height - height) * pixelRatio / 2) / pixelRatio : 0
    width: Math.max(0, Math.min(theme.dp(520), parent ? parent.width - theme.dp(32) : theme.dp(520)))
    height: Math.min(implicitHeight, parent ? Math.max(0, parent.height - theme.dp(32)) : implicitHeight)
    padding: theme.dp(24)
    modal: true; dim: true; focus: true
    closePolicy: C.Popup.CloseOnEscape
    header: null; footer: null
    background: RedesignDialogSurface { theme: root.theme; videoBackdropContent: root.videoBackdropContent }
    C.Overlay.modal: Rectangle { color: "#80000000" }

    function needsApproval(row) { return Object.keys(row.newPermissions || {}).length > 0 }
    function stateOf(id) { return (progress[id] || {}).state || "waiting" }
    function setState(id, state, error) {
        const next = Object.assign({}, progress); next[id] = {state: state, error: error || ""}; progress = next
    }
    function dismissed() {
        try { return root.ctx ? JSON.parse(root.ctx.settingValue(root.dismissedKey, "{}")) || {} : {} } catch (error) { return {} }
    }
    // Updates the user has not put off for this version.
    function pending() {
        const updates = root.backend && root.backend.appUpdates ? Array.from(root.backend.appUpdates) : []
        const later = dismissed()
        return updates.filter(update => later[update.id] !== update.version)
    }
    function maybeOpen() {
        if (opened || shownThisRun || blocked || !backend) return
        const updates = pending()
        if (!updates.length) return
        rows = updates.map(update => JSON.parse(JSON.stringify(update))); progress = ({}); queue = []; current = ""
        shownThisRun = true
        open()
    }
    // "Later": do not ask again for these versions; a newer one asks again.
    function putOff() {
        if (!root.ctx) return
        const later = dismissed()
        for (const row of rows) if (stateOf(row.id) !== "done") later[row.id] = row.version
        root.ctx.setSettingValue(root.dismissedKey, JSON.stringify(later))
    }
    function update(ids) {
        queue = queue.concat(ids.filter(id => !queue.includes(id) && id !== current && stateOf(id) !== "done"))
        for (const id of ids) if (stateOf(id) !== "done") setState(id, "working")
        step()
    }
    function step() {
        if (current.length || !queue.length || !backend) return
        const id = queue[0]; queue = queue.slice(1); current = id
        if (!backend.stageDistributionPackage(id)) finish(id, false, backend.errorMessage)
        else settle()
    }
    function finish(id, ok, error) {
        setState(id, ok ? "done" : "failed", ok ? "" : error || qsTr("The update did not install."))
        if (current === id) current = ""
        Qt.callLater(step)
    }
    // The staged copy is ready: install it with the add-on's on/off state.
    function settle() {
        if (!current.length || approving || !backend || backend.busy) return
        const pendingPackage = backend.pendingPackage || {}
        if (pendingPackage.id !== current) { finish(current, false, backend.errorMessage); return }
        const row = rows.find(entry => entry.id === current)
        approving = true
        // The install runs on background work; approved has the result.
        approval = backend.requestApprovePending(!!row && row.enabled === true)
        if (!approval.length) { approving = false; finish(current, false, backend.errorMessage) }
    }
    function approved(request, ok, error) {
        if (!request.length || request !== approval) return
        approval = ""; approving = false
        finish(current, ok, error)
    }
    Connections {
        target: root.backend
        ignoreUnknownSignals: true
        function onRevisionChanged() { root.settle() }
        function onWriteFinished(request, success, error) { root.approved(request, success, error) }
        function onAppUpdatesChanged() { root.maybeOpen() }
    }
    onBlockedChanged: maybeOpen()
    onBackendChanged: maybeOpen()
    Component.onCompleted: maybeOpen()
    onRejected: if (!finished) putOff()
    onOpened: (simple.length ? updateAll : later).forceActiveFocus()

    contentItem: ColumnLayout {
        Accessible.role: Accessible.Dialog
        Accessible.name: root.title
        spacing: root.theme.dp(16)
        Text {
            Layout.fillWidth: true
            text: root.title; textFormat: Text.PlainText
            color: root.theme.bright; font.pixelSize: root.theme.dp(20); wrapMode: Text.Wrap
        }
        Text {
            Layout.fillWidth: true
            text: root.finished ? qsTr("Your add-ons are up to date.")
                : qsTr("New versions of add-ons that come with the app are ready to install.")
            textFormat: Text.PlainText; color: root.theme.foreground; font.pixelSize: root.theme.dp(14); wrapMode: Text.Wrap
        }
        C.ScrollView {
            id: scroll
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 0
            implicitHeight: list.implicitHeight
            contentWidth: availableWidth; clip: true
            C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
            Column {
                id: list
                width: scroll.availableWidth
                Repeater {
                    model: root.rows
                    Rectangle {
                        id: entry
                        required property var modelData
                        required property int index
                        readonly property string phase: root.stateOf(modelData.id)
                        readonly property bool approval: root.needsApproval(modelData)
                        objectName: "addonUpdate_" + modelData.id
                        width: list.width; height: content.implicitHeight + root.theme.dp(24)
                        color: "transparent"
                        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: root.theme.border; visible: entry.index > 0 }
                        RowLayout {
                            id: content
                            anchors.left: parent.left; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                            spacing: root.theme.dp(12)
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: root.theme.dp(4)
                                Text {
                                    Layout.fillWidth: true
                                    text: entry.modelData.name || entry.modelData.id; textFormat: Text.PlainText
                                    color: root.theme.bright; font.pixelSize: root.theme.dp(14); elide: Text.ElideRight
                                }
                                Text {
                                    objectName: "addonUpdateVersions_" + entry.modelData.id
                                    Layout.fillWidth: true
                                    text: qsTr("%1 → %2").arg(entry.modelData.installedVersion).arg(entry.modelData.version)
                                    textFormat: Text.PlainText; color: root.theme.playerLabel; font.pixelSize: root.theme.dp(12)
                                }
                                Text {
                                    objectName: "addonUpdatePermissions_" + entry.modelData.id
                                    Layout.fillWidth: true; Layout.topMargin: root.theme.dp(4)
                                    visible: entry.approval && entry.phase !== "done"
                                    text: qsTr("This version asks for new permissions:") + "\n"
                                        + permissionText.summaries(entry.modelData.newPermissions).split("\n\n").map(line => "• " + line).join("\n")
                                    textFormat: Text.PlainText; color: root.theme.foreground; font.pixelSize: root.theme.dp(12); wrapMode: Text.Wrap
                                }
                                Text {
                                    objectName: "addonUpdateStatus_" + entry.modelData.id
                                    Layout.fillWidth: true
                                    visible: text.length > 0
                                    text: entry.phase === "done" ? qsTr("Updated") : entry.phase === "working" ? qsTr("Updating…")
                                        : entry.phase === "failed" ? (root.progress[entry.modelData.id] || {}).error || "" : ""
                                    textFormat: Text.PlainText; wrapMode: Text.Wrap; font.pixelSize: root.theme.dp(12)
                                    color: entry.phase === "failed" ? root.theme.danger : entry.phase === "done" ? root.theme.accent : root.theme.playerLabel
                                    Accessible.role: entry.phase === "failed" ? Accessible.AlertMessage : Accessible.StaticText
                                }
                            }
                            RedesignButton {
                                objectName: "addonUpdateApply_" + entry.modelData.id
                                Layout.alignment: Qt.AlignTop
                                visible: entry.phase === "waiting" || entry.phase === "failed"
                                enabled: !root.working
                                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                                caption: entry.approval ? qsTr("Approve and update") : qsTr("Update"); text: caption
                                primaryAction: entry.approval
                                onClicked: root.update([entry.modelData.id])
                            }
                        }
                    }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            visible: !root.finished && root.approvalLeft
            text: qsTr("Updates that ask for new permissions need their own approval.")
            textFormat: Text.PlainText; color: root.theme.playerLabel; font.pixelSize: root.theme.dp(12); wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: root.theme.dp(8)
            RedesignButton {
                id: later
                objectName: "addonUpdatesLater"
                visible: !root.finished
                enabled: !root.working
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                caption: qsTr("Later"); text: caption
                onClicked: root.reject()
            }
            RedesignButton {
                id: updateAll
                objectName: "addonUpdatesAll"
                visible: !root.finished && root.simple.length > 0
                enabled: !root.working
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                caption: root.approvalLeft ? qsTr("Update the others") : qsTr("Update all"); text: caption
                primaryAction: true
                onClicked: root.update(root.simple.map(row => row.id))
            }
            RedesignButton {
                objectName: "addonUpdatesDone"
                visible: root.finished
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                caption: qsTr("Done"); text: caption; primaryAction: true
                onClicked: root.accept()
                onVisibleChanged: if (visible) forceActiveFocus()
            }
        }
    }
    RedesignAddonPermissions { id: permissionText }
}

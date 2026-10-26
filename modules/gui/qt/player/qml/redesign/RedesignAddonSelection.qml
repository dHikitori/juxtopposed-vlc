// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C
import "AddonSelectionContracts.js" as Selections

ColumnLayout {
    id: root
    required property var registry
    required property string hook
    required property string kind
    required property string choiceSet
    property var targets: []
    property var assets: []
    property bool active: true
    property bool busy: false
    property bool showActions: true
    property bool showRecovery: true
    property real maximumContentHeight: theme.dp(360)
    property Component footerContent
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    property string errorMessage: ""
    property string consumedChoiceSet: ""
    property bool dismissed: false
    property int generation: 0
    property bool stale: true
    property var snapshotData: null
    readonly property var replacement: {
        if (!registry) return ({})
        registry.revision
        return registry.replacement(hook)
    }
    readonly property bool scopeAllowed: {
        if (!registry || !replacement.id || typeof registry.canUseSelection !== "function"
            || typeof registry.validateSessionResult !== "function") return false
        registry.revision
        return registry.canUseSelection(replacement.id, hook)
    }
    readonly property bool targetTitlesAllowed: kind === "playlist" || replacement.selectionTitlesAllowed === true
    readonly property bool prepared: active && !dismissed && !stale && scopeAllowed && !!snapshotData && mount.usable
        && !!mount.descriptor.state && (mount.descriptor.state.parameters || {}).choiceSet === snapshotData.choiceSet
    readonly property bool usingReplacement: visible && prepared
    readonly property var preview: {
        if (!usingReplacement || !snapshotData || !mount.descriptor.state) return null
        const result = mount.descriptor.state.result || ({})
        return kind === "playlist" ? Selections.playlistResult(snapshotData, result) : Selections.subtitleResult(snapshotData, result)
    }
    readonly property string selectionSummary: {
        if (!preview || !snapshotData) return ""
        if (kind === "playlist") {
            if (preview.name.length) return qsTr("New playlist: %1").arg(preview.name)
            const choice = snapshotData.publicData.targets.find(row => snapshotData.targetBindings[row.ref].key === preview.key)
            return choice ? qsTr("Selected playlist: %1").arg(choice.title) : ""
        }
        const review = preview.slice(0, 2).map(row => Object.values(snapshotData.targetBindings).find(binding => binding.key === row.targetKey).reviewTitle).join(", ")
        return qsTr("Selected media: %1. Subtitle files: %2.").arg(preview.length).arg(preview.reduce((count, row) => count + row.assetKeys.length, 0))
            + (review.length ? "\n" + review : "")
    }
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme }
    signal confirmed(var selection)
    signal canceled()
    signal fallbackRequested()
    spacing: theme.dp(10)

    function invalidate() { stale = true; refreshTimer.restart() }
    function begin() { dismissed = false; errorMessage = ""; consumedChoiceSet = ""; invalidate() }
    function confirm() {
        if (!usingReplacement || busy || (snapshotData && consumedChoiceSet === snapshotData.choiceSet)
            || !snapshotData || mount.parameters.choiceSet !== snapshotData.choiceSet
            || (mount.descriptor.state.parameters || {}).choiceSet !== snapshotData.choiceSet
            || typeof registry.validateSessionResult !== "function") return false
        const draft = mount.descriptor.state.result || ({})
        const schemaValid = registry.validateSessionResult(mount.sessionId, draft)
        const selection = !schemaValid ? null : kind === "playlist" ? Selections.playlistResult(snapshotData, draft) : Selections.subtitleResult(snapshotData, draft)
        if (!selection) {
            errorMessage = qsTr("The add-on selection is no longer correct. Use the built-in view.")
            return false
        }
        consumedChoiceSet = snapshotData.choiceSet
        confirmed(selection)
        return true
    }
    function cancel() {
        if (busy || dismissed) return
        dismissed = true; mount.release(); canceled()
    }
    function useBuiltin() {
        if (busy) return
        dismissed = true; mount.release()
        if (registry) registry.selectReplacement(hook, "")
        fallbackRequested()
    }
    onChoiceSetChanged: { dismissed = false; errorMessage = ""; consumedChoiceSet = ""; invalidate() }
    onTargetsChanged: invalidate()
    onAssetsChanged: invalidate()
    onKindChanged: invalidate()
    onHookChanged: invalidate()
    onTargetTitlesAllowedChanged: invalidate()
    onScopeAllowedChanged: fallbackTimer.restart()
    onSnapshotDataChanged: fallbackTimer.restart()
    onActiveChanged: { if (!active) mount.release(); else begin() }
    Component.onCompleted: invalidate()
    Timer {
        id: refreshTimer
        interval: 0
        onTriggered: {
            ++root.generation
            const targets = root.targets.map((row, index) => ({key: row.key, title: root.targetTitlesAllowed ? row.title : qsTr("Media %1").arg(index + 1),
                reviewTitle: row.title, enabled: row.enabled !== false}))
            root.snapshotData = Selections.snapshot(root.kind, root.choiceSet + ":" + root.generation, targets, root.assets)
            root.stale = false
            fallbackTimer.restart()
        }
    }
    Timer {
        id: fallbackTimer
        interval: 0
        onTriggered: if (root.active && !root.dismissed && !root.stale && (!root.snapshotData || !root.scopeAllowed)) root.fallbackRequested()
    }
    RedesignButton {
        objectName: "addonSelectionRecovery"
        visible: root.showRecovery && !!root.replacement.id
        caption: qsTr("Use built-in view"); text: caption; tooltipTextFormat: Text.PlainText
        enabled: !root.busy
        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
        onClicked: root.useBuiltin()
    }
    C.ScrollView {
        objectName: "addonSelectionBody"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 0
        Layout.maximumHeight: root.maximumContentHeight
        Layout.preferredHeight: Math.min(root.maximumContentHeight, bodyContents.implicitHeight)
        enabled: !root.busy
        clip: true; contentWidth: availableWidth
        C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
        Column {
          id: bodyContents
          width: parent.width
          RedesignAddonMount {
            id: mount
            objectName: "addonSelectionMount"
            width: parent.width; height: implicitHeight
            visible: root.active && !root.dismissed
            registry: root.registry
            contributionId: root.active && !root.dismissed && root.scopeAllowed && root.snapshotData ? root.replacement.id : ""
            parameters: root.snapshotData ? root.snapshotData.publicData : ({})
            hostContext: ({surface: root.hook})
            scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
            onFailed: root.fallbackRequested()
          }
          Loader { width: parent.width; sourceComponent: root.footerContent }
        }
    }
    Text {
        Layout.fillWidth: true
        visible: text.length > 0
        text: root.errorMessage; textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: root.theme.danger; font.pixelSize: root.theme.dp(13)
        Accessible.role: Accessible.AlertMessage
    }
    Text {
        objectName: "addonSelectionSummary"
        Layout.fillWidth: true
        visible: text.length > 0
        text: root.selectionSummary; textFormat: Text.PlainText; wrapMode: Text.Wrap
        maximumLineCount: 3; elide: Text.ElideRight
        color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
    }
    RowLayout {
        Layout.fillWidth: true
        visible: root.showActions
        RedesignButton {
            objectName: "addonSelectionCancel"
            caption: qsTr("Cancel"); text: caption; tooltipTextFormat: Text.PlainText
            enabled: !root.busy
            scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
            onClicked: root.cancel()
        }
        Item { Layout.fillWidth: true }
        RedesignButton {
            objectName: "addonSelectionConfirm"
            caption: qsTr("Confirm"); text: caption; tooltipTextFormat: Text.PlainText
            enabled: root.usingReplacement && !!root.preview && !root.busy && root.snapshotData && root.consumedChoiceSet !== root.snapshotData.choiceSet
            primaryAction: true
            scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
            onClicked: root.confirm()
        }
    }
}

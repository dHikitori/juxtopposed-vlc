// SPDX-License-Identifier: GPL-2.0-or-later
// Browse in Extensions: add-ons from the hosted registry (AddonRegistry).
// Install opens the same review as Add extension: the app fetches the
// release from its Git repository, checks it against the registry, and
// shows its permissions before anything is installed.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

ColumnLayout {
    id: root
    property var registry: null
    // The Installed list's rows, for the Installed and Update states.
    property var installedPackages: []
    property bool busy: false
    property bool pendingReview: false
    property bool active: true
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme }
    readonly property bool available: !!registry && registry.enabled === true && !registry.addressError
    readonly property var current: registry ? registry.current || ({}) : ({})
    readonly property var currentRelease: current.latestRelease || ({})
    readonly property var types: [
        {key: "", label: qsTr("All")}, {key: "metadata.provider", label: qsTr("Metadata")},
        {key: "discovery.source", label: qsTr("Media sources")}, {key: "ui.panel", label: qsTr("Player panels")},
        {key: "theme", label: qsTr("Themes")}, {key: "visualizer", label: qsTr("Visualizers")}]
    property string type: ""
    spacing: theme.dp(10)

    RedesignAddonPermissions { id: permissionText }

    function start() {
        if (!registry) return
        registry.refresh(false)
        if (available) registry.search(query.text, type)
    }
    onActiveChanged: if (active) start()
    Component.onCompleted: if (active) start()
    Connections {
        target: root.registry
        function onSettingsChanged() { if (root.active && root.available) root.registry.search(query.text, root.type) }
    }
    Timer { id: debounce; interval: 300; onTriggered: if (root.available) root.registry.search(query.text, root.type) }

    function installedRow(id) { return installedPackages.find(row => row.id === id) || null }
    function stateText(item) {
        const row = installedRow(item.id)
        if (!row) return item.compatible === false ? qsTr("Needs a newer app") : ""
        if (registry) registry.indexRevision
        const update = registry ? registry.updateFor(row.id, row.version || "", row.source || "") : ({})
        return update.version ? qsTr("Update to %1").arg(update.version) : qsTr("Installed")
    }
    // The installed copy of the add-on shown in the details, if any.
    readonly property var currentInstalled: current.id ? installedRow(current.id) : null
    readonly property var currentUpdate: {
        if (!currentInstalled || !registry) return ({})
        registry.indexRevision
        return registry.updateFor(currentInstalled.id, currentInstalled.version || "", currentInstalled.source || "")
    }
    function contributionText(release) {
        const names = {"metadata.provider": qsTr("Metadata"), "discovery.source": qsTr("Media source"), "ui.panel": qsTr("Player panel"),
            "ui.command": qsTr("Command"), "ui.route": qsTr("View"), "ui.modal": qsTr("Dialog"), "ui.section": qsTr("Settings section"),
            "ui.replacement": qsTr("Replacement view"), theme: qsTr("Theme"), visualizer: qsTr("Visualizer"), "session.controller": qsTr("Session")}
        return (release.contributions || []).map(item => (names[item.type] || item.type) + (item.name ? ": " + item.name : "")).join("\n")
    }

    Text {
        objectName: "addonBrowseState"
        Layout.fillWidth: true
        visible: text.length > 0
        text: !root.registry ? qsTr("The add-on registry is not available in this app.")
            : root.registry.enabled !== true ? qsTr("The add-on registry is turned off. Turn it on in Advanced Options to browse add-ons.")
            : root.registry.addressError || ""
        textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: root.registry && root.registry.addressError ? root.theme.danger : root.theme.foreground; font.pixelSize: root.theme.dp(13)
    }
    Text {
        objectName: "addonBrowseAddress"
        Layout.fillWidth: true
        visible: root.available && root.registry.defaultAddress !== true
        text: root.registry ? qsTr("Registry: %1").arg(root.registry.address) : ""
        textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; color: root.theme.playerLabel; font.pixelSize: root.theme.dp(12)
    }
    BrowseInput {
        id: query
        objectName: "addonBrowseSearch"
        visible: root.available
        placeholderText: qsTr("Search add-ons"); Accessible.name: placeholderText
        maximumLength: 100
        onTextChanged: debounce.restart()
        Keys.onDownPressed: { results.forceActiveFocus(); if (results.count > 0) results.currentIndex = 0 }
    }
    Flow {
        Layout.fillWidth: true; visible: root.available; spacing: root.theme.dp(6)
        Repeater {
            model: root.types
            delegate: ActionButton {
                required property var modelData
                objectName: "addonBrowseType_" + (modelData.key || "all")
                caption: modelData.label; selected: root.type === modelData.key
                onClicked: { root.type = modelData.key; debounce.stop(); root.registry.search(query.text, root.type) }
            }
        }
    }
    Rectangle {
        objectName: "addonBrowseList"
        Layout.fillWidth: true; Layout.preferredHeight: root.theme.dp(252); visible: root.available
        color: root.theme.field; radius: root.theme.dp(6); border.width: 1; border.color: root.theme.border
        ListView {
            id: results
            anchors.fill: parent; anchors.margins: 1; clip: true
            model: root.registry ? root.registry.results : []
            currentIndex: -1; boundsBehavior: Flickable.StopAtBounds; keyNavigationEnabled: true
            C.ScrollBar.vertical: C.ScrollBar { policy: C.ScrollBar.AsNeeded }
            onAtYEndChanged: if (atYEnd && count > 0 && root.registry.hasMore) root.registry.loadMore()
            Keys.onReturnPressed: if (currentItem) root.registry.open(currentItem.modelData.id)
            Keys.onEnterPressed: if (currentItem) root.registry.open(currentItem.modelData.id)
            delegate: C.ItemDelegate {
                id: resultRow
                required property var modelData
                required property int index
                objectName: "addonBrowseResult_" + modelData.id
                width: ListView.view.width; height: root.theme.dp(62)
                highlighted: root.current.id === modelData.id
                hoverEnabled: true; focusPolicy: Qt.StrongFocus
                leftPadding: root.theme.dp(16); rightPadding: root.theme.dp(12); topPadding: 0; bottomPadding: 0
                Accessible.name: [modelData.name, modelData.publisher, root.stateText(modelData)].filter(value => !!value).join(", ")
                onClicked: { results.currentIndex = index; root.registry.open(modelData.id) }
                background: Rectangle {
                    color: resultRow.highlighted ? root.theme.selected : resultRow.hovered ? root.theme.hover : "transparent"
                    Rectangle { anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: root.theme.dp(3); color: root.theme.accent; visible: resultRow.activeFocus }
                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: root.theme.border; visible: resultRow.index < results.count - 1 }
                }
                contentItem: RowLayout {
                    spacing: root.theme.dp(12)
                    RedesignIcon {
                        name: "nav-plugins"; color: resultRow.highlighted ? root.theme.bright : root.theme.foreground
                        Layout.preferredWidth: root.theme.dp(24); Layout.preferredHeight: root.theme.dp(24)
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: root.theme.dp(3)
                        Text {
                            Layout.fillWidth: true
                            text: resultRow.modelData.name || resultRow.modelData.id; textFormat: Text.PlainText
                            color: root.theme.bright; font.pixelSize: root.theme.dp(14); elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: [resultRow.modelData.publisher, resultRow.modelData.summary].filter(value => !!value).join(" · ")
                            textFormat: Text.PlainText; color: root.theme.foreground; font.pixelSize: root.theme.dp(12); elide: Text.ElideRight
                        }
                    }
                    Text {
                        objectName: "addonBrowseResultState_" + resultRow.modelData.id
                        text: root.stateText(resultRow.modelData); visible: text.length > 0
                        textFormat: Text.PlainText; color: root.theme.accent; font.pixelSize: root.theme.dp(12)
                    }
                }
            }
            ColumnLayout {
                anchors.centerIn: parent; width: Math.max(0, parent.width - root.theme.dp(32)); spacing: root.theme.dp(8)
                visible: results.count === 0
                C.BusyIndicator { Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: root.theme.dp(24); Layout.preferredHeight: root.theme.dp(24); running: !!root.registry && root.registry.searching; visible: running }
                Text {
                    objectName: "addonBrowseListState"
                    Layout.fillWidth: true
                    text: !root.registry ? "" : root.registry.searching ? qsTr("The app asks the add-on registry…")
                        : root.registry.searchError ? root.registry.searchError
                        : query.text.trim() || root.type ? qsTr("No add-ons match your search.") : qsTr("The registry lists no add-ons yet.")
                    textFormat: Text.PlainText; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter
                    color: root.registry && root.registry.searchError ? root.theme.danger : root.theme.foreground; font.pixelSize: root.theme.dp(14)
                    Accessible.role: root.registry && root.registry.searchError ? Accessible.AlertMessage : Accessible.StaticText
                }
                ActionButton {
                    objectName: "addonBrowseRetry"; Layout.alignment: Qt.AlignHCenter
                    caption: qsTr("Try again"); visible: !!root.registry && !root.registry.searching && !!root.registry.searchError
                    onClicked: { root.registry.refresh(true); root.registry.search(query.text, root.type) }
                }
            }
        }
    }
    Rectangle {
        objectName: "addonBrowseDetails"
        Layout.fillWidth: true
        visible: root.available && (!!root.current.id || root.registry.currentLoading || !!root.registry.currentError)
        implicitHeight: details.implicitHeight + root.theme.dp(24)
        color: root.theme.panel; radius: root.theme.dp(6); border.width: 1; border.color: root.theme.border
        ColumnLayout {
            id: details
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: root.theme.dp(12)
            spacing: root.theme.dp(8)
            RowLayout {
                Layout.fillWidth: true; visible: !!root.registry && root.registry.currentLoading
                C.BusyIndicator { running: parent.visible; Layout.preferredWidth: root.theme.dp(20); Layout.preferredHeight: root.theme.dp(20) }
                Text { Layout.fillWidth: true; text: qsTr("The app reads the add-on details…"); textFormat: Text.PlainText; color: root.theme.foreground; font.pixelSize: root.theme.dp(13) }
            }
            Text {
                objectName: "addonBrowseDetailsError"
                Layout.fillWidth: true; visible: text.length > 0
                text: root.registry ? root.registry.currentError || "" : ""
                textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.danger; font.pixelSize: root.theme.dp(13)
                Accessible.role: Accessible.AlertMessage
            }
            Text {
                Layout.fillWidth: true; visible: !!root.current.id
                text: root.current.name || ""; textFormat: Text.PlainText; elide: Text.ElideRight; color: root.theme.bright; font.pixelSize: root.theme.dp(16)
            }
            Text {
                objectName: "addonBrowseOverview"
                Layout.fillWidth: true; visible: !!root.current.id
                text: [root.current.publisher ? qsTr("By %1").arg(root.current.publisher) : "",
                    root.currentRelease.version ? qsTr("Version %1").arg(root.currentRelease.version) : qsTr("No installable version"),
                    root.current.license || ""].filter(value => value.length > 0).join(" · ")
                textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.foreground; font.pixelSize: root.theme.dp(12)
            }
            Text {
                Layout.fillWidth: true; visible: text.length > 0
                // Plain text only: the registry's Markdown is not rendered here.
                text: root.current.summary || ""
                textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
            }
            Text {
                objectName: "addonBrowsePermissions"
                Layout.fillWidth: true; visible: !!root.currentRelease.version
                text: qsTr("Asks for:") + "\n" + (permissionText.summaries(root.currentRelease.permissions || {}) || qsTr("No permissions"))
                textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
            }
            Text {
                Layout.fillWidth: true; visible: text.length > 0
                text: root.contributionText(root.currentRelease)
                textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.foreground; font.pixelSize: root.theme.dp(12)
            }
            Text {
                Layout.fillWidth: true; visible: !!root.currentRelease.version
                text: qsTr("The app shows the permissions again after it fetches the release from its repository, before anything is installed.")
                textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.playerLabel; font.pixelSize: root.theme.dp(12)
            }
            Flow {
                Layout.fillWidth: true; spacing: root.theme.dp(6); visible: !!root.current.id
                ActionButton {
                    objectName: "addonBrowseInstall"
                    readonly property bool installed: !!root.currentInstalled
                    caption: root.currentUpdate.version ? qsTr("Update to %1").arg(root.currentUpdate.version)
                        : installed ? qsTr("Installed") : qsTr("Install")
                    primaryAction: true
                    enabled: !root.busy && !root.pendingReview && !!root.currentRelease.version && root.currentRelease.compatible !== false
                        && (!installed || !!root.currentUpdate.version)
                    onClicked: root.currentUpdate.version
                        ? root.registry.installUpdate(root.currentInstalled.id, root.currentInstalled.version || "", root.currentInstalled.source || "")
                        : root.registry.install(root.current.id, root.currentRelease.version)
                }
                ActionButton {
                    objectName: "addonBrowseWebPage"
                    caption: qsTr("Open add-on page"); visible: !!root.current.webUrl
                    onClicked: Qt.openUrlExternally(root.current.webUrl)
                }
            }
            Text {
                Layout.fillWidth: true; visible: root.currentRelease.compatible === false
                text: qsTr("This version needs a newer version of the app.")
                textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.foreground; font.pixelSize: root.theme.dp(12)
            }
        }
    }

    component ActionButton: RedesignButton {
        captionMaximumWidth: Math.max(0, root.width - root.theme.dp(48))
        text: caption; captionTextFormat: Text.PlainText; tooltipTextFormat: Text.PlainText
        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
    }
    component BrowseInput: C.TextField {
        Layout.fillWidth: true; implicitHeight: root.theme.dp(36)
        color: root.theme.foreground; placeholderTextColor: root.theme.foreground
        font.pixelSize: root.theme.dp(14); selectByMouse: true
        background: Rectangle { color: root.theme.field; radius: root.theme.dp(6); border.width: 1; border.color: parent.activeFocus ? root.theme.accent : root.theme.border }
    }
}

// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

Item {
    id: root
    required property var backend
    property real scaleFactor: 1
    property bool lightTheme: false
    property color accentColor: "#d37918"
    property Component videoBackdropContent
    property bool selectorVisible: true
    property bool applicationProfile: false
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    readonly property var presets: { backend.revision; return backend.playerPresets }
    readonly property string selectedId: { backend.revision; return backend.selectedPlayerPreset }
    readonly property string selectedName: {
        const preset = presets.find(row => row.id === selectedId)
        return preset ? preset.name : root.applicationProfile ? qsTr("Default Profile") : qsTr("Default Preset")
    }
    property string contextId
    property string contextName
    property string nameAction
    property bool retryNameSave: false
    readonly property var shortcutContext: root.backend.ctx === undefined ? null : root.backend.ctx
    readonly property var controlWindow: root.Window.window
    readonly property var hostWindow: controlWindow && controlWindow.renderWindow !== undefined ? controlWindow.renderWindow : controlWindow
    Binding {
        target: root.shortcutContext; property: "useGlobalShortcuts"; value: false
        when: !!root.shortcutContext && root.visible && root.hostWindow && root.hostWindow.visible
            && (dropdown.visible || actions.visible || nameDialog.visible || deleteDialog.visible)
        restoreMode: Binding.RestoreBindingOrValue
    }
    implicitHeight: selectorVisible ? theme.dp(42) : 0

    function namePreset(action, id, name) {
        nameAction = action; contextId = id || ""; contextName = name || ""
        retryNameSave = false
        presetName.text = action === "rename" ? contextName : ""
        dropdown.close()
        nameDialog.open()
    }
    function openProfileActions(id, name, x, y) {
        contextId = id; contextName = name
        actions.x = Math.max(theme.dp(8), Math.min(x, C.Overlay.overlay.width - actions.width - theme.dp(8)))
        actions.y = Math.max(theme.dp(8), Math.min(y, C.Overlay.overlay.height - actions.implicitHeight - theme.dp(8)))
        actions.open()
    }
    function retrySelection(id) {
        nameAction = "select"; contextId = id
        retryNameSave = backend.pendingPlayerPreset
        nameDialog.open()
    }
    function saveName() {
        let success
        if (nameAction === "select") success = retryNameSave ? backend.save() : backend.selectPlayerPreset(contextId)
        else if (retryNameSave) success = backend.save()
        else if (nameAction === "delete") success = backend.deletePlayerPreset(contextId)
        else if (nameAction === "rename") success = backend.renamePlayerPreset(contextId, presetName.text)
        else if (nameAction === "duplicate") success = backend.duplicatePlayerPreset(contextId, presetName.text)
        else success = backend.createPlayerPreset(presetName.text)
        retryNameSave = !success && backend.pendingPlayerPreset
        if (success) { retryNameSave = false; nameDialog.close() }
    }
    function activate(row) {
        if (row.action === "create") namePreset("create", "", "")
        else { dropdown.close(); backend.selectPlayerPreset(row.id) }
    }

    RedesignButton {
        id: field
        objectName: "playerPresetSelector"
        visible: root.selectorVisible
        anchors.fill: parent
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        text: root.selectedName
        enabled: !root.backend.pendingPlayerPreset
        background: Rectangle { color: root.theme.field; radius: root.theme.dp(6); border.width: field.visualFocus ? 1 : 0; border.color: root.theme.accent }
        contentItem: RowLayout {
            anchors.fill: parent; anchors.leftMargin: root.theme.dp(16); anchors.rightMargin: root.theme.dp(16)
            Text { Layout.fillWidth: true; text: root.selectedName; color: root.theme.foreground; font.pixelSize: root.theme.dp(14); elide: Text.ElideRight }
            RedesignIcon { Layout.preferredWidth: root.theme.dp(24); Layout.preferredHeight: root.theme.dp(24); name: dropdown.visible ? "chevron-down" : "chevron-up"; color: root.theme.foreground }
        }
        onClicked: dropdown.visible ? dropdown.close() : dropdown.open()
    }
    C.Popup {
        id: dropdown
        objectName: "playerPresetMenu"
        y: -height - root.theme.dp(8)
        width: root.width
        height: Math.min(list.count * root.theme.dp(root.theme.compactMenus ? 28 : 40) + padding * 2, root.theme.dp(340), root.mapToItem(C.Overlay.overlay, 0, 0).y - root.theme.dp(12))
        padding: root.theme.dp(1)
        focus: true
        background: MenuSurface { cornerRadius: root.theme.dp(6); shown: dropdown.visible }
        onOpened: {
            list.currentIndex = Math.max(0, list.model.findIndex(row => row.id === root.selectedId && row.action !== "create"))
            list.forceActiveFocus()
        }
        contentItem: ListView {
            id: list
            clip: true
            model: [{action: "create", name: qsTr("New Preset"), id: ""}].concat(root.presets).concat([{name: qsTr("Default Preset"), id: ""}])
            keyNavigationEnabled: true
            Keys.onPressed: event => {
                if (event.key === Qt.Key_Home) { currentIndex = 0; event.accepted = true }
                else if (event.key === Qt.Key_End) { currentIndex = count - 1; event.accepted = true }
            }
            Keys.onReturnPressed: root.activate(model[currentIndex])
            Keys.onEnterPressed: root.activate(model[currentIndex])
            Keys.onRightPressed: {
                const row = model[currentIndex]
                if (row.id) openActions(row, currentItem)
            }
            function openActions(row, item) {
                root.contextId = row.id; root.contextName = row.name
                actions.x = width - root.theme.dp(4)
                actions.y = Math.max(0, Math.min(item.y - contentY, height - actions.implicitHeight))
                actions.open()
            }
            delegate: C.ItemDelegate {
                id: row
                required property var modelData
                required property int index
                objectName: modelData.action === "create" ? "playerPresetCreate" : modelData.id ? "playerPreset_" + index : "playerPresetDefault"
                width: list.width; height: root.theme.dp(root.theme.compactMenus ? 28 : 40)
                leftPadding: root.theme.dp(root.theme.compactMenus ? 8 : 15); rightPadding: leftPadding
                topPadding: 0; bottomPadding: 0
                text: modelData.name
                hoverEnabled: true
                highlighted: list.currentIndex === index
                onHoveredChanged: if (hovered) list.currentIndex = index
                onClicked: root.activate(modelData)
                background: Rectangle {
                    x: root.theme.dp(1); y: x
                    width: row.width - 2 * x; height: row.height - 2 * y
                    radius: root.theme.dp(4)
                    color: row.highlighted ? root.lightTheme ? root.theme.hover : "#1f1f1f" : "transparent"
                }
                contentItem: RowLayout {
                    spacing: root.theme.dp(10)
                    Item {
                        Layout.preferredWidth: root.theme.dp(root.theme.compactMenus ? 48 : 24); Layout.fillHeight: true
                        visible: (!root.theme.compactMenus && row.modelData.action === "create") || !!row.modelData.id
                        RedesignButton {
                            anchors.centerIn: parent; width: root.theme.dp(root.theme.compactMenus ? 48 : 30); height: root.theme.dp(root.theme.compactMenus ? 24 : 30)
                            visible: !!row.modelData.id
                            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                            iconName: root.theme.compactMenus ? "" : "more"; iconSize: root.theme.dp(24)
                            caption: root.theme.compactMenus ? qsTr("Actions") : ""
                            text: qsTr("Preset actions for %1").arg(row.modelData.name)
                            onClicked: list.openActions(row.modelData, row)
                        }
                        RedesignIcon { objectName: "presetCreateIcon"; anchors.centerIn: parent; width: root.theme.dp(24); height: width; visible: row.modelData.action === "create"; name: "add"; color: root.theme.playerLabel }
                    }
                    Text { Layout.fillWidth: true; text: row.modelData.name; font.pixelSize: root.theme.dp(14); color: row.modelData.action === "create" ? root.theme.playerLabel : root.theme.foreground; elide: Text.ElideRight }
                    RedesignIcon { objectName: "presetSelectionMark" + row.index; Layout.preferredWidth: root.theme.dp(24); Layout.preferredHeight: root.theme.dp(24); name: "check-active"; color: root.theme.accent; opacity: row.modelData.action !== "create" && row.modelData.id === root.selectedId ? 1 : 0 }
                }
            }
            C.ScrollIndicator.vertical: C.ScrollIndicator { }
        }
    }
    C.Menu {
        id: actions
        objectName: "playerPresetActions"
        parent: root.applicationProfile ? root.C.Overlay.overlay : dropdown.contentItem
        focus: true
        onAboutToShow: currentIndex = 0
        width: root.theme.dp(root.theme.compactMenus ? 220 : 265); padding: root.theme.menuPadding
        margins: root.theme.dp(10)
        background: MenuSurface { cornerRadius: root.theme.dp(10); shown: actions.visible }
        delegate: ProfileActionItem { }
        component ProfileActionItem: C.MenuItem {
            id: actionItem
            implicitHeight: root.theme.menuItemHeight
            leftPadding: 0; rightPadding: 0
            topPadding: 0; bottomPadding: 0
            Keys.onPressed: event => {
                if (event.key !== Qt.Key_Home && event.key !== Qt.Key_End) return
                actions.currentIndex = event.key === Qt.Key_Home ? 0 : actions.count - 1
                actions.itemAt(actions.currentIndex).forceActiveFocus()
                event.accepted = true
            }
            contentItem: Text { text: actionItem.text; color: root.theme.foreground; font.pixelSize: root.theme.dp(14); verticalAlignment: Text.AlignVCenter; leftPadding: root.theme.dp(root.theme.compactMenus ? 8 : 48); topPadding: root.theme.dp(root.theme.compactMenus ? 0 : 1) }
            background: Rectangle { color: !actionItem.highlighted ? "transparent" : root.lightTheme ? root.theme.hover : "#1f1f1f"; radius: root.theme.dp(6) }
        }
        C.Action { text: qsTr("Rename"); onTriggered: root.namePreset("rename", root.contextId, root.contextName) }
        C.Action { text: qsTr("Duplicate"); onTriggered: root.namePreset("duplicate", root.contextId, root.contextName) }
        C.Action { text: qsTr("Delete"); onTriggered: { dropdown.close(); deleteDialog.open() } }
        Instantiator {
            model: 1
            active: root.applicationProfile
            delegate: ProfileActionItem {
                objectName: "applicationProfileDefaultAction"
                text: qsTr("Default Profile")
                Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 1; color: root.theme.border }
                onTriggered: if (!root.backend.selectPlayerPreset("")) root.retrySelection("")
            }
            onObjectAdded: (index, object) => actions.addItem(object)
            onObjectRemoved: (index, object) => actions.removeItem(object)
        }
    }
    component MenuSurface: Rectangle {
        id: menuSurface
        property real cornerRadius
        property bool shown
        readonly property color fill: root.lightTheme ? root.theme.background : "#191919"
        color: Qt.rgba(fill.r, fill.g, fill.b, root.theme.floatingOpacity)
        radius: cornerRadius
        Loader {
            anchors.fill: parent
            z: -1
            active: menuSurface.shown
            sourceComponent: root.videoBackdropContent
            onLoaded: {
                if (item.cornerRadius !== undefined)
                    item.cornerRadius = Qt.binding(() => menuSurface.cornerRadius)
                if (item.menuShadow !== undefined) item.menuShadow = true
            }
        }
    }
    C.Popup {
        id: nameDialog
        objectName: "playerPresetNameDialog"
        parent: C.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(root.theme.dp(420), parent.width - root.theme.dp(32))
        padding: root.theme.dp(24)
        modal: true; dim: false; focus: true
        closePolicy: C.Popup.CloseOnEscape
        background: RedesignDialogSurface { theme: root.theme; videoBackdropContent: root.videoBackdropContent }
        onOpened: { presetName.forceActiveFocus(); presetName.selectAll() }
        onClosed: {
            if (root.backend.pendingPlayerPreset && (root.retryNameSave || (root.nameAction === "select" || root.nameAction === "delete"))) root.backend.cancelPlayerPreset()
            root.retryNameSave = false
        }
        contentItem: ColumnLayout {
            spacing: root.theme.dp(20)
            Text { Layout.fillWidth: true; text: root.nameAction === "delete" ? root.applicationProfile ? qsTr("Delete Profile") : qsTr("Delete Preset") : root.nameAction === "select" ? qsTr("Change Profile") : root.applicationProfile ? root.nameAction === "rename" ? qsTr("Rename Profile") : root.nameAction === "duplicate" ? qsTr("Duplicate Profile") : qsTr("New Profile") : root.nameAction === "rename" ? qsTr("Rename Preset") : root.nameAction === "duplicate" ? qsTr("Duplicate Preset") : qsTr("New Preset"); color: root.theme.bright; font.pixelSize: root.theme.dp(20) }
            Text {
                Layout.fillWidth: true; visible: root.applicationProfile && root.nameAction !== "select" && root.nameAction !== "delete"
                text: qsTr("Save all Hakone settings in this profile. Some settings apply to the next file or after you close Hakone and start it again.")
                color: root.theme.foreground; font.pixelSize: root.theme.dp(13); wrapMode: Text.WordWrap
            }
            C.TextField {
                id: presetName
                objectName: "playerPresetName"
                Layout.fillWidth: true; implicitHeight: root.theme.dp(42)
                visible: root.nameAction !== "select" && root.nameAction !== "delete"
                maximumLength: 80; readOnly: root.retryNameSave
                placeholderText: root.applicationProfile ? qsTr("Profile name") : qsTr("Preset name"); font.pixelSize: root.theme.dp(14)
                color: root.theme.bright; placeholderTextColor: root.theme.foreground
                selectByMouse: true
                background: Rectangle { color: root.theme.field; radius: root.theme.dp(4); border.color: presetName.activeFocus ? root.theme.accent : root.theme.border }
                onAccepted: root.saveName()
            }
            Text { Layout.fillWidth: true; visible: root.backend.errorMessage.length > 0; text: root.backend.errorMessage; color: root.theme.danger; wrapMode: Text.WordWrap; font.pixelSize: root.theme.dp(13) }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                RedesignButton { objectName: "playerPresetNameCancel"; scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor; caption: qsTr("Cancel"); captionFontWeight: Font.Medium; text: caption; onClicked: nameDialog.close() }
                RedesignButton { objectName: "playerPresetNameSave"; scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor; caption: root.retryNameSave || root.nameAction === "select" || root.nameAction === "delete" ? qsTr("Retry") : qsTr("Save"); text: caption; enabled: root.nameAction === "select" || root.nameAction === "delete" || presetName.text.trim().length > 0; primaryAction: true; onClicked: root.saveName() }
            }
        }
    }
    RedesignConfirmationDialog {
        id: deleteDialog
        objectName: "playerPresetDeleteDialog"
        parent: C.Overlay.overlay
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        videoBackdropContent: root.videoBackdropContent
        title: root.applicationProfile ? qsTr("Delete Profile") : qsTr("Delete Preset")
        message: qsTr("Delete “%1”?").arg(root.contextName)
        confirmText: qsTr("Delete"); destructive: true
        onAccepted: {
            if (root.backend.deletePlayerPreset(root.contextId)) return
            root.nameAction = "delete"; root.retryNameSave = root.backend.pendingPlayerPreset
            Qt.callLater(() => nameDialog.open())
        }
    }
}

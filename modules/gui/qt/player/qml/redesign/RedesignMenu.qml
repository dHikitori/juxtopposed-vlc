// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQml.Models
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

C.Menu {
    id: root
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    property Component videoBackdropContent
    property var recentModel: null
    property bool recentFilesAvailable: true
    property bool recentFilesEnabled: true
    property var profiles: []
    property var addonRegistry: typeof redesignAddonUi === "undefined" ? null : redesignAddonUi
    property var discoveryBackend: typeof redesignDiscoveryService === "undefined" ? null : redesignDiscoveryService
    readonly property bool discoveryAvailable: { if (!discoveryBackend) return false; discoveryBackend.revision; return discoveryBackend.sources.length > 0 }
    readonly property var addonCommands: {
        if (!addonRegistry) return []
        addonRegistry.revision
        return addonRegistry.commands.filter(entry => entry.document.hook === "player.actions@1").slice(0, 16)
    }
    readonly property string profileRows: JSON.stringify(profiles)
    property string selectedProfile: ""
    property Item triggerItem
    property bool pointerPressPending: false
    readonly property Item tooltipTrigger: triggerItem && ("tooltipEnabled" in triggerItem) ? triggerItem : null
    // Cancel a pending hint while this button owns an open popup.
    Binding {
        target: root.tooltipTrigger
        property: "tooltipEnabled"
        value: false
        when: root.visible && !!root.tooltipTrigger
        restoreMode: Binding.RestoreBindingOrValue
    }

    readonly property string profileName: profiles.find(profile => profile.id === selectedProfile)?.name || qsTr("Default Profile")
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    signal actionRequested(string action)
    signal discoveryRequested()
    signal recentRequested(var mediaId)
    signal clearRecentRequested()
    signal profileRequested(string profileId)
    signal profileActionsRequested(string profileId, string name, real x, real y)
    width: theme.dp(theme.compactMenus ? 280 : 375)
    padding: theme.menuPadding
    focus: true
    closePolicy: C.Popup.CloseOnEscape | C.Popup.CloseOnPressOutsideParent

    // Opens below anchor, kept inside area; closes when already open.
    function toggleFrom(anchor, area) {
        if (visible) { close(); return }
        // Exclude the opening button from outside-click dismissal. Its next
        // click must close this menu, rather than dismiss and reopen it.
        const point = anchor.mapToItem(area, 0, anchor.height)
        const local = area.mapToItem(anchor, Math.min(point.x, area.width - width), point.y)
        parent = anchor
        x = local.x; y = local.y
        open()
    }
    popupType: C.Popup.Item
    background: MenuSurface { shown: root.visible }
    delegate: MenuRow { }
    onAboutToShow: currentIndex = pointerPressPending ? -1 : 0
    onOpened: {
        // Qt can select a row while transferring focus to the popup.
        if (pointerPressPending) {
            currentIndex = -1
            contentItem.forceActiveFocus()
        }
        pointerPressPending = false
    }
    contentItem: ListView {
        implicitHeight: contentHeight
        model: root.contentModel
        interactive: Window.window ? contentHeight + root.topPadding + root.bottomPadding > root.height : false
        clip: true
        currentIndex: root.currentIndex
        C.ScrollIndicator.vertical: C.ScrollIndicator { }
        TapHandler {
            parent: root.triggerItem
            acceptedButtons: Qt.LeftButton
            // Observe the trigger without taking its click.
            gesturePolicy: TapHandler.DragThreshold
            onPressedChanged: {
                if (pressed) root.pointerPressPending = true
                else Qt.callLater(() => root.pointerPressPending = false)
            }
        }
        Keys.onPressed: event => {
            if (event.key !== Qt.Key_Home && event.key !== Qt.Key_End) return
            root.currentIndex = event.key === Qt.Key_Home ? 0 : root.count - 1
            root.itemAt(root.currentIndex).forceActiveFocus()
            event.accepted = true
        }
    }

    function activate(action) { dismiss(); actionRequested(action) }

    component MenuSurface: Rectangle {
        id: surface
        property bool shown: false
        readonly property color fill: root.theme.menuPanel
        color: Qt.rgba(fill.r, fill.g, fill.b, root.theme.floatingOpacity)
        radius: root.theme.dp(10)
        Loader {
            anchors.fill: parent
            z: -1
            active: surface.shown
            sourceComponent: root.videoBackdropContent
            onLoaded: {
                if (item.cornerRadius !== undefined)
                    item.cornerRadius = Qt.binding(() => surface.radius)
                if (item.menuShadow !== undefined) item.menuShadow = true
            }
        }
    }

    component MenuRow: C.MenuItem {
        id: row
        property string glyph: subMenu === recentMenu ? "menu-recent" : subMenu === profileMenu ? "menu-profile" : ""
        property string hint: ""
        property int textElide: Text.ElideRight
        implicitHeight: root.theme.menuItemHeight
        leftPadding: root.theme.dp(root.theme.compactMenus ? 8 : 14)
        rightPadding: root.theme.dp(root.theme.compactMenus ? 8 : 14)
        topPadding: 0; bottomPadding: 0
        hoverEnabled: true
        indicator: null
        arrow: null
        background: Rectangle { color: row.highlighted ? root.theme.hover : "transparent"; radius: root.theme.dp(6) }
        contentItem: RowLayout {
            spacing: root.theme.dp(root.theme.compactMenus ? 6 : 10)
            RedesignIcon {
                Layout.preferredWidth: root.theme.dp(root.theme.compactMenus ? 20 : 24); Layout.preferredHeight: Layout.preferredWidth
                visible: !root.theme.compactMenus || row.checkable
                name: row.checkable ? "check-active" : row.glyph
                color: row.checkable ? root.theme.accent : root.theme.foreground
                detailColor: root.theme.menuAccent
                opacity: row.checkable && !row.checked ? 0 : 1
            }
            Text {
                Layout.fillWidth: true
                text: row.text; textFormat: Text.PlainText; color: root.theme.foreground
                font.pixelSize: root.theme.dp(14)
                elide: row.textElide
            }
            Text { text: row.hint; color: root.theme.playerLabel; font.pixelSize: root.theme.dp(14); horizontalAlignment: Text.AlignRight }
            RedesignIcon {
                visible: !!row.subMenu
                Layout.preferredWidth: root.theme.dp(root.theme.compactMenus ? 20 : 24); Layout.preferredHeight: Layout.preferredWidth
                name: row.mirrored ? "chevron-left" : "chevron-right"; color: root.theme.foreground
            }
        }
        Keys.onPressed: event => {
            if (event.key !== Qt.Key_Home && event.key !== Qt.Key_End) return
            const menu = row.menu
            menu.currentIndex = event.key === Qt.Key_Home ? 0 : menu.count - 1
            menu.itemAt(menu.currentIndex).forceActiveFocus()
            event.accepted = true
        }
    }
    component MenuDivider: C.MenuSeparator {
        padding: 0
        verticalPadding: 0
        implicitHeight: root.theme.dp(1)
        contentItem: Item {
            implicitHeight: root.theme.dp(1)
            Rectangle {
                x: -root.theme.dp(0.5); y: -root.theme.dp(0.5)
                width: parent.width + root.theme.dp(1); height: root.theme.dp(1)
                radius: height / 2; antialiasing: true
                color: root.lightTheme ? root.theme.border : "#222222"
            }
        }
    }

    MenuRow { text: qsTr("Open File(s)"); glyph: "menu-file"; hint: "Ctrl + O"; onTriggered: root.activate("file") }
    MenuRow { text: qsTr("Open Folder"); glyph: "menu-folder"; hint: "Ctrl + F"; onTriggered: root.activate("directory") }
    MenuRow { text: qsTr("Open Network Stream"); glyph: "menu-network"; hint: "Ctrl + N"; onTriggered: root.activate("network") }
    MenuRow { text: qsTr("Open Disc"); glyph: "menu-disc"; hint: "Ctrl + D"; onTriggered: root.activate("disc") }
    MenuRow { text: qsTr("Open Capture Device"); glyph: "menu-capture"; hint: "Ctrl + C"; onTriggered: root.activate("capture") }
    MenuRow { text: qsTr("Convert"); glyph: "menu-convert"; hint: "Ctrl + R"; onTriggered: root.activate("convert") }
    C.Menu {
        id: recentMenu
        overlap: -root.theme.dp(11)
        objectName: "redesignRecentMenu"
        // An empty recent list has no enabled item to receive keyboard focus.
        // Keep navigation in this submenu so Back returns to its parent.
        onOpened: if (currentIndex < 0) contentItem.forceActiveFocus(Qt.PopupFocusReason)
        contentItem: ListView {
            implicitHeight: contentHeight
            model: recentMenu.contentModel
            interactive: Window.window ? contentHeight + recentMenu.topPadding + recentMenu.bottomPadding > recentMenu.height : false
            clip: true
            currentIndex: recentMenu.currentIndex
            C.ScrollIndicator.vertical: C.ScrollIndicator { }
            Keys.onPressed: event => {
                if (recentMenu.currentIndex >= 0
                    || event.key !== (recentMenu.mirrored ? Qt.Key_Right : Qt.Key_Left)) return
                recentMenu.close()
                const parentRow = root.itemAt(root.currentIndex)
                if (parentRow) parentRow.forceActiveFocus(Qt.PopupFocusReason)
                event.accepted = true
            }
        }
        title: qsTr("Open Recent")
        width: root.theme.dp(root.theme.compactMenus ? 280 : 375); padding: root.padding
        popupType: C.Popup.Item
        delegate: MenuRow { }
        background: MenuSurface { shown: recentMenu.visible }
        Instantiator {
            model: root.recentFilesAvailable && root.recentFilesEnabled ? root.recentModel : null
            delegate: MenuRow {
                id: recentRow
                required property var model
                text: model.url; glyph: "video"
                textElide: Text.ElideMiddle
                RedesignToolTip { theme: root.theme; text: recentRow.text; visible: recentRow.hovered }
                onTriggered: { root.dismiss(); root.recentRequested(model.id) }
            }
            onObjectAdded: (index, object) => recentMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => recentMenu.removeItem(object)
        }
        MenuRow {
            text: !root.recentFilesAvailable ? qsTr("Recent files need the media library.") : root.recentFilesEnabled ? qsTr("No recent files") : qsTr("Recent file history is off")
            enabled: false
            visible: !root.recentFilesAvailable || !root.recentFilesEnabled || !root.recentModel || root.recentModel.count === 0
            height: visible ? implicitHeight : 0
        }
        MenuDivider { }
        MenuRow {
            text: qsTr("Clear Recent Files"); glyph: "close"
            enabled: root.recentFilesAvailable && root.recentFilesEnabled && !!root.recentModel && root.recentModel.count > 0
            onTriggered: { root.dismiss(); root.clearRecentRequested() }
        }
    }
    MenuDivider { }
    C.Menu {
        id: profileMenu
        overlap: -root.theme.dp(11)
        objectName: "redesignProfileMenu"
        title: qsTr("Profile (%1)").arg(root.profileName)
        width: root.theme.dp(root.theme.compactMenus ? 240 : 265); padding: root.padding
        popupType: C.Popup.Item
        delegate: MenuRow { }
        background: MenuSurface { shown: profileMenu.visible }
        Instantiator {
            model: {
                const rows = JSON.parse(root.profileRows)
                return rows.length ? rows : [{id: "", name: qsTr("Default Profile")}]
            }
            delegate: MenuRow {
                required property var modelData
                id: profileRow
                function showActions() {
                    if (!modelData.id) return
                    const point = mapToItem(C.Overlay.overlay, width, height)
                    root.dismiss()
                    root.profileActionsRequested(modelData.id, modelData.name, point.x, point.y)
                }
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && event.modifiers & Qt.ShiftModifier)) {
                        showActions(); event.accepted = true
                    }
                }
                MouseArea { anchors.fill: parent; acceptedButtons: Qt.RightButton; onClicked: profileRow.showActions() }
                text: modelData.name
                checkable: true
                checked: root.selectedProfile === modelData.id
                onTriggered: { root.dismiss(); root.profileRequested(modelData.id) }
            }
            onObjectAdded: (index, object) => profileMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => profileMenu.removeItem(object)
        }
        MenuDivider { }
        MenuRow { objectName: "profileCreateAction"; text: qsTr("New"); glyph: "add"; onTriggered: root.activate("new-profile") }
    }
    MenuRow { text: qsTr("Keyboard Shortcuts"); glyph: "menu-keyboard"; onTriggered: root.activate("shortcuts") }
    Instantiator {
        objectName: "addonDiscoveryActions"
        model: root.discoveryAvailable ? [true] : []
        delegate: MenuRow {
            objectName: "addonDiscoveryMenuAction"
            text: qsTr("Find media"); glyph: "network"
            onTriggered: { root.dismiss(); root.discoveryRequested() }
        }
        onObjectAdded: (index, object) => root.insertItem(Math.max(0, root.count - 3), object)
        onObjectRemoved: (index, object) => root.removeItem(object)
    }
    Instantiator {
        model: root.addonCommands
        delegate: MenuRow {
            required property var modelData
            objectName: "addonPlayerAction_" + modelData.id
            text: qsTr("Add-on: %1").arg(modelData.name || modelData.id)
            onTriggered: { root.dismiss(); root.addonRegistry.triggerCommand(modelData.id, {}) }
        }
        onObjectAdded: (index, object) => root.insertItem(Math.max(0, root.count - 3), object)
        onObjectRemoved: (index, object) => root.removeItem(object)
    }
    MenuRow { text: qsTr("Customize Player"); glyph: "menu-customize"; onTriggered: root.activate("customize") }
    MenuRow { text: qsTr("Settings"); glyph: "menu-settings"; hint: "Ctrl + P"; onTriggered: root.activate("settings") }
    MenuRow { text: qsTr("About Hakone"); glyph: "menu-about"; hint: "Shift + F1"; onTriggered: root.activate("about") }
}

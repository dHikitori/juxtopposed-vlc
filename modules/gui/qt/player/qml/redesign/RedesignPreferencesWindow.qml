// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls.Basic as C
import VLC.MainInterface
import VLC.Style
import VLC.Dialogs
import VLC.Player
import VLC.Network

RedesignSettingsDialog {
    id: root
    scaleFactor: VLCStyle.scale
    accentColor: MainCtx.redesignAccent
    lightTheme: !VLCStyle.palette.isDark
    property string category: "general"
    readonly property bool usesReferenceSpacing: category === "interface" || category === "privacy" || category === "controls" || category === "library" || category === "audio" || category === "playback" || category === "video" || category === "subtitles" || category === "network" || category === "performance" || category === "recording"
    readonly property string categoryTitle: specification.pageTitles[category] || ""
    property bool advanced: false
    property bool expert: false
    property string search: ""
    onSearchChanged: {
        scroll.contentItem.contentY = 0
        if (!search.length) Qt.callLater(root.revealCurrentCategory)
    }
    property string actionMessage: ""
    property bool actionMessageIsError: true
    property bool resetFailed: false
    RedesignPreferences { id: preferenceStore; ctx: MainCtx; autoSave: true }
    RedesignPreferencesCatalog { id: specification }
    property bool addonsVisited: false
    onCategoryChanged: {
        if (category === "extensions") addonsVisited = true
        Qt.callLater(root.revealCurrentCategory)
    }
    onOpened: Qt.callLater(root.revealCurrentCategory)
    function revealNavigationItem(item) {
        const flick = categoryNavigation.contentItem
        if (!visible || !item || !flick || !flick.contentItem || flick.height <= 0) return
        const point = item.mapToItem(flick.contentItem, 0, 0)
        let position = flick.contentY
        if (point.y < position) position = point.y
        else if (point.y + item.height > position + flick.height)
            position = point.y + item.height - flick.height
        position = Math.max(0, Math.min(position, flick.contentHeight - flick.height))
        if (Math.abs(position - flick.contentY) > 0.01) {
            flick.cancelFlick()
            flick.contentY = position
        }
    }
    function revealCurrentCategory() {
        if (!visible || search.length) return
        for (let i = 0; i < categoryItems.count; ++i) {
            const entry = categoryItems.itemAt(i)
            if (entry && entry.modelData.key === category) {
                revealNavigationItem(entry.button)
                return
            }
        }
    }
    function revealNavigationAfterLayout() {
        if (!visible) return
        for (let i = 0; i < categoryItems.count; ++i) {
            const entry = categoryItems.itemAt(i)
            if (entry && entry.button && entry.button.visualFocus) {
                revealNavigationItem(entry.button)
                return
            }
        }
        revealCurrentCategory()
    }
    Binding {
        target: MainCtx; property: "useGlobalShortcuts"; value: false
        when: root.visible
        restoreMode: Binding.RestoreBindingOrValue
    }
    RedesignConfirmationDialog {
        id: resetDialog
        parent: content
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        videoBackdropContent: Component {
            RedesignSurfaceBackdrop {
                sourceItem: root.background ? root.background.parent : null
                scaleFactor: root.scaleFactor
                nativeVideo: false
            }
        }
        title: qsTr("Reset all settings")
        message: qsTr("This action replaces all Hakone settings with the default settings. After this action, close Hakone. Start Hakone again.")
        confirmText: qsTr("Reset"); destructive: true
        onAccepted: root.resetSettings()
    }
    Connections {
        target: preferenceStore
        function onChanged() { if (!preferenceStore.errorMessage.length) root.resetFailed = false }
        function onHistoryCleared(success) {
            root.actionMessageIsError = !success
            root.actionMessage = success ? qsTr("Hakone cleared the playback history.") : qsTr("Hakone cannot clear the playback history. Try again.")
        }
        function onPluginScanChanged() {
            root.actionMessage = preferenceStore.pluginScanMessage
            root.actionMessageIsError = preferenceStore.pluginScanFailed
        }
    }
    RedesignShortcuts {
        id: shortcuts
        objectName: "preferencesShortcuts"
        parent: content
        videoBackdropContent: Component {
            RedesignSurfaceBackdrop {
                sourceItem: content
                scaleFactor: root.scaleFactor
            }
        }
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        onSaved: preferenceStore.discard()
    }
    function begin() {
        preferenceStore.discard()
        actionMessage = preferenceStore.pluginScanMessage
        actionMessageIsError = preferenceStore.pluginScanFailed
        resetFailed = false
        searchField.clear()
        if (MainCtx.intfMainWindow.visibility === Window.Minimized) MainCtx.intfMainWindow.showNormal()
        else MainCtx.intfMainWindow.visible = true
        MainCtx.intfMainWindow.raise()
        MainCtx.intfMainWindow.requestActivate()
        open()
        content.forceActiveFocus()
    }
    function closeSettings() {
        content.forceActiveFocus()
        if (!preferenceStore.dirty || preferenceStore.save()) close()
    }
    function runAction(action) {
        if (action === "redesign-clear-history") {
            if (!preferenceStore.clearHistory()) {
                actionMessageIsError = true
                actionMessage = qsTr("Hakone cannot clear the playback history. Try again.")
            }
            return
        }
        if (action === "redesign-rescan-plugins") { preferenceStore.scanPlugins(); return }
        if (action === "redesign-reset-all") { resetDialog.open(); return }
        if (action === "redesign-reset-picture") {
            for (const name of ["hue", "brightness", "contrast", "saturation", "gamma"])
                preferenceStore.reset(name)
            return
        }
        if (action !== "redesign-config-folder") return
        actionMessageIsError = true
        const folder = preferenceStore.configurationFolder()
        actionMessage = folder.toString().length && Qt.openUrlExternally(folder) ? "" : qsTr("Hakone cannot open the settings folder.")
    }
    function resetSettings() {
        resetFailed = !preferenceStore.resetAll()
        if (!resetFailed) {
            actionMessageIsError = false
            actionMessage = qsTr("Hakone saved the default settings. Close Hakone. Start Hakone again to use all settings.")
        }
    }
    onDismissRequested: root.closeSettings()
    Connections {
        target: DialogsProvider
        function onShowRedesignPreferences() { if (!root.visible) root.begin(); else content.forceActiveFocus() }
        function onShowRedesignPreferencesPage(page) {
            if (!specification.pageTitles[page]) return
            root.begin()
            root.category = page
            root.advanced = false
            root.expert = false
            scroll.contentItem.contentY = 0
            Qt.callLater(root.revealCurrentCategory)
        }
    }
    contentItem: Item {
        id: content
        focus: true
        Rectangle {
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            height: theme.dp(50); radius: theme.dp(14); color: theme.field
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: parent.height / 2; color: parent.color }
        }
        Keys.onEscapePressed: root.closeSettings()
        Rectangle {
            id: navigation
            width: theme.dp(208); anchors.top: parent.top; anchors.bottom: parent.bottom
            color: "transparent"
            Rectangle {
                x: theme.dp(8); y: theme.dp(58); width: parent.width - theme.dp(8)
                height: parent.height - y - theme.dp(8); radius: theme.dp(6)
                color: root.lightTheme ? theme.background : "#191919"
                opacity: root.lightTheme ? 1 : 0.95
            }
            C.TextField {
                id: searchField
                objectName: "settingsSearch"
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                anchors.margins: theme.dp(8); anchors.rightMargin: 0; height: theme.dp(34)
                leftPadding: theme.dp(42)
                placeholderText: qsTr("Search")
                color: theme.foreground; placeholderTextColor: root.lightTheme ? theme.foreground : "#404040"
                font.pixelSize: theme.dp(14); selectByMouse: true
                background: Rectangle { color: root.lightTheme ? theme.background : "#151515"; radius: theme.dp(6); border.width: searchField.activeFocus ? 1 : 0; border.color: theme.accent }
                RedesignIcon { x: theme.dp(8); anchors.verticalCenter: parent.verticalCenter; width: theme.dp(24); height: width; name: "search"; color: root.lightTheme ? theme.foreground : "#6b6b6b" }
                onTextEdited: searchDelay.restart()
                onTextChanged: if (!text.length) { searchDelay.stop(); root.search = "" }
                onAccepted: { searchDelay.stop(); root.search = text }
                Timer {
                    id: searchDelay
                    interval: 200
                    onTriggered: root.search = searchField.text
                }
            }
            C.ScrollView {
                id: categoryNavigation
                objectName: "settingsNavigation"
                anchors.top: searchField.bottom; anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
                anchors.topMargin: theme.dp(16); anchors.bottomMargin: theme.dp(8)
                clip: true
                onHeightChanged: Qt.callLater(root.revealNavigationAfterLayout)
                onContentHeightChanged: Qt.callLater(root.revealNavigationAfterLayout)
                Column {
                    width: navigation.width
                    Repeater {
                        id: categoryItems
                        model: [
                            {group: qsTr("App"), key: "general", title: qsTr("General"), icon: "nav-preferences"},
                            {key: "interface", title: qsTr("Interface"), icon: "nav-interface"},
                            {key: "privacy", title: qsTr("Privacy"), icon: "nav-privacy"},
                            {key: "controls", title: qsTr("Controls"), icon: "nav-hotkeys"},
                            {group: qsTr("Media"), key: "playback", title: qsTr("Playback"), icon: "nav-playback"},
                            {key: "video", title: qsTr("Video"), icon: "nav-video"},
                            {key: "audio", title: qsTr("Audio"), icon: "nav-audio"},
                            {key: "subtitles", title: qsTr("Subtitles"), icon: "nav-subtitles"},
                            {key: "library", title: qsTr("Library"), icon: "nav-folder"},
                            {group: qsTr("System"), key: "network", title: qsTr("Network"), icon: "nav-streaming"},
                            {key: "performance", title: qsTr("Performance"), icon: "nav-preferences"},
                            {key: "recording", title: qsTranslate("Settings", "Recording"), icon: "nav-record"},
                            {key: "extensions", title: qsTr("Extensions"), icon: "nav-plugins"} ]
                        delegate: Column {
                            required property var modelData
                            property alias button: categoryButton
                            width: navigation.width
                            Text {
                                visible: !!modelData.group
                                height: visible ? theme.dp(modelData.key === "general" ? 47 : 41) : 0
                                leftPadding: theme.dp(24); topPadding: theme.dp(modelData.key === "general" ? 16 : 10)
                                text: modelData.group || ""; color: root.lightTheme ? theme.foreground : "#6b6b6b"; font.pixelSize: theme.dp(14)
                            }
                            Item {
                                width: parent.width; height: theme.dp(48)
                                RedesignButton {
                                    id: categoryButton
                                    objectName: "settingsCategory_" + modelData.key
                                    width: parent.width - theme.dp(20); x: theme.dp(14); height: theme.dp(40)
                                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                                    caption: modelData.title; text: caption; iconName: modelData.icon + (selected ? "-active" : "")
                                    leftAligned: true; contentLeftMargin: 8; captionSpacing: 12; captionVerticalOffset: 1
                                    selectedForeground: theme.bright
                                    background: Rectangle { radius: theme.dp(2); color: parent.selected ? root.lightTheme ? theme.selected : "#222222" : parent.hovered ? theme.hover : "transparent"; border.width: parent.visualFocus ? 1 : 0; border.color: theme.accent }
                                    selected: root.category === modelData.key && !root.search.length
                                    onActiveFocusChanged: if (activeFocus) root.revealNavigationItem(categoryButton)
                                    onClicked: {
                                        root.category = modelData.key
                                        root.search = ""; searchField.clear(); root.advanced = false; root.expert = false
                                        scroll.contentItem.contentY = 0
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        Text {
            id: heading
            anchors.left: navigation.right; anchors.leftMargin: theme.dp(32)
            y: theme.dp(16)
            text: root.search.length ? qsTr("Search results") : root.categoryTitle
            color: theme.foreground; font.pixelSize: theme.dp(16)
        }
        RedesignButton {
            id: closeButton
            anchors.right: parent.right; anchors.top: parent.top; anchors.margins: theme.dp(7); anchors.rightMargin: theme.dp(12)
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            iconName: "close"; text: qsTr("Close settings")
            onClicked: root.closeSettings()
        }
        C.ScrollView {
            id: scroll
            anchors.top: parent.top; anchors.topMargin: theme.dp(root.usesReferenceSpacing ? 70 : 72)
            anchors.left: navigation.right; anchors.right: parent.right; anchors.bottom: footer.top
            anchors.leftMargin: theme.dp(32); anchors.rightMargin: theme.dp(24)
            clip: true
            contentWidth: availableWidth
            Column {
                width: scroll.availableWidth
                spacing: theme.dp(root.category === "general" || root.usesReferenceSpacing ? 5 : 6)
                Loader {
                    width: parent.width
                    asynchronous: true
                    active: root.addonsVisited && typeof AddonManager !== "undefined"
                    visible: root.category === "extensions" && !root.search.length
                    sourceComponent: RedesignAddonManagement {
                        externalAdvanced: true
                        summaryBackend: typeof AddonPackageSummaries !== "undefined" ? AddonPackageSummaries : null
                        registry: typeof AddonRegistry !== "undefined" ? AddonRegistry : null
                        active: root.visible && root.category === "extensions" && !root.search.length
                        backend: typeof AddonManager !== "undefined" ? AddonManager : null
                        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    }
                }
                RedesignAddonSections {
                    width: parent.width
                    visible: root.category === "extensions" && !root.search.length
                    hook: "settings.addons.sections@1"
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                }
                RedesignGeneralAppearance {
                    visible: root.category === "general" && !root.search.length
                    width: parent.width
                    backend: preferenceStore; colorScheme: MainCtx.colorScheme
                    themeBackend: typeof AddonThemes !== "undefined" ? AddonThemes : null
                    appearanceRows: specification.sections.general.find(section => section.appearance).rows
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                }
                Repeater {
                    model: root.search.length ? [] : specification.visibleSections(root.category, preferenceStore)
                    delegate: Column {
                        required property var modelData
                        required property int index
                        width: parent.width
                        topPadding: root.category === "extensions" ? theme.dp(5) : 0
                        bottomPadding: root.category === "extensions" ? theme.dp(12) : 0
                        visible: modelData.rows.some(row => preferenceStore.describe(row[0]).kind !== undefined) || root.category === "controls" && modelData.title === qsTr("Keyboard Shortcuts")
                        Text {
                            topPadding: theme.dp((index === 0 && root.category !== "general" ? root.usesReferenceSpacing ? 9 : 8 : 24) + 0.5); bottomPadding: theme.dp(9.5)
                            text: modelData.title; color: theme.settingsHeading; font.pixelSize: theme.dp(16); font.weight: Font.Medium
                            visible: text.length > 0
                        }
                        ColumnLayout {
                            width: parent.width
                            visible: root.category === "controls" && modelData.title === qsTr("Keyboard Shortcuts")
                            spacing: 0
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.minimumHeight: theme.dp(42)
                                Text { Layout.fillWidth: true; text: qsTr("Defaults"); color: theme.foreground; font.pixelSize: theme.dp(14) }
                                RedesignButton {
                                    Layout.preferredWidth: theme.dp(320); Layout.preferredHeight: theme.dp(32)
                                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                                    objectName: "preferencesEditShortcuts"
                                    caption: qsTr("Edit Keyboard Shortcuts"); text: caption
                                    captionFontWeight: Font.Medium
                                    background: Rectangle { radius: theme.dp(6); color: parent.hovered ? theme.hover : root.lightTheme ? theme.background : "#191919" }
                                    onClicked: { content.forceActiveFocus(); if (!preferenceStore.dirty || preferenceStore.save()) shortcuts.open() }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.minimumHeight: theme.dp(42)
                                Text { Layout.fillWidth: true; text: qsTr("Reset all shortcuts"); color: theme.foreground; font.pixelSize: theme.dp(14) }
                                RedesignButton {
                                    Layout.preferredWidth: theme.dp(320); Layout.preferredHeight: theme.dp(32)
                                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                                    objectName: "preferencesResetShortcuts"
                                    caption: qsTr("Reset"); text: caption
                                    captionFontWeight: Font.Medium
                                    background: Rectangle { radius: theme.dp(6); color: parent.hovered ? theme.hover : root.lightTheme ? theme.background : "#191919" }
                                    onClicked: {
                                        for (const entry of preferenceStore.catalog("controls", ""))
                                            if (entry.name.startsWith("key-") || entry.name.startsWith("global-key-")) preferenceStore.reset(entry.name)
                                    }
                                }
                            }
                        }
                        Repeater {
                            model: modelData.rows
                            delegate: RedesignPreferenceRow {
                                required property var modelData
                                width: parent.width
                                backend: preferenceStore; optionName: modelData[0]; label: modelData[1]
                                presentation: modelData[2] || {}
                                onActionRequested: action => root.runAction(action)
                                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                            }
                        }
                    }
                }

                Item {
                    visible: !root.search.length
                    width: parent.width; height: theme.dp(root.category === "extensions" ? 40 : root.category === "general" || root.category === "interface" || root.category === "library" || root.category === "audio" || root.category === "playback" || root.category === "video" || root.category === "subtitles" || root.category === "network" || root.category === "performance" || root.category === "recording" ? 53 : 50)
                    RedesignButton {
                        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                        height: theme.dp(38)
                        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                        objectName: "preferencesAdvancedOptions"
                        caption: root.advanced ? qsTr("Hide Advanced Options") : qsTr("Advanced Options")
                        text: caption
                        contentItem: Item {
                            Row {
                                anchors.centerIn: parent; spacing: theme.dp(10)
                                Text { text: qsTr("Advanced Options"); color: theme.foreground; font.pixelSize: theme.dp(14); anchors.verticalCenter: parent.verticalCenter }
                                RedesignIcon { name: root.advanced ? "chevron-down" : (LayoutMirroring.enabled ? "chevron-left" : "chevron-right"); color: theme.foreground; width: theme.dp(20); height: width }
                            }
                        }
                        background: Rectangle { color: parent.hovered ? theme.hover : root.lightTheme ? theme.background : "#191919"; opacity: root.lightTheme ? 1 : 0.95; radius: theme.dp(2) }
                        onClicked: root.advanced = !root.advanced
                    }
                }
                Loader {
                    width: parent.width
                    active: root.advanced && root.category === "extensions" && !root.search.length && typeof AddonUiRegistry !== "undefined"
                    visible: root.category === "extensions" && !root.search.length
                    sourceComponent: RedesignAddonReplacements {
                        registry: typeof AddonUiRegistry !== "undefined" ? AddonUiRegistry : null
                        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    }
                }
                Loader {
                    objectName: "addonSessionsLoader"
                    width: parent.width
                    active: root.advanced && root.category === "extensions" && !root.search.length && typeof AddonSessions !== "undefined"
                    visible: root.category === "extensions" && !root.search.length
                    sourceComponent: RedesignAddonSessions {
                        backend: typeof AddonSessions !== "undefined" ? AddonSessions : null
                        active: root.visible && root.advanced && root.category === "extensions" && !root.search.length
                        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    }
                }
                Loader {
                    objectName: "settingsAdvancedVisualizer"
                    width: parent.width
                    active: root.advanced && root.category === "general" && !root.search.length && typeof AddonVisualizers !== "undefined"
                    visible: active
                    sourceComponent: RedesignAddonVisualizerSelector {
                        controller: typeof AddonVisualizers !== "undefined" ? AddonVisualizers : null
                        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    }
                }
                Repeater {
                    model: root.advanced && !root.search.length ? specification.advancedSections[root.category] || [] : []
                    delegate: Column {
                        required property var modelData
                        width: parent.width
                        topPadding: (root.category === "general" || root.category === "interface" || root.category === "library" || root.category === "audio" || root.category === "playback" || root.category === "video" || root.category === "subtitles" || root.category === "network" || root.category === "performance" || root.category === "recording") && !modelData.title.length ? theme.dp(15) : 0
                        Text {
                            visible: text.length > 0; topPadding: theme.dp((root.category === "general" || root.category === "performance" ? 24 : 20) + 0.5); bottomPadding: theme.dp(9.5)
                            text: modelData.title; color: theme.settingsHeading; font.pixelSize: theme.dp(16); font.weight: Font.Medium
                        }
                        Repeater {
                            model: modelData.rows
                            delegate: RedesignPreferenceRow {
                                required property var modelData
                                width: parent.width; backend: preferenceStore; optionName: modelData[0]; label: modelData[1]
                                presentation: modelData[2] || {}
                                onActionRequested: action => root.runAction(action)
                                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                            }
                        }
                    }
                }
                Loader {
                    width: parent.width
                    active: root.category === "extensions" && root.advanced && !root.search.length
                    visible: active
                    sourceComponent: RedesignPlugins {
                        plugins: preferenceStore.pluginCatalog()
                        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    }
                }
                RedesignSubtitleSourcePreferences {
                    videoBackdropContent: Component {
                        RedesignSurfaceBackdrop {
                            sourceItem: root.background ? root.background.parent : null
                            scaleFactor: root.scaleFactor
                            nativeVideo: false
                        }
                    }
                    width: parent.width
                    visible: root.search.length ? [qsTr("Online Subtitles"), qsTr("Add-on subtitle providers"), "OpenSubtitles", qsTr("API key")].some(text => text.toLowerCase().includes(root.search.toLowerCase())) : root.category === "subtitles"
                    sourceService: SubtitleSources
                    addonManager: typeof AddonManager !== "undefined" ? AddonManager : null
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                }
                RedesignButton {
                    visible: root.advanced && !root.search.length
                    width: parent.width
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    caption: root.expert ? qsTr("Hide all Hakone options") : qsTr("All Hakone options")
                    text: caption; selected: root.expert
                    onClicked: root.expert = !root.expert
                }
                Repeater {
                    model: {
                        if (root.search.length) {
                            return specification.searchResults(root.search, preferenceStore.catalog("", root.search))
                        }
                        return root.expert ? preferenceStore.catalog(root.category, "") : []
                    }
                    delegate: RedesignPreferenceRow {
                        required property var modelData
                        width: parent.width
                        backend: preferenceStore; optionName: modelData.name; label: modelData.title || ""; presentation: modelData.presentation || {}
                        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                        onActionRequested: action => root.runAction(action)
                    }
                }
                Item { width: 1; height: theme.dp(24) }
            }
        }
        Rectangle {
            id: footer
            anchors.left: navigation.right; anchors.right: parent.right; anchors.bottom: parent.bottom
            height: preferenceStore.errorMessage.length || root.actionMessage.length ? theme.dp(64) : 0
            visible: height > 0
            color: theme.background
            RowLayout {
                anchors.fill: parent; anchors.margins: theme.dp(16)
                Text {
                    Layout.fillWidth: true
                    text: preferenceStore.errorMessage || root.actionMessage
                    color: preferenceStore.errorMessage.length || root.actionMessageIsError ? "#e56b6b" : theme.foreground; font.pixelSize: theme.dp(13); wrapMode: Text.WordWrap
                }
                RedesignButton {
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    caption: qsTr("Retry"); text: caption; onClicked: root.resetFailed ? root.resetSettings() : preferenceStore.save()
                    visible: preferenceStore.errorMessage.length > 0
                }
            }
        }
    }
}

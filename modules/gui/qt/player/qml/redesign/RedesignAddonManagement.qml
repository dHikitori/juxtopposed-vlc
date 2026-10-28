// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

ColumnLayout {
    id: root
    required property var backend
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    property Component videoBackdropContent
    property bool active: true
    property bool developerSource: false
    property bool enableAfterInstall: false
    property bool showTechnicalDetails: false
    property bool advanced: false
    property bool externalAdvanced: false
    property string actionError: ""
    property string selectedId: ""
    property bool ready: false
    property string candidateKey: ""
    readonly property bool removalInProgress: packages.some(row => row.removing === true)
    readonly property bool busy: backend.busy === true
    readonly property bool safeMode: { backend.revision; return backend.safeMode === true }
    readonly property var pendingPackage: backend.pendingPackage || ({})
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme }
    // Read one package snapshot per revision. Delegates use this value only.
    readonly property var packages: { backend.revision; return backend.packages || [] }
    readonly property var managedPackages: packages.filter(row => row.builtin !== true && row.installed !== false && row.recoveryRequired !== true)
    readonly property var filteredPackages: {
        const query = search.text.trim().toLocaleLowerCase()
        return managedPackages.filter(row => !query || [row.name || "", row.id || ""].join(" ").toLocaleLowerCase().includes(query))
    }
    readonly property var selectedPackage: packages.find(row => row.id === selectedId && row.builtin !== true && row.installed !== false && row.recoveryRequired !== true) || ({})
    readonly property int recoveryCount: packages.filter(row => row.recoveryRequired === true || row.savedData === true || (row.suppressed === true && row.distributionSource === true)).length
    readonly property var updateResult: backend.updateResult || ({})
    readonly property var hostCapabilities: pendingPackage.id ? Object.assign({}, pendingPackage.hostCapabilities || backend.capabilities || ({}), {safeMode: safeMode}) : ({})
    readonly property bool updatingInstalledPackage: !!pendingPackage.id && packages.some(entry => entry.id === pendingPackage.id && entry.installed !== false)
    readonly property bool candidateCanRun: pendingPackage.workerRequired !== true || hostCapabilities.workerAvailable !== false
    spacing: theme.dp(12)
    onPendingPackageChanged: {
        const key = pendingPackage.id ? JSON.stringify([pendingPackage.id, pendingPackage.version || "", pendingPackage.digest || "", pendingPackage.source || "", pendingPackage.tag || ""]) : ""
        if (key === candidateKey) return
        candidateKey = key
        enableAfterInstall = false; showTechnicalDetails = false
        if (ready && active && pendingPackage.id) { openInstaller(); if (installerLoader.item) installerLoader.item.hadCandidate = true }
        else if (!pendingPackage.id && installerLoader.item && installerLoader.item.hadCandidate) installerLoader.item.close()
    }
    onActiveChanged: if (!active) closeDialogs()
    Component.onCompleted: { ready = true; if (active && pendingPackage.id) openInstaller() }
    function closeDialogs() {
        if (installerLoader.item) installerLoader.item.close()
        if (configurationLoader.item) configurationLoader.item.close()
        if (recoveryLoader.item) recoveryLoader.item.close()
        packageMenu.close(); changePackage.close()
    }
    function openInstaller() {
        if (recoveryLoader.item) recoveryLoader.item.close()
        installerLoader.active = true
        if (installerLoader.item) installerLoader.item.open()
    }
    function openRecovery() {
        recoveryLoader.active = true
        if (recoveryLoader.item) recoveryLoader.item.open()
    }
    function selectPackage(id) {
        selectedId = id
        packageList.currentIndex = filteredPackages.findIndex(row => row.id === id)
        if (packageList.currentIndex >= 0) packageList.positionViewAtIndex(packageList.currentIndex, ListView.Contain)
    }
    function stateText(row) {
        return row.removing === true ? qsTr("Removal in progress") : safeMode ? qsTr("Temporarily disabled")
            : row.configured === false ? row.enabled === true ? qsTr("Setup required") : qsTr("Disabled · Setup required") : row.enabled === true ? qsTr("Enabled") : qsTr("Disabled")
    }
    function setPaused(paused) {
        if (typeof backend.setSafeMode !== "function") return
        actionError = backend.setSafeMode(paused) ? "" : backend.errorMessage || qsTr("The app cannot change the extension state.")
    }
    readonly property var knownPermissions: ["metadata.context", "media.fingerprint", "media.stream", "network.http", "network.websocket", "credentials.use", "storage.private", "library.read", "queue.read", "queue.modify", "playback.observe", "playback.control", "audio.analysis", "ui.contribute", "session.background"]
    readonly property bool permissionsKnown: {
        const permissions = pendingPackage.permissions || {}
        if (!Object.keys(permissions).every(name => knownPermissions.includes(name))) return false
        const stream = permissions["media.stream"]
        if (stream !== undefined && (!stream || Object.keys(stream).length !== 1
            || !Array.isArray(stream.profiles) || stream.profiles.length !== 1
            || stream.profiles[0] !== "public-https-media@1")) return false
        const scope = permissions["network.http"] || {}
        const profiles = scope.assetRedirectProfiles === undefined ? [] : scope.assetRedirectProfiles
        return Array.isArray(profiles) && profiles.every(profile => profile === "internet-archive-images@1")
    }
    function destinations(origins) { return (origins || []).map(origin => String(origin).replace(/^https?:\/\//, "").replace(/^wss:\/\//, "")).join(", ") }
    function permissionSummaries(permissions) {
        const summaries = []
        const mediaFields = {kind: qsTr("media type"), type: qsTr("media type"), title: qsTr("title"), titles: qsTr("titles"), year: qsTr("release year"),
            season: qsTr("season number"), episodes: qsTr("episode numbers"), episode: qsTr("episode details"), ids: qsTr("catalog identifiers"), externalIds: qsTr("catalog identifiers"),
            language: qsTr("language"), languages: qsTr("languages"), duration: qsTr("duration")}
        const queueActions = {append: qsTr("add media"), enqueue: qsTr("add media"), insert: qsTr("insert media"), remove: qsTr("remove media"), move: qsTr("reorder items"),
            reorder: qsTr("reorder items"), clear: qsTr("clear the queue"), replace: qsTr("replace the queue")}
        const playbackActions = {play: qsTr("play"), pause: qsTr("pause"), stop: qsTr("stop"), seek: qsTr("seek"), rate: qsTr("change speed"), setRate: qsTr("change speed"), setPosition: qsTr("seek")}
        const hooks = {"home.sections@1": qsTr("Home sections"), "home.content@1": qsTr("Home content"), "library.navigation@1": qsTr("Library navigation"),
            "media.details.sections@1": qsTr("Media details"), "subtitles.results.sections@1": qsTr("Subtitle search results"), "subtitles.search.content@1": qsTr("Subtitle search"),
            "queue.item.actions@1": qsTr("Queue actions"), "player.actions@1": qsTr("Player controls"), "dialog.add-to-playlist.content@1": qsTr("Add-to-playlist picker"),
            "settings.addons.sections@1": qsTr("Add-on settings"), "actions.subtitles.open@1": qsTr("Open subtitle search"), "visualizer.surface@1": qsTr("Visualizer")}
        for (const name of Object.keys(permissions || {}).sort()) {
            const scope = permissions[name] || {}
            switch (name) {
            case "metadata.context": {
                const fields = [...new Set((scope.fields || []).map(field => mediaFields[field]).filter(Boolean))]
                summaries.push(fields.length ? qsTr("Read selected media details: %1.").arg(fields.join(", ")) : qsTr("Read permitted identity details about selected media."))
                break
            }
            case "media.fingerprint": summaries.push(qsTr("Calculate a fingerprint for selected media.")); break
            case "media.stream": summaries.push(qsTr("Play selected media from public online services. Each source also needs your playback approval.")); break
            case "network.http": {
                summaries.push(qsTr("Connect to these services: %1").arg(destinations(scope.origins)))
                if (Array.isArray(scope.assetRedirectProfiles) && scope.assetRedirectProfiles.includes("internet-archive-images@1"))
                    summaries.push(qsTr("Image downloads can follow redirects to archive.org and its subdomains. These redirects use HTTPS on port 443 and do not use credentials."))
                break
            }
            case "network.websocket": summaries.push(qsTr("Maintain a live connection with: %1").arg(destinations(scope.origins))); break
            case "credentials.use": summaries.push(qsTr("Use saved credentials only for these services: %1").arg(destinations([...new Set((scope.bindings || []).map(binding => binding.origin))]))); break
            case "storage.private": summaries.push(qsTr("Store a maximum of %1 MB of add-on data.").arg(Math.ceil(Number(scope.quotaBytes || 0) / (1024 * 1024)))); break
            case "library.read": summaries.push(qsTr("Read permitted media library items and their metadata.")); break
            case "queue.read": summaries.push(qsTr("Read the current play queue.")); break
            case "queue.modify": {
                const actions = [...new Set((scope.operations || []).map(operation => queueActions[operation]).filter(Boolean))]
                summaries.push(actions.length ? qsTr("Change the play queue: %1.").arg(actions.join(", ")) : qsTr("Change the play queue with its approved actions."))
                break
            }
            case "playback.observe": summaries.push(qsTr("Observe playback status, position and timing.")); break
            case "playback.control": {
                const actions = [...new Set((scope.operations || []).map(operation => playbackActions[operation]).filter(Boolean))]
                summaries.push(actions.length ? qsTr("Control playback: %1.").arg(actions.join(", ")) : qsTr("Control playback with its approved actions."))
                break
            }
            case "audio.analysis": summaries.push(qsTr("Receive audio analysis for visualizations.")); break
            case "ui.contribute": {
                const labels = [...new Set((scope.hooks || []).map(hook => hooks[hook] || qsTr("Declared app integration points")))]
                summaries.push(qsTr("Add interface content at: %1").arg(labels.join(", ")))
                break
            }
            case "session.background": summaries.push(qsTr("Continue an approved session while its screen is closed.")); break
            default: summaries.push(qsTr("The app cannot use a requested permission. You cannot install this package.")); break
            }
        }
        return summaries.length ? summaries.join("\n\n") : qsTr("The add-on does not need other permissions.")
    }
    function packageSignature(row) {
        return JSON.stringify([row.id, row.version || "", row.digest || "", row.source || "", row.enabled === true, row.hasPrevious === true])
    }
    function requestPackageChange(id, operation) {
        const row = packages.find(entry => entry.id === id)
        if (busy || !row || row.builtin === true || row.installed === false || row.recoveryRequired === true) return false
        actionError = ""; changePackage.packageId = id; changePackage.operation = operation
        changePackage.packageState = packageSignature(row); changePackage.revision = backend.revision
        changePackage.open(); return true
    }
    function configure(id) {
        const entry = packages.find(item => item.id === id)
        if (!entry) return
        configurationLoader.returnFocus = root.Window.window ? root.Window.window.activeFocusItem : null
        configurationLoader.packageId = id; configurationLoader.packageName = entry.name
        configurationLoader.active = true
        if (configurationLoader.item) configurationLoader.item.open()
    }
    function restoreConfigurationFocus() {
        if (configurationLoader.active) return
        const target = configurationLoader.returnFocus
        configurationLoader.returnFocus = null
        if (active && visible && target && target.visible && target.enabled) target.forceActiveFocus(Qt.PopupFocusReason)
    }
    function capabilityLimits(capabilities, permissions, workerRequired) {
        const limits = []
        if (workerRequired && capabilities.workerAvailable === false) limits.push(qsTr("This app cannot run add-on code."))
        if (permissions["network.http"] && capabilities.networkHttpAvailable === false) limits.push(qsTr("This app cannot connect to online services for add-ons."))
        if (permissions["storage.private"] && capabilities.privateStorageAvailable === false) limits.push(qsTr("This app does not support private add-on storage."))
        if (permissions["network.websocket"] && capabilities.webSocketAvailable === false) limits.push(qsTr("This app cannot keep a live service connection for add-ons."))
        if (permissions["media.stream"] && capabilities.mediaStreamAvailable !== true) limits.push(qsTr("This app cannot play media from discovery sources."))
        const operations = capabilities.coreOperations
        if (Array.isArray(operations)) {
            if (permissions["library.read"] && !operations.includes("library.query")) limits.push(qsTr("This app cannot give add-ons access to the media library."))
            if (permissions["queue.modify"] && !operations.includes("queue.apply")) limits.push(qsTr("This app cannot let add-ons change the play queue."))
            if (permissions["playback.control"] && !operations.includes("playback.command")) limits.push(qsTr("This app cannot let add-ons control playback."))
        }
        if (capabilities.safeMode === true) limits.push(qsTr("Add-ons are temporarily disabled."))
        return limits.join("\n")
    }

    Text {
        Layout.fillWidth: true
        text: qsTr("Add-ons"); textFormat: Text.PlainText
        color: root.theme.settingsHeading; font.pixelSize: root.theme.dp(14)
    }
    Rectangle {
        objectName: "addonInstalledList"
        Layout.fillWidth: true; Layout.preferredHeight: root.theme.dp(252)
        color: root.theme.field; radius: root.theme.dp(6)
        border.width: 1; border.color: root.theme.border
        ListView {
            id: packageList
            objectName: "addonPackageList"
            anchors.fill: parent; anchors.margins: 1; clip: true
            model: root.filteredPackages
            currentIndex: -1; boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: true; keyNavigationWraps: false
            C.ScrollBar.vertical: C.ScrollBar { policy: C.ScrollBar.AsNeeded }
            onCurrentItemChanged: if (currentItem) root.selectedId = currentItem.modelData.id
            Keys.onReturnPressed: if (root.selectedPackage.id) root.configure(root.selectedPackage.id)
            Keys.onEnterPressed: if (root.selectedPackage.id) root.configure(root.selectedPackage.id)
            delegate: C.ItemDelegate {
                id: packageRow
                required property var modelData
                required property int index
                objectName: "addonPackage_" + modelData.id
                width: ListView.view.width; height: root.theme.dp(62)
                highlighted: root.selectedId === modelData.id
                hoverEnabled: true; focusPolicy: Qt.StrongFocus
                leftPadding: root.theme.dp(16); rightPadding: root.theme.dp(16)
                Accessible.name: (modelData.name || modelData.id) + ", " + root.stateText(modelData)
                onClicked: root.selectPackage(modelData.id)
                background: Rectangle {
                    color: packageRow.highlighted ? root.theme.selected : packageRow.hovered ? root.theme.hover : "transparent"
                    Rectangle { anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: root.theme.dp(3); color: root.theme.accent; visible: packageRow.activeFocus }
                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: root.theme.border; visible: packageRow.index < packageList.count - 1 }
                }
                contentItem: RowLayout {
                    spacing: root.theme.dp(12)
                    RedesignIcon {
                        name: "nav-plugins"; color: packageRow.highlighted ? root.theme.bright : root.theme.foreground
                        Layout.preferredWidth: root.theme.dp(24); Layout.preferredHeight: root.theme.dp(24)
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: root.theme.dp(3)
                        Text {
                            Layout.fillWidth: true
                            text: packageRow.modelData.name || packageRow.modelData.id; textFormat: Text.PlainText
                            color: root.theme.bright; font.pixelSize: root.theme.dp(14); elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: [packageRow.modelData.version || "", root.stateText(packageRow.modelData)].filter(value => value.length > 0).join(" · ")
                            textFormat: Text.PlainText; color: root.theme.foreground; font.pixelSize: root.theme.dp(12); elide: Text.ElideRight
                        }
                    }
                    RedesignSwitchRow {
                        objectName: "addonPackageEnabled_" + packageRow.modelData.id
                        label: ""; accessibleName: qsTr("Enable %1").arg(packageRow.modelData.name || packageRow.modelData.id)
                        checked: packageRow.modelData.enabled === true && !root.safeMode
                        enabled: !root.busy && !root.safeMode && packageRow.modelData.removing !== true
                        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                        onToggled: checked => root.backend.setEnabled(packageRow.modelData.id, checked)
                    }
                }
            }
            Text {
                anchors.centerIn: parent; width: parent.width - root.theme.dp(32)
                visible: packageList.count === 0
                text: search.text.trim() ? qsTr("No extensions match your search.") : qsTr("No extensions are installed. Select Add extension to start.")
                textFormat: Text.PlainText; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter
                color: root.theme.foreground; font.pixelSize: root.theme.dp(14)
            }
        }
    }
    GridLayout {
        Layout.fillWidth: true
        columns: root.width < root.theme.dp(510) ? 1 : 2
        columnSpacing: root.theme.dp(12); rowSpacing: root.theme.dp(8)
        PackageInput {
            id: search
            objectName: "addonSearch"
            Layout.fillWidth: true; Layout.minimumWidth: 0
            placeholderText: qsTr("Search extensions"); Accessible.name: placeholderText
            maximumLength: 256
            onTextChanged: packageList.currentIndex = -1
            Keys.onDownPressed: { packageList.forceActiveFocus(); if (packageList.count > 0) packageList.currentIndex = 0 }
            Shortcut { sequence: StandardKey.Find; enabled: root.active && root.visible; onActivated: search.forceActiveFocus() }
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: root.theme.dp(6)
            ActionButton {
                objectName: "addonAddExtension"
                caption: root.pendingPackage.id ? qsTr("Review extension") : qsTr("Add extension")
                primaryAction: true; enabled: !root.removalInProgress
                onClicked: root.openInstaller()
            }
            ActionButton {
                objectName: "addonSelectedSettings"
                caption: qsTr("Settings"); visible: root.width >= root.theme.dp(360); enabled: !!root.selectedPackage.id && !root.busy
                onClicked: root.configure(root.selectedPackage.id)
            }
            ActionButton {
                id: moreButton
                objectName: "addonMoreActions"
                caption: root.width < root.theme.dp(360) ? "" : qsTr("More"); text: qsTr("More extension actions"); iconName: "more"
                onClicked: packageMenu.toggleFrom(moreButton)
            }
        }
    }
    Text {
        Layout.fillWidth: true; visible: !root.selectedPackage.id && !!root.managedPackages.length
        text: qsTr("Select an extension for settings and version details."); textFormat: Text.PlainText
        color: root.theme.playerLabel; font.pixelSize: root.theme.dp(12); wrapMode: Text.Wrap
    }
    Rectangle {
        objectName: "addonSelectedDetails"
        Layout.fillWidth: true; visible: !!root.selectedPackage.id
        implicitHeight: details.implicitHeight + root.theme.dp(24)
        color: root.theme.panel; radius: root.theme.dp(6); border.width: 1; border.color: root.theme.border
        ColumnLayout {
            id: details
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: root.theme.dp(12)
            spacing: root.theme.dp(8)
            RowLayout {
                Layout.fillWidth: true
                Text { Layout.fillWidth: true; text: root.selectedPackage.name || ""; textFormat: Text.PlainText; elide: Text.ElideRight; color: root.theme.bright; font.pixelSize: root.theme.dp(14) }
                Text { text: root.stateText(root.selectedPackage); textFormat: Text.PlainText; color: root.theme.foreground; font.pixelSize: root.theme.dp(12) }
            }
            Text {
                Layout.fillWidth: true
                text: [root.selectedPackage.version ? qsTr("Version %1").arg(root.selectedPackage.version) : "",
                    root.selectedPackage.distributionSource ? qsTr("Included with the app") : root.selectedPackage.developerSource ? qsTr("Local development package") : qsTr("Installed from a repository")].filter(value => value.length > 0).join(" · ")
                textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.foreground; font.pixelSize: root.theme.dp(12)
            }
            Flow {
                Layout.fillWidth: true; spacing: root.theme.dp(6)
                ActionButton {
                    objectName: "addonPackageConfigure_" + (root.selectedPackage.id || "")
                    caption: root.selectedPackage.configured === false ? qsTr("Set up…") : qsTr("Settings…")
                    enabled: !root.busy
                    onClicked: root.configure(root.selectedPackage.id)
                }
                ActionButton {
                    objectName: "addonPackageFindUpdates_" + (root.selectedPackage.id || "")
                    caption: qsTr("Find updates")
                    visible: root.selectedPackage.developerSource !== true && root.selectedPackage.distributionSource !== true
                        && /^https:\/\//i.test(root.selectedPackage.source || "") && typeof root.backend.checkRepositoryUpdates === "function"
                    enabled: !root.busy && !root.pendingPackage.id
                    onClicked: root.backend.checkRepositoryUpdates(root.selectedPackage.id, "^" + root.selectedPackage.version, false)
                }
                ActionButton {
                    objectName: "addonPackageAppUpdate_" + (root.selectedPackage.id || "")
                    caption: qsTr("Review app version %1").arg(root.selectedPackage.appUpdateVersion || "")
                    visible: root.selectedPackage.appUpdateAvailable === true && typeof root.backend.stageDistributionPackage === "function"
                    enabled: !root.busy && !root.pendingPackage.id
                    onClicked: root.backend.stageDistributionPackage(root.selectedPackage.id)
                }
            }
        }
    }
    Rectangle {
        Layout.fillWidth: true
        visible: root.safeMode || root.recoveryCount > 0
        implicitHeight: notice.implicitHeight + root.theme.dp(20)
        color: root.theme.field; radius: root.theme.dp(6)
        ColumnLayout {
            id: notice
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: root.theme.dp(10)
            spacing: root.theme.dp(6)
            Text {
                Layout.fillWidth: true
                text: root.safeMode ? qsTr("Extensions are temporarily disabled.") : qsTr("Some extensions or saved data need attention.")
                textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
            }
            Flow {
                Layout.fillWidth: true; spacing: root.theme.dp(6)
                ActionButton { caption: qsTr("Use extensions"); visible: root.safeMode; onClicked: root.setPaused(false) }
                ActionButton { objectName: "addonOpenRecovery"; caption: qsTr("Recovery and saved data…"); visible: root.recoveryCount > 0; onClicked: root.openRecovery() }
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true; visible: root.busy || !!root.updateResult.packageId || !!root.pendingPackage.id
        C.BusyIndicator { running: root.busy; visible: running; Layout.preferredWidth: root.theme.dp(20); Layout.preferredHeight: root.theme.dp(20) }
        Text {
            objectName: "addonUpdateStatus"
            Layout.fillWidth: true
            text: root.removalInProgress ? qsTr("The app removes the extension…") : root.updateResult.checking ? qsTr("Update search in progress.")
                : root.updateResult.canceled ? qsTr("The app canceled the update search.")
                : root.pendingPackage.id ? qsTr("An extension is ready for approval. The installed version stays available.")
                : root.busy ? qsTr("The app prepares the extension…") : root.backend.errorMessage ? root.backend.errorMessage
                : root.updateResult.available ? qsTr("An update is ready for approval.") : qsTr("No compatible stable update is available.")
            textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
        }
        ActionButton {
            objectName: "addonRepositoryCancel"; caption: qsTr("Cancel request")
            visible: root.busy && !root.removalInProgress
            onClicked: root.backend.discardPendingPackage()
        }
    }
    Text {
        objectName: "addonManagementError"
        Layout.fillWidth: true; visible: text.length > 0
        text: root.actionError || root.backend.errorMessage || ""; textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: root.theme.danger; font.pixelSize: root.theme.dp(13)
        Accessible.role: Accessible.AlertMessage
    }
    ActionButton {
        objectName: "addonAdvancedOptions"
        Layout.fillWidth: true; visible: !root.externalAdvanced; leftAligned: true
        caption: qsTr("Advanced Options"); iconName: root.advanced ? "chevron-up" : "chevron-down"
        onClicked: root.advanced = !root.advanced
        background: Rectangle { color: parent.hovered ? root.theme.hover : root.theme.panel; radius: root.theme.dp(6) }
    }
    ColumnLayout {
        Layout.fillWidth: true; visible: root.advanced && !root.externalAdvanced; spacing: root.theme.dp(8)
        Text { Layout.fillWidth: true; text: qsTr("Use these controls if an extension causes a problem."); textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.foreground; font.pixelSize: root.theme.dp(13) }
        Flow {
            Layout.fillWidth: true; spacing: root.theme.dp(8)
            ActionButton { objectName: "addonSafeMode"; caption: root.safeMode ? qsTr("Use extensions") : qsTr("Temporarily disable extensions"); onClicked: root.setPaused(!root.safeMode) }
            ActionButton { caption: qsTr("Recovery and saved data…"); onClicked: root.openRecovery() }
        }
    }
    RedesignActionMenu {
        id: packageMenu
        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
        videoBackdropContent: root.videoBackdropContent
        entries: [
            {action: "rollback", label: qsTr("Use previous version"), enabled: !!root.selectedPackage.id && root.selectedPackage.hasPrevious === true && !root.busy},
            {action: "remove", label: qsTr("Remove extension…"), enabled: !!root.selectedPackage.id && !root.busy},
            {action: "removeData", label: qsTr("Delete extension and saved data…"), enabled: !!root.selectedPackage.id && !root.busy},
            {action: "pause", label: root.safeMode ? qsTr("Use extensions") : qsTr("Temporarily disable extensions"), enabled: typeof root.backend.setSafeMode === "function"},
            {action: "recovery", label: qsTr("Recovery and saved data…"), enabled: true}]
        onActionRequested: action => action === "recovery" ? root.openRecovery() : action === "pause" ? root.setPaused(!root.safeMode) : root.requestPackageChange(root.selectedPackage.id, action)
    }
    Loader {
        id: configurationLoader
        property Item returnFocus: null
        property string packageId: ""
        property string packageName: ""
        active: false
        sourceComponent: RedesignAddonSettingsDialog {
            objectName: "addonManagementSettingsDialog"
            parent: root.Window.contentItem || root
            backend: root.backend; packageId: configurationLoader.packageId; packageName: configurationLoader.packageName
            scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
            videoBackdropContent: root.videoBackdropContent
            onClosed: { configurationLoader.active = false; Qt.callLater(root.restoreConfigurationFocus) }
        }
        onLoaded: item.open()
    }
    Loader {
        id: installerLoader
        active: false
        onLoaded: item.open()
        sourceComponent: C.Dialog {
            id: installer
            property bool hadCandidate: !!root.pendingPackage.id
            objectName: "addonInstallDialog"
            title: root.pendingPackage.id ? qsTr("Review extension") : qsTr("Add extension")
            parent: root.Window.contentItem || root
            popupType: C.Popup.Item
            x: Math.max(0, (parent.width - width) / 2); y: Math.max(0, (parent.height - height) / 2)
            width: Math.max(0, Math.min(root.theme.dp(600), parent.width - root.theme.dp(32)))
            height: Math.min(implicitHeight, Math.max(0, parent.height - root.theme.dp(32)))
            padding: root.theme.dp(24); modal: true; dim: true; focus: true
            header: null; footer: null; closePolicy: C.Popup.CloseOnEscape
            onClosed: installerLoader.active = false
            background: RedesignDialogSurface { theme: root.theme; videoBackdropContent: root.videoBackdropContent }
            C.Overlay.modal: Rectangle { color: "#80000000" }
            contentItem: ColumnLayout {
                Accessible.role: Accessible.Dialog
                Accessible.name: installer.title
                spacing: root.theme.dp(12)
                C.ScrollView {
                    id: installScroll
                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 0
                    implicitHeight: installContent.implicitHeight
                    contentWidth: availableWidth; clip: true
                    C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
                    ColumnLayout {
                        id: installContent
                        width: installScroll.availableWidth; spacing: root.theme.dp(12)
                            Text {
                                Layout.fillWidth: true
                                text: root.pendingPackage.id ? qsTr("Review extension") : qsTr("Add extension"); textFormat: Text.PlainText; wrapMode: Text.Wrap
                                color: root.theme.settingsHeading; font.pixelSize: root.theme.dp(20)
                            }
                            Text {
                                Layout.fillWidth: true
                                text: qsTr("Enter the extension repository and release tag. The app will show its permissions before installation.")
                                visible: !root.pendingPackage.id
                                textFormat: Text.PlainText; wrapMode: Text.Wrap
                                color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
                            }
                            PackageInput {
                                id: source
                                objectName: "addonRepositorySource"
                                placeholderText: qsTr("Repository URL (HTTPS)"); Accessible.name: placeholderText
                                maximumLength: 2048
                                readOnly: root.busy || !!root.pendingPackage.id
                                visible: !root.developerSource && !root.pendingPackage.id
                            }
                            PackageInput {
                                id: tag
                                objectName: "addonRepositoryTag"
                                placeholderText: qsTr("Release tag (for example, v1.0.0)"); Accessible.name: placeholderText
                                maximumLength: 128
                                readOnly: root.busy || !!root.pendingPackage.id
                                visible: !root.developerSource && !root.pendingPackage.id
                            }
                            RedesignSwitchRow {
                                objectName: "addonDeveloperSource"
                                Layout.fillWidth: true
                                label: qsTr("Use a local development package")
                                visible: !root.pendingPackage.id
                                checked: root.developerSource; enabled: !root.busy && !root.pendingPackage.id
                                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                                onToggled: checked => root.developerSource = checked
                            }
                            PackageInput {
                                id: directory
                                objectName: "addonLocalDirectory"
                                placeholderText: qsTr("Package folder"); Accessible.name: placeholderText
                                maximumLength: 4096
                                visible: root.developerSource && !root.pendingPackage.id; readOnly: root.busy || !!root.pendingPackage.id
                            }
                            ActionButton {
                                objectName: "addonStagePackage"
                                caption: root.busy ? qsTr("Prepare extension…") : qsTr("Continue")
                                primaryAction: true
                                visible: !root.pendingPackage.id
                                enabled: !root.busy && !root.pendingPackage.id && (root.developerSource
                                    ? directory.text.trim().length > 0 && typeof root.backend.stageLocalPackage === "function"
                                    : /^https:\/\/[^\s/]+\/[^\s]+$/i.test(source.text.trim()) && tag.text.trim().length > 0 && typeof root.backend.stageRepository === "function")
                                onClicked: root.developerSource ? root.backend.stageLocalPackage(directory.text.trim()) : root.backend.stageRepository(source.text.trim(), tag.text.trim())
                            }
                            Rectangle {
                                objectName: "addonInstallReview"
                                Layout.fillWidth: true; visible: !!root.pendingPackage.id
                                implicitHeight: review.implicitHeight + root.theme.dp(24)
                                color: root.theme.field; radius: root.theme.dp(6)
                                ColumnLayout {
                                    id: review
                                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: root.theme.dp(12)
                                    spacing: root.theme.dp(8)
                                    Text {
                                        Layout.fillWidth: true
                                        text: (root.updatingInstalledPackage ? qsTr("Update approval: %1") : qsTr("Installation approval: %1")).arg(root.pendingPackage.name || root.pendingPackage.id || "")
                                        textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.bright; font.pixelSize: root.theme.dp(16)
                                    }
                                    Text {
                                        objectName: "addonInstallOverview"
                                        Layout.fillWidth: true
                                        text: [qsTr("Version: %1").arg(root.pendingPackage.version || ""),
                                            qsTr("Repository: %1").arg(root.pendingPackage.source || ""), qsTr("Release tag: %1").arg(root.pendingPackage.tag || ""),
                                            root.pendingPackage.developerSource ? qsTr("Local development package") : "",
                                            root.pendingPackage.distributionSource ? qsTr("Package from the app distribution") : "",
                                            root.pendingPackage.publisherVerified === true ? qsTr("Publisher verified") : qsTr("Publisher not verified")].filter(value => value.length > 0).join("\n")
                                        textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: qsTr("This add-on needs permission to:"); textFormat: Text.PlainText; wrapMode: Text.Wrap
                                        color: root.theme.bright; font.pixelSize: root.theme.dp(14)
                                    }
                                    Text {
                                        objectName: "addonInstallPermissionSummary"
                                        Layout.fillWidth: true
                                        text: root.permissionSummaries(root.pendingPackage.permissions || {})
                                        textFormat: Text.PlainText; wrapMode: Text.Wrap
                                        color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
                                    }
                                    Text {
                                        objectName: "addonInstallCapabilityLimits"
                                        Layout.fillWidth: true; visible: text.length > 0
                                        text: root.capabilityLimits(root.hostCapabilities, root.pendingPackage.permissions || {}, root.pendingPackage.workerRequired === true)
                                        textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
                                    }
                                    ActionButton {
                                        objectName: "addonInstallTechnicalDetails"
                                        caption: root.showTechnicalDetails ? qsTr("Hide technical details") : qsTr("Technical details")
                                        onClicked: root.showTechnicalDetails = !root.showTechnicalDetails
                                    }
                                    Text {
                                        objectName: "addonInstallIdentity"
                                        Layout.fillWidth: true; visible: root.showTechnicalDetails
                                        text: [root.pendingPackage.id || "", qsTr("Package digest: %1").arg(root.pendingPackage.digest || "")].join("\n")
                                        textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere
                                        color: root.theme.foreground; font.pixelSize: root.theme.dp(12)
                                    }
                                    C.ScrollView {
                                        id: permissionsScroll
                                        visible: root.showTechnicalDetails
                                        Layout.fillWidth: true; Layout.preferredHeight: Math.min(permissions.contentHeight + root.theme.dp(8), root.theme.dp(180))
                                        contentWidth: availableWidth; clip: true
                                        C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
                                        TextEdit {
                                            id: permissions
                                            objectName: "addonInstallPermissions"
                                            width: permissionsScroll.availableWidth
                                            text: JSON.stringify(root.pendingPackage.permissions || {}, null, 2)
                                            textFormat: TextEdit.PlainText; wrapMode: TextEdit.WrapAnywhere
                                            readOnly: true; selectByMouse: true
                                            color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
                                            Accessible.name: qsTr("Requested add-on permissions")
                                        }
                                    }
                                    RedesignSwitchRow {
                                        objectName: "addonInstallEnable"
                                        Layout.fillWidth: true
                                        label: root.updatingInstalledPackage ? qsTr("Enable if disabled") : qsTr("Enable after installation"); checked: root.enableAfterInstall
                                        enabled: !root.busy && root.candidateCanRun && root.backend.safeMode !== true && root.hostCapabilities.safeMode !== true
                                        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                                        onToggled: checked => root.enableAfterInstall = checked
                                    }
                                    Flow {
                                        Layout.fillWidth: true; spacing: root.theme.dp(8)
                                        ActionButton {
                                            objectName: "addonInstallApprove"
                                            caption: qsTr("Give approval and install"); primaryAction: true
                                            enabled: !root.busy && root.permissionsKnown && (!root.enableAfterInstall || root.candidateCanRun)
                                            onClicked: root.backend.approvePendingPackage(root.enableAfterInstall && root.backend.safeMode !== true)
                                        }
                                        ActionButton {
                                            objectName: "addonInstallCancel"
                                            caption: qsTr("Cancel")
                                            enabled: !root.busy
                                            onClicked: root.backend.discardPendingPackage()
                                        }
                                    }
                                }
                            }
                    }
                }
                Text {
                    Layout.fillWidth: true; visible: !!root.backend.errorMessage
                    text: root.backend.errorMessage || ""; textFormat: Text.PlainText; wrapMode: Text.Wrap
                    color: root.theme.danger; font.pixelSize: root.theme.dp(13); Accessible.role: Accessible.AlertMessage
                }
                RowLayout {
                    Layout.fillWidth: true
                    C.BusyIndicator { running: root.busy; visible: running; Layout.preferredWidth: root.theme.dp(20); Layout.preferredHeight: root.theme.dp(20) }
                    Text { Layout.fillWidth: true; visible: root.busy; text: qsTr("The app prepares the extension…"); textFormat: Text.PlainText; color: root.theme.foreground; font.pixelSize: root.theme.dp(13); wrapMode: Text.Wrap }
                    Item { Layout.fillWidth: true; visible: !root.busy }
                    ActionButton { objectName: "addonInstallRequestCancel"; caption: qsTr("Cancel request"); visible: root.busy && !root.removalInProgress; onClicked: root.backend.discardPendingPackage() }
                    ActionButton { caption: qsTr("Close"); onClicked: installer.close() }
                }
            }
        }
    }
    Loader {
        id: recoveryLoader
        active: false
        onLoaded: item.open()
        sourceComponent: C.Dialog {
            id: recoveryDialog
            objectName: "addonRecoveryDialog"
            title: qsTr("Recovery and saved data")
            parent: root.Window.contentItem || root
            popupType: C.Popup.Item
            x: Math.max(0, (parent.width - width) / 2); y: Math.max(0, (parent.height - height) / 2)
            width: Math.max(0, Math.min(root.theme.dp(620), parent.width - root.theme.dp(32)))
            height: Math.min(implicitHeight, Math.max(0, parent.height - root.theme.dp(32)))
            padding: root.theme.dp(24); modal: true; dim: true; focus: true
            header: null; footer: null; closePolicy: C.Popup.CloseOnEscape
            onClosed: recoveryLoader.active = false
            background: RedesignDialogSurface { theme: root.theme; videoBackdropContent: root.videoBackdropContent }
            C.Overlay.modal: Rectangle { color: "#80000000" }
            contentItem: ColumnLayout {
                Accessible.role: Accessible.Dialog
                Accessible.name: recoveryDialog.title
                spacing: root.theme.dp(12)
                Text { Layout.fillWidth: true; text: qsTr("Recovery and saved data"); textFormat: Text.PlainText; color: root.theme.bright; font.pixelSize: root.theme.dp(20) }
                C.ScrollView {
                    id: recoveryScroll
                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 0
                    implicitHeight: recoveryContent.implicitHeight
                    contentWidth: availableWidth; clip: true
                    C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
                    RedesignAddonRecovery {
                        id: recoveryContent
                        objectName: "addonManagementRecovery"
                        width: recoveryScroll.availableWidth
                        backend: root.backend; packageSnapshot: root.packages
                        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                        videoBackdropContent: root.videoBackdropContent
                    }
                }
                ActionButton { Layout.alignment: Qt.AlignRight; caption: qsTr("Close"); onClicked: recoveryDialog.close() }
            }
        }
    }
    RedesignConfirmationDialog {
        id: changePackage
        objectName: "addonPackageChangeDialog"
        parent: root.Window.contentItem || root
        property string packageId: ""
        property string operation: ""
        property string packageState: ""
        property int revision: -1
        title: operation === "rollback" ? qsTr("Use the previous add-on version?") : operation === "removeData" ? qsTr("Delete add-on and saved data?") : qsTr("Remove add-on?")
        message: operation === "rollback" ? qsTr("The app will use the previous version with its saved settings and private data.")
            : operation === "removeData" ? qsTr("The app will remove the add-on and delete its saved settings, saved keys, and private data.")
            : qsTr("The app will remove the add-on. The app will keep its saved settings, saved keys, and private data.")
        confirmText: operation === "rollback" ? qsTr("Use previous version") : operation === "removeData" ? qsTr("Delete add-on and saved data") : qsTr("Remove")
        destructive: operation !== "rollback"
        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
        videoBackdropContent: root.videoBackdropContent
        onAccepted: {
            const row = root.packages.find(entry => entry.id === packageId)
            if (root.busy || !row || root.backend.revision !== revision || root.packageSignature(row) !== packageState) {
                root.actionError = qsTr("The add-on changed. Open this action again."); return
            }
            if (operation === "rollback") root.backend.rollback(packageId)
            else root.backend.uninstall(packageId, operation === "removeData")
        }
    }
    component ActionButton: RedesignButton {
        captionMaximumWidth: Math.max(0, root.width - root.theme.dp(48))
        text: caption; captionTextFormat: Text.PlainText; tooltipTextFormat: Text.PlainText
        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
    }
    component PackageInput: C.TextField {
        Layout.fillWidth: true; implicitHeight: root.theme.dp(36)
        color: root.theme.foreground; placeholderTextColor: root.theme.foreground
        font.pixelSize: root.theme.dp(14); selectByMouse: true
        background: Rectangle { color: root.theme.field; radius: root.theme.dp(6); border.width: 1; border.color: parent.activeFocus ? root.theme.accent : root.theme.border }
    }
}

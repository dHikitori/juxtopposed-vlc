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
    property bool developerSource: false
    property bool enableAfterInstall: false
    property bool showTechnicalDetails: false
    readonly property bool busy: backend.busy === true
    readonly property var pendingPackage: backend.pendingPackage || ({})
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme }
    readonly property var packages: { backend.revision; return backend.packages || [] }
    readonly property var updateResult: backend.updateResult || ({})
    readonly property var hostCapabilities: pendingPackage.hostCapabilities || backend.capabilities || ({})
    readonly property bool updatingInstalledPackage: !!pendingPackage.id && packages.some(entry => entry.id === pendingPackage.id && entry.installed !== false)
    readonly property bool candidateCanRun: pendingPackage.workerRequired !== true || hostCapabilities.workerAvailable !== false
    spacing: theme.dp(10)
    onPendingPackageChanged: { enableAfterInstall = false; showTechnicalDetails = false }
    readonly property var knownPermissions: ["metadata.context", "media.fingerprint", "network.http", "network.websocket", "credentials.use", "storage.private", "library.read", "queue.read", "queue.modify", "playback.observe", "playback.control", "audio.analysis", "ui.contribute", "session.background"]
    readonly property bool permissionsKnown: {
        const permissions = pendingPackage.permissions || {}
        if (!Object.keys(permissions).every(name => knownPermissions.includes(name))) return false
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
    function configure(id) {
        const entry = packages.find(item => item.id === id)
        if (!entry) return
        configuration.packageId = id; configuration.packageName = entry.name
        configuration.open()
    }
    function capabilityLimits(capabilities, permissions, workerRequired) {
        const limits = []
        if (workerRequired && capabilities.workerAvailable === false) limits.push(qsTr("This app cannot run add-on code."))
        if (permissions["network.http"] && capabilities.networkHttpAvailable === false) limits.push(qsTr("This app cannot connect to online services for add-ons."))
        if (permissions["storage.private"] && capabilities.privateStorageAvailable === false) limits.push(qsTr("This app does not support private add-on storage."))
        if (permissions["network.websocket"] && capabilities.webSocketAvailable === false) limits.push(qsTr("This app cannot keep a live service connection for add-ons."))
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
        text: qsTr("Install an add-on"); textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: root.theme.settingsHeading; font.pixelSize: root.theme.dp(16)
    }
    Text {
        Layout.fillWidth: true
        text: qsTr("Enter a Git repository and an exact release tag. Before installation, do a check of the package permissions.")
        textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
    }
    PackageInput {
        id: source
        objectName: "addonRepositorySource"
        placeholderText: qsTr("Repository URL (HTTPS)"); Accessible.name: placeholderText
        maximumLength: 2048
        readOnly: root.busy || !!root.pendingPackage.id
        visible: !root.developerSource
    }
    PackageInput {
        id: tag
        objectName: "addonRepositoryTag"
        placeholderText: qsTr("Release tag (for example, v1.0.0)"); Accessible.name: placeholderText
        maximumLength: 128
        readOnly: root.busy || !!root.pendingPackage.id
        visible: !root.developerSource
    }
    RedesignSwitchRow {
        objectName: "addonDeveloperSource"
        Layout.fillWidth: true
        label: qsTr("Use a local development package")
        checked: root.developerSource; enabled: !root.busy && !root.pendingPackage.id
        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
        onToggled: checked => root.developerSource = checked
    }
    PackageInput {
        id: directory
        objectName: "addonLocalDirectory"
        placeholderText: qsTr("Package folder"); Accessible.name: placeholderText
        maximumLength: 4096
        visible: root.developerSource; readOnly: root.busy || !!root.pendingPackage.id
    }
    ActionButton {
        objectName: "addonStagePackage"
        caption: root.busy ? qsTr("Package preparation") : qsTr("Prepare package")
        enabled: !root.busy && !root.pendingPackage.id && (root.developerSource
            ? directory.text.trim().length > 0 && typeof root.backend.stageLocalPackage === "function"
            : /^https:\/\/[^\s/]+\/[^\s]+$/i.test(source.text.trim()) && tag.text.trim().length > 0 && typeof root.backend.stageRepository === "function")
        onClicked: root.developerSource ? root.backend.stageLocalPackage(directory.text.trim()) : root.backend.stageRepository(source.text.trim(), tag.text.trim())
    }
    ActionButton {
        objectName: "addonRepositoryCancel"
        caption: qsTr("Cancel request"); visible: root.busy
        onClicked: root.backend.discardPendingPackage()
    }
    Text {
        objectName: "addonUpdateStatus"
        Layout.fillWidth: true; visible: !!root.updateResult.packageId
        text: root.updateResult.checking ? qsTr("Update search in progress.")
            : root.updateResult.canceled ? qsTr("VLC canceled the update search.")
            : root.backend.errorMessage ? root.backend.errorMessage
            : root.updateResult.available ? qsTr("An update is ready for approval. VLC keeps the installed release until you give approval.")
            : qsTr("No compatible stable update is available.")
        textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
    }
    Rectangle {
        objectName: "addonInstallReview"
        Layout.fillWidth: true; visible: !!root.pendingPackage.id
        implicitHeight: review.implicitHeight + root.theme.dp(24)
        color: root.theme.field; radius: root.theme.dp(6)
        ColumnLayout {
            id: review
            anchors.fill: parent; anchors.margins: root.theme.dp(12)
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
                enabled: !root.busy && root.candidateCanRun && root.hostCapabilities.safeMode !== true
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onToggled: checked => root.enableAfterInstall = checked
            }
            Flow {
                Layout.fillWidth: true; spacing: root.theme.dp(8)
                ActionButton {
                    objectName: "addonInstallApprove"
                    caption: qsTr("Give approval and install")
                    enabled: !root.busy && root.permissionsKnown && (!root.enableAfterInstall || root.candidateCanRun)
                    onClicked: root.backend.approvePendingPackage(root.enableAfterInstall)
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
    Text {
        Layout.fillWidth: true; Layout.topMargin: root.theme.dp(8)
        text: qsTr("Installed add-ons"); textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: root.theme.settingsHeading; font.pixelSize: root.theme.dp(16)
    }
    Repeater {
        model: root.packages
        Rectangle {
            id: row
            required property var modelData
            objectName: "addonPackage_" + modelData.id
            Layout.fillWidth: true
            implicitHeight: content.implicitHeight + root.theme.dp(20)
            color: root.theme.field; radius: root.theme.dp(6)
            ColumnLayout {
                id: content
                anchors.fill: parent; anchors.margins: root.theme.dp(10)
                spacing: root.theme.dp(6)
                Text { Layout.fillWidth: true; text: row.modelData.name; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.bright; font.pixelSize: root.theme.dp(15) }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Version %1 · %2").arg(row.modelData.version).arg(row.modelData.configured === false ? qsTr("Not configured") : row.modelData.enabled ? qsTr("Enabled") : qsTr("Disabled"))
                    textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
                }
                RedesignSwitchRow {
                    Layout.fillWidth: true
                    label: qsTr("Enabled"); accessibleName: qsTr("Enable %1").arg(row.modelData.name)
                    checked: row.modelData.enabled === true; enabled: !root.busy
                    scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                    onToggled: checked => root.backend.setEnabled(row.modelData.id, checked)
                }
                Flow {
                    Layout.fillWidth: true; spacing: root.theme.dp(6)
                    ActionButton {
                        objectName: "addonPackageConfigure_" + row.modelData.id
                        caption: qsTr("Configure…"); text: qsTr("Configure %1").arg(row.modelData.name)
                        enabled: !root.busy
                        onClicked: root.configure(row.modelData.id)
                    }
                    ActionButton {
                        objectName: "addonPackageFindUpdates_" + row.modelData.id
                        caption: qsTr("Find updates")
                        visible: row.modelData.installed !== false && row.modelData.developerSource !== true && row.modelData.distributionSource !== true
                            && /^https:\/\//i.test(row.modelData.source || "") && typeof root.backend.checkRepositoryUpdates === "function"
                        enabled: !root.busy && !root.pendingPackage.id
                        onClicked: root.backend.checkRepositoryUpdates(row.modelData.id, "^" + row.modelData.version, false)
                    }
                    ActionButton {
                        objectName: "addonPackageRollback_" + row.modelData.id
                        caption: qsTr("Use previous version")
                        visible: row.modelData.hasPrevious === true; enabled: !root.busy
                        onClicked: { changePackage.packageId = row.modelData.id; changePackage.operation = "rollback"; changePackage.open() }
                    }
                    ActionButton {
                        objectName: "addonPackageRemove_" + row.modelData.id
                        caption: qsTr("Remove")
                        enabled: !root.busy
                        onClicked: { changePackage.packageId = row.modelData.id; changePackage.operation = "remove"; changePackage.open() }
                    }
                }
            }
        }
    }
    Text {
        Layout.fillWidth: true; visible: !root.packages.length
        text: qsTr("No add-ons from repositories are installed."); textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: root.theme.foreground; font.pixelSize: root.theme.dp(14)
    }
    Text {
        objectName: "addonManagementError"
        Layout.fillWidth: true; visible: text.length > 0
        text: root.backend.errorMessage || ""; textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: root.theme.danger; font.pixelSize: root.theme.dp(13)
        Accessible.role: Accessible.AlertMessage
    }
    RedesignAddonSettingsDialog {
        id: configuration
        objectName: "addonManagementSettingsDialog"
        parent: root.Window.contentItem
        backend: root.backend
        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
        videoBackdropContent: root.videoBackdropContent
    }
    RedesignConfirmationDialog {
        id: changePackage
        objectName: "addonPackageChangeDialog"
        parent: root.Window.contentItem
        property string packageId: ""
        property string operation: ""
        title: operation === "rollback" ? qsTr("Use the previous add-on version?") : qsTr("Remove add-on?")
        message: operation === "rollback" ? qsTr("The app will use the previous version. The app will use the settings snapshot for that version.")
            : qsTr("The app will disable and remove the add-on. The app will keep its settings.")
        confirmText: operation === "rollback" ? qsTr("Use previous version") : qsTr("Remove")
        destructive: operation === "remove"
        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
        videoBackdropContent: root.videoBackdropContent
        onAccepted: operation === "rollback" ? root.backend.rollback(packageId) : root.backend.uninstall(packageId, false)
    }
    component ActionButton: RedesignButton {
        captionMaximumWidth: Math.max(0, root.width - root.theme.dp(48))
        text: caption; tooltipTextFormat: Text.PlainText
        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
    }
    component PackageInput: C.TextField {
        Layout.fillWidth: true; implicitHeight: root.theme.dp(36)
        color: root.theme.foreground; placeholderTextColor: root.theme.foreground
        font.pixelSize: root.theme.dp(14); selectByMouse: true
        background: Rectangle { color: root.theme.field; radius: root.theme.dp(6); border.width: 1; border.color: parent.activeFocus ? root.theme.accent : root.theme.border }
    }
}

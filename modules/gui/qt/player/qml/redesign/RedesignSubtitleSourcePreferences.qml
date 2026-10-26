// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Window
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var sourceService
    property real scaleFactor: 1
    property bool lightTheme: false
    property Component videoBackdropContent
    // The host supplies the manager; standalone dialogs/tests can omit it.
    property var addonManager: typeof AddonManager !== "undefined" ? AddonManager : null
    property color accentColor: "#d37918"
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    spacing: theme.dp(10)
    Text { text: qsTr("Online Subtitles"); color: root.theme.settingsHeading; font.pixelSize: root.theme.dp(16); Layout.topMargin: root.theme.dp(24) }
    Text {
        objectName: "subtitleProviderStatus"
        Layout.fillWidth: true
        text: root.sourceService.hasCustomKey ? qsTr("OpenSubtitles uses your saved API key.")
            : root.sourceService.hasConsumerKey ? qsTr("OpenSubtitles uses the app key. Downloads have a daily limit for each IP address.")
            : qsTr("Add an API key to search for and download subtitles from OpenSubtitles.")
        wrapMode: Text.WordWrap; color: root.theme.foreground; font.pixelSize: root.theme.dp(14)
    }
    RedesignButton {
        objectName: "subtitleProviderConfigure"
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        caption: root.sourceService.hasCustomKey ? qsTr("Change API key") : qsTr("Set up OpenSubtitles"); text: caption
        enabled: !root.sourceService.preparing
        onClicked: setup.open()
    }
    Loader {
        Layout.fillWidth: true
        active: !!root.addonManager
        sourceComponent: ColumnLayout {
            spacing: root.theme.dp(8)
            readonly property var providers: { root.addonManager.revision; return root.addonManager.providersForFeature("subtitles") }
            RedesignAddonProviders {
                Layout.fillWidth: true
                providers: parent.providers
                feature: "subtitles"; heading: qsTr("Subtitle providers")
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                onEnabledRequested: (id, enabled) => {
                    const provider = providers.find(entry => entry.id === id)
                    if (provider) root.addonManager.setEnabled(provider.packageId, enabled)
                }
                onMoveRequested: (id, direction) => {
                    const ids = providers.map(entry => entry.id)
                    const from = ids.indexOf(id), to = from + direction
                    if (from < 0 || to < 0 || to >= ids.length) return
                    const swap = ids[to]; ids[to] = ids[from]; ids[from] = swap
                    root.addonManager.setProviderOrder("subtitles", ids)
                }
                onConfigureRequested: packageId => {
                    const provider = providers.find(entry => entry.packageId === packageId)
                    if (provider && provider.builtin) { setup.open(); return }
                    addonConfiguration.packageId = packageId
                    addonConfiguration.packageName = provider ? provider.name : packageId
                    addonConfiguration.open()
                }
            }
            Text {
                Layout.fillWidth: true
                text: root.addonManager.errorMessage || ""; textFormat: Text.PlainText; wrapMode: Text.Wrap
                visible: text.length > 0; color: root.theme.danger; font.pixelSize: root.theme.dp(13)
                Accessible.role: Accessible.AlertMessage
            }
            RedesignAddonSettingsDialog {
                id: addonConfiguration
                objectName: "subtitleAddonSettingsDialog"
                parent: root.Window.contentItem
                backend: root.addonManager
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                videoBackdropContent: root.videoBackdropContent
            }
        }
    }
    RedesignSubtitleProviderSetup {
        id: setup
        objectName: "subtitlePreferencesProviderSetup"
        videoBackdropContent: root.videoBackdropContent
        parent: root.Window.contentItem
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        hasCustomKey: root.sourceService.hasCustomKey; hasConsumerKey: root.sourceService.hasConsumerKey
        busy: root.sourceService.preparing
        onSaveRequested: key => finish(root.sourceService.setApiKey(key))
        onRemoveRequested: finish(root.sourceService.clearApiKey())
        onHelpRequested: Qt.openUrlExternally("https://www.opensubtitles.com/consumers")
    }
}

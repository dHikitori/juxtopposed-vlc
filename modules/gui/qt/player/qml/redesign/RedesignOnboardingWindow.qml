// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Window
import VLC.MainInterface
import VLC.Style
import VLC.Dialogs

Window {
    id: root
    title: qsTr("Welcome — Hakone Media Player")
    width: Math.min(1114 * VLCStyle.scale, Screen.desktopAvailableWidth - 40)
    height: Math.min(783 * VLCStyle.scale, Screen.desktopAvailableHeight - 40)
    minimumWidth: Math.min(860 * VLCStyle.scale, Screen.desktopAvailableWidth - 40)
    minimumHeight: Math.min(600 * VLCStyle.scale, Screen.desktopAvailableHeight - 40)
    flags: Qt.Dialog | Qt.FramelessWindowHint
    modality: Qt.ApplicationModal
    transientParent: MainCtx.intfMainWindow
    color: "transparent"
    visible: DialogsProvider.firstRunDialogVisible
    RedesignFirstRunSetup { id: setup; ctx: MainCtx }
    onVisibleChanged: if (visible) { raise(); requestActivate(); view.forceActiveFocus() }
    onClosing: event => { event.accepted = false; view.dismiss() }
    MouseArea { anchors.top: parent.top; width: parent.width; height: 40 * VLCStyle.scale; onPressed: root.startSystemMove() }
    RedesignOnboardingView {
        id: view
        anchors.fill: parent; anchors.margins: 12 * VLCStyle.scale
        anchors.topMargin: 8 * VLCStyle.scale; anchors.bottomMargin: 16 * VLCStyle.scale
        backend: setup
        systemLight: !VLCStyle.palette.isDark
        onFinished: DialogsProvider.dismissFirstRunDialog()
        onDismissRequested: DialogsProvider.dismissFirstRunDialog()
    }
}

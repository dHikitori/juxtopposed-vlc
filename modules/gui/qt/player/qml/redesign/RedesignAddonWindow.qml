// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

C.Popup {
    id: root
    required property var registry
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    property string contributionId: ""
    property string modalTicket: ""
    property var parameters: ({})
    property string title: ""
    property string errorMessage: ""
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme }
    parent: C.Overlay.overlay
    x: Math.max(theme.dp(12), (parent.width - width) / 2)
    y: Math.max(theme.dp(12), (parent.height - height) / 2)
    width: Math.min(theme.dp(900), parent.width - theme.dp(24))
    height: Math.min(theme.dp(700), parent.height - theme.dp(24))
    padding: theme.dp(16)
    closePolicy: C.Popup.CloseOnEscape
    focus: true
    modal: modalTicket.length > 0
    function showContribution(id, input, ticket) {
        if (visible) close()
        contributionId = id; parameters = input; modalTicket = ticket || ""; errorMessage = ""
        title = id
        const route = (registry.routes || []).find(entry => entry.id === id)
        if (route) title = route.name || id
        open()
    }
    onClosed: {
        mount.release()
        if (modalTicket.length) registry.cancelModal(modalTicket)
        modalTicket = ""; contributionId = ""; parameters = ({})
    }
    Connections {
        target: root.registry
        function onRouteRequested(route, parameters) {
            const entry = (root.registry.routes || []).find(value => value.route === route)
            if (entry) root.showContribution(entry.id, parameters, "")
        }
        function onModalSessionRequested(ticket, contribution, parameters) { root.showContribution(contribution, parameters, ticket) }
        function onModalFinished(ticket, accepted, result) { if (ticket === root.modalTicket) { root.modalTicket = ""; root.close() } }
        function onChanged() { if (root.visible && !root.registry.view(root.contributionId).id) root.close() }
    }
    background: RedesignDialogSurface { theme: root.theme }
    contentItem: ColumnLayout {
        spacing: root.theme.dp(12)
        RowLayout {
            Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: root.title; textFormat: Text.PlainText; elide: Text.ElideRight; color: root.theme.bright; font.pixelSize: root.theme.dp(18) }
            RedesignButton {
                visible: root.modal
                caption: qsTr("Confirm"); text: caption
                enabled: mount.usable
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: {
                    if (!root.registry.completeModal(root.modalTicket, mount.descriptor.state.result || ({})))
                        root.errorMessage = qsTr("The add-on result does not agree with its schema.")
                }
            }
            RedesignButton {
                caption: root.modal ? qsTr("Cancel") : qsTr("Back"); text: caption
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.close()
            }
        }
        C.ScrollView {
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            RedesignAddonMount {
                id: mount
                width: parent.width; height: implicitHeight
                visible: root.visible
                registry: root.registry; contributionId: root.contributionId
                parameters: root.parameters; hostContext: ({surface: root.modal ? "ui.modal" : "library.navigation@1"})
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onFailed: root.errorMessage = qsTr("The add-on view is not available. Use Back or Cancel to return.")
            }
        }
        Text {
            Layout.fillWidth: true; visible: text.length > 0
            text: root.errorMessage; textFormat: Text.PlainText; wrapMode: Text.Wrap
            color: root.theme.danger; font.pixelSize: root.theme.dp(13)
            Accessible.role: Accessible.AlertMessage
        }
    }
}

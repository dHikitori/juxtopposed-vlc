// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

ColumnLayout {
    id: root
    required property var backend
    property real scaleFactor: 1
    property bool lightTheme: false
    property color accentColor: "#d37918"
    property string selectedId: ""
    property string actionMessage: ""
    property bool discoveryRequested: false
    signal websiteRequested(string url)
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    readonly property var selectedAddon: { backend.revision; return selectedId.length ? backend.addonById(selectedId) : ({}) }
    readonly property bool canAdd: !backend.loading && selectedAddon.state === 0 && selectedAddon.manageable && !selectedAddon.broken && !selectedAddon.pending
    readonly property bool canRemove: !backend.loading && selectedAddon.state === 2 && selectedAddon.manageable && !selectedAddon.pending
    readonly property string website: String(selectedAddon.website || "").trim()
    readonly property bool canOpenWebsite: /^https?:\/\/[^/\s]+(?:[/?#]|$)/i.test(website)
    spacing: theme.dp(6)
    function addSelected() {
        if (canAdd) actionMessage = backend.installAddon(selectedId) ? "" : qsTr("The app cannot add this add-on.")
    }
    function removeSelected() {
        if (canRemove) actionMessage = backend.removeAddon(selectedId) ? "" : qsTr("The app cannot remove this add-on.")
    }
    Text { text: qsTr("Add-ons"); color: theme.settingsHeading; font.pixelSize: theme.dp(16); font.weight: Font.Medium; Layout.topMargin: theme.dp(8); Layout.bottomMargin: theme.dp(3) }
    Rectangle {
        objectName: "addonsFrame"
        Layout.fillWidth: true; Layout.preferredHeight: theme.dp(194)
        color: theme.field; radius: theme.dp(6); border.width: theme.dp(2)
        border.color: list.activeFocus ? theme.accent : root.lightTheme ? theme.border : "#191919"
        C.ScrollView {
            anchors.fill: parent; anchors.margins: theme.dp(2)
            clip: true; contentWidth: availableWidth
            ListView {
                id: list
                objectName: "addonsList"
                model: root.backend
                currentIndex: -1
                boundsBehavior: Flickable.StopAtBounds
                Keys.onPressed: event => {
                    if (![Qt.Key_Up, Qt.Key_Down, Qt.Key_Home, Qt.Key_End].includes(event.key) || !count) return
                    currentIndex = event.key === Qt.Key_Home ? 0 : event.key === Qt.Key_End ? count - 1
                        : Math.max(0, Math.min(count - 1, currentIndex + (event.key === Qt.Key_Up ? -1 : 1)))
                    positionViewAtIndex(currentIndex, ListView.Contain)
                    forceLayout()
                    if (currentItem) root.selectedId = currentItem.addonId
                    event.accepted = true
                }
                delegate: C.ItemDelegate {
                    id: row
                    required property int index
                    required property var model
                    readonly property string addonId: model.identifier || ""
                    objectName: "addonRow_" + index
                    width: ListView.view.width; height: theme.dp(58)
                    leftPadding: theme.dp(14); rightPadding: theme.dp(14)
                    highlighted: root.selectedId.length > 0 && root.selectedId === addonId
                    Accessible.name: model.name || ""
                    background: Rectangle { color: row.highlighted ? theme.selected : row.hovered ? theme.hover : "transparent"; radius: theme.dp(4) }
                    contentItem: RowLayout {
                        spacing: theme.dp(10)
                        RedesignIcon { name: "nav-plugins"; color: theme.foreground; Layout.preferredWidth: theme.dp(24); Layout.preferredHeight: width }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: theme.dp(3)
                            Text { Layout.fillWidth: true; text: row.model.name || ""; textFormat: Text.PlainText; color: theme.foreground; font.pixelSize: theme.dp(14); elide: Text.ElideRight }
                            Text { Layout.fillWidth: true; visible: text.length > 0; text: row.model.summary || ""; textFormat: Text.PlainText; color: theme.playerLabel; font.pixelSize: theme.dp(12); elide: Text.ElideRight }
                        }
                        Text {
                            text: row.model.state === 1 ? qsTr("Adding…") : row.model.state === 3 ? qsTr("Removing…")
                                : row.model.state === 2 ? qsTr("Installed") : row.model.broken ? qsTr("Unavailable") : ""
                            textFormat: Text.PlainText; color: theme.playerLabel; font.pixelSize: theme.dp(12)
                        }
                    }
                    onClicked: { list.currentIndex = index; root.selectedId = addonId; root.actionMessage = ""; list.forceActiveFocus() }
                    RedesignToolTip { theme: root.theme; text: row.model.description || row.model.summary || ""; contentTextFormat: Text.PlainText; visible: row.hovered && text.length > 0 }
                }
            }
        }
        Text {
            objectName: "addonsEmptyState"
            anchors.centerIn: parent
            visible: !list.count && !root.actionMessage && !root.backend.errorMessage
                && (root.backend.loading || root.discoveryRequested || search.text.length > 0)
            text: root.backend.loading ? qsTr("Loading add-ons…") : qsTr("No add-ons found.")
            color: theme.playerLabel; font.pixelSize: theme.dp(14)
        }
    }
    GridLayout {
        readonly property bool wide: root.width >= theme.dp(620)
        Layout.fillWidth: true; columns: wide ? 5 : 2; columnSpacing: theme.dp(10); rowSpacing: theme.dp(8)
        C.TextField {
            id: search
            objectName: "addonsSearch"
            Layout.columnSpan: parent.wide ? 1 : 2
            Layout.preferredWidth: theme.dp(210); Layout.minimumWidth: theme.dp(90); Layout.fillWidth: !parent.wide
            implicitHeight: theme.dp(36)
            placeholderText: qsTr("Search"); placeholderTextColor: root.lightTheme ? theme.foreground : "#404040"
            color: theme.foreground; font.pixelSize: theme.dp(14); selectByMouse: true
            Accessible.name: qsTr("Search add-ons")
            background: Rectangle { color: theme.field; radius: theme.dp(6); border.width: theme.dp(2); border.color: search.activeFocus ? theme.accent : root.lightTheme ? theme.border : "#191919" }
            onTextEdited: { root.selectedId = ""; root.actionMessage = ""; root.backend.searchPattern = text }
        }
        Item { Layout.fillWidth: true; visible: parent.wide }
        ActionButton {
            objectName: "addonsFindMore"
            Layout.preferredWidth: theme.dp(185)
            Layout.columnSpan: parent.wide ? 1 : 2; Layout.fillWidth: !parent.wide
            caption: root.backend.loading ? qsTr("Loading…") : qsTr("Find more extensions"); enabled: !root.backend.loading
            onClicked: { root.actionMessage = ""; root.discoveryRequested = true; root.backend.loadFromDefaultRepository() }
        }
        ActionButton { objectName: "addonsAdd"; Layout.fillWidth: !parent.wide; caption: qsTr("Add"); enabled: root.canAdd; onClicked: root.addSelected() }
        ActionButton { objectName: "addonsRemove"; Layout.fillWidth: !parent.wide; caption: qsTr("Remove"); enabled: root.canRemove; onClicked: root.removeSelected() }
    }
    Text {
        objectName: "addonsError"
        Layout.fillWidth: true; visible: text.length > 0
        text: root.actionMessage || root.backend.errorMessage; textFormat: Text.PlainText; color: theme.danger; font.pixelSize: theme.dp(13); wrapMode: Text.WordWrap
        Accessible.role: Accessible.AlertMessage
    }
    Rectangle {
        objectName: "addonDetails"
        visible: !!root.selectedAddon.identifier
        Layout.fillWidth: true; Layout.minimumWidth: 0
        implicitHeight: header.implicitHeight + Math.min(detailsContent.implicitHeight, theme.dp(260)) + theme.dp(28)
        color: theme.field; radius: theme.dp(6); border.width: theme.dp(2)
        border.color: root.lightTheme ? theme.border : "#191919"
        ColumnLayout {
            anchors.fill: parent; anchors.margins: theme.dp(12); spacing: theme.dp(4)
            RowLayout {
                id: header
                Layout.fillWidth: true; spacing: theme.dp(8)
                Text {
                    objectName: "addonDetailsName"
                    Layout.fillWidth: true; Layout.minimumWidth: 0
                    text: root.selectedAddon.name || ""; textFormat: Text.PlainText
                    color: theme.settingsHeading; font.pixelSize: theme.dp(16); font.weight: Font.Medium; wrapMode: Text.Wrap
                }
                RedesignButton {
                    objectName: "addonDetailsClose"
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    iconName: "close"; text: qsTr("Close add-on details")
                    onClicked: { root.selectedId = ""; list.currentIndex = -1; list.forceActiveFocus() }
                }
            }
            C.ScrollView {
                id: detailsScroll
                objectName: "addonDetailsScroll"
                Layout.fillWidth: true; Layout.fillHeight: true
                clip: true; contentWidth: availableWidth
                C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
                ColumnLayout {
                    id: detailsContent
                    width: detailsScroll.availableWidth; spacing: theme.dp(10)
                    Repeater {
                        model: [
                            [qsTr("Type"), root.selectedAddon.typeLabel || ""],
                            [qsTr("Version"), root.selectedAddon.version || ""],
                            [qsTr("Author"), root.selectedAddon.author || ""]].filter(field => field[1].length > 0)
                        RowLayout {
                            required property var modelData
                            Layout.fillWidth: true; spacing: theme.dp(10)
                            Text { Layout.preferredWidth: theme.dp(64); text: modelData[0]; color: theme.playerLabel; font.pixelSize: theme.dp(13); wrapMode: Text.WrapAnywhere }
                            DetailText { Layout.fillWidth: true; Layout.minimumWidth: 0; text: modelData[1] }
                        }
                    }
                    DetailText {
                        objectName: "addonDetailsDescription"
                        Layout.fillWidth: true
                        text: [root.selectedAddon.summary || "", root.selectedAddon.description !== root.selectedAddon.summary ? root.selectedAddon.description || "" : ""].filter(Boolean).join("\n\n")
                        visible: text.length > 0
                    }
                    DetailText {
                        objectName: "addonDetailsManual"
                        Layout.fillWidth: true; visible: root.selectedAddon.manageable === false
                        text: qsTr("The app cannot change this add-on because it was added manually.")
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; visible: (root.selectedAddon.files || []).length > 0; spacing: theme.dp(4)
                        Text { text: qsTr("Files"); color: theme.playerLabel; font.pixelSize: theme.dp(13) }
                        DetailText { objectName: "addonDetailsFiles"; Layout.fillWidth: true; text: (root.selectedAddon.files || []).join("\n") }
                    }
                    ActionButton {
                        objectName: "addonDetailsWebsite"
                        visible: root.canOpenWebsite; Layout.preferredWidth: theme.dp(140)
                        caption: qsTr("Open website"); text: root.website; tooltipTextFormat: Text.PlainText
                        onClicked: root.websiteRequested(root.website)
                    }
                }
            }
        }
    }
    component DetailText: TextEdit {
        readOnly: true; selectByMouse: true; textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap
        color: theme.foreground; font.pixelSize: theme.dp(13)
        selectionColor: theme.accent; selectedTextColor: theme.field
        Layout.preferredHeight: contentHeight
    }
    component ActionButton: RedesignButton {
        Layout.preferredWidth: theme.dp(100); implicitHeight: theme.dp(32)
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        text: caption; captionFontWeight: Font.DemiBold
        background: Rectangle { radius: theme.dp(6); color: parent.hovered ? theme.hover : root.lightTheme ? theme.background : "#191919"; border.color: parent.visualFocus ? theme.accent : "transparent" }
    }
}

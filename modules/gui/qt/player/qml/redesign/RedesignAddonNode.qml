// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

Item {
    id: root
    required property var node
    required property var renderer
    property var itemContext: null
    readonly property var theme: renderer.theme
    readonly property bool horizontal: node.type === "Row"
    objectName: "addonNode_" + (node.id || node.type || "empty")
    implicitHeight: visible && content.item ? content.item.implicitHeight : 0
    visible: node.visible === undefined || renderer.resolve(node.visible, itemContext) === true
    enabled: renderer.valid && (node.enabled === undefined || renderer.resolve(node.enabled, itemContext) === true)
    function text(value) { return renderer.text(value, itemContext) }
    Loader {
        id: content
        width: parent.width; height: parent.implicitHeight
        sourceComponent: {
            switch (root.node.type) {
            case "Page": case "Section": case "Column": return columnComponent
            case "Row": return rowComponent
            case "Heading": case "Text": case "Notice": case "EmptyState": return textComponent
            case "Button": case "IconButton": return buttonComponent
            case "Image": return imageComponent
            case "MediaCard": return cardComponent
            case "List": return listComponent
            case "Progress": return progressComponent
            default: return null
            }
        }
    }
    Component {
        id: columnComponent
        Column {
            spacing: root.theme.dp(root.node.type === "Section" ? 12 : 8)
            Text {
                width: parent.width; visible: text.length > 0
                text: root.text(root.node.title); textFormat: Text.PlainText; wrapMode: Text.Wrap
                color: root.theme.bright; font.pixelSize: root.theme.dp(18); font.weight: Font.Medium
            }
            Repeater {
                model: root.node.children || []
                RedesignAddonChild {
                    required property var modelData
                    width: parent.width; height: implicitHeight
                    node: modelData; renderer: root.renderer; itemContext: root.itemContext
                }
            }
        }
    }
    Component {
        id: rowComponent
        Flow {
            spacing: root.theme.dp(8)
            Repeater {
                model: root.node.children || []
                RedesignAddonChild {
                    required property var modelData
                    width: Math.min(parent.width, root.theme.dp(220)); height: implicitHeight
                    node: modelData; renderer: root.renderer; itemContext: root.itemContext
                }
            }
        }
    }
    Component {
        id: textComponent
        Text {
            text: root.text(root.node.text); textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: root.node.type === "Heading" ? root.theme.bright : root.theme.foreground
            font.pixelSize: root.theme.dp(root.node.type === "Heading" ? 18 : 14)
            font.weight: root.node.type === "Heading" ? Font.Medium : Font.Normal
            Accessible.role: root.node.type === "Notice" ? Accessible.AlertMessage : Accessible.StaticText
        }
    }
    Component {
        id: buttonComponent
        RedesignButton {
            id: actionButton
            caption: root.node.type === "IconButton" ? "" : root.text(root.node.text)
            text: root.text(root.node.text); tooltipTextFormat: Text.PlainText
            iconName: root.node.icon || ""
            primaryAction: root.node.intent === "primary"
            foreground: root.node.intent === "destructive" ? root.theme.danger : root.node.intent === "primary" ? root.theme.accent : root.theme.foreground
            scaleFactor: root.renderer.scaleFactor; lightTheme: root.renderer.lightTheme; accentColor: root.renderer.accentColor
            onClicked: root.renderer.dispatch(root.node.action, root.itemContext)
            contentItem: Row {
                spacing: root.theme.dp(6)
                RedesignIcon {
                    id: actionIcon
                    width: root.node.icon ? root.theme.dp(24) : 0; height: root.theme.dp(24)
                    visible: !!root.node.icon; name: root.node.icon || ""; color: actionButton.foreground
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    objectName: "addonActionLabel"
                    visible: root.node.type !== "IconButton"
                    width: Math.min(implicitWidth, Math.max(0, root.width - actionIcon.width - root.theme.dp(32)))
                    text: root.text(root.node.text); textFormat: Text.PlainText; elide: Text.ElideRight
                    font.pixelSize: root.theme.dp(14); color: actionButton.foreground
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }
    Component {
        id: imageComponent
        Item {
            implicitHeight: root.theme.dp(160)
            Accessible.role: Accessible.Graphic; Accessible.name: root.text(root.node.text)
            Image {
                anchors.fill: parent
                source: root.renderer.assetFor(root.text(root.node.artwork))
                sourceSize: Qt.size(512, 512); asynchronous: true; fillMode: Image.PreserveAspectFit
                Accessible.ignored: true
            }
        }
    }
    Component {
        id: cardComponent
        RedesignButton {
            implicitHeight: Math.max(root.theme.dp(64), labels.implicitHeight + root.theme.dp(16))
            text: root.text(root.node.title); tooltipTextFormat: Text.PlainText
            scaleFactor: root.renderer.scaleFactor; lightTheme: root.renderer.lightTheme; accentColor: root.renderer.accentColor
            onClicked: if (root.node.primaryAction) root.renderer.dispatch(root.node.primaryAction, root.itemContext)
            contentItem: RowLayout {
                spacing: root.theme.dp(10)
                RedesignLibraryArtwork {
                    Layout.preferredWidth: root.theme.dp(48); Layout.preferredHeight: root.theme.dp(48)
                    source: root.renderer.assetFor(root.text(root.node.artwork)); iconName: "music"
                    scaleFactor: root.renderer.scaleFactor; lightTheme: root.renderer.lightTheme; accentColor: root.renderer.accentColor
                }
                ColumnLayout {
                    id: labels
                    Layout.fillWidth: true; spacing: root.theme.dp(3)
                    Text { Layout.fillWidth: true; text: root.text(root.node.title); textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.bright; font.pixelSize: root.theme.dp(14) }
                    Text { Layout.fillWidth: true; visible: text.length > 0; text: root.text(root.node.subtitle); textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.foreground; font.pixelSize: root.theme.dp(12) }
                }
            }
        }
    }
    Component {
        id: listComponent
        Column {
            property var entries: root.renderer.resolve(root.node.items, root.itemContext) || []
            ListView {
                id: list
                objectName: "addonList_" + (root.node.id || "items")
                width: parent.width; height: Math.min(contentHeight, root.theme.dp(360))
                visible: parent.entries.length > 0
                model: parent.entries
                clip: true; boundsBehavior: Flickable.StopAtBounds; keyNavigationEnabled: true
                C.ScrollBar.vertical: C.ScrollBar { }
                delegate: RedesignAddonChild {
                    required property var modelData
                    width: ListView.view.width; height: implicitHeight
                    node: root.node.itemTemplate; renderer: root.renderer; itemContext: modelData
                }
            }
            Text {
                id: empty
                width: parent.width; visible: !parent.entries.length
                text: root.node.emptyText ? root.text(root.node.emptyText) : qsTr("No items to display.")
                textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.foreground; font.pixelSize: root.theme.dp(14)
            }
        }
    }
    Component {
        id: progressComponent
        Column {
            spacing: root.theme.dp(4)
            Text { width: parent.width; text: root.text(root.node.text); textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.foreground; font.pixelSize: root.theme.dp(13) }
            C.ProgressBar {
                width: parent.width
                value: Math.max(0, Math.min(1, Number(root.renderer.resolve(root.node.value, root.itemContext) || 0)))
                palette.highlight: root.theme.accent
                Accessible.name: root.text(root.node.text)
            }
        }
    }
}

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
            case "Tabs": return tabsComponent
            case "Menu": return menuComponent
            case "MediaRow": return mediaRowComponent
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
            RedesignAddonAssetImage {
                anchors.fill: parent
                renderer: root.renderer; artworkHandle: root.text(root.node.artwork)
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
                RedesignAddonAssetImage {
                    Layout.preferredWidth: root.theme.dp(48); Layout.preferredHeight: root.theme.dp(48)
                    renderer: root.renderer; artworkHandle: root.text(root.node.artwork); iconName: "music"
                    fillMode: Image.PreserveAspectCrop
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
            // A list with "scroll": false shows every row; the frame around it
            // scrolls. The add-on pages long lists with "more".
            readonly property bool scrolls: root.node.scroll !== false
            spacing: root.theme.dp(4)
            ListView {
                id: list
                objectName: "addonList_" + (root.node.id || "items")
                width: parent.width; height: parent.scrolls ? Math.min(contentHeight, root.theme.dp(360)) : contentHeight
                interactive: parent.scrolls
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
            RedesignButton {
                objectName: "addonListMore_" + (root.node.id || "items")
                width: parent.width
                visible: !!root.node.moreAction && root.renderer.resolve(root.node.more, root.itemContext) === true
                caption: root.node.moreText ? root.text(root.node.moreText) : qsTr("Show more"); text: caption
                scaleFactor: root.renderer.scaleFactor; lightTheme: root.renderer.lightTheme; accentColor: root.renderer.accentColor
                onClicked: root.renderer.dispatch(root.node.moreAction, root.itemContext)
            }
        }
    }
    Component {
        id: tabsComponent
        Item {
            id: tabs
            readonly property var entries: root.renderer.resolve(root.node.items, root.itemContext) || []
            readonly property var chosen: root.renderer.resolve(root.node.selected, root.itemContext)
            // Few short choices are buttons; more become a combo box.
            readonly property bool compact: entries.length > 4
            function choose(entry) { root.renderer.dispatch(root.node.action, Object.assign({}, root.itemContext || {}, {tab: entry})) }
            implicitHeight: compact ? combo.implicitHeight : buttons.implicitHeight
            Accessible.role: Accessible.PageTabList; Accessible.name: root.text(root.node.text)
            Flow {
                id: buttons
                width: parent.width; spacing: root.theme.dp(6)
                visible: !tabs.compact
                Repeater {
                    model: tabs.compact ? [] : tabs.entries
                    RedesignButton {
                        required property var modelData
                        objectName: "addonTab_" + modelData.key
                        caption: modelData.label; text: caption; captionTextFormat: Text.PlainText; tooltipTextFormat: Text.PlainText
                        selected: modelData.key === tabs.chosen
                        scaleFactor: root.renderer.scaleFactor; lightTheme: root.renderer.lightTheme; accentColor: root.renderer.accentColor
                        Accessible.role: Accessible.PageTab
                        onClicked: if (!selected) tabs.choose(modelData)
                    }
                }
            }
            RedesignComboBox {
                id: combo
                objectName: "addonTabs_" + (root.node.id || "choice")
                width: parent.width; visible: tabs.compact
                scaleFactor: root.renderer.scaleFactor; lightTheme: root.renderer.lightTheme; accentColor: root.renderer.accentColor
                model: tabs.compact ? tabs.entries.map(entry => entry.label) : []
                labelTextFormat: Text.PlainText
                currentIndex: tabs.entries.findIndex(entry => entry.key === tabs.chosen)
                onActivated: index => { if (tabs.entries[index] && tabs.entries[index].key !== tabs.chosen) tabs.choose(tabs.entries[index]) }
                Accessible.name: root.text(root.node.text)
            }
        }
    }
    Component {
        id: menuComponent
        Item {
            readonly property var actionsMenu: actions
            implicitHeight: menuButton.implicitHeight
            readonly property var shown: root.node.entries.map((entry, index) => ({entry: entry, index: index}))
                .filter(row => row.entry.visible === undefined || root.renderer.resolve(row.entry.visible, root.itemContext) === true)
            RedesignButton {
                id: menuButton
                objectName: "addonMenu_" + (root.node.id || "actions")
                iconName: root.node.icon || "more"; text: root.text(root.node.text); tooltipTextFormat: Text.PlainText
                scaleFactor: root.renderer.scaleFactor; lightTheme: root.renderer.lightTheme; accentColor: root.renderer.accentColor
                enabled: parent.shown.length > 0
                onClicked: actions.toggleFrom(this)
            }
            RedesignActionMenu {
                id: actions
                objectName: "addonMenuActions_" + (root.node.id || "actions")
                scaleFactor: root.renderer.scaleFactor; lightTheme: root.renderer.lightTheme; accentColor: root.renderer.accentColor
                entries: parent.shown.map(row => ({action: String(row.index), label: root.text(row.entry.text), icon: row.entry.icon || ""}))
                onActionRequested: action => {
                    const entry = root.node.entries[Number(action)]
                    if (entry) root.renderer.dispatch(entry.action, root.itemContext)
                }
            }
        }
    }
    Component {
        id: mediaRowComponent
        Column {
            id: mediaRow
            readonly property bool expanded: root.renderer.resolve(root.node.expanded, root.itemContext) === true
            readonly property real progress: Math.max(0, Math.min(1, Number(root.renderer.resolve(root.node.progress, root.itemContext) || 0)))
            readonly property string mark: root.text(root.node.mark)
            spacing: 0
            RedesignButton {
                objectName: "addonMediaRow_" + (root.itemContext && root.itemContext.key !== undefined ? root.itemContext.key : root.node.id || "row")
                width: parent.width; height: Math.max(root.theme.dp(56), labels.implicitHeight + root.theme.dp(16))
                cornerRadius: 0
                text: root.text(root.node.title) + (mediaRow.mark ? ", " + mediaRow.mark : ""); tooltipTextFormat: Text.PlainText
                selected: mediaRow.expanded
                scaleFactor: root.renderer.scaleFactor; lightTheme: root.renderer.lightTheme; accentColor: root.renderer.accentColor
                onClicked: if (root.node.primaryAction) root.renderer.dispatch(root.node.primaryAction, root.itemContext)
                contentItem: RowLayout {
                    spacing: root.theme.dp(10)
                    Text {
                        Layout.leftMargin: root.theme.dp(12)
                        visible: text.length > 0
                        text: root.text(root.node.number); textFormat: Text.PlainText
                        color: root.theme.accent; font.pixelSize: root.theme.dp(14)
                    }
                    ColumnLayout {
                        id: labels
                        Layout.fillWidth: true; spacing: root.theme.dp(3)
                        Text { Layout.fillWidth: true; text: root.text(root.node.title); textFormat: Text.PlainText; elide: Text.ElideRight; color: root.theme.foreground; font.pixelSize: root.theme.dp(14) }
                        Text { Layout.fillWidth: true; visible: text.length > 0; text: root.text(root.node.subtitle); textFormat: Text.PlainText; elide: Text.ElideRight; color: root.theme.playerLabel; font.pixelSize: root.theme.dp(11) }
                        Text { Layout.fillWidth: true; visible: text.length > 0; text: mediaRow.mark; textFormat: Text.PlainText; elide: Text.ElideRight; color: root.theme.accent; font.pixelSize: root.theme.dp(11) }
                    }
                    RedesignIcon {
                        Layout.rightMargin: root.theme.dp(12); Layout.preferredWidth: root.theme.dp(20); Layout.preferredHeight: root.theme.dp(20)
                        visible: (root.node.children || []).length > 0
                        name: mediaRow.expanded ? "chevron-up" : "chevron-down"; color: root.theme.playerLabel
                    }
                }
            }
            Item {
                width: parent.width; height: root.theme.dp(2)
                visible: mediaRow.progress > 0
                Rectangle { x: root.theme.dp(12); width: (parent.width - root.theme.dp(24)) * mediaRow.progress; height: parent.height; color: root.theme.accent }
            }
            Column {
                width: parent.width - root.theme.dp(24); x: root.theme.dp(12)
                topPadding: root.theme.dp(10); bottomPadding: root.theme.dp(12)
                visible: mediaRow.expanded
                spacing: root.theme.dp(10)
                RedesignAddonAssetImage {
                    width: parent.width; height: Math.min(width * 9 / 16, root.theme.dp(155))
                    visible: mediaRow.expanded && root.text(root.node.artwork).length > 0
                    renderer: root.renderer; artworkHandle: mediaRow.expanded ? root.text(root.node.artwork) : ""; iconName: "video"
                    fillMode: Image.PreserveAspectFit
                }
                Repeater {
                    model: mediaRow.expanded ? root.node.children || [] : []
                    RedesignAddonChild {
                        required property var modelData
                        width: parent.width; height: implicitHeight
                        node: modelData; renderer: root.renderer; itemContext: root.itemContext
                    }
                }
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

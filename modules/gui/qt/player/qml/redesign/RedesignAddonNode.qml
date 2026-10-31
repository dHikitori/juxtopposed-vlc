// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C
import "AddonUi.js" as Contract

Item {
    id: root
    required property var node
    required property var renderer
    property var itemContext: null
    readonly property var theme: renderer.theme
    readonly property bool horizontal: node.type === "Row"
    objectName: "addonNode_" + (node.id || node.type || "empty")
    implicitHeight: visible && content.item ? content.item.implicitHeight : 0
    implicitWidth: visible && content.item ? content.item.implicitWidth : 0
    visible: node.visible === undefined || renderer.resolve(node.visible, itemContext) === true
    enabled: renderer.valid && (node.enabled === undefined || renderer.resolve(node.enabled, itemContext) === true)
    function text(value) { return renderer.text(value, itemContext) }
    // Whether a child node is shown, from its own binding: a wrapper must not
    // take it from the child's size, which is zero while the wrapper is hidden.
    function shown(child) { return child.visible === undefined || renderer.resolve(child.visible, itemContext) === true }
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
            id: column
            // An edge-to-edge panel page: lists and media rows span the width,
            // other children are inset, and the header row ends with the
            // frame's control above a divider.
            readonly property bool bleed: root.renderer.bleed === true && root.node.type === "Page"
            spacing: bleed ? 0 : root.theme.dp(root.node.type === "Section" ? 12 : 8)
            Text {
                width: parent.width; visible: text.length > 0
                text: root.text(root.node.title); textFormat: Text.PlainText; wrapMode: Text.Wrap
                color: root.theme.bright; font.pixelSize: root.theme.dp(18); font.weight: Font.Medium
            }
            Repeater {
                model: root.node.children || []
                Item {
                    id: slot
                    required property var modelData
                    readonly property bool header: column.bleed && !!root.renderer.headerRow && modelData.id === root.renderer.headerRow
                    readonly property bool full: !column.bleed || ["List", "MediaRow"].includes(modelData.type)
                    readonly property real inset: header ? root.theme.dp(10) : full ? 0 : root.theme.dp(12)
                    readonly property real pad: header ? root.theme.dp(10) : full ? 0 : root.theme.dp(6)
                    // The header row moves to the frame's header place, which
                    // does not scroll; its slot here takes no room.
                    readonly property Item host: header ? root.renderer.headerHost : null
                    width: parent.width
                    visible: root.shown(modelData)
                    height: visible && !host ? box.height : 0
                    Binding { when: !!slot.host; target: slot.host; property: "implicitHeight"; value: slot.visible ? box.height : 0 }
                    Item {
                        id: box
                        objectName: slot.header ? "addonPanelHeader" : ""
                        parent: slot.host || slot
                        width: parent.width
                        visible: slot.visible
                        height: child.implicitHeight + 2 * slot.pad + (slot.header ? divider.height : 0)
                        RedesignAddonChild {
                            id: child
                            x: slot.inset; y: slot.pad
                            width: parent.width - 2 * slot.inset; height: implicitHeight
                            node: slot.modelData; renderer: root.renderer; itemContext: root.itemContext
                        }
                        Rectangle {
                            id: divider
                            visible: slot.header
                            anchors.bottom: parent.bottom
                            width: parent.width; height: slot.header ? root.theme.dp(1) : 0; color: root.theme.border
                        }
                    }
                }
            }
        }
    }
    Component {
        id: rowComponent
        Loader {
            // A row whose children may grow lays them out on one line: a
            // growing child takes the free width, the others their own. A
            // row without one wraps its children.
            readonly property bool header: !!root.renderer.headerRow && root.node.id === root.renderer.headerRow && !!root.renderer.headerTrailing
            readonly property bool line: header || (root.node.children || []).some(child => child.grow === true)
            sourceComponent: line ? lineComponent : flowComponent
        }
    }
    Component {
        id: flowComponent
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
        id: lineComponent
        RowLayout {
            spacing: root.theme.dp(4)
            Repeater {
                model: root.node.children || []
                RedesignAddonChild {
                    required property var modelData
                    Layout.fillWidth: modelData.grow === true
                    Layout.preferredWidth: modelData.grow === true ? -1 : implicitWidth
                    Layout.alignment: Qt.AlignVCenter
                    visible: root.shown(modelData)
                    node: modelData; renderer: root.renderer; itemContext: root.itemContext
                }
            }
            Loader {
                objectName: "addonHeaderTrailing"
                active: !!root.renderer.headerRow && root.node.id === root.renderer.headerRow
                visible: active
                sourceComponent: root.renderer.headerTrailing
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }
    Component {
        id: textComponent
        // The text takes no height from the loader: with a line limit, a
        // Text elides to its height, which would loop with its own size.
        Item {
            implicitHeight: label.implicitHeight; implicitWidth: label.implicitWidth
            Accessible.role: root.node.type === "Notice" ? Accessible.AlertMessage : Accessible.StaticText
            Accessible.name: label.text
            Text {
                id: label
                objectName: "addonText"
                readonly property string style: root.node.type === "Text" ? root.node.style || "body" : ""
                readonly property int lines: root.node.maxLines || 0
                width: parent.width
                text: root.text(root.node.text); textFormat: Text.PlainText
                wrapMode: lines === 1 ? Text.NoWrap : Text.Wrap
                maximumLineCount: lines > 0 ? lines : Number.MAX_SAFE_INTEGER
                elide: lines === 1 ? Text.ElideMiddle : lines > 1 ? Text.ElideRight : Text.ElideNone
                color: root.node.type === "Heading" ? root.theme.bright : style === "caption" ? root.theme.playerLabel : root.theme.foreground
                font.pixelSize: root.theme.dp(root.node.type === "Heading" ? 18 : style === "small" ? 13 : style === "caption" ? 12 : 14)
                font.weight: root.node.type === "Heading" ? Font.Medium : Font.Normal
                Accessible.ignored: true
            }
        }
    }
    Component {
        id: buttonComponent
        RedesignButton {
            id: actionButton
            // A subtitle makes a two-line heading button; "link" is a quiet
            // text action; a growing button centers its label.
            readonly property bool heading: root.node.type === "Button" && root.node.subtitle !== undefined
            readonly property string subtitle: heading ? root.text(root.node.subtitle) : ""
            readonly property bool link: root.node.intent === "link"
            readonly property bool centered: root.node.grow === true && !heading && !link
            caption: root.node.type === "IconButton" ? "" : root.text(root.node.text)
            text: [root.text(root.node.text), subtitle].filter(value => value.length > 0).join(", "); tooltipTextFormat: Text.PlainText
            iconName: root.node.icon || ""
            primaryAction: root.node.intent === "primary"
            implicitHeight: heading ? root.theme.dp(44) : root.theme.dp(36)
            contentLeftMargin: link ? 0 : 12
            foreground: root.node.intent === "destructive" ? root.theme.danger : root.node.intent === "primary" ? root.theme.accent
                : link ? root.theme.playerLabel : root.theme.foreground
            scaleFactor: root.renderer.scaleFactor; lightTheme: root.renderer.lightTheme; accentColor: root.renderer.accentColor
            onClicked: root.renderer.dispatch(root.node.action, root.itemContext)
            contentItem: Item {
                // From the labels' natural widths: the row's width follows this.
                implicitWidth: (root.node.icon ? actionIcon.width + line.spacing : 0)
                    + (root.node.type === "IconButton" ? 0 : Math.max(label.implicitWidth, subtitleLabel.visible ? subtitleLabel.implicitWidth : 0))
                implicitHeight: line.implicitHeight
                // A heading button that cannot open anything is still the title.
                opacity: actionButton.enabled || actionButton.heading ? 1 : 0.35
                Row {
                    id: line
                    x: actionButton.centered ? Math.max(0, (parent.width - width) / 2) : actionButton.link ? -actionButton.leftPadding : 0
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: root.theme.dp(actionButton.heading ? 8 : 6)
                    RedesignIcon {
                        id: actionIcon
                        width: root.node.icon ? root.theme.dp(24) : 0; height: root.theme.dp(24)
                        visible: !!root.node.icon; name: root.node.icon || ""; color: actionButton.foreground
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: root.theme.dp(3)
                        Text {
                            id: label
                            objectName: "addonActionLabel"
                            visible: root.node.type !== "IconButton"
                            width: Math.min(implicitWidth, Math.max(0, root.width - actionIcon.width - root.theme.dp(32)))
                            text: root.text(root.node.text); textFormat: Text.PlainText; elide: Text.ElideRight
                            font.pixelSize: root.theme.dp(actionButton.heading ? 16 : actionButton.link ? 11 : 14)
                            font.weight: actionButton.heading ? Font.Medium : Font.Normal
                            color: actionButton.foreground
                        }
                        Text {
                            id: subtitleLabel
                            objectName: "addonActionSubtitle"
                            visible: actionButton.subtitle.length > 0
                            width: Math.min(implicitWidth, Math.max(0, root.width - actionIcon.width - root.theme.dp(32)))
                            text: actionButton.subtitle; textFormat: Text.PlainText; elide: Text.ElideRight
                            font.pixelSize: root.theme.dp(12); color: root.theme.playerLabel
                        }
                    }
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
                x: root.renderer.bleed ? root.theme.dp(16) : 0; topPadding: root.renderer.bleed ? root.theme.dp(8) : 0
                width: parent.width - 2 * x; visible: !parent.entries.length
                text: root.node.emptyText ? root.text(root.node.emptyText) : qsTr("No items to display.")
                textFormat: Text.PlainText; wrapMode: Text.Wrap; color: root.theme.foreground; font.pixelSize: root.theme.dp(14)
            }
            RedesignButton {
                objectName: "addonListMore_" + (root.node.id || "items")
                x: root.renderer.bleed ? root.theme.dp(12) : 0; width: parent.width - 2 * x
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
            readonly property bool compact: root.node.style === "dropdown" || (root.node.style !== "buttons" && entries.length > 4)
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
            implicitHeight: menuButton.implicitHeight; implicitWidth: menuButton.implicitWidth
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
            // The highlighted row: "selected" when the view binds it (the
            // playing episode, say), else the open row.
            readonly property bool chosen: root.node.selected !== undefined ? root.renderer.resolve(root.node.selected, root.itemContext) === true : expanded
            readonly property string artwork: root.text(root.node.artwork)
            readonly property real progress: Math.max(0, Math.min(1, Number(root.renderer.resolve(root.node.progress, root.itemContext) || 0)))
            readonly property string mark: root.text(root.node.mark)
            readonly property string icon: { const name = root.text(root.node.icon); return Contract.icons.includes(name) ? name : "" }
            // A row with a number, subtitle or mark is an item row; a plain
            // row (a season, say) is shorter.
            readonly property bool detailed: root.node.number !== undefined || root.node.subtitle !== undefined || root.node.mark !== undefined
            spacing: 0
            RedesignButton {
                objectName: "addonMediaRow_" + (root.itemContext && root.itemContext.key !== undefined ? root.itemContext.key : root.node.id || "row")
                width: parent.width; height: Math.max(root.theme.dp(mediaRow.detailed ? 60 : 48), labels.implicitHeight + root.theme.dp(16))
                cornerRadius: 0
                text: root.text(root.node.title) + (mediaRow.mark ? ", " + mediaRow.mark : ""); tooltipTextFormat: Text.PlainText
                selected: mediaRow.chosen
                scaleFactor: root.renderer.scaleFactor; lightTheme: root.renderer.lightTheme; accentColor: root.renderer.accentColor
                onClicked: if (root.node.primaryAction) root.renderer.dispatch(root.node.primaryAction, root.itemContext)
                contentItem: RowLayout {
                    spacing: root.theme.dp(10)
                    RedesignIcon {
                        Layout.leftMargin: root.theme.dp(12); Layout.preferredWidth: root.theme.dp(20); Layout.preferredHeight: root.theme.dp(20)
                        visible: root.node.icon !== undefined
                        name: mediaRow.icon; color: root.theme.accent
                    }
                    Text {
                        Layout.leftMargin: root.node.icon !== undefined ? 0 : root.theme.dp(12)
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
                        visible: (root.node.children || []).length > 0 || root.node.navigates === true
                        name: root.node.navigates === true ? "chevron-right" : mediaRow.expanded ? "chevron-up" : "chevron-down"; color: root.theme.playerLabel
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
                topPadding: root.theme.dp(12); bottomPadding: root.theme.dp(12)
                visible: mediaRow.expanded
                spacing: root.theme.dp(12)
                // App artwork (media: handles) looks like the library's; a
                // package image keeps its own proportions.
                RedesignLibraryArtwork {
                    width: parent.width; height: Math.min(width * 9 / 16, root.theme.dp(155))
                    visible: mediaRow.expanded && mediaRow.artwork.startsWith("media:")
                    scaleFactor: root.renderer.scaleFactor; lightTheme: root.renderer.lightTheme; accentColor: root.renderer.accentColor
                    source: visible ? root.renderer.hostArtworkFor(mediaRow.artwork) : ""
                    iconName: "video"; surroundColor: root.theme.floatingPanel; cornerRadius: 6
                }
                RedesignAddonAssetImage {
                    width: parent.width; height: Math.min(width * 9 / 16, root.theme.dp(155))
                    visible: mediaRow.expanded && mediaRow.artwork.length > 0 && !mediaRow.artwork.startsWith("media:")
                    renderer: root.renderer; artworkHandle: visible ? mediaRow.artwork : ""; iconName: "video"
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

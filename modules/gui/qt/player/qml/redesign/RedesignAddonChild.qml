// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

Item {
    id: root
    required property var node
    required property var renderer
    property var itemContext: null
    implicitHeight: content.item ? content.item.implicitHeight : 0
    onNodeChanged: if (content.item) content.item.node = node
    onRendererChanged: if (content.item) content.item.renderer = renderer
    onItemContextChanged: if (content.item) content.item.itemContext = itemContext
    Loader {
        id: content
        width: parent.width; height: parent.implicitHeight
        // Fixed host resource breaks the declarative recursion cycle. No path or
        // QML source from the addon is ever accepted here.
        Component.onCompleted: setSource(Qt.resolvedUrl("RedesignAddonNode.qml"), {
            node: root.node, renderer: root.renderer, itemContext: root.itemContext
        })
    }
}

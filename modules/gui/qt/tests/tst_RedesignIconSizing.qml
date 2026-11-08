// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "JuxtopposedIconSizing"
    width: 400; height: 160
    visible: true; when: windowShown

    Component {
        id: iconComponent
        RedesignIcon { name: "folder" }
    }
    Component {
        id: rowComponent
        RowLayout {
            property alias glyph: glyph
            width: 280; height: 44
            RedesignIcon {
                id: glyph
                name: "folder"
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
            }
            Text { Layout.fillWidth: true; text: "Queue item" }
        }
    }
    Component {
        id: chevronComponent
        Item {
            // Some software grabs contain physical pixels but report DPR 1.
            // Reserve enough bounds for either result, then sample physical pixels.
            width: 96 * Screen.devicePixelRatio; height: width
            property alias iconName: arrow.name
            property alias status: arrow.status
            Rectangle {
                width: 96; height: 96; color: "black"
                RedesignIcon { id: arrow; anchors.fill: parent; color: "white" }
            }
        }
    }
    Component { id: filterComponent; RedesignFilterSection { width: 320; label: "Filter" } }
    Component { id: homeComponent; RedesignHomeView { width: 320; height: 100; sections: [{key: "recent", title: "Recent", emptyText: "", model: []}] } }
    function chevron(item) {
        if (String(item.name || "").startsWith("chevron-")) return item
        for (const child of item.children || []) {
            const found = chevron(child)
            if (found) return found
        }
        return null
    }
    function test_sectionArrowsFollowMirroring_data() {
        return [{tag: "filter", component: filterComponent, disclosure: true},
                {tag: "home", component: homeComponent, disclosure: false}]
    }
    function test_sectionArrowsFollowMirroring(data) {
        const section = createTemporaryObject(data.component, testCase)
        verify(section !== null)
        const arrow = chevron(section)
        verify(arrow !== null)
        compare(arrow.name, "chevron-right")
        section.LayoutMirroring.childrenInherit = true
        section.LayoutMirroring.enabled = true
        tryCompare(arrow, "name", "chevron-left")
        if (data.disclosure) {
            section.expanded = true
            tryCompare(arrow, "name", "chevron-down")
            section.expanded = false
            tryCompare(arrow, "name", "chevron-left")
        }
        section.LayoutMirroring.enabled = false
        tryCompare(arrow, "name", "chevron-right")
    }

    function init() {
        if (typeof failOnWarning === "function") failOnWarning(/Binding loop detected/)
    }
    function test_chevronsPointInTheirNamedDirection_data() {
        return [{tag: "left", glyph: "chevron-left", tip: 36, empty: 60},
                {tag: "right", glyph: "chevron-right", tip: 60, empty: 36}]
    }
    function test_chevronsPointInTheirNamedDirection(data) {
        const icon = createTemporaryObject(chevronComponent, testCase,
            {iconName: data.glyph})
        verify(icon !== null)
        tryCompare(icon, "status", Image.Ready)
        wait(20)
        const image = grabImage(icon)
        const ratio = Screen.devicePixelRatio
        verify(image.red(Math.round(data.tip * ratio), Math.round(48 * ratio)) > 200)
        verify(image.red(Math.round(data.empty * ratio), Math.round(48 * ratio)) < 20)
    }

    function test_intrinsicSizeDoesNotFollowRasterRequests() {
        const icon = createTemporaryObject(iconComponent, testCase)
        verify(icon !== null)
        tryCompare(icon, "status", Image.Ready)
        compare(icon.width, 24); compare(icon.height, 24)
        for (const size of [18, 30, 42.5, 16]) {
            icon.width = size; icon.height = size
            wait(10)
            compare(icon.implicitWidth, 24)
            compare(icon.implicitHeight, 24)
            compare(icon.sourceSize.width, Math.round(size * Screen.devicePixelRatio))
            compare(icon.sourceSize.height, Math.round(size * Screen.devicePixelRatio))
        }
    }
    function test_layoutSizeIsStable_data() {
        return [
            {tag: "visible", shown: true},
            {tag: "hidden", shown: false}
        ]
    }
    function test_layoutSizeIsStable(data) {
        const row = createTemporaryObject(rowComponent, testCase)
        verify(row !== null)
        const icon = row.glyph
        icon.visible = data.shown
        for (const size of [18, 24, 36, 15.3]) {
            icon.Layout.preferredWidth = size
            icon.Layout.preferredHeight = size
            row.width = 300 + size
            row.height = 44 + size
            wait(15)
            compare(icon.implicitWidth, 24)
            compare(icon.implicitHeight, 24)
            if (data.shown) {
                // The layout polishes asynchronously; wait for it under load.
                tryVerify(() => Math.abs(icon.width - size) <= 1, 5000, "width " + icon.width + " != " + size)
                tryVerify(() => Math.abs(icon.height - size) <= 1, 5000, "height " + icon.height + " != " + size)
            }
            icon.visible = !icon.visible
            icon.name = icon.name === "folder" ? "playlist" : "folder"
            icon.color = icon.color === "#8f8f8f" ? "#228e53" : "#8f8f8f"
            wait(15)
            compare(icon.implicitWidth, 24)
            compare(icon.implicitHeight, 24)
            icon.visible = data.shown
        }
    }
}

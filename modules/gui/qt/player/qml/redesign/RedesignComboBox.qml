// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Layouts

C.ComboBox {
    id: root
    property real scaleFactor: 1
    property bool lightTheme: false
    property bool showCurvePreviews: false
    property bool showIcons: false
    property real choicePadding: -1
    property bool showSelectionMark: showCurvePreviews
    // Opt in for array models whose unavailable rows have enabled: false.
    property bool restrictChoices: false
    property real choiceHeight: 34
    property real subsequentLabelOffset: 0
    property real labelVerticalOffset: 0
    property real indicatorRightMargin: 16
    property real popupGap: 4
    property real popupBottomPadding: 2
    property Component videoBackdropContent
    property var labelOverrides: ({})
    function presetPreviewAt(index) {
        const value = model && model[index] ? model[index].value : ""
        const names = {flat: "flat", classical: "classical", club: "club", dance: "dance",
            fullbass: "full-bass", fullbasstreble: "full-bass-and-treble", fulltreble: "full-treble",
            headphones: "headphones", largehall: "large-hall", live: "live", party: "party",
            pop: "pop", reggae: "reggae", rock: "rock", ska: "ska", soft: "soft", softrock: "soft-rock", techno: "techno"}
        return names[value] ? "eq-" + names[value] : ""
    }
    function labelAt(index) { const text = textAt(index); return labelOverrides[text] || text }
    function choiceEnabled(index) {
        return index >= 0 && index < count && (!restrictChoices || model[index].enabled !== false)
    }
    function nextChoice(index, direction) {
        for (let i = index + direction; i >= 0 && i < count; i += direction)
            if (choiceEnabled(i)) return i
        return -1
    }
    function navigateTo(index) {
        if (!choiceEnabled(index)) return
        if (!popup.visible) {
            if (currentIndex !== index) { currentIndex = index; activated(index) }
            return
        }
        // The template's highlightedIndex is read-only. Its public navigation
        // methods update it without activating intermediate popup rows.
        while (highlightedIndex < index) incrementCurrentIndex()
        while (highlightedIndex > index) decrementCurrentIndex()
    }
    Keys.priority: Keys.BeforeItem
    Keys.onPressed: event => {
        if (!restrictChoices || editable) return
        const index = popup.visible ? highlightedIndex : currentIndex
        let target = -1
        if (event.key === Qt.Key_Down) target = nextChoice(index, 1)
        else if (event.key === Qt.Key_Up) target = nextChoice(index < 0 ? count : index, -1)
        else if (event.key === Qt.Key_Home) target = nextChoice(-1, 1)
        else if (event.key === Qt.Key_End) target = nextChoice(count, -1)
        else if (event.text.length > 0 && event.key !== Qt.Key_Space
                 && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))) {
            for (let offset = 1; offset <= count; ++offset) {
                const row = (index + offset + count) % count
                if (choiceEnabled(row) && labelAt(row).toLocaleLowerCase().startsWith(event.text.toLocaleLowerCase())) {
                    target = row
                    break
                }
            }
        } else return
        navigateTo(target)
        event.accepted = true
    }
    signal aboutToOpen()
    property color accentColor: "#d37918"
    property color labelColor: theme.foreground
    property int labelTextFormat: Text.AutoText
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    implicitHeight: theme.dp(34)
    font.pixelSize: theme.dp(14)
    leftPadding: theme.dp(16); rightPadding: theme.dp(40)
    contentItem: Text {
        text: root.displayText; textFormat: root.labelTextFormat; font: root.font
        topPadding: root.theme.dp(root.labelVerticalOffset); bottomPadding: -topPadding
        color: root.labelColor
        verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
    }
    background: Rectangle {
        radius: root.theme.dp(root.theme.controlRadius); color: root.theme.menuPanel
        border.width: root.theme.dp(2)
        border.color: root.visualFocus ? root.theme.focusRing : root.theme.field
    }
    indicator: RedesignIcon {
        name: "chevron-down"; color: root.theme.foreground
        width: root.theme.dp(24); height: width
        x: root.width - width - root.theme.dp(root.indicatorRightMargin); y: (root.height - height) / 2
    }
    delegate: C.ItemDelegate {
        id: choiceDelegate
        required property int index
        objectName: "comboChoice_" + index
        enabled: root.choiceEnabled(index)
        width: ListView.view.width
        implicitHeight: root.theme.dp(root.theme.compactMenus ? Math.min(28, root.choiceHeight) : root.choiceHeight)
        leftPadding: root.theme.dp(root.theme.compactMenus ? 8 : root.choicePadding >= 0 ? root.choicePadding : 16); rightPadding: leftPadding
        topPadding: root.theme.compactMenus ? 0 : padding; bottomPadding: topPadding
        text: root.labelAt(index)
        highlighted: root.highlightedIndex === index
        contentItem: RowLayout {
            spacing: root.theme.dp(10)
            RedesignIcon {
                objectName: "choicePresetPreview"
                name: root.presetPreviewAt(index)
                visible: !root.theme.compactMenus && root.showCurvePreviews && name.length > 0
                Layout.preferredWidth: root.theme.dp(24); Layout.preferredHeight: root.theme.dp(24)
                color: root.theme.foreground; detailColor: root.theme.accent
            }
            RedesignIcon {
                objectName: "choiceIcon"
                readonly property string icon: root.model && root.model[index] ? root.model[index].iconName || "" : ""
                visible: root.showIcons && !root.theme.compactMenus && icon.length > 0
                Layout.preferredWidth: root.theme.dp(24); Layout.preferredHeight: root.theme.dp(24)
                name: icon; color: root.theme.foreground; detailColor: root.theme.menuAccent
            }
            RedesignSvg {
                objectName: "choiceCurvePreview"
                readonly property var curve: root.model && root.model[index] ? root.model[index].curve || [] : []
                readonly property color fillColor: root.theme.accent
                readonly property color lineColor: root.theme.foreground
                Layout.preferredWidth: root.theme.dp(24); Layout.preferredHeight: root.theme.dp(18)
                visible: !root.theme.compactMenus && root.showCurvePreviews && curve.length > 0 && root.presetPreviewAt(index).length === 0
                svg: {
                    if (curve.length < 2 || width <= 0 || height <= 0) return ""
                    const middle = height / 2
                    let path = "M0 " + middle
                    for (let i = 0; i < curve.length; ++i)
                        path += " L" + (i * width / (curve.length - 1)) + " " + (middle - Number(curve[i]) * middle / 20)
                    path += " L" + width + " " + middle + " Z"
                    return (
                        '<svg xmlns="http://www.w3.org/2000/svg" width="' + width + '" height="' + height + '" viewBox="0 0 ' + width + ' ' + height + '">'
                        + '<path d="' + path + '" fill="' + fillColor + '"/>'
                        + '<path d="M0 ' + middle + ' H' + width + '" fill="none" stroke="' + lineColor + '" stroke-width="1"/></svg>')
                }
            }
            Text {
                Layout.fillWidth: true
                text: root.labelAt(index); textFormat: root.labelTextFormat; font: root.font; color: root.theme.foreground
                opacity: choiceDelegate.enabled ? 1 : .4
                verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                Layout.topMargin: choiceDelegate.index > 0 && !root.theme.compactMenus ? root.theme.dp(root.subsequentLabelOffset) : 0
                Layout.bottomMargin: -Layout.topMargin
            }
            Item {
                objectName: "choiceSelectionMark"
                Layout.preferredWidth: root.theme.dp(24); Layout.preferredHeight: root.theme.dp(24)
                visible: root.showSelectionMark && index === root.currentIndex
                RedesignIcon { anchors.fill: parent; name: "check-active"; color: root.theme.accent }
            }
        }
        background: Rectangle {
            x: root.theme.dp(2); y: root.theme.dp(2)
            width: parent.width - root.theme.dp(4); height: parent.height - root.theme.dp(2)
            radius: root.theme.dp(4)
            color: !parent.highlighted ? "transparent" : root.theme.menuHover
        }
    }
    popup: C.Popup {
        onAboutToShow: root.aboutToOpen()
        y: root.height + root.theme.dp(root.popupGap)
        width: root.width
        implicitHeight: Math.min(contentItem.implicitHeight + bottomPadding, Math.max(root.theme.dp(root.choiceHeight), root.Window.height - root.theme.dp(24)))
        margins: root.theme.dp(12)
        padding: 0
        bottomPadding: root.theme.dp(root.popupBottomPadding)
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
            C.ScrollIndicator.vertical: C.ScrollIndicator { }
        }
        background: Rectangle {
            id: surface
            readonly property color fill: root.theme.menuPanel
            color: Qt.rgba(fill.r, fill.g, fill.b, root.theme.floatingOpacity)
            radius: root.theme.dp(root.theme.controlRadius)
            Loader {
                anchors.fill: parent
                z: -1
                active: root.popup.visible
                sourceComponent: root.videoBackdropContent
                onLoaded: {
                    if (item.cornerRadius !== undefined)
                        item.cornerRadius = Qt.binding(() => surface.radius)
                    if (item.menuShadow !== undefined) item.menuShadow = true
                }
            }
        }
    }
}

// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls.Basic as C
import QtQuick.Templates as T

ColumnLayout {
    id: root
    required property var backend
    required property string optionName
    property string label
    property var presentation: ({})
    property real scaleFactor: 1
    property real compactEditorWidth: 140
    property color accentColor: "#d37918"
    property bool lightTheme: false
    signal actionRequested(string action)
    signal editorResetRequested()
    readonly property var description: {
        // C++ descriptions do not register the translations they read as QML
        // dependencies. Refresh them on language changes, without reloading
        // static choice lists on each ordinary value edit.
        if ("translationRevision" in backend) backend.translationRevision
        const data = backend.describe(optionName)
        if (presentation.dynamicChoices || optionName.endsWith("-language")
            || data.restartRequired !== undefined || data.supported !== undefined) backend.revision
        return data
    }
    readonly property var currentValue: { backend.revision; return backend.value(optionName) }
    readonly property var choices: {
        const options = presentation.choices || description.choices || []
        if (!presentation.choiceLabels) return options
        return options.map(row => Object.assign({}, row, {label: presentation.choiceLabels[row.value] || row.label}))
    }
    readonly property string editor: presentation.editor || description.editor || ""
    readonly property real factor: presentation.factor || 1
    readonly property bool fullWidth: !!presentation.fullWidth
    readonly property bool compact: !!presentation.compact
    readonly property real preferredEditorWidth: compact ? editor === "language"
        ? Math.min(theme.dp(184), Math.max(theme.dp(root.compactEditorWidth), languageMetrics.advanceWidth + theme.dp(56)))
        : theme.dp(compactEditorWidth) : theme.dp(320)
    readonly property string helpText: presentation.help || description.help || ""
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    readonly property bool available: description.kind !== undefined
    visible: available
    spacing: theme.dp(4)
    readonly property bool editorEnabled: {
        backend.revision
        return description.supported !== false && (!presentation.enabledWhen || !!backend.value(presentation.enabledWhen))
            && (!presentation.enabledWhenEquals || backend.value(presentation.enabledWhenEquals.name) === presentation.enabledWhenEquals.value)
    }
    opacity: editorEnabled ? 1 : presentation.disabledOpacity === undefined ? 0.5 : presentation.disabledOpacity
    layer.enabled: !editorEnabled && presentation.disabledOpacity !== undefined
    function commit(value) {
        if (presentation.inverted) value = !value
        if (presentation.toggleValues) value = value ? presentation.toggleValues.on : presentation.toggleValues.off
        return backend.stage(optionName, value)
    }
    readonly property var displayedValue: presentation.toggleValues ? currentValue !== presentation.toggleValues.off
        : presentation.inverted ? !currentValue
        : currentValue === "" && presentation.emptyChoiceValue !== undefined ? presentation.emptyChoiceValue : currentValue
    TextMetrics {
        id: languageMetrics
        font.family: Qt.application.font.family; font.pixelSize: root.theme.dp(14)
        text: root.editor === "language" ? (root.choices.find(row => row.value === root.currentValue)?.label || "") : ""
    }
    readonly property Component editorComponent: editor === "action" ? actionEditor : editor === "toggle" ? toggleEditor : editor === "startup-volume" ? startupVolumeEditor : editor === "slider" ? sliderEditor
        : editor === "segmented" ? segmentedEditor : editor === "number-choice" ? numberChoiceEditor
        : editor === "track" ? trackEditor : editor === "optical-drive" ? opticalDriveEditor : editor === "language" ? languageEditor
        : description.kind === "bool" && !choices.length ? toggleEditor
        : editor === "color" ? colorEditor
        : choices.length ? choiceEditor : textEditor
    RowLayout {
        Layout.fillWidth: true
        Layout.minimumHeight: !available ? 0 : theme.dp(root.presentation.rowHeight || (root.compact ? 52 : root.editor === "startup-volume" ? 92 : root.editor === "slider" ? root.presentation.inlineSlider ? 42 : 68 : root.editor === "toggle" ? 42 : root.choices.length ? 46 : root.description.kind === "bool" ? 42 : 48))
        spacing: theme.dp(10)
        implicitHeight: available ? Math.max(theme.dp(root.presentation.rowHeight || (root.presentation.inlineSlider ? 42 : 44)), labelText.implicitHeight + theme.dp(10)) : 0
        Item {
            id: labelArea
            Layout.fillWidth: true
            // Use intrinsic size hints so the editor cannot change this row's
            // implicit width while its parent is arranging the controls.
            Layout.preferredWidth: Math.max(0, root.preferredEditorWidth * (root.compact ? 39 / 61 : 57 / 43) - theme.dp(10))
            Layout.minimumWidth: 0
            implicitHeight: Math.max(labelText.implicitHeight, theme.dp(20))
            HoverHandler { id: rowHover }
            Text {
                id: labelText
                // Reserve space for Reset even before hover, so it cannot cover
                // the help button when a long label wraps in a narrow window.
                width: Math.min(implicitWidth, Math.max(0, parent.width - (helpButton.visible ? theme.dp(root.description.kind === "action" ? 32 : 66) : 0)))
                anchors.verticalCenter: parent.verticalCenter
                text: root.label || root.description.title || ""
                color: root.compact ? theme.playerLabel : theme.foreground; font.pixelSize: theme.dp(14)
                wrapMode: Text.WordWrap
            }
            C.ToolButton {
                id: helpButton
                objectName: "preferenceHelpIcon"
                width: theme.dp(24); height: width; padding: theme.dp(3)
                x: labelText.width + theme.dp(8); anchors.verticalCenter: parent.verticalCenter
                visible: root.helpText.length > 0 && !root.compact && !root.presentation.hideHelp
                focusPolicy: Qt.StrongFocus
                hoverEnabled: true
                Accessible.name: labelText.text
                Accessible.description: root.helpText
                onClicked: helpPopup.visible ? helpPopup.close() : helpPopup.open()
                onVisibleChanged: if (!visible) helpPopup.close()
                contentItem: RedesignIcon {
                    name: "info"; color: theme.foreground
                    opacity: helpButton.hovered || helpButton.visualFocus || helpPopup.visible ? 1 : 0.4
                }
                background: Rectangle {
                    radius: theme.dp(4)
                    color: helpButton.hovered ? theme.hover : "transparent"
                    border.width: helpButton.visualFocus ? 1 : 0
                    border.color: theme.accent
                }
                C.Popup {
                    id: helpPopup
                    objectName: "preferenceHelpPopup"
                    popupType: C.Popup.Item
                    y: helpButton.height + theme.dp(4)
                    width: Math.min(theme.dp(360), root.Window.window ? root.Window.window.width - 2 * margins : theme.dp(360))
                    padding: theme.dp(12); margins: theme.dp(6)
                    focus: true
                    closePolicy: C.Popup.CloseOnEscape | C.Popup.CloseOnPressOutside
                    contentItem: Text {
                        text: root.helpText; textFormat: Text.PlainText
                        color: theme.bright; font.pixelSize: theme.dp(14)
                        wrapMode: Text.Wrap
                    }
                    background: Rectangle {
                        radius: theme.dp(6); color: theme.panel
                        border.width: 1; border.color: theme.border
                    }
                }
            }
            RedesignToolTip {
                theme: root.theme; text: root.helpText
                visible: (rowHover.hovered || helpButton.visualFocus) && !helpPopup.visible
                    && !resetButton.hovered && !resetButton.visualFocus && text.length > 0
                delay: helpButton.hovered || helpButton.visualFocus ? 0 : 500
            }
            RedesignButton {
                id: resetButton
                objectName: "preferenceResetButton"
                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                width: theme.dp(26); height: theme.dp(28); iconSize: theme.dp(18)
                iconName: "history"; text: qsTr("Reset to default")
                enabled: root.editorEnabled
                visible: root.description.kind !== "action" && (rowHover.hovered || hovered || visualFocus)
                onClicked: { root.backend.reset(root.optionName); root.editorResetRequested() }
            }
        }
        Loader {
            visible: !root.fullWidth
            active: visible
            enabled: root.editorEnabled
            Layout.minimumWidth: root.editorComponent === toggleEditor && item ? item.implicitWidth : 0
            Layout.fillWidth: true
            Layout.preferredWidth: root.preferredEditorWidth
            Layout.maximumWidth: root.preferredEditorWidth
            Layout.preferredHeight: item ? item.implicitHeight || theme.dp(34) : theme.dp(34)
            Layout.alignment: root.presentation.alignSliderTop ? Qt.AlignTop : Qt.AlignVCenter
            Layout.topMargin: theme.dp(root.presentation.sliderTopMargin || 0)
            sourceComponent: root.editorComponent
        }

    }
    Loader {
        Layout.fillWidth: true
        Layout.minimumWidth: root.editorComponent === toggleEditor && item ? item.implicitWidth : 0
        Layout.preferredHeight: item ? item.implicitHeight : 0
        active: root.fullWidth; visible: active
        enabled: root.editorEnabled
        sourceComponent: root.editorComponent
    }
    Loader {
        Layout.fillWidth: true
        Layout.topMargin: active ? theme.dp(1) : 0
        Layout.bottomMargin: active ? theme.dp(5) : 0
        active: !!root.presentation.equalizer
        visible: active
        sourceComponent: RedesignEqualizer {
            backend: root.backend
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        }
    }
    Loader {
        Layout.fillWidth: true
        Layout.topMargin: active ? theme.dp(8) : 0
        active: !!root.presentation.spatializer
        visible: active
        sourceComponent: RedesignSpatializer {
            backend: root.backend
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        }
    }
    Text {
        objectName: "preferenceRestartNote"
        Layout.fillWidth: true
        visible: !!root.description.restartRequired
        text: qsTr("Close Hakone. Start Hakone again to use this change.")
        color: theme.foreground; font.pixelSize: theme.dp(12); wrapMode: Text.WordWrap
    }
    Component {
        id: segmentedEditor
        Rectangle {
            objectName: "preferenceSegmentedEditor"
            implicitHeight: theme.dp(root.presentation.editorHeight || (root.compact ? 36 : 34))
            radius: theme.dp(root.presentation.inset ? 6 : 4); color: theme.field
            RowLayout {
                anchors.fill: parent; anchors.margins: theme.dp(root.presentation.inset || 0); spacing: theme.dp(2)
                Repeater {
                    model: root.choices
                    RedesignButton {
                        required property var modelData
                        required property int index
                        objectName: "preferenceSegment_" + index
                        Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredWidth: 1
                        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                        caption: modelData.label; text: caption
                        selected: root.displayedValue === modelData.value
                        background: Rectangle {
                            radius: theme.dp(4)
                            color: parent.selected ? theme.progress : parent.hovered ? theme.hover : root.presentation.inset && !root.lightTheme ? "#191919" : theme.background
                            border.width: parent.visualFocus ? 1 : 0; border.color: theme.accent
                        }
                        onClicked: root.commit(modelData.value)
                    }
                }
            }
        }
    }
    Component {
        id: numberChoiceEditor
        Item {
            id: numberControl
            implicitHeight: theme.dp(root.presentation.editorHeight || (root.compact ? 36 : 34))
            property bool custom: false
            readonly property string nativeOption: root.description.name || ""
            onNativeOptionChanged: if (custom) cancel()
            onVisibleChanged: if (!visible) custom = false
            Connections {
                target: root
                function onEditorResetRequested() { if (numberControl.custom) numberControl.cancel() }
            }
            function cancel() {
                custom = false
                numberCombo.forceActiveFocus()
            }
            function save() {
                if (!custom) return
                const number = Number(numberField.text)
                numberField.invalid = !numberField.text.trim().length || !Number.isInteger(number)
                    || number < root.description.minimum || number > root.description.maximum
                    || !root.commit(number)
                if (!numberField.invalid) cancel()
            }
            RedesignComboBox {
                id: numberCombo
                objectName: "preferenceChoiceEditor"
                anchors.fill: parent; visible: !numberControl.custom
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                Accessible.name: root.label || root.description.title
                model: root.choices.concat([{label: qsTr("Custom…"), value: "__custom__"}])
                textRole: "label"; valueRole: "value"
                Binding on currentIndex {
                    when: !numberControl.custom
                    restoreMode: Binding.RestoreNone
                    value: root.choices.findIndex(row => row.value === root.displayedValue)
                }
                onActivated: {
                    if (currentValue !== "__custom__") { root.commit(currentValue); return }
                    numberField.text = String(root.currentValue)
                    numberField.invalid = false
                    numberControl.custom = true
                    numberField.forceActiveFocus(); numberField.selectAll()
                }
            }
            RowLayout {
                anchors.fill: parent; spacing: theme.dp(4)
                visible: numberControl.custom
                C.TextField {
                    id: numberField
                    objectName: "preferenceNumberChoiceField"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    property bool invalid: false
                    selectByMouse: true
                    inputMethodHints: Qt.ImhDigitsOnly
                    font.pixelSize: theme.dp(14); color: invalid ? theme.danger : theme.foreground
                    Accessible.name: root.label || root.description.title
                    background: Rectangle { radius: theme.dp(root.compact ? 6 : 4); border.width: root.compact ? theme.dp(2) : 1; color: theme.field; border.color: numberField.invalid ? theme.danger : numberField.activeFocus ? theme.accent : theme.border }
                    onAccepted: numberControl.save()
                    Keys.onEscapePressed: numberControl.cancel()
                }
                RedesignButton {
                    objectName: "preferenceNumberChoiceSave"
                    Layout.preferredWidth: theme.dp(26); Layout.fillHeight: true
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    iconName: "check"; text: qsTr("Use this value")
                    onClicked: numberControl.save()
                }
                RedesignButton {
                    objectName: "preferenceNumberChoiceCancel"
                    Layout.preferredWidth: theme.dp(26); Layout.fillHeight: true
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    iconName: "close"; text: qsTr("Cancel")
                    onClicked: numberControl.cancel()
                }
            }
        }
    }
    Component {
        id: actionEditor
        Item {
            implicitHeight: theme.dp(root.compact ? 36 : 32)
            RedesignButton {
                objectName: "preferenceActionButton"
                anchors.right: parent.right
                width: root.presentation.fullWidthAction ? parent.width : theme.dp(100); height: parent.height
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                caption: root.description.buttonLabel || ""; text: caption
                captionFontWeight: root.presentation.fullWidthAction ? Font.DemiBold : Font.Normal
                foreground: root.description.destructive ? theme.danger : theme.foreground
                background: Rectangle { radius: theme.dp(6); color: parent.hovered ? theme.hover : root.description.destructive ? theme.dangerBackground : root.lightTheme ? theme.background : "#191919"; border.color: parent.visualFocus ? parent.foreground : "transparent" }
                onClicked: root.actionRequested(root.optionName)
            }
        }
    }
    Component {
        id: startupVolumeEditor
        RedesignStartupVolume {
            backend: root.backend
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        }
    }
    Component {
        id: toggleEditor
        Item {
            implicitWidth: preferenceToggle.implicitWidth
            implicitHeight: theme.dp(root.compact ? 36 : 34)
            RedesignButton {
                id: preferenceToggle
                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                // Measure both labels so the width stays stable when the state changes.
                implicitWidth: Math.max(theme.dp(68),
                    Math.ceil(onMetrics.advanceWidth) + theme.dp(40),
                    Math.ceil(offMetrics.advanceWidth) + theme.dp(39))
                width: implicitWidth; height: theme.dp(32)
                objectName: "preferenceToggleEditor"
                checkable: true; checked: !!root.displayedValue && (root.editorEnabled || !root.presentation.hideDisabledValue)
                text: root.label || root.description.title
                onClicked: root.commit(checked)
                TextMetrics { id: onMetrics; font: stateText.font; text: qsTr("ON") }
                TextMetrics { id: offMetrics; font: stateText.font; text: qsTr("OFF") }
                background: Rectangle {
                    radius: theme.dp(6); color: root.compact && parent.checked ? theme.progress : theme.field
                    border.width: parent.visualFocus ? 1 : 0; border.color: theme.accent
                }
                contentItem: Item {
                    Rectangle {
                        visible: parent.parent.checked && !root.compact
                        x: theme.dp(3); y: theme.dp(3)
                        width: parent.width - theme.dp(35); height: parent.height - theme.dp(6)
                        radius: theme.dp(3); color: theme.progress
                    }
                    Rectangle {
                        visible: !parent.parent.checked
                        x: theme.dp(32); y: theme.dp(3)
                        width: parent.width - x - theme.dp(3); height: parent.height - theme.dp(6)
                        radius: theme.dp(3); color: root.lightTheme ? theme.background : "#191919"
                    }
                    Rectangle {
                        x: parent.parent.checked ? parent.width - width - theme.dp(3) : theme.dp(3)
                        y: theme.dp(3); width: theme.dp(26); height: parent.height - theme.dp(6)
                        radius: theme.dp(3); color: parent.parent.checked ? root.compact ? theme.accent : theme.menuAccent : "#404040"
                    }
                    Text {
                        id: stateText
                        x: parent.parent.checked ? theme.dp(8) : theme.dp(root.compact ? 36 : 34)
                        anchors.verticalCenter: parent.verticalCenter
                        text: parent.parent.checked ? qsTr("ON") : qsTr("OFF")
                        font.pixelSize: theme.dp(12); color: parent.parent.checked ? theme.accent : theme.playerLabel
                    }
                }
            }
        }
    }
    Component {
        id: languageEditor
        Item {
            id: languageControl
            implicitHeight: theme.dp(root.presentation.editorHeight || (root.compact ? 36 : 34))
            property bool custom: false
            function cancelCustom() {
                if (languageOrder.invalid) root.backend.cancelLanguageEdit(root.optionName)
                languageOrder.invalid=false;custom=false
            }
            readonly property var shortcutContext: root.backend.ctx === undefined ? null : root.backend.ctx
            readonly property var controlWindow: languageControl.Window.window
            readonly property var hostWindow: controlWindow && controlWindow.renderWindow !== undefined ? controlWindow.renderWindow : controlWindow
            Binding {
                target: languageControl.shortcutContext
                property: "useGlobalShortcuts"; value: false
                when: !!languageControl.shortcutContext && languageControl.visible
                    && languageControl.hostWindow && languageControl.hostWindow.visible
                    && (languageMenu.visible || languageControl.custom)
                restoreMode: Binding.RestoreBindingOrValue
            }
            RedesignComboBox {
                id: languageCombo
                objectName: "preferenceChoiceEditor"
                leftPadding: theme.dp(root.compact ? 14 : 16)
                background: Rectangle {
                    radius: theme.dp(6)
                    color: root.compact ? theme.field : root.lightTheme ? theme.background : "#191919"
                    border.width: theme.dp(2)
                    border.color: languageCombo.visualFocus ? theme.accent : root.compact ? root.lightTheme ? theme.border : "#191919" : theme.field
                }
                anchors.fill: parent; visible: !languageControl.custom
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                model: root.choices; textRole: "label"; valueRole: "value"
                currentIndex: { languageControl.custom; return model.findIndex(row => row.value === root.currentValue) }
                onActivated: index => {
                    const selected = model[index].value
                    if (selected === "__custom__") {
                        languageOrder.text = root.currentValue
                        languageOrder.invalid = false; languageControl.custom = true
                        languageOrder.forceActiveFocus(); languageOrder.selectAll()
                    } else root.commit(selected)
                }
                popup: C.Popup {
                    id: languageMenu
                    objectName: "languageChoicePopup"
                    x: languageCombo.width - width; y: languageCombo.height + root.theme.dp(4)
                    width: Math.max(languageCombo.width, root.theme.dp(260)); padding: root.theme.dp(4)
                    implicitHeight: Math.min(root.theme.dp((Math.max(entries.length,1)+1)*34+12), root.theme.dp(320))
                    margins: root.theme.dp(8); focus: true
                    readonly property var entries: root.choices.map((row,index) => ({label:row.label,value:row.value,index:index})).filter(row =>
                        row.label.toLocaleLowerCase().includes(languageSearch.text.trim().toLocaleLowerCase()) || String(row.value).toLowerCase().includes(languageSearch.text.trim().toLowerCase()))
                        .sort((a,b) => matchRank(a)-matchRank(b))
                    function matchRank(row) {
                        const query=languageSearch.text.trim().toLocaleLowerCase()
                        const label=row.label.toLocaleLowerCase(),code=String(row.value).toLowerCase()
                        return label===query || code===query ? 0 : label.startsWith(query) || code.startsWith(query) ? 1 : 2
                    }
                    function choose(index) {
                        const row=entries[index]
                        if (!row) return
                        languageCombo.activated(row.index); close()
                    }
                    onOpened: {
                        languageSearch.text="";languageSearch.forceActiveFocus()
                        languageList.currentIndex=Math.max(0, entries.findIndex(row => row.value===root.currentValue))
                        languageList.positionViewAtIndex(languageList.currentIndex,ListView.Contain)
                    }
                    onClosed: if (languageControl.custom) Qt.callLater(() => { languageOrder.forceActiveFocus(); languageOrder.selectAll() })
                    background: Rectangle { color: root.theme.panel; radius: root.theme.dp(6); border.color: root.theme.border }
                    contentItem: ColumnLayout {
                        spacing: root.theme.dp(4)
                        C.TextField {
                            id: languageSearch
                            objectName: "languageChoiceSearch"
                            Layout.fillWidth: true; Layout.preferredHeight: root.theme.dp(34)
                            placeholderText: qsTr("Search languages"); Accessible.name: placeholderText
                            color: root.theme.foreground; font.pixelSize: root.theme.dp(14); selectByMouse: true
                            background: Rectangle { color: root.theme.background; radius: root.theme.dp(4); border.color: languageSearch.activeFocus ? root.theme.accent : root.theme.border }
                            onTextChanged: languageList.currentIndex=0
                            onAccepted: languageMenu.choose(languageList.currentIndex)
                            Keys.onDownPressed: { languageList.forceActiveFocus();languageList.currentIndex=0 }
                        }
                        ListView {
                            id: languageList
                            objectName: "languageChoiceList"
                            Layout.fillWidth: true; Layout.fillHeight: true
                            visible: languageMenu.entries.length>0
                            clip: true; keyNavigationEnabled: true
                            model: languageMenu.entries
                            Keys.onReturnPressed: languageMenu.choose(currentIndex)
                            Keys.onEnterPressed: languageMenu.choose(currentIndex)
                            delegate: C.ItemDelegate {
                                required property var modelData
                                required property int index
                                width: ListView.view.width; height: root.theme.dp(root.theme.compactMenus ? 28 : 34)
                                leftPadding: root.theme.compactMenus ? root.theme.dp(8) : padding; rightPadding: leftPadding
                                topPadding: root.theme.compactMenus ? 0 : padding; bottomPadding: topPadding
                                text: modelData.label; highlighted: languageList.currentIndex===index
                                font.pixelSize: root.theme.dp(14)
                                onClicked: languageMenu.choose(index)
                                contentItem: Text { text: parent.text; font: parent.font; color: root.theme.foreground; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                                background: Rectangle { color: parent.highlighted || parent.hovered ? root.theme.hover : "transparent"; radius: root.theme.dp(4) }
                            }
                            C.ScrollIndicator.vertical: C.ScrollIndicator {}
                        }
                        Text {
                            visible: languageMenu.entries.length===0
                            Layout.fillWidth: true; Layout.preferredHeight: root.theme.dp(34)
                            text: qsTr("No languages found"); color: root.theme.foreground; font.pixelSize: root.theme.dp(14)
                        }
                    }
                }
            }
            RowLayout {
                anchors.fill: parent; visible: languageControl.custom; spacing: root.theme.dp(4)
                RedesignButton {
                    objectName: "languageOrderCancel"
                    Layout.preferredWidth: root.theme.dp(26); Layout.fillHeight: true
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    iconName: "back"; text: qsTr("Cancel language order")
                    onClicked: languageControl.cancelCustom()
                }
                C.TextField {
                    id: languageOrder
                    objectName: "preferenceLanguageOrderEditor"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    property bool invalid: false
                    placeholderText: "en, ja"
                    Accessible.name: qsTr("Language names or codes, separated by commas")
                    color: root.theme.foreground; font.pixelSize: root.theme.dp(14); selectByMouse: true
                    background: Rectangle { color: root.theme.background; radius: root.theme.dp(4); border.color: languageOrder.invalid ? root.theme.danger : languageOrder.activeFocus ? root.theme.accent : root.theme.field }
                    function save() {
                        invalid = !root.commit(text)
                        if (!invalid) languageControl.custom = false
                    }
                    onAccepted: save()
                    Keys.onEscapePressed: event => { languageControl.cancelCustom(); event.accepted = true }
                }
                RedesignButton {
                    objectName: "languageOrderSave"
                    Layout.preferredWidth: root.theme.dp(26); Layout.fillHeight: true
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    iconName: "check"; text: qsTr("Use this language order")
                    onClicked: languageOrder.save()
                }
            }
        }
    }
    Component {
        id: choiceEditor
        RedesignComboBox {
            id: choice
            hoverEnabled: true
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            objectName: "preferenceChoiceEditor"
            leftPadding: theme.dp(root.compact ? 14 : 16)
            Accessible.name: root.label || root.description.title
            implicitHeight: theme.dp(root.presentation.editorHeight || 36)
            model: root.choices; textRole: "label"; valueRole: "value"
            showCurvePreviews: root.choices.some(row => row.curve !== undefined)
            showSelectionMark: !!root.presentation.showSelectionMark || showCurvePreviews
            choiceHeight: root.presentation.choiceHeight || 34
            popupGap: root.presentation.popupGap !== undefined ? root.presentation.popupGap : 4
            currentIndex: {
                for (let i = 0; i < count; ++i) if (model[i].value == root.displayedValue) return i
                return -1
            }
            onAboutToOpen: if (root.presentation.dynamicChoices) root.backend.refresh()
            onActivated: root.commit(currentValue)
            RedesignToolTip {
                theme: root.theme; text: choice.displayText; contentTextFormat: Text.PlainText
                visible: choice.hovered && choice.contentItem.truncated
            }
        }
    }
    Component {
        id: opticalDriveEditor
        RedesignComboBox {
            id: drive
            objectName: "preferenceChoiceEditor"
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            implicitHeight: theme.dp(36)
            editable: true
            model: root.choices; textRole: "label"; valueRole: "value"
            currentIndex: model.findIndex(row => row.value === root.currentValue)
            Accessible.name: root.label
            contentItem: TextInput {
                objectName: "preferenceOpticalDriveField"
                text: drive.editText; font: drive.font; color: theme.foreground
                selectionColor: theme.accent; selectedTextColor: theme.background
                verticalAlignment: TextInput.AlignVCenter; selectByMouse: true; clip: true
                onActiveFocusChanged: if (activeFocus) selectAll()
                onEditingFinished: {
                    const selected = root.choices.find(row => row.label === text)
                    root.commit(selected ? selected.value : text)
                }
            }
            onAboutToOpen: root.backend.refresh()
            onActivated: root.commit(currentValue)
        }
    }
    Component {
        id: sliderEditor
        ColumnLayout {
            implicitHeight: theme.dp(root.presentation.sliderHeight || (root.presentation.inlineSlider ? 34 : 48))
            spacing: theme.dp(root.presentation.sliderSpacing === undefined ? 2 : root.presentation.sliderSpacing)
            C.Slider {
                id: slider
                objectName: "preferenceSliderEditor"
                Layout.fillWidth: true
                implicitHeight: theme.dp(28)
                leftPadding: root.presentation.sliderPadding === undefined ? padding : theme.dp(root.presentation.sliderPadding)
                rightPadding: leftPadding
                from: root.presentation.from === undefined ? root.description.minimum : root.presentation.from
                to: root.presentation.to === undefined ? root.description.maximum : root.presentation.to
                stepSize: root.presentation.step || (root.description.kind === "integer" ? 1 : 0.01)
                value: Number(root.currentValue) * root.factor
                Accessible.name: root.label || root.description.title
                Keys.onPressed: event => {
                    if (event.key !== Qt.Key_Home && event.key !== Qt.Key_End) return
                    const value = (event.key === Qt.Key_Home ? from : to) / root.factor
                    root.commit(root.description.kind === "integer" ? Math.round(value) : value)
                    event.accepted = true
                }
                background: Rectangle {
                    x: slider.leftPadding; y: (slider.height - height) / 2
                    width: slider.availableWidth; height: theme.dp(root.presentation.trackHeight || 14); radius: theme.dp(root.presentation.trackRadius || 5); color: theme.field
                    Rectangle {
                        id: sliderFill
                        readonly property real origin: {
                            const crossesZero = !root.presentation.fillFromMinimum
                                && Math.min(slider.from, slider.to) < 0 && Math.max(slider.from, slider.to) > 0
                            const position = crossesZero ? -slider.from / (slider.to - slider.from) : 0
                            return slider.mirrored ? 1 - position : position
                        }
                        readonly property real inset: theme.dp(root.presentation.trackInset || 0)
                        x: inset + Math.min(slider.visualPosition, origin) * (parent.width - 2 * inset)
                        y: inset
                        width: Math.abs(slider.visualPosition - origin) * (parent.width - 2 * inset)
                        height: parent.height - 2 * inset; radius: Math.max(0, parent.radius - inset); color: theme.progress
                    }
                    Repeater {
                        model: root.presentation.trackTicks || []
                        Rectangle {
                            required property int index
                            required property real modelData
                            objectName: "preferenceTickMark_" + index
                            visible: modelData > Math.min(slider.from, slider.to) && modelData < Math.max(slider.from, slider.to)
                            width: theme.dp(2); height: theme.dp(8); radius: theme.dp(1)
                            anchors.verticalCenter: parent.verticalCenter
                            readonly property real position: (modelData - slider.from) / (slider.to - slider.from)
                            readonly property real inset: theme.dp(root.presentation.trackInset || 0)
                            x: inset + (slider.mirrored ? 1 - position : position) * (parent.width - 2 * inset) - width / 2
                            color: {
                                if (root.lightTheme) return theme.sliderTick
                                const center = x + width / 2
                                const base = sliderFill.width > 0 && center >= sliderFill.x && center <= sliderFill.x + sliderFill.width
                                    ? theme.progress : theme.field
                                const ink = theme.sliderTick
                                return Qt.rgba(1 - (1 - base.r) * (1 - ink.r),
                                    1 - (1 - base.g) * (1 - ink.g),
                                    1 - (1 - base.b) * (1 - ink.b), 1)
                            }
                        }
                    }
                }
                handle: Rectangle {
                    x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
                    y: (slider.height - height) / 2
                    width: theme.dp(8); height: theme.dp(24); radius: theme.dp(4); color: root.compact ? theme.accent : theme.menuAccent
                    border.width: slider.visualFocus ? 1 : 0; border.color: theme.bright
                }
                onMoved: root.commit(root.description.kind === "integer" ? Math.round(value / root.factor) : value / root.factor)
                hoverEnabled: true
                RedesignToolTip { theme: root.theme; text: Number(parent.value).toFixed(root.presentation.decimals || 0) + (root.presentation.suffix || ""); visible: parent.hovered || parent.pressed; delay: 0 }
            }
            RowLayout {
                visible: !root.presentation.inlineSlider && !root.presentation.ticks
                Layout.fillWidth: true
                Text { text: slider.from + (root.presentation.suffix || ""); color: theme.foreground; font.pixelSize: theme.dp(11) }
                Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: Number(slider.value).toFixed(root.presentation.decimals || 0) + (root.presentation.suffix || ""); color: theme.foreground; font.pixelSize: theme.dp(11) }
                Text { text: slider.to + (root.presentation.suffix || ""); color: theme.foreground; font.pixelSize: theme.dp(11) }
            }
            Item {
                Layout.fillWidth: true; Layout.preferredHeight: theme.dp(root.presentation.tickHeight || 14)
                visible: !root.presentation.inlineSlider && !!root.presentation.ticks
                Repeater {
                    model: parent.width < theme.dp(280) && root.presentation.compactTicks ? root.presentation.compactTicks : root.presentation.ticks || []
                    Text {
                        required property int index
                        required property var modelData
                        objectName: "preferenceSliderTick_" + index
                        readonly property real position: (Number(modelData) - slider.from) / (slider.to - slider.from)
                        readonly property real visualPosition: slider.mirrored ? 1 - position : position
                        readonly property real markerCenter: slider.leftPadding + slider.handle.width / 2
                            + visualPosition * (slider.availableWidth - slider.handle.width) + theme.dp(0.5)
                        x: root.presentation.alignTicksToHandle
                            ? Math.max(theme.dp(2), Math.min(parent.width - width - theme.dp(2), markerCenter - width / 2))
                            : visualPosition * parent.width - (visualPosition === 0 ? 0 : visualPosition === 1 ? width : width / 2)
                        text: root.presentation.tickLabels?.[index] ?? ((root.presentation.positiveTickSign && modelData > 0 ? "+" : "")
                            + Number(modelData).toLocaleString(Qt.locale(), 'f', Number.isInteger(Number(modelData)) ? 0 : String(modelData).split('.')[1].length)
                            + (root.presentation.tickSuffix === undefined ? root.presentation.suffix || "" : root.presentation.tickSuffix))
                        color: root.presentation.mutedTicks ? theme.playerLabel : theme.foreground; font.pixelSize: theme.dp(root.presentation.tickFontSize || 11)
                    }
                }
            }
        }
    }
    Component {
        id: colorEditor
        RedesignButton {
            objectName: "preferenceColorEditor"
            implicitHeight: theme.dp(root.presentation.editorHeight || (root.compact ? 36 : 34))
            leftPadding: theme.dp(root.presentation.flatColorField ? 12 : 14); rightPadding: theme.dp(root.presentation.flatColorField ? 5 : root.compact ? 7 : 6)
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            readonly property string chosenColor: "#" + Number(root.currentValue).toString(16).padStart(6, "0")
            caption: chosenColor.toUpperCase(); text: qsTr("Select %1").arg(root.label)
            background: Rectangle { radius: theme.dp(root.compact ? 6 : 4); border.width: root.presentation.flatColorField ? 0 : root.compact ? theme.dp(2) : 1; color: theme.field; border.color: root.compact && !root.lightTheme ? "#191919" : theme.border }
            contentItem: RowLayout {
                Text { Layout.fillWidth: true; text: parent.parent.caption; color: theme.foreground; font.pixelSize: theme.dp(14) }
                Rectangle { objectName: "preferenceColorSwatch"; width: theme.dp(root.presentation.flatColorField || root.compact ? 22 : 20); height: width; color: parent.parent.chosenColor; border.width: root.presentation.flatColorField ? Math.max(color.r, color.g, color.b) > 0.2 ? 0 : 1 : root.compact ? theme.dp(1.5) : 1; border.color: root.presentation.flatColorField && !root.lightTheme ? "#323232" : root.compact ? Qt.rgba(1, 1, 1, .21) : theme.border; radius: theme.dp(2) }
            }
            onClicked: { const color = root.backend.chooseColor(chosenColor); if (color.length) root.commit(parseInt(color.slice(1), 16)) }
        }
    }
    Component {
        id: trackEditor
        T.SpinBox {
            id: trackField
            objectName: "preferenceTrackEditor"
            implicitHeight: theme.dp(root.presentation.editorHeight || (root.compact ? 36 : 34))
            from: 0; to: root.description.maximum
            value: Number(root.currentValue)
            editable: true
            font.pixelSize: theme.dp(14)
            leftPadding: theme.dp(14); rightPadding: theme.dp(10)
            textFromValue: (value, locale) => value === 0 ? qsTr("Automatic") : String(value)
            function parseTrack(text) {
                const input = text.trim()
                if (input.toLocaleLowerCase() === qsTr("Automatic").toLocaleLowerCase()) return 0
                const number = Number(input)
                return input.length && Number.isInteger(number) && number >= 1 && number <= to ? number : undefined
            }
            valueFromText: (text, locale) => parseTrack(text) ?? value
            contentItem: TextInput {
                property bool edited: false
                property string draftValue: ""
                text: trackField.displayText; font: trackField.font; color: theme.foreground
                onTextEdited: { edited = true; draftValue = text }
                onEditingFinished: {
                    if (!edited) return
                    edited = false
                    const parsed = trackField.parseTrack(draftValue)
                    // Explicitly choosing the same number must clear any legacy ID.
                    if (parsed !== undefined) root.commit(parsed)
                }
                selectionColor: theme.accent; selectedTextColor: theme.background
                verticalAlignment: TextInput.AlignVCenter; selectByMouse: true; clip: true
                onActiveFocusChanged: if (activeFocus) selectAll()
            }
            background: Rectangle {
                radius: theme.dp(6); color: theme.field
                border.width: theme.dp(2)
                border.color: trackField.activeFocus ? theme.accent : root.lightTheme ? theme.border : "#191919"
            }
            Accessible.name: root.label
            onValueModified: root.commit(value)
        }
    }
    Component {
        id: textEditor
        RowLayout {
            implicitHeight: theme.dp(36)
            spacing: theme.dp(10)
            C.TextField {
                id: field
                objectName: "preferenceTextEditor"
                Layout.fillWidth: true; Layout.fillHeight: true
                property bool invalid: false
                readonly property bool numeric: root.description.kind === "integer" || root.description.kind === "float"
                readonly property string formattedValue: root.presentation.hideDisabledValue && !root.editorEnabled ? "" : root.currentValue === undefined ? "" : numeric
                    ? root.presentation.decimals === undefined ? String(Number(root.currentValue) * root.factor) : (Number(root.currentValue) * root.factor).toFixed(root.presentation.decimals)
                    : String(root.currentValue)
                Binding on text {
                    when: !field.activeFocus && !field.invalid
                    restoreMode: Binding.RestoreNone
                    value: field.formattedValue
                }
                leftPadding: theme.dp(14); rightPadding: theme.dp(10)
                Layout.minimumHeight: theme.dp(36)
                color: invalid ? "#e56b6b" : theme.foreground; font.pixelSize: theme.dp(14)
                selectByMouse: true
                placeholderText: root.description.placeholder || root.presentation.placeholder || ""
                placeholderTextColor: theme.playerLabel
                autoScroll: root.optionName !== "startup-options" || activeFocus
                echoMode: root.description.kind === "password" ? TextInput.Password : TextInput.Normal
                inputMethodHints: root.description.kind === "integer" || root.description.kind === "float" ? Qt.ImhFormattedNumbersOnly : Qt.ImhNone
                Accessible.name: root.label || root.description.title
                background: Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: -theme.dp(root.presentation.fieldOutset || 0)
                    anchors.rightMargin: -theme.dp(root.presentation.fieldOutset || 0)
                    radius: theme.dp(root.presentation.fieldRadius || 6); border.width: theme.dp(2); color: theme.field
                    border.color: field.invalid ? "#e56b6b" : field.activeFocus ? theme.accent : !root.lightTheme ? "#191919" : theme.border
                }
                onEditingFinished: {
                    if (!root.editorEnabled) return
                    const number = Number(text)
                    invalid = numeric ? !text.trim().length || !Number.isFinite(number) || !root.commit(root.description.kind === "integer" ? Math.round(number / root.factor) : number / root.factor) : !root.commit(text)
                    if (!invalid) text = formattedValue
                }
                Connections {
                    target: root
                    function onEditorResetRequested() { field.invalid = false; field.text = field.formattedValue }
                    function onEditorEnabledChanged() {
                        if (root.presentation.hideDisabledValue && !root.editorEnabled) { field.invalid = false; field.text = "" }
                    }
                }
                TextMetrics { id: valueMetrics; text: field.text; font: field.font }
                Text {
                    x: field.leftPadding + valueMetrics.advanceWidth
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.presentation.suffix || ""; color: theme.foreground; font.pixelSize: theme.dp(14)
                    visible: !field.activeFocus && (!root.presentation.hideDisabledValue || root.editorEnabled)
                }
                onActiveFocusChanged: if (activeFocus) selectAll()
            }
            RedesignButton {
                visible: root.editor === "file" || root.editor === "directory"
                Layout.preferredWidth: root.compact ? implicitWidth : theme.dp(100)
                Layout.preferredHeight: theme.dp(32)
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                filled: true; captionFontWeight: Font.DemiBold
                background: Rectangle { radius: theme.dp(6); color: parent.hovered ? theme.hover : root.lightTheme ? theme.background : "#191919"; border.width: parent.visualFocus ? 1 : 0; border.color: theme.accent }
                caption: qsTr("Browse"); text: caption
                onClicked: { const path = root.backend.choosePath(root.optionName); if (path.length) root.commit(path) }
            }
        }
    }
}

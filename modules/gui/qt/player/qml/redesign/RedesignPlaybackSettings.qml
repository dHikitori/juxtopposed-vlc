// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQml.Models

ColumnLayout {
    id: root
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    property Component videoBackdropContent
    property bool showStatistics: false
    property bool showSubtitleTimeline: false
    property string subtitleTimelineState: "off"
    property bool reversed: false
    property bool reverseAvailable: false
    property real playbackRate: 1
    property bool rateAvailable: false
    property int sleepTimerMinutes: 0
    property bool showWaveform: false
    property bool waveformAvailable: true
    property string waveformState: "off"
    property var visualizationModel
    readonly property var sleepChoices: [
        {label: qsTr("Off"), value: 0}, {label: qsTr("15 minutes"), value: 15},
        {label: qsTr("30 minutes"), value: 30}, {label: qsTr("60 minutes"), value: 60},
        {label: qsTr("90 minutes"), value: 90}
    ]
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    signal statisticsRequested(bool enabled)
    signal subtitleTimelineRequested(bool enabled)
    signal reverseRequested(bool reversed)
    signal rateRequested(real rate)
    signal sleepTimerRequested(int minutes)
    signal visualizationRequested(int index)
    signal waveformRequested()
    spacing: 0

    RedesignSwitchRow {
        objectName: "showSubtitleTimelineSetting"
        Layout.fillWidth: true
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        label: qsTr("Show Subtitle Timeline on Seekbar"); checked: root.showSubtitleTimeline
        labelColor: theme.playerLabel
        onToggled: checked => root.subtitleTimelineRequested(checked)
    }
    Text {
        Layout.fillWidth: true; Layout.bottomMargin: theme.dp(10)
        visible: root.showSubtitleTimeline && root.subtitleTimelineState !== "ready" && root.subtitleTimelineState !== "off"
        text: root.subtitleTimelineState === "loading" ? qsTr("Reading subtitle times…")
            : root.subtitleTimelineState === "no-track" ? qsTr("Select a subtitle track to show its times.")
            : root.subtitleTimelineState === "empty" ? qsTr("This subtitle track has no cue times.")
            : qsTr("Hakone cannot read the times for this subtitle track.")
        color: theme.foreground; font.pixelSize: theme.dp(12); wrapMode: Text.WordWrap
    }
    RedesignSwitchRow {
        objectName: "showStatisticsSetting"
        Layout.fillWidth: true
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        label: qsTr("Show Statistics"); checked: root.showStatistics
        labelColor: theme.playerLabel
        onToggled: checked => root.statisticsRequested(checked)
    }
    RedesignSwitchRow {
        objectName: "reverseSetting"
        Layout.fillWidth: true
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        label: qsTr("Reverse"); checked: root.reversed; enabled: root.reverseAvailable || root.reversed
        labelColor: theme.playerLabel
        onToggled: checked => root.reverseRequested(checked)
    }
    RowLayout {
        Layout.fillWidth: true
        Layout.minimumHeight: theme.dp(68)
        spacing: theme.dp(12)
        Text {
            Layout.fillWidth: true
            text: qsTr("Sleep Timer"); color: theme.playerLabel
            font.pixelSize: theme.dp(14); wrapMode: Text.WordWrap
        }
        RedesignComboBox {
            objectName: "sleepTimerChoice"
            Layout.preferredWidth: theme.dp(220)
            Layout.minimumWidth: 0
            implicitHeight: theme.dp(48)
            videoBackdropContent: root.videoBackdropContent
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            model: root.sleepChoices; textRole: "label"; valueRole: "value"
            currentIndex: root.sleepChoices.findIndex(choice => choice.value === root.sleepTimerMinutes)
            Accessible.name: qsTr("Sleep Timer")
            onActivated: root.sleepTimerRequested(currentValue)
        }
    }
    Instantiator {
        id: visualizations
        model: root.visualizationModel
        delegate: QtObject {
            required property string display
            required property bool checked
        }
        onObjectAdded: root.visualizationRevision++
        onObjectRemoved: root.visualizationRevision++
    }
    property int visualizationRevision: 0
    readonly property var visualizerOptions: {
        visualizationRevision
        const options = []
        const labels = {"": qsTr("Disabled"), scope: qsTr("Scope"), spectrum: qsTr("Spectrum"), spectrometer: qsTr("Spectrometer"), vuMeter: qsTr("VU meter"), glspectrum: qsTr("3D spectrum"), projectm: "projectM"}
        const icons = {scope: "visual-scope", spectrum: "visual-spectrum", spectrometer: "visual-spectrometer"}
        for (let i = 0; i < visualizations.count; ++i) {
            const entry = visualizations.objectAt(i)
            if (!entry) continue
            const value = root.visualizationModel && typeof root.visualizationModel.choiceValue === "function"
                ? String(root.visualizationModel.choiceValue(i)) : entry.display === "Disable" ? "" : entry.display.toLowerCase()
            options.push({label: labels[value] || entry.display, iconName: icons[value] || "", nativeIndex: i,
                checked: entry.checked, enabled: true, value: value})
        }
        if (options.length) {
            const meter = options.findIndex(option => option.value === "vuMeter")
            options.splice(meter >= 0 ? meter : Math.min(4, options.length), 0,
                {label: qsTr("Waveform"), iconName: "visual-waveform", waveform: true, enabled: root.waveformAvailable || root.showWaveform})
        }
        return options
    }
    RowLayout {
        objectName: "visualizerSetting"
        Layout.fillWidth: true; Layout.minimumHeight: theme.dp(68)
        spacing: theme.dp(12)
        Text {
            Layout.fillWidth: true
            text: qsTr("Audio Visualizer"); color: theme.playerLabel; font.pixelSize: theme.dp(14)
        }
        RedesignComboBox {
            objectName: "modelChoiceField"
            Layout.preferredWidth: theme.dp(220); Layout.minimumWidth: 0
            implicitHeight: theme.dp(48); popupGap: 20
            videoBackdropContent: root.videoBackdropContent
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            model: root.visualizerOptions; textRole: "label"
            currentIndex: root.showWaveform ? root.visualizerOptions.findIndex(option => option.waveform)
                : root.visualizerOptions.findIndex(option => !option.waveform && option.checked)
            displayText: currentIndex < 0 ? qsTr("Disabled") : currentText
            enabled: count > 0
            opacity: enabled ? 1 : .5
            showSelectionMark: true; showIcons: true; choiceHeight: 46
            restrictChoices: true
            Accessible.name: qsTr("Audio Visualizer")
            onActivated: index => {
                const choice = root.visualizerOptions[index]
                if (choice.waveform) root.waveformRequested()
                else root.visualizationRequested(choice.nativeIndex)
            }
        }
    }
    Text {
        Layout.fillWidth: true; Layout.bottomMargin: theme.dp(8)
        visible: root.showWaveform && root.waveformState === "unavailable"
        text: qsTr("Hakone cannot read audio levels for this file.")
        color: theme.foreground; font.pixelSize: theme.dp(12); wrapMode: Text.WordWrap
    }
    RedesignSettingSlider {
        objectName: "playbackRateControl"
        Layout.fillWidth: true; Layout.topMargin: theme.dp(16)
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        label: qsTr("Speed"); suffix: "x"; decimals: 5; trimTrailingZeros: true
        labelColor: theme.playerLabel
        valueColor: theme.playerLabel
        from: 0.25; to: 2; stepSize: 0.05
        inputFrom: 0.03125; inputTo: 31.25
        ticks: [0.25, 0.5, 0.75, 1, 1.25, 1.5, 1.75, 2]
        tickWidth: 2; tickHeight: 8; tickRadius: 1; ticksUseFullTrack: true
        tickColor: theme.sliderTick; screenBlendTicks: !lightTheme; showTickEndpoints: false
        valueFieldHeight: 32; valueFieldOutline: false
        sliderTopMargin: 12
        sliderLeftMargin: -4
        currentValue: Math.abs(root.playbackRate); enabled: root.rateAvailable
        onMoved: value => root.rateRequested(value)
    }
}

// SPDX-License-Identifier: GPL-2.0-or-later
import QtQml

QtObject {
    function interfaceSlider(options) {
        return Object.assign({editor: "slider", rowHeight: 57, alignSliderTop: true,
            sliderTopMargin: 3, sliderPadding: 0, trackHeight: 16, trackRadius: 6, trackInset: 2, hideHelp: true}, options)
    }
    function pictureSlider(options) {
        return Object.assign({editor: "slider", inlineSlider: true, sliderPadding: 0,
            trackHeight: 16, trackRadius: 6, trackInset: 2, fillFromMinimum: true, hideHelp: true}, options)
    }
    function subtitleSlider(options) {
        return Object.assign({editor: "slider", rowHeight: 67, sliderHeight: 51,
            sliderSpacing: 8, tickHeight: 15, tickFontSize: 12, mutedTicks: true,
            alignTicksToHandle: false, alignSliderTop: true, sliderTopMargin: 7,
            sliderPadding: 0, trackHeight: 16, trackRadius: 6, trackInset: 2,
            fillFromMinimum: true, hideHelp: true}, options)
    }
    function visibleSections(category, backend) {
        return (sections[category] || []).filter(section => !section.appearance
            && (section.rows.some(row => backend.describe(row[0]).kind !== undefined)
                || category === "controls" && section.title === qsTr("Keyboard Shortcuts")))
    }
    readonly property var pageTitles: ({
        general: qsTr("Global Settings"), interface: qsTr("Interface Settings"), privacy: qsTr("Privacy Settings"),
        controls: qsTr("Mouse, Keyboard & Remote Controls"), playback: qsTr("Playback Preferences"),
        video: qsTr("Video Defaults"), audio: qsTr("Audio Defaults"), subtitles: qsTr("Subtitle Defaults"),
        library: qsTr("Playlists, Folders & Files"), network: qsTr("Network Connections & Security"),
        performance: qsTr("Performance Settings"), recording: qsTr("Recording, Casting & Streaming Out"),
        extensions: qsTr("Add-ons & Scripts")
    })
    // Order, grouping, default visibility and editor choice follow the Figma
    // screens. Unsupported options are omitted by the native metadata adapter.
    readonly property var sections: ({
        general: [
            { title: qsTr("Appearance"), appearance: true, rows: [
                ["qt-backdrop-blur", qsTr("Transparency and blur")],
                ["qt-compact-menus", qsTr("Compact menus"), {help: qsTr("Show menus without icons. Decrease the space around menu items.")}] ] },
            { title: qsTr("Startup"), rows: [
                ["qt-error-dialogs", qsTr("Show confirmation and error dialogs")],
                ["qt-updates-notif", qsTr("Check for updates automatically")] ] },
            { title: qsTr("Diagnostics"), rows: [
                ["qt-verbose", qsTr("Developer mode")], ["file-logging", qsTr("Save a log")], ["logfile", qsTr("Log file"), {enabledWhen: "file-logging", rowHeight: 46}],
                ["redesign-log-details", qsTr("Log details"), {dynamicChoices: true}] ] } ],
        interface: [
            { title: qsTr("Skin"), rows: [ ["skins2-last", qsTr("Skin file")] ] },
            { title: qsTr("Window"), rows: [
                ["embedded-video", qsTr("Play video inside main window")],
                ["qt-video-autoresize", qsTr("Resize window to fit video"), {choices: [{label: qsTr("Never"), value: false}, {label: qsTr("Always"), value: true}]}],
                ["qt-remember-window-geometry", qsTr("Remember window size and position")],
                ["video-on-top", qsTr("Keep window above other windows")],
                ["video-deco", qsTr("Show window decorations"), {help: qsTr("Show a border and title bar on a separate video window. Close Hakone. Start Hakone again to use this setting.")}],
                ["qt-name-in-title", qsTr("Show the file name in the title bar")],
                ["qt-fullscreen-screennumber", qsTr("Open fullscreen on")] ] },
            { title: qsTr("Playback Controls"), rows: [
                ["qt-minimal-view", qsTr("Start in compact mode")],
                ["qt-controls-autohide", qsTr("Auto-hide the controls")],
                ["qt-controls-hide-timeout", qsTr("Hide after"), interfaceSlider({from: 0, to: 10, factor: 0.001, step: 0.1, decimals: 1, suffix: " s", ticks: [0, 5, 10], trackTicks: [0.5, 1, 1.5, 2, 2.5, 3, 3.5, 4, 4.5, 5, 5.5, 6, 6.5, 7, 7.5, 8, 8.5, 9, 9.5]})],
                ["qt-fs-controller", qsTr("Show controls in fullscreen")] ] },
            { title: qsTr("Notifications and System Tray"), rows: [
                ["qt-system-tray", qsTr("Show a system tray icon")],
                ["qt-start-minimized", qsTr("Start minimized to the tray")],
                ["qt-notification", qsTr("Show a popup when the track changes"), {enabledWhen: "qt-system-tray"}],
                ["qt-notification-timeout", qsTr("Popup duration"), interfaceSlider({enabledWhen: "qt-system-tray", from: 0, to: 10, factor: 0.001, step: 0.1, decimals: 1, suffix: " s", ticks: [0, 5, 10], trackTicks: [0.5, 1, 1.5, 2, 2.5, 3, 3.5, 4, 4.5, 5, 5.5, 6, 6.5, 7, 7.5, 8, 8.5, 9, 9.5]})] ] },
            { title: qsTr("Playlist"), rows: [ ["qt-single-click-activate", qsTr("Single click to open or play items")], ["playlist-tree", qsTr("Show the playlist as a tree"), {help: qsTr("Show folders and playlists as groups in the queue. Expand a group to show its items. The playback order does not change.")}] ] } ],
        privacy: [
            { title: qsTr("History"), rows: [
                ["save-recentplay", qsTr("Save recently played items"), {help: qsTr("Keep a list of the items that you play. If this option is OFF, Hakone does not add items to the list.")}],
                ["restore-playback-pos", qsTr("Remember the playback position"), {editor: "toggle", toggleValues: {on: 2, off: 0}}],
                ["recentplay-filter", qsTr("Never remember items matching"), {rowHeight: 46}],
                ["redesign-clear-history", qsTr("Clear history now"), {rowHeight: 42}] ] },
            { title: qsTr("Network"), rows: [
                ["metadata-network-access", qsTr("Fetch titles and cover art from the internet"), {help: qsTr("Get media titles and cover art from the internet.")}],
                ["filter:audio-filter:audioscrobbler", qsTr("Submit played tracks to last.fm")],
                ["lastfm-username", qsTr("Last.fm username"), {rowHeight: 46}], ["lastfm-password", qsTr("Last.fm password"), {rowHeight: 46}],
                ["scrobbler-url", qsTr("Last.fm submission URL"), {rowHeight: 46}] ] } ],
        controls: [
            { title: qsTr("Keyboard Shortcuts"), rows: [] },
            { title: qsTr("Skip Amounts"), rows: [
                ["extrashort-jump-size", qsTr("Very short skip"), {suffix: " s"}], ["short-jump-size", qsTr("Short skip"), {suffix: " s"}],
                ["medium-jump-size", qsTr("Medium skip"), {suffix: " s"}], ["long-jump-size", qsTr("Long skip"), {suffix: " s"}] ] },
            { title: qsTr("Mouse"), rows: [
                ["mouse-pause-policy", qsTr("Single click on the video"), {choices: [{label: qsTr("Play / Pause"), value: "auto"}, {label: qsTr("Do nothing"), value: "disabled"}, {label: qsTr("Play / Pause on release"), value: "pause-on-released"}]}],
                ["mouse-double-click-action", qsTr("Double click on the video")],
                ["hotkeys-y-wheel-mode", qsTr("Scroll wheel"), {choices: [{label: qsTr("Volume"), value: 0}, {label: qsTr("Seek"), value: 2}, {label: qsTr("Seek (reversed)"), value: 3}, {label: qsTr("Do nothing"), value: -1}]}],
                ["hotkeys-x-wheel-mode", qsTr("Horizontal scroll"), {choices: [{label: qsTr("Volume"), value: 0}, {label: qsTr("Seek"), value: 2}, {label: qsTr("Seek (reversed)"), value: 3}, {label: qsTr("Do nothing"), value: -1}]}],
                ["hotkeys-wheel-direction", qsTr("Reverse scroll direction")] ] },
            { title: qsTr("Remotes"), rows: [ ["redesign-web-remote", qsTr("Web remote control")],
                ["http-port", qsTr("Port"), {enabledWhen: "redesign-web-remote"}],
                ["http-password", qsTr("Password"), {enabledWhen: "redesign-web-remote"}] ] } ],
        playback: [
            { title: qsTr("Startup"), rows: [
                ["restore-playback-pos", qsTr("Continue where you left off"), {choiceLabels: {0: qsTr("Never"), 1: qsTr("Ask"), 2: qsTr("Always")}}],
                ["playlist-autostart", qsTr("Start playing as soon as a file opens")],
                ["rate", qsTr("Default playback speed"), {editor: "slider", rowHeight: 67, sliderHeight: 51, sliderSpacing: 8, tickHeight: 15, tickFontSize: 12, mutedTicks: true, alignTicksToHandle: true, alignSliderTop: true, sliderTopMargin: 7, sliderPadding: 0, trackHeight: 16, trackRadius: 6, trackInset: 2, hideHelp: true, from: 0.25, to: 2, step: 0.05, decimals: 2, suffix: "×", tickSuffix: "", ticks: [0.25, 0.5, 0.75, 1, 1.25, 1.5, 1.75, 2], compactTicks: [0.25, 1, 2], trackTicks: [0.25, 0.5, 0.75, 1, 1.25, 1.5, 1.75, 2]}],
                ["playlist-cork", qsTr("Pause when another app uses audio")] ] },
            { title: qsTr("Tracks"), rows: [
                ["preferred-resolution", qsTr("Preferred video quality")], ["redesign-menu-language", qsTr("Preferred menu language"), {editorHeight: 36, dynamicChoices: true}],
                ["redesign-audio-track", qsTr("Default audio track"), {editorHeight: 36, rowHeight: 48}], ["redesign-sub-track", qsTr("Default subtitle track"), {editorHeight: 36, rowHeight: 46}] ] },
            { title: qsTr("Order"), rows: [
                ["redesign-repeat", qsTr("Repeat")], ["random", qsTr("Shuffle")],
                ["play-and-stop", qsTr("Stop after each item")], ["redesign-after-playback", qsTr("When playback finishes")] ] },
            { title: qsTr("Seeking"), rows: [
                ["input-fast-seek", qsTr("Seek precisely"), {inverted: true}],
                ["qt-seek-preview", qsTr("Show video previews on the seekbar")] ] },
            { title: qsTr("Files & Discs"), rows: [ ["dvd", qsTr("Default optical drive"), {editor: "optical-drive", rowHeight: 46, dynamicChoices: true}],
                ["avi-index", qsTr("Repair damaged AVI files")], ["mkv-preload-local-dir", qsTr("Load matching .mkv files from the same folder")] ] } ],
        video: [
            { title: qsTr("Output"), rows: [ ["video", qsTr("Enable video")], ["fullscreen", qsTr("Start videos in fullscreen")],
                ["disable-screensaver", qsTr("Stop the screensaver during playback"), {editor: "toggle", toggleValues: {on: 1, off: 0}}], ["video-wallpaper", qsTr("Play video as desktop wallpaper")] ] },
            { title: qsTr("Picture"), rows: [ ["hue", qsTr("Hue"), pictureSlider({from: -180, to: 180, step: 1, trackTicks: [0]})],
                ["brightness", qsTr("Brightness"), pictureSlider({from: 0, to: 2, decimals: 2, trackTicks: [1]})],
                ["contrast", qsTr("Contrast"), pictureSlider({from: 0, to: 2, decimals: 2, trackTicks: [1]})],
                ["saturation", qsTr("Saturation"), pictureSlider({from: 0, to: 3, decimals: 2, trackTicks: [1, 2]})],
                ["gamma", qsTr("Gamma"), pictureSlider({from: 0.01, to: 10, decimals: 2, trackTicks: [1, 2, 3, 4, 5, 6, 7, 8, 9]})],
                ["redesign-reset-picture", qsTr("Reset picture"), {hideHelp: true, rowHeight: 42}] ] },
            { title: qsTr("Deinterlacing"), rows: [ ["deinterlace", qsTr("Deinterlacing")], ["deinterlace-mode", qsTr("Method"), {choiceLabels: {auto: qsTr("Automatic")}}] ] },
            { title: qsTr("Framing"), rows: [ ["aspect-ratio", qsTr("Aspect ratio"), {dynamicChoices: true}], ["crop", qsTr("Crop"), {dynamicChoices: true}],
                ["autoscale", qsTr("Scale video to the window")], ["redesign-video-zoom", qsTr("Zoom"), {dynamicChoices: true}], ["align", qsTr("Position in the window")] ] },
            { title: qsTr("Snapshots"), rows: [ ["snapshot-path", qsTr("Save to"), {rowHeight: 42, fieldOutset: 2, fieldRadius: 8}], ["snapshot-prefix", qsTr("File name prefix"), {rowHeight: 42, fieldOutset: 2, fieldRadius: 8}], ["snapshot-format", qsTr("Format"), {choiceLabels: {png: "PNG", jpg: "JPG", tiff: "TIFF", webp: "WebP"}}],
                ["redesign-snapshot-size", qsTr("Size"), {dynamicChoices: true}],
                ["snapshot-sequential", qsTr("Number files sequentially")], ["snapshot-preview", qsTr("Show a preview after capture")] ] } ],
        audio: [
            { title: qsTr("Output"), rows: [ ["audio", qsTr("Enable audio")], ["redesign-audio-device", qsTr("Output device"), {dynamicChoices: true}], ["mix-mode", qsTr("Speaker setup"), {choiceLabels: {0: qsTr("Automatic"), 2: qsTr("Headphones"), 3: qsTr("Surround 4.0"), 4: qsTr("Surround 5.1"), 5: qsTr("Surround 7.1")}}] ] },
            { title: qsTr("Volume"), rows: [ ["qt-max-volume", qsTr("Maximum volume"), {editor: "slider", rowHeight: 67, sliderHeight: 50, sliderSpacing: 7, tickHeight: 15, tickFontSize: 12, mutedTicks: true, alignTicksToHandle: true, alignSliderTop: true, sliderTopMargin: 7, sliderPadding: 0, trackHeight: 16, trackRadius: 6, trackInset: 2, from: 100, to: 300, step: 1, suffix: "%", ticks: [100, 200, 300], trackTicks: [125, 150, 175, 200, 225, 250, 275], hideHelp: true}],
                ["volume-save", qsTr("Startup volume"), {editor: "startup-volume", rowHeight: 98, help: qsTr("Select the volume level to use when you start Hakone.")}], ["volume-step", qsTr("Volume step"), {factor: 100 / 256, decimals: 1, suffix: "%"}] ] },
            { title: qsTr("Loudness"), rows: [ ["filter:audio-filter:normvol", qsTr("Normalize loudness")],
                ["norm-max-level", qsTr("Target level"), {enabledWhen: "filter:audio-filter:normvol"}],
                ["filter:audio-filter:compressor", qsTr("Night mode")], ["audio-replay-gain-mode", qsTr("Replay gain"), {choiceLabels: {none: qsTr("Off")}}],
                ["audio-replay-gain-preamp", qsTr("Replay gain preamp"), {decimals: 1}] ] },
            { title: qsTr("Effects"), rows: [ ["filter:audio-filter:equalizer", qsTr("Equalizer")],
                ["redesign-equalizer-preset", qsTr("Equalizer preset"), {equalizer: true, rowHeight: 48, dynamicChoices: true}],
                ["equalizer-2pass", qsTr("Two-pass")],
                ["audio-spatial", qsTr("Spatial audio")],
                ["audio-spatial-mode", qsTr("Spatial audio mode"), {enabledWhen: "audio-spatial"}],
                ["filter:audio-filter:stereo_widen", qsTr("Stereo widening")], ["filter:audio-filter:compressor", qsTr("Compressor")],
                ["pitch-shift", qsTr("Pitch shift"), {decimals: 2, rowHeight: 42}], ["filter:audio-filter:chorus_flanger", qsTr("Chorus/flanger")], ["filter:audio-filter:karaoke", qsTr("Karaoke")] ] },
            { title: qsTr("Tracks"), rows: [ ["redesign-audio-language", qsTr("Preferred audio language"), {editorHeight: 36}], ["audio-desync", qsTr("Audio delay"), {factor: 0.001, decimals: 2, suffix: " s", rowHeight: 46}] ] },
            { title: qsTr("Visualization"), rows: [ ["audio-visual", qsTr("Visualizer"), {choiceLabels: {none: qsTr("Disabled")}}] ] } ],
        subtitles: [
            { title: qsTr("Tracks & Languages"), rows: [ ["spu", qsTr("Enable subtitles")], ["redesign-sub-language", qsTr("Preferred language"), {editorHeight: 36}], ["redesign-subtitle-encoding", qsTr("Text encoding"), {dynamicChoices: true}] ] },
            { title: qsTr("Loading Subtitles"), rows: [ ["sub-autodetect-file", qsTr("Load subtitle files automatically")], ["sub-autodetect-fuzzy", qsTr("File name matching"), {dynamicChoices: true, showSelectionMark: true, choiceHeight: 46, popupGap: 20}] ] },
            { title: qsTr("Appearance"), rows: [ ["freetype-font", qsTr("Font"), {placeholder: qsTr("System, sans-serif")}],
                ["sub-text-size-mode", qsTr("Size values"), {editor: "segmented", editorHeight: 36, rowHeight: 48, inset: 2}],
                ["redesign-subtitle-font-size", qsTr("Font size"), {dynamicChoices: true, editorHeight: 36, hideHelp: true}],
                ["freetype-bold", qsTr("Bold")], ["freetype-color", qsTr("Text color"), {rowHeight: 42, editorHeight: 32, flatColorField: true, hideHelp: true}],
                ["freetype-opacity", qsTr("Text opacity"), subtitleSlider({from: 0, to: 100, factor: 100/255, suffix: "%", ticks: [0, 50, 100], trackTicks: [10, 20, 30, 40, 50, 60, 70, 80, 90]})],
                ["freetype-outline-thickness", qsTr("Outline thickness"), {choices: [{label: qsTr("None"), value: 0}, {label: qsTr("Thin"), value: 1}, {label: qsTr("Normal"), value: 4}, {label: qsTr("Thick"), value: 8}]}], ["freetype-outline-color", qsTr("Outline color"), {rowHeight: 42, editorHeight: 32, flatColorField: true, hideHelp: true}],
                ["freetype-shadow-opacity", qsTr("Shadow"), {editor: "toggle", toggleValues: {on: 128, off: 0}}],
                ["freetype-shadow-color", qsTr("Shadow color"), {rowHeight: 42, editorHeight: 32, flatColorField: true, hideHelp: true}], ["freetype-shadow-distance", qsTr("Shadow distance"), {dynamicChoices: true}],
                ["redesign-subtitle-background", qsTr("Background")], ["freetype-background-color", qsTr("Background color"), {rowHeight: 42, editorHeight: 32, flatColorField: true, hideHelp: true}],
                ["redesign-subtitle-background-opacity", qsTr("Background opacity"), subtitleSlider({from: 0, to: 100, factor: 100/255, suffix: "%", ticks: [0, 50, 100], trackTicks: [10, 20, 30, 40, 50, 60, 70, 80, 90]})],
                ["sub-text-vertical-position", qsTr("Vertical position")] ] },
            { title: qsTr("Timing"), rows: [ ["redesign-subtitle-delay", qsTr("Subtitle delay"), subtitleSlider({from: -5, to: 5, factor: 0.001, step: 0.001, decimals: 3, suffix: " s", ticks: [-5, 0, 5], positiveTickSign: true, trackTicks: [-4, -3, -2, -1, 0, 1, 2, 3, 4]})] ] },
            { title: qsTr("On-Screen Messages"), rows: [ ["osd", qsTr("Show feedback for shortcuts")],
                ["osd-duration", qsTr("Show feedback for"), subtitleSlider({from: 0.5, to: 5, factor: 0.001, step: 0.1, decimals: 1, suffix: " s", ticks: [0.5, 5], trackTicks: [1, 1.5, 2, 2.5, 3, 3.5, 4, 4.5], enabledWhen: "osd"})],
                ["video-title-show", qsTr("Show filename when a video starts")],
                ["video-title-timeout", qsTr("Show filename for"), subtitleSlider({from: 1, to: 10, factor: 0.001, step: 0.1, suffix: " s", ticks: [1, 10], trackTicks: [2, 3, 4, 5, 6, 7, 8, 9]})] ] } ],
        library: [
            { title: qsTr("Opening Files"), rows: [ ["one-instance", qsTr("Only allow one Hakone window")],
                ["one-instance-when-started-from-file", qsTr("Apply to files opened from the file manager"), {enabledWhen: "one-instance", hideDisabledValue: true, disabledOpacity: 0.3}],
                ["playlist-enqueue", qsTr("Add to the playlist instead of replacing it"), {enabledWhen: "one-instance", hideDisabledValue: true, disabledOpacity: 0.3}],
                ["recursive", qsTr("When opening a folder"), {dynamicChoices: true}], ["show-hiddenfiles", qsTr("Show hidden files")] ] },
            { title: qsTr("Metadata & Artwork"), rows: [ ["metadata-network-access", qsTr("Fetch titles and artwork"), {choices: [{label: qsTr("From the file only"), value: false}, {label: qsTr("From the file and the internet"), value: true}]}], ["media-library-startup", qsTr("Use the media library"), {dynamicChoices: true}] ] } ],
        network: [
            { title: qsTr("Buffering"), rows: [ ["redesign-buffering", qsTr("Buffering")],
                ["file-caching", qsTr("Local files"), {enabledWhenEquals: {name: "redesign-buffering", value: 0}, hideDisabledValue: true, disabledOpacity: 0.3, hideHelp: true, suffix: " ms"}],
                ["disc-caching", qsTr("Discs"), {enabledWhenEquals: {name: "redesign-buffering", value: 0}, hideDisabledValue: true, disabledOpacity: 0.3, hideHelp: true, suffix: " ms"}],
                ["network-caching", qsTr("Network streams"), {enabledWhenEquals: {name: "redesign-buffering", value: 0}, hideDisabledValue: true, disabledOpacity: 0.3, hideHelp: true, suffix: " ms"}],
                ["live-caching", qsTr("Live capture"), {enabledWhenEquals: {name: "redesign-buffering", value: 0}, hideDisabledValue: true, disabledOpacity: 0.3, hideHelp: true, suffix: " ms"}] ] },
            { title: qsTr("Proxy"), rows: [ ["http-proxy", qsTr("HTTP proxy"), {placeholder: qsTr("System proxy")}], ["http-proxy-pwd", qsTr("HTTP proxy password")] ] },
            { title: qsTr("Security"), rows: [ ["gnutls-system-trust", qsTr("Verify certificates against the system trust store")], ["gnutls-priorities", qsTr("Cipher preference"), {choiceLabels: {NORMAL: qsTr("Balanced")}}] ] } ],
        performance: [
            { title: qsTr("Decoding"), rows: [ ["redesign-hardware-decoding", qsTr("Hardware-accelerated decoding"), {dynamicChoices: true}], ["redesign-postprocessing", qsTr("Video post-processing"), {dynamicChoices: true}],
                ["avcodec-skiploopfilter", qsTr("Skip the H.264 loop filter"), {choiceLabels: {0: qsTr("Never")}}], ["avcodec-threads", qsTr("Decoding threads")] ] },
            { title: qsTr("Smooth Playback"), rows: [ ["drop-late-frames", qsTr("Skip late video frames"), {hideHelp: true}], ["high-priority", qsTr("Give Hakone higher priority")],
                ["redesign-rescan-plugins", qsTr("Rescan plugins"), {fullWidthAction: true, rowHeight: 42, hideHelp: true}] ] } ],
        recording: [
            { title: qsTranslate("Settings", "Recording"), rows: [ ["input-record-path", qsTr("Save recordings to")], ["input-record-native", qsTr("Record without re-encoding when possible"), {hideHelp: true}] ] },
            { title: qsTr("Casting"), rows: [ ["qt-discover-cast-devices", qsTr("Discover cast devices"), {hideHelp: true}],
                ["sout-chromecast-conversion-quality", qsTr("Cast quality"), {choiceLabels: {
                    "-1": qsTr("Automatic"), 0: qsTr("High"), 1: qsTr("Medium"), 2: qsTr("Low"), 3: qsTr("Low CPU use")},
                    help: qsTr("Automatic uses the default quality for this device. High can use more network data. Low CPU use can make conversion faster.")}] ] },
            { title: qsTr("Streaming"), rows: [ ["sout-display", qsTr("Play locally while streaming"), {hideHelp: true}], ["sout-audio", qsTr("Include audio"), {hideHelp: true}],
                ["sout-video", qsTr("Include video"), {hideHelp: true}], ["sout-spu", qsTr("Include subtitles"), {hideHelp: true}],
                ["sout-x264-preset", qsTr("Encoding preset"), {emptyChoiceValue: "medium", choiceLabels: {
                    ultrafast: qsTr("Ultra fast"), superfast: qsTr("Super fast"), veryfast: qsTr("Very fast"),
                    faster: qsTr("Faster"), fast: qsTr("Fast"), medium: qsTr("Medium"), slow: qsTr("Slow"),
                    slower: qsTr("Slower"), veryslow: qsTr("Very slow"), placebo: qsTr("Placebo")}}],
                ["sout-x264-tune", qsTr("Tuning")] ] } ],
        extensions: []
    })
    readonly property var advancedSections: ({
        extensions: [{title: qsTr("Add-on registry"), rows: [["qt-addon-registry", qsTr("Use the Hakone add-on registry"),
                {help: qsTr("Browse add-ons and find their updates in the registry. Add-ons always install from their own Git repositories.")}],
                ["qt-addon-registry-url", qsTr("Registry address"), {rowHeight: 42, fieldOutset: 2, fieldRadius: 8, placeholder: "https://api.hakone.org/v1",
                    help: qsTr("Use http://api.localhost:3000/v1 for a development registry on this computer.")}]]},
            {title: qsTr("Legacy extensions"), rows: [["qt-autoload-extensions", qsTr("Load legacy Lua extensions on startup"),
            {help: qsTr("This setting controls legacy Lua extensions. It does not change enabled add-ons.")}]]}],
        general: [ {title: "", rows: [ ["qt-minor-warnings", qsTr("Show minor warnings")], ["startup-options", qsTr("Additional command line options"), {rowHeight: 46}], ["logmode", qsTr("Log format"), {choiceLabels: {text: qsTr("Plain text"), html: "HTML"}}], ["stats", qsTr("Collect playback statistics")], ["redesign-config-folder", qsTr("Open configurations folder"), {rowHeight: 42}], ["redesign-reset-all", qsTr("Reset all settings"), {rowHeight: 42}] ] },
            {title: qsTr("Developer Options"), rows: [ ["syslog", qsTr("Write to the system log")], ["syslog-debug", qsTr("Include debug messages in the system log")],
                ["syslog-ident", qsTr("System log identity"), {rowHeight: 42}], ["syslog-facility", qsTr("System log facility"), {choiceLabels: {user: qsTr("User")}}] ] } ],
        interface: [ {title: "", rows: [ ["qt-opacity", qsTr("Window opacity"), interfaceSlider({from: 10, to: 100, factor: 100, suffix: "%", ticks: [10, 100], trackTicks: [20, 30, 40, 50, 60, 70, 80, 90]})],
            ["qt-pause-minimized", qsTr("Pause video when window is minimized"), {hideHelp: true}], ["qt-auto-raise", qsTr("Bring Hakone to the front when playback starts")],
            ["qt-bgcone", qsTr("Show cover art in the window when playing audio"), {hideHelp: true}], ["qt-embedded-open", qsTr("Show the file browser inside the open dialog"), {hideHelp: true}],
            ["qt-fs-opacity", qsTr("Fullscreen controls opacity"), interfaceSlider({rowHeight: 52, from: 10, to: 100, factor: 100, suffix: "%", ticks: [10, 100], trackTicks: [20, 30, 40, 50, 60, 70, 80, 90]})],
            ["redesign-fullscreen-sensitivity", qsTr("Fullscreen controls sensitivity"), interfaceSlider({rowHeight: 52, from: 0, to: 6, step: 1, ticks: [0, 3, 6], tickLabels: [qsTr("Low"), qsTr("Medium"), qsTr("High")], trackTicks: [3]})] ] } ],
        playback: [ {title: "", rows: [ ["input-timeshift-path", qsTr("Time-shift buffer folder"), {rowHeight: 42, fieldOutset: 2, fieldRadius: 8, placeholder: qsTr("Default")}], ["input-timeshift-granularity", qsTr("Time-shift buffer size"), {dynamicChoices: true}] ] } ],
        audio: [ {title: "", rows: [ ["redesign-audio-passthrough", qsTr("Pass compressed audio to the receiver"), {dynamicChoices: true}], ["force-dolby-surround", qsTr("Detect Dolby Surround"), {choiceLabels: {0: qsTr("Automatic")}}], ["redesign-resampling-quality", qsTr("Resampling quality"), {dynamicChoices: true}], ["role", qsTr("Media role")],
            ["visualization-size", qsTr("Visualization size"), {dynamicChoices: true}],
            ["aout", qsTr("Audio output driver")], ["stereo-mode", qsTr("Stereo channel mapping"), {choiceLabels: {0: qsTr("Automatic")}}], ["audio-resampler", qsTr("Resampler")], ["filter:audio-filter:spatializer", qsTr("Spatializer"), {spatializer: true}] ] } ],
        video: [ {title: "", rows: [ ["custom-aspect-ratios", qsTr("Custom aspect ratios"), {rowHeight: 42, fieldOutset: 2, fieldRadius: 8}], ["custom-crop-ratios", qsTr("Custom crop ratios"), {rowHeight: 42, fieldOutset: 2, fieldRadius: 8}],
            ["monitor-par", qsTr("Monitor pixel aspect ratio"), {rowHeight: 42, fieldOutset: 2, fieldRadius: 8, placeholder: qsTr("Automatic")}], ["input-timeshift-granularity", qsTr("Time-shift buffer size"), {dynamicChoices: true}] ] } ],
        subtitles: [ {title: "", rows: [ ["sub-autodetect-path", qsTr("Load subtitles from these folders")],
            ["sub-text-use-embedded-style", qsTr("Ignore embedded subtitle styles"), {inverted: true, help: qsTr("Use your subtitle appearance settings instead of the styles from the subtitle file.")}],
            ["freetype-shadow-angle", qsTr("Shadow angle")], ["freetype-text-direction", qsTr("Text direction"), {choices: [
                {label: qsTr("Automatic"), value: 2}, {label: qsTr("Left to right"), value: 0},
                {label: qsTr("Right to left"), value: 1}]}],
            ["freetype-monofont", qsTr("Monospace font"), {placeholder: qsTr("System monospace")}], ["visualization-size", qsTr("Visualization size"), {dynamicChoices: true}] ] } ],
        performance: [ {title: qsTr("Developer"), rows: [
            ["sout-transcode-venc", qsTr("Video encoder"), {dynamicChoices: true, emptyChoiceValue: "none"}],
            ["sout-transcode-aenc", qsTr("Audio encoder"), {dynamicChoices: true, emptyChoiceValue: "none"}],
            ["sout-transcode-senc", qsTr("Subtitle encoder"), {dynamicChoices: true, emptyChoiceValue: "none"}],
            ["codec", qsTr("Decoder"), {dynamicChoices: true, emptyChoiceValue: "any"}],
            ["clock-synchro", qsTr("Clock synchronisation")], ["clock-jitter", qsTr("Clock jitter"), {suffix: " ms"}],
            ["cr-average", qsTr("Clock reference averaging")] ] } ],
        library: [ {title: "", rows: [ ["ignore-filetypes", qsTr("Ignore file types")], ["auto-preparse", qsTr("Read file metadata automatically"), {dynamicChoices: true}] ] } ],
        network: [ {title: "", rows: [ ["http-reconnect", qsTr("Reconnect automatically")], ["http-user-agent", qsTr("User agent")], ["ipv4-timeout", qsTr("Connection timeout (ms)")] ] } ]
    })
    function find(query) {
        const found = [], names = {}, needle = query.toLocaleLowerCase()
        for (const collection of [sections, advancedSections]) {
            for (const category in collection) {
                for (const section of collection[category]) {
                    for (const row of section.rows) {
                        if (!names[row[0]] && (row[1].toLocaleLowerCase().includes(needle) || row[0].includes(needle))) {
                            names[row[0]] = true
                            found.push({name: row[0], title: row[1], presentation: row[2] || {}})
                        }
                    }
                }
            }
        }
        return found
    }
    // Several design controls combine native options or use different units.
    // Search uses the same controls as the settings pages. The expert list,
    // and an exact search for a hyphenated native name, retain the native option.
    readonly property var searchAliases: ({
        "log-level": "redesign-log-details",
        "log-verbose": "redesign-log-details",
        "subsdec-encoding": "redesign-subtitle-encoding",
        "subsdec-autodetect-utf8": "redesign-subtitle-encoding",
        "media-library": "media-library-startup",
        "qt-buffering-custom": "redesign-buffering",
        "qt-fs-sensitivity": "redesign-fullscreen-sensitivity",
        "freetype-background-mode": "redesign-subtitle-background",
        "freetype-background-opacity": "redesign-subtitle-background-opacity",
        "sub-text-scale": "redesign-subtitle-font-size",
        "sub-text-size-pixels": "redesign-subtitle-font-size",
        "zoom": "redesign-video-zoom",
        "fit": "redesign-video-zoom",
        "snapshot-width": "redesign-snapshot-size",
        "snapshot-height": "redesign-snapshot-size",
        "equalizer-preset": "redesign-equalizer-preset",
        "audio-device-default": "redesign-audio-device",
        "audio-startup-volume": "volume-save",
        "menu-language": "redesign-menu-language",
        "repeat": "redesign-repeat",
        "loop": "redesign-repeat",
        "play-and-exit": "redesign-after-playback",
        "play-and-pause": "redesign-after-playback"
    })
    function searchResults(query, nativeMatches) {
        const curated = {}
        for (const row of find("")) curated[row.name] = row
        let matches = []
        const names = new Set()
        function append(row) {
            if (names.has(row.name)) return
            names.add(row.name)
            const presentation = row.presentation || {}
            const dependency = presentation.enabledWhen || presentation.enabledWhenEquals?.name
            if (dependency && curated[dependency]) append(curated[dependency])
            matches.push(row)
        }
        for (const row of find(query)) append(row)
        const needle = query.toLocaleLowerCase()
        for (const entry of nativeMatches) {
            const alias = searchAliases[entry.name]
            if (alias && entry.name.includes("-") && entry.name.toLocaleLowerCase() === needle) {
                matches = matches.filter(row => row.name !== alias)
                names.delete(alias)
                if (!names.has(entry.name)) {
                    matches.push(entry)
                    names.add(entry.name)
                }
                continue
            }
            const row = curated[alias || entry.name] || entry
            append(row)
        }
        return matches
    }
}

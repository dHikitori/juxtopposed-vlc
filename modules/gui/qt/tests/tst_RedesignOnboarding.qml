// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "JuxtopposedOnboarding"
    width: 1090; height: 759
    visible: true; when: windowShown
    QtObject {
        id: setup
        property string defaultFolder: "~/Videos"
        property bool hasMediaLibrary: true
        property string errorMessage: ""
        property bool saveFails: false
        property int skipCount: 0
        property var saved: ({})
        readonly property var defaultShortcuts: [
            {title: "Play / Pause", keys: ["Space"]},
            {title: "Seek Backward / Forward", keys: ["Alt+Left", "Alt+Right"]},
            {title: "Previous / Next Item", keys: ["P", "N"]},
            {title: "Previous / Next Frame", keys: ["Shift+E", "E"]},
            {title: "Cycle Subtitle Track", keys: ["V"]},
            {title: "Cycle Audio Track", keys: ["B"]},
            {title: "Toggle Fullscreen", keys: ["F"]},
            {title: "Increase / Decrease Volume", keys: ["Ctrl+Up", "Ctrl+Down"]},
            {title: "Show / Hide Queue", keys: ["Ctrl+L"]},
            {title: "Show / Hide File Browser", keys: ["Ctrl+Shift+L"]},
            {title: "Take a Snapshot", keys: ["Shift+S"]},
            {title: "Record", keys: ["Shift+R"]}
        ]
        property var shortcuts: defaultShortcuts
        function validateFolder(folder) { errorMessage = folder === "missing" ? "Select an existing folder." : ""; return !errorMessage.length }
        function browseFolder(folder) { return "/media/Movies" }
        function finish(folder, scheme, accent, network) {
            if (saveFails) { errorMessage = "Hakone cannot save these settings."; return false }
            saved = {folder: folder, scheme: scheme, accent: accent, network: network}; return true
        }
        function skip() { skipCount++; return !saveFails }
    }
    RedesignOnboardingView { id: onboarding; anchors.fill: parent; backend: setup }
    SignalSpy { id: finished; target: onboarding; signalName: "finished" }
    SignalSpy { id: dismissed; target: onboarding; signalName: "dismissRequested" }
    function item(name) { return findChild(onboarding, name) }
    function init() {
        onboarding.page = 0; onboarding.folder = "~/Videos"; onboarding.scheme = 2
        onboarding.accentColor = "#d37918"; onboarding.onlineMetadata = true
        setup.hasMediaLibrary = true; setup.errorMessage = ""; setup.saveFails = false; setup.saved = ({})
        setup.skipCount = 0
        setup.shortcuts = setup.defaultShortcuts
        finished.clear(); dismissed.clear(); onboarding.width = 1090; onboarding.height = 759
    }
    function test_navigationUsesThreePagesAndRetainsEdits() {
        onboarding.folder = "/media/Music"; onboarding.next(); compare(onboarding.page, 1)
        mouseClick(item("onboardingScheme_1")); mouseClick(item("onboardingAccent_5"))
        onboarding.next(); compare(onboarding.page, 2)
        mouseClick(item("onboardingBack")); compare(onboarding.page, 1)
        compare(onboarding.scheme, 1); compare(onboarding.accentColor, "#185dd3")
        mouseClick(item("onboardingBack")); compare(onboarding.page, 0); compare(onboarding.folder, "/media/Music")
        compare(finished.count, 0)
    }
    function test_welcomeAndAppearanceMatchReferencePositions() {
        const cone = item("onboardingWelcomeCone"), folder = item("onboardingFolder")
        compare(cone.x, 475); compare(cone.y, 116); compare(cone.width, 140); compare(cone.height, 140)
        compare(folder.x, 248); compare(folder.y, 534); compare(folder.width, 469); compare(folder.height, 34)
        compare(item("onboardingNext").y, 671)
        onboarding.page = 1
        compare(item("onboardingAccent_0").mapToItem(onboarding, 0, 0).y, 655)
        compare(item("onboardingBack").y, 671)
    }
    function test_shortcutKeycapsMatchReferenceAndKeepLiveCombinations() {
        onboarding.page = 2
        const space = item("onboardingKeycap_0_0_0"), fullscreen = item("onboardingKeycap_6_0_0")
        compare(space.mapToItem(onboarding, 0, 0), Qt.point(302, 154))
        compare(space.width, 93); compare(space.height, 42)
        compare(fullscreen.mapToItem(onboarding, 0, 0), Qt.point(845, 154))
        compare(fullscreen.width, 56)
        const shadow = item("onboardingKeycap_0_0_0_shadow")
        compare(shadow.y, 7); compare(shadow.height, 42)
        const shift = item("onboardingKeycap_10_0_0")
        compare(shift.width, 92)
        // Configured multi-key alternatives still fit the two-column page.
        const volumeEnd = item("onboardingKeycap_7_1_1")
        verify(volumeEnd.mapToItem(onboarding, volumeEnd.width, 0).x <= 1080)
        compare(volumeEnd.mapToItem(onboarding, 0, 0).y, item("onboardingShortcutRow_7").y)
        const browserEnd = item("onboardingKeycap_9_0_2")
        verify(browserEnd.mapToItem(onboarding, browserEnd.width, 0).x <= 1080)
    }
    function test_configuredPlusKeyDoesNotCreateEmptyKeycaps() {
        setup.shortcuts = [{title: "Custom action", keys: ["Ctrl++", "+"]}]
        onboarding.page = 2
        tryVerify(() => item("onboardingKeycap_0_1_0") !== null)
        compare(item("onboardingKeycap_0_0_1").modelData, "+")
        compare(item("onboardingKeycap_0_1_0").modelData, "+")
        compare(item("onboardingKeycap_0_0_2"), null)
        compare(item("onboardingKeycap_0_1_1"), null)
    }
    function test_longConfiguredShortcutAlternativesStayOnOneRow() {
        setup.shortcuts = [{title: "Custom action", keys: ["Ctrl+Alt+Shift+Left", "Ctrl+Alt+Shift+Right"]}]
        onboarding.page = 2
        tryVerify(() => item("onboardingKeycap_0_1_3") !== null)
        const last = item("onboardingKeycap_0_1_3"), end = last.mapToItem(onboarding, last.width, last.height)
        verify(end.x <= 538); verify(end.y <= 196)
        compare(last.mapToItem(onboarding, 0, 0).y, 154)
    }
    function test_browseChangesOnlyTheFolder() {
        mouseClick(item("onboardingBrowse")); compare(onboarding.folder, "/media/Movies")
        compare(onboarding.page, 0); compare(finished.count, 0)
    }
    function test_missingFolderKeepsWelcomeVisible() {
        onboarding.folder = "missing"; onboarding.next(); compare(onboarding.page, 0)
        verify(item("onboardingError").visible)
    }
    function test_eachAccentAndThemeUpdatesTheRealPlayerPreview() {
        onboarding.page = 1
        for (let i = 0; i < 9; ++i) {
            mouseClick(item("onboardingAccent_" + i))
            compare(item("onboardingPlayerPreview").accentColor, onboarding.accents[i])
        }
        mouseClick(item("onboardingScheme_1")); compare(item("onboardingPlayerPreview").lightTheme, true)
        mouseClick(item("onboardingScheme_2")); compare(item("onboardingPlayerPreview").lightTheme, false)
        mouseClick(item("onboardingScheme_0")); onboarding.systemLight = true
        compare(item("onboardingPlayerPreview").lightTheme, true)
    }
    function test_finishIncludesMetadataChoiceAndAllSelections() {
        mouseClick(item("onboardingMetadata")); compare(onboarding.onlineMetadata, false)
        onboarding.next(); onboarding.next(); onboarding.next()
        compare(finished.count, 1); compare(setup.saved.folder, "~/Videos")
        compare(setup.saved.scheme, 2); compare(setup.saved.accent, "#d37918"); compare(setup.saved.network, false)
    }
    function test_saveFailureRetainsFinalPageAndAllowsRetry() {
        onboarding.page = 2; setup.saveFails = true; onboarding.next()
        compare(onboarding.page, 2); compare(finished.count, 0); verify(item("onboardingError").visible)
        setup.saveFails = false; onboarding.next(); compare(finished.count, 1)
    }
    function test_noMediaLibraryDisablesFolderControls() {
        setup.hasMediaLibrary = false; compare(item("onboardingFolder").enabled, false)
        compare(item("onboardingBrowse").enabled, false)
    }
    function test_closeAndEscapeStillDismissAfterSaveFailure() {
        setup.saveFails = true; onboarding.page = 2; onboarding.next()
        compare(finished.count, 0); verify(item("onboardingError").visible)
        mouseClick(item("onboardingClose"))
        compare(setup.skipCount, 1); compare(dismissed.count, 1)
        onboarding.forceActiveFocus(); keyClick(Qt.Key_Escape)
        compare(setup.skipCount, 2); compare(dismissed.count, 2)
        onboarding.page = 1
        verify(!item("onboardingClose").visible)
        mouseClick(findChild(item("onboardingPlayerPreview"), "windowCloseButton"))
        compare(setup.skipCount, 3); compare(dismissed.count, 3)
        compare(finished.count, 0)
    }
    function test_privacyExplanationCanOpenAndClose() {
        mouseClick(item("onboardingPrivacyInfo")); verify(item("onboardingPrivacyPolicy").opened)
        keyClick(Qt.Key_Escape)
        tryCompare(item("onboardingPrivacyPolicy"), "visible", false)
        compare(dismissed.count, 0)
    }
}

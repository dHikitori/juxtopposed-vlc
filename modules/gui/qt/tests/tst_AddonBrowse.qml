// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "AddonBrowse"
    width: 700; height: 900; visible: true; when: windowShown

    // A stand-in for AddonRegistry: it records what QML asks for.
    QtObject {
        id: fakeRegistry
        property bool enabled: true
        property string address: "https://api.hakone.org/v1"
        property string addressError: ""
        property bool developmentAddress: false
        property bool defaultAddress: true
        property bool searching: false
        property string searchError: ""
        property var results: []
        property bool hasMore: false
        property var current: ({})
        property bool currentLoading: false
        property string currentError: ""
        property int indexRevision: 0
        property var searches: []
        property var opened: []
        property var installs: []
        property var updates: []
        property int refreshes: 0
        signal settingsChanged()
        function refresh(force) { refreshes++ }
        function search(text, type) { searches.push({text: text, type: type}); searches = searches }
        function loadMore() {}
        function open(id) { opened.push(id); opened = opened; current = item(id, true) }
        function closeCurrent() { current = ({}) }
        function install(id, version) { installs.push(id + "@" + version); installs = installs; return true }
        function installUpdate(id, version, source) { updates.push(id + "@" + version); updates = updates; return true }
        function updateFor(id, version, source) { return id === "org.example.radio" && version === "1.0.0" ? {version: "1.1.0", tag: "v1.1.0"} : ({}) }
        function withdrawn(id, version) { return false }
        function listed(id, source) { return true }
        function item(id, detail) {
            const value = {id: id, name: id === "org.example.radio" ? "Radio" : "Midnight", publisher: "Example", summary: "Summary",
                latestVersion: "1.1.0", compatible: true, webUrl: "https://addons.hakone.org/addons/" + id}
            if (detail) value.latestRelease = {version: "1.1.0", compatible: true, permissions: {"network.http": {origins: ["https://api.example.org"]}},
                contributions: [{id: "c", type: "theme", name: "Midnight"}]}
            return value
        }
    }

    Component {
        id: browseComponent
        RedesignAddonBrowse {
            width: 600
            registry: fakeRegistry
            installedPackages: [{id: "org.example.radio", version: "1.0.0", source: "https://git.example.org/radio.git"}]
        }
    }

    function init() {
        fakeRegistry.enabled = true; fakeRegistry.addressError = ""; fakeRegistry.defaultAddress = true
        fakeRegistry.searches = []; fakeRegistry.opened = []; fakeRegistry.installs = []; fakeRegistry.updates = []; fakeRegistry.refreshes = 0
        fakeRegistry.results = [fakeRegistry.item("org.example.midnight"), fakeRegistry.item("org.example.radio")]
        fakeRegistry.current = ({})
    }

    function test_startsWithASearchAndShowsInstalledState() {
        const browse = createTemporaryObject(browseComponent, testCase)
        verify(browse)
        compare(fakeRegistry.refreshes, 1)
        compare(fakeRegistry.searches.length, 1)
        compare(fakeRegistry.searches[0].type, "")
        tryVerify(() => findChild(browse, "addonBrowseResultState_org.example.radio") !== null)
        compare(findChild(browse, "addonBrowseResultState_org.example.radio").text, "Update to 1.1.0")
        compare(findChild(browse, "addonBrowseResultState_org.example.midnight").visible, false)
    }

    function test_typeFiltersSearch() {
        const browse = createTemporaryObject(browseComponent, testCase)
        const themes = findChild(browse, "addonBrowseType_theme")
        verify(themes)
        mouseClick(themes)
        compare(fakeRegistry.searches[fakeRegistry.searches.length - 1].type, "theme")
    }

    function test_installPassesOnlyTheIdAndVersion() {
        const browse = createTemporaryObject(browseComponent, testCase)
        tryVerify(() => findChild(browse, "addonBrowseResult_org.example.midnight") !== null)
        mouseClick(findChild(browse, "addonBrowseResult_org.example.midnight"))
        compare(fakeRegistry.opened[0], "org.example.midnight")
        const install = findChild(browse, "addonBrowseInstall")
        tryVerify(() => install.visible && install.enabled)
        compare(install.caption, "Install")
        verify(findChild(browse, "addonBrowsePermissions").text.indexOf("api.example.org") >= 0)
        waitForItemPolished(browse)
        mouseClick(install)
        compare(fakeRegistry.installs, ["org.example.midnight@1.1.0"])
        compare(fakeRegistry.updates.length, 0)
    }

    function test_installedAddonOffersItsUpdate() {
        const browse = createTemporaryObject(browseComponent, testCase)
        tryVerify(() => findChild(browse, "addonBrowseResult_org.example.radio") !== null)
        mouseClick(findChild(browse, "addonBrowseResult_org.example.radio"))
        const install = findChild(browse, "addonBrowseInstall")
        tryCompare(install, "caption", "Update to 1.1.0")
        // The details appear first and are laid out on the next polish.
        waitForItemPolished(browse)
        mouseClick(install)
        compare(fakeRegistry.updates, ["org.example.radio@1.0.0"])
        compare(fakeRegistry.installs.length, 0)
    }

    function test_reviewInProgressBlocksInstall() {
        const browse = createTemporaryObject(browseComponent, testCase, {pendingReview: true})
        tryVerify(() => findChild(browse, "addonBrowseResult_org.example.midnight") !== null)
        mouseClick(findChild(browse, "addonBrowseResult_org.example.midnight"))
        const install = findChild(browse, "addonBrowseInstall")
        tryVerify(() => install.visible)
        verify(!install.enabled)
    }

    function test_turnedOffRegistryAsksNothing() {
        fakeRegistry.enabled = false
        const browse = createTemporaryObject(browseComponent, testCase)
        compare(fakeRegistry.searches.length, 0)
        verify(findChild(browse, "addonBrowseState").text.indexOf("turned off") >= 0)
        verify(!findChild(browse, "addonBrowseList").visible)
    }

    function test_badAddressIsShown() {
        fakeRegistry.addressError = "The registry address must use HTTPS."
        const browse = createTemporaryObject(browseComponent, testCase)
        compare(fakeRegistry.searches.length, 0)
        compare(findChild(browse, "addonBrowseState").text, "The registry address must use HTTPS.")
    }

    function test_otherRegistryAddressIsShown() {
        fakeRegistry.defaultAddress = false; fakeRegistry.address = "http://api.localhost:3000/v1"
        const browse = createTemporaryObject(browseComponent, testCase)
        verify(findChild(browse, "addonBrowseAddress").visible)
        verify(findChild(browse, "addonBrowseAddress").text.indexOf("api.localhost") >= 0)
        fakeRegistry.address = "https://api.hakone.org/v1"
    }
}

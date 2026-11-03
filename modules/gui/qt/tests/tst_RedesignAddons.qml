// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    name: "JuxtopposedAddons"
    width: 900; height: 1000; visible: true; when: windowShown
    ListModel {
        id: store
        property int revision: 0
        property bool loading: false
        property string errorMessage: ""
        property string searchPattern: ""
        property int discoveryCalls: 0
        property int installCalls: 0
        property int removeCalls: 0
        property string lastId: ""
        property bool acceptAction: true
        property var records: []
        function rebuild() {
            clear()
            for (const row of records)
                if (row.name.toLowerCase().includes(searchPattern.toLowerCase())) append(row)
            revision++
        }
        function addonById(id) { return records.find(row => row.identifier === id) || ({}) }
        function loadFromDefaultRepository() { discoveryCalls++; loading = true }
        function installAddon(id) { installCalls++; lastId = id; return begin(id, 1) }
        function removeAddon(id) { removeCalls++; lastId = id; return begin(id, 3) }
        function begin(id, state) {
            if (!acceptAction) return false
            records = records.map(row => row.identifier === id ? Object.assign({}, row, {pending: true, state: state}) : row)
            rebuild(); return true
        }
        function complete(state) {
            records = records.map(row => row.identifier === lastId ? Object.assign({}, row, {pending: false, state: state}) : row)
            rebuild()
        }
        onSearchPatternChanged: rebuild()
    }
    RedesignAddons { id: page; width: 784; backend: store }
    SignalSpy { id: websiteSpy; target: page; signalName: "websiteRequested" }
    function item(name) { return findChild(page, name) }
    function init() {
        page.width = 784; page.lightTheme = false; store.errorMessage = ""; websiteSpy.clear()
        store.loading = false; store.searchPattern = ""
        store.discoveryCalls = 0; store.installCalls = 0; store.removeCalls = 0; store.lastId = ""; store.acceptAction = true
        store.records = [
            {identifier: "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", name: "Available", summary: "An available add-on", description: "Plain text", state: 0, manageable: true, broken: false, pending: false},
            {identifier: "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", name: "Installed", summary: "An installed add-on", description: "Plain text", state: 2, manageable: true, broken: false, pending: false},
            {identifier: "cccccccccccccccccccccccccccccccc", name: "Manual", summary: "A manual install", description: "Plain text", state: 2, manageable: false, broken: false, pending: false}]
        store.rebuild()
        page.selectedId = ""; page.actionMessage = ""; page.discoveryRequested = false
        item("addonsSearch").clear(); wait(30)
    }
    function test_initialPageDoesNotFetchOrChangeAddons() {
        compare(store.discoveryCalls, 0); compare(store.installCalls, 0); compare(store.removeCalls, 0)
        verify(!item("addonsAdd").enabled); verify(!item("addonsRemove").enabled)
        mouseClick(item("addonsFindMore"))
        compare(store.discoveryCalls, 1); verify(!item("addonsFindMore").enabled)
        mouseClick(item("addonsFindMore")); compare(store.discoveryCalls, 1)
    }
    function test_actionsKeepIdentityAcrossReorderAndAsyncReset() {
        mouseClick(item("addonRow_0"))
        verify(item("addonsAdd").enabled)
        store.records = [store.records[1], store.records[2], store.records[0]]; store.rebuild(); wait(20)
        mouseClick(item("addonsAdd")); compare(store.lastId, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")
        verify(!item("addonsAdd").enabled); verify(!item("addonsRemove").enabled)
        page.addSelected(); compare(store.installCalls, 1)
        store.complete(2); wait(20); verify(item("addonsRemove").enabled)
        mouseClick(item("addonsRemove")); compare(store.lastId, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")
        page.removeSelected(); compare(store.removeCalls, 1)
        store.complete(0); wait(20); verify(item("addonsAdd").enabled)
    }
    function test_manualAndBrokenAddonsCannotBeInstalled() {
        mouseClick(item("addonRow_2")); verify(!item("addonsRemove").enabled)
        store.records[0].broken = true; store.rebuild(); wait(20)
        mouseClick(item("addonRow_0")); verify(!item("addonsAdd").enabled)
        compare(store.installCalls, 0); compare(store.removeCalls, 0)
    }
    function test_searchClearsSelectionBeforeFiltering() {
        mouseClick(item("addonRow_1")); verify(item("addonsRemove").enabled)
        mouseClick(item("addonsSearch"))
        for (const key of [Qt.Key_A, Qt.Key_V, Qt.Key_A, Qt.Key_I, Qt.Key_L, Qt.Key_A, Qt.Key_B, Qt.Key_L, Qt.Key_E]) keyClick(key)
        compare(store.searchPattern, "available"); compare(page.selectedId, "")
        verify(!item("addonsRemove").enabled); compare(store.count, 1)
    }
    function test_rejectedActionKeepsItsSelectionAndShowsAnError() {
        store.acceptAction = false
        mouseClick(item("addonRow_0")); mouseClick(item("addonsAdd"))
        compare(page.selectedId, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")
        verify(page.actionMessage.length > 0); verify(item("addonsAdd").enabled)
    }
    function test_keyboardSelectsWithoutInstalling() {
        const list = item("addonsList"); list.forceActiveFocus()
        keyClick(Qt.Key_End)
        compare(page.selectedId, "cccccccccccccccccccccccccccccccc")
        keyClick(Qt.Key_Home)
        compare(page.selectedId, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")
        compare(store.installCalls, 0); compare(store.removeCalls, 0)
    }
    function test_narrowPageKeepsItsActionsInsideTheCard() {
        page.width = 244; wait(30)
        for (const name of ["addonsSearch", "addonsFindMore", "addonsAdd", "addonsRemove"]) {
            const control = item(name), point = control.mapToItem(page, 0, 0)
            verify(point.x >= 0, name); verify(point.x + control.width <= page.width + 1, name)
        }
        verify(item("addonsFindMore").y > item("addonsSearch").y)
        verify(item("addonsAdd").y > item("addonsFindMore").y)
    }
    function test_asyncFailureNamesItsAddonAndUsesPlainText() {
        store.errorMessage = "The app cannot add <Fixture>."
        const message = item("addonsError")
        compare(message.text, store.errorMessage)
        compare(message.textFormat, Text.PlainText)
        verify(message.visible)
    }
    function test_failedDiscoveryDoesNotLookLikeAnEmptyResult() {
        store.records = []; store.rebuild(); page.discoveryRequested = true; wait(20)
        verify(item("addonsEmptyState").visible)
        store.errorMessage = "The app cannot load the add-on list. Try again."; wait(20)
        verify(item("addonsError").visible); verify(!item("addonsEmptyState").visible)
        store.errorMessage = ""; store.loading = true; wait(20)
        verify(item("addonsEmptyState").visible)
        compare(item("addonsEmptyState").text, "Loading add-ons…")
        store.loading = false; wait(20)
        compare(item("addonsEmptyState").text, "No add-ons found.")
    }
    function test_detailsUseTheStableIdAndKeepMetadataLiteral() {
        store.records[0] = Object.assign({}, store.records[0], {
            name: "<Title>", author: "<Author>", version: "1.2", typeLabel: "Extensions",
            summary: "Summary", description: "<Description>", files: ["<one>.lua", "two.lua"], website: "https://example.test/addon"})
        store.rebuild(); wait(20)
        verify(!item("addonDetails").visible)
        mouseClick(item("addonRow_0")); wait(20)
        verify(item("addonDetails").visible)
        compare(item("addonDetailsName").text, "<Title>")
        compare(item("addonDetailsName").textFormat, Text.PlainText)
        compare(item("addonDetailsDescription").text, "Summary\n\n<Description>")
        compare(item("addonDetailsDescription").textFormat, TextEdit.PlainText)
        compare(item("addonDetailsFiles").text, "<one>.lua\ntwo.lua")
        verify(item("addonDetailsDescription").readOnly)
        store.records = [store.records[1], store.records[0], store.records[2]]; store.rebuild(); wait(20)
        compare(item("addonDetailsName").text, "<Title>")
        mouseClick(item("addonDetailsClose")); verify(!item("addonDetails").visible)
        compare(page.selectedId, ""); compare(store.installCalls, 0); compare(store.removeCalls, 0)
    }
    function test_manualDetailsExplainWhyActionsAreDisabled() {
        mouseClick(item("addonRow_2")); wait(20)
        verify(item("addonDetails").visible); verify(item("addonDetailsManual").visible)
        verify(!item("addonsAdd").enabled); verify(!item("addonsRemove").enabled)
    }
    function test_websiteNeedsAnExplicitClickAndAWebAddress() {
        page.selectedId = store.records[0].identifier
        for (const url of ["file:///tmp/test", "javascript:alert(1)", "", "https://example.test/addon", "http://example.test/addon"]) {
            store.records[0].website = url; store.rebuild(); wait(20)
            const web = url.startsWith("http")
            compare(page.canOpenWebsite, web); compare(item("addonDetailsWebsite").visible, web)
            compare(websiteSpy.count, 0)
            if (web) {
                mouseClick(item("addonDetailsWebsite")); compare(websiteSpy.count, 1)
                compare(websiteSpy.signalArguments[0][0], url); websiteSpy.clear()
            }
        }
    }
    function test_longDetailsFitNarrowPagesInBothThemes() {
        store.records[0] = Object.assign({}, store.records[0], {name: "A".repeat(100), description: "Long detail ".repeat(500), author: "B".repeat(100)})
        store.rebuild(); page.selectedId = store.records[0].identifier; page.width = 244
        for (const light of [false, true]) {
            page.lightTheme = light; wait(30)
            const card = item("addonDetails"), description = item("addonDetailsDescription")
            verify(card.width <= page.width + 1); verify(card.height < 500)
            verify(description.width < card.width); verify(description.contentHeight > item("addonDetailsScroll").height)
        }
    }
}

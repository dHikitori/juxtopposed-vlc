// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: test
    name: "AddonSourceConsent"
    width: 800; height: 800; visible: true; when: windowShown
    QtObject {
        id: service
        property var review: ({})
        property bool preparing: false
        property bool acting: false
        property string errorMessage: ""
        property string status: ""
        property int preparations: 0
        property int approvals: 0
        property int revocations: 0
        property int closes: 0
        property string selected: ""
        function prepare(source) {
            preparations++; selected = source
            review = {name: "<b>Source</b>", source: "https://git.example/<i>source</i>", accountLabel: "<b>Account</b>",
                accountFields: {region: "<i>Region</i>"}, publisherVerified: false, approved: false}
            return true
        }
        function approve() { approvals++; review = ({}); return true }
        function revoke() { revocations++; review = ({}); return true }
        function viewClosed() { closes++; review = ({}); preparing = false }
    }
    RedesignAddonSourceConsentDialog { id: dialog; backend: service }
    function item(name) { return findChild(dialog, name) }
    function openReview() {
        dialog.reviewSource("org.hakone.test/source")
        tryCompare(dialog, "opened", true)
    }
    function init() {
        dialog.close(); wait(20)
        service.review = ({}); service.preparing = false; service.acting = false
        service.errorMessage = ""; service.status = ""
        service.preparations = 0; service.approvals = 0; service.revocations = 0; service.closes = 0
    }
    function cleanup() { dialog.close(); wait(20) }
    function test_openOnlyPreparesTheCapturedSource() {
        openReview()
        compare(service.preparations, 1); compare(service.selected, "org.hakone.test/source")
        compare(service.approvals, 0); compare(service.revocations, 0)
        verify(item("sourceConsentApprove").visible); verify(!item("sourceConsentRevoke").visible)
    }
    function test_capturedValuesStayPlainText() {
        openReview()
        compare(item("sourceConsentName").text, "<b>Source</b>")
        compare(item("sourceConsentName").textFormat, Text.PlainText)
        compare(item("sourceConsentRepository").textFormat, Text.PlainText)
        compare(item("sourceConsentAccount").text, "<b>Account</b>")
        compare(item("sourceConsentAccount").textFormat, Text.PlainText)
    }
    function test_approvalRequiresItsButton() {
        openReview(); mouseClick(item("sourceConsentApprove"))
        compare(service.approvals, 1); compare(service.revocations, 0)
    }
    function test_existingApprovalHasASeparateRemovalAction() {
        openReview(); service.review = Object.assign({}, service.review, {approved: true})
        verify(!item("sourceConsentApprove").visible); verify(item("sourceConsentRevoke").visible)
        mouseClick(item("sourceConsentRevoke")); compare(service.revocations, 1); compare(service.approvals, 0)
    }
    function test_busyActionsCannotBeRepeated() {
        openReview(); service.acting = true
        verify(!item("sourceConsentApprove").enabled)
        compare(service.approvals, 0)
    }
    function test_closingDiscardsTheViewWithoutRemovingApproval() {
        openReview(); mouseClick(item("sourceConsentClose"))
        tryCompare(dialog, "opened", false)
        compare(service.closes, 1); compare(service.approvals, 0); compare(service.revocations, 0)
    }
    function test_statusAndErrorsArePlainText() {
        openReview(); service.errorMessage = "<b>Refused</b>"; service.status = "<i>Pending</i>"
        compare(item("sourceConsentError").textFormat, Text.PlainText)
        compare(item("sourceConsentError").text, "<b>Refused</b>")
        compare(item("sourceConsentStatus").textFormat, Text.PlainText)
    }
}

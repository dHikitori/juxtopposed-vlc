// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

// Trusted host display only. The Manager owns the exact View mapping. These
// tickets request joined display checks and grant no provider execution.
QtObject {
    id: root
    property var backend: null
    property string feature: ""
    property bool active: false
    property bool eligible: true
    property bool mounted: false
    property bool displayMode: false
    property bool bindingReady: false
    property bool reconcileQueued: false
    property bool retired: false
    property bool preparing: false
    property bool attempted: false
    property bool hadBackend: false
    property var capturedBackend: null
    property var ticketOwner: null
    property string ticket: ""
    property string ticketFeature: ""
    property int generation: 0
    property string errorMessage: ""
    readonly property bool current: {
        if (!bindingReady || retired || !active) return false
        if (!displayMode) return true
        if (!active || !eligible || preparing || !ticket.length || !ticketOwner || ticketOwner !== backend || ticketFeature !== feature) return false
        ticketOwner.catalogueReady; ticketOwner.catalogueRevision
        return typeof ticketOwner.displayRequestCurrent === "function" && ticketOwner.displayRequestCurrent(ticket)
    }
    readonly property bool loading: displayMode && !retired && !!capturedBackend && capturedBackend.catalogueLoading === true
    Component.onCompleted: { mounted = true; invalidateBackend() }
    Component.onDestruction: { mounted = false; bindingReady = false; releaseTicket() }
    onBackendChanged: invalidateBackend()
    onFeatureChanged: if (mounted) scheduleReconcile()
    onActiveChanged: if (mounted) scheduleReconcile()
    onEligibleChanged: if (mounted) scheduleReconcile()

    function invalidateBackend() {
        // Capture identity without querying the owner during its binding.
        bindingReady = false; ++generation; capturedBackend = backend
        retired = !backend && hadBackend
        if (backend) hadBackend = true
        if (mounted) scheduleReconcile()
    }
    // External mode/acquire/rebind callbacks must run after the parent binding
    // has finished. Readiness remains false until fixed mode is captured.
    function scheduleReconcile() {
        if (!mounted || reconcileQueued) return
        reconcileQueued = true
        Qt.callLater(root.reconcile)
    }
    function reconcile() {
        reconcileQueued = false
        if (!mounted) return
        captureBackend()
        if (mounted && bindingReady) synchronize()
    }

    function releaseTicket(callOwner = true) {
        const releasedGeneration = ++generation
        const owner = ticketOwner, previous = ticket
        ticket = ""; ticketOwner = null; ticketFeature = ""; preparing = false; attempted = false
        try {
            if (callOwner && owner && previous.length && typeof owner.releaseDisplayRequest === "function") owner.releaseDisplayRequest(previous)
        } catch (error) {}
        return releasedGeneration
    }
    function captureBackend() {
        if (capturedBackend === backend && bindingReady) return
        if (capturedBackend === backend && retired) return
        const next = backend, releasedGeneration = releaseTicket()
        if (releasedGeneration !== generation || next !== backend) return
        if (!next) {
            // QObject loss does not select the legacy read path.
            capturedBackend = null; retired = hadBackend
            bindingReady = !retired
            return
        }
        capturedBackend = next; hadBackend = true; retired = false; errorMessage = ""
        const mode = typeof next.displayCatalogueAttached === "function" && next.displayCatalogueAttached()
        if (releasedGeneration !== generation || next !== backend || retired) return
        displayMode = mode
        bindingReady = true
    }
    function synchronize() {
        if (!mounted || !bindingReady) return
        if (!active || !eligible || !feature.length) { releaseTicket(); return }
        if (ticket.length && ticketFeature !== feature) releaseTicket()
        if (!bindingReady || !active || !eligible || retired || capturedBackend !== backend) return
        if (!displayMode || retired || !capturedBackend || capturedBackend !== backend) return
        if (ticket.length || preparing || attempted || capturedBackend.catalogueReady !== true) return
        if (typeof capturedBackend.prepareDisplayFeature !== "function") {
            attempted = true; errorMessage = qsTr("The provider list is not available."); return
        }
        const owner = capturedBackend, selected = feature, capturedGeneration = ++generation
        preparing = true; attempted = true; errorMessage = ""; ticketOwner = owner; ticketFeature = selected
        const issued = owner.prepareDisplayFeature(selected)
        if (capturedGeneration !== generation || owner !== capturedBackend || selected !== feature || retired || !active || !eligible) {
            // A queued hide may be followed by a new show before reconciliation.
            // Clear this exact unfinished acquisition before external cleanup.
            if (capturedGeneration === generation && ticketOwner === owner && preparing) releaseTicket(false)
            try { if (issued.length && typeof owner.releaseDisplayRequest === "function") owner.releaseDisplayRequest(issued) } catch (error) {}
            return
        }
        ticket = issued; preparing = false
        if (!ticket.length) errorMessage = qsTr("The app cannot check provider settings. Close another settings panel and try again.")
    }
    function currentForAction() {
        if (!bindingReady || retired || !active) return false
        if (!displayMode) return true
        if (!retired && active && eligible && !preparing && ticketOwner && ticketOwner === backend
                && ticketOwner === capturedBackend && ticketFeature === feature && ticket.length
                && typeof ticketOwner.displayRequestCurrent === "function" && ticketOwner.displayRequestCurrent(ticket)) return true
        errorMessage = qsTr("The provider list changed. Wait for it to finish or try again.")
        return false
    }
    function managementCurrent() {
        if (!bindingReady || retired || !active) return false
        if (!displayMode) return true
        // Disabled and missing-setup providers still need Enable/Configure.
        // Those actions do not require a credential-presence demand.
        return !!capturedBackend && capturedBackend === backend && capturedBackend.catalogueReady === true
    }
    function retry() {
        if (!bindingReady || !displayMode || retired || !active || !capturedBackend || capturedBackend !== backend) return
        attempted = false; errorMessage = ""
        capturedBackend.refreshDisplayCatalogue()
    }
    property Connections notifications: Connections {
        target: root.capturedBackend
        ignoreUnknownSignals: true
        function onCatalogueChanged() { root.scheduleReconcile() }
        function onCatalogueFreshnessChanged() { root.scheduleReconcile() }
        function onRetiring() {
            // Manager/View retirement owns native cleanup. Do not re-enter the
            // retiring Manager to release a public display id.
            if (root.capturedBackend === root.backend) {
                root.retired = true; root.bindingReady = false
                root.releaseTicket(root.ticketOwner !== root.capturedBackend)
            }
        }
    }
}

// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

QtObject {
    id: root
    required property var sourceService
    property double pendingPreparation: -1
    property int pendingRequest: -1
    property bool pendingSubtitles: true
    property bool active: false
    signal prepared(var rows, bool subtitles, int request, var resolved)
    signal failed(int request, string error, var resolved)

    function requiresPreparation(rows) {
        return rows.some(row => (row.tracks || []).some(url => /^(?:opensubtitles|subtitlelink|addon-subtitle):/.test(String(url))))
    }
    function prepare(rows, subtitles, request) {
        if (active) cancel()
        pendingRequest = request; pendingSubtitles = subtitles
        if (!requiresPreparation(rows)) {
            pendingPreparation = -1
            prepared(rows, subtitles, request, {})
            return -1
        }
        if (!sourceService) { failed(request, qsTr("The subtitle service is not available."), {}); return -1 }
        active = true
        pendingPreparation = sourceService.prepareRows(rows)
        return pendingPreparation
    }
    function cancel() {
        const wasActive = active
        active = false; pendingPreparation = -1; pendingRequest = -1
        if (wasActive && sourceService) sourceService.cancelPreparation()
    }
    property Connections sourceConnections: Connections {
        target: root.sourceService
        ignoreUnknownSignals: true
        function onPreparationFinished(request, result) {
            if (!root.active || request !== root.pendingPreparation) return
            root.active = false
            const saveRequest = root.pendingRequest
            root.pendingPreparation = -1; root.pendingRequest = -1
            if (result.success) root.prepared(result.rows || [], root.pendingSubtitles, saveRequest, result.resolved || {})
            else root.failed(saveRequest, result.error || qsTr("VLC cannot prepare these subtitles."), result.resolved || {})
        }
    }
}

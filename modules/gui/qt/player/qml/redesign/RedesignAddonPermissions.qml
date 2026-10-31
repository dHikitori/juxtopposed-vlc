// SPDX-License-Identifier: GPL-2.0-or-later
// Plain-language summaries of add-on permissions, for the Extensions review
// and the startup update prompt.
import QtQml

QtObject {
    function destinations(origins) { return (origins || []).map(origin => String(origin).replace(/^https?:\/\//, "").replace(/^wss:\/\//, "")).join(", ") }
    function summaries(permissions) {
        const summaries = []
        const mediaFields = {kind: qsTr("media type"), type: qsTr("media type"), title: qsTr("title"), titles: qsTr("titles"), year: qsTr("release year"),
            season: qsTr("season number"), episodes: qsTr("episode numbers"), episode: qsTr("episode details"), ids: qsTr("catalog identifiers"), externalIds: qsTr("catalog identifiers"),
            language: qsTr("language"), languages: qsTr("languages"), duration: qsTr("duration")}
        const queueActions = {append: qsTr("add media"), enqueue: qsTr("add media"), insert: qsTr("insert media"), remove: qsTr("remove media"), move: qsTr("reorder items"),
            reorder: qsTr("reorder items"), clear: qsTr("clear the queue"), replace: qsTr("replace the queue")}
        const playbackActions = {play: qsTr("play"), pause: qsTr("pause"), stop: qsTr("stop"), seek: qsTr("seek"), rate: qsTr("change speed"), setRate: qsTr("change speed"), setPosition: qsTr("seek")}
        const hooks = {"home.sections@1": qsTr("Home sections"), "home.content@1": qsTr("Home content"), "library.navigation@1": qsTr("Library navigation"),
            "media.details.sections@1": qsTr("Media details"), "subtitles.results.sections@1": qsTr("Subtitle search results"), "subtitles.search.content@1": qsTr("Subtitle search"),
            "queue.item.actions@1": qsTr("Queue actions"), "player.actions@1": qsTr("Player controls"), "player.panels@1": qsTr("Player panels and their buttons"), "dialog.add-to-playlist.content@1": qsTr("Add-to-playlist picker"),
            "settings.addons.sections@1": qsTr("Add-on settings"), "actions.subtitles.open@1": qsTr("Open subtitle search"), "visualizer.surface@1": qsTr("Visualizer")}
        for (const name of Object.keys(permissions || {}).sort()) {
            const scope = permissions[name] || {}
            switch (name) {
            case "metadata.context": {
                const fields = [...new Set((scope.fields || []).map(field => mediaFields[field]).filter(Boolean))]
                summaries.push(fields.length ? qsTr("Read selected media details: %1.").arg(fields.join(", ")) : qsTr("Read permitted identity details about selected media."))
                break
            }
            case "media.fingerprint": summaries.push(qsTr("Calculate a fingerprint for selected media.")); break
            case "media.stream": summaries.push(qsTr("Play selected media from public online services. Each source also needs your playback approval.")); break
            case "network.http": {
                summaries.push(qsTr("Connect to these services: %1").arg(destinations(scope.origins)))
                if (Array.isArray(scope.assetRedirectProfiles) && scope.assetRedirectProfiles.includes("internet-archive-images@1"))
                    summaries.push(qsTr("Image downloads can follow redirects to archive.org and its subdomains. These redirects use HTTPS on port 443 and do not use credentials."))
                break
            }
            case "network.websocket": summaries.push(qsTr("Maintain a live connection with: %1").arg(destinations(scope.origins))); break
            case "credentials.use": summaries.push(qsTr("Use saved credentials only for these services: %1").arg(destinations([...new Set((scope.bindings || []).map(binding => binding.origin))]))); break
            case "storage.private": summaries.push(qsTr("Store a maximum of %1 MB of add-on data.").arg(Math.ceil(Number(scope.quotaBytes || 0) / (1024 * 1024)))); break
            case "library.read": summaries.push(qsTr("Read permitted media library items and their metadata.")); break
            case "queue.read": summaries.push(qsTr("Read the current play queue.")); break
            case "queue.modify": {
                const actions = [...new Set((scope.operations || []).map(operation => queueActions[operation]).filter(Boolean))]
                summaries.push(actions.length ? qsTr("Change the play queue: %1.").arg(actions.join(", ")) : qsTr("Change the play queue with its approved actions."))
                break
            }
            case "playback.observe": summaries.push(qsTr("Observe playback status, position and time.")); break
            case "playback.control": {
                const actions = [...new Set((scope.operations || []).map(operation => playbackActions[operation]).filter(Boolean))]
                summaries.push(actions.length ? qsTr("Control playback: %1.").arg(actions.join(", ")) : qsTr("Control playback with its approved actions."))
                break
            }
            case "audio.analysis": summaries.push(qsTr("Receive audio analysis for visualizations.")); break
            case "ui.contribute": {
                const labels = [...new Set((scope.hooks || []).map(hook => hooks[hook] || qsTr("Declared app integration points")))]
                summaries.push(qsTr("Add interface content at: %1").arg(labels.join(", ")))
                break
            }
            case "session.background": summaries.push(qsTr("Continue an approved session while its screen is closed.")); break
            case "media.siblings": summaries.push(qsTr("Read the other episodes in the folder or podcast feed of the playing media, without their file locations. Play one when you select it.")); break
            default: summaries.push(qsTr("The app cannot use a requested permission. You cannot install this package.")); break
            }
        }
        return summaries.length ? summaries.join("\n\n") : qsTr("The add-on does not need other permissions.")
    }
}

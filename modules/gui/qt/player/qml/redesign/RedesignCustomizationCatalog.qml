// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

QtObject {
    // Enabled player panels from the add-on registry.
    property var panels: []
    readonly property var panelEntries: (panels || []).map(panel => ({id: "addon:" + panel.id, label: panel.name, icon: "episodes",
        iconMarkup: panel.iconMarkup || "", panel: panel.id}))
    readonly property var entries: [
        {id: "spacer", label: qsTr("Spacer"), kind: "spacer", icon: ""},
        {id: "divider", label: qsTr("Divider"), kind: "divider", icon: "divider"},
        {id: "speed", label: qsTr("Playback Speed"), icon: "speed"},
        {id: "forward10", label: qsTr("Forward 10s"), icon: "forward-10-sec"},
        {id: "backward10", label: qsTr("Backward 10s"), icon: "backward-10-sec"},
        {id: "forward15", label: qsTr("Forward 15s"), icon: "forward-15-sec"},
        {id: "backward15", label: qsTr("Backward 15s"), icon: "backward-15-sec"},
        {id: "stop", label: qsTr("Stop"), icon: "stop"},
        {id: "repeat", label: qsTr("Repeat"), icon: "repeat"},
        {id: "shuffle", label: qsTr("Shuffle"), icon: "shuffle", paletteIcon: "queue-shuffle"},
        {id: "record", label: qsTr("Record"), icon: "record"},
        {id: "snapshot", label: qsTr("Snapshot"), icon: "snapshot"},
        {id: "pip", label: qsTr("Picture in Picture"), icon: "pip"},
        {id: "hdr", label: "HDR", icon: "hdr"},
        {id: "zoom", label: qsTr("Zoom"), icon: "zoom"},
        {id: "rightSidebar", label: qsTr("Right Sidebar"), icon: "sidebar-right"},
        {id: "quit", label: qsTr("Quit Hakone"), icon: "quit"},
        {id: "preferences", label: qsTr("Hakone Settings"), icon: "settings"},
        {id: "home", label: qsTr("Go Home"), icon: "home"},
        {id: "thumbnail", label: qsTr("Thumbnail"), kind: "image", icon: "thumbnail"},
        {id: "fileName", label: qsTr("File Name"), kind: "text", icon: "info"},
        {id: "artist", label: qsTr("Artist Name"), kind: "text", icon: "info"},
        {id: "folderName", label: qsTr("Folder Name"), kind: "text", icon: "info"},
        {id: "playlistName", label: qsTr("Playlist Name"), kind: "text", icon: "info"},
        {id: "properties", label: qsTr("Properties"), icon: "info"},
        {id: "replay", label: qsTr("Replay"), icon: "replay"},
        {id: "chapters", label: qsTr("Chapters"), icon: "chapters"},
        {id: "favorite", label: qsTr("Favorite"), icon: "favorite"},
        {id: "bookmark", label: qsTr("Bookmark"), icon: "bookmark"},
        {id: "bookmarks", label: qsTr("Bookmarks"), icon: "bookmarks"},
        {id: "playlists", label: qsTr("Playlists"), icon: "playlist"},
        {id: "menu", label: qsTr("Menu"), icon: "more"},
        {id: "library", label: qsTr("Library"), icon: "sidebar"},
        {id: "folder", label: qsTr("Media Folder"), icon: "folder"},
        {id: "title", label: qsTr("Media Title"), kind: "text", icon: "info"},
        {id: "minimize", label: qsTr("Minimize"), icon: "minimize"},
        {id: "maximize", label: qsTr("Maximize"), icon: "maximize"},
        {id: "fullscreen", label: qsTr("Fullscreen"), icon: "fullscreen"},
        {id: "close", label: qsTr("Close"), icon: "close"},
        {id: "previous", label: qsTr("Previous"), icon: "previous"},
        {id: "play", label: qsTr("Play"), icon: "play"},
        {id: "next", label: qsTr("Next"), icon: "next"},
        {id: "timeline", label: qsTr("Timeline"), icon: "timeline"},
        {id: "mute", label: qsTr("Mute"), icon: "volume"},
        {id: "volume", label: qsTr("Volume"), icon: "volume"},
        {id: "audio", label: qsTr("Audio"), icon: "audio"},
        {id: "subtitles", label: qsTr("Subtitles"), icon: "subtitles"},
        {id: "settings", label: qsTr("Playback Settings"), icon: "playback-settings"},
        {id: "queue", label: qsTr("Queue"), icon: "queue"}
    ]
    function definition(id) {
        if (id.startsWith("addon:"))
            // A layout can name a panel whose package is not installed now.
            return panelEntries.find(row => row.id === id) || {id: id, label: qsTr("Unavailable add-on panel"), icon: "info", panel: id.slice(6), missing: true}
        return entries.find(row => row.id === id.split(":")[0])
    }
    function available(layout) {
        const used = new Set()
        for (const zone of Object.keys(layout.zones))
            for (const id of layout.zones[zone]) used.add(id)
        return entries.concat(panelEntries).filter(row => row.kind === "spacer" || row.kind === "divider" || !used.has(row.id))
    }
}

// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import VLC.Player
import VLC.MainInterface
import VLC.MediaLibrary
import VLC.Dialogs
import VLC.Style

RedesignTrackAssignmentDialog {
    id: root
    nestedBackdropContent: Component {
        RedesignSurfaceBackdrop {
            sourceItem: root.background ? root.background.parent : null
            scaleFactor: root.scaleFactor
            nativeVideo: false
        }
    }
    sourceFolderDisplay: MainCtx.displayMRL(sourceFolder)
    sourceFolderRoots: MediaLib.folderRoots
    property bool restartNeeded: false
    property double pendingSearch: -1
    property int pendingSearchRequest: -1
    property double pendingPreparation: -1
    property int pendingSaveRequest: -1
    property var pendingSaveRule: ({})
    RedesignPreferences { id: searchPreferences; ctx: MainCtx }
    providerConfigured: SubtitleSources.configured
    providerConfigurationKnown: SubtitleSources.configurationKnown
    providerListCurrent: !SubtitleSources.providerDisplayAttached || SubtitleSources.providerDisplayCurrent
    providerListLoading: SubtitleSources.providerDisplayLoading
    providerListError: SubtitleSources.providerDisplayError
    providerDisplayBackend: typeof AddonManager !== "undefined" ? AddonManager : null
    providerDisplayCandidates: SubtitleSources.displayProviders
    providerActionGuard: () => SubtitleSources.providerDisplayIsCurrent()
    providerHasCustomKey: SubtitleSources.hasCustomKey
    providerHasConsumerKey: SubtitleSources.hasConsumerKey
    onlineLanguages: SubtitleSources.languages
    searchProgressText: SubtitleSources.searchTotal > 1
        ? qsTr("Searching subtitles for file %1 of %2…").arg(Math.min(SubtitleSources.searchCompleted + 1, SubtitleSources.searchTotal)).arg(SubtitleSources.searchTotal) : ""
    parseSearchName: name => SubtitleSources.parseMediaName(name)
    rankOnlineSources: (identity, sources, url, language) => SubtitleSources.rankSources(identity, sources, url, language)
    folderRuleDefinition: draftFolderRule()
    function ruleRows(target) {
        return (target.urls || []).map(url => root.drafts[url]).filter(row => row)
    }
    function draftFolderRule() {
        const target = root.folderRuleTarget
        if (!target.folder || target.remove) return ""
        const rows = ruleRows(target)
        if (root.folderRuleBaseline.length && !root.folderRuleTouched && !rows.some(row => row.manual)) return root.folderRuleBaseline
        return MediaLib.trackFolderRuleDefinition(rows, target.folder, target.source, root.folderRuleBaseline)
    }
    function saveNative(rows, subtitles, request) {
        const target = root.pendingSaveRule
        const definition = target.remove ? "" : target.folder
            ? MediaLib.trackFolderRuleDefinition(ruleRows(target), target.folder, target.source, root.folderRuleBaseline) : ""
        const rule = target.folder && definition !== root.folderRuleBaseline ? {folder:target.folder,definition:definition} : ({})
        if (rule.folder) MediaLib.saveTrackAssignmentsWithRule(rows, subtitles, rule, result => root.finish(request, result))
        else MediaLib.saveTrackAssignments(rows, subtitles, result => root.finish(request, result))
    }
    Connections {
        target: SubtitleSources
        function onSearchFinished(request, result) {
            if (request === root.pendingSearch) root.loadSearch(root.pendingSearchRequest, result)
        }
    }
    RedesignAddonTrackPreparation {
        id: preparation
        sourceService: SubtitleSources
        onPrepared: (rows, subtitles, request, resolved) => {
            if (!root.visible || request !== root.requestId) return
            root.cancelableSave = false
            root.resolveSources(resolved)
            root.saveNative(rows, subtitles, request)
        }
        onFailed: (request, error, resolved) => {
            if (!root.visible || request !== root.requestId) return
            root.cancelableSave = false
            root.resolveSources(resolved)
            root.finish(request, {success:false,error:error,saved:[]})
        }
    }
    scaleFactor: VLCStyle.scale
    lightTheme: !VLCStyle.palette.isDark
    accentColor: MainCtx.redesignAccent
    Binding {
        target: MainCtx; property: "useGlobalShortcuts"; value: false
        when: root.visible; restoreMode: Binding.RestoreBindingOrValue
    }
    onOpened: {
        restartNeeded = false; pendingSaveRule = ({}); SubtitleSources.resetSession()
        searchPreferences.refresh()
        preferredSearchLanguages = searchPreferences.subtitleSearchLanguages()
    }
    onClosed: { pendingSaveRule = ({}); preparation.cancel(); SubtitleSources.resetSession() }
    onSearchRequested: (query, language, request) => {
        pendingSearchRequest = request
        pendingSearch = SubtitleSources.searchMedia({query:query, manual:root.manualSearch,
            details:root.searchDetails, anchor:root.anchorFile, targets:root.searchTargets()}, language)
    }
    onSearchCanceled: SubtitleSources.cancelSearch()
    onLanguagesRequested: SubtitleSources.fetchLanguages()
    onSetupRequested: key => root.finishSetup(SubtitleSources.setApiKey(key))
    onRemoveSetupRequested: root.finishSetup(SubtitleSources.clearApiKey())
    onApiKeyHelpRequested: Qt.openUrlExternally("https://www.opensubtitles.com/consumers")
    onLinksRequested: urls => root.loadLinkedSources(SubtitleSources.addLinks(urls))
    onRowsRequested: (target, subtitles, request) => MediaLib.trackAssignmentRows(target, subtitles, result => root.loadRows(request, result))
    onSourcesRequested: (folder, subtitles, request) => MediaLib.trackAssignmentSources(folder, subtitles, result => root.loadSources(request, result))
    onMatchRequested: (media, tracks, includeSaved, request) => {
        const finish = matches => root.loadMatches(request, matches, includeSaved)
        if (root.onlineMode) MediaLib.requestSubtitleMatches(root.matchRows(), root.onlineSources, root.searchLanguage, finish)
        else MediaLib.requestTrackMatches(media, tracks, finish)
    }
    onMatchingCanceled: MediaLib.cancelTrackMatching()
    onDroppedSourcesRequested: (urls, subtitles, request, target) => {
        const remote = subtitles ? urls.filter(url => /^https?:/i.test(String(url))) : []
        const local = urls.filter(url => !remote.includes(url))
        const linked = remote.length ? SubtitleSources.addLinks(remote.map(String)) : {sources:[],error:""}
        const finish = result => root.loadDroppedSources(request, {
            success: result.success || linked.success,
            sources: (result.sources || []).concat(linked.sources || []),
            folder: result.folder || "",
            error: [result.error, linked.error].filter(value => value).join("\n")
        }, target)
        if (local.length) MediaLib.prepareTrackDrop(local, subtitles, finish)
        else finish({sources:[]})
    }
    onFilesRequested: target => root.addFiles(DialogsProvider.selectTrackFiles(root.subtitles, root.sourceFolder), target)
    onFolderRequested: {
        const folder = DialogsProvider.selectTrackFolder(root.sourceFolder)
        if (folder.toString().length) { root.folderRuleTouched = true; root.sourceFolder = String(folder); root.requestSources(true) }
    }
    onSaveRequested: (rows, subtitles, request) => {
        pendingSaveRule = root.folderRuleChanged ? Object.assign({}, root.folderRuleTarget, {urls:(root.folderRuleTarget.urls || []).slice()}) : ({})
        root.cancelableSave = preparation.requiresPreparation(rows)
        root.pendingSaveRequest = request
        root.pendingPreparation = preparation.prepare(rows, subtitles, request)
    }
    onSaved: (rows, subtitles) => restartNeeded = DialogsProvider.applySavedTrackAssignments(rows, subtitles) || restartNeeded
    onAccepted: if (restartNeeded) DialogsProvider.messageDialog(qsTr("Tracks saved. Stop and play this file to use all changes."), qsTr("VLC Media Player"))
}

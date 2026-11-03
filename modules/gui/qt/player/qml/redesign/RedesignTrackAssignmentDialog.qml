// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

C.Dialog {
    id: root
    popupType: C.Popup.Item
    property real scaleFactor: 1
    property bool lightTheme: false
    property color accentColor: "#d37918"
    property Component videoBackdropContent
    property Component nestedBackdropContent
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    property bool subtitles: true
    property var context: ({})
    property var rows: []
    property var addonRegistry: typeof redesignAddonUi === "undefined" ? null : redesignAddonUi
    property bool addonFallback: false
    readonly property var addonReplacement: { if (!addonRegistry) return ({}); addonRegistry.revision; return addonRegistry.replacement("subtitles.search.content@1") }
    readonly property string addonSelectionId: addonReplacement.id || ""
    readonly property bool addonWanted: onlineMode && providerConfigured && addonSelectionId.length > 0 && !addonFallback
    onAddonSelectionIdChanged: addonFallback = false
    readonly property var addonTargets: rows.reduce((result, row) => {
        const key = String(row.url)
        if (!result.some(item => item.key === key)) result.push({key: key, title: String(row.name || fileName(key)).slice(0, 512)})
        return result
    }, [])
    readonly property var addonAssets: visibleSources.map(row => ({key: String(row.url), title: String(row.name || fileName(row.url)).slice(0, 512), enabled: row.available !== false}))
    readonly property bool addonSelectionChanged: addonPicker.usingReplacement && !!addonPicker.preview && addonPicker.preview.some(assignment => {
        const draft = drafts[assignment.targetKey]
        return !!draft && (draft.inheritedRule || !same(draft.initialTracks, addonTracks(assignment)))
    })
    readonly property real fileNameColumnWidth: {
        fileNameMetrics.font
        return Math.min(theme.dp(240), rows.reduce((width, row) =>
            Math.max(width, fileNameMetrics.advanceWidth(String(row.name)) + theme.dp(2)), theme.dp(156)))
    }
    FontMetrics { id: fileNameMetrics; font.family: Qt.application.font.family; font.pixelSize: root.theme.dp(14) }
    property var sources: []
    property var drafts: ({})
    property var selectedRows: ({})
    property string anchorFile: ""
    property string targetFolder: ""
    property string sourceFolder: ""
    property string sourceFolderDisplay: sourceFolder
    property var sourceFolderRoots: []
    property int sourceFolderCategory: 0
    readonly property string sourceFolderCaption: sourceFolder.length
        ? folderPath.libraryCaption(sourceFolder, sourceFolderRoots, sourceFolderCategory === 1 ? qsTr("Videos") : sourceFolderCategory === 2 ? qsTr("Music") : "") || sourceFolderDisplay
        : qsTr("Select track folder")
    RedesignLibraryPath { id: folderPath }
    property int sourceIndex: 0
    property bool providerHasCustomKey: false
    property bool providerHasConsumerKey: false
    property bool providerConfigured: false
    property bool providerConfigurationKnown: true
    property bool providerListCurrent: true
    property bool providerListLoading: false
    property string providerListError: ""
    property var providerDisplayBackend: null
    property var providerDisplayCandidates: []
    property var providerActionGuard: null
    property bool providerCheckPending: false
    readonly property bool providerSearchReady: providerConfigured && providerConfigurationKnown && providerListCurrent
    onProviderSearchReadyChanged: runPendingProviderSearch()
    onOnlineModeChanged: if (!onlineMode) providerCheckPending = false
    RedesignAddonFeatureDemand {
        id: providerDemand
        backend: root.providerDisplayBackend; feature: "subtitles"
        active: root.visible && root.onlineMode
        eligible: root.providerDisplayCandidates.some(row => row.enabled === true && row.builtin !== true
            && (row.configurationKnown !== true || row.configured === true))
        onCurrentChanged: Qt.callLater(root.runPendingProviderSearch)
        onBindingReadyChanged: Qt.callLater(root.runPendingProviderSearch)
    }
    property bool searching: false
    property string searchProgressText: ""
    property int searchRequestId: 0
    property string searchTitle: ""
    property bool manualSearch: false
    property var searchDetails: ({})
    property var searchIdentities: ({})
    property var parseSearchName: null
    property var rankOnlineSources: null
    property string searchLanguage: ""
    property var preferredSearchLanguages: []
    property bool manualSearchLanguage: false
    onPreferredSearchLanguagesChanged: refreshSearchLanguage()
    onOnlineLanguagesChanged: Qt.callLater(refreshSearchLanguage)
    readonly property var searchLanguageChoices: [{code:"",text:qsTr("All languages")}].concat(onlineLanguages)
    property var sourceMappings: ({})
    property var sourceKinds: ({})
    property var onlineSources: []
    property var linkSources: []
    property string linkText: ""
    property var onlineLanguages: [{code:"en",text:qsTr("English")},{code:"es",text:qsTr("Spanish")},{code:"ja",text:qsTr("Japanese")},{code:"de",text:qsTr("German")},{code:"nl",text:qsTr("Dutch")},{code:"fr",text:qsTr("French")}]
    property bool cancelableSave: false
    readonly property bool onlineMode: subtitles && sourceIndex === 1
    readonly property bool linkMode: subtitles && sourceIndex === 2
    readonly property var activeSources: onlineMode ? onlineSources : linkMode ? linkSources : sources
    readonly property var sourceChoices: subtitles ? [qsTr("From Computer"),qsTr("Online providers"),qsTr("From Link")] : [qsTr("From Computer")]
    onSourceIndexChanged: Qt.callLater(() => sourceChoice.currentIndex = root.sourceIndex)
    property int scopeIndex: 0
    onScopeIndexChanged: Qt.callLater(() => scopeChoice.currentIndex = root.scopeIndex)
    property int requestId: 0
    property int sourceRequestId: 0
    property bool loading: false
    property bool loadingSources: false
    property bool matchSavedSources: false
    property bool matching: false
    property int matchRequestId: 0
    property bool loadingDrop: false
    property int dropRequestId: 0
    property bool saving: false
    property string errorMessage: ""
    property string folderRuleBaseline: ""
    property string folderRuleDefinition: ""
    property bool folderRuleLoaded: false
    property bool folderRuleTouched: false
    property bool folderRuleAutomatic: true
    property bool removeFolderRule: false
    property var pendingFolderRule: ({})
    readonly property var folderRuleTarget: scopeIndex === 1
        ? {folder:targetFolder,source:sourceFolder,urls:rows.map(row => row.url),remove:removeFolderRule} : pendingFolderRule
    readonly property bool folderRuleNeedsPreparation: !!folderRuleTarget.folder
        && (folderRuleTarget.urls || []).some(url => tracks(url).some(track => /^(?:opensubtitles|subtitlelink|addon-subtitle):/.test(String(track))))
    readonly property bool folderRuleChanged: !!folderRuleTarget.folder
        && ((folderRuleTarget.remove && folderRuleBaseline.length > 0) || folderRuleNeedsPreparation || folderRuleDefinition !== folderRuleBaseline)
    readonly property bool fileMode: scopeIndex === 0
    readonly property var scopes: [
        {text: qsTr("To This File"), enabled: anchorFile.length > 0},
        {text: qsTr("To This Folder"), enabled: targetFolder.length > 0},
        {text: qsTr("To Playlist"), enabled: !!context.key || (context.urls || []).length > 0}
    ]
    readonly property var changedRows: Object.keys(drafts).map(url => drafts[url]).filter(row => !same(row.initialTracks, row.tracks) || (row.inheritedRule && row.manual))
    readonly property var visibleSources: {
        const result = activeSources.slice()
        const seen = result.map(item => item.url)
        for (const row of rows) for (const url of (drafts[row.url]?.initialTracks || []).concat(tracks(row.url)))
            if (sourceKind(url) === sourceIndex && !seen.includes(url)) { seen.push(url); result.push({url: url, name: fileName(url)}) }
        return result
    }
    signal searchRequested(string query, string language, int request)
    signal searchCanceled()
    signal languagesRequested()
    signal setupRequested(string key)
    signal apiKeyHelpRequested()
    signal removeSetupRequested()
    signal linksRequested(var urls)
    signal rowsRequested(var target, bool subtitles, int request)
    signal sourcesRequested(url folder, bool subtitles, int request)
    signal matchRequested(var media, var tracks, bool includeSaved, int request)
    signal matchingCanceled()
    signal droppedSourcesRequested(var urls, bool subtitles, int request, string target)
    signal filesRequested(string target)
    signal folderRequested()
    signal saveRequested(var rows, bool subtitles, int request)
    signal saved(var rows, bool subtitles)
    title: subtitles ? qsTr("Add Subtitles") : qsTr("Add Audio Tracks")
    readonly property real pixelRatio: parent ? parent.Screen.devicePixelRatio : 1
    x: parent ? Math.round((parent.width - width) * pixelRatio / 2) / pixelRatio : 0
    y: parent ? Math.round((parent.height - height) * pixelRatio / 2) / pixelRatio : 0
    width: Math.min(theme.dp(1048), parent ? parent.width - theme.dp(32) : theme.dp(1048))
    height: Math.min(theme.dp(800), parent ? parent.height - theme.dp(32) : theme.dp(800))
    padding: 0; spacing: 0; modal: true; dim: true; focus: true
    closePolicy: saving && !cancelableSave ? C.Popup.NoAutoClose : C.Popup.CloseOnEscape
    background: RedesignDialogSurface {
        theme: root.theme
        fill: root.theme.lightTheme ? root.theme.panel : "#0e0e0e"
        videoBackdropContent: root.videoBackdropContent
    }
    C.Overlay.modal: Rectangle { color: "#80000000" }

    function fileName(url) {
        const name = String(url).split("/").pop()
        try { return decodeURIComponent(name) } catch (error) { return name }
    }
    function sourceKind(url) {
        if (String(url).startsWith("subtitlelink:") || linkSources.some(item => item.url === url)) return 2
        if (/^(?:opensubtitles|addon-subtitle):/.test(String(url)) || onlineSources.some(item => item.url === url)) return 1
        return sourceKinds[url] || 0
    }
    function parentFolder(url) { return String(url).startsWith("file:") ? String(url).substring(0, String(url).lastIndexOf("/") + 1) : "" }
    function same(a, b) { return [...new Set(a || [])].sort().join("\n") === [...new Set(b || [])].sort().join("\n") }
    function tracks(url) { return drafts[url] ? drafts[url].tracks : [] }
    function identityQuery(identity) {
        if (!identity || !identity.title) return ""
        let query = identity.title
        if (identity.year > 0) query += " (" + identity.year + ")"
        if (identity.season >= 0) {
            query += " S" + String(identity.season).padStart(2, "0")
            for (const episode of identity.episodes || []) query += "E" + String(episode).padStart(2, "0")
        } else if (identity.absoluteEpisode) query += " - " + identity.absoluteEpisode
        else if (identity.airDate) query += " " + identity.airDate
        return query
    }
    function searchTargets() {
        return fileMode ? rows.filter(row => row.url === anchorFile) : rows
    }
    function matchRows() {
        return searchTargets().map(row => Object.assign({}, row,
            searchIdentities[row.url] ? {identity:searchIdentities[row.url]} : {}))
    }
    function editSearchDetails() {
        const row = rows.find(row => row.url === anchorFile)
        const identity = manualSearch && parseSearchName ? Object.assign({}, parseSearchName(searchTitle), searchDetails)
            : row?.identity || {title:searchTitle}
        detailTitle.text = identity.title || ""
        detailYear.text = identity.year > 0 ? String(identity.year) : ""
        detailSeason.text = identity.season >= 0 ? String(identity.season) : ""
        detailEpisodes.text = (identity.episodes || []).join(", ")
        detailAbsolute.text = identity.absoluteEpisode || ""
        detailAirDate.text = identity.airDate || ""
        detailType.currentIndex = identity.type === "movie" ? 1 : identity.type === "episode" ? 2 : 0
        searchEditor.open()
    }
    function resetSearchDetails() {
        manualSearch = false; searchDetails = ({}); searchIdentities = ({})
        const row = rows.find(row => row.url === anchorFile)
        searchTitle = row?.identity ? identityQuery(row.identity) : fileName(anchorFile).replace(/\.[^.]+$/, "").replace(/[._]/g, " ")
        requestSearch()
    }
    function pickerSources() {
        if (!onlineMode || !rankOnlineSources) return visibleSources
        const row = matchRows().find(row => row.url === picker.targetUrl)
        return row?.identity ? rankOnlineSources(row.identity, visibleSources, picker.targetUrl, searchLanguage) : visibleSources
    }
    function sourceDescription(source) {
        return [source.matchReason, source.language, source.forced ? qsTr("Forced") : "", source.hearingImpaired ? qsTr("SDH") : ""].filter(Boolean).join(" · ")
    }
    function refreshSearchLanguage() {
        let language = searchLanguage
        if (!manualSearchLanguage)
            language = preferredSearchLanguages.find(code => onlineLanguages.some(item => item.code === code)) || ""
        else if (!searchLanguageChoices.some(item => item.code === language)) language = ""
        const changed = language !== searchLanguage
        searchLanguage = language
        if (changed && visible && onlineMode) requestSearch()
    }
    function begin(target, subtitleMode) {
        if (visible || saving) return false
        context = target; subtitles = subtitleMode
        addonFallback = false
        sourceIndex = 0; providerCheckPending = false; onlineSources = []; linkSources = []; linkText = ""; sourceMappings = ({}); sourceKinds = ({}); searchTitle = ""; manualSearch = false; searchDetails = ({}); searchIdentities = ({}); manualSearchLanguage = false; searching = false; cancelableSave = false
        refreshSearchLanguage()
        rows = []; sources = []; drafts = ({}); selectedRows = ({}); errorMessage = ""
        sourceFolderCategory = 0
        folderRuleBaseline = ""; folderRuleLoaded = false; folderRuleTouched = false; folderRuleAutomatic = true; removeFolderRule = false; pendingFolderRule = ({})
        anchorFile = target.scope === "folder" || target.scope === "playlist" ? "" : String(target.url || "")
        targetFolder = target.scope === "folder" ? String(target.folder || target.url || "") : parentFolder(anchorFile)
        sourceFolder = targetFolder
        scopeIndex = target.scope === "folder" ? 1 : ["playlist", "queue"].includes(target.scope) ? 2 : 0
        open(); requestRows()
        return true
    }
    function cancelMatching() { ++matchRequestId; matching = false; matchingCanceled() }
    function requestRows() {
        ++dropRequestId; loadingDrop = false
        cancelMatching()
        ++requestId; loading = true; errorMessage = ""; selectedRows = ({})
        const target = scopeIndex === 0 ? {scope: "file", url: anchorFile}
            : scopeIndex === 1 ? {scope: "folder", folder: targetFolder}
            : context.key ? {scope: "playlist", key: context.key} : {scope: "queue", urls: context.urls || []}
        target.runtimeUrl = context.runtimeUrl
        target.runtimeTracks = context.runtimeTracks
        target.runtimeCategory = context.runtimeCategory
        target.runtimeMetadata = context.runtimeMetadata
        target.libraryCategory = context.libraryCategory
        rowsRequested(target, subtitles, requestId)
    }
    function loadRows(request, result) {
        if (!visible || request !== requestId) return
        loading = false
        if (!result.success) { rows = []; errorMessage = result.error || qsTr("Hakone cannot read these files. Try again."); return }
        sourceFolderCategory = result.folderCategory || 0
        rows = result.rows || []
        if (scopeIndex === 1) {
            folderRuleBaseline = result.folderRule || ""
            folderRuleAutomatic = result.automaticMatching !== false
            if (!folderRuleLoaded && !folderRuleTouched && result.sourceFolder) sourceFolder = result.sourceFolder
            folderRuleLoaded = true
            if (result.ruleError) errorMessage = result.ruleError
        }
        const next = Object.assign({}, drafts)
        for (const row of rows) if (!next[row.url] || (!next[row.url].manual && same(next[row.url].initialTracks, next[row.url].tracks))) next[row.url] = {
            url: row.url, baseline: Array.from(row.baseline || []),
            initialTracks: Array.from(row.initialTracks || row.baseline || []),
            tracks: Array.from(row.initialTracks || row.baseline || []),
            explicitSelection: !!row.explicitSelection, inheritedRule: !!row.inheritedRule, manual: false
        }
        drafts = next
        if (!anchorFile && rows.length) anchorFile = rows[0].url
        if (!targetFolder) targetFolder = parentFolder(anchorFile)
        if (!sourceFolder && targetFolder) sourceFolder = targetFolder
        if (!searchTitle && anchorFile) {
            const anchor = rows.find(row => row.url === anchorFile)
            searchTitle = anchor?.identity ? identityQuery(anchor.identity) : fileName(anchorFile).replace(/\.[^.]+$/, "").replace(/[._]/g, " ")
        }
        if (onlineMode) requestSearch()
        else if (linkMode) performMatching()
        else if (sourceFolder) requestSources()
        else performMatching()
    }
    function requestSources(includeSaved = false) {
        ++dropRequestId; loadingDrop = false
        cancelMatching()
        matchSavedSources = includeSaved
        ++sourceRequestId; loadingSources = true
        sourcesRequested(sourceFolder, subtitles, sourceRequestId)
    }
    function loadSources(request, result) {
        if (!visible || request !== sourceRequestId) return
        loadingSources = false
        if (!result.success) { errorMessage = result.error || qsTr("Hakone cannot read this folder. Try again."); return }
        sources = result.sources || []; performMatching(matchSavedSources)
    }
    function performMatching(includeSaved = false) {
        cancelMatching()
        if ((!fileMode || onlineMode) && !loading && !loadingSources) {
            matching = true
            matchRequested(rows.map(row => row.url), activeSources.map(source => source.url), includeSaved, matchRequestId)
        }
    }
    function loadMatches(request, matches, includeSaved = false) {
        if (!visible || request !== matchRequestId) return
        matching = false
        applyMatches(matches, includeSaved)
    }
    function applyMatches(matches, includeSaved = false) {
        const next = Object.assign({}, drafts)
        for (const row of rows) {
            const draft = next[row.url]
            if (draft && !draft.manual && ((!draft.explicitSelection && !draft.inheritedRule) || includeSaved)) {
                const selection = [...new Set(draft.initialTracks.concat(matches[row.url] || []))]
                if (includeSaved && !same(draft.tracks, selection)
                    && ((folderRuleTarget.urls || []).includes(row.url) || parentFolder(row.url) === targetFolder)) folderRuleTouched = true
                next[row.url] = Object.assign({}, draft, {tracks: selection})
            }
        }
        drafts = next
    }
    function selectSource(index) {
        if (saving || loadingDrop || index === sourceIndex || index < 0 || index >= sourceChoices.length) return
        searchDebounce.stop(); ++searchRequestId; searching = false; searchCanceled(); cancelMatching()
        ++sourceRequestId; loadingSources = false; errorMessage = ""; sourceIndex = index
        if (onlineMode) { providerCheckPending = true; runPendingProviderSearch() }
        else if (linkMode) performMatching()
        else if (sourceFolder) requestSources()
    }
    function requestSearch() {
        searchDebounce.stop()
        if (!visible || !onlineMode || saving || loading || loadingDrop) return
        if (!providerSearchReady) { providerCheckPending = true; return }
        if (!providerCurrentForAction()) { providerCheckPending = true; return }
        providerCheckPending = false
        cancelMatching(); ++searchRequestId; searching = true; errorMessage = ""
        searchRequested(searchTitle, searchLanguage, searchRequestId)
    }
    function providerCurrentForAction() {
        // An attached backend must finish fixed-mode capture first. Portable
        // providers require their exact presence ticket. Configured builtins
        // use the current native guard without creating a portable demand.
        let demandReady = !providerDisplayBackend && !providerDemand.hadBackend
        if (!demandReady && providerDemand.bindingReady) {
            if (!providerDemand.displayMode || providerDemand.eligible)
                demandReady = providerDemand.currentForAction()
            else
                demandReady = typeof providerActionGuard === "function" && providerDemand.managementCurrent()
                    && providerDisplayCandidates.some(row => row.builtin === true && row.enabled === true
                        && row.available !== false && row.configured === true && row.configurationKnown !== false)
        }
        const current = demandReady && (typeof providerActionGuard !== "function" || providerActionGuard())
        if (!current) errorMessage = qsTr("The subtitle provider list changed. Wait for it to finish or try again.")
        return current
    }
    function runPendingProviderSearch() {
        if (!providerCheckPending || !visible || !onlineMode || !providerSearchReady || saving || loading || loadingDrop) return
        if (!providerCurrentForAction()) return
        providerCheckPending = false; languagesRequested(); requestSearch()
    }
    function loadSearch(request, result) {
        if (!visible || request !== searchRequestId || !onlineMode) return
        searching = false
        if (!result.success) { errorMessage = result.error || qsTr("Hakone cannot search for subtitles. Try again."); return }
        searchIdentities = result.identities || ({})
        errorMessage = result.warning || ""
        onlineSources = (result.sources || []).map(item => Object.assign({}, item, {url: sourceMappings[item.url] || item.url})); performMatching(true)
    }
    function resolveSources(mapping) {
        const kinds = Object.assign({}, sourceKinds)
        for (const url of Object.keys(mapping || {})) kinds[mapping[url]] = sourceKind(url)
        sourceKinds = kinds
        sourceMappings = Object.assign({}, sourceMappings, mapping || {})
        onlineSources = onlineSources.map(item => Object.assign({}, item, {url: sourceMappings[item.url] || item.url}))
        linkSources = linkSources.map(item => sourceMappings[item.url]
            ? Object.assign({}, item, {url: sourceMappings[item.url], name: fileName(sourceMappings[item.url])}) : item)
        const next = Object.assign({}, drafts)
        for (const url of Object.keys(next)) next[url] = Object.assign({}, next[url], {
            tracks: [...new Set(next[url].tracks.map(track => sourceMappings[track] || track))]
        })
        drafts = next
    }
    function finishSetup(success, error = "") {
        if (success) {
            providerSetup.close()
            providerCheckPending = true; runPendingProviderSearch()
        }
        else providerSetup.errorMessage = error || qsTr("Hakone cannot save the API key. Make sure that the API key is correct. Try again.")
    }
    function setTracks(url, selection) {
        const draft = drafts[url]
        if (!draft || saving) return
        if ((folderRuleTarget.urls || []).includes(url) || parentFolder(url) === targetFolder) folderRuleTouched = true
        drafts = Object.assign({}, drafts, {[url]: Object.assign({}, draft, {tracks: [...new Set(selection)], manual: true})})
        errorMessage = ""
    }
    function toggle(url, track) {
        const selected = tracks(url)
        setTracks(url, selected.includes(track) ? selected.filter(item => item !== track) : selected.concat([track]))
    }
    function addFiles(files, target) {
        addSources(files.map(value => ({url: String(value), name: fileName(value)})), target)
    }
    function addSources(items, target) {
        if (!items.length) return
        const local = sources.slice(), linked = linkSources.slice()
        for (const item of items) {
            const next = item.kind === "link" || String(item.url).startsWith("subtitlelink:") ? linked : local
            if (!next.some(source => source.url === item.url)) next.push(item)
        }
        sources = local; linkSources = linked
        const url = target || (fileMode ? anchorFile : "")
        if (url) setTracks(url, tracks(url).concat(items.map(item => item.url)))
        else performMatching(true)
    }
    function addLink() {
        if (saving || loading || loadingDrop || !linkText.trim()) return
        linksRequested([linkText.trim()])
    }
    function loadLinkedSources(result) {
        if (!visible || !linkMode) return
        if (result.success) { addSources(result.sources || [], ""); linkText = "" }
        errorMessage = result.error || ""
    }
    function dropUrls(drop) {
        return drop.hasUrls ? drop.urls : subtitles && drop.hasText ? drop.text.split(/\r?\n/).map(value => value.trim()).filter(value => value.length) : []
    }
    function dropSources(urls, target = "") {

        if (!visible || saving || loading || loadingDrop) return false
        if (!urls.length) { errorMessage = qsTr("Hakone cannot read these files. Try again."); return false }
        const values = Array.from(urls, value => String(value))
        if (subtitles && values.every(url => /^https?:/i.test(url))) selectSource(2)
        else selectSource(0)
        cancelMatching(); ++sourceRequestId; loadingSources = false
        ++dropRequestId; loadingDrop = true; errorMessage = ""
        droppedSourcesRequested(values, subtitles, dropRequestId, target)
        return true
    }
    function loadDroppedSources(request, result, target = "") {

        if (!visible || request !== dropRequestId) return
        loadingDrop = false
        if (result.success) {
            if (result.folder) { sourceFolder = String(result.folder); folderRuleTouched = true }
            addSources(result.sources || [], target)
        }
        errorMessage = result.error || ""
    }
    function selectScope(index) {
        if (index === scopeIndex || !scopes[index].enabled || saving || loadingDrop) return
        if (scopeIndex === 1) pendingFolderRule = folderRuleChanged ? folderRuleTarget : ({})
        scopeIndex = index; picker.close(); requestRows()
    }
    function chooseRow(url) { picker.targetUrl = url; picker.open() }
    function submit() {
        if (saving || loading || loadingSources || loadingDrop || matching) return
        if (addonWanted) {
            if (!addonPicker.prepared || !addonPicker.usingReplacement) return
            if (addonPicker.preview && (addonSelectionChanged || folderRuleChanged)) addonPicker.confirm()
            else if (folderRuleChanged) { discardAddonAutomaticDrafts(); submitCore() }
            return
        }
        submitCore()
    }
    function addonTracks(assignment) {
        const draft = drafts[assignment.targetKey]
        if (!draft || !addonPicker.snapshotData) return []
        const offered = new Set(Object.values(addonPicker.snapshotData.assetBindings).map(binding => binding.key))
        const current = draft.manual ? draft.tracks : draft.initialTracks
        return current.filter(track => !offered.has(String(track))).concat(assignment.assetKeys)
    }
    function discardAddonAutomaticDrafts() {
        const next = Object.assign({}, drafts)
        for (const key of Object.keys(next)) if (!next[key].manual) next[key] = Object.assign({}, next[key], {tracks: Array.from(next[key].initialTracks)})
        drafts = next
    }
    function applyAddonSelection(assignments) {
        const selections = assignments.map(assignment => ({target: assignment.targetKey, tracks: addonTracks(assignment)}))
        discardAddonAutomaticDrafts()
        for (const selection of selections) setTracks(selection.target, selection.tracks)
        submitCore()
    }
    function submitCore() {
        if (saving || loading || loadingSources || loadingDrop || matching || (!changedRows.length && !folderRuleChanged)) return
        searchDebounce.stop(); ++searchRequestId; searching = false; searchCanceled()
        saving = true; picker.close(); errorMessage = ""
        saveRequested(changedRows, subtitles, requestId)
    }
    function finish(request, result) {
        if (!visible || request !== requestId) return
        saving = false
        const completed = result.saved || []
        const next = Object.assign({}, drafts)
        for (const row of completed) if (next[row.url]) next[row.url] = Object.assign({}, next[row.url], {baseline: Array.from(row.tracks), initialTracks: Array.from(row.tracks), tracks: Array.from(row.tracks), explicitSelection: true, inheritedRule: false, manual: false})
        drafts = next
        if (result.folderRuleSaved) { folderRuleBaseline = result.folderRule || ""; pendingFolderRule = ({}); folderRuleTouched = false; removeFolderRule = false }
        if (completed.length) saved(completed, subtitles)
        if (result.success) accept()
        else {
            errorMessage = (result.error || qsTr("Hakone cannot save these tracks. Try again."))
                + (completed.length ? "\n" + qsTr("Hakone kept the changes that it saved. Try Save again for the other files.") : "")
            addonPicker.begin()
        }
    }
    onClosed: { ++searchRequestId; providerCheckPending = false; searching = false; searchDebounce.stop(); searchCanceled(); providerSetup.close(); searchEditor.close(); manualSearch = false; searchDetails = ({}); searchIdentities = ({}); linkText = ""; linkSources = []; onlineSources = []; sourceMappings = ({}); sourceKinds = ({}); pendingFolderRule = ({}); removeFolderRule = false; folderRuleBaseline = ""; folderRuleLoaded = false; folderRuleTouched = false; saving = false; cancelableSave = false; ++dropRequestId; loadingDrop = false; cancelMatching(); ++requestId; ++sourceRequestId; picker.close(); rows = []; sources = []; drafts = ({}); selectedRows = ({}) }
    Timer { id: searchDebounce; interval: 600; onTriggered: root.requestSearch() }
    Shortcut { sequence: "Ctrl+O"; enabled: root.visible && !root.saving; onActivated: { root.selectSource(0); root.filesRequested("") } }
    Shortcut { sequences: ["Ctrl+Return", "Ctrl+Enter"]; enabled: root.visible && !root.saving; onActivated: root.submit() }

    component HeaderComboBox: RedesignComboBox {
        labelColor: root.lightTheme ? root.theme.foreground : "#969696"
        leftPadding: root.theme.dp(12); labelVerticalOffset: 0.5
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        background: Rectangle {
            color: root.lightTheme ? root.theme.panel : "#151515"; radius: root.theme.dp(6)
            border.color: parent.visualFocus ? root.theme.accent : "transparent"
        }
        indicator: RedesignIcon {
            width: root.theme.dp(24); height: width
            x: parent.width - width - root.theme.dp(8); y: (parent.height - height) / 2
            name: "chevron-down"; color: root.theme.foreground
        }
    }

    header: Item {
        implicitHeight: root.theme.dp(96)
        Rectangle {
            anchors.fill: parent; radius: root.theme.dp(14); color: root.theme.field
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: parent.height / 2; color: parent.color }
        }
        Text {
            x: root.theme.dp(19); y: (root.theme.dp(50) - height) / 2 + root.theme.dp(0.5)
            width: root.onlineMode && root.addonReplacement.id ? Math.max(0, parent.width - root.theme.dp(root.width >= root.theme.dp(650) ? 380 : 270)) : implicitWidth
            text: root.title; textFormat: Text.PlainText; elide: Text.ElideRight; color: root.theme.foreground; font.pixelSize: root.theme.dp(14)
        }
        RedesignButton {
            objectName: "addonSubtitleRecovery"
            anchors.right: parent.right; anchors.rightMargin: root.theme.dp(192); y: root.theme.dp(7)
            caption: root.width >= root.theme.dp(650) ? qsTr("Use built-in view") : ""; iconName: "back"; text: qsTr("Use built-in view")
            tooltipTextFormat: Text.PlainText; visible: root.onlineMode && !!root.addonReplacement.id; enabled: !root.saving
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            onClicked: addonPicker.useBuiltin()
        }
        RedesignButton {
            objectName: "trackAssignmentDetails"
            visible: root.onlineMode
            anchors.right: parent.right; anchors.rightMargin: root.theme.dp(52); y: root.theme.dp(7)
            implicitWidth: root.theme.dp(130); implicitHeight: root.theme.dp(36)
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            caption: qsTr("Search details"); text: caption; enabled: !root.saving && !root.loading
            onClicked: root.editSearchDetails()
        }
        RedesignButton {
            objectName: "trackAssignmentClose"
            anchors.right: parent.right; anchors.rightMargin: root.theme.dp(7); y: root.theme.dp(7)
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            iconName: "close"; text: qsTr("Close"); enabled: !root.saving || root.cancelableSave; onClicked: root.reject()
        }
        RowLayout {
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            anchors.leftMargin: root.theme.dp(9); anchors.rightMargin: root.theme.dp(9); anchors.bottomMargin: root.theme.dp(9)
            spacing: root.theme.dp(10)
            HeaderComboBox {
                id: scopeChoice
                objectName: "trackAssignmentScope"
                Layout.preferredWidth: Math.min(root.theme.dp(200), root.width * 0.23); Layout.preferredHeight: root.theme.dp(34)
                model: root.scopes; textRole: "text"; restrictChoices: true
                currentIndex: root.scopeIndex; enabled: !root.loading && !root.loadingDrop && !root.saving
                onModelChanged: Qt.callLater(() => scopeChoice.currentIndex = root.scopeIndex)
                onActivated: index => root.selectScope(index)
            }
            HeaderComboBox {
                id: sourceChoice
                objectName: "trackAssignmentFiles"
                Layout.preferredWidth: Math.min(root.theme.dp(200), root.width * 0.23); Layout.preferredHeight: root.theme.dp(34)
                model: root.sourceChoices; currentIndex: root.sourceIndex
                enabled: !root.loading && !root.loadingDrop && !root.saving
                onModelChanged: Qt.callLater(() => sourceChoice.currentIndex = root.sourceIndex)
                onActivated: index => root.selectSource(index)
            }
            RedesignButton {
                objectName: "trackAssignmentFolder"
                visible: root.sourceIndex === 0
                Layout.fillWidth: true; Layout.preferredHeight: root.theme.dp(34)
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                iconName: "folder"; text: root.sourceFolder.length ? root.sourceFolderDisplay : root.sourceFolderCaption; tooltipTextFormat: Text.PlainText; leftAligned: true
                enabled: !root.saving && !root.loadingDrop
                background: Rectangle { color: root.lightTheme ? root.theme.panel : "#151515"; radius: root.theme.dp(6); border.color: parent.visualFocus ? root.theme.accent : "transparent" }
                contentItem: RowLayout {
                    spacing: root.theme.dp(10)
                    RedesignIcon { objectName: "trackAssignmentFolderIcon"; Layout.leftMargin: root.theme.dp(8); Layout.preferredWidth: root.theme.dp(24); Layout.preferredHeight: root.theme.dp(24); name: "folder"; color: root.theme.foreground }
                    Text { objectName: "trackAssignmentFolderCaption"; Layout.fillWidth: true; Layout.rightMargin: root.theme.dp(12); text: root.sourceFolderCaption; textFormat: Text.PlainText; elide: Text.ElideMiddle; font.pixelSize: root.theme.dp(14); color: root.lightTheme ? root.theme.foreground : "#969696" }
                }
                onClicked: root.folderRequested()
            }
            C.TextField {
                objectName: "trackAssignmentSearch"
                visible: root.onlineMode
                Layout.fillWidth: true; Layout.preferredHeight: root.theme.dp(34)
                enabled: root.providerSearchReady && !root.saving
                text: root.searchTitle; placeholderText: qsTr("Search subtitles")
                leftPadding: root.theme.dp(42); rightPadding: root.theme.dp(10)
                color: root.lightTheme ? root.theme.foreground : "#969696"; placeholderTextColor: root.theme.foreground
                font.pixelSize: root.theme.dp(14); selectByMouse: true
                Accessible.name: qsTr("Search subtitles")
                background: Rectangle { color: root.lightTheme ? root.theme.panel : "#151515"; radius: root.theme.dp(6); border.color: parent.visualFocus ? root.theme.accent : "transparent" }
                RedesignButton {
                    objectName: "trackAssignmentSearchButton"
                    anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                    implicitWidth: root.theme.dp(40); implicitHeight: root.theme.dp(34)
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    iconName: "search"; iconSize: root.theme.dp(24); text: qsTr("Search subtitles")
                    onClicked: root.requestSearch()
                }
                onTextEdited: { root.searchTitle = text; root.manualSearch = true; root.searchDetails = ({}); searchDebounce.restart() }
                onAccepted: root.requestSearch()
            }
            HeaderComboBox {
                id: languageChoice
                objectName: "trackAssignmentLanguage"
                visible: root.onlineMode
                Layout.preferredWidth: Math.min(root.theme.dp(200), root.width * 0.20); Layout.preferredHeight: root.theme.dp(34)
                model: root.searchLanguageChoices; textRole: "text"
                enabled: root.providerSearchReady && !root.saving
                function syncLanguage() { currentIndex = root.searchLanguageChoices.findIndex(item => item.code === root.searchLanguage) }
                Connections { target: root; function onSearchLanguageChanged() { languageChoice.syncLanguage() } }
                onModelChanged: Qt.callLater(syncLanguage)
                Component.onCompleted: syncLanguage()
                onActivated: index => { root.manualSearchLanguage = true; root.searchLanguage = root.searchLanguageChoices[index].code; root.requestSearch() }
                background: Rectangle { color: root.lightTheme ? root.theme.panel : "#151515"; radius: root.theme.dp(6); border.color: parent.visualFocus ? root.theme.accent : "transparent" }
            }
            C.TextField {
                objectName: "trackAssignmentLink"
                visible: root.linkMode
                Layout.fillWidth: true; Layout.preferredHeight: root.theme.dp(34)
                text: root.linkText; placeholderText: qsTr("Paste a direct subtitle file link")
                enabled: !root.saving && !root.loadingDrop
                leftPadding: root.theme.dp(40); rightPadding: root.theme.dp(10)
                color: root.theme.foreground; placeholderTextColor: root.theme.foreground
                font.pixelSize: root.theme.dp(14); selectByMouse: true
                Accessible.name: placeholderText
                background: Rectangle { color: root.lightTheme ? root.theme.panel : "#151515"; radius: root.theme.dp(6); border.color: parent.visualFocus ? root.theme.accent : "transparent" }
                RedesignIcon { anchors.left: parent.left; anchors.leftMargin: root.theme.dp(10); anchors.verticalCenter: parent.verticalCenter; width: root.theme.dp(20); height: width; name: "link"; color: root.theme.foreground }
                onTextEdited: root.linkText = text
                onAccepted: root.addLink()
            }
            RedesignButton {
                objectName: "trackAssignmentAddLink"
                visible: root.linkMode
                Layout.preferredWidth: root.theme.dp(60); Layout.preferredHeight: root.theme.dp(34)
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                caption: qsTr("Add"); text: caption; enabled: !root.saving && !root.loadingDrop && root.linkText.trim().length > 0
                onClicked: root.addLink()
            }

        }
    }
    contentItem: Item {
        DropArea {
            id: fileDrop
            objectName: "trackAssignmentDropArea"
            anchors.fill: parent
            enabled: root.visible && !root.saving && !root.loading && !root.loadingDrop
            onEntered: drag => drag.accepted = drag.hasUrls || (root.subtitles && drag.hasText)
            onDropped: drop => {
                drop.accepted = false
                if (root.dropSources(root.dropUrls(drop))) drop.accept(Qt.CopyAction)
            }
        }
        Rectangle {
            anchors.fill: parent; anchors.margins: root.theme.dp(12); z: 4
            visible: fileDrop.containsDrag
            color: Qt.alpha(root.theme.field, root.theme.floatingOpacity); radius: root.theme.dp(8)
            border.color: root.theme.accent; border.width: root.theme.dp(2)
            Text {
                anchors.centerIn: parent; width: parent.width - root.theme.dp(40); wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                text: root.subtitles ? qsTr("Drop subtitle files, folders or links here") : qsTr("Drop audio files or folders here")
                color: root.theme.foreground; font.pixelSize: root.theme.dp(18)
            }
        }
        C.ScrollView {
            visible: !addonPicker.usingReplacement
            enabled: !root.addonWanted
            anchors.fill: parent; anchors.topMargin: root.theme.dp(22); anchors.leftMargin: root.theme.dp(20); anchors.rightMargin: root.theme.dp(20)
            clip: true; contentWidth: availableWidth
            C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
            ListView {
                id: list
                objectName: "trackAssignmentList"
                footer: RedesignAddonSections {
                    width: list.width; height: visible ? implicitHeight : 0
                    visible: root.onlineMode
                    hook: "subtitles.results.sections@1"
                    hostContext: ({surface: "subtitles.results.sections@1", search: root.searchTitle})
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                }
                model: root.loading ? [] : root.fileMode ? root.visibleSources : root.rows
                spacing: root.theme.dp(4)
                delegate: C.ItemDelegate {
                    id: entry
                    required property var modelData
                    required property int index
                    objectName: "trackAssignmentRow_" + index
                    readonly property real connectorNameWidth: {
                        fileNameMetrics.font
                        return Math.min(root.fileNameColumnWidth, Math.round(fileNameMetrics.advanceWidth(String(modelData.name))))
                    }
                    width: ListView.view.width; height: root.theme.dp(36)
                    enabled: !root.saving; padding: 0
                    leftPadding: root.theme.dp(root.fileMode ? 8 : 10); rightPadding: leftPadding
                    highlighted: root.fileMode && root.tracks(root.anchorFile).includes(modelData.url)
                    Accessible.name: modelData.name
                    background: Rectangle { radius: root.theme.dp(6); color: entry.highlighted ? root.theme.field : entry.hovered ? root.theme.hover : "transparent"; border.color: entry.visualFocus || rowDrop.containsDrag ? root.theme.accent : "transparent" }
                    DropArea {
                        id: rowDrop
                        objectName: "trackAssignmentRowDrop_" + entry.index
                        anchors.fill: parent
                        enabled: !root.fileMode && !root.saving && !root.loadingDrop
                        onEntered: drag => drag.accepted = drag.hasUrls || (root.subtitles && drag.hasText)
                        onDropped: drop => {
                            drop.accepted = false
                            if (root.dropSources(root.dropUrls(drop), entry.modelData.url)) drop.accept(Qt.CopyAction)
                        }
                    }
                    onClicked: root.fileMode ? root.toggle(root.anchorFile, modelData.url) : root.chooseRow(modelData.url)
                    contentItem: RowLayout {
                        spacing: root.theme.dp(root.fileMode ? 14 : 18)
                        C.CheckBox {
                            visible: !root.fileMode
                            Layout.preferredWidth: root.theme.dp(20); Layout.preferredHeight: root.theme.dp(24)
                            Layout.topMargin: root.theme.dp(4)
                            checked: !!root.selectedRows[entry.modelData.url]
                            Accessible.name: qsTr("Select %1").arg(entry.modelData.name)
                            onToggled: root.selectedRows = Object.assign({}, root.selectedRows, {[entry.modelData.url]: checked})
                            indicator: Rectangle { x: root.theme.dp(3); y: (parent.height - height) / 2; width: root.theme.dp(18); height: root.theme.dp(16); radius: root.theme.dp(3); color: "transparent"; border.width: root.theme.dp(1.5); border.pixelAligned: false; border.color: parent.visualFocus ? root.theme.accent : root.theme.playerLabel
                                RedesignIcon { sourceSize: Qt.size(root.theme.dp(40), root.theme.dp(40)); anchors.fill: parent; anchors.margins: root.theme.dp(1); name: "check"; color: root.theme.accent; visible: parent.parent.checked }
                            }
                            contentItem: Item {}
                        }
                        RedesignIcon { visible: root.fileMode; Layout.preferredWidth: root.theme.dp(24); Layout.preferredHeight: root.theme.dp(24); name: root.subtitles ? "subtitle-file" : "audio-file"; color: root.theme.playerLabel }
                        Text {
                            id: fileLabel
                            objectName: "trackAssignmentName_" + entry.index
                            Layout.fillWidth: root.fileMode; Layout.preferredWidth: root.fileMode ? -1 : root.fileNameColumnWidth
                            Layout.minimumWidth: 0; Layout.topMargin: root.fileMode ? 0 : root.theme.dp(4)
                            text: entry.modelData.name; textFormat: Text.PlainText; font.pixelSize: root.theme.dp(14); color: root.theme.playerHeading; elide: Text.ElideMiddle
                        }
                        Text {
                            objectName: "trackAssignmentMatchReason_" + entry.index
                            visible: root.onlineMode && root.fileMode && !!entry.modelData.matchReason
                            Layout.preferredWidth: Math.min(root.theme.dp(320), root.width * 0.36)
                            Layout.minimumWidth: 0
                            text: root.sourceDescription(entry.modelData); textFormat: Text.PlainText; elide: Text.ElideRight
                            color: root.theme.playerLabel; font.pixelSize: root.theme.dp(12)
                            Accessible.name: text
                        }
                        Item {
                            objectName: "trackAssignmentConnector_" + entry.index
                            visible: !root.fileMode
                            Layout.preferredWidth: root.theme.dp(436) - root.fileNameColumnWidth
                            Layout.maximumWidth: Layout.preferredWidth
                            Layout.minimumWidth: 0; Layout.preferredHeight: root.theme.dp(1)
                            Layout.leftMargin: -root.theme.dp(3); Layout.topMargin: root.theme.dp(4)
                            // Use the unused name-column space without moving the track column.
                            RedesignSeparator {
                                objectName: "trackAssignmentConnectorLine_" + entry.index
                                x: Math.min(0, entry.connectorNameWidth - Math.ceil(fileLabel.width))
                                width: Math.max(0, Math.floor(parent.width) - x)
                                scaleFactor: root.scaleFactor
                                color: root.lightTheme ? root.theme.border : "#373737"
                                dashArray: "8 8"
                            }
                        }
                        RedesignButton {
                            visible: !root.fileMode
                            objectName: "trackAssignmentChoose_" + entry.index
                            Layout.fillWidth: true; Layout.preferredWidth: root.theme.dp(460); Layout.preferredHeight: root.theme.dp(32)
                            Layout.minimumWidth: 0; Layout.leftMargin: root.theme.dp(10)
                            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                            iconName: root.subtitles ? "subtitle-file" : "audio-file"; leftAligned: true
                            text: qsTr("Select tracks for %1").arg(entry.modelData.name)
                            tooltipTextFormat: Text.PlainText
                            contentItem: RowLayout {
                                spacing: root.theme.dp(14)
                                RedesignIcon { Layout.preferredWidth: root.theme.dp(24); Layout.preferredHeight: root.theme.dp(24); name: root.subtitles ? "subtitle-file" : "audio-file"; color: root.theme.playerLabel; Layout.leftMargin: -root.theme.dp(1) }
                                Text { objectName: "trackAssignmentSelection_" + entry.index; Layout.fillWidth: true; text: root.tracks(entry.modelData.url).length ? root.tracks(entry.modelData.url).map(url => root.fileName(url)).join(", ") : qsTr("Select tracks"); textFormat: Text.PlainText; elide: Text.ElideMiddle; color: root.theme.playerHeading; font.pixelSize: root.theme.dp(14) }
                            }
                            onClicked: root.chooseRow(entry.modelData.url)
                        }
                        RedesignIcon { visible: entry.highlighted; Layout.preferredWidth: root.theme.dp(24); Layout.preferredHeight: root.theme.dp(24); name: "check-active"; color: root.theme.accent }
                    }
                }
            }
        }
        RedesignAddonSelection {
            id: addonPicker
            objectName: "addonSubtitleSelection"
            anchors.fill: parent; anchors.margins: root.theme.dp(20)
            registry: root.addonRegistry; hook: "subtitles.search.content@1"; kind: "subtitles"
            choiceSet: "subtitles:" + root.requestId + ":" + root.searchRequestId + ":" + root.sourceIndex + ":" + root.scopeIndex
            targets: root.addonTargets; assets: root.addonAssets
            active: root.visible && root.addonWanted && !root.loading && !root.loadingSources && !root.loadingDrop && !root.matching && !root.searching
            visible: resolved; busy: root.saving; showActions: false; showRecovery: false
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            onConfirmed: assignments => root.applyAddonSelection(assignments)
            onFallbackRequested: root.addonFallback = true
            footerContent: Component {
                RedesignAddonSections {
                    registry: root.addonRegistry; hook: "subtitles.results.sections@1"
                    hostContext: ({surface: "subtitles.results.sections@1", search: root.searchTitle})
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                }
            }
        }
        Text {
            anchors.centerIn: parent; width: parent.width - root.theme.dp(40); horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
            visible: !addonPicker.usingReplacement && !(root.onlineMode && !root.providerSearchReady) && (root.loading || root.loadingSources || root.loadingDrop || root.matching || root.searching || (!list.count && !root.errorMessage))
            text: root.searching ? root.searchProgressText || qsTr("Searching subtitles…") : root.matching ? qsTr("Matching tracks…") : root.loading || root.loadingSources || root.loadingDrop ? qsTr("Loading files…") : root.onlineMode ? qsTr("No subtitles found. Try another title or language.") : root.linkMode ? qsTr("Paste a direct subtitle file link. Then select Add.") : root.fileMode ? qsTr("Select a folder or select track files from your computer.") : qsTr("There are no media files here.")
            color: root.theme.foreground; font.pixelSize: root.theme.dp(14)
        }        Column {
            anchors.centerIn: parent; width: Math.max(0, parent.width - root.theme.dp(60))
            spacing: root.theme.dp(16); visible: root.onlineMode && !root.providerSearchReady && !root.visibleSources.length
            Text {
                width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                objectName: "trackAssignmentProviderState"
                text: root.providerListError || providerDemand.errorMessage || (root.providerListLoading || !root.providerConfigurationKnown
                    ? qsTr("The app checks subtitle provider settings.")
                    : !root.providerListCurrent ? qsTr("The subtitle provider list is not ready. Try again.")
                    : qsTr("Set up a subtitle provider in extension settings, or add an OpenSubtitles key."))
                textFormat: Text.PlainText
                color: root.theme.foreground; font.pixelSize: root.theme.dp(14)
            }
            RedesignButton {
                objectName: "trackAssignmentSetup"
                anchors.horizontalCenter: parent.horizontalCenter
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                caption: qsTr("Set up OpenSubtitles"); text: caption; onClicked: providerSetup.open()
            }
            RedesignButton {
                objectName: "trackAssignmentProviderRetry"
                anchors.horizontalCenter: parent.horizontalCenter
                visible: providerDemand.displayMode && !root.providerListLoading
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                caption: qsTr("Try again"); text: caption
                onClicked: providerDemand.retry()
            }
        }

    }
    footer: Item {
        implicitHeight: Math.max(root.theme.dp(72), error.visible ? error.implicitHeight + root.theme.dp(60) : 0)
        Text {
            id: error
            objectName: "trackAssignmentError"
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: root.theme.dp(20); anchors.topMargin: root.theme.dp(4)
            visible: text.length > 0
            text: root.errorMessage || (root.scopeIndex === 1
                ? root.removeFolderRule ? qsTr("Hakone will remove the saved folder matches.")
                : !root.folderRuleAutomatic ? qsTr("Automatic matching is off in Settings. These choices apply to the current files.")
                : root.folderRuleDefinition.length || root.folderRuleNeedsPreparation ? qsTr("Hakone will use these matches for new files in this folder.")
                : qsTr("Hakone will save these choices for the current files.")
                : root.folderRuleChanged ? qsTr("Hakone will also save the folder matches.") : "")
            textFormat: Text.PlainText; wrapMode: Text.WordWrap
            font.pixelSize: root.theme.dp(14); color: root.errorMessage.length ? root.theme.danger : root.theme.foreground
            Accessible.role: root.errorMessage.length ? Accessible.AlertMessage : Accessible.StaticText
        }
        RedesignButton {
            objectName: "trackAssignmentCancel"
            x: root.theme.dp(20); anchors.bottom: parent.bottom; anchors.bottomMargin: root.theme.dp(20)
            implicitWidth: root.theme.dp(100); implicitHeight: root.theme.dp(32)
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            caption: qsTr("Cancel"); captionFontWeight: Font.DemiBold; captionVerticalOffset: 1; text: caption; enabled: !root.saving || root.cancelableSave; onClicked: root.reject()
            background: Rectangle { radius: root.theme.dp(6); color: parent.hovered ? root.theme.hover : root.theme.background; border.color: parent.visualFocus ? root.theme.accent : "transparent" }
        }
        RedesignButton {
            objectName: "trackAssignmentProviderSettings"
            x: root.theme.dp(132); anchors.bottom: parent.bottom; anchors.bottomMargin: root.theme.dp(20)
            visible: root.onlineMode && root.providerConfigured && root.width > root.theme.dp(600)
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            caption: qsTr("OpenSubtitles setup"); text: caption; enabled: !root.saving; onClicked: providerSetup.open()
        }
        RedesignButton {
            objectName: "trackAssignmentClearSelected"
            anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: root.theme.dp(20)
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            visible: !root.fileMode && Object.values(root.selectedRows).some(value => value)
            iconName: "remove"; caption: root.width >= root.theme.dp(640) ? qsTr("Clear selected tracks") : ""
            text: qsTr("Clear selected tracks"); enabled: !root.saving
            onClicked: { for (const url of Object.keys(root.selectedRows)) if (root.selectedRows[url]) root.setTracks(url, []) }
        }
        RedesignButton {
            objectName: "trackAssignmentRemoveFolderRule"
            anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: root.theme.dp(20)
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            visible: root.scopeIndex === 1 && (root.folderRuleBaseline.length > 0 || root.removeFolderRule)
                && !Object.values(root.selectedRows).some(value => value)
            iconName: root.removeFolderRule ? "refresh" : "remove"
            caption: root.width >= root.theme.dp(640) ? text : ""
            text: root.removeFolderRule ? qsTr("Keep folder matches") : qsTr("Remove folder matches")
            enabled: !root.saving; onClicked: { root.removeFolderRule = !root.removeFolderRule; root.folderRuleTouched = true }
        }
        RedesignButton {
            objectName: "trackAssignmentSave"
            anchors.right: parent.right; anchors.rightMargin: root.theme.dp(20); anchors.bottom: parent.bottom; anchors.bottomMargin: root.theme.dp(20)
            implicitWidth: root.theme.dp(100); implicitHeight: root.theme.dp(32)
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            caption: root.saving ? qsTr("Saving…") : qsTr("Save"); text: caption
            enabled: !root.saving && !root.loading && !root.loadingSources && !root.loadingDrop && !root.matching
                && (root.addonWanted ? addonPicker.prepared && addonPicker.usingReplacement && (root.addonSelectionChanged || root.folderRuleChanged) : root.changedRows.length > 0 || root.folderRuleChanged)
            primaryAction: true; captionFontWeight: Font.DemiBold; captionVerticalOffset: 1
            onClicked: root.submit()
        }
    }
    component DetailField: C.TextField {
        Layout.fillWidth: true; implicitHeight: root.theme.dp(34)
        color: root.theme.foreground; selectByMouse: true
        font.pixelSize: root.theme.dp(14)
        background: Rectangle { radius: root.theme.dp(6); color: root.theme.field; border.color: parent.visualFocus ? root.theme.accent : root.theme.border }
    }
    C.Dialog {
        id: searchEditor
        objectName: "trackAssignmentSearchDetails"
        popupType: C.Popup.Item; parent: root.parent; modal: true; dim: true; focus: true
        x: parent ? (parent.width - width) / 2 : 0; y: parent ? (parent.height - height) / 2 : 0
        width: Math.min(root.theme.dp(560), root.width - root.theme.dp(32)); padding: root.theme.dp(20)
        closePolicy: C.Popup.CloseOnEscape
        C.Overlay.modal: Rectangle { color: "#80000000" }
        background: RedesignDialogSurface { theme: root.theme; videoBackdropContent: root.nestedBackdropContent }
        height: Math.min(implicitHeight, parent ? parent.height - root.theme.dp(32) : implicitHeight)
        contentItem: C.ScrollView {
            id: searchEditorScroll
            implicitHeight: searchEditorFields.implicitHeight
            contentWidth: availableWidth; clip: true
            ColumnLayout {
                id: searchEditorFields
                width: searchEditorScroll.availableWidth
                spacing: root.theme.dp(10)
                Text { text: qsTr("Subtitle search details"); color: root.theme.foreground; font.pixelSize: root.theme.dp(18) }
                Text { text: qsTr("Correct the detected movie or episode for this search."); color: root.theme.playerLabel; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Text { text: qsTr("Title"); color: root.theme.foreground }
                DetailField { id: detailTitle; objectName: "subtitleSearchDetailTitle"; Layout.fillWidth: true; Accessible.name: qsTr("Title"); maximumLength: 512 }
                HeaderComboBox { id: detailType; objectName: "subtitleSearchDetailType"; Layout.fillWidth: true; model: [qsTr("Automatic"), qsTr("Movie"), qsTr("Episode")]; Accessible.name: qsTr("Media type")
                    onActivated: index => { if (index === 1) { detailSeason.text = ""; detailEpisodes.text = ""; detailAbsolute.text = ""; detailAirDate.text = "" } }
                }
                Text { text: detailType.currentIndex === 2 ? qsTr("Series start year (optional)") : qsTr("Release year (optional)"); color: root.theme.foreground }
                DetailField { id: detailYear; objectName: "subtitleSearchDetailYear"; Layout.fillWidth: true; Accessible.name: qsTr("Year"); validator: IntValidator { bottom: 1880; top: 2099 } }
                Text { text: qsTr("Season (optional)"); color: root.theme.foreground }
                DetailField { id: detailSeason; objectName: "subtitleSearchDetailSeason"; Layout.fillWidth: true; Accessible.name: qsTr("Season"); validator: IntValidator { bottom: 0; top: 999 } }
                Text { text: qsTr("Episodes, separated by commas (optional)"); color: root.theme.foreground }
                DetailField { id: detailEpisodes; objectName: "subtitleSearchDetailEpisodes"; Layout.fillWidth: true; Accessible.name: qsTr("Episodes"); maximumLength: 512; validator: RegularExpressionValidator { regularExpression: /\s*(?:\d{1,4}(?:\s*,\s*\d{1,4})*)?\s*/ } }
                Text { text: qsTr("Absolute episode (optional)"); color: root.theme.foreground }
                DetailField { id: detailAbsolute; objectName: "subtitleSearchDetailAbsolute"; Layout.fillWidth: true; Accessible.name: qsTr("Absolute episode"); validator: RegularExpressionValidator { regularExpression: /(?:\d{1,4}(?:\.\d{1,2})?)?/ } }
                Text { text: qsTr("Air date, YYYY-MM-DD (optional)"); color: root.theme.foreground }
                DetailField { id: detailAirDate; objectName: "subtitleSearchDetailAirDate"; Layout.fillWidth: true; Accessible.name: qsTr("Air date"); validator: RegularExpressionValidator { regularExpression: /(?:\d{4}-\d{2}-\d{2})?/ } }
            }
        }
        footer: Item {
            implicitHeight: root.theme.dp(64)
            RowLayout {
                anchors.fill: parent; anchors.margins: root.theme.dp(20); anchors.topMargin: root.theme.dp(10)
                RedesignButton {
                    objectName: "subtitleSearchDetailReset"
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    caption: qsTr("Use detected details"); text: caption
                    onClicked: { searchEditor.close(); root.resetSearchDetails() }
                }
                Item { Layout.fillWidth: true }
                RedesignButton {
                    objectName: "subtitleSearchDetailCancel"
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    caption: qsTr("Cancel"); text: caption; onClicked: searchEditor.close()
                }
                RedesignButton {
                    objectName: "subtitleSearchDetailApply"
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    caption: qsTr("Search"); text: caption; primaryAction: true
                    enabled: detailTitle.text.trim().length > 0 && (!detailYear.text.trim() || detailYear.acceptableInput) && (!detailSeason.text.trim() || detailSeason.acceptableInput) && detailEpisodes.acceptableInput && detailAbsolute.acceptableInput && detailAirDate.acceptableInput
                    onClicked: {
                        root.manualSearch = true
                        root.searchDetails = {year:detailYear.text.trim() ? Number(detailYear.text) : -1,
                            season:detailSeason.text.trim() ? Number(detailSeason.text) : -1,
                            episodes:detailEpisodes.text.trim() ? detailEpisodes.text.split(",").map(value => Number(value.trim())) : [],
                            absoluteEpisode:detailAbsolute.text, airDate:detailAirDate.text,
                            type:detailType.currentIndex === 1 ? "movie" : detailType.currentIndex === 2 ? "episode" : "unknown"}
                        root.searchTitle = root.identityQuery(Object.assign({title:detailTitle.text.trim()}, root.searchDetails))
                        searchEditor.close(); root.requestSearch()
                    }
                }
            }
        }
    }
    RedesignSubtitleProviderSetup {
        id: providerSetup
        objectName: "trackAssignmentProviderSetup"
        parent: root.parent
        videoBackdropContent: root.nestedBackdropContent
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        hasCustomKey: root.providerHasCustomKey; hasConsumerKey: root.providerHasConsumerKey
        busy: root.saving
        onSaveRequested: key => root.setupRequested(key)
        onRemoveRequested: root.removeSetupRequested()
        onHelpRequested: root.apiKeyHelpRequested()
    }
    C.Popup {
        id: picker
        property string targetUrl: ""
        readonly property bool stackedActions: availableWidth < root.theme.dp(340)
        objectName: "trackAssignmentPicker"
        popupType: C.Popup.Item
        parent: root.parent
        x: parent ? Math.round((parent.width - width) * root.pixelRatio / 2) / root.pixelRatio : 0
        y: parent ? Math.round((parent.height - height) * root.pixelRatio / 2) / root.pixelRatio : 0
        width: Math.max(0, Math.min(root.theme.dp(640), root.width - root.theme.dp(40)))
        height: Math.max(0, Math.min(root.theme.dp(460), parent ? parent.height - root.theme.dp(32) : root.theme.dp(460)))
        padding: root.theme.dp(20)
        modal: true; dim: true; focus: true
        closePolicy: C.Popup.CloseOnEscape | C.Popup.CloseOnPressOutside
        C.Overlay.modal: Rectangle { color: "#80000000" }
        background: RedesignDialogSurface {
            theme: root.theme
            videoBackdropContent: root.nestedBackdropContent
        }
        onOpened: { pickerList.currentIndex = pickerList.count ? 0 : -1; pickerList.forceActiveFocus() }
        contentItem: ColumnLayout {
            Accessible.role: Accessible.Dialog
            Accessible.name: root.fileName(picker.targetUrl)
            spacing: root.theme.dp(16)
            RowLayout {
                Layout.fillWidth: true
                spacing: root.theme.dp(12)
                Text {
                    objectName: "trackAssignmentPickerTitle"
                    Layout.fillWidth: true; Layout.minimumWidth: 0
                    text: root.fileName(picker.targetUrl); textFormat: Text.PlainText; elide: Text.ElideMiddle
                    color: root.theme.bright; font.pixelSize: root.theme.dp(20)
                }
                RedesignButton {
                    objectName: "trackAssignmentPickerClose"
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    iconName: "close"; text: qsTr("Close")
                    onClicked: picker.close()
                }
            }
            ListView {
                id: pickerList
                objectName: "trackAssignmentPickerList"
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 0
                clip: true; spacing: root.theme.dp(4)
                model: root.pickerSources()
                activeFocusOnTab: true
                DropArea {
                    objectName: "trackAssignmentPickerDropArea"
                    anchors.fill: parent
                    enabled: picker.visible && !root.saving && !root.loadingDrop
                    onEntered: drag => drag.accepted = drag.hasUrls || (root.subtitles && drag.hasText)
                    onDropped: drop => {
                        drop.accepted = false
                        if (root.dropSources(root.dropUrls(drop), picker.targetUrl)) drop.accept(Qt.CopyAction)
                    }
                }
                C.ScrollBar.vertical: C.ScrollBar {}
                delegate: C.ItemDelegate {
                    id: pickerEntry
                    required property var modelData
                    required property int index
                    objectName: "trackAssignmentPickerRow_" + index
                    width: ListView.view.width; height: root.theme.dp(36)
                    leftPadding: root.theme.dp(8); rightPadding: root.theme.dp(10)
                    text: modelData.name; highlighted: root.tracks(picker.targetUrl).includes(modelData.url)
                    Accessible.role: Accessible.CheckBox
                    Accessible.name: text
                    Accessible.checkable: true
                    Accessible.checked: highlighted
                    Accessible.onToggleAction: root.toggle(picker.targetUrl, modelData.url)
                    onClicked: root.toggle(picker.targetUrl, modelData.url)
                    Keys.onReturnPressed: clicked()
                    Keys.onEnterPressed: clicked()
                    background: Rectangle {
                        radius: root.theme.dp(6)
                        color: pickerEntry.highlighted ? root.theme.field : pickerEntry.hovered ? root.theme.hover : "transparent"
                        border.color: pickerEntry.visualFocus ? root.theme.accent : "transparent"
                    }
                    contentItem: RowLayout {
                        spacing: root.theme.dp(14)
                        RedesignIcon {
                            Layout.preferredWidth: root.theme.dp(24); Layout.preferredHeight: root.theme.dp(24)
                            name: root.subtitles ? "subtitle-file" : "audio-file"; color: root.theme.playerLabel
                        }
                        Text {
                            objectName: "trackAssignmentPickerName"
                            Layout.fillWidth: true; Layout.minimumWidth: 0
                            text: pickerEntry.text; textFormat: Text.PlainText; elide: Text.ElideMiddle
                            color: root.theme.playerHeading; font.pixelSize: root.theme.dp(14)
                        }
                        Text {
                            visible: root.onlineMode && !!pickerEntry.modelData.matchReason
                            Layout.preferredWidth: Math.min(root.theme.dp(240), picker.width * 0.4); Layout.minimumWidth: 0
                            text: root.sourceDescription(pickerEntry.modelData); textFormat: Text.PlainText; elide: Text.ElideRight
                            color: root.theme.playerLabel; font.pixelSize: root.theme.dp(12)
                        }
                        RedesignIcon {
                            Layout.preferredWidth: root.theme.dp(20); Layout.preferredHeight: root.theme.dp(20)
                            name: "check"; color: root.theme.accent; visible: pickerEntry.highlighted
                        }
                    }
                }
            }
            RedesignSeparator { Layout.fillWidth: true; scaleFactor: root.scaleFactor; color: root.theme.border }
            GridLayout {
                Layout.fillWidth: true
                columns: picker.stackedActions ? 1 : 4
                columnSpacing: root.theme.dp(8); rowSpacing: root.theme.dp(8)
                RedesignButton {
                    objectName: "trackAssignmentPickerFiles"
                    Layout.fillWidth: picker.stackedActions; Layout.preferredHeight: root.theme.dp(32)
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    caption: qsTr("Select files…"); captionFontWeight: Font.DemiBold; text: caption
                    filled: true; onClicked: root.filesRequested(picker.targetUrl)
                }
                RedesignButton {
                    objectName: "trackAssignmentPickerClear"
                    Layout.fillWidth: picker.stackedActions; Layout.preferredWidth: root.theme.dp(100); Layout.preferredHeight: root.theme.dp(32)
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    caption: qsTr("Clear"); captionFontWeight: Font.DemiBold; text: caption
                    filled: true; onClicked: root.setTracks(picker.targetUrl, [])
                }
                Item { Layout.fillWidth: true; visible: !picker.stackedActions }
                RedesignButton {
                    objectName: "trackAssignmentPickerDone"
                    Layout.fillWidth: picker.stackedActions; Layout.preferredWidth: root.theme.dp(100); Layout.preferredHeight: root.theme.dp(32)
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    caption: qsTr("Done"); captionFontWeight: Font.DemiBold; text: caption
                    primaryAction: true; onClicked: picker.close()
                }
            }
        }
    }
}

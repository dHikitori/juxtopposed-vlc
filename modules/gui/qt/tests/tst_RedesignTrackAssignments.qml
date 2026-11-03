// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "JuxtopposedTrackAssignments"
    width: 1280; height: 900; visible: true; when: windowShown
    // Scaled software grabs can contain physical pixels with DPR metadata set to 1.
    Item {
        id: pixelCaptureBounds
        width: testCase.width * Screen.devicePixelRatio
        height: testCase.height * Screen.devicePixelRatio
        visible: false
    }
    readonly property string first: "file:///show/Show_S01E01.mkv"
    readonly property string second: "file:///show/Show_S01E02.mkv"
    readonly property string english: "file:///show/Show_S01E01.en.srt"
    readonly property string spanish: "file:///show/Show_S01E02.es.srt"
    property bool finishMatching: true
    RedesignTrackAssignmentDialog {
        id: dialog; parent: testCase
        onMatchRequested: (media, tracks, includeSaved, request) => {
            if (testCase.finishMatching) dialog.loadMatches(request, {}, includeSaved)
        }
    }
    SignalSpy { id: saveSpy; target: dialog; signalName: "saveRequested" }
    SignalSpy { id: dropSpy; target: dialog; signalName: "droppedSourcesRequested" }
    SignalSpy { id: rowSpy; target: dialog; signalName: "rowsRequested" }
    SignalSpy { id: searchSpy; target: dialog; signalName: "searchRequested" }
    SignalSpy { id: linkSpy; target: dialog; signalName: "linksRequested" }
    SignalSpy { id: folderSpy; target: dialog; signalName: "folderRequested" }
    Component { id: displaySpy; SignalSpy {} }
    Component {
        id: displayOwner
        QtObject {
            property bool catalogueReady: true
            property bool catalogueLoading: false
            property int catalogueRevision: 1
            property bool ownerCurrent: true
            property int modeReads: 0
            property var observations: ({nativeChecks:0})
            property int preparations: 0
            property int releases: 0
            property var tickets: []
            signal catalogueChanged()
            signal catalogueFreshnessChanged()
            signal capabilitiesChanged()
            signal retiring()
            function displayCatalogueAttached() { modeReads++; return true }
            function prepareDisplayFeature(feature) { const id=String(++preparations); tickets=tickets.concat([id]); return id }
            function releaseDisplayRequest(id) { releases++; tickets=tickets.filter(value=>value!==id) }
            function displayRequestCurrent(id) { return ownerCurrent && catalogueReady && tickets.includes(id) }
            function refreshDisplayCatalogue() { catalogueFreshnessChanged() }
        }
    }
    Component {
        id: displayDialog
        RedesignTrackAssignmentDialog {
            id: candidateDialog
            onMatchRequested: (media,tracks,includeSaved,request) => candidateDialog.loadMatches(request,{},includeSaved)
        }
    }
    TextMetrics { id: literalMetrics }
    function rows() { return [{url:first,name:"Show_S01E01.mkv",baseline:[]},{url:second,name:"Show_S01E02.mkv",baseline:[]}] }
    function load(items) {
        dialog.loadRows(dialog.requestId,{success:true,rows:items})
        dialog.loadSources(dialog.sourceRequestId,{success:true,sources:[{url:english,name:"English.srt"},{url:spanish,name:"Spanish.srt"}]})
    }
    function init() {
        dialog.sourceFolderDisplay = Qt.binding(() => dialog.sourceFolder)
        dialog.sourceFolderRoots = []
        finishMatching=true
        width=1280; height=900; dialog.scaleFactor=1; dialog.lightTheme=false
        dialog.saving=false; dialog.close(); tryCompare(dialog,"visible",false)
        saveSpy.clear(); rowSpy.clear(); dropSpy.clear(); searchSpy.clear(); linkSpy.clear(); folderSpy.clear(); dialog.providerConfigured=false; dialog.folderRuleDefinition=""
        dialog.preferredSearchLanguages=[]
        dialog.onlineLanguages=[{code:"en",text:"English"},{code:"es",text:"Spanish"},{code:"ja",text:"Japanese"},{code:"nl",text:"Dutch"}]
        verify(dialog.begin({scope:"file",url:first,urls:[first,second]},true))
        tryCompare(dialog,"opened",true); load([rows()[0]])
    }
    function cleanup() { dialog.saving=false; dialog.close(); tryCompare(dialog,"visible",false) }
    function test_configuredBuiltinWaitsForFixedModeAndUsesNativeAuthorityWithoutATicket() {
        const owner=createTemporaryObject(displayOwner,testCase)
        const pane=createTemporaryObject(displayDialog,testCase,{parent:testCase,providerDisplayBackend:owner,
            providerDisplayCandidates:[{id:"org.hakone.builtin/subtitles",builtin:true,enabled:true,configured:true,configurationKnown:true}],
            providerConfigured:true,providerActionGuard:()=>{owner.observations.nativeChecks++;return owner.ownerCurrent}})
        const searches=createTemporaryObject(displaySpy,testCase,{target:pane,signalName:"searchRequested"})
        verify(pane.begin({scope:"file",url:first},true))
        pane.loadRows(pane.requestId,{success:true,rows:[rows()[0]]}); pane.selectSource(1)
        compare(searches.count,0); compare(owner.observations.nativeChecks,0); compare(owner.preparations,0)
        tryCompare(searches,"count",1); compare(owner.modeReads,1); compare(owner.preparations,0)
        owner.ownerCurrent=false; pane.requestSearch(); compare(searches.count,1)
        owner.ownerCurrent=true; owner.catalogueReady=false; pane.requestSearch(); compare(searches.count,1)
        owner.catalogueReady=true; pane.requestSearch(); compare(searches.count,2)
        pane.reject(); tryCompare(pane,"visible",false); compare(owner.releases,0)
    }
    function test_builtinExceptionCannotAdmitAnUnavailableOrDisabledProvider_data() {
        return [
            {tag:"native-refusal",row:{builtin:true,enabled:true,available:true,configured:true,configurationKnown:true},nativeCurrent:false},
            {tag:"unavailable-builtin",row:{builtin:true,enabled:true,available:false,configured:true,configurationKnown:true},nativeCurrent:true},
            {tag:"disabled-builtin",row:{builtin:true,enabled:false,available:true,configured:true,configurationKnown:true},nativeCurrent:true},
            {tag:"unconfigured-builtin",row:{builtin:true,enabled:true,available:true,configured:false,configurationKnown:true},nativeCurrent:true},
            {tag:"all-disabled-portable",row:{builtin:false,enabled:false,available:true,configured:true,configurationKnown:true},nativeCurrent:true}
        ]
    }
    function test_builtinExceptionCannotAdmitAnUnavailableOrDisabledProvider(data) {
        const owner=createTemporaryObject(displayOwner,testCase,{ownerCurrent:data.nativeCurrent})
        const pane=createTemporaryObject(displayDialog,testCase,{parent:testCase,providerDisplayBackend:owner,
            providerDisplayCandidates:[Object.assign({id:"org.example.subtitles/search"},data.row)],
            providerConfigured:true,providerActionGuard:()=>owner.ownerCurrent})
        const searches=createTemporaryObject(displaySpy,testCase,{target:pane,signalName:"searchRequested"})
        verify(pane.begin({scope:"file",url:first},true))
        pane.loadRows(pane.requestId,{success:true,rows:[rows()[0]]}); pane.selectSource(1)
        tryCompare(owner,"modeReads",1); wait(10)
        compare(searches.count,0); compare(owner.preparations,0); verify(!pane.providerCurrentForAction())
        pane.reject(); tryCompare(pane,"visible",false); compare(owner.releases,0)
    }
    function test_visibleOnlineModeOwnsDemandAndWaitsForUnknownSettings() {
        const owner=createTemporaryObject(displayOwner,testCase)
        const pane=createTemporaryObject(displayDialog,testCase,{parent:testCase,providerDisplayBackend:owner,
            providerDisplayCandidates:[{id:"org.example.subtitles/search",enabled:true,configured:false,configurationKnown:false}],
            providerConfigurationKnown:false,providerActionGuard:()=>owner.ownerCurrent})
        const searches=createTemporaryObject(displaySpy,testCase,{target:pane,signalName:"searchRequested"})
        const languages=createTemporaryObject(displaySpy,testCase,{target:pane,signalName:"languagesRequested"})
        compare(owner.preparations,0); verify(pane.begin({scope:"file",url:first},true)); tryCompare(pane,"opened",true)
        pane.loadRows(pane.requestId,{success:true,rows:[rows()[0]]}); compare(owner.preparations,0)
        pane.selectSource(1); compare(searches.count,0); compare(languages.count,0)
        tryCompare(owner,"preparations",1)
        verify(findChild(pane,"trackAssignmentProviderState").text.includes("checks"))
        const title=pane.searchTitle; pane.providerConfigured=true; pane.providerConfigurationKnown=true
        compare(searches.count,1); compare(languages.count,1); compare(pane.searchTitle,title)
        owner.capabilitiesChanged(); owner.catalogueRevision++; compare(owner.preparations,1)
        pane.selectSource(0); tryCompare(owner,"releases",1); compare(owner.tickets.length,0)
        pane.reject(); tryCompare(pane,"visible",false)
    }
    function test_staleProviderDisplayPreservesResultsAndTrackDrafts() {
        const owner=createTemporaryObject(displayOwner,testCase)
        const pane=createTemporaryObject(displayDialog,testCase,{parent:testCase,providerDisplayBackend:owner,
            providerDisplayCandidates:[{id:"org.example.subtitles/search",enabled:true,configured:true,configurationKnown:true}],
            providerConfigured:true,providerActionGuard:()=>owner.ownerCurrent})
        const searches=createTemporaryObject(displaySpy,testCase,{target:pane,signalName:"searchRequested"})
        verify(pane.begin({scope:"file",url:first},true)); tryCompare(pane,"opened",true)
        pane.loadRows(pane.requestId,{success:true,rows:[rows()[0]]}); pane.selectSource(1)
        compare(searches.count,0); tryCompare(owner,"preparations",1); tryCompare(searches,"count",1)
        const remote="addon-subtitle:/opaque/English.srt"
        pane.loadSearch(pane.searchRequestId,{success:true,sources:[{url:remote,name:"English"}]}); pane.toggle(first,remote)
        pane.providerListCurrent=false; pane.providerListLoading=true; pane.requestSearch()
        compare(searches.count,1); compare(pane.onlineSources.length,1); verify(pane.tracks(first).includes(remote))
        owner.ownerCurrent=false; pane.providerListCurrent=true; pane.providerListLoading=false
        compare(searches.count,1); owner.ownerCurrent=true; pane.requestSearch(); compare(searches.count,2)
        pane.loadSearch(pane.searchRequestId,{success:false,error:"Provider changed"})
        compare(pane.onlineSources.length,1); verify(pane.tracks(first).includes(remote)); compare(owner.preparations,1)
        pane.reject(); tryCompare(pane,"visible",false)
    }
    function test_fifteenEpisodeRowsFitWithFolderInformation_data() {
        return [{tag:"normal",scale:1},{tag:"fractional",scale:1.125}]
    }
    function test_fifteenEpisodeRowsFitWithFolderInformation(data) {
        width=1920*data.scale; height=1080*data.scale; dialog.scaleFactor=data.scale
        dialog.selectScope(1)
        const episodes=Array.from({length:15},(_,index)=>{
            const name="The_Wand_S01E"+String(index+1).padStart(2,"0")+".mp4"
            return {url:"file:///show/"+name,name:name,baseline:[]}
        })
        load(episodes); dialog.folderRuleDefinition="fixture-rule"
        const list=findChild(dialog,"trackAssignmentList")
        tryVerify(()=>!!list.itemAtIndex(14))
        verify(findChild(dialog,"trackAssignmentError").visible)
        compare(list.contentY,0)
        const last=list.itemAtIndex(14)
        tryVerify(()=>last.mapToItem(list,0,last.height).y<=list.height)
        for(let index=0;index<15;++index) {
            const label=findChild(list.itemAtIndex(index),"trackAssignmentName_"+index)
            tryVerify(()=>!label.truncated)
        }
        const choose=findChild(list.itemAtIndex(0),"trackAssignmentChoose_0")
        // Wider Inter filenames use space from the dashed connector.
        fuzzyCompare(choose.mapToItem(dialog.background,0,0).x,547*data.scale,2)
        const message=findChild(dialog,"trackAssignmentError"), save=findChild(dialog,"trackAssignmentSave")
        verify(message.mapToItem(dialog.background,0,message.height).y<=save.mapToItem(dialog.background,0,0).y)
    }
    function test_referenceLayout_data() {
        const cases = []
        for (const mode of ["file", "folder", "online"])
            for (const style of [{tag: "dark", scale: 1}, {tag: "light", scale: 1, light: true}, {tag: "scaled", scale: 1.5}])
                cases.push({tag: mode + "-" + style.tag, mode: mode, scale: style.scale, light: style.light})
        return cases
    }
    function test_referenceLayout(data) {
        width=1800; height=1400; dialog.scaleFactor=data.scale; dialog.lightTheme=!!data.light
        if (data.mode === "folder") { dialog.selectScope(1); load(rows()) }
        else if (data.mode === "online") {
            dialog.providerConfigured=true; dialog.sourceIndex=1
            dialog.onlineSources=[{url: "opensubtitles:1", name: "English.srt"}]
        }
        wait(20)
        function geometry(item, x, y, w, h) {
            const point=item.mapToItem(dialog.background, 0, 0)
            fuzzyCompare(point.x, x * data.scale, .6, item.objectName)
            fuzzyCompare(point.y, y * data.scale, .6, item.objectName)
            fuzzyCompare(item.width, w * data.scale, .6, item.objectName)
            fuzzyCompare(item.height, h * data.scale, .6, item.objectName)
        }
        geometry(findChild(dialog,"trackAssignmentClose"),1005,7,36,36)
        geometry(findChild(dialog,"trackAssignmentScope"),9,53,200,34)
        geometry(findChild(dialog,"trackAssignmentFiles"),219,53,200,34)
        geometry(findChild(dialog,"trackAssignmentScope").indicator,177,58,24,24)
        geometry(findChild(dialog,"trackAssignmentFiles").indicator,387,58,24,24)
        if (data.mode === "online") {
            geometry(findChild(dialog,"trackAssignmentSearch"),429,53,400,34)
            geometry(findChild(dialog,"trackAssignmentLanguage"),839,53,200,34)
            geometry(findChild(dialog,"trackAssignmentLanguage").indicator,1007,58,24,24)
            geometry(findChild(dialog,"trackAssignmentSearchButton"),429,53,40,34)
        } else {
            geometry(findChild(dialog,"trackAssignmentFolder"),429,53,610,34)
            geometry(findChild(dialog,"trackAssignmentFolderIcon"),437,58,24,24)
        }
        geometry(findChild(dialog,"trackAssignmentCancel"),20,748,100,32)
        geometry(findChild(dialog,"trackAssignmentSave"),928,748,100,32)
        function pixel(item, x, y, color) {
            const point=item.mapToItem(testCase,x * data.scale,y * data.scale)
            const image=grabImage(pixelCaptureBounds), expected=Qt.color(color)
            x=Math.floor(point.x * image.width / testCase.width)
            y=Math.floor(point.y * image.height / testCase.height)
            compare(image.red(x,y),Math.round(expected.r*255))
            compare(image.green(x,y),Math.round(expected.g*255))
            compare(image.blue(x,y),Math.round(expected.b*255))
        }
        const scope=findChild(dialog,"trackAssignmentScope")
        pixel(scope.background,20,4,data.light ? "#c3c3c3" : "#151515")
        const list=findChild(dialog,"trackAssignmentList")
        tryVerify(() => !!list.itemAtIndex(0))
        geometry(list.itemAtIndex(0),20,118,1008,36)
        const label=findChild(list.itemAtIndex(0),"trackAssignmentName_0")
        const labelPoint=label.mapToItem(dialog.background,0,0)
        fuzzyCompare(labelPoint.x, (data.mode === "folder" ? 68 : 66) * data.scale, .1)
        if (data.mode === "folder") {
            geometry(findChild(list.itemAtIndex(0),"trackAssignmentConnector_0"),239,137.5,280,1)
            geometry(findChild(list.itemAtIndex(0),"trackAssignmentChoose_0"),547,120,471,32)
            geometry(list.itemAtIndex(1),20,158,1008,36)
        } else {
            dialog.setTracks(first,[dialog.visibleSources[0].url]); wait(10)
            pixel(list.itemAtIndex(0).background,80,3,data.light ? "#dedede" : "#090909")
            fuzzyCompare(label.mapToItem(dialog.background,0,0).x,66 * data.scale,.1)
        }
    }
    function test_folderCaptionUsesNativeDisplayWithoutChangingTheUrl_data() {
        return [
            {tag:"drive",url:"file:///C:/My%20Movies/Subtitles/",display:"C:\\My Movies\\Subtitles\\"},
            {tag:"unc",url:"file://NAS/Shows%20%26%20Films/",display:"\\\\NAS\\Shows & Films\\"},
            {tag:"unix",url:"file:///videos/%3Cb%3EFavorite%3C/b%3E/",display:"/videos/<b>Favorite</b>/"},
            {tag:"percent",url:"file:///videos/100%25/",display:"/videos/100%/"},
            {tag:"remote",url:"smb://viewer:hidden@example.test/Shows/",display:"smb://viewer@example.test/Shows/"}
        ]
    }
    function test_folderCaptionUsesNativeDisplayWithoutChangingTheUrl(data) {
        dialog.sourceFolder = data.url; dialog.sourceFolderDisplay = data.display
        const button=findChild(dialog,"trackAssignmentFolder"), caption=findChild(dialog,"trackAssignmentFolderCaption")
        compare(button.text,data.display); compare(caption.text,data.display)
        compare(caption.textFormat,Text.PlainText)
        const tooltip=findChild(button,"redesignToolTip")
        compare(tooltip.text,data.display); compare(tooltip.contentItem.textFormat,Text.PlainText)
        compare(dialog.sourceFolder,data.url)
        dialog.sourceFolder=""
        compare(button.text,"Select track folder"); compare(caption.text,"Select track folder")
    }
    function test_invalidEscapeDoesNotBreakFolderCaption() {
        dialog.sourceFolder="file:///videos/100%broken/"
        compare(findChild(dialog,"trackAssignmentFolderCaption").text,dialog.sourceFolder)
    }
    function test_libraryFolderCaptionKeepsTheAddressAndSelection() {
        const media="file://NAS/Shows/The%20Wand/S01/Episode.mkv", folder="file://NAS/Shows/The%20Wand/S01/"
        const address="\\\\NAS\\Shows\\The Wand\\S01\\"
        dialog.reject(); tryCompare(dialog,"visible",false)
        verify(dialog.begin({scope:"file",url:media,runtimeCategory:1},true)); tryCompare(dialog,"opened",true)
        compare(rowSpy.signalArguments[rowSpy.count-1][0].runtimeCategory,1)
        dialog.sourceFolderRoots=["file://nas/Shows"]
        dialog.loadRows(dialog.requestId,{success:true,rows:[{url:media,name:"Episode.mkv",baseline:[]}],folderCategory:1})
        dialog.loadSources(dialog.sourceRequestId,{success:true,sources:[]})
        dialog.sourceFolderDisplay=address
        const button=findChild(dialog,"trackAssignmentFolder"), caption=findChild(dialog,"trackAssignmentFolderCaption")
        compare(caption.text,"Videos / Shows / The Wand / S01")
        compare(button.text,address); compare(button.Accessible.name,address)
        compare(findChild(button,"redesignToolTip").text,address)
        compare(dialog.sourceFolder,folder)
        mouseClick(button); compare(folderSpy.count,1); compare(dialog.sourceFolder,folder)
        dialog.sourceFolder="file://NAS/Other/"
        dialog.sourceFolderDisplay="\\\\NAS\\Other\\"
        compare(caption.text,dialog.sourceFolderDisplay)
        compare(button.text,dialog.sourceFolderDisplay)
        dialog.reject(); tryCompare(dialog,"visible",false)
        verify(dialog.begin({scope:"file",url:first},true)); tryCompare(dialog,"opened",true)
        compare(dialog.sourceFolderCaption,dialog.sourceFolderDisplay); compare(dialog.sourceFolderCategory,0)
    }
    function test_selectAndCancelDoesNotSave() {
        compare(dialog.changedRows.length,0)
        const list=findChild(dialog,"trackAssignmentList")
        tryVerify(() => !!list.itemAtIndex(0))
        mouseClick(list.itemAtIndex(0))
        compare(dialog.tracks(first)[0],english)
        compare(dialog.changedRows.length,1)
        mouseClick(findChild(dialog,"trackAssignmentCancel")); tryCompare(dialog,"visible",false)
        compare(saveSpy.count,0); compare(Object.keys(dialog.drafts).length,0)
    }
    function test_trackNamesRenderLiterally_data() {
        return [{tag:"subtitles",subtitles:true},{tag:"audio",subtitles:false}]
    }
    function test_trackNamesRenderLiterally(data) {
        const media="file:///show/%3Cb%3EShow.mkv"
        const source="file:///show/%3Cb%3EEN." + (data.subtitles ? "srt" : "wav")
        const mediaName="<b>Show.mkv", trackName="<b>EN." + (data.subtitles ? "srt" : "wav")
        dialog.reject(); tryCompare(dialog,"visible",false)
        verify(dialog.begin({scope:"file",url:media},data.subtitles)); tryCompare(dialog,"opened",true)
        function loadNames() {
            dialog.loadRows(dialog.requestId,{success:true,rows:[{url:media,name:mediaName,baseline:[]}]})
            dialog.loadSources(dialog.sourceRequestId,{success:true,sources:[{url:source,name:trackName}]})
        }
        function literal(label, expected) {
            verify(!!label)
            compare(label.text,expected)
            literalMetrics.font=label.font; literalMetrics.text=expected
            fuzzyCompare(label.contentWidth,literalMetrics.advanceWidth,1,"Filename must retain all literal characters")
        }
        loadNames()
        const list=findChild(dialog,"trackAssignmentList")
        tryVerify(() => !!list.itemAtIndex(0))
        literal(findChild(list.itemAtIndex(0),"trackAssignmentName_0"),trackName)
        dialog.selectScope(1); loadNames(); dialog.setTracks(media,[source])
        tryVerify(() => !!list.itemAtIndex(0))
        literal(findChild(list.itemAtIndex(0),"trackAssignmentName_0"),mediaName)
        literal(findChild(list.itemAtIndex(0),"trackAssignmentSelection_0"),trackName)
        mouseClick(findChild(list.itemAtIndex(0),"trackAssignmentChoose_0"))
        const picker=findChild(dialog,"trackAssignmentPicker")
        tryCompare(picker,"opened",true)
        literal(findChild(picker,"trackAssignmentPickerTitle"),mediaName)
        const choices=findChild(picker,"trackAssignmentPickerList")
        tryVerify(() => !!choices.itemAtIndex(0))
        literal(findChild(choices.itemAtIndex(0),"trackAssignmentPickerName"),trackName)
        picker.close(); tryCompare(picker,"visible",false)
        dialog.errorMessage="Hakone cannot read " + trackName
        literal(findChild(dialog,"trackAssignmentError"),dialog.errorMessage)
    }
    function openPicker() {
        dialog.selectScope(1); load(rows())
        const list=findChild(dialog,"trackAssignmentList")
        tryVerify(() => !!list.itemAtIndex(0))
        mouseClick(findChild(list.itemAtIndex(0),"trackAssignmentChoose_0"))
        const picker=findChild(dialog,"trackAssignmentPicker")
        tryCompare(picker,"opened",true)
        return picker
    }
    function test_pickerKeyboardChangesRemainInDraft() {
        const picker=openPicker(), choices=findChild(picker,"trackAssignmentPickerList")
        tryVerify(() => !!choices.itemAtIndex(1))
        compare(choices.currentIndex,0)
        keyClick(Qt.Key_Return)
        compare(dialog.tracks(first).join(","),english)
        keyClick(Qt.Key_Down)
        compare(choices.currentIndex,1)
        keyClick(Qt.Key_Space)
        compare(dialog.tracks(first).length,2)
        choices.itemAtIndex(0).Accessible.toggleAction()
        compare(dialog.tracks(first).length,1)
        choices.itemAtIndex(0).Accessible.toggleAction()
        compare(dialog.tracks(first).length,2)
        keyClick(Qt.Key_Escape)
        tryCompare(picker,"visible",false)
        verify(dialog.visible); compare(saveSpy.count,0)
        compare(dialog.tracks(first).length,2)
        dialog.chooseRow(first); tryCompare(picker,"opened",true)
        mouseClick(findChild(picker,"trackAssignmentPickerClear"))
        compare(dialog.tracks(first).length,0)
        keyClick(Qt.Key_Escape); tryCompare(picker,"visible",false)
        verify(dialog.visible); compare(saveSpy.count,0)
        dialog.reject(); tryCompare(dialog,"visible",false)
        compare(Object.keys(dialog.drafts).length,0)
    }
    function test_pickerActionsFit_data() {
        return [{tag:"dark",w:1280,h:900,scale:1},{tag:"light",w:1280,h:900,scale:1,light:true},
            {tag:"scaled",w:1280,h:1000,scale:1.5},{tag:"narrow",w:360,h:640,scale:1}]
    }
    function test_pickerActionsFit(data) {
        dialog.scaleFactor=data.scale; dialog.lightTheme=!!data.light
        const picker=openPicker()
        width=data.w; height=data.h
        const files=findChild(picker,"trackAssignmentPickerFiles"), clear=findChild(picker,"trackAssignmentPickerClear"), done=findChild(picker,"trackAssignmentPickerDone")
        const bounds=picker.background
        for(const button of [files,clear,done]) {
            tryVerify(() => {
                const point=button.mapToItem(bounds,0,0)
                return point.x>=0 && point.y>=0 && point.x+button.width<=bounds.width+.5 && point.y+button.height<=bounds.height+.5
            },5000,button.objectName + " stays inside the popup")
            verify(button.width>=button.contentItem.implicitWidth)
        }
        if(data.w===360) {
            const a=files.mapToItem(bounds,0,files.height), b=clear.mapToItem(bounds,0,0), c=done.mapToItem(bounds,0,0)
            verify(a.y<=b.y && b.y+clear.height<=c.y)
        }
        const choices=findChild(picker,"trackAssignmentPickerList")
        tryVerify(()=>!!choices.itemAtIndex(0))
        mouseClick(choices.itemAtIndex(0)); compare(dialog.tracks(first)[0],english)
        mouseClick(done); tryCompare(picker,"visible",false)
        verify(dialog.visible); compare(saveSpy.count,0)
        compare(dialog.tracks(first)[0],english)
        dialog.submit(); compare(saveSpy.count,1)
        compare(saveSpy.signalArguments[0][0][0].tracks[0],english)
    }
    function test_scopeSwitchPreservesAllDrafts() {
        dialog.toggle(first,english)
        dialog.selectScope(1); load(rows()); dialog.applyMatches({[second]:[spanish]})
        compare(dialog.tracks(first)[0],english); compare(dialog.tracks(second)[0],spanish)
        dialog.selectScope(0); load([rows()[0]])
        compare(dialog.changedRows.length,2)
        dialog.submit(); compare(saveSpy.count,1)
        compare(saveSpy.signalArguments[0][0].length,2)
        dialog.submit(); compare(saveSpy.count,1)
        keyClick(Qt.Key_Escape); verify(dialog.visible)
        dialog.finish(dialog.requestId,{success:true,saved:saveSpy.signalArguments[0][0]})
        tryCompare(dialog,"visible",false)
    }
    function test_partialSaveUpdatesBaselineAndRetryKeepsDraft() {
        dialog.selectScope(1); load(rows())
        dialog.applyMatches({[first]:[english],[second]:[spanish]})
        dialog.submit(); compare(saveSpy.count,1)
        dialog.finish(dialog.requestId,{success:false,error:"Second file failed",saved:[{url:first,tracks:[english]}]})
        verify(dialog.visible); compare(dialog.changedRows.length,1)
        compare(dialog.changedRows[0].url,second)
        verify(dialog.errorMessage.includes("Second file failed"))
        dialog.submit(); compare(saveSpy.count,2)
        compare(saveSpy.signalArguments[1][0].length,1)
        compare(saveSpy.signalArguments[1][0][0].url,second)
    }
    function test_initialFolderKeepsScopeLabel() {
        dialog.reject(); tryCompare(dialog,"visible",false)
        verify(dialog.begin({scope:"folder",folder:"file:///show/",urls:[]},true))
        tryCompare(dialog,"opened",true); load(rows())
        compare(dialog.scopeIndex,1)
        tryCompare(findChild(dialog,"trackAssignmentScope"),"currentIndex",1)
        dialog.selectScope(0); load([rows()[0]])
        tryCompare(findChild(dialog,"trackAssignmentScope"),"currentIndex",0)
    }
    function test_removalAndManualChoiceSurviveMatching() {
        dialog.selectScope(1)
        dialog.loadRows(dialog.requestId,{success:true,rows:[{url:first,name:"Episode.mkv",baseline:[english]}]})
        dialog.loadSources(dialog.sourceRequestId,{success:true,sources:[{url:english,name:"English.srt"}]})
        dialog.setTracks(first,[])
        dialog.applyMatches({[first]:[english]})
        compare(dialog.tracks(first).length,0); compare(dialog.changedRows.length,1)
        dialog.submit(); compare(saveSpy.signalArguments[0][0][0].tracks.length,0)
    }
    function test_staleAndClosedResultsAreIgnored() {
        const old=dialog.requestId, oldSources=dialog.sourceRequestId
        dialog.selectScope(1)
        dialog.loadRows(old,{success:true,rows:[rows()[1]]})
        verify(dialog.loading)
        dialog.reject()
        dialog.loadRows(dialog.requestId,{success:true,rows:rows()})
        dialog.loadSources(oldSources,{success:true,sources:[{url:english,name:"English"}]})
        compare(dialog.rows.length,0); compare(dialog.sources.length,0)
        compare(saveSpy.count,0)
    }
    function test_savedEmptySelectionIsNotUndoneByMatching() {
        dialog.selectScope(1)
        dialog.loadRows(dialog.requestId,{success:true,rows:[{url:first,name:"Episode.mkv",baseline:[],explicitSelection:true}]})
        dialog.loadSources(dialog.sourceRequestId,{success:true,sources:[{url:english,name:"English.srt"}]})
        dialog.applyMatches({[first]:[english]})
        compare(dialog.tracks(first).length,0); compare(dialog.changedRows.length,0)
        dialog.applyMatches({[first]:[english]},true)
        compare(dialog.tracks(first)[0],english); compare(dialog.changedRows.length,1)
    }
    function test_delayedMatchesKeepManualChanges() {
        finishMatching=false
        dialog.selectScope(1); load(rows())
        verify(dialog.matching)
        const request=dialog.matchRequestId
        dialog.setTracks(first,[english])
        dialog.submit(); compare(saveSpy.count,0)
        dialog.loadMatches(request,{[first]:[],[second]:[spanish]})
        verify(!dialog.matching)
        compare(dialog.tracks(first)[0],english)
        compare(dialog.tracks(second)[0],spanish)
        dialog.submit(); compare(saveSpy.count,1)
    }
    function test_staleMatchesFromAnotherScopeAreIgnored() {
        finishMatching=false
        dialog.selectScope(1); load(rows())
        const request=dialog.matchRequestId
        dialog.selectScope(0); load([rows()[0]])
        dialog.loadMatches(request,{[first]:[english]})
        compare(dialog.tracks(first).length,0)
        verify(!dialog.matching)
        dialog.selectScope(1); load(rows())
        const closedRequest=dialog.matchRequestId
        dialog.reject()
        dialog.loadMatches(closedRequest,{[first]:[english]})
        compare(Object.keys(dialog.drafts).length,0)
        verify(!dialog.matching)
    }
    function test_loadedTrackCanBeClearedWithoutChangingTheSavedBaseline() {
        dialog.loadRows(dialog.requestId,{success:true,rows:[{url:first,name:"Episode.mkv",baseline:[],initialTracks:[english]}]})
        dialog.loadSources(dialog.sourceRequestId,{success:true,sources:[{url:english,name:"English.srt"}]})
        compare(dialog.tracks(first)[0],english)
        compare(dialog.changedRows.length,0)
        dialog.setTracks(first,[])
        compare(dialog.changedRows.length,1)
        dialog.submit(); compare(saveSpy.count,1)
        const row=saveSpy.signalArguments[0][0][0]
        compare(row.baseline.length,0); compare(row.initialTracks[0],english); compare(row.tracks.length,0)
    }
    function test_loadedTrackIsRetainedWhenAnotherTrackIsAdded() {
        dialog.loadRows(dialog.requestId,{success:true,rows:[{url:first,name:"Episode.mkv",baseline:[],initialTracks:[english]}]})
        dialog.loadSources(dialog.sourceRequestId,{success:true,sources:[{url:english,name:"English.srt"},{url:spanish,name:"Spanish.srt"}]})
        dialog.toggle(first,spanish)
        compare(dialog.tracks(first).length,2)
        dialog.selectScope(1); load(rows())
        verify(dialog.tracks(first).includes(english)); verify(dialog.tracks(first).includes(spanish))
        dialog.submit(); compare(saveSpy.count,1)
        compare(saveSpy.signalArguments[0][0][0].baseline.length,0)
        compare(saveSpy.signalArguments[0][0][0].tracks.length,2)
    }
    function test_dropSelectsFilesWithoutSaving() {
        verify(dialog.dropSources([english,spanish]))
        verify(dialog.loadingDrop); compare(dropSpy.count,1)
        verify(!findChild(dialog,"trackAssignmentSave").enabled)
        verify(!dialog.dropSources([english]))
        dialog.loadDroppedSources(dialog.dropRequestId,{success:true,sources:[{url:english,name:"English.srt"},{url:spanish,name:"Spanish.srt"}],error:""})
        verify(!dialog.loadingDrop); compare(dialog.tracks(first).length,2)
        compare(saveSpy.count,0); compare(dialog.changedRows.length,1)
        dialog.reject(); compare(saveSpy.count,0)
    }
    function test_dropOnBatchRowKeepsItsTargetAndExistingChoice() {
        dialog.selectScope(1); load(rows()); dialog.setTracks(first,[english])
        verify(dialog.dropSources([spanish],second))
        compare(dropSpy.signalArguments[0][3],second)
        dialog.loadDroppedSources(dialog.dropRequestId,{success:true,sources:[{url:spanish,name:"Spanish.srt"}],folder:"file:///tracks/",error:""},second)
        compare(dialog.sourceFolder,"file:///tracks/")
        compare(dialog.tracks(first)[0],english); compare(dialog.tracks(second)[0],spanish)
        compare(dialog.changedRows.length,2); compare(saveSpy.count,0)
    }
    function test_emptyDropShowsAnError() {
        verify(!dialog.dropSources([]))
        verify(dialog.errorMessage.length>0); verify(!dialog.loadingDrop)
        compare(dropSpy.count,0); compare(dialog.changedRows.length,0)
    }
    function test_failedOrLateDropsDoNotChangeDrafts() {
        dialog.setTracks(first,[english])
        verify(dialog.dropSources(["file:///wrong.mkv"]))
        dialog.loadDroppedSources(dialog.dropRequestId,{success:false,sources:[],error:"Select track files."})
        compare(dialog.tracks(first)[0],english); verify(dialog.errorMessage.length>0)
        verify(dialog.dropSources([spanish]))
        const request=dialog.dropRequestId
        dialog.reject()
        dialog.loadDroppedSources(request,{success:true,sources:[{url:spanish,name:"Spanish.srt"}]})
        compare(dialog.rows.length,0); compare(dialog.sources.length,0); compare(Object.keys(dialog.drafts).length,0)
        compare(saveSpy.count,0)
    }
    function test_onlineNeedsSetupAndAudioOnlyHasComputer() {
        dialog.selectSource(1);verify(dialog.onlineMode);compare(searchSpy.count,0)
        verify(findChild(dialog,"trackAssignmentSetup").visible)
        dialog.reject();tryCompare(dialog,"visible",false)
        verify(dialog.begin({scope:"file",url:first},false));load([rows()[0]])
        compare(dialog.sourceChoices.length,1);dialog.selectSource(1);verify(!dialog.onlineMode)
    }
    function test_onlineStartsWithTheFirstAvailablePreferredLanguage() {
        dialog.preferredSearchLanguages=["zzz","nl","en"]
        dialog.providerConfigured=true;dialog.selectSource(1)
        compare(searchSpy.count,1);compare(searchSpy.signalArguments[0][1],"nl")
        const language=findChild(dialog,"trackAssignmentLanguage")
        tryCompare(language,"currentText","Dutch")
        compare(dialog.searchLanguage,"nl")
    }
    function test_onlineAutomaticAndUnavailablePreferencesUseAllLanguages() {
        dialog.providerConfigured=true;dialog.selectSource(1)
        compare(searchSpy.signalArguments[0][1],"")
        const language=findChild(dialog,"trackAssignmentLanguage")
        tryCompare(language,"currentText","All languages")
        dialog.preferredSearchLanguages=["zzz"]
        compare(dialog.searchLanguage,"");compare(searchSpy.count,1)
    }
    function test_onlineLanguageListRefreshesTheDefaultAndCancelsOldSearch() {
        dialog.preferredSearchLanguages=["cs","en"]
        dialog.providerConfigured=true;dialog.selectSource(1)
        compare(searchSpy.signalArguments[0][1],"en")
        const old=dialog.searchRequestId
        dialog.onlineLanguages=[{code:"cs",text:"Czech"},{code:"en",text:"English"}]
        tryCompare(dialog,"searchLanguage","cs");compare(searchSpy.count,2)
        tryCompare(findChild(dialog,"trackAssignmentLanguage"),"currentText","Czech")
        compare(searchSpy.signalArguments[1][1],"cs")
        dialog.loadSearch(old,{success:true,sources:[{url:"opensubtitles:/42/English.srt",name:"English.srt"}]})
        compare(dialog.onlineSources.length,0);verify(dialog.searching)
    }
    function test_onlineLanguageRefreshKeepsAnExplicitChoice() {
        dialog.preferredSearchLanguages=["nl"]
        dialog.providerConfigured=true;dialog.selectSource(1)
        const language=findChild(dialog,"trackAssignmentLanguage")
        language.activated(dialog.searchLanguageChoices.findIndex(item=>item.code==="es"))
        compare(dialog.searchLanguage,"es");compare(searchSpy.count,2)
        dialog.onlineLanguages=[{code:"nl",text:"Dutch"},{code:"es",text:"Spanish"},{code:"en",text:"English"}]
        wait(30);compare(dialog.searchLanguage,"es");compare(searchSpy.count,2)
        tryCompare(language,"currentText","Spanish")
        dialog.preferredSearchLanguages=["en"]
        compare(dialog.searchLanguage,"es");compare(searchSpy.count,2)
        dialog.reject();tryCompare(dialog,"visible",false)
        verify(dialog.begin({scope:"file",url:first},true));tryCompare(dialog,"opened",true);load([rows()[0]])
        dialog.selectSource(1);compare(dialog.searchLanguage,"en")
        tryCompare(language,"currentText","English")
    }
    function test_onlineSearchKeepsChoicesAndIgnoresOldResults() {
        dialog.providerConfigured=true;dialog.selectSource(1);compare(searchSpy.count,1)
        const old=dialog.searchRequestId,remote="opensubtitles:/42/Show_S01E01.en.srt"
        dialog.requestSearch();dialog.loadSearch(old,{success:true,sources:[{url:remote,name:"Old"}]})
        verify(dialog.searching);compare(dialog.onlineSources.length,0)
        dialog.loadSearch(dialog.searchRequestId,{success:true,sources:[{url:remote,name:"English"}]})
        dialog.toggle(first,remote);dialog.requestSearch()
        dialog.loadSearch(dialog.searchRequestId,{success:true,sources:[]})
        compare(dialog.visibleSources[0].url,remote);compare(saveSpy.count,0)
        dialog.selectSource(0);dialog.loadSources(dialog.sourceRequestId,{success:true,sources:[]})
        compare(dialog.visibleSources.length,0);verify(dialog.tracks(first).includes(remote))
        dialog.loadSearch(dialog.searchRequestId,{success:true,sources:[{url:remote,name:"Late"}]})
        compare(dialog.onlineSources.length,0)
    }
    function test_structuredSearchDetailsCanBeCorrectedAndReset() {
        const identity={title:"Show",type:"episode",year:2005,season:1,episodes:[1]}
        dialog.searchTitle="";load([Object.assign({},rows()[0],{identity:identity})])
        compare(dialog.searchTitle,"Show (2005) S01E01")
        dialog.providerConfigured=true;dialog.selectSource(1)
        compare(dialog.searchTargets().length,1);verify(!dialog.manualSearch)
        mouseClick(findChild(dialog,"trackAssignmentDetails"))
        const editor=findChild(dialog,"trackAssignmentSearchDetails")
        tryCompare(editor,"opened",true)
        const title=findChild(dialog,"subtitleSearchDetailTitle"),year=findChild(dialog,"subtitleSearchDetailYear")
        const season=findChild(dialog,"subtitleSearchDetailSeason"),episodes=findChild(dialog,"subtitleSearchDetailEpisodes")
        compare(title.text,"Show");compare(year.text,"2005");compare(season.text,"1");compare(episodes.text,"1")
        title.text="Corrected Show";year.text="2020";season.text="2";episodes.text="3, 4"
        mouseClick(findChild(dialog,"subtitleSearchDetailApply"));tryCompare(editor,"visible",false)
        verify(dialog.manualSearch);compare(dialog.searchTitle,"Corrected Show (2020) S02E03E04")
        compare(dialog.searchDetails.episodes.length,2);compare(dialog.searchDetails.episodes[1],4)
        dialog.editSearchDetails();tryCompare(editor,"opened",true)
        mouseClick(findChild(dialog,"subtitleSearchDetailReset"));tryCompare(editor,"visible",false)
        verify(!dialog.manualSearch);compare(dialog.searchTitle,"Show (2005) S01E01")
        compare(Object.keys(dialog.searchDetails).length,0)
    }
    function test_searchDetailsAllowUnknownYearAndEpisode() {
        dialog.rows=[Object.assign({},rows()[0],{identity:{title:"A Movie",type:"unknown",year:-1,season:-1,episodes:[]}})]
        dialog.providerConfigured=true;dialog.selectSource(1);dialog.editSearchDetails()
        const editor=findChild(dialog,"trackAssignmentSearchDetails")
        tryCompare(editor,"opened",true)
        const apply=findChild(dialog,"subtitleSearchDetailApply")
        verify(apply.enabled);mouseClick(apply);tryCompare(editor,"visible",false)
        compare(dialog.searchTitle,"A Movie");compare(dialog.searchDetails.year,-1);compare(dialog.searchDetails.episodes.length,0)
    }
    function test_searchDetailsPreserveAbsoluteNumbersAndAirDates_data() {
        return [{tag:"anime",identity:{title:"Anime",type:"episode",absoluteEpisode:"452",episodes:[]}},
            {tag:"daily",identity:{title:"Daily Show",type:"episode",airDate:"2024-03-15",episodes:[]}}]
    }
    function test_searchDetailsPreserveAbsoluteNumbersAndAirDates(data) {
        dialog.rows=[Object.assign({},rows()[0],{identity:data.identity})]
        dialog.providerConfigured=true;dialog.selectSource(1);dialog.editSearchDetails()
        const editor=findChild(dialog,"trackAssignmentSearchDetails");tryCompare(editor,"opened",true)
        mouseClick(findChild(dialog,"subtitleSearchDetailApply"));tryCompare(editor,"visible",false)
        compare(dialog.searchDetails.absoluteEpisode,data.identity.absoluteEpisode || "")
        compare(dialog.searchDetails.airDate,data.identity.airDate || "")
    }
    function test_searchDetailsActionsStayVisibleInSmallScaledWindow() {
        width=800;height=600;dialog.scaleFactor=1.5
        dialog.providerConfigured=true;dialog.selectSource(1);dialog.editSearchDetails()
        const editor=findChild(dialog,"trackAssignmentSearchDetails");tryCompare(editor,"opened",true)
        const apply=findChild(dialog,"subtitleSearchDetailApply")
        tryVerify(()=>apply.mapToItem(testCase,0,apply.height).y<=height)
        verify(apply.mapToItem(testCase,0,0).y>=0)
        verify(apply.mapToItem(testCase,apply.width,0).x<=width)
    }
    function test_onlineBatchKeepsEachIdentityAndWarnings() {
        dialog.selectScope(1);load(rows());compare(dialog.searchTargets().length,2)
        dialog.providerConfigured=true;dialog.selectSource(1)
        const corrected={title:"Show",type:"episode",season:1,episodes:[1]}
        dialog.loadSearch(dialog.searchRequestId,{success:true,sources:[],identities:{[first]:corrected},warning:"Some results could not be loaded"})
        compare(dialog.matchRows()[0].identity.title,"Show")
        compare(dialog.errorMessage,"Some results could not be loaded")
        dialog.setTracks(first,[english]);dialog.applyMatches({[first]:["opensubtitles:/1"]},true)
        compare(dialog.tracks(first)[0],english)
    }
    function test_onlineMatchReasonIsVisibleAndLiteral() {
        dialog.providerConfigured=true;dialog.selectSource(1)
        dialog.loadSearch(dialog.searchRequestId,{success:true,sources:[{url:"opensubtitles:/42",name:"Subtitle",language:"en",matchReason:"<b>Exact file match</b>"}]})
        const list=findChild(dialog,"trackAssignmentList")
        tryVerify(()=>!!list.itemAtIndex(0))
        const reason=findChild(list.itemAtIndex(0),"trackAssignmentMatchReason_0")
        verify(reason!==null)
        verify(reason.visible)
        compare(reason.textFormat,Text.PlainText);verify(reason.text.includes("<b>Exact file match</b>"))
    }
    function test_downloadResolutionPreservesDraftsForRetry() {
        const remote="opensubtitles:/42/Show_S01E01.en.srt",cached="file:///cache/42/Show_S01E01.en.srt"
        dialog.providerConfigured=true;dialog.selectSource(1)
        dialog.loadSearch(dialog.searchRequestId,{success:true,sources:[{url:remote,name:"English"}]})
        dialog.setTracks(first,[remote]);dialog.submit();dialog.resolveSources({[remote]:cached})
        dialog.finish(dialog.requestId,{success:false,error:"Try again",saved:[]})
        compare(dialog.changedRows.length,1);compare(dialog.tracks(first)[0],cached)
        dialog.loadSearch(dialog.searchRequestId,{success:true,sources:[{url:remote,name:"English"}]})
        compare(dialog.visibleSources.length,1);compare(dialog.visibleSources[0].url,cached)
        dialog.submit();compare(saveSpy.count,2);compare(saveSpy.signalArguments[1][0][0].tracks[0],cached)
    }
    function test_downloadStageCanBeCanceled() {
        dialog.setTracks(first,["opensubtitles:/42/English.srt"]);dialog.submit();dialog.cancelableSave=true
        keyClick(Qt.Key_Escape);tryCompare(dialog,"visible",false);verify(!dialog.saving)
        dialog.finish(dialog.requestId,{success:true,saved:[]});compare(Object.keys(dialog.drafts).length,0)
    }
    function test_linkEntryAddsToDraftAndCancelDoesNotSave() {
        dialog.selectSource(2);verify(dialog.linkMode);compare(searchSpy.count,0)
        dialog.linkText="https://example.com/Show_S01E01.en.srt";dialog.addLink();compare(linkSpy.count,1)
        const remote="subtitlelink:/hash/Show_S01E01.en.srt"
        dialog.loadLinkedSources({success:true,sources:[{url:remote,name:"Show_S01E01.en.srt",kind:"link"}]})
        compare(dialog.linkText,"");compare(dialog.tracks(first)[0],remote);compare(dialog.visibleSources[0].url,remote)
        compare(saveSpy.count,0);dialog.reject();tryCompare(dialog,"visible",false)
        compare(dialog.linkSources.length,0);compare(dialog.linkText,"");compare(saveSpy.count,0)
    }
    function test_linkErrorKeepsTextAndMixedSelectionsSurviveSourceChanges() {
        dialog.toggle(first,english);dialog.selectSource(2);dialog.linkText="invalid"
        dialog.loadLinkedSources({success:false,sources:[],error:"Use a direct subtitle file link."})
        compare(dialog.linkText,"invalid");compare(dialog.tracks(first)[0],english);verify(dialog.errorMessage.length>0)
        const remote="subtitlelink:/hash/Show_S01E01.en.srt",cached="file:///cache/Show_S01E01.en.srt"
        dialog.loadLinkedSources({success:true,sources:[{url:remote,name:"English.srt",kind:"link"}]})
        dialog.resolveSources({[remote]:cached});compare(dialog.tracks(first).length,2)
        compare(dialog.visibleSources.length,1);compare(dialog.visibleSources[0].url,cached)
        dialog.selectSource(0);load([rows()[0]])
        verify(dialog.tracks(first).includes(cached));verify(!dialog.visibleSources.some(item=>item.url===cached))
        dialog.selectSource(2);dialog.submit();compare(saveSpy.count,1)
        verify(saveSpy.signalArguments[0][0][0].tracks.includes(cached))
    }
    function test_linkDropTargetsOnlyTheChosenBatchRow() {
        dialog.selectScope(1);load(rows());dialog.setTracks(first,[english])
        const remote="subtitlelink:/hash/Show_S01E02.es.srt"
        verify(dialog.dropSources(["https://example.com/Show_S01E02.es.srt"],second));verify(dialog.linkMode)
        dialog.loadDroppedSources(dialog.dropRequestId,{success:true,sources:[{url:remote,name:"Spanish.srt",kind:"link"}]},second)
        compare(dialog.tracks(first)[0],english);compare(dialog.tracks(second)[0],remote);compare(saveSpy.count,0)
    }
    function test_plainTextLinkDropAndClosedResults() {
        compare(dialog.dropUrls({hasUrls:false,hasText:true,text:"https://example.com/a.srt\n\nhttps://example.com/b.srt\r\n"}).length,2)
        verify(dialog.dropSources(["https://example.com/a.srt"]));const request=dialog.dropRequestId
        dialog.reject();dialog.loadDroppedSources(request,{success:true,sources:[{url:"subtitlelink:/hash/a.srt",kind:"link"}]})
        compare(dialog.linkSources.length,0);compare(dialog.rows.length,0)
    }
    function test_linkLayout_data() {
        return [{tag:"small",w:500,h:350,scale:1},{tag:"scaled",w:1280,h:900,scale:1.5},{tag:"light",w:1280,h:900,scale:1,light:true}]
    }
    function test_linkLayout(data) {
        width=data.w;height=data.h;dialog.scaleFactor=data.scale;dialog.lightTheme=!!data.light;dialog.selectSource(2);wait(50)
        for(const name of ["trackAssignmentLink","trackAssignmentAddLink","trackAssignmentSave"]) {
            const item=findChild(dialog,name),point=item.mapToItem(testCase,0,0)
            verify(point.x>=0 && point.y>=0,name);verify(point.x+item.width<=width && point.y+item.height<=height,name)
        }
    }
    function test_folderRuleOnlySaveAndRetry() {
        dialog.selectScope(1);load(rows());dialog.folderRuleDefinition="new-rule"
        compare(dialog.changedRows.length,0);verify(dialog.folderRuleChanged)
        dialog.submit();compare(saveSpy.count,1);compare(saveSpy.signalArguments[0][0].length,0)
        dialog.finish(dialog.requestId,{success:false,error:"Could not save folder matches",saved:[]})
        verify(dialog.visible);verify(findChild(dialog,"trackAssignmentSave").enabled)
        dialog.submit();compare(saveSpy.count,2)
        dialog.finish(dialog.requestId,{success:true,saved:[],folderRuleSaved:true,folderRule:"new-rule"})
        tryCompare(dialog,"visible",false)
    }
    function test_pendingRuleSurvivesReturningToFileScope() {
        dialog.selectScope(1);load(rows());dialog.folderRuleDefinition="new-rule"
        dialog.selectScope(0);load([rows()[0]])
        verify(dialog.folderRuleChanged);compare(dialog.pendingFolderRule.urls.length,2)
        dialog.submit();compare(saveSpy.count,1);compare(saveSpy.signalArguments[0][0].length,0)
    }
    function test_inheritedChoiceCanBePinnedWithoutChangingTracks() {
        dialog.loadRows(dialog.requestId,{success:true,rows:[{url:first,name:"Episode.mkv",baseline:[english],initialTracks:[english],inheritedRule:true}]})
        dialog.loadSources(dialog.sourceRequestId,{success:true,sources:[{url:english,name:"English.srt"}]})
        dialog.toggle(first,english);dialog.toggle(first,english)
        compare(dialog.changedRows.length,1);dialog.submit();compare(saveSpy.count,1)
        const row=saveSpy.signalArguments[0][0][0]
        verify(row.inheritedRule && row.manual);compare(row.tracks[0],english)
    }
    function test_missingInheritedLanguageDoesNotSelectAnotherLanguage() {
        dialog.selectScope(1)
        dialog.loadRows(dialog.requestId,{success:true,folderRule:"old",rows:[{url:first,name:"Episode.mkv",baseline:[],initialTracks:[],inheritedRule:true}]})
        dialog.folderRuleDefinition="old"
        dialog.loadSources(dialog.sourceRequestId,{success:true,sources:[{url:spanish,name:"Spanish.srt"}]})
        dialog.applyMatches({[first]:[spanish]});compare(dialog.tracks(first).length,0)
        compare(dialog.changedRows.length,0);verify(!dialog.folderRuleChanged)
        dialog.applyMatches({[first]:[spanish]},true);compare(dialog.tracks(first)[0],spanish);verify(dialog.folderRuleTouched)
    }
    function test_inheritedSourceOutsideTheCurrentFolderCanBeSelectedAgain() {
        dialog.loadRows(dialog.requestId,{success:true,rows:[{url:first,name:"Episode.mkv",baseline:[],initialTracks:[english],inheritedRule:true}]})
        dialog.loadSources(dialog.sourceRequestId,{success:true,sources:[]})
        const list=findChild(dialog,"trackAssignmentList")
        tryVerify(() => !!list.itemAtIndex(0))
        mouseClick(list.itemAtIndex(0))
        compare(dialog.tracks(first).length,0)
        compare(dialog.visibleSources.length,1)
        mouseClick(list.itemAtIndex(0))
        compare(dialog.tracks(first)[0],english)
        dialog.submit();compare(saveSpy.count,1)
        verify(saveSpy.signalArguments[0][0][0].manual)
    }
    function test_savedSourceRestoredWithoutOverwritingPendingSource() {
        dialog.selectScope(1)
        dialog.loadRows(dialog.requestId,{success:true,folderRule:"old",sourceFolder:"file:///captions/",rows:rows()})
        dialog.loadSources(dialog.sourceRequestId,{success:true,sources:[]})
        compare(dialog.sourceFolder,"file:///captions/")
        dialog.folderRuleTouched=true;dialog.sourceFolder="file:///other/"
        dialog.selectScope(0);load([rows()[0]]);dialog.selectScope(1)
        dialog.loadRows(dialog.requestId,{success:true,folderRule:"old",sourceFolder:"file:///captions/",rows:rows()})
        compare(dialog.sourceFolder,"file:///other/")
    }
    function test_removeRuleIsReversibleUntilSave() {
        dialog.selectScope(1);load(rows());dialog.folderRuleBaseline="old";dialog.folderRuleDefinition="old"
        const button=findChild(dialog,"trackAssignmentRemoveFolderRule")
        verify(button.visible);mouseClick(button);verify(dialog.removeFolderRule);verify(dialog.folderRuleChanged)
        mouseClick(button);verify(!dialog.removeFolderRule);verify(!dialog.folderRuleChanged)
        mouseClick(button);dialog.reject();compare(saveSpy.count,0);verify(!dialog.removeFolderRule)
    }
    function test_removeRuleCanSaveWithoutChangingFileChoices() {
        dialog.selectScope(1);load(rows());dialog.folderRuleBaseline="old";dialog.folderRuleDefinition="old"
        mouseClick(findChild(dialog,"trackAssignmentRemoveFolderRule"));dialog.submit()
        compare(saveSpy.count,1);compare(saveSpy.signalArguments[0][0].length,0);verify(dialog.folderRuleTarget.remove)
    }
    function test_lateFolderRuleResultDoesNotChangeFileScope() {
        dialog.selectScope(1);const stale=dialog.requestId;dialog.selectScope(0);load([rows()[0]])
        dialog.loadRows(stale,{success:true,folderRule:"stale",sourceFolder:"file:///stale/",rows:rows()})
        compare(dialog.folderRuleBaseline,"");verify(!dialog.folderRuleLoaded)
    }
    function test_folderRuleSmallScaledFooter() {
        dialog.selectScope(1);load(rows());dialog.folderRuleBaseline="old";dialog.folderRuleDefinition="old"
        width=500;height=350;dialog.scaleFactor=1.5;dialog.folderRuleAutomatic=false;wait(50)
        const cancel=findChild(dialog,"trackAssignmentCancel"),save=findChild(dialog,"trackAssignmentSave"),remove=findChild(dialog,"trackAssignmentRemoveFolderRule")
        const a=cancel.mapToItem(testCase,0,0),b=remove.mapToItem(testCase,0,0),c=save.mapToItem(testCase,0,0)
        verify(a.x+cancel.width<=b.x);verify(b.x+remove.width<=c.x)
        verify(c.x+save.width<=width);verify(c.y+save.height<=height)
    }
    function test_onlineLayout_data() {
        return [{tag:"small",w:500,h:350,scale:1},{tag:"scaled",w:1280,h:900,scale:1.5},{tag:"light",w:1280,h:900,scale:1,light:true}]
    }
    function test_onlineLayout(data) {
        width=data.w;height=data.h;dialog.scaleFactor=data.scale;dialog.lightTheme=!!data.light
        dialog.selectSource(1);wait(50)
        for (const name of ["trackAssignmentSearch","trackAssignmentLanguage","trackAssignmentSetup"]) {
            const item=findChild(dialog,name),point=item.mapToItem(testCase,0,0)
            verify(point.x>=0 && point.y>=0,name)
            verify(point.x+item.width<=width && point.y+item.height<=height,name)
        }
        mouseClick(findChild(dialog,"trackAssignmentSetup"))
        const setup=findChild(dialog,"trackAssignmentProviderSetup");tryCompare(setup,"opened",true)
        const field=findChild(dialog,"subtitleProviderApiKey");field.text="unit-secret"
        verify(field.echoMode!==TextInput.Normal);keyClick(Qt.Key_Escape);tryCompare(setup,"visible",false)
        compare(field.text,"");verify(dialog.visible)
    }
    function test_scaledAndSmallWindow_data() {
        return [{tag:"small",w:500,h:350,scale:1},{tag:"scaled",w:1280,h:900,scale:1.5},{tag:"light",w:1280,h:900,scale:1,light:true}]
    }
    function test_scaledAndSmallWindow(data) {
        width=data.w; height=data.h; dialog.scaleFactor=data.scale; dialog.lightTheme=!!data.light
        wait(50)
        verify(dialog.x>=0 && dialog.y>=0)
        verify(dialog.x+dialog.width<=width && dialog.y+dialog.height<=height)
        for (const name of ["trackAssignmentClose","trackAssignmentScope","trackAssignmentFiles","trackAssignmentFolder","trackAssignmentCancel","trackAssignmentSave"]) {
            const button=findChild(dialog,name), point=button.mapToItem(testCase,0,0)
            verify(point.x>=0 && point.y>=0,name)
            verify(point.x+button.width<=width && point.y+button.height<=height,name)
        }
    }
}

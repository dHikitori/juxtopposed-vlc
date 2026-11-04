// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "JuxtopposedPlaylistTrackTarget"
    width: 1280; height: 800; visible: true; when: windowShown
    RedesignPlaylistTrackTarget { id: dialog; parent: testCase }
    SignalSpy { id: tracksSpy; target: dialog; signalName: "tracksRequested" }
    function result() {
        return {success:true,name:"Saved playlist",items:[
            {name:"Missing video.mkv",url:"file:///missing.mkv",location:"/missing.mkv",enabled:false},
            {name:"Episode.mkv",url:"file:///one/episode%20%26%20fran%C3%A7ais.mkv",location:"/one/episode & français.mkv",enabled:true},
            {name:"Episode.mkv",url:"file:///two/episode.mkv",location:"/two/episode.mkv",enabled:true}]}
    }
    function init() {
        width=1280; height=800; dialog.scaleFactor=1; dialog.lightTheme=false; dialog.choosing=false
        dialog.close(); tracksSpy.clear()
        const request=dialog.begin("VLC_ML_PARENT_PLAYLIST - 42",true)
        tryCompare(dialog,"opened",true); dialog.load(request,result())
    }
    function cleanup() { dialog.choosing=false; dialog.close(); tryCompare(dialog,"visible",false) }
    function test_fileSelectionKeepsUrlAndTrackType() {
        compare(dialog.selectedIndex,1)
        verify(dialog.valid)
        compare(findChild(dialog,"playlistTrackLocation").text,"/one/episode & français.mkv")
        dialog.selectedIndex=2
        mouseClick(findChild(dialog,"playlistTrackSelect"))
        compare(tracksSpy.count,1)
        compare(tracksSpy.signalArguments[0][0].toString(),"file:///two/episode.mkv")
        compare(tracksSpy.signalArguments[0][1],true)
        dialog.finish(); tryCompare(dialog,"visible",false)
        const request=dialog.begin("VLC_ML_PARENT_PLAYLIST - 42",false)
        tryCompare(dialog,"opened",true); dialog.load(request,result())
        compare(dialog.title,"Add Audio Tracks")
        dialog.submit(); compare(tracksSpy.count,2)
        compare(tracksSpy.signalArguments[1][0].toString(),Qt.resolvedUrl("file:///one/episode%20%26%20fran%C3%A7ais.mkv").toString())
        compare(tracksSpy.signalArguments[1][1],false)
    }
    function test_unavailableFilesCannotBeSubmitted() {
        dialog.selectedIndex=0; verify(!dialog.valid); dialog.submit(); compare(tracksSpy.count,0)
        const choice=findChild(dialog,"playlistTrackFile"); choice.forceActiveFocus()
        keyClick(Qt.Key_Down); compare(dialog.selectedIndex,1)
        dialog.begin("VLC_ML_PARENT_PLAYLIST - 43",true)
        dialog.load(dialog.requestId,{success:true,items:[result().items[0]]})
        verify(!dialog.valid); verify(dialog.errorMessage.length>0)
        verify(!findChild(dialog,"playlistTrackSelect").enabled)
    }
    function test_staleAndClosedResultsDoNotChangeSelection() {
        const old=dialog.requestId, request=dialog.begin("VLC_ML_PARENT_PLAYLIST - 43",false)
        dialog.load(old,result()); verify(dialog.loading); compare(dialog.items.length,0)
        dialog.reject(); dialog.load(request,result()); compare(dialog.items.length,0)
        compare(tracksSpy.count,0)
    }
    function test_cancelAndEmptyOrFailedPlaylist() {
        mouseClick(findChild(dialog,"playlistTrackCancel")); tryCompare(dialog,"visible",false)
        compare(tracksSpy.count,0)
        dialog.begin("VLC_ML_PARENT_PLAYLIST - 43",true)
        dialog.load(dialog.requestId,{success:true,name:"Empty",items:[]})
        verify(!dialog.valid); verify(dialog.errorMessage.length>0)
        dialog.begin("VLC_ML_PARENT_PLAYLIST - 99",true)
        dialog.load(dialog.requestId,{success:false,error:"Playlist removed"})
        compare(dialog.errorMessage,"Playlist removed"); dialog.submit(); compare(tracksSpy.count,0)
    }
    function test_chooseDisablesDuplicateRequestsAndClose() {
        keyClick(Qt.Key_Return,Qt.ControlModifier); compare(tracksSpy.count,1)
        dialog.submit(); compare(tracksSpy.count,1)
        compare(dialog.begin("VLC_ML_PARENT_PLAYLIST - 43",false),-1)
        keyClick(Qt.Key_Escape); verify(dialog.visible)
        verify(!findChild(dialog,"playlistTrackClose").enabled)
        dialog.finish(); tryCompare(dialog,"visible",false)
    }
    function test_scaledAndSmallWindow_data() {
        return [{tag:"small",w:500,h:300,scale:1},{tag:"scaled",w:1280,h:800,scale:1.5},{tag:"light",w:1280,h:800,scale:1,light:true}]
    }
    function test_scaledAndSmallWindow(data) {
        width=data.w; height=data.h; dialog.scaleFactor=data.scale; dialog.lightTheme=!!data.light
        dialog.load(dialog.requestId,{success:false,error:"Hakone cannot read this playlist. Select it again."})
        wait(50)
        verify(dialog.x>=0 && dialog.y>=0)
        verify(dialog.x+dialog.width<=width && dialog.y+dialog.height<=height)
        for (const name of ["playlistTrackClose","playlistTrackCancel","playlistTrackSelect"]) {
            const button=findChild(dialog,name), point=button.mapToItem(testCase,0,0)
            verify(point.x>=0 && point.y>=0,name)
            verify(point.x+button.width<=width && point.y+button.height<=height,name)
        }
    }
}

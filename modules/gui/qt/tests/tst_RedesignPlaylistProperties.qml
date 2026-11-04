// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "JuxtopposedPlaylistProperties"
    width: 1280; height: 800
    visible: true; when: windowShown
    RedesignPlaylistProperties { id: dialog; parent: testCase }
    SignalSpy { id: saveSpy; target: dialog; signalName: "saveRequested" }
    SignalSpy { id: coverSpy; target: dialog; signalName: "coverRequested" }
    function details(extra) {
        return Object.assign({success:true, name:"Saved playlist", cover:"", readOnly:false,
            count:4, videoCount:2, audioCount:1, unknownCount:1, missingCount:1,
            duration:3723000, unknownDurationCount:1, created:1700000000,
            source:"file:///playlists/saved%20playlist.m3u8"}, extra || {})
    }
    function init() {
        width=1280; height=800; dialog.scaleFactor=1; dialog.lightTheme=false
        dialog.saving=false; dialog.close(); saveSpy.clear(); coverSpy.clear()
        const request=dialog.begin("PLAYLIST - 42")
        tryCompare(dialog,"opened",true)
        dialog.load(request, details())
    }
    function cleanup() { dialog.saving=false; dialog.close(); tryCompare(dialog,"visible",false) }
    function test_unchangedAndBlankCannotSave() {
        verify(!dialog.dirty); verify(!findChild(dialog,"playlistPropertiesSave").enabled)
        dialog.submit(); compare(saveSpy.count,0)
        dialog.playlistName=" \t "; dialog.submit(); compare(saveSpy.count,0)
        dialog.playlistName=" Updated name "
        mouseClick(findChild(dialog,"playlistPropertiesSave"))
        compare(saveSpy.count,1); compare(saveSpy.signalArguments[0][0],"PLAYLIST - 42")
        compare(saveSpy.signalArguments[0][1],"Updated name")
    }
    function test_staleLoadAndSaveCannotChangeAnotherPlaylist() {
        const previous=dialog.requestId
        dialog.reject(); const request=dialog.begin("PLAYLIST - 43")
        dialog.load(previous,details({name:"Old response"}))
        verify(dialog.loading); compare(dialog.playlistName,"")
        dialog.load(request,details({name:"Second playlist"}))
        dialog.playlistName="Second edit"; dialog.submit()
        dialog.finish(previous,{success:true}); verify(dialog.visible); verify(dialog.saving)
        dialog.finish(request,{success:false,error:"Cannot save"})
        compare(dialog.playlistName,"Second edit"); verify(!dialog.saving)
    }
    function test_cancelDiscardsDraftAndLateResponse() {
        dialog.playlistName="Draft"; dialog.cover=Qt.resolvedUrl("../pixmaps/prefs/spref_interface.png")
        mouseClick(findChild(dialog,"playlistPropertiesCancel")); tryCompare(dialog,"visible",false)
        dialog.load(dialog.requestId,details({name:"Late response"}))
        compare(dialog.playlistName,"Draft"); compare(saveSpy.count,0)
        const request=dialog.begin("PLAYLIST - 42"); tryCompare(dialog,"opened",true)
        compare(dialog.playlistName,""); compare(dialog.cover.toString(),"")
        dialog.load(request,details()); verify(!dialog.dirty)
    }
    function test_failurePreservesNameAndCoverForRetry() {
        dialog.playlistName="Retry"; dialog.cover=Qt.resolvedUrl("../pixmaps/prefs/spref_interface.png")
        dialog.submit(); dialog.finish(dialog.requestId,{success:false,error:"Cannot save"})
        verify(dialog.visible); verify(!dialog.saving); compare(dialog.playlistName,"Retry")
        verify(dialog.cover.toString().length>0); compare(dialog.errorMessage,"Cannot save")
        dialog.submit(); compare(saveSpy.count,2)
        dialog.finish(dialog.requestId,{success:true}); tryCompare(dialog,"visible",false)
    }
    function test_readOnlyMetadataAndUnavailablePlaylist() {
        dialog.load(dialog.requestId,details({readOnly:true}))
        verify(findChild(dialog,"playlistPropertiesName").readOnly)
        verify(!findChild(dialog,"playlistPropertiesCover").visible)
        verify(!findChild(dialog,"playlistPropertiesSave").visible)
        compare(findChild(dialog,"playlistPropertiesSource").text,"file:///playlists/saved%20playlist.m3u8")
        compare(dialog.duration(3723000),"01:02:03")
        dialog.submit(); compare(saveSpy.count,0)
        dialog.begin("PLAYLIST - 99"); dialog.load(dialog.requestId,{success:false,error:"Playlist removed"})
        verify(!dialog.valid); compare(dialog.errorMessage,"Playlist removed")
    }
    function test_coverRemoveAndKeyboardBusyGuard() {
        mouseClick(findChild(dialog,"playlistPropertiesCover")); compare(coverSpy.count,1)
        dialog.cover=Qt.resolvedUrl("../pixmaps/prefs/spref_interface.png")
        mouseClick(findChild(dialog,"playlistPropertiesRemoveCover")); compare(dialog.cover.toString(),"")
        dialog.playlistName="Keyboard"
        keyClick(Qt.Key_Return,Qt.ControlModifier); compare(saveSpy.count,1)
        keyClick(Qt.Key_Escape); verify(dialog.visible)
        verify(!findChild(dialog,"playlistPropertiesClose").enabled)
        compare(dialog.begin("PLAYLIST - 99"),-1)
        dialog.finish(dialog.requestId,{success:false,error:"Cannot save"})
        keyClick(Qt.Key_Escape); tryCompare(dialog,"visible",false)
    }
    function test_scaledAndSmallWindow_data() {
        return [{tag:"small",w:500,h:400,scale:1},{tag:"scaled",w:1280,h:800,scale:1.5},{tag:"light",w:1280,h:800,scale:1,light:true}]
    }
    function test_scaledAndSmallWindow(data) {
        width=data.w; height=data.h; dialog.scaleFactor=data.scale; dialog.lightTheme=!!data.light
        dialog.playlistName="Retry"; dialog.submit()
        dialog.finish(dialog.requestId,{success:false,error:"Hakone cannot read or save this image. Select another image."})
        wait(50)
        verify(dialog.x>=0 && dialog.y>=0)
        verify(dialog.x+dialog.width<=width && dialog.y+dialog.height<=height)
        for (const name of ["playlistPropertiesClose","playlistPropertiesSave","playlistPropertiesCancel"]) {
            const button=findChild(dialog,name), point=button.mapToItem(testCase,0,0)
            verify(point.x>=0 && point.y>=0,name)
            verify(point.x+button.width<=width && point.y+button.height<=height,name)
        }
        const error=findChild(dialog,"playlistPropertiesError"), save=findChild(dialog,"playlistPropertiesSave")
        verify(error.mapToItem(testCase,0,0).y+error.height<=save.mapToItem(testCase,0,0).y)
    }
}

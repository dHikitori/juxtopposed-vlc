// SPDX-License-Identifier: GPL-2.0-or-later
// Reuse the real input/demux fixture. Do not replace native player operations.
#define main native_session_original_test_main
#include "../../../../../test/src/player/addon_session.c"
#undef main
#include "native_session_fixture.h"
struct native_session_fixture { struct fixture value; };
struct native_session_fixture* native_session_create(void) {
    test_setup();
    struct native_session_fixture* value=malloc(sizeof(*value));
    if(value) init(&value->value);
    return value;
}
void native_session_destroy(struct native_session_fixture* value) { if(value) { clean(&value->value); free(value); } }
vlc_player_t* native_session_player(struct native_session_fixture* value) { return value->value.player; }
void native_session_block(struct native_session_fixture* value) { block_demux(&value->value); }
void native_session_release(struct native_session_fixture* value) { release_demux(&value->value); }
void native_session_lock(struct native_session_fixture* value) { vlc_player_Lock(value->value.player); }
void native_session_unlock(struct native_session_fixture* value) { vlc_player_Unlock(value->value.player); }
int native_session_active(struct native_session_fixture* value) { return snapshot(&value->value).active; }
void native_session_local_pause(struct native_session_fixture* value) {
    vlc_player_Lock(value->value.player); vlc_player_Pause(value->value.player); vlc_player_Unlock(value->value.player);
}
void native_session_local_rate(struct native_session_fixture* value,float rate) {
    vlc_player_Lock(value->value.player); vlc_player_ChangeRate(value->value.player,rate); vlc_player_Unlock(value->value.player);
}
void native_session_restart(struct native_session_fixture* value) {
    vlc_player_t* player=value->value.player;
    vlc_player_Lock(player); vlc_player_Stop(player); vlc_player_Unlock(player);
    wait_state(&value->value,VLC_PLAYER_STATE_STOPPED);
    vlc_player_Lock(player); assert(vlc_player_Start(player)==VLC_SUCCESS); vlc_player_Unlock(player);
    wait_state(&value->value,VLC_PLAYER_STATE_PLAYING);
}
unsigned native_session_seeks(struct native_session_fixture* value) {
    vlc_mutex_lock(&value->value.lock); const unsigned result=value->value.seeks; vlc_mutex_unlock(&value->value.lock); return result;
}
unsigned native_session_retained(struct native_session_fixture* value) { return snapshot(&value->value).retained_tokens; }
int64_t native_session_last_seek(struct native_session_fixture* value) {
    vlc_mutex_lock(&value->value.lock); const int64_t result=value->value.last_seek; vlc_mutex_unlock(&value->value.lock); return result;
}
float native_session_rate(struct native_session_fixture* value) {
    vlc_mutex_lock(&value->value.lock); const float result=value->value.effective_rate; vlc_mutex_unlock(&value->value.lock); return result;
}

// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct native_session_fixture;
struct vlc_player_t;
struct native_session_fixture* native_session_create(void);
void native_session_destroy(struct native_session_fixture*);
struct vlc_player_t* native_session_player(struct native_session_fixture*);
void native_session_block(struct native_session_fixture*);
void native_session_release(struct native_session_fixture*);
void native_session_lock(struct native_session_fixture*);
void native_session_unlock(struct native_session_fixture*);
int native_session_active(struct native_session_fixture*);
void native_session_local_pause(struct native_session_fixture*);
void native_session_local_rate(struct native_session_fixture*,float);
void native_session_restart(struct native_session_fixture*);
unsigned native_session_seeks(struct native_session_fixture*);
unsigned native_session_retained(struct native_session_fixture*);
int64_t native_session_last_seek(struct native_session_fixture*);
float native_session_rate(struct native_session_fixture*);
#ifdef __cplusplus
}
#endif

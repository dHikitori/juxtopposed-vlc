#pragma once
#include <vlc_common.h>
#include <vlc_metadata_backend.h>
#include <vlc_tick.h>
typedef struct vlc_metadata_artwork_admission vlc_metadata_artwork_admission_t;
#ifdef __cplusplus
extern "C" {
#endif
struct keeper_fixture;
struct keeper_fixture *keeper_fixture_new(void);
void keeper_fixture_delete(struct keeper_fixture *);
vlc_metadata_job_t *keeper_fixture_child(struct keeper_fixture *);
int keeper_fixture_prepare_anchor(struct keeper_fixture *);
input_item_t *keeper_fixture_approved(struct keeper_fixture *, const char *uri);
int keeper_fixture_prepare_fetched(struct keeper_fixture *, input_item_t *approved);
int keeper_fixture_apply(struct keeper_fixture *);
void keeper_fixture_lock_target(struct keeper_fixture *);
void keeper_fixture_unlock_target(struct keeper_fixture *);
int keeper_fixture_status(struct keeper_fixture *);
bool keeper_fixture_gate_locked(vlc_metadata_artwork_admission_t *);
bool keeper_fixture_enter(vlc_metadata_artwork_admission_t *);
void keeper_fixture_leave(vlc_metadata_artwork_admission_t *);
#ifdef __cplusplus
}
#endif

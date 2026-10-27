// SPDX-License-Identifier: LGPL-2.1-or-later
#include "../../libvlc/test.h"
#include "../../../lib/libvlc_internal.h"
#include <vlc_media_library.h>
#include <vlc_medialibrary_work.h>
#include <vlc_modules.h>
#include <vlc_tick.h>
#include <vlc_fs.h>
#include <stdatomic.h>
#include <sys/stat.h>

static libvlc_instance_t *create(void)
{
    const char *args[]={"--ignore-config","--no-plugins-cache","--media-library","--no-metadata-network-access","--quiet"};
    libvlc_instance_t *instance=libvlc_new(ARRAY_SIZE(args),args);
    assert(instance != NULL); return instance;
}
static vlc_medialibrary_t *library(libvlc_instance_t *instance)
{
    vlc_medialibrary_t *ml=vlc_ml_instance_get(instance->p_libvlc_int);
    assert(ml != NULL); return ml;
}
struct cleanup {
    libvlc_instance_t *instance;
    atomic_bool complete;
};
static void *destroy(void *opaque)
{
    struct cleanup *state=opaque;
    libvlc_release(state->instance); atomic_store(&state->complete,true); return NULL;
}
static void active_work_delays_native_destruction(void)
{
    libvlc_instance_t *instance=create(); vlc_medialibrary_t *ml=library(instance);
    vlc_ml_work_t *active=vlc_ml_work_Acquire(ml), *pending=vlc_ml_work_Acquire(ml);
    assert(active != NULL && pending != NULL && vlc_ml_work_Begin(active) == ml);
    assert(vlc_ml_work_Begin(active) == NULL);
    struct cleanup state={.instance=instance}; atomic_init(&state.complete,false);
    vlc_thread_t thread; assert(vlc_clone(&thread,destroy,&state) == 0);
    const vlc_tick_t deadline=vlc_tick_now()+VLC_TICK_FROM_SEC(5);
    for (;;) {
        vlc_ml_work_t *probe=vlc_ml_work_Acquire(ml);
        if (probe == NULL) break;
        vlc_ml_work_Release(probe);
        assert(vlc_tick_now() < deadline); vlc_tick_sleep(VLC_TICK_FROM_MS(100));
    }
    assert(!atomic_load(&state.complete));
    assert(vlc_ml_work_Begin(pending) == NULL);
    vlc_ml_work_End(active); vlc_ml_work_End(active);
    vlc_join(thread,NULL); assert(atomic_load(&state.complete));
    assert(vlc_ml_work_Begin(active) == NULL);
    vlc_ml_work_Release(active); vlc_ml_work_Release(pending);
}
static void pending_work_needs_no_qt_delivery(void)
{
    libvlc_instance_t *instance=create(); vlc_medialibrary_t *ml=library(instance);
    vlc_ml_work_t *canceled=vlc_ml_work_Acquire(ml), *retained=vlc_ml_work_Acquire(ml);
    assert(canceled != NULL && retained != NULL);
    vlc_ml_work_Release(canceled); /* A queued functor was destroyed. */
    libvlc_release(instance); /* The other functor still has no Qt delivery. */
    assert(vlc_ml_work_Begin(retained) == NULL);
    vlc_ml_work_End(retained); vlc_ml_work_Release(retained);
}
static void a_new_instance_has_an_independent_gate(void)
{
    libvlc_instance_t *first=create(); vlc_ml_work_t *old=vlc_ml_work_Acquire(library(first)); assert(old != NULL);
    libvlc_release(first);
    libvlc_instance_t *second=create(); vlc_medialibrary_t *ml=library(second); vlc_ml_work_t *fresh=vlc_ml_work_Acquire(ml);
    assert(fresh != NULL && vlc_ml_work_Begin(fresh) == ml && vlc_ml_work_Begin(old) == NULL);
    vlc_ml_work_End(fresh); vlc_ml_work_Release(fresh); libvlc_release(second); vlc_ml_work_Release(old);
}
static void remove_profile(const char *path)
{
    vlc_DIR *directory=vlc_opendir(path); if (directory == NULL) { assert(errno == ENOENT); return; }
    const char *name;
    while ((name=vlc_readdir(directory)) != NULL) {
        if (!strcmp(name,".") || !strcmp(name,"..")) continue;
        char *child; assert(asprintf(&child,"%s/%s",path,name) >= 0);
        struct stat st; assert(vlc_lstat(child,&st) == 0);
        if (S_ISDIR(st.st_mode)) remove_profile(child); else assert(vlc_unlink(child) == 0);
        free(child);
    }
    vlc_closedir(directory); assert(rmdir(path) == 0);
}
int main(void)
{
    test_init();
    char *profile; assert(asprintf(&profile,TOP_BUILDDIR "/test/ml-work-%lu",(unsigned long)getpid()) >= 0);
    assert(vlc_mkdir(profile,0700) == 0);
#ifdef _WIN32
    assert(_putenv_s("VLC_USERDATA_PATH",profile) == 0);
#else
    assert(setenv("VLC_USERDATA_PATH",profile,1) == 0);
#endif
    libvlc_instance_t *probe=create();
    if (!module_exists("medialibrary")) { libvlc_release(probe); remove_profile(profile); free(profile); return 77; }
    libvlc_release(probe);
    assert(vlc_ml_work_Acquire(NULL) == NULL && vlc_ml_work_Begin(NULL) == NULL);
    vlc_ml_work_End(NULL); vlc_ml_work_Release(NULL);
    active_work_delays_native_destruction(); pending_work_needs_no_qt_delivery(); a_new_instance_has_an_independent_gate();
    remove_profile(profile); free(profile);
    puts("Four native media-library work groups passed."); return 0;
}

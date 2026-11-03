// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <vlc_common.h>
#include <vlc_input_policy.h>
#include <vlc_stream.h>
#include <vlc/vlc.h>
#include "../../../lib/libvlc_internal.h"
#include <assert.h>
#include <errno.h>

static unsigned spawn_calls;
static void *managed;
static void *policy_test_malloc(vlc_object_t *object, size_t size)
{ managed = vlc_obj_malloc(object, size); return managed; }
#undef vlc_obj_malloc
#define vlc_obj_malloc policy_test_malloc
int input_policy_test_spawn(pid_t *, const char *, const int *, const char *const *);
#define vlc_spawnp input_policy_test_spawn
#define MODULE_NAME input_policy_ytdl_test
#ifndef EXEEXT
# define EXEEXT ""
#endif
#include "../../../modules/demux/ytdl.c"
#undef vlc_spawnp
#undef vlc_obj_malloc
const char vlc_module_name[] = "input-policy-ytdl-test";

int input_policy_test_spawn(pid_t *pid, const char *path, const int *fdv, const char *const *argv)
{
    (void)pid; assert(strcmp(path, "native-policy-test-only") == 0);
    assert(fdv != NULL && argv != NULL && argv[4] != NULL); ++spawn_calls;
    return EACCES;
}
static stream_t *memory(vlc_object_t *parent)
{
    static uint8_t bytes[] = "not a media file";
    stream_t *stream = vlc_stream_MemoryNew(parent, bytes, sizeof(bytes), true); assert(stream != NULL);
    assert(var_Create(stream, "ytdl-path", VLC_VAR_STRING) == VLC_SUCCESS);
    assert(var_SetString(stream, "ytdl-path", "native-policy-test-only") == VLC_SUCCESS);
    return stream;
}
int main(void)
{
    const char *arguments[] = {"--ignore-config", "--quiet", "--no-plugins-cache", "--no-media-library"};
    libvlc_instance_t *instance = libvlc_new(ARRAY_SIZE(arguments), arguments); assert(instance != NULL);
    vlc_object_t *root = VLC_OBJECT(instance->p_libvlc_int);
    stream_t *ordinary = memory(root);
    assert(OpenCommon(VLC_OBJECT(ordinary), "https://example.invalid/watch") == VLC_EGENERIC);
    assert(spawn_calls == 1); vlc_obj_free(ordinary, managed); managed = NULL; vlc_stream_Delete(ordinary);

    vlc_input_policy_t *policy = vlc_input_policy_NewProxy(32123, "/native_test/", NULL, false); assert(policy != NULL);
    stream_t *scoped = memory(root); assert(vlc_input_policy_AttachObject(VLC_OBJECT(scoped), policy) == VLC_SUCCESS);
    assert(OpenCommon(VLC_OBJECT(scoped), "http://127.0.0.1:32123/native_test/media") == VLC_EGENERIC);
    assert(spawn_calls == 1);
    stream_t *child = memory(VLC_OBJECT(scoped)); assert(vlc_input_policy_Has(VLC_OBJECT(child)));
    assert(OpenCommon(VLC_OBJECT(child), "https://example.invalid/watch") == VLC_EGENERIC); assert(spawn_calls == 1);
    vlc_input_policy_Revoke(policy);
    assert(OpenCommon(VLC_OBJECT(scoped), "http://127.0.0.1:32123/native_test/media") == VLC_EGENERIC); assert(spawn_calls == 1);
    vlc_stream_Delete(child); vlc_stream_Delete(scoped); vlc_input_policy_Release(policy); libvlc_release(instance);
    return 0;
}

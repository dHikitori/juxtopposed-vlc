// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <vlc/vlc.h>
#include <vlc_common.h>
#undef NDEBUG
#include <assert.h>
#include <sys/stat.h>
#include <vlc_fs.h>
#include <vlc_media_library.h>
#include <vlc_player.h>
#include <vlc_url.h>
#include "../../../lib/libvlc_internal.h"

static libvlc_instance_t *open_library(void)
{
    const char *args[] = {
        "--quiet", "--ignore-config", "--media-library", "--no-auto-preparse",
        "--no-restore-playback-states",
        "--no-video", "--aout=dummy", "--clock-master=monotonic",
    };
    libvlc_instance_t *instance = libvlc_new(ARRAY_SIZE(args), args);
    assert(instance);
    return instance;
}

static void check_files(vlc_medialibrary_t *ml, int64_t id,
                        const char *main, const char *subtitle,
                        const char *audio)
{
    vlc_ml_media_t *media = vlc_ml_get_media(ml, id);
    assert(media && media->p_files);
    assert(media->b_subtitle_track_selection);
    assert(media->p_files->i_nb_items == 1u + !!subtitle + !!audio);
    unsigned main_count = 0, subtitle_count = 0, audio_count = 0;
    for (size_t i = 0; i < media->p_files->i_nb_items; ++i)
    {
        const vlc_ml_file_t *file = &media->p_files->p_items[i];
        assert(!file->b_unselected);
        switch (file->i_type)
        {
            case VLC_ML_FILE_TYPE_MAIN:
                assert(!strcmp(file->psz_mrl, main));
                ++main_count;
                break;
            case VLC_ML_FILE_TYPE_SUBTITLE:
                assert(subtitle && !strcmp(file->psz_mrl, subtitle));
                ++subtitle_count;
                break;
            case VLC_ML_FILE_TYPE_SOUNDTRACK:
                assert(audio && !strcmp(file->psz_mrl, audio));
                ++audio_count;
                break;
            default:
                assert(!"Unexpected file type");
        }
    }
    assert(main_count == 1 && subtitle_count == !!subtitle && audio_count == !!audio);
    vlc_ml_media_release(media);
}

static void write_u32(FILE *file, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        assert(fputc((value >> (8 * i)) & 0xff, file) != EOF);
}

static void wait_stopped(vlc_player_t *player)
{
    const vlc_tick_t deadline = vlc_tick_now() + VLC_TICK_FROM_SEC(5);
    for (;;)
    {
        vlc_player_Lock(player);
        const bool stopped = vlc_player_GetState(player) == VLC_PLAYER_STATE_STOPPED;
        vlc_player_Unlock(player);
        if (stopped) return;
        assert(vlc_tick_now() < deadline);
        vlc_tick_sleep(VLC_TICK_FROM_MS(20));
    }
}

static void play_tracks(vlc_player_t *player, input_item_t *item, size_t subtitles)
{
    vlc_player_Lock(player);
    assert(vlc_player_SetCurrentMedia(player, item) == VLC_SUCCESS);
    assert(vlc_player_Start(player) == VLC_SUCCESS);
    vlc_player_Unlock(player);
    const vlc_tick_t deadline = vlc_tick_now() + VLC_TICK_FROM_SEC(5);
    for (;;)
    {
        vlc_player_Lock(player);
        const bool playing = vlc_player_GetState(player) == VLC_PLAYER_STATE_PLAYING &&
                             vlc_player_GetTime(player) >= VLC_TICK_FROM_MS(200);
        if (playing)
        {
            assert(vlc_player_GetTrackCount(player, SPU_ES) == subtitles);
            vlc_player_Stop(player);
            vlc_player_Unlock(player);
            break;
        }
        vlc_player_Unlock(player);
        assert(vlc_tick_now() < deadline);
        vlc_tick_sleep(VLC_TICK_FROM_MS(20));
    }
    wait_stopped(player);
}

static void check_folder_rules(libvlc_instance_t *instance,
                               vlc_medialibrary_t *ml, const char *directory,
                               const char *wave_path)
{
    char *source, *sample, *caption, *future, *definition;
    assert(asprintf(&source, "%s/captions", directory) >= 0);
    assert(vlc_mkdir(source, 0700) == 0);
    assert(asprintf(&sample, "%s/show_s01e01.en.srt", source) >= 0);
    assert(asprintf(&caption, "%s/show_s01e02.en.srt", source) >= 0);
    assert(asprintf(&future, "%s/show_s01e02.wav", directory) >= 0);
    const char *name = "show_s01e01.wav";
    assert(asprintf(&definition, "1|%zu:%s%zu:%s%zu:%s",
                    strlen(name), name, strlen(sample), sample, strlen(source), source) >= 0);
    char *native_directory = strdup(directory);
    assert(native_directory);
#ifdef _WIN32
    for (char *p = native_directory; *p; ++p) if (*p == '/') *p = '\\';
    for (char *p = future; *p; ++p) if (*p == '/') *p = '\\';
    for (char *p = caption; *p; ++p) if (*p == '/') *p = '\\';
#endif
    char *folder = vlc_path2uri(native_directory, NULL);
    char *uri = vlc_path2uri(future, NULL);
    char *caption_uri = vlc_path2uri(caption, NULL);
    free(native_directory);
    assert(folder && uri && caption_uri);
    assert(vlc_ml_folder_set_track_rule(ml, folder, VLC_ML_FILE_TYPE_SUBTITLE, definition) == VLC_SUCCESS);
    char *saved = NULL;
    assert(vlc_ml_folder_get_track_rule(ml, folder, VLC_ML_FILE_TYPE_SUBTITLE, &saved) == VLC_SUCCESS);
    assert(saved && !strcmp(saved, definition)); free(saved);

    /* The episode and its subtitle appear after the rule is saved. */
    FILE *from = vlc_fopen(wave_path, "rb"), *to = vlc_fopen(future, "wb");
    assert(from && to);
    char bytes[4096]; size_t count;
    while ((count = fread(bytes, 1, sizeof(bytes), from)) > 0)
        assert(fwrite(bytes, 1, count, to) == count);
    assert(!ferror(from)); assert(fclose(from) == 0); assert(fclose(to) == 0);
    to = vlc_fopen(caption, "wb"); assert(to);
    assert(fputs("1\n00:00:00,000 --> 00:00:04,000\nFolder subtitle\n", to) >= 0);
    assert(fclose(to) == 0);

    bool applies = false; vlc_ml_file_list_t *matches = NULL;
    assert(vlc_ml_media_get_folder_tracks(ml, uri, VLC_ML_FILE_TYPE_SUBTITLE, &applies, &matches) == VLC_SUCCESS);
    assert(applies && matches && matches->i_nb_items == 1);
    assert(!strcmp(matches->p_items[0].psz_mrl, caption_uri)); vlc_ml_file_list_release(matches);
    vlc_player_t *player = vlc_player_New(VLC_OBJECT(instance->p_libvlc_int), VLC_PLAYER_LOCK_NORMAL);
    input_item_t *item = input_item_New(uri, "Future episode test");
    assert(player && item);
    play_tracks(player, item, 1);
    assert(vlc_ml_folder_set_track_rule(ml, folder, VLC_ML_FILE_TYPE_SUBTITLE, NULL) == VLC_SUCCESS);
    play_tracks(player, item, 0); // A cached inherited source must be discarded.
    assert(vlc_ml_folder_set_track_rule(ml, folder, VLC_ML_FILE_TYPE_SUBTITLE, definition) == VLC_SUCCESS);
    play_tracks(player, item, 1);
    vlc_ml_media_t *media = vlc_ml_get_media_by_mrl(ml, uri);
    if (!media) media = vlc_ml_new_external_media(ml, uri);
    assert(media && !media->b_subtitle_track_selection);
    const int64_t id = media->i_id; vlc_ml_media_release(media);
    assert(vlc_ml_media_set_external_mrls(ml, id, VLC_ML_FILE_TYPE_SUBTITLE, NULL, 0) == VLC_SUCCESS);
    play_tracks(player, item, 0); // An explicit empty choice overrides the rule.
    assert(vlc_ml_media_set_external_mrls(ml, id, VLC_ML_FILE_TYPE_SUBTITLE,
                                         (const char* const[]){caption_uri}, 1) == VLC_SUCCESS);
    assert(vlc_ml_folder_set_track_rule(ml, folder, VLC_ML_FILE_TYPE_SUBTITLE, NULL) == VLC_SUCCESS);
    play_tracks(player, item, 1); // A pinned choice survives rule removal.
    vlc_player_Delete(player); input_item_Release(item);
    free(source); free(sample); free(caption); free(future); free(definition);
    free(folder); free(uri); free(caption_uri);
    fprintf(stderr, "Folder rules, future episodes, cached replay and explicit overrides passed\n");
}

static void check_subtitle_discovery(libvlc_instance_t *instance,
                                     vlc_medialibrary_t *ml, const char *directory)
{
    char *audio_path, *subtitle_path;
    assert(asprintf(&audio_path, "%s/automatic.wav", directory) >= 0);
    assert(asprintf(&subtitle_path, "%s/automatic.srt", directory) >= 0);
    FILE *file = vlc_fopen(audio_path, "wb");
    assert(file);
    assert(fwrite("RIFF", 1, 4, file) == 4); write_u32(file, 36 + 80000);
    assert(fwrite("WAVEfmt ", 1, 8, file) == 8); write_u32(file, 16);
    assert(fwrite("\1\0\1\0", 1, 4, file) == 4); // PCM, mono.
    write_u32(file, 8000); write_u32(file, 16000);
    assert(fwrite("\2\0\20\0data", 1, 8, file) == 8); write_u32(file, 80000);
    const char silence[16000] = {0};
    for (unsigned i = 0; i < 5; ++i)
        assert(fwrite(silence, 1, sizeof(silence), file) == sizeof(silence));
    assert(fclose(file) == 0);
    file = vlc_fopen(subtitle_path, "wb");
    assert(file);
    assert(fputs("1\n00:00:00,000 --> 00:00:04,000\nAutomatic subtitle\n", file) >= 0);
    assert(fclose(file) == 0);
    char *audio = vlc_path2uri(audio_path, NULL);
    char *subtitle = vlc_path2uri(subtitle_path, NULL);
    assert(audio && subtitle);
    vlc_ml_media_t *media = vlc_ml_new_external_media(ml, audio);
    assert(media);
    const int64_t id = media->i_id;
    assert(!media->b_subtitle_track_selection);
    vlc_ml_media_release(media);
    vlc_player_t *player = vlc_player_New(VLC_OBJECT(instance->p_libvlc_int), VLC_PLAYER_LOCK_NORMAL);
    input_item_t *item = input_item_New(audio, "Automatic subtitle test");
    assert(player && item);
    play_tracks(player, item, 1); // Prove automatic discovery works in this test.
    assert(vlc_ml_media_set_external_mrls(ml, id,
            VLC_ML_FILE_TYPE_SUBTITLE, NULL, 0) == VLC_SUCCESS);
    play_tracks(player, item, 0); // Reuse the item that cached the discovered subtitle.
    assert(vlc_ml_media_set_external_mrls(ml, id,
            VLC_ML_FILE_TYPE_SUBTITLE, (const char* const[]){subtitle}, 1) == VLC_SUCCESS);
    play_tracks(player, item, 1);
    vlc_player_Delete(player);
    input_item_Release(item);
    assert(vlc_stat(subtitle_path, &(struct stat){0}) == 0);
    check_folder_rules(instance, ml, directory, audio_path);
    free(audio); free(subtitle); free(audio_path); free(subtitle_path);
    fprintf(stderr, "Automatic subtitles, empty saved choice and cached-item replay passed\n");
}

int main(int argc, char **argv)
{
    /* A Windows cross-build can supply a host path. Otherwise use the build
     * directory. Never use the user's media library or configuration. */
    const char *base = argc > 1 ? argv[1] : TOP_BUILDDIR "/test";
    char *directory;
    assert(asprintf(&directory, "%s/media-tracks-%"PRId64,
                    base, vlc_tick_now()) >= 0);
    assert(vlc_mkdir(directory, 0700) == 0);
    char *nested;
    assert(asprintf(&nested, "%s/nested/track folders", directory) >= 0);
#ifdef _WIN32
    for (char *p = nested; *p; ++p) if (*p == '/') *p = '\\';
#endif
    assert(vlc_mkdir_parent(nested, 0700) == 0);
    assert(vlc_mkdir_parent(nested, 0700) == 0);
    struct stat nested_stat;
    assert(vlc_stat(nested, &nested_stat) == 0 && S_ISDIR(nested_stat.st_mode));
#ifdef _WIN32
    char *extended;
    assert(asprintf(&extended, "%s%s\\more\\tracks",
                    !strncmp(nested, "\\\\", 2) ? "\\\\?\\UNC\\" : "\\\\?\\",
                    nested + (!strncmp(nested, "\\\\", 2) ? 2 : 0)) >= 0);
    assert(vlc_mkdir_parent(extended, 0700) == 0);
    free(extended);
#endif
    free(nested);
    assert(setenv("VLC_USERDATA_PATH", directory, 1) == 0);
    fprintf(stderr, "Using owned media library: %s\n", directory);

    char *path;
    assert(asprintf(&path, "%s/keep captions & accents.srt", directory) >= 0);
    FILE *file = vlc_fopen(path, "wb");
    assert(file);
    assert(fputs("Keep this disk file", file) >= 0);
    assert(fclose(file) == 0);
    char *subtitle = vlc_path2uri(path, NULL);
    assert(subtitle);

    /* The library is opened twice. Keep one LibVLC instance alive so the
     * plugin bank loads once: on macOS each plugin load costs seconds. */
    const char *keep_args[] = {"--quiet", "--ignore-config", "--no-media-library"};
    libvlc_instance_t *keep = libvlc_new(ARRAY_SIZE(keep_args), keep_args);
    assert(keep);

    libvlc_instance_t *instance = open_library();
    vlc_medialibrary_t *ml = vlc_ml_instance_get(instance->p_libvlc_int);
    assert(ml);
    vlc_ml_media_t *first = vlc_ml_new_external_media(ml, "file:///first.mkv");
    vlc_ml_media_t *second = vlc_ml_new_external_media(ml, "file:///second.mkv");
    assert(first && second);
    const int64_t first_id = first->i_id, second_id = second->i_id;
    vlc_ml_media_release(first);
    vlc_ml_media_release(second);

    /* Source builds with the unpatched dependency do not have this operation. */
    if (vlc_ml_media_set_external_mrls(ml, first_id,
            VLC_ML_FILE_TYPE_SUBTITLE, NULL, 0) == VLC_ENOTSUP)
    {
        fprintf(stderr, "External track replacement is unavailable\n");
        libvlc_release(instance);
        free(subtitle);
        free(path);
        free(directory);
        return 77;
    }

    const char *audio = "file:///sound.wav";
    assert(vlc_ml_media_add_external_mrl(ml, first_id, audio,
                                         VLC_ML_FILE_TYPE_SOUNDTRACK) == VLC_SUCCESS);
    assert(vlc_ml_media_add_external_mrl(ml, first_id, "file:///old.srt",
                                         VLC_ML_FILE_TYPE_SUBTITLE) == VLC_SUCCESS);
    const char *tracks[] = { subtitle, subtitle };
    assert(vlc_ml_media_set_external_mrls(ml, first_id,
            VLC_ML_FILE_TYPE_SUBTITLE, tracks, ARRAY_SIZE(tracks)) == VLC_SUCCESS);
    assert(vlc_ml_media_set_external_mrls(ml, second_id,
            VLC_ML_FILE_TYPE_SUBTITLE, tracks, 1) == VLC_SUCCESS);
    check_files(ml, first_id, "file:///first.mkv", subtitle, audio);
    check_files(ml, second_id, "file:///second.mkv", subtitle, NULL);

    const char *invalid[][1] = { { NULL }, { "" }, { "file:///first.mkv" }, { audio } };
    for (size_t i = 0; i < ARRAY_SIZE(invalid); ++i)
    {
        assert(vlc_ml_media_set_external_mrls(ml, first_id,
                VLC_ML_FILE_TYPE_SUBTITLE, invalid[i], 1) != VLC_SUCCESS);
        check_files(ml, first_id, "file:///first.mkv", subtitle, audio);
    }
    assert(vlc_ml_media_set_external_mrls(ml, first_id,
            VLC_ML_FILE_TYPE_SUBTITLE, NULL, 1) != VLC_SUCCESS);
    assert(vlc_ml_media_set_external_mrls(ml, first_id,
            VLC_ML_FILE_TYPE_MAIN, NULL, 0) != VLC_SUCCESS);

    libvlc_release(instance);
    instance = open_library();
    ml = vlc_ml_instance_get(instance->p_libvlc_int);
    assert(ml);
    check_files(ml, first_id, "file:///first.mkv", subtitle, audio);
    check_files(ml, second_id, "file:///second.mkv", subtitle, NULL);
    assert(vlc_ml_media_set_external_mrls(ml, first_id,
            VLC_ML_FILE_TYPE_SUBTITLE, NULL, 0) == VLC_SUCCESS);
    check_files(ml, first_id, "file:///first.mkv", NULL, audio);
    check_files(ml, second_id, "file:///second.mkv", subtitle, NULL);
    assert(vlc_ml_media_set_external_mrls(ml, first_id,
            VLC_ML_FILE_TYPE_SOUNDTRACK, NULL, 0) == VLC_SUCCESS);
    check_files(ml, first_id, "file:///first.mkv", NULL, NULL);
    check_subtitle_discovery(instance, ml, directory);
    libvlc_release(instance);

    file = vlc_fopen(path, "rb");
    assert(file);
    assert(fclose(file) == 0);
    free(subtitle);
    free(path);
    free(directory);
    libvlc_release(keep);
    /* Retain only this test's database and file under the build directory for
     * inspection. The test runner supplies the process timeout. */
    fprintf(stderr, "Track replacement, validation, restart and disk preservation passed\n");
    return 0;
}

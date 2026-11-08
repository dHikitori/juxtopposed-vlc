// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <vlc/vlc.h>
#include <vlc_common.h>
#include <vlc_log_observer.h>
#include <vlc_fs.h>
#include "../../../lib/libvlc_internal.h"
#include "../../libvlc/test.h"
#undef NDEBUG
#include <assert.h>
#include <unistd.h>

const char vlc_module_name[] = "file_logger_test";
enum { INFO = 1u << VLC_MSG_INFO, ERROR = 1u << VLC_MSG_ERR,
       WARN = 1u << VLC_MSG_WARN, DEBUG = 1u << VLC_MSG_DBG };

static void observe(void *opaque, int type, const vlc_log_t *meta,
                    const char *format, va_list args)
{
    (void)format; (void)args;
    if (!strcmp(meta->psz_module, vlc_module_name))
        *(unsigned *)opaque |= 1u << type;
}

static void check(const char *mode, const char *level, int verbose, int log_verbose,
                  bool enabled, unsigned expected)
{
    char name[] = "file-log-level-XXXXXX";
    int fd = vlc_mkstemp(name); assert(fd >= 0);
    vlc_close(fd); assert(vlc_unlink(name) == 0);
    char *path, *format, *severity, *verbosity, *legacy;
    assert(asprintf(&path, "--logfile=%s", name) >= 0);
    assert(asprintf(&format, "--logmode=%s", mode) >= 0);
    assert(asprintf(&severity, "--log-level=%s", level) >= 0);
    assert(asprintf(&verbosity, "--verbose=%d", verbose) >= 0);
    assert(asprintf(&legacy, "--log-verbose=%d", log_verbose) >= 0);
    const char *options[] = {"--ignore-config", "--no-media-library", "--no-metadata-network-access",
        "--no-plugins-cache", enabled ? "--file-logging" : "--no-file-logging",
        path, format, severity, verbosity, legacy};
    libvlc_instance_t *vlc = libvlc_new(ARRAY_SIZE(options), options); assert(vlc);
    unsigned observed = 0;
    static const struct vlc_logger_operations ops = {observe, NULL};
    struct vlc_log_subscription *sub = vlc_LogSubscribe(vlc->p_libvlc_int, &ops, &observed); assert(sub);
    msg_Info(vlc->p_libvlc_int, "file-level-marker-info");
    msg_Err(vlc->p_libvlc_int, "file-level-marker-error");
    msg_Warn(vlc->p_libvlc_int, "file-level-marker-warning");
    msg_Dbg(vlc->p_libvlc_int, "file-level-marker-debug");
    vlc_LogUnsubscribe(sub);
    assert(observed == (INFO | ERROR | WARN | DEBUG));
    libvlc_release(vlc);
    FILE *file = vlc_fopen(name, "rb");
    if (expected == 0) assert(file == NULL);
    else
    {
        assert(file != NULL);
        unsigned found = 0;
        char line[4096];
        bool header = false, footer = false;
        while (fgets(line, sizeof(line), file))
        {
            if (strstr(line, "file-level-marker-info")) found |= INFO;
            if (strstr(line, "file-level-marker-error")) found |= ERROR;
            if (strstr(line, "file-level-marker-warning")) found |= WARN;
            if (strstr(line, "file-level-marker-debug")) found |= DEBUG;
            header |= strstr(line, "logger module started") != NULL;
            footer |= strstr(line, "logger module stopped") != NULL;
        }
        fclose(file);
        if (found != expected)
            fprintf(stderr, "format=%s level=%s verbose=%d log-verbose=%d found=%u expected=%u\n",
                    mode, level, verbose, log_verbose, found, expected);
        assert(found == expected && header && footer);
        vlc_unlink(name);
    }
    free(path); free(format); free(severity); free(verbosity); free(legacy);
}

int main(void)
{
    test_init();
    /* Each scenario starts its own libvlc. Keep one instance alive so the
     * plugin bank loads once: on macOS each plugin load costs seconds. */
    const char *keep_args[] = {"--ignore-config", "--no-media-library",
                               "--no-metadata-network-access"};
    libvlc_instance_t *keep = libvlc_new(ARRAY_SIZE(keep_args), keep_args);
    assert(keep);
    const char *modes[] = {"text", "html"};
    const struct {const char *level; unsigned mask;} levels[] = {
        {"errors", ERROR}, {"warnings", ERROR | WARN}, {"info", INFO | ERROR | WARN},
        {"debug", INFO | ERROR | WARN | DEBUG}, {"silent", 0}
    };
    for (size_t m = 0; m < ARRAY_SIZE(modes); ++m)
    {
        // Native defaults retain their historical verbosity and override rules.
        check(modes[m], "", -1, -1, true, 0);
        check(modes[m], "", 0, -1, true, INFO | ERROR);
        check(modes[m], "", 1, -1, true, INFO | ERROR | WARN);
        check(modes[m], "", 2, -1, true, INFO | ERROR | WARN | DEBUG);
        check(modes[m], "", 2, 0, true, INFO | ERROR);
        check(modes[m], "", 2, 1, true, INFO | ERROR | WARN);
        check(modes[m], "", 0, 3, true, INFO | ERROR | WARN | DEBUG);
        for (size_t l = 0; l < ARRAY_SIZE(levels); ++l)
            for (int verbose = -1; verbose <= 2; verbose += verbose == -1 ? 1 : 2)
                check(modes[m], levels[l].level, verbose, -1, true, levels[l].mask);
        check(modes[m], "errors", 2, 3, true, ERROR);
        check(modes[m], "debug", 0, 0, true, INFO | ERROR | WARN | DEBUG);
        check(modes[m], "invalid", 1, -1, true, INFO | ERROR | WARN);
        check(modes[m], "debug", 2, 3, false, 0);
    }
    libvlc_release(keep);
    puts("52 file-logger scenarios passed");
    return 0;
}

// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <vlc/vlc.h>
#include <vlc_common.h>
#include <vlc_interface.h>
#include <vlc_log_observer.h>
#include <vlc_threads.h>
#include <vlc_tick.h>
#include <vlc_fs.h>
#include "../../../lib/libvlc_internal.h"
#include "../../libvlc/test.h"
#undef NDEBUG
#include <assert.h>
#include <stdatomic.h>
#include <unistd.h>

const char vlc_module_name[] = "log_observer_test";

struct capture {
    vlc_mutex_t lock;
    vlc_cond_t condition;
    unsigned count;
    char text[128];
    bool block, entered, released;
    atomic_bool destroyed;
};

static void capture_init(struct capture *c)
{
    memset(c, 0, sizeof(*c));
    vlc_mutex_init(&c->lock);
    vlc_cond_init(&c->condition);
    atomic_init(&c->destroyed, false);
}

static void capture_log(void *opaque, int type, const vlc_log_t *meta,
                        const char *format, va_list args)
{
    struct capture *c = opaque;
    (void)type;
    if (strcmp(meta->psz_module, "log_observer_test")) return;
    assert(!atomic_load(&c->destroyed));
    vlc_mutex_lock(&c->lock);
    ++c->count;
    vsnprintf(c->text, sizeof(c->text), format, args);
    if (c->block && !strcmp(c->text, "held-message")) {
        c->entered = true;
        vlc_cond_signal(&c->condition);
        while (!c->released) vlc_cond_wait(&c->condition, &c->lock);
    }
    vlc_mutex_unlock(&c->lock);
}

static void capture_destroy(void *opaque)
{
    struct capture *c = opaque;
    assert(!atomic_exchange(&c->destroyed, true));
}

static const struct vlc_logger_operations ops = {capture_log, capture_destroy};

static libvlc_instance_t *instance(void)
{
    const char *args[] = {"--ignore-config", "--no-media-library", "--no-metadata-network-access", "--no-plugins-cache"};
    libvlc_instance_t *vlc = libvlc_new(ARRAY_SIZE(args), args);
    assert(vlc != NULL);
    return vlc;
}

static void test_fanout(void)
{
    libvlc_instance_t *vlc = instance();
    struct capture base, a, b;
    capture_init(&base); capture_init(&a); capture_init(&b);
    vlc_LogSet(vlc->p_libvlc_int, &ops, &base);
    unsigned before = base.count;
    struct vlc_log_subscription *sa = vlc_LogSubscribe(vlc->p_libvlc_int, &ops, &a);
    struct vlc_log_subscription *sb = vlc_LogSubscribe(vlc->p_libvlc_int, &ops, &b);
    assert(sa && sb);
    msg_Warn(vlc->p_libvlc_int, "sample-%d:%s", 7, "tail");
    assert(base.count == before + 1 && a.count == 1 && b.count == 1);
    assert(!strcmp(base.text, "sample-7:tail"));
    assert(!strcmp(a.text, base.text) && !strcmp(b.text, base.text));
    vlc_LogUnsubscribe(sa);
    assert(atomic_load(&a.destroyed));
    msg_Warn(vlc->p_libvlc_int, "remaining-%u", 9u);
    assert(a.count == 1 && b.count == 2);
    assert(!strcmp(b.text, "remaining-9"));
    vlc_LogUnsubscribe(sb);
    vlc_LogUnsubscribe(NULL);
    assert(atomic_load(&b.destroyed));
    libvlc_release(vlc);
    assert(atomic_load(&base.destroyed));
}

static void test_backend_switch(void)
{
    libvlc_instance_t *vlc = instance();
    struct capture old, replacement, observer;
    capture_init(&old); capture_init(&replacement); capture_init(&observer);
    vlc_LogSet(vlc->p_libvlc_int, &ops, &old);
    struct vlc_log_subscription *sub = vlc_LogSubscribe(vlc->p_libvlc_int, &ops, &observer);
    assert(sub);
    vlc_LogSet(vlc->p_libvlc_int, &ops, &replacement);
    assert(atomic_load(&old.destroyed));
    unsigned before = observer.count;
    msg_Err(vlc->p_libvlc_int, "new-backend-%d", 3);
    assert(observer.count == before + 1);
    assert(!strcmp(observer.text, "new-backend-3"));
    assert(!strcmp(replacement.text, observer.text));
    vlc_LogSet(vlc->p_libvlc_int, NULL, NULL);
    assert(atomic_load(&replacement.destroyed));
    before = observer.count;
    msg_Warn(vlc->p_libvlc_int, "observer-without-backend");
    assert(observer.count == before + 1);
    vlc_LogUnsubscribe(sub);
    libvlc_release(vlc);
}

struct writer { libvlc_int_t *vlc; unsigned messages; };
static void *write_messages(void *opaque)
{
    struct writer *w = opaque;
    if (w->messages == 1) msg_Warn(w->vlc, "held-message");
    else for (unsigned i = 0; i < w->messages; ++i) msg_Warn(w->vlc, "concurrent-%u", i);
    return NULL;
}

struct unsubscriber {
    struct vlc_log_subscription *sub;
    vlc_sem_t started;
    atomic_bool finished;
};
static void *stop_observation(void *opaque)
{
    struct unsubscriber *u = opaque;
    vlc_sem_post(&u->started);
    vlc_LogUnsubscribe(u->sub);
    atomic_store(&u->finished, true);
    return NULL;
}

static void test_inflight(void)
{
    libvlc_instance_t *vlc = instance();
    vlc_LogSet(vlc->p_libvlc_int, NULL, NULL);
    struct capture c; capture_init(&c); c.block = true;
    struct unsubscriber u = {.sub = vlc_LogSubscribe(vlc->p_libvlc_int, &ops, &c)};
    assert(u.sub); vlc_sem_init(&u.started, 0); atomic_init(&u.finished, false);
    struct writer w = {vlc->p_libvlc_int, 1};
    vlc_thread_t writer, remover;
    assert(vlc_clone(&writer, write_messages, &w) == 0);
    vlc_mutex_lock(&c.lock);
    while (!c.entered) vlc_cond_wait(&c.condition, &c.lock);
    vlc_mutex_unlock(&c.lock);
    assert(vlc_clone(&remover, stop_observation, &u) == 0);
    vlc_sem_wait(&u.started);
    vlc_mutex_lock(&c.lock);
    const vlc_tick_t deadline = vlc_tick_now() + VLC_TICK_FROM_MS(20);
    while (vlc_cond_timedwait(&c.condition, &c.lock, deadline) == 0) {}
    assert(!atomic_load(&u.finished) && !atomic_load(&c.destroyed));
    c.released = true; vlc_cond_signal(&c.condition); vlc_mutex_unlock(&c.lock);
    vlc_join(writer, NULL); vlc_join(remover, NULL);
    assert(atomic_load(&u.finished) && atomic_load(&c.destroyed));
    msg_Warn(vlc->p_libvlc_int, "after-unsubscribe");
    assert(c.count == 1);
    libvlc_release(vlc);
}

static void test_concurrent_changes(void)
{
    libvlc_instance_t *vlc = instance();
    vlc_LogSet(vlc->p_libvlc_int, NULL, NULL);
    struct writer w = {vlc->p_libvlc_int, 2000}; vlc_thread_t writer;
    assert(vlc_clone(&writer, write_messages, &w) == 0);
    for (unsigned i = 0; i < 64; ++i) {
        struct capture c; capture_init(&c);
        struct vlc_log_subscription *sub = vlc_LogSubscribe(vlc->p_libvlc_int, &ops, &c);
        assert(sub); vlc_LogUnsubscribe(sub); assert(atomic_load(&c.destroyed));
    }
    vlc_join(writer, NULL);
    libvlc_release(vlc);
}

static void test_slow_backend(void)
{
    libvlc_instance_t *vlc = instance();
    struct capture backend, observer; capture_init(&backend); capture_init(&observer);
    backend.block = true;
    vlc_LogSet(vlc->p_libvlc_int, &ops, &backend);
    struct unsubscriber u = {.sub = vlc_LogSubscribe(vlc->p_libvlc_int, &ops, &observer)};
    assert(u.sub); vlc_sem_init(&u.started, 0); atomic_init(&u.finished, false);
    struct writer w = {vlc->p_libvlc_int, 1}; vlc_thread_t writer, remover;
    assert(vlc_clone(&writer, write_messages, &w) == 0);
    vlc_mutex_lock(&backend.lock);
    while (!backend.entered) vlc_cond_wait(&backend.condition, &backend.lock);
    vlc_mutex_unlock(&backend.lock);
    assert(vlc_clone(&remover, stop_observation, &u) == 0);
    vlc_sem_wait(&u.started);
    vlc_mutex_lock(&backend.lock);
    const vlc_tick_t deadline = vlc_tick_now() + VLC_TICK_FROM_MS(100);
    while (vlc_cond_timedwait(&backend.condition, &backend.lock, deadline) == 0) {}
    bool stopped_before_io = atomic_load(&u.finished);
    backend.released = true; vlc_cond_signal(&backend.condition); vlc_mutex_unlock(&backend.lock);
    vlc_join(writer, NULL); vlc_join(remover, NULL);
    assert(stopped_before_io && observer.count == 0);
    libvlc_release(vlc);
}

static void test_file_backend(void)
{
    char name[] = "log-observer-XXXXXX";
    int fd = vlc_mkstemp(name); assert(fd >= 0); vlc_close(fd);
    char *logfile; assert(asprintf(&logfile, "--logfile=%s", name) >= 0);
    const char *args[] = {"--ignore-config", "--no-media-library", "--no-metadata-network-access",
        "--no-plugins-cache", "--file-logging", "--verbose=2", logfile};
    libvlc_instance_t *vlc = libvlc_new(ARRAY_SIZE(args), args); assert(vlc);
    struct capture c; capture_init(&c);
    msg_Warn(vlc->p_libvlc_int, "file-before-observer");
    struct vlc_log_subscription *sub = vlc_LogSubscribe(vlc->p_libvlc_int, &ops, &c); assert(sub);
    msg_Warn(vlc->p_libvlc_int, "file-during-observer");
    assert(c.count == 1);
    vlc_LogUnsubscribe(sub);
    msg_Warn(vlc->p_libvlc_int, "file-after-observer");
    libvlc_release(vlc);
    FILE *file = vlc_fopen(name, "rb"); assert(file);
    bool before = false, during = false, after = false;
    char line[2048];
    while (fgets(line, sizeof(line), file)) {
        before |= strstr(line, "file-before-observer") != NULL;
        during |= strstr(line, "file-during-observer") != NULL;
        after |= strstr(line, "file-after-observer") != NULL;
    }
    fclose(file); assert(before && during && after);
    vlc_unlink(name); free(logfile);
}

static void test_shutdown(void)
{
    libvlc_instance_t *vlc = instance();
    struct capture c; capture_init(&c);
    struct vlc_log_subscription *sub = vlc_LogSubscribe(vlc->p_libvlc_int, &ops, &c);
    assert(sub);
    libvlc_release(vlc);
    assert(atomic_load(&c.destroyed));
}

int main(void)
{
#ifndef _WIN32
    alarm(20);
#endif
    test_setup();
    /* Each case starts its own libvlc. Keep one instance alive so the plugin
     * bank loads once: on macOS each plugin load costs seconds. */
    const char *keep_args[] = {"--ignore-config", "--no-media-library"};
    libvlc_instance_t *keep = libvlc_new(ARRAY_SIZE(keep_args), keep_args);
    assert(keep != NULL);
    test_fanout(); test_backend_switch(); test_inflight();
    test_concurrent_changes(); test_slow_backend(); test_file_backend(); test_shutdown();
    libvlc_release(keep);
    puts("7 log-observer scenarios passed");
    return 0;
}

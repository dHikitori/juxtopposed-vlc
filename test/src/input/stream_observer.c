// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <vlc_common.h>
#include <vlc_interrupt.h>
#include <vlc_threads.h>
#include <vlc_variables.h>
#include <vlc/vlc.h>
#include "../../../lib/libvlc_internal.h"
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <zlib.h>

const char vlc_module_name[] = "native-artwork-observer-test";

/* The fixture uses the real Core read code and native observer. Two separate
 * units compile the actual inflate constructor and prefetch worker. A fresh
 * Core must have the same private object layout. No source I/O is replaced
 * with a successful test result: the source supplies bytes, errors, and EOF.
 */
#include "../../../src/libvlc.h"
#include "../../../src/input/stream_observer.h"
#include "../../../src/misc/variables.h"
#include <vlc_stream.h>
#ifdef VLC_TEST_STREAM_OBSERVER_REAL_SOURCE
# include "../../../src/input/stream_observer.c"
# include "../../../src/input/stream.c"
#endif

enum { FIXTURE_STREAM_OBSERVER_LIMIT = 64 };

int meta_art_prefetch_open(stream_t *);
void meta_art_prefetch_close(stream_t *);
int meta_art_inflate_open(stream_t *);
void meta_art_inflate_close(stream_t *);

enum finish_mode { CLEAN_EOF, ERROR_THEN_EOF, ERROR_UNTIL_INTERRUPT };
struct source {
    const char *bytes;
    size_t size, offset;
    enum finish_mode finish;
    unsigned calls, errors, destroyed;
    bool constructor_error, constructor_failed;
    unsigned long reader_thread;
    vlc_interrupt_t *interrupt;
};
static block_t *source_block(stream_t *, bool *);

static ssize_t source_read(stream_t *stream, void *buffer, size_t size)
{
    struct source *source = stream->p_sys;
    ++source->calls;
    source->reader_thread = vlc_thread_id();
    if (source->constructor_error && source->offset == 1 && !source->constructor_failed) {
        source->constructor_failed = true;
        errno = EIO;
        return -1;
    }
    if (source->offset < source->size) {
        size_t count = source->size - source->offset;
        if (count > size) count = size;
        if (source->constructor_error && source->offset == 0) count = 1;
        memcpy(buffer, source->bytes + source->offset, count);
        source->offset += count;
        return count;
    }
    if (source->finish == ERROR_THEN_EOF && source->errors++ == 0) {
        errno = EIO;
        return -1;
    }
    if (source->finish == ERROR_UNTIL_INTERRUPT) {
        if (++source->errors == 3) vlc_interrupt_kill(source->interrupt);
        errno = EIO;
        return -1;
    }
    return 0;
}

static int source_control(stream_t *stream, int query, va_list args)
{
    struct source *source = stream->p_sys;
    switch (query) {
        case STREAM_CAN_SEEK:
        case STREAM_CAN_FASTSEEK:
        case STREAM_CAN_PAUSE:
        case STREAM_CAN_CONTROL_PACE:
            *va_arg(args, bool *) = false;
            return VLC_SUCCESS;
        case STREAM_GET_SIZE:
            *va_arg(args, uint64_t *) = source->size;
            return VLC_SUCCESS;
        case STREAM_GET_PTS_DELAY:
            *va_arg(args, vlc_tick_t *) = 0;
            return VLC_SUCCESS;
        default:
            return VLC_EGENERIC;
    }
}

static void source_destroy(stream_t *stream)
{
    ++((struct source *)stream->p_sys)->destroyed;
}

static stream_t *source_new(vlc_object_t *parent, struct source *source)
{
    stream_t *stream = vlc_stream_CommonNew(parent, source_destroy);
    assert(stream != NULL);
    stream->p_sys = source;
    stream->pf_read = source_read;
    stream->pf_control = source_control;
    return stream;
}

struct scope {
    vlc_object_t *parent;
    vlc_stream_observer_t *observer;
};

static struct scope scope_open(vlc_object_t *instance)
{
    struct scope scope = {0};
    scope.parent = vlc_object_create(instance, sizeof(*scope.parent));
    assert(scope.parent != NULL);
    scope.observer = vlc_stream_observer_New();
    assert(scope.observer != NULL);
    assert(vlc_stream_observer_AttachObject(scope.parent, scope.observer) == VLC_SUCCESS);
    return scope;
}

static bool scope_close(struct scope *scope)
{
    const bool valid = vlc_stream_observer_Seal(scope->observer, false);
    vlc_stream_observer_Release(scope->observer);
    vlc_object_delete(scope->parent);
    return valid;
}

static void read_outcome(vlc_object_t *instance, enum finish_mode finish, bool unread)
{
    struct scope scope = scope_open(instance);
    struct source source = {.bytes=unread ? "empty-tail" : "empty", .size=unread ? 10 : 5, .finish=finish};
    source.interrupt = vlc_interrupt_create();
    assert(source.interrupt != NULL);
    vlc_interrupt_t *previous = vlc_interrupt_set(source.interrupt);
    stream_t *input = source_new(scope.parent, &source);
    char bytes[64];
    assert(vlc_stream_Read(input, bytes, unread ? 5 : sizeof(bytes)) == 5);
    assert(!memcmp(bytes, "empty", 5));
    if (finish == ERROR_THEN_EOF) assert(source.calls == 3);
    if (finish == ERROR_UNTIL_INTERRUPT) assert(source.calls == 4 && source.errors == 3);
    vlc_stream_Delete(input);
    assert(source.destroyed == 1);
    vlc_interrupt_set(previous);
    vlc_interrupt_destroy(source.interrupt);
    assert(scope_close(&scope) == (!unread && finish == CLEAN_EOF));
}

static void unused_destroy(stream_t *stream)
{
    (void)stream;
}

static void constructor_peek(vlc_object_t *instance, bool error)
{
    struct scope scope = scope_open(instance);
    struct source source = {.bytes="empty", .size=5, .constructor_error=error};
    stream_t *input = source_new(scope.parent, &source);
    stream_t *candidate = vlc_stream_CommonNew(scope.parent, unused_destroy);
    assert(candidate != NULL);
    candidate->s = input;
    /* The actual automatic-filter Open probes with Peek before its ordinary
     * non-zlib refusal. The bytes remain cached for the subsequent read. */
    assert(meta_art_inflate_open(candidate) == VLC_EGENERIC);
    vlc_stream_Delete(candidate);
    char bytes[64];
    assert(vlc_stream_Read(input, bytes, sizeof(bytes)) == 5);
    assert(!memcmp(bytes, "empty", 5));
    vlc_stream_Delete(input);
    assert(source.constructor_failed == error);
    assert(scope_close(&scope) == !error);
}

static void prefetch_destroy(stream_t *stream)
{
    meta_art_prefetch_close(stream); /* Includes the actual native worker join. */
    vlc_stream_Delete(stream->s);
}

static void prefetch_outcome(vlc_object_t *instance, bool error, bool unread, bool blocks)
{
    struct scope scope = scope_open(instance);
    var_Create(scope.parent, "prefetch-buffer-size", VLC_VAR_INTEGER);
    var_SetInteger(scope.parent, "prefetch-buffer-size", 1);
    var_Create(scope.parent, "prefetch-seek-threshold", VLC_VAR_INTEGER);
    var_SetInteger(scope.parent, "prefetch-seek-threshold", 1024);
    struct source source = {.bytes=unread ? "empty-tail" : "empty", .size=unread ? 10 : 5,
        .finish=error ? ERROR_THEN_EOF : CLEAN_EOF};
    const unsigned long owner_thread = vlc_thread_id();
    stream_t *filter = vlc_stream_CommonNew(scope.parent, prefetch_destroy);
    assert(filter != NULL);
    filter->s = source_new(scope.parent, &source);
    if (blocks) {
        filter->s->pf_read = NULL;
        filter->s->pf_block = source_block;
    }
    assert(meta_art_prefetch_open(filter) == VLC_SUCCESS);
    char bytes[64];
    assert(vlc_stream_Read(filter, bytes, unread ? 5 : sizeof(bytes)) == 5);
    assert(!memcmp(bytes, "empty", 5));
    vlc_stream_Delete(filter);
    assert(source.reader_thread != owner_thread && source.destroyed == 1);
    if (error && !unread) assert(source.errors >= 1);
    assert(scope_close(&scope) == (!error && !unread));
}

static void inheritance_and_bounds(vlc_object_t *instance)
{
    struct scope scope = scope_open(instance);
    vlc_object_t *child = vlc_object_create(scope.parent, sizeof(*child));
    assert(child != NULL);
    assert(vlc_internals(child)->stream_observer == scope.observer);
    vlc_stream_observer_t *replacement = vlc_stream_observer_New();
    assert(replacement != NULL);
    assert(vlc_stream_observer_AttachObject(child, replacement) == VLC_EGENERIC);
    vlc_stream_observer_Release(replacement);
    stream_t *streams[FIXTURE_STREAM_OBSERVER_LIMIT];
    for (size_t i = 0; i < ARRAY_SIZE(streams); ++i) {
        streams[i] = vlc_stream_CommonNew(child, unused_destroy);
        assert(streams[i] != NULL);
    }
    assert(vlc_stream_CommonNew(child, unused_destroy) == NULL);
    for (size_t i = 0; i < ARRAY_SIZE(streams); ++i) vlc_stream_Delete(streams[i]);
    vlc_object_delete(child);
    assert(!scope_close(&scope));
}

static void seal_and_retained_scope(vlc_object_t *instance)
{
    struct scope scope = scope_open(instance);
    vlc_object_t *child = vlc_object_create(scope.parent, sizeof(*child));
    assert(child != NULL);
    /* The child and parent associations keep this borrowed reference alive. */
    vlc_stream_observer_t *held = vlc_internals(child)->stream_observer;
    assert(held == scope.observer);
    assert(vlc_stream_observer_Seal(held, true));
    vlc_stream_observer_Release(scope.observer);
    /* The child association and held reference retain the closed scope.
     * Late stream creation cannot silently lose that inherited scope. */
    assert(vlc_stream_CommonNew(child, unused_destroy) == NULL);
    vlc_stream_observer_t *replacement = vlc_stream_observer_New();
    assert(replacement != NULL);
    assert(vlc_stream_observer_AttachObject(child, replacement) == VLC_SUCCESS);
    assert(vlc_stream_observer_Seal(replacement, true));
    vlc_stream_observer_Release(replacement);
    vlc_object_delete(child);
    vlc_object_delete(scope.parent);
}

static void untracked_control(vlc_object_t *instance)
{
    struct source source = {.bytes="empty", .size=5, .finish=ERROR_THEN_EOF};
    stream_t *input = source_new(instance, &source);
    assert(vlc_internals(VLC_OBJECT(input))->stream_observer == NULL);
    char bytes[64];
    assert(vlc_stream_Read(input, bytes, sizeof(bytes)) == 5);
    assert(!memcmp(bytes, "empty", 5) && source.calls == 3);
    vlc_stream_Delete(input);
}

static void live_stream_refuses_seal(vlc_object_t *instance)
{
    struct scope scope = scope_open(instance);
    stream_t *candidate = vlc_stream_CommonNew(scope.parent, unused_destroy);
    assert(candidate != NULL);
    assert(!vlc_stream_observer_Seal(scope.observer, false));
    vlc_stream_Delete(candidate);
    assert(!scope_close(&scope));
}

static block_t *source_block(stream_t *stream, bool *eof)
{
    struct source *source = stream->p_sys;
    ++source->calls;
    source->reader_thread = vlc_thread_id();
    if (source->offset == 0) {
        block_t *block = block_Alloc(source->size);
        assert(block != NULL);
        memcpy(block->p_buffer, source->bytes, source->size);
        source->offset = source->size;
        return block;
    }
    if (source->finish == ERROR_THEN_EOF && source->errors++ == 0) {
        errno = EIO;
        return NULL;
    }
    *eof = true;
    return NULL;
}

static void block_outcome(vlc_object_t *instance, bool error)
{
    struct scope scope = scope_open(instance);
    struct source source = {.bytes="empty", .size=5,
        .finish=error ? ERROR_THEN_EOF : CLEAN_EOF};
    stream_t *input = source_new(scope.parent, &source);
    input->pf_read = NULL;
    input->pf_block = source_block;
    block_t *block = vlc_stream_ReadBlock(input);
    assert(block != NULL && block->i_buffer == 5 && !memcmp(block->p_buffer, "empty", 5));
    block_Release(block);
    assert(vlc_stream_ReadBlock(input) == NULL);
    if (error) {
        assert(!vlc_stream_Eof(input));
        assert(vlc_stream_ReadBlock(input) == NULL);
    }
    assert(vlc_stream_Eof(input));
    vlc_stream_Delete(input);
    assert(scope_close(&scope) == !error);
}

static void not_applicable_requires_no_io(vlc_object_t *instance)
{
    struct scope scope = scope_open(instance);
    struct source source = {.bytes="empty", .size=5};
    stream_t *input = source_new(scope.parent, &source);
    char bytes[64];
    assert(vlc_stream_Read(input, bytes, sizeof(bytes)) == 5);
    vlc_stream_Delete(input);
    assert(!vlc_stream_observer_Seal(scope.observer, true));
    vlc_stream_observer_Release(scope.observer);
    vlc_object_delete(scope.parent);
}

static void inflate_destroy(stream_t *stream)
{
    meta_art_inflate_close(stream);
    vlc_stream_Delete(stream->s);
}

static void inflate_outcome(vlc_object_t *instance, bool truncated, bool trailing, bool header_only)
{
    struct scope scope = scope_open(instance);
    unsigned char compressed[128];
    uLongf length = sizeof(compressed);
    assert(compress2(compressed, &length, (const Bytef *)"empty", 5, Z_BEST_SPEED) == Z_OK);
    assert(length > 4);
    if (trailing) compressed[length++] = 0xA5;
    struct source source = {.bytes=(const char *)compressed,
                            .size=header_only ? 2 : truncated ? length - 4 : length};
    stream_t *filter = vlc_stream_CommonNew(scope.parent, inflate_destroy);
    assert(filter != NULL);
    filter->s = source_new(scope.parent, &source);
    assert(meta_art_inflate_open(filter) == VLC_SUCCESS);
    char bytes[64];
    assert(vlc_stream_Read(filter, bytes, sizeof(bytes)) == (header_only ? 0 : 5));
    if (!header_only) assert(!memcmp(bytes, "empty", 5)); /* Keep the old bytes. */
    vlc_stream_Delete(filter);
    assert(source.destroyed == 1);
    assert(scope_close(&scope) == (!truncated && !trailing && !header_only));
}

int main(void)
{
    libvlc_int_t *instance = libvlc_InternalCreate();
    assert(instance != NULL);
    vlc_object_t *parent = VLC_OBJECT(instance);
    read_outcome(parent, CLEAN_EOF, false);
    read_outcome(parent, ERROR_THEN_EOF, false);
    read_outcome(parent, CLEAN_EOF, true);
    read_outcome(parent, ERROR_UNTIL_INTERRUPT, false);
    constructor_peek(parent, false);
    constructor_peek(parent, true);
    prefetch_outcome(parent, false, false, false);
    prefetch_outcome(parent, true, false, false);
    prefetch_outcome(parent, false, true, false);
    prefetch_outcome(parent, false, false, true);
    prefetch_outcome(parent, true, false, true);
    inheritance_and_bounds(parent);
    seal_and_retained_scope(parent);
    untracked_control(parent);
    live_stream_refuses_seal(parent);
    block_outcome(parent, false);
    block_outcome(parent, true);
    not_applicable_requires_no_io(parent);
    inflate_outcome(parent, false, false, false);
    inflate_outcome(parent, true, false, false);
    inflate_outcome(parent, false, true, false);
    inflate_outcome(parent, false, false, true);
    libvlc_InternalDestroy(instance);
    puts("All 22 native stream observer groups passed.");
    return 0;
}

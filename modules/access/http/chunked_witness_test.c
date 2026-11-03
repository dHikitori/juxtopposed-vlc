// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <vlc_common.h>
#include <vlc_block.h>
#include <vlc_tls.h>
#include <assert.h>
#include <string.h>

/* Compile the actual parser with one explicit allocation fault. Its old
 * block/NULL/error return is unchanged. Only the completion query is new.
 */
static bool fail_allocation;
static block_t *witness_allocate(size_t size)
{
    return fail_allocation ? NULL : vlc_frame_Alloc(size);
}
#define vlc_frame_Alloc witness_allocate
#include "chunked.c"
#undef vlc_frame_Alloc

const char vlc_module_name[] = "test_http_chunked_witness";
static const char *bytes;
static size_t remaining;
static bool expected_bad;

static int socket_fd(struct vlc_tls *tls, short *events)
{
    (void)tls; (void)events;
    return -1;
}
static ssize_t socket_read(struct vlc_tls *tls, struct iovec *iov, unsigned count)
{
    (void)tls;
    size_t read = 0;
    for (unsigned i = 0; i < count; ++i) {
        size_t size = iov[i].iov_len;
        if (size > remaining) size = remaining;
        memcpy(iov[i].iov_base, bytes, size);
        bytes += size;
        remaining -= size;
        read += size;
    }
    return read;
}
static void socket_close(struct vlc_tls *tls) { (void)tls; }
static const struct vlc_tls_operations socket_ops = {
    .get_fd=socket_fd, .readv=socket_read, .close=socket_close,
};
static struct vlc_tls witness_socket = {.ops=&socket_ops};
static void parent_close(struct vlc_http_stream *stream, bool bad)
{
    (void)stream;
    assert(bad == expected_bad);
}
static const struct vlc_http_stream_cbs parent_ops = {.close=parent_close};
static struct vlc_http_stream parent = {.cbs=&parent_ops};

static void terminal(bool good)
{
    bytes = good ? "0\r\n\r\n" : "0\r\n";
    remaining = strlen(bytes);
    expected_bad = !good;
    struct vlc_http_stream *stream = vlc_chunked_open(&parent, &witness_socket);
    assert(stream != NULL);
    assert(vlc_http_stream_read(stream) == NULL);
    assert(vlc_http_stream_is_complete(stream) == good);
    assert(vlc_http_stream_read(stream) == NULL);
    assert(vlc_http_stream_is_complete(stream) == good);
    vlc_http_stream_close(stream, false);
}

int main(void)
{
    terminal(true);
    terminal(false);
    bytes = "5\r\nempty\r\n0\r\n\r\n";
    remaining = strlen(bytes);
    struct vlc_http_stream *stream = vlc_chunked_open(&parent, &witness_socket);
    assert(stream != NULL);
    fail_allocation = true;
    assert(vlc_http_stream_read(stream) == NULL); /* Preserve the old return. */
    assert(!vlc_http_stream_is_complete(stream));
    fail_allocation = false;
    block_t *block = vlc_http_stream_read(stream);
    assert(block != NULL && block != vlc_http_error && block->i_buffer == 5);
    block_Release(block);
    assert(vlc_http_stream_read(stream) == NULL);
    assert(vlc_http_stream_is_complete(stream));
    expected_bad = false;
    vlc_http_stream_close(stream, false);
    puts("All 3 chunked witness groups passed.");
    return 0;
}

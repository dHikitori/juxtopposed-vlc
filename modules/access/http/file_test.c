/*****************************************************************************
 * file_test.c: HTTP file download test
 *****************************************************************************
 * Copyright (C) 2015 Rémi Denis-Courmont
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#undef NDEBUG

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <vlc_common.h>
#include <vlc_block.h>
#include <vlc_http.h>
#include "resource.h"
#include "file.h"
#include "message.h"

const char vlc_module_name[] = "test_http_file";

static const char url[] = "https://www.example.com:8443/dir/file.ext?a=b";
static const char url_http[] = "http://www.example.com:8443/dir/file.ext?a=b";
static const char url_mmsh[] = "mmsh://www.example.com:8443/dir/file.ext?a=b";
static const char url_icyx[] = "icyx://www.example.com:8443/dir/file.ext?a=b";
static const char ua[] = PACKAGE_NAME "/" PACKAGE_VERSION " (test suite)";

static const char *replies[2] = { NULL, NULL };
static uintmax_t offset = 0;
static bool secure = true;
static bool etags = false;
static int lang = -1;

static vlc_http_cookie_jar_t *jar;

struct read_step {
    const char *bytes;
    bool error;
    bool complete;
};
static const struct read_step *read_steps;
static size_t read_count, read_index;
static bool read_terminal;
static void test_read_outcomes(void);

int main(void)
{
    struct vlc_http_resource *f;
    char *str;

    jar = vlc_http_cookies_new();

    /* Request failure test */
    f = vlc_http_file_create(NULL, url, ua, NULL);
    assert(f != NULL);
    vlc_http_res_set_login(f, NULL, NULL);
    vlc_http_res_set_login(f, "john", NULL);
    vlc_http_res_set_login(f, NULL, NULL);
    vlc_http_res_set_login(f, "john", "secret");
    vlc_http_res_set_login(f, NULL, NULL);
    vlc_http_file_seek(f, 0);
    assert(vlc_http_file_get_status(f) < 0);
    assert(vlc_http_file_get_redirect(f) == NULL);
    assert(vlc_http_file_get_size(f) == (uintmax_t)-1);
    assert(!vlc_http_file_can_seek(f));
    assert(vlc_http_file_get_type(f) == NULL);
    assert(vlc_http_file_read(f) == NULL);
    vlc_http_res_destroy(f);

    /* Non-seekable stream test */
    replies[0] = "HTTP/1.1 200 OK\r\n"
                 "ETag: \"foobar42\"\r\n"
                 "Content-Type: video/mpeg\r\n"
                 "\r\n";

    offset = 0;
    etags = true;
    f = vlc_http_file_create(NULL, url, ua, NULL);
    assert(f != NULL);
    assert(vlc_http_file_get_status(f) == 200);
    assert(!vlc_http_file_can_seek(f));
    assert(vlc_http_file_get_size(f) == (uintmax_t)-1);
    str = vlc_http_file_get_type(f);
    assert(str != NULL && !strcmp(str, "video/mpeg"));
    free(str);

    /* Seek failure */
    replies[0] = "HTTP/1.1 200 OK\r\nETag: \"foobar42\"\r\n\r\n";

    assert(vlc_http_file_seek(f, offset = 1234) < 0);
    vlc_http_file_destroy(f);

    /* Seekable file test */
    replies[0] = "HTTP/1.1 206 Partial Content\r\n"
                 "Content-Range: bytes 0-2344/2345\r\n"
                 "ETag: W/\"foobar42\"\r\n"
                 "Last-Modified: Mon, 21 Oct 2013 20:13:22 GMT\r\n"
                 "\r\n";

    offset = 0;
    f = vlc_http_file_create(NULL, url, ua, NULL);
    assert(f != NULL);
    assert(vlc_http_file_can_seek(f));
    assert(vlc_http_file_get_size(f) == 2345);
    assert(vlc_http_file_read(f) == NULL);

    /* Seek success */
    replies[0] = "HTTP/1.1 206 Partial Content\r\n"
                 "Content-Range: bytes 1234-3455/3456\r\n"
                 "ETag: W/\"foobar42\"\r\n"
                 "Last-Modified: Mon, 21 Oct 2013 20:13:22 GMT\r\n"
                 "\r\n";
    assert(vlc_http_file_seek(f, offset = 1234) == 0);
    assert(vlc_http_file_can_seek(f));
    assert(vlc_http_file_get_size(f) == 3456);
    assert(vlc_http_file_read(f) == NULL);

    /* Seek too far */
    replies[0] = "HTTP/1.1 416 Range Not Satisfiable\r\n"
                 "Content-Range: bytes */4567\r\n"
                 "ETag: W/\"foobar42\"\r\n"
                 "Last-Modified: Mon, 21 Oct 2013 20:13:22 GMT\r\n"
                 "\r\n";
    vlc_http_file_seek(f, offset = 5678);
    assert(vlc_http_file_can_seek(f));
    assert(vlc_http_file_get_size(f) == 4567);
    assert(vlc_http_file_read(f) == NULL);
    vlc_http_file_destroy(f);

    /* Redirect */
    replies[0] = "HTTP/1.1 301 Permanent Redirect\r\n"
                 "Location: /somewhere/else/#here\r\n"
                 "\r\n";

    offset = 0;
    f = vlc_http_file_create(NULL, url, ua, NULL);
    assert(f != NULL);
    assert(!vlc_http_file_can_seek(f));
    assert(vlc_http_file_get_size(f) == (uintmax_t)-1);
    str = vlc_http_file_get_redirect(f);
    assert(str != NULL);
    assert(!strcmp(str, "https://www.example.com:8443/somewhere/else/"));
    free(str);
    vlc_http_file_destroy(f);

    /* Continuation */
    replies[0] = "HTTP/1.1 100 Standby\r\n"
                 "\r\n";
    replies[1] = "HTTP/1.1 200 OK\r\n"
                 "Content-Length: 9999\r\n"
                 "\r\n";
    offset = 0;
    f = vlc_http_file_create(NULL, url, ua, NULL);
    assert(f != NULL);
    assert(vlc_http_file_get_size(f) == 9999);
    assert(vlc_http_file_get_redirect(f) == NULL);
    vlc_http_file_destroy(f);

    /* No entity tags */
    replies[0] = "HTTP/1.1 206 Partial Content\r\n"
                 "Content-Range: bytes 0-2344/2345\r\n"
                 "Last-Modified: Mon, 21 Oct 2013 20:13:22 GMT\r\n"
                 "\r\n";

    offset = 0;
    etags = false;
    f = vlc_http_file_create(NULL, url, ua, NULL);
    assert(f != NULL);
    assert(vlc_http_file_can_seek(f));

    replies[0] = "HTTP/1.1 206 Partial Content\r\n"
                 "Content-Range: bytes 1234-3455/3456\r\n"
                 "Last-Modified: Mon, 21 Oct 2013 20:13:22 GMT\r\n"
                 "\r\n";
    assert(vlc_http_file_seek(f, offset = 1234) == 0);
    vlc_http_file_destroy(f);

    /* Invalid responses */
    replies[0] = "HTTP/1.1 206 Partial Content\r\n"
                 "Content-Type: multipart/byteranges\r\n"
                 "\r\n";
    offset = 0;

    f = vlc_http_file_create(NULL, url, ua, NULL);
    assert(f != NULL);
    assert(vlc_http_file_get_size(f) == (uintmax_t)-1);

    replies[0] = "HTTP/1.1 206 Partial Content\r\n"
                 "Content-Range: seconds 60-120/180\r\n"
                 "\r\n";
    assert(vlc_http_file_seek(f, 0) == -1);

    /* Incomplete range */
    replies[0] = "HTTP/1.1 206 Partial Content\r\n"
                 "Content-Range: bytes 0-1233/*\r\n"
                 "\r\n";
    assert(vlc_http_file_seek(f, 0) == 0);
    assert(vlc_http_file_get_size(f) == 1234);

    /* Extraneous range */
    replies[0] = "HTTP/1.1 200 OK\r\n"
                 "Content-Range: bytes 0-1233/1234\r\n"
                 "\r\n";
    assert(vlc_http_file_seek(f, 0) == 0);
    assert(vlc_http_file_get_size(f) == (uintmax_t)-1);

    /* Non-negotiable language */
    replies[0] = "HTTP/1.1 406 Not Acceptable\r\n"
                 "\r\n";
    replies[1] = "HTTP/1.1 206 OK\r\n"
                 "Content-Range: bytes 0-1/2\r\n"
                 "\r\n";
    lang = 1;
    assert(vlc_http_file_seek(f, 0) == 0);
    assert(vlc_http_file_can_seek(f));
    assert(vlc_http_file_get_size(f) == 2);

    /* Protocol redirect hacks - not over TLS */
    replies[0] = "HTTP/1.1 200 OK\r\n"
                 "Pragma: features\r\n"
                 "\r\n";
    assert(vlc_http_file_seek(f, 0) == 0);
    assert(vlc_http_file_get_redirect(f) == NULL);

    replies[0] = "HTTP/1.1 200 OK\r\n"
                 "Icy-Name:CraptasticRadio\r\n"
                 "\r\n";
    assert(vlc_http_file_seek(f, 0) == 0);
    assert(vlc_http_file_get_redirect(f) == NULL);

    vlc_http_file_destroy(f);

    secure = false;
    lang = -1;
    f = vlc_http_file_create(NULL, url_http, ua, NULL);
    assert(f != NULL);

    /* Protocol redirect hacks - over insecure HTTP */
    replies[0] = "HTTP/1.1 200 OK\r\n"
                 "Pragma: features\r\n"
                 "\r\n";
    str = vlc_http_file_get_redirect(f);
    assert(str != NULL && strcmp(str, url_mmsh) == 0);
    free(str);

    replies[0] = "HTTP/1.1 200 OK\r\n"
                 "Icy-Name:CraptasticRadio\r\n"
                 "\r\n";
    vlc_http_file_seek(f, 0);
    str = vlc_http_file_get_redirect(f);
    assert(str != NULL && strcmp(str, url_icyx) == 0);
    free(str);

    vlc_http_file_destroy(f);

    /* Dummy API calls */
    f = vlc_http_file_create(NULL, "ftp://localhost/foo", NULL, NULL);
    assert(f == NULL);
    f = vlc_http_file_create(NULL, "/foo", NULL, NULL);
    assert(f == NULL);
    f = vlc_http_file_create(NULL, "http://www.example.com", NULL, NULL);
    assert(f != NULL);
    vlc_http_file_destroy(f);

    test_read_outcomes();
    vlc_http_cookies_destroy(jar);
    return 0;
}

/* Callback for vlc_http_msg_h2_frame */
#include "h2frame.h"

struct vlc_h2_frame *
vlc_h2_frame_headers(uint_fast32_t id, uint_fast32_t mtu, bool eos,
                     unsigned count, const char *const tab[][2])
{
    (void) id; (void) mtu; (void) count, (void) tab;
    assert(!eos);
    return NULL;
}

/* Callback for the HTTP request */
#include "connmgr.h"

static struct vlc_http_stream stream;

static struct vlc_http_msg *stream_read_headers(struct vlc_http_stream *s)
{
    assert(s == &stream);

    /* return next reply */
    struct vlc_http_msg *m = NULL;
    const char *answer = replies[0];

    if (answer != NULL)
    {
        m = vlc_http_msg_headers(answer);
        assert(m != NULL);
        vlc_http_msg_attach(m, s);
    }

    memmove(replies, replies + 1, sizeof (replies) - sizeof (replies[0]));
    replies[ARRAY_SIZE(replies) - 1] = NULL;

    return m;
}

static block_t *stream_read(struct vlc_http_stream *s)
{
    assert(s == &stream);
    if (read_steps != NULL && read_index < read_count) {
        const struct read_step *step = &read_steps[read_index++];
        read_terminal = step->complete;
        if (step->error) return vlc_http_error;
        if (step->bytes != NULL) {
            const size_t length = strlen(step->bytes);
            block_t *block = block_Alloc(length);
            assert(block != NULL);
            memcpy(block->p_buffer, step->bytes, length);
            offset += length;
            return block;
        }
    }
    return NULL;
}

static bool stream_is_complete(struct vlc_http_stream *s)
{
    assert(s == &stream);
    return read_terminal;
}

static void stream_close(struct vlc_http_stream *s, bool abort)
{
    assert(s == &stream);
    assert(!abort);
}

static const struct vlc_http_stream_cbs stream_callbacks =
{
    stream_read_headers,
    NULL,
    stream_read,
    stream_close,
    stream_is_complete,
};

static const struct vlc_http_stream_cbs no_witness_callbacks = {
    stream_read_headers, NULL, stream_read, stream_close, NULL,
};

static struct vlc_http_stream stream = { &stream_callbacks };

struct vlc_http_msg *vlc_http_mgr_request(struct vlc_http_mgr *mgr, bool https,
                                          const char *host, unsigned port,
                                          const struct vlc_http_msg *req,
                                          bool idempotent, bool payload)
{
    const char *str;
    char *end;

    assert(https == secure);
    assert(mgr == NULL);
    assert(!strcmp(host, "www.example.com"));
    assert(port == 8443);
    assert(idempotent);
    assert(!payload);

    str = vlc_http_msg_get_method(req);
    assert(!strcmp(str, "GET"));
    str = vlc_http_msg_get_scheme(req);
    assert(!strcmp(str, secure ? "https" : "http"));
    str = vlc_http_msg_get_authority(req);
    assert(!strcmp(str, "www.example.com:8443"));
    str = vlc_http_msg_get_path(req);
    assert(!strcmp(str, "/dir/file.ext?a=b"));
    str = vlc_http_msg_get_agent(req);
    assert(!strcmp(str, ua));
    str = vlc_http_msg_get_header(req, "Referer");
    assert(str == NULL);
    str = vlc_http_msg_get_header(req, "Accept");
    assert(str == NULL || strstr(str, "*/*") != NULL);

    str = vlc_http_msg_get_header(req, "Accept-Language");
    /* This test case does not call setlocale(), so en_US can be assumed. */
    if (lang != 0)
    {
        assert(str != NULL && strncmp(str, "en_US", 5) == 0);
        if (lang > 0)
            lang--;
    }
    else
        assert(str == NULL);

    str = vlc_http_msg_get_header(req, "Range");
    assert(str != NULL && !strncmp(str, "bytes=", 6)
        && strtoul(str + 6, &end, 10) == offset && *end == '-');

    time_t mtime = vlc_http_msg_get_time(req, "If-Unmodified-Since");
    str = vlc_http_msg_get_header(req, "If-Match");

    if (etags)
    {
        if (offset != 0)
            assert(str != NULL && !strcmp(str, "\"foobar42\""));
        else
        if (str != NULL)
            assert(strcmp(str, "*") || strcmp(str, "\"foobar42\""));
    }
    else
    {
        if (offset != 0)
            assert(mtime == 1382386402);
    }

    return vlc_http_msg_get_initial(&stream);
}

struct vlc_http_cookie_jar_t *vlc_http_mgr_get_jar(struct vlc_http_mgr *mgr)
{
    assert(mgr == NULL);
    return jar;
}

static struct vlc_http_resource *outcome_start(const char *first, const char *retry,
                const struct read_step *steps, size_t count, bool witness)
{
    replies[0] = first;
    replies[1] = retry;
    offset = 0;
    secure = true;
    etags = true;
    lang = -1;
    read_steps = steps;
    read_count = count;
    read_index = 0;
    read_terminal = false;
    stream.cbs = witness ? &stream_callbacks : &no_witness_callbacks;
    struct vlc_http_resource *file = vlc_http_file_create(NULL, url, ua, NULL);
    assert(file != NULL);
    return file;
}

static void outcome_read(struct vlc_http_resource *file,
                         enum vlc_http_file_read_outcome expected, const char *bytes)
{
    enum vlc_http_file_read_outcome outcome = VLC_HTTP_FILE_COMPLETE;
    block_t *block = vlc_http_file_read_outcome(file, &outcome);
    assert(outcome == expected && block != vlc_http_error);
    if (bytes == NULL) assert(block == NULL);
    else {
        assert(block != NULL && block->i_buffer == strlen(bytes));
        assert(!memcmp(block->p_buffer, bytes, block->i_buffer));
        block_Release(block);
    }
}

static void test_read_outcomes(void)
{
    static const char known[] = "HTTP/1.1 200 OK\r\nETag: \"foobar42\"\r\nContent-Length: 5\r\n\r\n";
    static const char unknown[] = "HTTP/1.1 200 OK\r\nETag: \"foobar42\"\r\n\r\n";
    static const char whole[] = "HTTP/1.1 206 OK\r\nETag: \"foobar42\"\r\nContent-Range: bytes 0-4/5\r\nContent-Length: 5\r\n\r\n";
    static const char resume[] = "HTTP/1.1 206 OK\r\nETag: \"foobar42\"\r\nContent-Range: bytes 2-4/5\r\nContent-Length: 3\r\n\r\n";
    static const struct read_step clean[] = {{"empty",false,false},{NULL,false,true}};
    static const struct read_step error[] = {{"em",false,false},{NULL,true,false}};
    static const struct read_step short_body[] = {{"em",false,false},{NULL,false,true}};
    static const struct read_step recovered[] = {
        {"em",false,false},{NULL,true,false},{"pty",false,false},{NULL,false,true},
    };
    static const struct read_step resumed[] = {
        {"em",false,false},{NULL,false,true},{"pty",false,false},{NULL,false,true},
    };
    struct vlc_http_resource *file = outcome_start(known, NULL, clean, ARRAY_SIZE(clean), true);
    outcome_read(file, VLC_HTTP_FILE_DATA, "empty");
    outcome_read(file, VLC_HTTP_FILE_COMPLETE, NULL);
    vlc_http_file_destroy(file);

    /* Unknown length is valid only with the parser's explicit terminal state. */
    file = outcome_start(unknown, NULL, clean, ARRAY_SIZE(clean), true);
    outcome_read(file, VLC_HTTP_FILE_DATA, "empty");
    outcome_read(file, VLC_HTTP_FILE_COMPLETE, NULL);
    vlc_http_file_destroy(file);
    file = outcome_start(unknown, NULL, clean, ARRAY_SIZE(clean), false);
    outcome_read(file, VLC_HTTP_FILE_DATA, "empty");
    outcome_read(file, VLC_HTTP_FILE_UNAVAILABLE, NULL);
    vlc_http_file_destroy(file);

    file = outcome_start(known, NULL, error, ARRAY_SIZE(error), true);
    outcome_read(file, VLC_HTTP_FILE_DATA, "em");
    outcome_read(file, VLC_HTTP_FILE_UNAVAILABLE, NULL);
    vlc_http_file_destroy(file);
    file = outcome_start(known, NULL, short_body, ARRAY_SIZE(short_body), true);
    outcome_read(file, VLC_HTTP_FILE_DATA, "em");
    outcome_read(file, VLC_HTTP_FILE_UNAVAILABLE, NULL);
    vlc_http_file_destroy(file);

    /* All old resume bytes are returned. Recovered evidence stays refused. */
    file = outcome_start(whole, resume, recovered, ARRAY_SIZE(recovered), true);
    outcome_read(file, VLC_HTTP_FILE_DATA, "em");
    outcome_read(file, VLC_HTTP_FILE_UNAVAILABLE, "pty");
    outcome_read(file, VLC_HTTP_FILE_UNAVAILABLE, NULL);
    vlc_http_file_destroy(file);
    file = outcome_start(whole, resume, resumed, ARRAY_SIZE(resumed), true);
    outcome_read(file, VLC_HTTP_FILE_DATA, "em");
    outcome_read(file, VLC_HTTP_FILE_UNAVAILABLE, "pty");
    outcome_read(file, VLC_HTTP_FILE_UNAVAILABLE, NULL);
    vlc_http_file_destroy(file);
    file = outcome_start(whole, NULL, error, ARRAY_SIZE(error), true);
    outcome_read(file, VLC_HTTP_FILE_DATA, "em");
    outcome_read(file, VLC_HTTP_FILE_UNAVAILABLE, NULL);
    vlc_http_file_destroy(file);

    file = outcome_start(whole, NULL, clean, ARRAY_SIZE(clean), true);
    outcome_read(file, VLC_HTTP_FILE_DATA, "empty");
    outcome_read(file, VLC_HTTP_FILE_COMPLETE, NULL);
    vlc_http_file_destroy(file);
    file = outcome_start("HTTP/1.1 206 OK\r\nContent-Range: bytes 0-4/*\r\nContent-Length: 5\r\n\r\n",
                         NULL, clean, ARRAY_SIZE(clean), true);
    outcome_read(file, VLC_HTTP_FILE_DATA, "empty");
    outcome_read(file, VLC_HTTP_FILE_UNAVAILABLE, NULL);
    vlc_http_file_destroy(file);
    file = outcome_start("HTTP/1.1 200 OK\r\nContent-Length: 5junk\r\n\r\n",
                         NULL, clean, ARRAY_SIZE(clean), true);
    outcome_read(file, VLC_HTTP_FILE_DATA, "empty");
    outcome_read(file, VLC_HTTP_FILE_UNAVAILABLE, NULL);
    vlc_http_file_destroy(file);
    file = outcome_start("HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\n",
                         NULL, clean, ARRAY_SIZE(clean), true);
    outcome_read(file, VLC_HTTP_FILE_DATA, "empty");
    outcome_read(file, VLC_HTTP_FILE_UNAVAILABLE, NULL);
    vlc_http_file_destroy(file);
    file = outcome_start("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 999\r\n\r\n",
                         NULL, clean, ARRAY_SIZE(clean), true);
    outcome_read(file, VLC_HTTP_FILE_DATA, "empty");
    outcome_read(file, VLC_HTTP_FILE_COMPLETE, NULL);
    vlc_http_file_destroy(file);

    file = outcome_start(whole, resume, recovered, ARRAY_SIZE(recovered), true);
    block_t *block = vlc_http_file_read(file);
    assert(block != NULL && block->i_buffer == 2);
    block_Release(block);
    block = vlc_http_file_read(file);
    assert(block != NULL && block->i_buffer == 3);
    block_Release(block);
    assert(vlc_http_file_read(file) == NULL);
    vlc_http_file_destroy(file);
    read_steps = NULL;
    stream.cbs = &stream_callbacks;
    puts("All 14 HTTP file outcome groups passed.");
}

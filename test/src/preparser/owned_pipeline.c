// SPDX-License-Identifier: LGPL-2.1-or-later
#include "../../libvlc/test.h"
#include "../../../lib/libvlc_internal.h"
#include <vlc_input_item.h>
#include <vlc_input_policy.h>
#include <vlc_metadata_backend.h>
#include <vlc_metadata_owner.h>
#include <vlc_preparser.h>
#include <vlc_url.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdatomic.h>

struct completion { vlc_sem_t done; unsigned calls; int status; };
static void ended(vlc_preparser_req *request, int status, void *opaque)
{ (void)request; struct completion *state = opaque; ++state->calls; state->status = status; vlc_sem_post(&state->done); }
static int no_dispatch(void *opaque, vlc_metadata_job_t *job) { (void)opaque; (void)job; abort(); }
static bool allow_url(void *opaque, const char *url) { (void)opaque; (void)url; return true; }
static bool allow_connection(void *opaque, const char *host, unsigned port, bool tls)
{ (void)opaque; (void)host; (void)port; (void)tls; return true; }
static libvlc_instance_t *instance(void)
{
    const char *args[] = {"--ignore-config", "--no-plugins-cache", "--no-media-library", "--quiet", "--no-metadata-network-access"};
    libvlc_instance_t *core = libvlc_new(ARRAY_SIZE(args), args); assert(core != NULL); return core;
}
static void wait_ended(struct completion *completion, int status)
{
    assert(vlc_sem_timedwait(&completion->done, vlc_tick_now() + VLC_TICK_FROM_SEC(8)) == 0);
    assert(completion->calls == 1 && completion->status == status);
}
static void fetch_only_and_scoped_requests_do_not_need_a_child_binary(void)
{
    libvlc_instance_t *core = instance();
    char *old = getenv("VLC_LIB_PATH") != NULL ? strdup(getenv("VLC_LIB_PATH")) : NULL;
    char directory[] = "/tmp/hakone-no-child-XXXXXX"; assert(mkdtemp(directory) != NULL);
    char *missing = NULL; assert(asprintf(&missing, "%s/missing/modules", directory) >= 0);
    assert(setenv("VLC_LIB_PATH", missing, 1) == 0);
    static const struct vlc_preparser_cbs cbs = {.on_ended=ended};
    const struct vlc_input_policy_operations operations = {.allow_url=allow_url,.allow_connection=allow_connection};
    for (unsigned state = 0; state < 5; ++state) {
        vlc_object_t *parent = VLC_OBJECT(core->p_libvlc_int), *scoped = NULL;
        input_item_t *item = input_item_New(state == 2 ? "HaKoNe-AdDoN-SoUrCe:v1:unissued" : "file:///not-read.wav", "Native"); assert(item != NULL);
        vlc_input_policy_t *policy = NULL;
        if (state == 1 || state >= 3) {
            policy = vlc_input_policy_New(&operations, NULL); assert(policy != NULL);
            if (state == 1) assert(vlc_input_policy_AttachItem(item, policy) == VLC_SUCCESS);
            else {
                scoped = vlc_object_create(parent, sizeof(*scoped)); assert(scoped != NULL); parent = scoped;
                assert(vlc_input_policy_AttachObject(parent, policy) == VLC_SUCCESS);
                if (state == 4) vlc_input_policy_Revoke(policy);
            }
        }
        const struct vlc_preparser_cfg cfg = {.types=VLC_PREPARSER_TYPE_PARSE | VLC_PREPARSER_TYPE_FETCHMETA_LOCAL,
            .timeout=VLC_TICK_FROM_SEC(3),.external_process=true};
        vlc_preparser_t *preparser = vlc_preparser_New(parent, &cfg); assert(preparser != NULL);
        struct completion completion = {0}; vlc_sem_init(&completion.done, 0);
        vlc_preparser_req *request = vlc_preparser_req_NewParse(preparser, item,
            state == 0 ? VLC_PREPARSER_TYPE_FETCHMETA_LOCAL : VLC_PREPARSER_TYPE_PARSE, &cbs, &completion); assert(request != NULL);
        assert(vlc_preparser_Submit(preparser, request) == VLC_SUCCESS); wait_ended(&completion, state == 0 ? VLC_SUCCESS : -EACCES);
        assert(input_item_GetDuration(item) == VLC_TICK_INVALID);
        vlc_preparser_req_Release(request); vlc_preparser_Delete(preparser); input_item_Release(item);
        if (scoped != NULL) vlc_object_delete(scoped);
        vlc_input_policy_Release(policy);
    }
    if (old != NULL) assert(setenv("VLC_LIB_PATH", old, 1) == 0); else assert(unsetenv("VLC_LIB_PATH") == 0);
    free(old); free(missing); assert(rmdir(directory) == 0); libvlc_release(core);
}
struct server {
    int listener;
    unsigned port;
    vlc_sem_t accepted, release;
    atomic_bool eof;
    bool send_wave;
};
static void *serve(void *opaque)
{
    struct server *server = opaque;
    int client = accept(server->listener, NULL, NULL); assert(client >= 0);
    char request[8192] = {0}; size_t bytes = 0;
    while (bytes < sizeof(request) - 1 && strstr(request, "\r\n\r\n") == NULL) {
        const ssize_t count = recv(client, request + bytes, sizeof(request) - 1 - bytes, 0); assert(count > 0); bytes += count;
    }
    assert(strstr(request, "\r\n\r\n") != NULL); vlc_sem_post(&server->accepted); vlc_sem_wait(&server->release);
    if (server->send_wave) {
        unsigned char wave[8044] = {0};
        memcpy(wave, "RIFF", 4); const uint32_t length = htole32(sizeof(wave) - 8); memcpy(wave + 4, &length, 4);
        memcpy(wave + 8, "WAVEfmt ", 8); wave[16] = 16; wave[20] = wave[22] = wave[32] = 1;
        wave[24] = wave[28] = 0x40; wave[25] = wave[29] = 0x1f; wave[34] = 8; memcpy(wave + 36, "data", 4);
        wave[40] = 0x40; wave[41] = 0x1f; memset(wave + 44, 128, 8000);
        const char headers[] = "HTTP/1.1 200 OK\r\nContent-Type: audio/wav\r\nContent-Length: 8044\r\nConnection: close\r\n\r\n";
        assert(send(client, headers, sizeof(headers) - 1, MSG_NOSIGNAL) == sizeof(headers) - 1);
        size_t sent = 0;
        while (sent < sizeof(wave)) { const ssize_t count = send(client, wave + sent, sizeof(wave) - sent, MSG_NOSIGNAL); assert(count > 0); sent += count; }
    } else {
        char byte; atomic_store(&server->eof, recv(client, &byte, 1, 0) == 0);
    }
    close(client); return NULL;
}
static void server_start(struct server *server, vlc_thread_t *thread)
{
    vlc_sem_init(&server->accepted, 0); vlc_sem_init(&server->release, 0); atomic_init(&server->eof, false);
    server->listener = socket(AF_INET, SOCK_STREAM, 0); assert(server->listener >= 0);
    struct sockaddr_in address = {.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
    assert(bind(server->listener, (const struct sockaddr *)&address, sizeof(address)) == 0 && listen(server->listener, 1) == 0);
    socklen_t size = sizeof(address); assert(getsockname(server->listener, (struct sockaddr *)&address, &size) == 0);
    server->port = ntohs(address.sin_port); assert(vlc_clone(thread, serve, server) == 0);
}
static void real_child_cancel_timeout_close_and_uri_aba(void)
{
    for (unsigned state = 0; state < 4; ++state) {
        fprintf(stderr, "Native child state=%u\n", state);
        struct server server = {.send_wave=state == 3}; vlc_thread_t thread; server_start(&server, &thread);
        libvlc_instance_t *core = instance();
        static const struct vlc_metadata_backend_operations operations = {.dispatch=no_dispatch};
        vlc_metadata_backend_t *backend = vlc_metadata_backend_New(&operations, NULL); assert(backend != NULL);
        if (state == 1) assert(vlc_metadata_owner_Set(VLC_OBJECT(core->p_libvlc_int), backend) == VLC_SUCCESS);
        const struct vlc_preparser_cfg cfg = {.types=VLC_PREPARSER_TYPE_PARSE,.timeout=state == 2 ? VLC_TICK_FROM_SEC(2) : 0,.external_process=true};
        vlc_preparser_t *preparser = vlc_preparser_New(VLC_OBJECT(core->p_libvlc_int), &cfg); assert(preparser != NULL);
        char *uri = NULL; assert(asprintf(&uri, "http://127.0.0.1:%u/native.wav", server.port) >= 0);
        input_item_t *item = input_item_New(uri, "Native"); assert(item != NULL); input_item_SetTitle(item, "User title");
        assert(input_item_AddOption(item, "http-proxy=", VLC_INPUT_OPTION_TRUSTED) == VLC_SUCCESS);
        struct completion completion = {0}; vlc_sem_init(&completion.done, 0);
        static const struct vlc_preparser_cbs cbs = {.on_ended=ended};
        vlc_preparser_req *request = vlc_preparser_req_NewParse(preparser, item, VLC_PREPARSER_TYPE_PARSE, &cbs, &completion); assert(request != NULL);
        assert(vlc_preparser_Submit(preparser, request) == VLC_SUCCESS);
        if (vlc_sem_timedwait(&server.accepted, vlc_tick_now() + VLC_TICK_FROM_SEC(5)) != 0) {
            if (vlc_sem_trywait(&completion.done) == 0)
                fprintf(stderr, "Native child ended before server admission: status=%d\n", completion.status);
            abort();
        }
        if (state == 0) assert(vlc_preparser_Cancel(preparser, request) == 1);
        if (state == 1) vlc_metadata_backend_Close(backend);
        if (state == 3) {
            input_item_SetURI(item, "file:///different.wav"); input_item_SetURI(item, uri); vlc_sem_post(&server.release);
        }
        wait_ended(&completion, state < 2 ? -EINTR : state == 2 ? VLC_ETIMEOUT : -EAGAIN);
        vlc_preparser_req_Release(request); vlc_preparser_Delete(preparser);
        assert(input_item_GetDuration(item) == VLC_TICK_INVALID);
        if (state != 3) vlc_sem_post(&server.release);
        vlc_join(thread, NULL); if (state != 3) assert(atomic_load(&server.eof)); close(server.listener);
        input_item_Release(item); free(uri); vlc_metadata_backend_Close(backend); vlc_metadata_backend_Release(backend); libvlc_release(core);
    }
}
int main(void)
{
#ifndef HAVE_VLC_EXTERNAL_PREPARSER
    return 77;
#else
    test_init();
    const char *proxy_variables[] = {"http_proxy", "https_proxy", "all_proxy", "HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY"};
    for (size_t i = 0; i < ARRAY_SIZE(proxy_variables); ++i) assert(unsetenv(proxy_variables[i]) == 0);
    fetch_only_and_scoped_requests_do_not_need_a_child_binary();
    real_child_cancel_timeout_close_and_uri_aba();
    return 0;
#endif
}

// SPDX-License-Identifier: GPL-2.0-or-later
// Native test process. No add-on code runs in this fixture.
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
# include <windows.h>
# include <wchar.h>
# include <io.h>
# include <fcntl.h>
#endif

#define FRAME_LIMIT (1024u * 1024u)
#define MESSAGE_LIMIT 4096u

struct text { const char *begin, *end; };
static const char *space(const char *p, const char *end)
{ while (p != end && isspace((unsigned char)*p)) ++p; return p; }
static const char *string_end(const char *p, const char *end)
{
    if (p == end || *p++ != '"') return NULL;
    while (p != end) {
        const unsigned char c = (unsigned char)*p++;
        if (c == '"') return p;
        if (c < 0x20) return NULL;
        if (c == '\\') {
            if (p == end) return NULL;
            const char escape = *p++;
            if (escape == 'u') {
                for (unsigned i = 0; i < 4; ++i)
                    if (p == end || !isxdigit((unsigned char)*p++)) return NULL;
            } else if (!strchr("\"\\/bfnrt", escape)) return NULL;
        }
    }
    return NULL;
}
static const char *value_end(const char *p, const char *end, unsigned depth)
{
    if (depth == 64 || (p = space(p, end)) == end) return NULL;
    if (*p == '"') return string_end(p, end);
    if (*p == '{' || *p == '[') {
        const int object = *p == '{';
        const char close = object ? '}' : ']';
        p = space(p + 1, end);
        if (p != end && *p == close) return p + 1;
        for (;;) {
            if (object) {
                p = string_end(p, end); if (!p) return NULL;
                p = space(p, end); if (p == end || *p++ != ':') return NULL;
            }
            p = value_end(p, end, depth + 1); if (!p) return NULL;
            p = space(p, end); if (p == end) return NULL;
            if (*p == close) return p + 1;
            if (*p++ != ',') return NULL;
            p = space(p, end);
        }
    }
    const char *first = p;
    while (p != end && !isspace((unsigned char)*p) && !strchr(",]}", *p)) ++p;
    return p != first ? p : NULL;
}
static int field(struct text object, const char *name, struct text *value)
{
    const char *p = space(object.begin, object.end);
    if (p == object.end || *p++ != '{') return 0;
    while ((p = space(p, object.end)) != object.end && *p != '}') {
        const char *key = p, *after = string_end(p, object.end);
        if (!after) return 0;
        p = space(after, object.end); if (p == object.end || *p++ != ':') return 0;
        p = space(p, object.end);
        const char *last = value_end(p, object.end, 0); if (!last) return 0;
        if ((size_t)(after - key) == strlen(name) + 2 && !memcmp(key + 1, name, strlen(name))) {
            value->begin = p; value->end = last; return 1;
        }
        p = space(last, object.end);
        if (p == object.end || *p != ',') break;
        ++p;
    }
    return 0;
}
static int equals(struct text value, const char *expected)
{ return (size_t)(value.end - value.begin) == strlen(expected) && !memcmp(value.begin, expected, strlen(expected)); }
struct pending {
    char id[132], mode[64];
    unsigned step;
    int active;
};
static struct pending pending[32];
static int emit_result(struct pending *call, const char *status, const char *data,
                       struct text *identity, const char *extra)
{
    printf("{\"type\":\"messages\",\"messages\":[{\"type\":\"result\",\"id\":%s,\"status\":\"%s\",\"data\":", call->id, status);
    if (identity) {
        fputs("{\"identity\":", stdout);
        fwrite(identity->begin, 1, (size_t)(identity->end - identity->begin), stdout);
        fputc('}', stdout);
    } else fputs(data, stdout);
    if (extra) fputs(extra, stdout);
    fputs("}]}\n", stdout);
    return fflush(stdout);
}
static int emit_host(struct pending *call, int network, struct text *request)
{
    printf("{\"type\":\"messages\",\"messages\":[{\"type\":\"host-call\",\"id\":\"call-%u\",\"invocation\":%s,\"operation\":\"%s\",\"request\":",
        call->step, call->id, network ? "network.http" : "playback.observe");
    if (request) fwrite(request->begin, 1, (size_t)(request->end - request->begin), stdout);
    else printf("{\"step\":%u}", call->step);
    fputs("}]}\n", stdout);
    return fflush(stdout);
}
static int copy_text(char *out, size_t capacity, struct text value)
{
    const size_t length = (size_t)(value.end - value.begin);
    if (length >= capacity) return 0;
    memcpy(out, value.begin, length); out[length] = 0; return 1;
}
static FILE *marker_file(const char *path)
{
#ifdef _WIN32
    wchar_t wide[1024];
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, 1024)) return NULL;
    return _wfopen(wide, L"wb");
#else
    return fopen(path, "wb");
#endif
}
int main(int argc, char **argv)
{
    (void)argv;
    if (argc != 2) return 2;
#ifdef _WIN32
    if (_setmode(_fileno(stdin), _O_BINARY) == -1 || _setmode(_fileno(stdout), _O_BINARY) == -1) return 3;
#endif
    puts("{\"type\":\"ready\",\"protocol\":\"wasm-message-v1\"}"); fflush(stdout);
    char *line = malloc(FRAME_LIMIT + 2); if (!line) return 4;
    unsigned messages = 0;
    char terminal_id[132] = {0}, terminal_mode[64] = {0}, eof_marker[1024] = {0};
    while (fgets(line, FRAME_LIMIT + 2, stdin)) {
        const size_t length = strlen(line);
        /* Synthetic test credentials must never enter a guest packet. */
        if (strstr(line, "unit-prepared-key")) { free(line); return 18; }
        if (++messages > MESSAGE_LIMIT || length > FRAME_LIMIT || !length || line[length - 1] != '\n') { free(line); return 5; }
        const struct text root = {line, line + length};
        const char *end = value_end(root.begin, root.end, 0);
        struct text type, id;
        if (!end || space(end, root.end) != root.end || !field(root, "type", &type)) { free(line); return 6; }
        if (equals(type, "\"cancel\"")) {
            struct text owner;
            if (!field(root, "id", &owner)) { free(line); return 7; }
            for (unsigned i = 0; i < 32; ++i)
                if (pending[i].active && equals(owner, pending[i].id)) pending[i].active = 0;
            continue;
        }
        if (equals(type, "\"host-result\"")) {
            struct text owner;
            if (!field(root, "invocation", &owner)) { free(line); return 7; }
            for (unsigned i = 0; i < 32; ++i) {
                struct pending *call = &pending[i];
                if (!call->active || !equals(owner, call->id)) continue;
                if (!strcmp(call->mode, "\"host-retry\"") && call->step++ == 1) {
                    if (emit_host(call, 0, NULL)) { free(line); return 8; }
                } else {
                    if (emit_result(call, "no_match", "{}", NULL, NULL)) { free(line); return 8; }
                    call->active = 0;
                }
                break;
            }
            continue;
        }
        if (!equals(type, "\"invoke\"")) continue;
        if (!field(root, "id", &id) || id.end - id.begin > 130 || *id.begin != '"') { free(line); return 7; }
        struct pending *call = NULL;
        for (unsigned i = 0; i < 32; ++i) if (!pending[i].active) { call = &pending[i]; break; }
        if (!call || !copy_text(call->id, sizeof(call->id), id)) { free(line); return 9; }
        strcpy(call->mode, "\"zero\""); call->step = 1; call->active = 1;
        struct text context = {0}, mode, identity, request;
        struct text media = {0};
        if (field(root, "context", &context)) {
            if ((field(context, "fixture", &mode) || field(context, "query", &mode))
                && !copy_text(call->mode, sizeof(call->mode), mode)) { free(line); return 10; }
            field(context, "media", &media);
        }
        struct text marker;
        if (media.begin && field(media, "fixtureEofMarker", &marker)) {
            const size_t bytes = (size_t)(marker.end - marker.begin);
            /* Owned temporary paths use UTF-8 and forward slashes in this fixture. */
            if (bytes < 2 || bytes - 2 >= sizeof(eof_marker) || *marker.begin != '"'
                || marker.end[-1] != '"' || memchr(marker.begin + 1, '\\', bytes - 2)) { free(line); return 19; }
            memcpy(eof_marker, marker.begin + 1, bytes - 2); eof_marker[bytes - 2] = 0;
        }
        if (!strcmp(call->mode, "\"hold\"")) continue;
        if (!strcmp(call->mode, "\"host\"") || !strcmp(call->mode, "\"host-retry\"")
            || !strcmp(call->mode, "\"pending\"")) {
            if (emit_host(call, 0, NULL)) { free(line); return 8; }
            if (strcmp(call->mode, "\"pending\"")) continue;
        } else if (!strcmp(call->mode, "\"network\"")) {
            if ((!field(context, "fixtureRequest", &request) && (!media.begin || !field(media, "fixtureRequest", &request))) || *request.begin != '{'
                || request.end - request.begin > 8192 || emit_host(call, 1, &request)) { free(line); return 11; }
            continue;
        }
        const char *status = "no_match", *data = "{}", *extra = NULL;
        struct text *echo = NULL;
        if (!strcmp(call->mode, "\"echo\"")) {
            if (!field(context, "identity", &identity)) { free(line); return 12; }
            echo = &identity;
        } else if (!strcmp(call->mode, "\"returned\"")) {
            if (!field(context, "fixtureIdentity", &identity)) { free(line); return 12; }
            echo = &identity;
        } else if (!strcmp(call->mode, "\"wrong-identity\"")) data = "{\"identity\":{\"kind\":\"movie\",\"title\":\"Other film\"}}";
        else if (!strcmp(call->mode, "\"mixed-numbering\"")) data = "{\"identity\":{\"kind\":\"episode\",\"title\":\"Fixture\",\"episodeNumbering\":{\"scheme\":\"seasonal\",\"season\":1,\"episodes\":[2],\"number\":\"99\"}}}";
        else if (!strcmp(call->mode, "\"records-type\"")) data = "{\"records\":{}}";
        else if (!strcmp(call->mode, "\"records-nonempty\"")) data = "{\"records\":[{\"fields\":{\"artwork\":\"image\"}}]}";
        else if (!strcmp(call->mode, "\"claim\"")) data = "{\"claim\":{}}";
        else if (!strcmp(call->mode, "\"cursor\"")) data = "{\"cursor\":\"next\"}";
        else if (!strcmp(call->mode, "\"partial\"")) status = "partial";
        else if (!strcmp(call->mode, "\"error-type\"")) extra = ",\"error\":false";
        else if (!strcmp(call->mode, "\"error-text\"")) extra = ",\"error\":\"A lookup failed.\"";
        else if (!strcmp(call->mode, "\"unknown-key\"")) extra = ",\"complete\":true";
        if (!strcmp(call->mode, "\"duplicate\"") || !strcmp(call->mode, "\"late-host\"")) {
            printf("{\"type\":\"messages\",\"messages\":[{\"type\":\"result\",\"id\":%s,\"status\":\"no_match\",\"data\":{}},", call->id);
            if (!strcmp(call->mode, "\"duplicate\""))
                printf("{\"type\":\"result\",\"id\":%s,\"status\":\"no_match\",\"data\":{}}]}\n", call->id);
            else printf("{\"type\":\"host-call\",\"id\":\"late\",\"invocation\":%s,\"operation\":\"playback.observe\",\"request\":{}}]}\n", call->id);
            if (fflush(stdout)) { free(line); return 8; }
        } else if (emit_result(call, status, data, echo, extra)) { free(line); return 8; }
        strcpy(terminal_id, call->id); strcpy(terminal_mode, call->mode);
        call->active = 0;
        if (!strcmp(terminal_mode, "\"immediate\"")) { free(line); return 0; }
    }
    const int failed = ferror(stdin); free(line);
    if (*eof_marker) {
        FILE *marker = marker_file(eof_marker);
        if (!marker) return 20;
        const int written = fputs("stdin-eof\n", marker);
        if (fclose(marker) || written < 0) return 20;
    }
    if (!strcmp(terminal_mode, "\"eof-duplicate\"")) {
        printf("{\"type\":\"messages\",\"messages\":[{\"type\":\"result\",\"id\":%s,\"status\":\"no_match\",\"data\":{}}]}\n", terminal_id);
        fflush(stdout);
    } else if (!strcmp(terminal_mode, "\"eof-fragment\"")) { fputs("{\"type\":", stdout); fflush(stdout); }
    else if (!strcmp(terminal_mode, "\"eof-crash\"")) return 17;
    else if (!strcmp(terminal_mode, "\"eof-hold\"")) {
#ifdef _WIN32
        Sleep(4000);
#else
        const struct timespec duration = {4, 0}; nanosleep(&duration, NULL);
#endif
    }
    return failed ? 13 : 0;
}

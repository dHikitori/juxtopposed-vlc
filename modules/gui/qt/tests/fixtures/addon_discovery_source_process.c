// SPDX-License-Identifier: GPL-2.0-or-later
// Native test process. No add-on code runs in this fixture.
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
static FILE *resolve_log(const char *program)
{
#ifdef _WIN32
    (void)program;
    wchar_t path[32768];
    const DWORD length = GetModuleFileNameW(NULL, path, 32768);
    const wchar_t suffix[] = L".resolves";
    if (!length || length >= 32768 - sizeof(suffix) / sizeof(*suffix)) return NULL;
    memcpy(path + length, suffix, sizeof(suffix));
    return _wfopen(path, L"ab");
#else
    const size_t length = strlen(program);
    if (length > 32760) return NULL;
    char *path = malloc(length + sizeof(".resolves")); if (!path) return NULL;
    memcpy(path, program, length); memcpy(path + length, ".resolves", sizeof(".resolves"));
    FILE *file = fopen(path, "ab"); free(path); return file;
#endif
}
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
#ifdef _WIN32
    if (_setmode(_fileno(stdin), _O_BINARY) == -1 || _setmode(_fileno(stdout), _O_BINARY) == -1) return 3;
#endif
    puts("{\"type\":\"ready\",\"protocol\":\"wasm-message-v1\"}"); fflush(stdout);
    char *line = malloc(FRAME_LIMIT + 2); if (!line) return 4;
    unsigned messages = 0;
    while (fgets(line, FRAME_LIMIT + 2, stdin)) {
        const size_t length = strlen(line);
        if (++messages > MESSAGE_LIMIT || length > FRAME_LIMIT || !length || line[length - 1] != '\n') { free(line); return 5; }
        const struct text root = {line, line + length};
        const char *end = value_end(root.begin, root.end, 0);
        struct text type, id, operation;
        if (!end || space(end, root.end) != root.end || !field(root, "type", &type)) { free(line); return 6; }
        if (!equals(type, "\"invoke\"")) continue;
        if (!field(root, "id", &id) || id.end - id.begin > 130 || *id.begin != '"'
            || !field(root, "operation", &operation)) { free(line); return 7; }
        const char *data;
        if (equals(operation, "\"resolve\"")) {
            FILE *file = resolve_log(argv[0]); if (!file) { free(line); return 8; }
            const int written = fputs("resolve\n", file), closed = fclose(file);
            if (written < 0 || closed) { free(line); return 9; }
            data = "{\"url\":\"https://media.example.test/audio.wav\",\"format\":\"progressive\"}";
        } else {
            data = "{\"items\":[{\"id\":\"station\",\"kind\":\"media\",\"title\":\"Station\",\"mediaKind\":\"musicTrack\"}]}";
        }
        fputs("{\"type\":\"messages\",\"messages\":[{\"type\":\"result\",\"id\":", stdout);
        fwrite(id.begin, 1, (size_t)(id.end - id.begin), stdout);
        printf(",\"status\":\"match\",\"data\":%s}]}\n", data);
        if (fflush(stdout)) { free(line); return 10; }
    }
    const int failed = ferror(stdin); free(line); return failed ? 11 : 0;
}

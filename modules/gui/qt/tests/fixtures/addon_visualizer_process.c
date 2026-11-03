// SPDX-License-Identifier: GPL-2.0-or-later
// Native test process. It records one request and keeps the response pending.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    FILE *file = fopen(argv[1], "rb"); if (!file) return 3;
    char control[8192]; const size_t count = fread(control, 1, sizeof(control) - 1, file); fclose(file); control[count] = 0;
    const char *field = strstr(control, "\"request\""); if (!field) return 4;
    field = strchr(field + 9, ':'); if (!field) return 4;
    do { ++field; } while (isspace((unsigned char)*field));
    if (*field++ != '"') return 4;
    char path[4096]; size_t length = 0;
    while (*field && *field != '"') {
        if (*field == '\\' || length + 1 == sizeof(path)) return 4;
        path[length++] = *field++;
    }
    if (*field != '"' || !length) return 4;
    path[length] = 0;
    puts("{\"type\":\"ready\",\"protocol\":\"wasm-message-v1\"}"); fflush(stdout);
    char *line = malloc(1024 * 1024 + 2); if (!line) return 5;
    while (fgets(line, 1024 * 1024 + 2, stdin)) {
        if (strstr(line, "\"type\":\"invoke\"")) {
            file = fopen(path, "wb"); if (!file) { free(line); return 6; }
            const size_t length = strlen(line); const size_t written = fwrite(line, 1, length, file); fclose(file);
            if (written != length) { free(line); return 7; }
            puts("{\"type\":\"messages\",\"messages\":[]}"); fflush(stdout);
        }
    }
    free(line); return 0;
}

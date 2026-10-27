// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <vlc_common.h>
#include <vlc_fs.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *const names[] = {
    "core-00-musicbrainz.lua", "core-01-google.lua", "core-02-frenchtv.lua",
    "core-03-lastfm.lua", "core-10-alpha.lua", "core-20-zulu.lua",
};
static bool known(const char *name)
{
    for (size_t i = 0; i < ARRAY_SIZE(names); ++i)
        if (!strcmp(name, names[i])) return true;
    return false;
}
static int selected(const char *name) { return !strncmp(name, "core-", 5); }
static int ascending(const char **left, const char **right)
{
    assert(known(*left) && known(*right));
    return strcmp(*left, *right);
}
static int descending(const char **left, const char **right)
{ return ascending(right, left); }
static void path(char *output, size_t capacity, const char *directory, const char *name)
{
    const int size = snprintf(output, capacity, "%s/%s", directory, name);
    assert(size >= 0 && (size_t)size < capacity);
}
static void verify(char **entries, int count, bool reverse)
{
    assert(count == (int)ARRAY_SIZE(names));
    for (int i = 0; i < count; ++i) {
        const size_t index = reverse ? ARRAY_SIZE(names) - 1 - i : (size_t)i;
        assert(!strcmp(entries[i], names[index])); free(entries[i]);
    }
    free(entries);
}
int main(void)
{
    char directory[] = "/tmp/hakone-directory-sort-XXXXXX", filename[256];
    assert(mkdtemp(directory) != NULL);
    for (size_t i = ARRAY_SIZE(names); i > 0; --i) {
        path(filename, sizeof(filename), directory, names[i - 1]);
        FILE *file = vlc_fopen(filename, "wb"); assert(file != NULL); assert(!fclose(file));
    }
    path(filename, sizeof(filename), directory, "skip.txt");
    FILE *file = vlc_fopen(filename, "wb"); assert(file != NULL); assert(!fclose(file));
    for (unsigned i = 0; i < 4; ++i) {
        char **entries = NULL;
        int count = vlc_scandir(directory, &entries, selected, ascending);
        verify(entries, count, false);
        vlc_DIR *dir = vlc_opendir(directory); assert(dir != NULL);
        count = vlc_loaddir(dir, &entries, selected, descending);
        vlc_closedir(dir); verify(entries, count, true);
    }
    assert(!unlink(filename));
    for (size_t i = 0; i < ARRAY_SIZE(names); ++i) {
        path(filename, sizeof(filename), directory, names[i]); assert(!unlink(filename));
    }
    assert(!rmdir(directory)); return 0;
}

// SPDX-License-Identifier: GPL-2.0-or-later
// Loaded into the app by addon_app_smoke.py --gui-file-audit. It records each
// open of a path under VLC_ADDON_GUI_FILE_AUDIT_ROOT, with the thread that
// opened it, in VLC_ADDON_GUI_FILE_AUDIT_LOG. The smoke test then fails when
// the GUI thread opened an add-on store file.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

typedef int (*open_fn)(const char *, int, ...);
typedef int (*openat_fn)(int, const char *, int, ...);
static __thread int busy;

static open_fn real_open(const char *name)
{
    return (open_fn)dlsym(RTLD_NEXT, name);
}

static void note(const char *path)
{
    const char *root = getenv("VLC_ADDON_GUI_FILE_AUDIT_ROOT");
    const char *log = getenv("VLC_ADDON_GUI_FILE_AUDIT_LOG");
    if (busy || !root || !log || !path || !*root || strncmp(path, root, strlen(root)) != 0)
        return;
    busy = 1;
    static open_fn plain;
    if (!plain)
        plain = real_open("open");
    const int fd = plain ? plain(log, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600) : -1;
    if (fd >= 0) {
        char line[4352];
        const int size = snprintf(line, sizeof line, "%ld\t%s\n", (long)syscall(SYS_gettid), path);
        if (size > 0 && (size_t)size < sizeof line && write(fd, line, (size_t)size) < 0) {}
        close(fd);
    }
    busy = 0;
}

static int mode_of(int flags, va_list arguments)
{
    return (flags & (O_CREAT | O_TMPFILE)) ? va_arg(arguments, int) : 0;
}

#define OPEN(name) \
int name(const char *path, int flags, ...) \
{ \
    va_list arguments; va_start(arguments, flags); const int mode = mode_of(flags, arguments); va_end(arguments); \
    note(path); \
    static open_fn next; if (!next) next = real_open(#name); \
    return next(path, flags, mode); \
}
#define OPENAT(name) \
int name(int directory, const char *path, int flags, ...) \
{ \
    va_list arguments; va_start(arguments, flags); const int mode = mode_of(flags, arguments); va_end(arguments); \
    note(path); \
    static openat_fn next; if (!next) next = (openat_fn)dlsym(RTLD_NEXT, #name); \
    return next(directory, path, flags, mode); \
}
OPEN(open)
OPEN(open64)
OPENAT(openat)
OPENAT(openat64)

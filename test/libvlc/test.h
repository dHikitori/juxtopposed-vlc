/*
 * test.h - libvlc smoke test common definitions
 *
 */

/**********************************************************************
 *  Copyright (C) 2007 Rémi Denis-Courmont.                           *
 *  Copyright (C) 2008 Pierre d'Herbemont.                            *
 *  This program is free software; you can redistribute and/or modify *
 *  it under the terms of the GNU General Public License as published *
 *  by the Free Software Foundation; version 2 of the license, or (at *
 *  your option) any later version.                                   *
 *                                                                    *
 *  This program is distributed in the hope that it will be useful,   *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of    *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.              *
 *  See the GNU General Public License for more details.              *
 *                                                                    *
 *  You should have received a copy of the GNU General Public License *
 *  along with this program; if not, you can get it from:             *
 *  http://www.gnu.org/copyleft/gpl.html                              *
 **********************************************************************/

#ifndef TEST_H
#define TEST_H

/*********************************************************************
 * Some useful common headers
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <vlc/vlc.h>
#include <vlc_common.h>
#include <vlc_threads.h>

#undef NDEBUG
#include <assert.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#ifdef _WIN32
# include <windows.h>
#endif

/*********************************************************************
 * Some useful global var
 */

static const char * test_defaults_args[] = {
    "-v", "--vout=vdummy", "--aout=adummy", "--text-renderer=tdummy",
};

static const int test_defaults_nargs =
    sizeof (test_defaults_args) / sizeof (test_defaults_args[0]);

static const char test_default_sample[] = "mock://";

/*********************************************************************
 * Some useful common functions
 */

#define test_log( ... ) fprintf(stderr, "testapi: " __VA_ARGS__);

static inline void test_setup(void)
{
    /* Standalone test executables can use an exported player package. */
    const char *modules = getenv("VLC_TEST_PLUGIN_PATH");
    if (modules == NULL || modules[0] == '\0') modules = TOP_BUILDDIR"/modules";
#ifdef _WIN32
    assert(_putenv_s("VLC_PLUGIN_PATH", modules) == 0);
    assert(_putenv_s("VLC_LIB_PATH", modules) == 0);
#else
    setenv("VLC_PLUGIN_PATH", modules, 1);
    setenv("VLC_LIB_PATH", modules, 1);
#endif
}

#ifdef _WIN32
static inline DWORD WINAPI test_watchdog(void *opaque)
{
    unsigned seconds = (uintptr_t)opaque;
    while (seconds-- != 0) Sleep(1000);
    fputs("testapi: timeout\n", stderr);
    ExitProcess(124);
}
#endif

static inline void test_init (void)
{
    (void)test_default_sample; /* This one may not be used */

    /* Make sure "make check" does not get stuck */
    /* Timeout of 10secs by default, unless the build sets another one */
#ifdef VLC_TEST_DEFAULT_TIMEOUT
    unsigned alarm_timeout = VLC_TEST_DEFAULT_TIMEOUT;
#else
    unsigned alarm_timeout = 10;
#endif
    /* Valid timeout value are < 0, for infinite, and > 0, for the number of
     * seconds */
    char *alarm_timeout_str = getenv("VLC_TEST_TIMEOUT");
    if (alarm_timeout_str)
    {
        int val = atoi(alarm_timeout_str);
        if (val <= 0)
            alarm_timeout = 0; /* infinite */
        else
            alarm_timeout = val;
    }
    if (alarm_timeout != 0)
    {
#ifdef _WIN32
        HANDLE watchdog = CreateThread(NULL, 0, test_watchdog,
                                      (void *)(uintptr_t)alarm_timeout, 0, NULL);
        assert(watchdog != NULL);
        CloseHandle(watchdog);
#else
        alarm (alarm_timeout);
#endif
    }

    test_setup();
}

#endif /* TEST_H */

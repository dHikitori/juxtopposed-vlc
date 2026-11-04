// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include "../../libvlc/test.h"
#include "../../../lib/libvlc_internal.h"
#if defined(HAVE_DBUS) && !defined(_WIN32) && !defined(__OS2__)
#include <stdatomic.h>
#include <sys/wait.h>
#include <errno.h>

static atomic_uint raised;
static int on_raise(vlc_object_t *obj, const char *name, vlc_value_t old,
                    vlc_value_t value, void *opaque)
{
    (void)obj; (void)name; (void)old; (void)value; (void)opaque;
    atomic_fetch_add(&raised, 1);
    return VLC_SUCCESS;
}
int main(int argc, char **argv)
{
    if (argc == 1) {
        execlp("dbus-run-session", "dbus-run-session", "--", argv[0],
               "--private-bus", (char *)NULL);
        if (errno == ENOENT)
            return 77;
        perror("dbus-run-session");
        return 1;
    }
    assert(argc == 2 && !strcmp(argv[1], "--private-bus"));
    test_init();
    const char *args[] = {"--ignore-config", "--no-plugins-cache", "--no-media-library", "--dbus"};
    libvlc_instance_t *instance = libvlc_new(ARRAY_SIZE(args), args);
    assert(instance);
    libvlc_int_t *vlc = instance->p_libvlc_int;
    assert(var_GetBool(vlc, "single-instance-listening"));
    var_AddCallback(vlc, "intf-show", on_raise, NULL);
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        execl(TOP_BUILDDIR "/bin/hakone", "hakone", "--ignore-config", "--no-plugins-cache",
              "--no-media-library", "--one-instance", "--intf=dummy", (char *)NULL);
        _exit(127);
    }
    int status;
    while (waitpid(child, &status, 0) == -1) assert(errno == EINTR);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    unsigned actual = atomic_load(&raised);
    fprintf(stderr, "Empty relaunch: %u raise callback(s)\n", actual);
    assert(actual == 1);
    var_DelCallback(vlc, "intf-show", on_raise, NULL);
    libvlc_release(instance);
    return 0;
}

#else
int main(void)
{
    return 77;
}
#endif

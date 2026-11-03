// SPDX-License-Identifier: LGPL-2.1-or-later
/* Hakone copies the pre-rebrand configuration and data folders on its first
 * start, but only from a VLC 4 profile, and only once. */
#include "../../libvlc/test.h"
#include <vlc_fs.h>

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>

#ifdef __APPLE__
# define CONFIG_PARENT "Library/Preferences"
# define DATA_PARENT "Library/Application Support"
# define LEGACY_NAME "org.juxtopposed.vlc"
# define NEW_NAME "org.hakone.hakone"
#else
# define CONFIG_PARENT "config"
# define DATA_PARENT "data"
# define LEGACY_NAME "vlc"
# define NEW_NAME "hakone"
#endif

static char root[256];

static char *Path(const char *parent, const char *name, const char *file)
{
    char *path;
    assert(asprintf(&path, "%s/%s/%s%s%s", root, parent, name,
                    file ? "/" : "", file ? file : "") >= 0);
    return path;
}

static void MakeDirs(char *path)
{
    for (char *sep = strchr(path + 1, '/'); sep != NULL; sep = strchr(sep + 1, '/'))
    {
        *sep = '\0';
        assert(vlc_mkdir(path, 0700) == 0 || errno == EEXIST);
        *sep = '/';
    }
    assert(vlc_mkdir(path, 0700) == 0 || errno == EEXIST);
}

static void Write(const char *parent, const char *name, const char *file,
                  const char *text)
{
    char *dir = Path(parent, name, NULL);
    MakeDirs(dir);
    free(dir);
    char *path = Path(parent, name, file);
    FILE *stream = vlc_fopen(path, "wb");
    assert(stream != NULL);
    assert(fputs(text, stream) >= 0);
    assert(fclose(stream) == 0);
    free(path);
}

static bool Exists(const char *parent, const char *name, const char *file)
{
    char *path = Path(parent, name, file);
    struct stat st;
    bool exists = vlc_stat(path, &st) == 0;
    free(path);
    return exists;
}

static bool Contains(const char *parent, const char *name, const char *file,
                     const char *text)
{
    char *path = Path(parent, name, file);
    FILE *stream = vlc_fopen(path, "rb");
    free(path);
    if (stream == NULL)
        return false;
    char buf[1024];
    size_t len = fread(buf, 1, sizeof (buf) - 1, stream);
    fclose(stream);
    buf[len] = '\0';
    return strstr(buf, text) != NULL;
}

static void Start(void)
{
    const char *args[] = { "--no-ignore-config", "--no-media-library",
                           "--no-video" };
    libvlc_instance_t *instance = libvlc_new(ARRAY_SIZE(args), args);
    assert(instance != NULL);
    libvlc_release(instance);
}

static void NewProfile(const char *version)
{
    const char *tmp = getenv("TMPDIR");
    snprintf(root, sizeof (root), "%s/hakone-migrate-XXXXXX", tmp ? tmp : "/tmp");
    assert(mkdtemp(root) != NULL);
    setenv("HOME", root, 1);
    char *config, *data;
    assert(asprintf(&config, "%s/" CONFIG_PARENT, root) >= 0);
    assert(asprintf(&data, "%s/" DATA_PARENT, root) >= 0);
    setenv("XDG_CONFIG_HOME", config, 1);
    setenv("XDG_DATA_HOME", data, 1);
    free(config);
    free(data);

    char vlcrc[256];
    snprintf(vlcrc, sizeof (vlcrc),
             "\xEF\xBB\xBF###\n###  vlc %s\n###\n\n[core]\nvolume-step=7.000000\n",
             version);
    Write(CONFIG_PARENT, LEGACY_NAME, "vlcrc", vlcrc);
    Write(DATA_PARENT, LEGACY_NAME "/ml", "ml.db", "library");
}

int main(void)
{
    test_init();
    unsetenv("VLC_USERDATA_PATH");

    /* A beta profile is copied, and the old folders stay for VLC. */
    NewProfile("4.0.0-beta.5");
    Start();
    assert(Contains(CONFIG_PARENT, NEW_NAME, "vlcrc", "volume-step=7"));
    assert(Contains(DATA_PARENT, NEW_NAME "/ml", "ml.db", "library"));
    assert(Exists(CONFIG_PARENT, LEGACY_NAME, "vlcrc"));
    assert(!Exists(CONFIG_PARENT, NEW_NAME ".migrating", NULL));

    /* Later starts keep Hakone's own folders. */
    Write(DATA_PARENT, LEGACY_NAME, "later.txt", "later");
    Start();
    assert(!Exists(DATA_PARENT, NEW_NAME, "later.txt"));

    /* An interrupted copy is finished on the next start. */
    NewProfile("4.0.0-dev");
    Write(CONFIG_PARENT, NEW_NAME ".migrating", "vlcrc", "partial");
    Start();
    assert(Contains(CONFIG_PARENT, NEW_NAME, "vlcrc", "volume-step=7"));
    assert(Contains(DATA_PARENT, NEW_NAME "/ml", "ml.db", "library"));

    /* VLC 3 wrote the same folders on Linux: leave its profile alone. */
    NewProfile("3.0.21");
    Start();
    assert(!Exists(DATA_PARENT, NEW_NAME "/ml", "ml.db"));
    assert(!Contains(CONFIG_PARENT, NEW_NAME, "vlcrc", "volume-step=7"));
    return 0;
}

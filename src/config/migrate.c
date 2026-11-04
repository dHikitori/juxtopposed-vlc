// SPDX-License-Identifier: LGPL-2.1-or-later
/*****************************************************************************
 * migrate.c: Copy settings and data from the folders used before Hakone
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <vlc_common.h>
#include <vlc_configuration.h>
#include <vlc_fs.h>
#include "configuration.h"

/* Folder names used before the rebrand, by the names that replaced them.
 * The macOS betas used their own bundle identifier; elsewhere they shared
 * VLC's folders. A portable or differently bundled profile matches neither. */
static const struct
{
    const char *name;
    const char *legacy;
} legacy_names[] = {
    { "hakone", "vlc" },
    { "org.hakone.hakone", "org.juxtopposed.vlc" },
};

static char *LegacyDir(const char *dir)
{
    const char *sep = strrchr(dir, DIR_SEP_CHAR);
    if (sep == NULL)
        return NULL;

    const char *base = sep + 1;
    for (size_t i = 0; i < ARRAY_SIZE(legacy_names); i++)
    {
        if (strcmp(base, legacy_names[i].name))
            continue;

        char *legacy;
        if (asprintf(&legacy, "%.*s%s", (int)(base - dir), dir,
                     legacy_names[i].legacy) == -1)
            return NULL;
        return legacy;
    }
    return NULL;
}

static bool Exists(const char *path)
{
    struct stat st;
    return vlc_stat(path, &st) == 0;
}

/* VLC 3 wrote the same folders on Linux and Windows. Only copy a profile
 * whose configuration file VLC 4, which the betas were built on, wrote. */
static bool IsVLC4Profile(const char *dir)
{
    char *path;
    if (asprintf(&path, "%s" DIR_SEP CONFIG_FILE, dir) == -1)
        return false;

    FILE *file = vlc_fopen(path, "rb");
    free(path);
    if (file == NULL)
        return false;

    char header[64];
    size_t len = fread(header, 1, sizeof (header) - 1, file);
    fclose(file);
    header[len] = '\0';
    return strstr(header, "\n###  vlc 4.") != NULL;
}

static int CopyFile(const char *from, const char *to, int mode)
{
    int in = vlc_open(from, O_RDONLY);
    if (in == -1)
        return -1;

    int out = vlc_open(to, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (out == -1)
    {
        vlc_close(in);
        return -1;
    }

    char buf[16384];
    ssize_t len;
    int ret = 0;
    while (ret == 0 && (len = read(in, buf, sizeof (buf))) != 0)
    {
        if (len < 0)
        {
            ret = -1;
            break;
        }
        for (ssize_t done = 0; done < len;)
        {
            ssize_t written = write(out, buf + done, len - done);
            if (written <= 0)
            {
                ret = -1;
                break;
            }
            done += written;
        }
    }

    vlc_close(in);
    if (vlc_close(out))
        ret = -1;
    return ret;
}

static int CopyTree(const char *from, const char *to)
{
    if (vlc_mkdir(to, 0700) && errno != EEXIST)
        return -1;

    vlc_DIR *dir = vlc_opendir(from);
    if (dir == NULL)
        return -1;

    int ret = 0;
    const char *name;
    while (ret == 0 && (name = vlc_readdir(dir)) != NULL)
    {
        if (!strcmp(name, ".") || !strcmp(name, ".."))
            continue;

        char *src, *dst;
        if (asprintf(&src, "%s" DIR_SEP "%s", from, name) == -1)
        {
            ret = -1;
            break;
        }
        if (asprintf(&dst, "%s" DIR_SEP "%s", to, name) == -1)
        {
            free(src);
            ret = -1;
            break;
        }

        struct stat st;
        if (vlc_lstat(src, &st))
            ret = -1;
        else if (S_ISDIR(st.st_mode))
            ret = CopyTree(src, dst);
        else if (S_ISREG(st.st_mode))
            ret = CopyFile(src, dst, (st.st_mode & 0777) | 0600);
        /* Links, sockets and the like belong to a running instance. */

        free(src);
        free(dst);
    }
    vlc_closedir(dir);
    return ret;
}

/* Copies a folder beside its final path. An interrupted copy leaves only
 * that folder behind, and the next start copies over it again. */
static char *CopyBeside(vlc_object_t *obj, const char *legacy, const char *dir)
{
    char *temp;
    if (asprintf(&temp, "%s.migrating", dir) == -1)
        return NULL;

    if (CopyTree(legacy, temp) == 0)
        return temp;

    msg_Warn(obj, "cannot copy %s to %s: %s", legacy, temp,
             vlc_strerror_c(errno));
    free(temp);
    return NULL;
}

static void Finish(vlc_object_t *obj, const char *legacy, const char *temp,
                   const char *dir)
{
    if (vlc_rename(temp, dir) == 0)
        msg_Info(obj, "copied %s to %s", legacy, dir);
    else
        msg_Warn(obj, "cannot rename %s to %s: %s", temp, dir,
                 vlc_strerror_c(errno));
}

void config_MigrateLegacyDirs(vlc_object_t *obj)
{
    /* An explicit data folder is a test or a custom setup: leave it alone. */
    if (getenv("VLC_USERDATA_PATH") != NULL)
        return;

    char *config = platform_GetUserDir(VLC_CONFIG_DIR);
    char *data = platform_GetUserDir(VLC_USERDATA_DIR);
    char *legacy_config = config != NULL ? LegacyDir(config) : NULL;
    char *legacy_data = data != NULL ? LegacyDir(data) : NULL;

    /* Only before the first start: after that Hakone keeps its own folders. */
    if (legacy_config == NULL || legacy_data == NULL
     || Exists(config) || Exists(data) || !IsVLC4Profile(legacy_config))
        goto out;

    /* Windows keeps both in one folder. */
    const bool shared = !strcmp(config, data);
    char *config_temp = CopyBeside(obj, legacy_config, config);
    char *data_temp = NULL;
    if (config_temp != NULL && !shared && Exists(legacy_data))
    {
        data_temp = CopyBeside(obj, legacy_data, data);
        if (data_temp == NULL)
            goto done;
        Finish(obj, legacy_data, data_temp, data);
    }
    if (config_temp != NULL)
        Finish(obj, legacy_config, config_temp, config);
done:
    free(data_temp);
    free(config_temp);
out:
    free(legacy_data);
    free(legacy_config);
    free(data);
    free(config);
}

/*****************************************************************************
 * dirs.c: directories configuration
 *****************************************************************************
 * Copyright (C) 2001-2010 VLC authors and VideoLAN
 * Copyright © 2007-2012 Rémi Denis-Courmont
 *
 * Authors: Gildas Bazin <gbazin@videolan.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>

#include <shlobj.h>

#include <vlc_charset.h>
#include "../config/configuration.h"

#include <assert.h>


static char *config_GetKnownFolder (KNOWNFOLDERID id)
{
    PWSTR wdir = NULL;

    const HRESULT hr = SHGetKnownFolderPath (&id, 0, NULL, &wdir);

    char *result = NULL;

    if ( hr == S_OK )
        result = FromWide(wdir);

    CoTaskMemFree(wdir);
    return result;
}

static bool config_CanWritePortableDir(const WCHAR *directory)
{
    WCHAR filename[MAX_PATH];
    const DWORD pid = GetCurrentProcessId(), tick = GetTickCount();
    for (unsigned attempt = 0; attempt < 8; ++attempt)
    {
        if (swprintf(filename, ARRAY_SIZE(filename),
                     L"%ls\\vlc-write-%lu-%lu-%u.tmp", directory, pid, tick,
                     attempt) < 0)
            return false;

        HANDLE file = CreateFileW(filename, GENERIC_WRITE | DELETE, 0, NULL,
                                  CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY |
                                  FILE_FLAG_DELETE_ON_CLOSE, NULL);
        if (file != INVALID_HANDLE_VALUE)
        {
            DWORD written;
            const bool writable = WriteFile(file, "v", 1, &written, NULL)
                               && written == 1;
            CloseHandle(file);
            return writable;
        }
        if (GetLastError() != ERROR_FILE_EXISTS &&
            GetLastError() != ERROR_ALREADY_EXISTS)
            break;
    }
    return false;
}

static BOOL CALLBACK config_InitPortableDir(PINIT_ONCE once, PVOID data,
                                           PVOID *context)
{
    VLC_UNUSED(once);
    VLC_UNUSED(context);
    WCHAR *portable = data;
    WCHAR path[MAX_PATH];
    DWORD read = GetModuleFileNameW (NULL, path, ARRAY_SIZE(path));
    if (read != 0 && read != ARRAY_SIZE(path))
    {
        WCHAR *lastDir = wcsrchr (path, L'\\');
        if (lastDir)
        {
            *lastDir = L'\0';
            if (wcscat_s(path, ARRAY_SIZE(path), TEXT("\\portable")) == 0)
            {
                DWORD attrib = GetFileAttributesW (path);
                if (attrib != INVALID_FILE_ATTRIBUTES &&
                    (attrib & FILE_ATTRIBUTE_DIRECTORY) &&
                    config_CanWritePortableDir(path))
                    wcscpy_s(portable, MAX_PATH, path);
            }
        }
    }
    return TRUE;
}

static char *config_GetAppDir (void)
{
    /* Keep one profile for the process, including on slow network shares. */
    static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
    static WCHAR portable[MAX_PATH];
    InitOnceExecuteOnce(&once, config_InitPortableDir, portable, NULL);
    if (portable[0] != L'\0')
        return FromWide(portable);

    char *psz_dir;
    char *psz_parent = config_GetKnownFolder (FOLDERID_RoamingAppData);

    if (psz_parent == NULL
     ||  asprintf (&psz_dir, "%s\\hakone", psz_parent) == -1)
        psz_dir = NULL;
    free (psz_parent);
    return psz_dir;
}

char *platform_GetUserDir (vlc_userdir_t type)
{
    switch (type)
    {
        case VLC_HOME_DIR:
            return config_GetKnownFolder (FOLDERID_Documents);
        case VLC_CONFIG_DIR:
        case VLC_USERDATA_DIR:
            return config_GetAppDir ();
        case VLC_CACHE_DIR:
            return config_GetAppDir ();
        case VLC_DESKTOP_DIR:
            return config_GetKnownFolder (FOLDERID_Desktop);
        case VLC_DOWNLOAD_DIR:
            return config_GetKnownFolder (FOLDERID_Downloads);
        case VLC_TEMPLATES_DIR:
        case VLC_PUBLICSHARE_DIR:
        case VLC_DOCUMENTS_DIR:
            return config_GetUserDir(VLC_HOME_DIR);
        case VLC_MUSIC_DIR:
            return config_GetKnownFolder (FOLDERID_Music);
        case VLC_SNAPSHOTS_DIR:
        {
            char *folder = config_GetKnownFolder (FOLDERID_Screenshots);
            if (folder != NULL)
                return folder;
            /* fall through */
        }
        case VLC_PICTURES_DIR:
            return config_GetKnownFolder (FOLDERID_Pictures);
        case VLC_VIDEOS_DIR:
            return config_GetKnownFolder (FOLDERID_Videos);
    }
    vlc_assert_unreachable ();
}

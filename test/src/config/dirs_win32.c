/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Exercise the Windows profile selector with real filesystem permissions. */
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#undef NDEBUG
#include <assert.h>
#include <vlc_common.h>
#include <vlc_charset.h>
#include <shlobj.h>
#include <aclapi.h>

static WCHAR executable[MAX_PATH], roaming[MAX_PATH];

static DWORD test_GetModuleFileNameW(HMODULE module, LPWSTR path, DWORD length)
{
    assert(module == NULL && wcslen(executable) < length);
    wcscpy_s(path, length, executable);
    return wcslen(path);
}

static HRESULT test_SHGetKnownFolderPath(REFKNOWNFOLDERID id, DWORD flags,
                                        HANDLE token, PWSTR *path)
{
    assert(IsEqualGUID(id, &FOLDERID_RoamingAppData));
    VLC_UNUSED(flags); VLC_UNUSED(token);
    const size_t bytes = (wcslen(roaming) + 1) * sizeof(WCHAR);
    *path = CoTaskMemAlloc(bytes);
    assert(*path != NULL);
    memcpy(*path, roaming, bytes);
    return S_OK;
}

/* Redirect only directory discovery, keeping the production Windows I/O. */
#define GetModuleFileNameW test_GetModuleFileNameW
#define SHGetKnownFolderPath test_SHGetKnownFolderPath
#define platform_GetUserDir test_platform_GetUserDir
#include "../../../src/win32/dirs.c"
#undef platform_GetUserDir
#undef SHGetKnownFolderPath
#undef GetModuleFileNameW

static void path_join(WCHAR *path, const WCHAR *base, const WCHAR *name)
{
    assert(swprintf(path, MAX_PATH, L"%ls\\%ls", base, name) > 0);
}

static PSECURITY_DESCRIPTOR deny_writes(WCHAR *directory, PACL *original)
{
    PSECURITY_DESCRIPTOR descriptor;
    assert(GetNamedSecurityInfoW(directory, SE_FILE_OBJECT,
             DACL_SECURITY_INFORMATION, NULL, NULL, original, NULL,
             &descriptor) == ERROR_SUCCESS);
    BYTE sid[SECURITY_MAX_SID_SIZE];
    DWORD size = sizeof(sid);
    assert(CreateWellKnownSid(WinWorldSid, NULL, sid, &size));
    EXPLICIT_ACCESSW deny = {0};
    deny.grfAccessPermissions = FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY |
        FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA | DELETE | FILE_DELETE_CHILD;
    deny.grfAccessMode = DENY_ACCESS;
    deny.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    deny.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    deny.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    deny.Trustee.ptstrName = (LPWSTR)sid;
    PACL acl;
    assert(SetEntriesInAclW(1, &deny, *original, &acl) == ERROR_SUCCESS);
    assert(SetNamedSecurityInfoW(directory, SE_FILE_OBJECT,
             DACL_SECURITY_INFORMATION, NULL, NULL, acl, NULL) == ERROR_SUCCESS);
    LocalFree(acl);
    return descriptor;
}

int main(int argc, char **argv)
{
    if (argc == 1)
    {
        WCHAR self[MAX_PATH];
        assert(GetModuleFileNameW(NULL, self, MAX_PATH));
        const WCHAR *cases[] = {L"writable", L"denied", L"absent", L"attribute", L"file"};
        for (size_t i = 0; i < ARRAY_SIZE(cases); ++i)
        {
            WCHAR command[MAX_PATH + 32];
            assert(swprintf(command, ARRAY_SIZE(command), L"\"%ls\" %ls", self, cases[i]) > 0);
            STARTUPINFOW startup = {.cb = sizeof(startup)};
            PROCESS_INFORMATION process;
            assert(CreateProcessW(self, command, NULL, NULL, FALSE, 0, NULL, NULL,
                                  &startup, &process));
            assert(WaitForSingleObject(process.hProcess, 10000) == WAIT_OBJECT_0);
            DWORD code; assert(GetExitCodeProcess(process.hProcess, &code));
            CloseHandle(process.hThread); CloseHandle(process.hProcess);
            assert(code == 0);
        }
        return 0;
    }
    assert(argc == 2);
    const bool denied = !strcmp(argv[1], "denied");
    const bool absent = !strcmp(argv[1], "absent");
    const bool attribute = !strcmp(argv[1], "attribute");
    const bool file = !strcmp(argv[1], "file");
    assert(denied || absent || attribute || file || !strcmp(argv[1], "writable"));
    WCHAR temp[MAX_PATH], root[MAX_PATH], app[MAX_PATH], portable[MAX_PATH];
    assert(GetTempPathW(MAX_PATH, temp));
    assert(GetTempFileNameW(temp, L"vcp", 0, root));
    assert(DeleteFileW(root) && CreateDirectoryW(root, NULL));
    path_join(app, root, L"app-\x7528\x6237");
    assert(CreateDirectoryW(app, NULL));
    path_join(executable, app, L"hakone.exe");
    path_join(portable, app, L"portable");
    if (file)
    {
        HANDLE handle = CreateFileW(portable, GENERIC_WRITE, 0, NULL,
                                   CREATE_NEW, 0, NULL);
        assert(handle != INVALID_HANDLE_VALUE); CloseHandle(handle);
    }
    else if (!absent)
        assert(CreateDirectoryW(portable, NULL));
    path_join(roaming, root, L"roaming");
    assert(CreateDirectoryW(roaming, NULL));

    PACL original = NULL;
    PSECURITY_DESCRIPTOR descriptor = denied ? deny_writes(portable, &original) : NULL;
    if (attribute)
        assert(SetFileAttributesW(portable, FILE_ATTRIBUTE_READONLY));

    const bool use_portable = !denied && !absent && !file;
    WCHAR expected[MAX_PATH];
    if (use_portable) wcscpy_s(expected, MAX_PATH, portable);
    else path_join(expected, roaming, L"vlc");
    char *expected_utf8 = FromWide(expected);
    char *selected = test_platform_GetUserDir(VLC_CONFIG_DIR);
    bool passed = selected != NULL && !strcmp(selected, expected_utf8);
    free(selected);

    if (denied)
    {
        assert(SetNamedSecurityInfoW(portable, SE_FILE_OBJECT,
                 DACL_SECURITY_INFORMATION, NULL, NULL, original, NULL) == ERROR_SUCCESS);
        LocalFree(descriptor);
    }
    if (attribute)
        assert(SetFileAttributesW(portable, FILE_ATTRIBUTE_NORMAL));

    /* All consumers keep the same profile, even if permissions later change. */
    for (vlc_userdir_t type = VLC_CONFIG_DIR; type <= VLC_CACHE_DIR; ++type)
    {
        selected = test_platform_GetUserDir(type);
        passed = passed && selected != NULL && !strcmp(selected, expected_utf8);
        free(selected);
    }
    if (passed)
    {
        if (!use_portable) assert(CreateDirectoryW(expected, NULL));
        WCHAR setting[MAX_PATH]; path_join(setting, expected, L"vlcrc");
        HANDLE handle = CreateFileW(setting, GENERIC_WRITE, 0, NULL,
                                   CREATE_NEW, 0, NULL);
        assert(handle != INVALID_HANDLE_VALUE);
        DWORD written; assert(WriteFile(handle, "saved", 5, &written, NULL) && written == 5);
        CloseHandle(handle);
        handle = CreateFileW(setting, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        assert(handle != INVALID_HANDLE_VALUE);
        char contents[5]; DWORD read;
        assert(ReadFile(handle, contents, sizeof(contents), &read, NULL));
        assert(read == 5 && !memcmp(contents, "saved", 5)); CloseHandle(handle);
        assert(DeleteFileW(setting));
        if (!use_portable) assert(RemoveDirectoryW(expected));
    }
    free(expected_utf8);
    if (file) assert(DeleteFileW(portable));
    else if (!absent) assert(RemoveDirectoryW(portable)); /* No probe files remain. */
    assert(RemoveDirectoryW(roaming) && RemoveDirectoryW(app) && RemoveDirectoryW(root));
    assert(passed);
    return 0;
}

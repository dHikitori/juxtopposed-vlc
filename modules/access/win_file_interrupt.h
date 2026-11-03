// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_ACCESS_WIN_FILE_INTERRUPT_H
#define VLC_ACCESS_WIN_FILE_INTERRUPT_H 1
#include <vlc_common.h>
#include <wchar.h>
#include <windows.h>
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
# define VLC_WIN_FILE_INTERRUPT 1
#endif

/* Private Windows access helper. Its owner keeps it alive through every read
 * and close call, and deletes it only after its callers have returned. */
typedef struct vlc_win_file_interrupt vlc_win_file_interrupt_t;
typedef ssize_t (*vlc_win_file_read_cb)(void *, void *, size_t);
/* Return 1 only for a UNC or mapped remote drive path, 0 for local/device
 * paths, and -1 if an absolute path cannot be formed. No file is opened. */
int vlc_win_file_remote_path(const wchar_t *);
vlc_win_file_interrupt_t *vlc_win_file_interrupt_new(void);
void vlc_win_file_interrupt_close(vlc_win_file_interrupt_t *);
void vlc_win_file_interrupt_delete(vlc_win_file_interrupt_t *);
/* The callback performs only its one synchronous native/CRT file read, with
 * no logging, other I/O or user callback. It may use native event waits in a
 * fixture. The caller cannot start another I/O until this wrapper returns.
 * The wrapper saves/restores native/CRT error state and joins cancellation
 * acknowledgement before returning, including cancellation before issue. */
ssize_t vlc_win_file_interrupt_read(vlc_win_file_interrupt_t *,
    vlc_win_file_read_cb, void *, void *, size_t);
#endif

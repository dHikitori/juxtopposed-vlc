// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include "win_file_interrupt.h"
#include <vlc_interrupt.h>
#include <vlc_threads.h>
#include <windows.h>
#include <errno.h>
#include <stdlib.h>

struct vlc_win_file_interrupt {
    SRWLOCK lock;
    HANDLE wake, cancel, read_done, ack, close_done;
    HANDLE target;
    vlc_thread_t worker;
    bool active, closing;
    uint64_t sequence;
};

static bool separator(wchar_t c) { return c == L'\\' || c == L'/'; }

int vlc_win_file_remote_path(const wchar_t *path)
{
    if (!path) return 0;
    /* Reject device namespaces, but retain extended UNC and drive paths. */
    if (separator(path[0]) && separator(path[1])) {
        if (path[2] == L'.' && (!path[3] || separator(path[3]))) return 0;
        if (path[2] == L'?' && !path[3]) return 0;
        if (path[2] == L'?' && separator(path[3])) {
            path += 4;
            if (!_wcsnicmp(path, L"UNC\\", 4)) return path[4] != 0;
            if (!((path[0] >= L'A' && path[0] <= L'Z') ||
                  (path[0] >= L'a' && path[0] <= L'z')) || path[1] != L':')
                return 0;
        } else return path[2] != 0;
    }
    DWORD count = GetFullPathNameW(path, 0, NULL, NULL);
    if (!count || count > 32768) { errno = EINVAL; return -1; }
    wchar_t *absolute = malloc((size_t)count * sizeof(*absolute));
    if (!absolute) { errno = ENOMEM; return -1; }
    DWORD result = GetFullPathNameW(path, count, absolute, NULL);
    if (!result || result >= count) { free(absolute); errno = EINVAL; return -1; }
    bool remote = false;
    if (absolute[0] && absolute[1] == L':') {
        wchar_t drive[] = {absolute[0], L':', L'\\', 0};
        remote = GetDriveTypeW(drive) == DRIVE_REMOTE;
    }
    free(absolute);
    return remote;
}

static void interrupt_read(void *opaque)
{
    vlc_win_file_interrupt_t *reader = opaque;
    /* vlc_interrupt invokes this under its own lock. Never wait here. */
    SetEvent(reader->cancel);
}

static void *cancel_thread(void *opaque)
{
    vlc_win_file_interrupt_t *reader = opaque;
    uint64_t seen = 0;
    for (;;) {
        WaitForSingleObject(reader->wake, INFINITE);
        AcquireSRWLockExclusive(&reader->lock);
        if (reader->closing && !reader->active) {
            ReleaseSRWLockExclusive(&reader->lock);
            return NULL;
        }
        if (!reader->active || reader->sequence == seen) {
            ReleaseSRWLockExclusive(&reader->lock);
            continue;
        }
        /* End/start wakes may coexist. Process each read scope once, so a
         * stale wake cannot retain its old target across the next event reset. */
        seen = reader->sequence;
        HANDLE target = reader->target;
        ReleaseSRWLockExclusive(&reader->lock);

        HANDLE events[] = {reader->read_done, reader->cancel};
        DWORD event = WaitForMultipleObjects(2, events, FALSE, INFINITE);
        if (event != WAIT_OBJECT_0) {
            /* ERROR_NOT_FOUND can mean the guarded read has not started yet.
             * Retry through that gap. Never target a later operation: the
             * reader cannot return or do another I/O until ack below. */
            while (WaitForSingleObject(reader->read_done, 0) != WAIT_OBJECT_0) {
                CancelSynchronousIo(target);
                WaitForSingleObject(reader->read_done, 10);
            }
        }
        /* No access to the target handle after ack. Its duplicated reference
         * stays owned by the read until this acknowledgement. */
        SetEvent(reader->ack);
    }
}

static void close_events(vlc_win_file_interrupt_t *reader)
{
    if (reader->wake) CloseHandle(reader->wake);
    if (reader->cancel) CloseHandle(reader->cancel);
    if (reader->read_done) CloseHandle(reader->read_done);
    if (reader->ack) CloseHandle(reader->ack);
    if (reader->close_done) CloseHandle(reader->close_done);
}

vlc_win_file_interrupt_t *vlc_win_file_interrupt_new(void)
{
    vlc_win_file_interrupt_t *reader = calloc(1, sizeof(*reader));
    if (!reader) { errno = ENOMEM; SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    InitializeSRWLock(&reader->lock);
    reader->wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    reader->cancel = CreateEventW(NULL, TRUE, FALSE, NULL);
    reader->read_done = CreateEventW(NULL, TRUE, FALSE, NULL);
    reader->ack = CreateEventW(NULL, TRUE, FALSE, NULL);
    reader->close_done = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!reader->wake || !reader->cancel || !reader->read_done || !reader->ack || !reader->close_done
        || vlc_clone(&reader->worker, cancel_thread, reader)) {
        DWORD error = GetLastError();
        close_events(reader); free(reader);
        errno = ENOMEM; SetLastError(error ? error : ERROR_NOT_ENOUGH_MEMORY); return NULL;
    }
    return reader;
}

void vlc_win_file_interrupt_close(vlc_win_file_interrupt_t *reader)
{
    if (!reader) return;
    int saved_errno = errno; DWORD saved_error = GetLastError();
    AcquireSRWLockExclusive(&reader->lock);
    bool join = !reader->closing;
    reader->closing = true;
    SetEvent(reader->cancel); SetEvent(reader->wake);
    ReleaseSRWLockExclusive(&reader->lock);
    if (join) {
        vlc_join(reader->worker, NULL);
        SetEvent(reader->close_done);
    } else WaitForSingleObject(reader->close_done, INFINITE);
    errno = saved_errno; SetLastError(saved_error);
}

void vlc_win_file_interrupt_delete(vlc_win_file_interrupt_t *reader)
{
    if (!reader) return;
    int saved_errno = errno; DWORD saved_error = GetLastError();
    vlc_win_file_interrupt_close(reader);
    close_events(reader); free(reader);
    errno = saved_errno; SetLastError(saved_error);
}

ssize_t vlc_win_file_interrupt_read(vlc_win_file_interrupt_t *reader,
    vlc_win_file_read_cb read, void *opaque, void *buffer, size_t length)
{
    int entry_errno = errno; DWORD entry_error = GetLastError();
    if (!reader || !read) { errno = EINVAL; SetLastError(ERROR_INVALID_PARAMETER); return -1; }
    HANDLE target;
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                         &target, THREAD_TERMINATE, FALSE, 0)) {
        errno = ENOMEM; return -1;
    }
    AcquireSRWLockExclusive(&reader->lock);
    if (reader->closing || reader->active || reader->sequence == UINT64_MAX) {
        bool closing = reader->closing;
        bool exhausted = reader->sequence == UINT64_MAX;
        ReleaseSRWLockExclusive(&reader->lock); CloseHandle(target);
        errno = closing ? EINTR : exhausted ? EOVERFLOW : EBUSY;
        SetLastError(closing ? ERROR_OPERATION_ABORTED : exhausted ? ERROR_ARITHMETIC_OVERFLOW : ERROR_BUSY); return -1;
    }
    ResetEvent(reader->cancel); ResetEvent(reader->read_done); ResetEvent(reader->ack);
    ++reader->sequence;
    reader->target = target; reader->active = true;
    SetEvent(reader->wake);
    ReleaseSRWLockExclusive(&reader->lock);

    vlc_interrupt_register(interrupt_read, reader);
    ssize_t value; int saved_errno; DWORD saved_error;
    if (vlc_killed() || WaitForSingleObject(reader->cancel, 0) == WAIT_OBJECT_0) {
        SetEvent(reader->cancel);
        value = -1; saved_errno = EINTR; saved_error = ERROR_OPERATION_ABORTED;
    } else {
        errno = entry_errno; SetLastError(entry_error);
        value = read(opaque, buffer, length);
        saved_error = GetLastError(); saved_errno = errno;
    }
    /* No logging, other I/O or caller code until the waiter acknowledges the
     * guarded read is over. Cancellation may complete normally; keep bytes. */
    SetEvent(reader->read_done);
    int interrupted = vlc_interrupt_unregister();
    bool canceled = interrupted || WaitForSingleObject(reader->cancel, 0) == WAIT_OBJECT_0;
    WaitForSingleObject(reader->ack, INFINITE);
    AcquireSRWLockExclusive(&reader->lock);
    reader->target = NULL; reader->active = false;
    SetEvent(reader->wake);
    ReleaseSRWLockExclusive(&reader->lock);
    /* No reader state access after dropping active. Only this call owns the
     * thread duplicate, never the file; the owner still joins read callers. */
    CloseHandle(target);
    errno = value < 0 && canceled ? EINTR : saved_errno;
    SetLastError(saved_error);
    return value;
}

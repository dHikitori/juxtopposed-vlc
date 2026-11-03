// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <vlc_common.h>
#include <vlc_interrupt.h>
#include <vlc_threads.h>
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char vlc_module_name[]="fixture";
static HANDLE attempted, not_found, cancel_entered, cancel_allowed;
static LONG cancel_count, block_cancel, refuse_cancel;
static PVOID volatile watched_ack, watched_wake;
static HANDLE ack_observed, allow_read_drop, extra_wake_observed, repeated_ack;
static LONG acknowledgements;
static DWORD fixture_wait(HANDLE handle,DWORD timeout)
{
    DWORD result=WaitForSingleObject(handle,timeout),error=GetLastError();
    if(result==WAIT_OBJECT_0 && handle==InterlockedCompareExchangePointer(&watched_ack,NULL,NULL)) {
        SetEvent(ack_observed);WaitForSingleObject(allow_read_drop,INFINITE);
    }
    if(result==WAIT_OBJECT_0 && handle==InterlockedCompareExchangePointer(&watched_wake,NULL,NULL)) SetEvent(extra_wake_observed);
    SetLastError(error);return result;
}
static BOOL fixture_event(HANDLE handle)
{
    BOOL result=SetEvent(handle);DWORD error=GetLastError();
    if(handle==InterlockedCompareExchangePointer(&watched_ack,NULL,NULL)
       && InterlockedIncrement(&acknowledgements)>1) SetEvent(repeated_ack);
    SetLastError(error);return result;
}
static BOOL fixture_cancel(HANDLE thread)
{
    InterlockedIncrement(&cancel_count);
    SetEvent(attempted);
    if (InterlockedCompareExchange(&block_cancel, 0, 1) == 1) {
        SetEvent(cancel_entered);
        WaitForSingleObject(cancel_allowed, INFINITE);
    }
    if (InterlockedCompareExchange(&refuse_cancel, 0, 0)) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    BOOL result = CancelSynchronousIo(thread);
    DWORD error = GetLastError();
    if (!result && error == ERROR_NOT_FOUND) SetEvent(not_found);
    SetLastError(error);
    return result;
}
static UINT fixture_drive_type(LPCWSTR root)
{
    if (root[0] == L'X') return DRIVE_REMOTE;
    if (root[0] == L'Y') return DRIVE_UNKNOWN;
    return GetDriveTypeW(root);
}
#define WaitForSingleObject fixture_wait
#define SetEvent fixture_event
#define CancelSynchronousIo fixture_cancel
#define GetDriveTypeW fixture_drive_type
#include "../../../modules/access/win_file_interrupt.c"
#undef GetDriveTypeW
#undef CancelSynchronousIo
#undef SetEvent
#undef WaitForSingleObject

unsigned fixture_file_count(size_t);
unsigned fixture_unc_count(size_t);
ssize_t fixture_file_adapter(vlc_win_file_interrupt_t *,int,void *,size_t,HANDLE);
ssize_t fixture_unc_adapter(vlc_win_file_interrupt_t *,HANDLE,void *,size_t,HANDLE);
static LONG assertions;
static unsigned groups;
#define CHECK(c) do { InterlockedIncrement(&assertions); if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #c); exit(1); } } while (0)
static void wait_event(HANDLE event) { CHECK(WaitForSingleObject(event, 5000) == WAIT_OBJECT_0); }
static HANDLE event(bool signaled) { HANDLE h = CreateEventW(NULL, TRUE, signaled, NULL); CHECK(h != NULL); return h; }
static void reset_audit(void)
{
    ResetEvent(attempted); ResetEvent(not_found); ResetEvent(cancel_entered);
    SetEvent(cancel_allowed);
    InterlockedExchange(&cancel_count, 0); InterlockedExchange(&block_cancel, 0); InterlockedExchange(&refuse_cancel, 0);
}
struct pipe_pair { HANDLE server, client; };
static struct pipe_pair pipe_pair(void)
{
    static LONG serial;
    wchar_t name[160];
    swprintf(name, 160, L"\\\\.\\pipe\\hakone-read-interrupt-%lu-%ld", GetCurrentProcessId(), InterlockedIncrement(&serial));
    struct pipe_pair p;
    p.server = CreateNamedPipeW(name, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                               1, 4096, 4096, 0, NULL);
    CHECK(p.server != INVALID_HANDLE_VALUE);
    p.client = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    CHECK(p.client != INVALID_HANDLE_VALUE);
    CHECK(ConnectNamedPipe(p.server, NULL) || GetLastError() == ERROR_PIPE_CONNECTED);
    return p;
}
static void put(HANDLE server, const char *bytes, DWORD length)
{
    DWORD written = 0; CHECK(WriteFile(server, bytes, length, &written, NULL)); CHECK(written == length);
}
static void pipe_close(struct pipe_pair p) { CHECK(CloseHandle(p.client)); CHECK(CloseHandle(p.server)); }
struct read_call {
    vlc_win_file_interrupt_t *reader;
    vlc_interrupt_t *interrupt;
    HANDLE file, entered, issue_allowed, native_returned, wrapper_done, done, next_entered;
    int fd;
    bool crt, next;
    int adapter;
    LONG callback_count;
    char bytes[32], next_byte;
    ssize_t value, next_value;
    int error, callback_error;
    DWORD native_error, callback_native_error;
};
static ssize_t native_read(void *opaque, void *buffer, size_t length)
{
    struct read_call *call = opaque;
    InterlockedIncrement(&call->callback_count);
    SetEvent(call->entered);
    WaitForSingleObject(call->issue_allowed, INFINITE);
    ssize_t value;
    if (call->crt) value = _read(call->fd, buffer, (unsigned)length);
    else {
        DWORD count;
        if (!ReadFile(call->file, buffer, (DWORD)length, &count, NULL)) {
            DWORD error = GetLastError(); errno = EIO; SetLastError(error); value = -1;
        } else value = count;
    }
    call->callback_native_error = GetLastError(); call->callback_error = errno;
    SetEvent(call->native_returned);
    SetLastError(call->callback_native_error); errno = call->callback_error;
    return value;
}
static void *read_thread(void *opaque)
{
    struct read_call *call = opaque;
    vlc_interrupt_t *previous = vlc_interrupt_set(call->interrupt);
    errno = EDOM; SetLastError(0x1234);
    if(call->adapter==1) call->value=fixture_file_adapter(call->reader,call->fd,call->bytes,sizeof(call->bytes),call->entered);
    else if(call->adapter==2) call->value=fixture_unc_adapter(call->reader,call->file,call->bytes,sizeof(call->bytes),call->entered);
    else call->value = vlc_win_file_interrupt_read(call->reader, native_read, call, call->bytes, sizeof(call->bytes));
    call->native_error = GetLastError(); call->error = errno;
    SetEvent(call->wrapper_done);
    if (call->next) {
        DWORD count;
        SetEvent(call->next_entered);
        call->next_value = ReadFile(call->file, &call->next_byte, 1, &count, NULL) ? (ssize_t)count : -1;
    }
    CHECK(vlc_interrupt_set(previous) == call->interrupt);
    SetEvent(call->done);
    return NULL;
}
static struct read_call call_new(HANDLE file)
{
    struct read_call call = {0};
    call.reader = vlc_win_file_interrupt_new(); CHECK(call.reader != NULL);
    call.interrupt = vlc_interrupt_create(); CHECK(call.interrupt != NULL);
    call.file = file; call.fd = -1;
    call.entered=event(false); call.issue_allowed=event(true); call.native_returned=event(false);
    call.wrapper_done=event(false); call.done=event(false); call.next_entered=event(false);
    return call;
}
static void call_delete(struct read_call *call)
{
    vlc_win_file_interrupt_delete(call->reader); vlc_interrupt_destroy(call->interrupt);
    CloseHandle(call->entered); CloseHandle(call->issue_allowed); CloseHandle(call->native_returned);
    CloseHandle(call->wrapper_done); CloseHandle(call->done); CloseHandle(call->next_entered);
}
static vlc_thread_t launch_read(struct read_call *call)
{
    vlc_thread_t thread; CHECK(vlc_clone(&thread, read_thread, call) == 0); return thread;
}
static void test_preissue(void)
{
    reset_audit(); struct pipe_pair p = pipe_pair(); struct read_call call = call_new(p.client);
    ResetEvent(call.issue_allowed); vlc_thread_t thread = launch_read(&call); wait_event(call.entered);
    vlc_interrupt_raise(call.interrupt); wait_event(not_found);
    CHECK(WaitForSingleObject(call.done, 30) == WAIT_TIMEOUT);
    SetEvent(call.issue_allowed);
    bool completed = WaitForSingleObject(call.done, 500) == WAIT_OBJECT_0;
    if (!completed) put(p.server, "r", 1); /* Release a failing one-shot control safely. */
    wait_event(call.done); vlc_join(thread, NULL);
    ssize_t value=call.value; int error=call.error; LONG attempts=cancel_count;
    CHECK(GetFileType(p.client) == FILE_TYPE_PIPE);
    call_delete(&call); pipe_close(p); ++groups;
    CHECK(completed); CHECK(value == -1); CHECK(error == EINTR); CHECK(attempts >= 2);
}
static void test_pending(bool crt)
{
    reset_audit(); struct pipe_pair p = pipe_pair(); struct read_call call = call_new(p.client);
    call.crt = crt;
    if (crt) { call.fd = _open_osfhandle((intptr_t)p.client, _O_BINARY | _O_RDONLY); CHECK(call.fd >= 0); }
    vlc_thread_t thread = launch_read(&call); wait_event(call.entered); vlc_interrupt_kill(call.interrupt);
    wait_event(call.done); vlc_join(thread, NULL);
    CHECK(call.value == -1); CHECK(call.error == EINTR); CHECK(call.callback_count == 1);
    call_delete(&call);
    if (crt) { CHECK(_close(call.fd) == 0); CHECK(CloseHandle(p.server)); }
    else pipe_close(p);
    ++groups;
}
static void test_adapter(int adapter)
{
    reset_audit(); struct pipe_pair p=pipe_pair(); struct read_call call=call_new(p.client);
    call.adapter=adapter;
    if(adapter==1) { call.fd=_open_osfhandle((intptr_t)p.client,_O_BINARY|_O_RDONLY);CHECK(call.fd>=0); }
    vlc_thread_t thread=launch_read(&call);wait_event(call.entered);vlc_interrupt_raise(call.interrupt);
    wait_event(call.done);vlc_join(thread,NULL);CHECK(call.value==-1 && call.error==EINTR);
    call_delete(&call);
    if(adapter==1) { CHECK(_close(call.fd)==0);CHECK(CloseHandle(p.server)); }
    else pipe_close(p);
    ++groups;
}
static void test_adapter_count(void)
{
    CHECK(fixture_file_count(0)==0);CHECK(fixture_unc_count(0)==0);
    CHECK(fixture_file_count(1)==1);CHECK(fixture_unc_count(1)==1);
    CHECK(fixture_file_count(INT_MAX)==INT_MAX);CHECK(fixture_unc_count(INT_MAX)==INT_MAX);
    CHECK(fixture_file_count(SIZE_MAX)==INT_MAX);CHECK(fixture_unc_count(SIZE_MAX)==INT_MAX);
    ++groups;
}
static void test_ack_before_next_io(void)
{
    reset_audit(); struct pipe_pair p = pipe_pair(); struct read_call call = call_new(p.client);
    call.next = true; ResetEvent(cancel_allowed); InterlockedExchange(&block_cancel, 1);
    vlc_thread_t thread = launch_read(&call); wait_event(call.entered);
    vlc_interrupt_raise(call.interrupt); wait_event(cancel_entered);
    put(p.server, "a", 1); wait_event(call.native_returned);
    bool returned_before_ack=WaitForSingleObject(call.wrapper_done, 40)!=WAIT_TIMEOUT;
    bool next_before_ack=WaitForSingleObject(call.next_entered, 0)!=WAIT_TIMEOUT;
    SetEvent(cancel_allowed); wait_event(call.next_entered);
    put(p.server, "b", 1); wait_event(call.done); vlc_join(thread, NULL);
    ssize_t value=call.value,next_value=call.next_value; char byte=call.bytes[0],next_byte=call.next_byte;
    bool errors_preserved=call.error==call.callback_error && call.native_error==call.callback_native_error;
    call_delete(&call); pipe_close(p); ++groups;
    CHECK(!returned_before_ack); CHECK(!next_before_ack);
    CHECK(value==1 && byte=='a'); /* A completed positive read survives interruption. */
    CHECK(next_value==1 && next_byte=='b'); CHECK(errors_preserved);
}
static void test_extra_wake_is_not_a_new_scope(void)
{
    reset_audit();struct pipe_pair p=pipe_pair();struct read_call call=call_new(p.client);
    ResetEvent(ack_observed);ResetEvent(allow_read_drop);ResetEvent(extra_wake_observed);ResetEvent(repeated_ack);
    InterlockedExchange(&acknowledgements,0);
    InterlockedExchangePointer(&watched_ack,call.reader->ack);InterlockedExchangePointer(&watched_wake,call.reader->wake);
    vlc_thread_t thread=launch_read(&call);wait_event(call.entered);put(p.server,"g",1);wait_event(ack_observed);
    /* Keep the first reader active after ack. Force the same extra wake that
     * can remain when one end wake and a new start wake are both observed. */
    ResetEvent(extra_wake_observed);SetEvent(call.reader->wake);wait_event(extra_wake_observed);
    bool repeated=WaitForSingleObject(repeated_ack,100)==WAIT_OBJECT_0;
    SetEvent(allow_read_drop);wait_event(call.done);vlc_join(thread,NULL);
    ssize_t value=call.value;call_delete(&call);pipe_close(p);
    LONG count=InterlockedCompareExchange(&acknowledgements,0,0);
    InterlockedExchangePointer(&watched_ack,NULL);InterlockedExchangePointer(&watched_wake,NULL);
    CHECK(!repeated);CHECK(count==1);CHECK(value==1);++groups;
}
static void test_sequence_exhaustion(void)
{
    struct read_call call=call_new(INVALID_HANDLE_VALUE);
    AcquireSRWLockExclusive(&call.reader->lock);call.reader->sequence=UINT64_MAX;ReleaseSRWLockExclusive(&call.reader->lock);
    CHECK(vlc_win_file_interrupt_read(call.reader,native_read,&call,call.bytes,1)==-1);
    CHECK(errno==EOVERFLOW && GetLastError()==ERROR_ARITHMETIC_OVERFLOW);CHECK(call.callback_count==0);
    CHECK(vlc_win_file_interrupt_read(call.reader,native_read,&call,call.bytes,1)==-1 && errno==EOVERFLOW);
    call_delete(&call);++groups;
}
struct close_call { vlc_win_file_interrupt_t *reader; HANDLE done; };
static void *close_thread(void *opaque)
{
    struct close_call *call = opaque; vlc_win_file_interrupt_close(call->reader); SetEvent(call->done); return NULL;
}
static void test_repeated_close(void)
{
    reset_audit(); struct pipe_pair p = pipe_pair(); struct read_call call = call_new(p.client);
    ResetEvent(cancel_allowed); InterlockedExchange(&block_cancel, 1);
    vlc_thread_t thread=launch_read(&call); wait_event(call.entered);
    struct close_call a={call.reader,event(false)}, b={call.reader,event(false)};
    vlc_thread_t first, second;
    CHECK(vlc_clone(&first, close_thread, &a)==0); wait_event(cancel_entered);
    CHECK(vlc_clone(&second, close_thread, &b)==0);
    CHECK(WaitForSingleObject(a.done,30)==WAIT_TIMEOUT); CHECK(WaitForSingleObject(b.done,30)==WAIT_TIMEOUT);
    SetEvent(cancel_allowed); wait_event(call.done); wait_event(a.done); wait_event(b.done);
    vlc_join(thread,NULL); vlc_join(first,NULL); vlc_join(second,NULL);
    CHECK(call.value==-1 && call.error==EINTR);
    CHECK(vlc_win_file_interrupt_read(call.reader,native_read,&call,call.bytes,1)==-1 && errno==EINTR);
    call_delete(&call); CloseHandle(a.done); CloseHandle(b.done);
    put(p.server,"h",1); DWORD count; char byte; CHECK(ReadFile(p.client,&byte,1,&count,NULL)); CHECK(count==1 && byte=='h');
    pipe_close(p); ++groups;
}
static void test_refusal_waits_for_io(void)
{
    reset_audit(); InterlockedExchange(&refuse_cancel,1);
    struct pipe_pair p=pipe_pair(); struct read_call call=call_new(p.client);
    vlc_thread_t thread=launch_read(&call); wait_event(call.entered); vlc_interrupt_raise(call.interrupt); wait_event(attempted);
    CHECK(WaitForSingleObject(call.done,60)==WAIT_TIMEOUT);
    put(p.server,"s",1); wait_event(call.done); vlc_join(thread,NULL);
    CHECK(call.value==1 && call.bytes[0]=='s'); call_delete(&call); pipe_close(p); ++groups;
}
static void test_preexisting_interrupt(void)
{
    reset_audit(); struct pipe_pair p=pipe_pair(); struct read_call call=call_new(p.client);
    vlc_interrupt_raise(call.interrupt); vlc_thread_t thread=launch_read(&call);
    wait_event(call.done); vlc_join(thread,NULL);
    CHECK(call.value==-1 && call.error==EINTR); CHECK(call.callback_count==0);
    call_delete(&call); pipe_close(p); ++groups;
}
static ssize_t error_read(void *opaque, void *buffer, size_t count)
{
    (void)opaque;(void)buffer;(void)count; errno=EBADF; SetLastError(ERROR_INVALID_HANDLE); return -1;
}
static void test_error_and_large_scope(void)
{
    reset_audit(); vlc_win_file_interrupt_t *reader=vlc_win_file_interrupt_new(); CHECK(reader);
    CHECK(vlc_win_file_interrupt_read(reader,error_read,NULL,NULL,SIZE_MAX)==-1);
    CHECK(GetLastError()==ERROR_INVALID_HANDLE && errno==EBADF);
    errno=ERANGE; SetLastError(0x1234); vlc_win_file_interrupt_close(reader);
    CHECK(GetLastError()==0x1234 && errno==ERANGE); vlc_win_file_interrupt_delete(reader);
    CHECK(GetLastError()==0x1234 && errno==ERANGE); ++groups;
}
static void test_regular_file(void)
{
    reset_audit(); wchar_t temporary[MAX_PATH],name[MAX_PATH];
    CHECK(GetTempPathW(MAX_PATH,temporary)); CHECK(GetTempFileNameW(temporary,L"hki",0,name));
    HANDLE file=CreateFileW(name,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,NULL);
    CHECK(file!=INVALID_HANDLE_VALUE); put(file,"abcdef",6); LARGE_INTEGER at={.QuadPart=0}; CHECK(SetFilePointerEx(file,at,NULL,FILE_BEGIN));
    struct read_call call=call_new(file); vlc_interrupt_t *previous=vlc_interrupt_set(call.interrupt);
    CHECK(vlc_win_file_interrupt_read(call.reader,native_read,&call,call.bytes,2)==2 && !memcmp(call.bytes,"ab",2));
    CHECK(vlc_win_file_interrupt_read(call.reader,native_read,&call,call.bytes,32)==4 && !memcmp(call.bytes,"cdef",4));
    CHECK(vlc_win_file_interrupt_read(call.reader,native_read,&call,call.bytes,32)==0);
    CHECK(vlc_win_file_interrupt_read(call.reader,native_read,&call,call.bytes,0)==0);
    at.QuadPart=1; CHECK(SetFilePointerEx(file,at,NULL,FILE_BEGIN));
    CHECK(vlc_win_file_interrupt_read(call.reader,native_read,&call,call.bytes,1)==1 && call.bytes[0]=='b');
    CHECK(vlc_interrupt_set(previous)==call.interrupt); call_delete(&call);
    CHECK(GetFileType(file)==FILE_TYPE_DISK); CHECK(CloseHandle(file)); ++groups;
}
static void test_path_scope(void)
{
    CHECK(vlc_win_file_remote_path(L"\\\\server\\share\\file")==1);
    CHECK(vlc_win_file_remote_path(L"//server/share/file")==1);
    CHECK(vlc_win_file_remote_path(L"\\\\?\\UNC\\server\\share\\file")==1);
    CHECK(vlc_win_file_remote_path(L"X:\\file")==1);
    CHECK(vlc_win_file_remote_path(L"\\\\?\\X:\\file")==1);
    CHECK(vlc_win_file_remote_path(L"Y:\\file")==0);
    CHECK(vlc_win_file_remote_path(L"C:\\file")==0);
    CHECK(vlc_win_file_remote_path(L"\\\\.\\pipe\\file")==0);
    CHECK(vlc_win_file_remote_path(L"\\\\?\\Volume{123}\\file")==0);
    CHECK(vlc_win_file_remote_path(L"\\\\")==0);
    CHECK(vlc_win_file_remote_path(L"\\\\.")==0);
    CHECK(vlc_win_file_remote_path(L"\\\\?")==0);
    CHECK(vlc_win_file_remote_path(L"\\\\?\\")==0);
    CHECK(vlc_win_file_remote_path(L"\\\\?\\U")==0);
    CHECK(vlc_win_file_remote_path(L"\\\\?\\UNC\\")==0);
    ++groups;
}
int main(int argc,char **argv)
{
    attempted=event(false);not_found=event(false);cancel_entered=event(false);cancel_allowed=event(true);
    ack_observed=event(false);allow_read_drop=event(true);extra_wake_observed=event(false);repeated_ack=event(false);
    if(argc==2 && !strcmp(argv[1],"preissue")) test_preissue();
    else if(argc==2 && !strcmp(argv[1],"ack")) test_ack_before_next_io();
    else if(argc==2 && !strcmp(argv[1],"extra-wake")) test_extra_wake_is_not_a_new_scope();
    else {
        test_path_scope(); test_regular_file(); test_error_and_large_scope(); test_preexisting_interrupt();
        test_preissue(); test_pending(false); test_pending(true); test_adapter(1); test_adapter(2); test_adapter_count(); test_ack_before_next_io();
        test_repeated_close(); test_refusal_waits_for_io();test_extra_wake_is_not_a_new_scope();test_sequence_exhaustion();
    }
    CloseHandle(attempted); CloseHandle(not_found); CloseHandle(cancel_entered); CloseHandle(cancel_allowed);
    CloseHandle(ack_observed);CloseHandle(allow_read_drop);CloseHandle(extra_wake_observed);CloseHandle(repeated_ack);
    printf("{\"success\":true,\"groups\":%u,\"assertions\":%ld,\"actualWindowsSynchronousIo\":true}\n",groups,assertions);
    return 0;
}

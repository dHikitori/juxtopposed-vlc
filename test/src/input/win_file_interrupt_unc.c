// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <vlc_common.h>
#include <windows.h>
static HANDLE entered;
static DWORD observed;
static BOOL adapter_read(HANDLE file,LPVOID buffer,DWORD count,LPDWORD result,LPOVERLAPPED overlap)
{
    if(file==(HANDLE)(intptr_t)-100) { observed=count;*result=0;return TRUE; }
    if(entered) SetEvent(entered);
    return ReadFile(file,buffer,count,result,overlap);
}
#define ReadFile adapter_read
#include "../../../modules/access/unc.c"
#undef ReadFile
unsigned fixture_unc_count(size_t count)
{
    access_sys_t sys={.h=(HANDLE)(intptr_t)-100};
    ReadNative(&sys,NULL,count);
    return observed;
}
ssize_t fixture_unc_adapter(vlc_win_file_interrupt_t *interrupt,HANDLE file,void *buffer,size_t count,HANDLE entry)
{
    access_sys_t sys={.h=file,.interrupt=interrupt};
    stream_t access={0};access.p_sys=&sys;
    entered=entry;
    ssize_t result=Read(&access,buffer,count);
    entered=NULL;
    return result;
}

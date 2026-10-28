// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <vlc_common.h>
#include <windows.h>
#include <io.h>
static HANDLE entered;
static unsigned observed;
static int adapter_read(int fd,void *buffer,unsigned count)
{
    if(fd==-100) { observed=count; return 0; }
    if(entered) SetEvent(entered);
    return _read(fd,buffer,count);
}
#define _read adapter_read
#include "../../../modules/access/file.c"
#undef _read
unsigned fixture_file_count(size_t count)
{
    access_sys_t sys={.fd=-100};
    ReadRemote(&sys,NULL,count);
    return observed;
}
ssize_t fixture_file_adapter(vlc_win_file_interrupt_t *interrupt,int fd,void *buffer,size_t count,HANDLE entry)
{
    access_sys_t sys={.fd=fd,.interrupt=interrupt};
    stream_t access={0};access.p_sys=&sys;
    entered=entry;
    ssize_t result=Read(&access,buffer,count);
    entered=NULL;
    return result;
}

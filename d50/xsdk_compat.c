/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xsdk_compat.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef DR_XBOX360
#include <xsdk/log.h>
#endif
int D50_ReadFile(const char *path,D50Buffer *out)
{
    if(!out)return -2;
    out->data=NULL;out->size=0;
    if(!path)return -1;
    FILE *f=fopen(path,"rb");
    if(!f)return -1;
    if(fseek(f,0,SEEK_END)||ftell(f)<0){fclose(f);return -2;}
    long size=ftell(f);
    if(size<0 || fseek(f,0,SEEK_SET)){fclose(f);return -2;}
    void *data=size?malloc((size_t)size):NULL;
    if(size && !data){fclose(f);return -2;}
    if(size && fread(data,1,(size_t)size,f)!=(size_t)size){free(data);fclose(f);return -2;}
    fclose(f);
    out->data=data;out->size=(size_t)size;return 0;
}
void D50_ReleaseFile(D50Buffer *buffer)
{
    if(!buffer)return;
    free(buffer->data);buffer->data=NULL;buffer->size=0;
}
void *D50_Alloc(size_t bytes)
{
    if(!bytes)return NULL;
    return calloc(1,bytes);
}
void D50_Release(void *memory){free(memory);}
uint64_t D50_NowUs(void)
{
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;
    return (uint64_t)t.tv_sec*1000000u+(uint64_t)t.tv_nsec/1000u;
}
void D50_Log(const char *format,...)
{
    char line[512];
    va_list args;
    va_start(args,format);
    vsnprintf(line,sizeof line,format,args);
    va_end(args);
#ifdef DR_XBOX360
    xsdk_log_write("%s",line);
#else
    fputs(line,stdout);fputc('\n',stdout);
#endif
}

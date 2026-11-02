/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "dr_gs_batch.h"
#include <string.h>
int dr_gs_batch_merge(DrXenosTargetCommand *left,const DrXenosTargetCommand *right)
{
    if(!left || !right || left==right || left->snapshot || right->snapshot || left->target!=right->target)return 0;
    DrXenosCommand *a=&left->command;const DrXenosCommand *b=&right->command;
    if(a->texture!=b->texture || !a->count || !b->count || a->draw.topology>2 ||
       a->draw.vu_program>3 || a->draw.date || b->draw.date || a->draw.effect || b->draw.effect ||
       a->draw.stencil || b->draw.stencil || a->draw.textured==2 || b->draw.textured==2)return 0;
    unsigned unit=a->draw.topology==0?3:a->draw.topology==1?2:1;
    unsigned limit=DR_XENOS_VERTEX_CAPACITY/unit*unit;
    if(a->count%unit || b->count%unit || a->count>limit || b->count>limit-a->count)return 0;
    /* Compare the entire state, including future fields, but compare uniform
     * values instead of their arena addresses. Padding can only prevent a
     * merge; it cannot make different states compare equal. */
    DrXenosDraw da,db;memcpy(&da,&a->draw,sizeof da);memcpy(&db,&b->draw,sizeof db);
    da.vu_uniform=NULL;db.vu_uniform=NULL;
    if(memcmp(&da,&db,sizeof da))return 0;
    uintptr_t start,next;unsigned stride;
    if(a->draw.vu_program){
        if(a->vertices || b->vertices || !a->vu_vertices || !b->vu_vertices ||
           !a->draw.vu_uniform || !b->draw.vu_uniform ||
           ((uintptr_t)a->draw.vu_uniform&3) || ((uintptr_t)b->draw.vu_uniform&3))return 0;
        if(a->draw.vu_uniform!=b->draw.vu_uniform && memcmp(a->draw.vu_uniform,b->draw.vu_uniform,76*sizeof(float)))return 0;
        start=(uintptr_t)a->vu_vertices;next=(uintptr_t)b->vu_vertices;stride=12*sizeof(float);
    }else{
        if(!a->vertices || !b->vertices || a->vu_vertices || b->vu_vertices)return 0;
        start=(uintptr_t)a->vertices;next=(uintptr_t)b->vertices;stride=sizeof(DrGsVertex);
    }
    if(start>UINTPTR_MAX-(uintptr_t)a->count*stride || next!=start+(uintptr_t)a->count*stride)return 0;
    a->count+=b->count;return 1;
}

/* SPDX-License-Identifier: AGPL-3.0-or-later */
#define main iso_adapter_fixture_main
#include "test_xb_iso_file.c"
#undef main
#include "dr_container.h"
#include "dr_body_model.h"
#include "dr_texfile.h"
#include "dr_stage_collision.h"
static uint32_t u32(const uint8_t *p){return dr_resource_le32(p);}
static uint64_t fingerprint(const uint8_t *p,size_t n){uint64_t h=0xcbf29ce484222325ull;for(size_t i=0;i<n;i++)h=(h^p[i])*0x100000001b3ull;return h;}
static uint8_t *load(int id,size_t *n){char path[64];snprintf(path,sizeof(path),"pzs3us1/%05d.bin",id-1);dr_span s;assert(!XbDisc_FindFile(path,&s) && s.size && s.size<=8*1024*1024);*n=(size_t)s.size;uint8_t *p=malloc(*n);assert(p && !XbDisc_ReadSpan(&s,0,p,*n));return p;}
static unsigned pack(const uint8_t *p,size_t n){unsigned count=u32(p);assert(count && count<4096);for(unsigned i=0;i<count;i++){size_t off,bytes;unsigned flags;int e=dr_pack_memory_entry(p,n,i,&off,&bytes,&flags);assert(!e || e==DR_NOT_FOUND);}return count;}
int main(int argc,char **argv){assert(argc==2);image_path=argv[1];assert(!XbDisc_Mount("game:/test.iso"));
 uint8_t empty_bpe[8]={0},untouched[256];memset(untouched,0xa5,sizeof(untouched));size_t written=77;assert(dr_motion_from_memory(empty_bpe,8,1,1,untouched,sizeof(untouched),&written)==DR_LOAD_EMPTY && !written);for(unsigned i=0;i<sizeof(untouched);i++)assert(untouched[i]==0xa5);
 const struct {const char *name;int id,kind;} rows[]={
  {"common-2",2,0},{"common-3",3,0},{"common-4",4,0},{"common-boot",5,0},{"progress-default",6,0},
  {"member-left",8,1},{"member-right",11,1},{"battle-sound",0x14a,2},{"stage-sound",0x14e,2},
  {"stage-model",0x171,3},{"left-model",0x590,4},{"right-model",0x59a,4},
  {"left-animation",0x598,5},{"right-animation",0x5a2,5},{"left-extra",0x599,3},{"right-extra",0x5a3,3},
  {"left-voice",0xbda,2},{"right-voice",0xbdb,2}};
 for(unsigned r=0;r<sizeof(rows)/sizeof(*rows);r++){
  size_t n;uint8_t *p=load(rows[r].id,&n);unsigned entries=0,parts=0,anims=0,rejected=0,empty=0;int decoded=0,first_error=-1;
  if(rows[r].kind>=2){entries=pack(p,n);}
  if(rows[r].kind==1){uint8_t *rgba=malloc(4*1024*1024);DrTexFileImage image;assert(rgba);decoded=dr_texfile_decode(p,n,0,rgba,4*1024*1024,&image);free(rgba);assert(!decoded);entries=u32(p);}
  if(rows[r].kind==2){assert(entries==3 && u32(p+4)>=20 && u32(p+8)>u32(p+4) && u32(p+12)>u32(p+8) && u32(p+16)<=n);}
  if(rows[r].id==0x171){DrStageCollision collision;assert(!dr_stage_collision_open(p,n,&collision));assert(collision.zones==100 && collision.meshes==151 && collision.nodes==27015 && collision.polygons==13583);}
  if(rows[r].kind==4){size_t off,bytes,bad;DrBodyPart typed[256];DrBodyModel model;assert(!dr_pack_memory_entry(p,n,2,&off,&bytes,NULL));assert(!dr_body_model_read(p+off,bytes,typed,256,&model,&bad));parts=model.parts;assert(parts && model.chains);}
  if(rows[r].kind==5){uint8_t *converted=malloc(0xc000);assert(converted);for(unsigned i=0;i<0x19e && i<entries;i++){size_t off,bytes,written;int e=dr_pack_memory_entry(p,n,i,&off,&bytes,NULL);if(e==DR_NOT_FOUND)continue;assert(!e);e=dr_motion_from_memory(p+off,bytes,1,1,converted,0xc000,&written);if(e==DR_LOAD_EMPTY){empty++;continue;}if(e){fprintf(stderr,"animation id=%d entry=%u rc=%d\n",rows[r].id,i,e);if(first_error<0)first_error=(int)i;rejected++;continue;}anims++;}free(converted);}
  printf("{\"name\":\"%s\",\"file_id\":%d,\"afs_index\":%d,\"bytes\":%zu,\"fnv64\":\"%016llx\",\"entries\":%u,\"typed_body_parts\":%u,\"animations_converted\":%u,\"empty_animation_slots\":%u,\"animation_rejected\":%u,\"first_animation_error_index\":%d,\"resource_conversion_complete\":false}\n",rows[r].name,rows[r].id,rows[r].id-1,n,(unsigned long long)fingerprint(p,n),entries,parts,anims,empty,rejected,first_error);
  free(p);
 }
 XbDisc_Unmount();return 0;
}

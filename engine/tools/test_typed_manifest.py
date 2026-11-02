#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Exercise the actual typed loader with real programs in registered host buffers.
Only expected hashes, layouts and pointer addresses are compiled into fixtures.
"""
from pathlib import Path
import argparse,json,struct,subprocess,os,hashlib,sys
p=argparse.ArgumentParser();p.add_argument('manifest',type=Path);p.add_argument('disc',type=Path);p.add_argument('output',type=Path);p.add_argument('--sanitize',action='store_true');a=p.parse_args();e=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
r=json.loads(a.manifest.read_text());elf=(a.disc/'SLUS_216.78').read_bytes();flat=bytearray(0x1ff180);shoff=struct.unpack_from('<I',elf,32)[0];step,n=struct.unpack_from('<HH',elf,46)
for i in range(n):
 _,kind,flags,addr,off,size=struct.unpack_from('<6I',elf,shoff+i*step)
 if flags&2 and kind!=8 and 0x100000<=addr and addr+size<=0x2ff180:flat[addr-0x100000:addr-0x100000+size]=elf[off:off+size]
sources=[flat,(a.disc/'BIN/DBZP.BIN').read_bytes()];expected=[bytearray(b'\xa5'*rg['size']) for rg in r['regions']]
def fnv(data):
 v=0xcbf29ce484222325
 for byte in data:v=((v^byte)*0x100000001b3)&0xffffffffffffffff
 return v
for op in r['operations']:
 rg=r['regions'][op['region']];start=rg['source_offset']+op['offset'];src=sources[rg['source_file']][start:start+op['length']]
 if op['kind']=='pointer':data=op['native_value_in_reference_link'].to_bytes(4,sys.byteorder)
 else:data=src if sys.byteorder=='little' else b''.join(src[i:i+op['width']][::-1] for i in range(0,len(src),op['width']))
 expected[op['region']][op['offset']:op['offset']+op['length']]=data
s='''#include "xb_typed_data.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *root;static int recurse;
static XbTypedCatalog catalog;
static int reader(void *ctx,const char *name,uint8_t **p,size_t *n){(void)ctx;
 if(recurse){recurse=0;XbDataFiles f={reader,NULL};assert(XbTypedData_Load(&f,&catalog,2)==XBTD_STATE);}
 char path[4096];snprintf(path,sizeof(path),"%s/%s%s",root,!strcmp(name,"DBZP.BIN")?"BIN/":"",name);FILE *f=fopen(path,"rb");if(!f)return -1;
 assert(!fseek(f,0,SEEK_END));long size=ftell(f);assert(size>0 && !fseek(f,0,SEEK_SET));*n=(size_t)size;*p=malloc(*n);assert(*p && fread(*p,1,*n,f)==*n);fclose(f);return 0;
}
static uint64_t hash(const void *p,size_t n){const unsigned char *q=p;uint64_t h=0xcbf29ce484222325ull;for(size_t i=0;i<n;i++)h=(h^q[i])*0x100000001b3ull;return h;}
'''
for i,rg in enumerate(r['regions']):s+=f'static unsigned char memory_{i}[{rg["size"]}];\n'
s+='static XbTypedRegion regions[]={\n'
for i,rg in enumerate(r['regions']):s+='{'+f'memory_{i},{rg["size"]},{rg["source_file"]},{rg["source_offset"]}'+'},\n'
s+='};\nstatic XbTypedOp ops[]={\n'
for o in r['operations']:
 kind=1 if o['kind']=='pointer' else 2 if o['kind'] in ('padding','zero_invariant') else 0
 s+='{'+','.join(str(x) for x in [o['region'],o['offset'],o['length'],o['width'],kind,o.get('expected',0),o['native_value_in_reference_link']])+'},\n'
s+='};\nstatic const uint64_t expected[]={'+','.join(hex(fnv(x))+'ull' for x in expected)+'};\n'
s+='''static void fill(void){for(size_t i=0;i<sizeof(regions)/sizeof(*regions);i++)memset(regions[i].bytes,0xa5,regions[i].size);}
static void unchanged(void){for(size_t i=0;i<sizeof(regions)/sizeof(*regions);i++){unsigned char *p=regions[i].bytes;for(size_t j=0;j<regions[i].size;j++)assert(p[j]==0xa5);}}
int main(int argc,char **argv){assert(argc==2);root=argv[1];fill();
'''
s+=f'catalog=(XbTypedCatalog){{1,0,regions,{len(r["regions"])},ops,{len(r["operations"])},0x{r["operation_source_fnv64"]}ull}};\n'
s+='''XbDataFiles files={reader,NULL};
 assert(XbTypedData_Load(&files,&catalog,0)==XBTD_INCOMPLETE);unchanged();
 assert(!XbTypedData_Load(&files,&catalog,XBTD_ALLOW_PARTIAL|XBTD_VALIDATE_ONLY));unchanged();
 recurse=1;int rc=XbTypedData_Load(&files,&catalog,XBTD_ALLOW_PARTIAL);if(rc)fprintf(stderr,"typed rc=%d\\n",rc);assert(!rc);
 for(size_t i=0;i<catalog.region_count;i++)assert(hash(regions[i].bytes,regions[i].size)==expected[i]);
 assert(gPortDataStripped==2 && XbTypedData_RequireBattleData()==XBTD_INCOMPLETE);fill();catalog.source_hash^=1;assert(XbTypedData_Load(&files,&catalog,2)==XBTD_HASH);unchanged();catalog.source_hash^=1;
 size_t ptr=0;while(ops[ptr].kind!=XBTD_POINTER)ptr++;uint32_t save=ops[ptr].expected_mips;ops[ptr].expected_mips^=4;assert(XbTypedData_Load(&files,&catalog,2)==XBTD_POINTER_MISMATCH);unchanged();ops[ptr].expected_mips=save;
 XbTypedOp op0=ops[1];ops[1]=ops[0];assert(XbTypedData_Load(&files,&catalog,2)==XBTD_BAD_CATALOG);unchanged();ops[1]=op0;
 XbTypedRegion reg=regions[0];regions[0].source_offset=0x1ff17f;assert(XbTypedData_Load(&files,&catalog,2)==XBTD_SOURCE);unchanged();regions[0]=reg;
 catalog.complete=ACTUAL_COMPLETE;
 if(catalog.complete){assert(!XbTypedData_Load(&files,&catalog,0));assert(gPortDataStripped==2 && XbTypedData_RequireBattleData()==XBTD_INCOMPLETE);}
 puts("PASS: actual typed loader; all real scalar/mixed/pointer ops and whole-buffer hashes; opaque bytes untouched; incomplete boot refused; dry run, bad hash, pointer mismatch, overlap, source bounds and reentry refuse atomically; host buffers only");return 0;}
'''
s=s.replace('ACTUAL_COMPLETE',str(int(r['complete'])))
(out/'fixture.c').write_text(s);cmd=['gcc','-std=gnu17','-O1','-g','-Wall','-Wextra','-Werror','-DXB_HOST_TEST','-I'+str(e/'platform/platform')]
if a.sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
exe=out/'typed-test';subprocess.run(cmd+[str(out/'fixture.c'),str(e/'platform/platform/xb_gamedata.c'),'-o',str(exe)],check=True)
run=subprocess.run([str(exe),str(a.disc.resolve())],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));(out/'typed-test.txt').write_text(run.stdout+run.stderr);print(run.stdout+run.stderr,end='');run.check_returncode()
(out/'typed-test.json').write_text(json.dumps({'result':'PASS','regions':len(r['regions']),'operations':len(r['operations']),'typed_bytes':sum(x['typed_bytes'] for x in r['regions']),'pointer_ops':sum(o['kind']=='pointer' for o in r['operations']),'complete':r['complete'],'real_source_programs':True,'actual_loader':True,'sanitizers':a.sanitize,'destination':'registered host buffers, not native game globals','PPC_execution':False,'battle_execution':False},indent=2)+'\n')

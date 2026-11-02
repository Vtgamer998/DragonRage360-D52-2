/* SPDX-License-Identifier: AGPL-3.0-only */
#include "save_unlock.h"
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv)
{
    uint8_t data[DR_SAVE_BYTES];int fresh=argc==3 && !strcmp(argv[1],"--new");
    if(argc!=3){fprintf(stderr,"Usage: unlock-save INPUT_RAW_SAVE NEW_OUTPUT\n       unlock-save --new NEW_OUTPUT\n");return 2;}
    if(fresh)dr_save_new(data,1);
    else {
        FILE *f=fopen(argv[1],"rb");if(!f){perror(argv[1]);return 1;}
        size_t n=fread(data,1,sizeof(data),f);int extra=fgetc(f);fclose(f);
        if(n!=sizeof(data)||extra!=EOF||!dr_save_unlock(data,n,1)) {
            fprintf(stderr,"Invalid raw save size/checksum; no output written.\n");return 1;
        }
    }
    FILE *f=fopen(argv[2],"wbx");if(!f){perror(argv[2]);return 1;}
    int ok=fwrite(data,1,sizeof(data),f)==sizeof(data);if(fclose(f))ok=0;
    if(!ok){remove(argv[2]);return 1;}
    puts("Created separate unlocked raw save; original unchanged. Not verified in-game.");return 0;
}

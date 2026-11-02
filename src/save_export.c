/* SPDX-License-Identifier: AGPL-3.0-only */
#include "save_export.h"
#include "save_unlock.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
int dr_save_export(const char *root,int import_existing,char *message,size_t cap)
{
    uint8_t data[DR_SAVE_BYTES];char path[640],directory[600];
    if(!root || strlen(root)>400)return 0;
    if(import_existing) {
        snprintf(path,sizeof(path),"%s/save_import/BASLUS-21678DBZT3",root);
        FILE *f=fopen(path,"rb");
        if(!f){snprintf(message,cap,"Save de entrada não encontrado.\nCopie o arquivo RAW USA para save_import/\ncom o nome BASLUS-21678DBZT3.");return 0;}
        size_t n=fread(data,1,sizeof(data),f);int extra=fgetc(f);int err=ferror(f);fclose(f);
        if(n!=sizeof(data)||extra!=EOF||err||!dr_save_unlock(data,n,1)) {
            snprintf(message,cap,"Save recusado: tamanho ou checksum inválido.\nUse o arquivo RAW de 16.384 bytes.\nImagens de memory card e PSU/PSV não servem.\nO arquivo original não foi modificado.");return 0;
        }
    } else dr_save_new(data,1);
    snprintf(directory,sizeof(directory),"%s/exports",root);
    if(mkdir(directory,0755)!=0 && errno!=EEXIST) {
        snprintf(message,cap,"Falha ao criar a pasta exports/.\nConfira se há espaço e permissão de gravação.");return 0;
    }
    int slot,reserved=0;
    for(slot=1;slot<=999;slot++) {
        snprintf(directory,sizeof(directory),"%s/exports/save-%03d",root,slot);
        if(mkdir(directory,0755)==0){reserved=1;break;}
        if(errno!=EEXIST)break;
    }
    if(!reserved) {
        snprintf(message,cap,"Não foi possível reservar uma nova pasta.\nNenhum save existente foi substituído.");return 0;
    }
    snprintf(path,sizeof(path),"%s/BASLUS-21678DBZT3",directory);
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0644);
    if(fd<0){snprintf(message,cap,"Não foi possível criar um novo arquivo.\nNenhum save existente foi substituído.");return 0;}
    size_t offset=0;
    while(offset<sizeof(data)) {
        ssize_t n=write(fd,data+offset,sizeof(data)-offset);
        if(n<0 && errno==EINTR)continue;
        if(n<=0)break;
        offset+=(size_t)n;
    }
    int ok=offset==sizeof(data);if(close(fd))ok=0;
    if(!ok) {
        unlink(path); /* Keep the now-empty export directory; never remove user directories. */
        snprintf(message,cap,"Falha ao gravar o save.\nO original foi preservado.");return 0;
    }
    snprintf(message,cap,"Save desbloqueado criado separadamente.\nPasta: exports/save-%03d/\nPersonagens, cenários, músicas e itens.\nZeni: 9.999.999.\n\nAinda não foi validado dentro do jogo.\nConcluir a história não é garantido.",slot);
    return 1;
}

/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Explicit format map; never classify a file by heuristics. Parse/validate
 * the entire optional file before installing any ranges.
 */
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include "xb_demo_resources.h"
static int number(char **at,long *value) {
    char *end;
    while(isspace((unsigned char)**at))(*at)++;
    errno=0;*value=strtol(*at,&end,10);
    if(errno==ERANGE || end==*at || (*end && !isspace((unsigned char)*end)))return -1;
    *at=end;return 0;
}
int XbRes_LoadMap(const char *path) {
    struct Entry {int first,last,packed,skeleton;} entries[64];
    char line[160],kind[16];unsigned count=0;
    FILE *f=fopen(path,"r");
    if(!f)return 0; /* optional: log unconverted ids instead */
    while(fgets(line,sizeof(line),f)) {
        int first,last,packed,used=0;unsigned i;char *s=line;
        while(*s==' ' || *s=='\t')s++;
        if(*s=='#' || *s=='\n' || !*s)continue;
        long a,b,c;
        if(sscanf(s,"%15s%n",kind,&used)!=1){fclose(f);return -1;}
        s+=used;
        if(number(&s,&a) || number(&s,&b) || number(&s,&c)) {fclose(f);return -1;}
        while(isspace((unsigned char)*s))s++;
        if(*s ||
           a<INT_MIN || a>INT_MAX || b<INT_MIN || b>INT_MAX || a>b ||
           (c!=0 && c!=1) || count==64 ||
           (strcmp(kind,"animation") && strcmp(kind,"skeleton"))) { fclose(f);return -1; }
        first=(int)a;last=(int)b;packed=(int)c;
        for(i=0;i<count;i++)if(first<=entries[i].last && last>=entries[i].first){fclose(f);return -1;}
        entries[count++]=(struct Entry){first,last,packed,!strcmp(kind,"skeleton")};
    }
    if(ferror(f)){fclose(f);return -1;}fclose(f);
    for(unsigned i=0;i<count;i++) {
        struct Entry *e=&entries[i];
        int rc=e->skeleton?XbRes_RegisterSkeleton(e->first,e->last,e->packed):XbRes_RegisterAnimation(e->first,e->last,e->packed);
        if(rc)return -1;
    }
    return (int)count;
}

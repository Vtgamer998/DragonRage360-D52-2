/* SPDX-License-Identifier: AGPL-3.0-only */
#include "save_export.h"
#include <stdio.h>
int main(int argc,char **argv) {char message[700];if(argc!=3)return 2;int ok=dr_save_export(argv[1],argv[2][0]=='1',message,sizeof(message));puts(message);return ok?0:1;}

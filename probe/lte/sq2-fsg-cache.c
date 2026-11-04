/* SPDX-License-Identifier: BSD-3-Clause */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "fsg-record.h"
int main(int argc,char **argv) {
 if(argc!=4){fprintf(stderr,"usage: %s CACHE UUID OUTPUT\n",argv[0]);return 2;}
 FILE *f=fopen(argv[1],"rb");if(!f){perror(argv[1]);return 1;}
 uint8_t *p=malloc(SPX_FSG_HEADER+SPX_FSG_CAPACITY+1);
 if(!p){perror("FSG allocation");fclose(f);return 1;}
 size_t n=fread(p,1,SPX_FSG_HEADER+SPX_FSG_CAPACITY+1,f);int bad=ferror(f);fclose(f);
 if(bad || !fsg_record_valid(p,n,argv[2])){fprintf(stderr,"FSG cache incomplete, corrupt or belongs to another device: %s\n",argv[1]);free(p);return 1;}
 f=fopen(argv[3],"wbx");if(!f){perror(argv[3]);free(p);return 1;}
 size_t bytes=fsg_u32(p+8);bad=fwrite(p+SPX_FSG_HEADER,1,bytes,f)!=bytes;
 if(fclose(f))bad=1;
 memset(p,0,n);free(p);if(bad){perror("FSG write");remove(argv[3]);return 1;}return 0;
}

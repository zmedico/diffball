/*
  Copyright (C) 2003 Brian Harring

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, US 
*/
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
//#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
//#include "delta.h"
//#include "pdbuff.h"
#include "cfile.h"
#include "gdiff.h"
#include "dcbuffer.h"
#include "apply-patch.h"


//offset = fh_pos + readSignedBytes(cpy_buff, ctmp);
//len = readUnsignedBytes(cpy_buff+ctmp, clen);


int main(int argc, char **argv)
{
    struct stat src_stat, delta_stat;
    int src_fh, delta_fh, out_fh;
    struct cfile src_cfh, delta_cfh, out_cfh;
    struct CommandBuffer dcbuff;
    if(argc <4){
		printf("pardon, but...\nI need at least 3 args- (reference file), patch-file, target file\n");
		exit(EXIT_FAILURE);
    }
    if(stat(argv[1], &src_stat)) {
		perror("what the hell, stat failed.  wtf?\n");
		exit(EXIT_FAILURE);
    }
    printf("src_fh size=%lu\n", (unsigned long)src_stat.st_size);
    if ((src_fh = open(argv[1], O_RDONLY,0)) == -1) {
		printf("Couldn't open %s, does it exist?\n", argv[1]);
		exit(EXIT_FAILURE);
    }
    if((delta_fh = open(argv[2], O_RDONLY,0))==-1) {
		printf("Couldn't open patch file.\n");
		exit(EXIT_FAILURE);
    }
    if(stat(argv[2], &delta_stat)) {
		perror("what the hell, stat failed.  wtf?\n");
		exit(EXIT_FAILURE);
    }
    printf("delta_fh size=%lu\n", (unsigned long)delta_stat.st_size);
    if((out_fh = open(argv[3], O_WRONLY | O_TRUNC | O_CREAT, 0644))==-1) {
		printf("Couldn't create\truncate output file.\n");
		exit(EXIT_FAILURE);
    }
    /*void initcfile(struct cfile *cfile, int fh, unsigned long fh_start,
    unsigned int compressor_type);*/
    /*signed int gdiffReconstructFiles(int src_fh, int out_fh,
    struct PatchDeltaBuffer *PDBuff, unsigned int offset_type,
    unsigned int gdiff_version);*/
    //initPDBuffer(&PDBuff, delta_fh, 5, 4096);
    copen(&src_cfh, src_fh, 0, src_stat.st_size, NO_COMPRESSOR, CFILE_RONLY);
    copen(&delta_cfh, delta_fh, 5, delta_stat.st_size, NO_COMPRESSOR, CFILE_RONLY);
    copen(&out_cfh, out_fh, 0, 0, NO_COMPRESSOR, CFILE_WONLY);
    printf("here goes...\n");
    /*printf("dumping initial buffer\n");
    printf("initial value(%u)\n", PDBuff.buffer[0]);
    printf("filled_len(%u), buff(%u)\n", PDBuff.filled_len, PDBuff.buff_size);
    printf("%*s\n", 5800, PDBuff.buffer);*/
    printf("patchf->raw_fh_pos(%lu)\n", delta_cfh.raw_fh_pos);
	DCBufferInit(&dcbuff, 1000000);
    //gdiffReconstructFile(src_fh, out_fh, &patchfile, ENCODING_OFFSET_START, 4);
    printf("patchf cposition(%lu)\n", ctell(&delta_cfh,CSEEK_ABS));
    printf("converting gdiff encoding to a dcbuff...\n");
   	gdiffReconstructDCBuff(&delta_cfh, &dcbuff, ENCODING_OFFSET_START, 4);
   	printf("reconstructing target file based off of dcbuff commands...\n");
   	reconstructFile(&dcbuff, &src_cfh, &delta_cfh, &out_cfh);
   	printf("reconstruction done.  calling close.\n");
	cclose(&out_cfh);
	return 0;
}


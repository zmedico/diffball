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
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "cfile.h"
#include "diff-algs.h"
#include "bit-functions.h"
#include <string.h>

unsigned long convertDec(unsigned char *buff, unsigned int len)
{
    unsigned long num=0,x=0;
    while(len > x)
	num = (num * 10) + (buff[x++] - 48);
    return num;
}

int main(int argc, char **argv)
{
    struct stat src_stat, ver_stat;
    struct cfile out_cfile, src_cfile, ver_cfile;
    int src_fh, ver_fh, out_fh;
    //char *src, *trg;
    unsigned char *src;
    unsigned long seed_len, multi;
    if(argc <3){
		printf("pardon, but...\nI need at least 2 args- (source file), (target file), [patch-file]\n");
		exit(EXIT_FAILURE);
    }
    if(stat(argv[1], &src_stat)) {
		perror("what the hell, stat failed.  wtf?\n");
		exit(1);
    }
    if(stat(argv[2], &ver_stat)) {
		perror("what the hell, stat failed.  wtf?\n");
		exit(1);
    }
    //printf("src_fh size=%lu\ntrg_fh size=%lu\n", 
    //	(unsigned long)src_stat.st_size, (unsigned long)ver_stat.st_size);
    if ((src_fh = open(argv[1], O_RDONLY,0)) == -1) {
		printf("Couldn't open %s, does it exist?\n", argv[1]);
		exit(EXIT_FAILURE);
    }
    if ((ver_fh = open(argv[2], O_RDONLY,0)) == -1) {
		printf("Couldn't open %s, does it exist?\n", argv[2]);
		exit(EXIT_FAILURE);
    }
/*    if(argc==3) {
		if((out_fh = dup(0))==-1){
	    	printf("well crud, couldn't duplicate stdout.  Likely a bug in differ.\n");
	   		exit(EXIT_FAILURE);
		}
    } else */if((out_fh = open(argv[3], O_WRONLY | O_TRUNC | O_CREAT, 0644))==-1) {
		printf("Couldn't create\truncate patch file.\n");
		exit(EXIT_FAILURE);
    } else {
		fprintf(stderr,"storing generated delta in '%s'\n", argv[3]);
    }
    copen(&out_cfile, out_fh, 0, 0, NO_COMPRESSOR, CFILE_WONLY);
    printf("allocing %lu\n", src_stat.st_size);
	src=(unsigned char *)malloc(src_stat.st_size);
	printf("alloced\n");
	//ver=(unsigned char *)malloc(ver_stat.st_size);
	printf("reading\n");
	read(src_fh, src, src_stat.st_size);
	printf("read\n");
	//read(trg_fh, trg, trg_stat.st_size);
	/*signed int cmemopen(struct cfile *cfile, unsigned char *buff, 
	unsigned long fh_start, unsigned long fh_end, unsigned int compressor_type)*/
	printf("cmemopening\n");
	cmemopen(&src_cfile, src, 0, src_stat.st_size, NO_COMPRESSOR);
	printf("opened\n");
	//cmemopen(&trg_cfile, trg, 0, trg_stat.st_size, NO_COMPRESSOR);
	//copen(&src_cfile, src_fh, 0, src_stat.st_size, NO_COMPRESSOR, CFILE_RONLY);
	printf("copening ver_cfile\n");
	copen(&ver_cfile, ver_fh, 0, ver_stat.st_size, NO_COMPRESSOR, CFILE_RONLY);
	printf("opened\n");
    if(argc < 5) {
		seed_len = 16;
		multi = 1;
    } else {	     
		seed_len = convertDec(argv[4], strlen(argv[4]));
	if(argc < 6) {
		    multi = 1;
		} else {
	    	multi = convertDec(argv[5], strlen(argv[5]));
		}
    }
    printf("using seed_len(%lu), multi(%lu)\n", seed_len, multi);
    /*OneHalfPassCorrecting(USE_GDIFF_ENCODING, ENCODING_OFFSET_DC_POS, src, 
    	(unsigned long)src_stat.st_size,
	trg, trg_stat.st_size, (unsigned int)seed_len, &out_cfile);*/
	/*signed int OneHalfPassCorrecting(unsigned int encoding_type,
    unsigned int offset_type, struct cfile *ref_cfh, 
    struct cfile *ver_cfh, unsigned int seed_len);*/
    printf("calling one half\n");
    OneHalfPassCorrecting(USE_GDIFF_ENCODING, ENCODING_OFFSET_START, &src_cfile,
    	&ver_cfile, &out_cfile, (unsigned int)seed_len, (unsigned int)multi);
    printf("exiting\n");
    cclose(&out_cfile);
    return 0;
}

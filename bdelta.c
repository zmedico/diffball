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
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include "dcbuffer.h"
#include "cfile.h"
#include "bit-functions.h"
#include "bdelta.h"
#define MAX(x,y) ((x) > (y) ? (x) : (y))
#define MIN(x,y) ((x) < (y) ? (x) : (y))

signed int 
bdeltaEncodeDCBuffer(CommandBuffer *dcbuff, cfile *ver_cfh, 
    cfile *patchf)
{
    unsigned long dc_pos, count, maxnum, matches;
    unsigned long add_len, copy_len, copy_offset;
    DCLoc *add_prev;
    unsigned char prev, current;
    unsigned int intsize;
    unsigned char buff[16];
    buff[0] = 'B';
    buff[1] = 'D';
    buff[2] = 'T';
    writeUBytesLE(buff + 3, 1, 2); //version
    count = DCBufferReset(dcbuff);
    /* since this will be collapsing all adds... */
    current = DC_COPY;
    matches=0;
    while(count--) {
	prev = current;
	current = current_command_type(dcbuff);
	if(! ((prev==DC_ADD && current==DC_COPY) ||
	    (prev==DC_ADD && current==DC_ADD)) )
	    matches++;
	DCBufferIncr(dcbuff);
    }
    maxnum = MAX(matches, MAX(dcbuff->src_size, dcbuff->ver_size) );
    if(maxnum <= 0x7f)
	intsize=1;
    else if(maxnum <= 0x7fff)
	intsize=2;
    else if(maxnum <= 0x7fffff)
	intsize=3;
    else
	intsize=4;
    printf("size1=%lu, size2=%lu, matches=%lu, intsize=%u\n", dcbuff->src_size, 
	dcbuff->ver_size, matches, intsize);
    buff[5] = intsize;
    cwrite(patchf, buff, 6);
    writeUBytesLE(buff, dcbuff->src_size, intsize);
    writeUBytesLE(buff + intsize, dcbuff->ver_size, intsize);
    writeUBytesLE(buff + (2 * intsize), matches, intsize);
    cwrite(patchf, buff, (3 * intsize));
    count = DCBufferReset(dcbuff);
    dc_pos=0;
    unsigned long match_orig = matches;
    while(matches--) {
	printf("handling match(%lu)\n", match_orig - matches);
	add_len=0;
	if(DC_ADD==current_command_type(dcbuff)) {
	    do {
		add_len += dcbuff->lb_tail->len;
	    	count--;
		DCBufferIncr(dcbuff);
	    } while(count!=0 && current_command_type(dcbuff)==DC_ADD);
	    printf("writing add len=%lu\n", add_len);
	}
	/* basically a fall through to copy, if count!=0 */
	if(count != 0) {
	    copy_len = dcbuff->lb_tail->len;
	    if(dc_pos > dcbuff->lb_tail->offset) {
		printf("negative offset, dc_pos(%lu), offset(%lu)\n",
		    dc_pos, dcbuff->lb_tail->offset);
		copy_offset = dcbuff->lb_tail->offset + (~dc_pos + 1);
	    } else {
		printf("positive offset, dc_pos(%lu), offset(%lu)\n",
		    dc_pos, dcbuff->lb_tail->offset);
		copy_offset = dcbuff->lb_tail->offset - dc_pos;
	    }
	    dc_pos = dcbuff->lb_tail->offset + dcbuff->lb_tail->len;
printf("writing copy_len=%lu, offset=%lu, dc_pos=%lu, stored_off=0x%x\n",
		copy_len, dcbuff->lb_tail->offset, dc_pos, copy_offset);
	} else {
	    copy_len = 0;
	    copy_offset = 0;
	}
	writeUBytesLE(buff, copy_offset, intsize);
	printf("stored offset in buff is 0x%2.2x%2.2x%2.2x==0x%2.2x%2.2x%2.2x\n",
	   buff[0], buff[1], buff[2], buff[2], buff[1], buff[0]);
	writeUBytesLE(buff + intsize, add_len, intsize);
	writeUBytesLE(buff + (2 * intsize), copy_len, intsize);
	cwrite(patchf, buff, (3 * intsize));
	DCBufferIncr(dcbuff);
	count--;
    }
    /* control block wrote. */
    count = DCBufferReset(dcbuff);
    printf("writing add_block at %lu\n", ctell(patchf, CSEEK_FSTART));
    while(count--) {
	if(current_command_type(dcbuff)==DC_ADD) {
	    if(dcbuff->lb_tail->len != 
		copy_cfile_block(patchf, ver_cfh, dcbuff->lb_tail->offset,
		dcbuff->lb_tail->len) )
		abort();
	}
	DCBufferIncr(dcbuff);
    }
    return 0;
}

signed int 
bdeltaReconstructDCBuff(cfile *patchf, CommandBuffer *dcbuff)
{

    unsigned int int_size;
    #define BUFF_SIZE 12
    unsigned int ver;
    unsigned char buff[BUFF_SIZE];
    unsigned long matches, add_len, copy_len, copy_offset;
    unsigned long size1, size2, or_mask=0, neg_mask;
    unsigned long ver_pos = 0, add_pos;
    unsigned long add_start;
    cseek(patchf, 3, CSEEK_FSTART);
    cread(patchf, buff, 2);
    ver = readUBytesLE(buff, 2);
    printf("ver=%u\n", ver);
    cread(patchf, buff, 1);
    int_size = buff[0];
    printf("int_size=%u\n", int_size);
    /* yes, this is an intentional switch fall through. */
    switch(int_size) {
	case 1: or_mask |= 0x0000ff00;
	case 2: or_mask |= 0x00ff0000;
	case 3: or_mask |= 0xff000000; 
    }
    /* yes this will have problems w/ int_size==0 */
    neg_mask = (1 << ((int_size * 8) -1));
    cread(patchf, buff, 3 * int_size);
    size1 = readUBytesLE(buff, int_size);
    size2 = readUBytesLE(buff + int_size, int_size);
    matches = readUBytesLE(buff + (2 * int_size), int_size);
    printf("size1=%lu, size2=%lu\nmatches=%lu\n", size1, size2, matches);
    /* add_pos = header info, 3 int_size's (size(1|2), num_match) */
    add_pos = 3 + 2 + 1 + (3 * int_size);
    /* add block starts after control data. */
    add_pos += (matches * (3 * int_size));
    add_start = add_pos;
    printf("add block starts at %lu\nprocessing commands\n", add_pos);
    unsigned long match_orig = matches;
    while(matches--){
	printf("handling match(%lu)\n", match_orig - matches);
	cread(patchf, buff, 3 * int_size);
	copy_offset = readUBytesLE(buff, int_size);
	add_len = readUBytesLE(buff + int_size, int_size);
	copy_len = readUBytesLE(buff + int_size * 2, int_size);
    	if(add_len) {
	    printf("add  len(%lu)\n", add_len);
	    DCBufferAddCmd(dcbuff, DC_ADD, add_pos, add_len);
	    add_pos += add_len;
	}
	/* hokay, this is screwed up.  Course bdelta seems like it's got 
	a possible problem w/ files produced on one's complement systems when
	read on a 2's complement system. */
	if((copy_offset & neg_mask) > 0) {
	    copy_offset |= or_mask;
/*	    copy_offset = ~copy_offset;
	    copy_offset += 1;
	    ver_pos = copy_offset;*/
	    ver_pos -= (~copy_offset) +1;
	    printf("ver_pos now(%lu)\n", ver_pos);
	} else {
	    printf("positive offset\n", copy_offset);
	    ver_pos += copy_offset;
	}
	/* an attempt to ensure things aren't whacky. */
	assert(ver_pos <= size1);
	if(copy_len) {
	    printf("copy len(%lu), off(%ld), pos(%lu)\n", 
		copy_len, (signed long)copy_offset, ver_pos);
	    DCBufferAddCmd(dcbuff, DC_COPY, ver_pos, copy_len);
	    ver_pos += copy_len;
	}
    }
    assert(ctell(patchf, CSEEK_FSTART)==add_start);
    printf("finished reading.  ver_pos=%lu, add_pos=%lu\n",
	ver_pos, add_pos);
    return 0;
}

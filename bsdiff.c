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
#include "defs.h"
#include <string.h>
#include "bsdiff.h"
#include "bit-functions.h"

unsigned int
check_bsdiff_magic(cfile *patchf)
{
    unsigned char buff[BSDIFF_MAGIC_LEN];
    cseek(patchf, 0, CSEEK_FSTART);
    if(BSDIFF_MAGIC_LEN != cread(patchf, buff, BSDIFF_MAGIC_LEN)) {
	return 0;
    }
    if(memcmp(buff, BSDIFF_4_MAGIC, BSDIFF_MAGIC_LEN)!=0 && 
	memcmp(buff, BSDIFF_3_MAGIC, BSDIFF_MAGIC_LEN)!=0 && 
	memcmp(buff, BSDIFF_QS_MAGIC, BSDIFF_MAGIC_LEN)!=0) {
	return 0;
    }
    return 1;
}

signed int 
bsdiffEncodeDCBuffer(CommandBuffer *buffer, cfile *ver_cfh, cfile *out_cfh)
{
#define BUFFER_SIZE 1024
//    unsigned char src_md5[16], ver_md5[16];
/*    unsigned char buff[BUFFER_SIZE];
    unsigned long count, fh_pos, delta_pos;
    unsigned int lb;
    DCommand dc;
    DCBufferReset(buffer);
    memcpy(buff, BDIFF_MAGIC, BDIFF_MAGIC_LEN);
    buff[BDIFF_MAGIC_LEN] = BDIFF_VERSION;
    writeUBytesBE(buff + BDIFF_MAGIC_LEN + 1,
	(BDIFF_DEFAULT_MAXBLOCKSIZE), 4);
    cwrite(out_cfh, buff, BDIFF_MAGIC_LEN + 5);
    delta_pos = 10;
    fh_pos = 0;
    count=0;
    while(DCB_commands_remain(buffer)) {
//    while(count--) {
	DCB_get_next_command(buffer, &dc);
	if(DC_COPY == dc.type) {
	    v2printf("copy command, out_cfh(%lu), fh_pos(%lu), offset(%lu), len(%lu)\n",
		delta_pos, fh_pos, dc.loc.offset, dc.loc.len);
	    fh_pos += dc.loc.len;
	    lb = 5;
	    buff[0] = 0;
	    writeUBytesBE(buff + 1, dc.loc.offset, 4);
	    if(dc.loc.len > 5 && dc.loc.len <= 5 + 0x3f) {
		buff[0] = dc.loc.len -5 ;
	    } else {
		writeUBytesBE(buff + 5, dc.loc.len, 4);
		lb += 4;
	    }
	    delta_pos += lb;
	    cwrite(out_cfh, buff, lb);
	} else {
	    v2printf("add  command, out_cfh(%lu), fh_pos(%lu), len(%lu)\n", 
		delta_pos, fh_pos, dc.loc.len);
	    fh_pos += dc.loc.len;
	    buff[0] = 0x80;
	    lb = 1;
	    if(dc.loc.len > 5 && dc.loc.len <= 5 + 0x3f) {
		buff[0] |= dc.loc.len - 5;
	    } else {
		writeUBytesBE(buff + 1, dc.loc.len, 4);
		lb += 4;
	    }
	    delta_pos += lb + dc.loc.len;
	    cwrite(out_cfh, buff, lb);
	    if(dc.loc.len != copy_cfile_block(out_cfh, ver_cfh, dc.loc.offset, 
		dc.loc.len))
		abort();
	}
    }*/
    return 0;
}

signed int 
bsdiffReconstructDCBuff(cfile *patchf, CommandBuffer *dcbuff)
{
    

/*    unsigned char src_md5[16], ver_md5[16], buff[17];
    unsigned long len, offset, maxlength;
    unsigned long fh_pos;
    memset(src_md5, 0, 16);
    memset(ver_md5, 0, 16);
    cseek(patchf, 6, CSEEK_ABS);
    cread(patchf, buff, 4);
    maxlength = readUBytesBE(buff, 4);
    fh_pos = 0;
    while(1 == cread(patchf, buff, 1)) {
	v2printf("got command(%u): ", buff[0]);
	if((buff[0] >> 6)==00) {
	    buff[0] &= 0x3f;
	    v2printf("got a copy at %lu, fh_pos(%lu): ", 
		ctell(patchf, CSEEK_FSTART), fh_pos);
	    if(4 != cread(patchf, buff + 1, 4)) {
		abort();
	    }
	    offset = readUBytesBE(buff + 1, 4);
	    if(buff[0]) {
		len = readUBytesBE(buff, 1) + 5;
	    } else {
		if(4 != cread(patchf, buff, 4)) {
		    abort();
		}
		len = readUBytesBE(buff, 4);
	    }
	    fh_pos += len;
	    v2printf(" offset(%lu), len=%lu\n", offset, len);
	    DCB_add_copy(dcbuff, offset, 0, len);
	} else if ((buff[0] >> 6)==2) {
	    buff[0] &= 0x3f;
	    v2printf("got an add at %lu, fh_pos(%lu):", 
		ctell(patchf, CSEEK_FSTART), fh_pos);
	    if(buff[0]) {
		len = readUBytesBE(buff, 1) + 5;
	    } else {
		if(4 != cread(patchf, buff, 4)) {
		    abort();
		}
		len = readUBytesBE(buff, 4);
	    }
	    fh_pos += len;
	    v2printf(" len=%lu\n", len);
	    DCB_add_add(dcbuff, ctell(patchf, CSEEK_FSTART), len);
	    cseek(patchf, len, CSEEK_CUR);
	} else if((buff[0] >> 6)==1) {
	    buff[0] &= 0x3f;
	    v2printf("got a checksum at %lu\n", ctell(patchf, CSEEK_FSTART));
	    if(buff[0] <= 1) {
		if(16 != cread(patchf, buff + 1, 16)) 
		    abort();
		if(buff[0]==0) 
		    memcpy(src_md5, buff + 1, 16);
		else 
		    memcpy(ver_md5, buff + 1, 16);
	    } else {
		abort();
	    }
	}
    }
    DCBUFFER_REGISTER_ADD_CFH(dcbuff, patchf);*/
    return 0;
}
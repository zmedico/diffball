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
#include <unistd.h>
#include "defs.h"
#include <string.h>
#include <fcntl.h>
#include "cfile.h"

#define MIN(x,y) ((x) < (y) ? (x) : (y))


unsigned int internal_gzopen(cfile *cfh);

/* quick kludge */
signed long
cfile_identify_compressor(int fh)
{
    unsigned char buff[2];
    if(read(fh, buff, 2)!=2) {
	return -1;
    } else {
	lseek(fh, -2, SEEK_CUR);
    }
    if(memcmp(buff, "BZ", 2)==0) {
        return BZIP2_COMPRESSOR;
    } else if(0x1f==buff[0] && 0x8b==buff[1]) {
        return GZIP_COMPRESSOR;
    }
    return NO_COMPRESSOR;
}


unsigned int
copen_child_cfh(cfile *cfh, cfile *parent, unsigned long fh_start, 
    unsigned long fh_end, unsigned int compressor_type, unsigned int 
    access_flags)
{
    dcprintf("copen_child_cfh: calling internal_copen\n");
    cfh->state_flags = CFILE_CHILD_CFH;
    cfh->lseek_info.last_ptr = &parent->lseek_info.parent.last;
    parent->lseek_info.parent.handle_count++;
    cfh->cfh_id = parent->lseek_info.parent.handle_count;
    unsigned int x = internal_copen(cfh, parent->raw_fh, fh_start, fh_end, 
	compressor_type, access_flags);
    return x;
}


unsigned int
copen(cfile *cfh, int fh, unsigned long fh_start, unsigned long fh_end, 
    unsigned int compressor_type, unsigned int access_flags)
{
    dcprintf("copen (parent): calling internal_copen\n");
    cfh->state_flags = 0;
    cfh->lseek_info.parent.last = 0;
    cfh->lseek_info.parent.handle_count =1;
    cfh->cfh_id = 1;
    unsigned int x = internal_copen(cfh, fh, fh_start, fh_end, 
	compressor_type, access_flags);
    return x;
}


unsigned int
internal_copen(cfile *cfh, int fh, unsigned long fh_start, unsigned long fh_end, 
    unsigned int compressor_type, unsigned int access_flags)
{
    const EVP_MD *md;
    signed long ret_val;
    /* this will need adjustment for compressed files */
    cfh->raw_fh = fh;

    assert(fh_start <= fh_end);
    cfh->access_flags = (access_flags & ~CFILE_COMPUTE_MD5);
    cfh->data_md5 = NULL;
    cfh->zs = NULL;
    if(access_flags & CFILE_COMPUTE_MD5) {
	cfh->state_flags |= CFILE_COMPUTE_MD5;
	OpenSSL_add_all_digests();
	md = EVP_get_digestbyname("md5");
	EVP_DigestInit(cfh->data_md5_ctx, md);
	cfh->data_md5_pos = 0;
    } else {
	cfh->data_md5_ctx = NULL;
	/* watch this, may need to change it when compression comes about */
	cfh->data_md5_pos = cfh->data_total_len;
    }

    if(AUTODETECT_COMPRESSOR == compressor_type) {
	dcprintf("copen: autodetecting comp_type: ");
	ret_val = cfile_identify_compressor(fh);
	if(ret_val < 0) {
	    abort();
	}
	dcprintf("got %i\n", ret_val);
	cfh->compressor_type = ret_val;
    } else {
	cfh->compressor_type = compressor_type;
    }

    switch(cfh->compressor_type) {
    case NO_COMPRESSOR:
	dcprintf("copen: opening w/ no_compressor\n");
	cfh->data_fh_offset = fh_start;
	cfh->data_total_len = fh_end - fh_start;
	if(access_flags & CFILE_BUFFER_ALL) {
	    cfh->data.size = cfh->data_total_len;
	} else {
	    cfh->data.size = CFILE_DEFAULT_BUFFER_SIZE;
	}
	if((cfh->data.buff = (unsigned char *)malloc(cfh->data.size))==NULL) {
	    abort();
        }
	dcprintf("copen: buffer size(%lu), buffer_all(%u)\n", cfh->data.size,
	    access_flags & CFILE_BUFFER_ALL);
	cfh->raw.size = 0;
        cfh->raw.buff = NULL;
	cfh->raw.pos = cfh->raw.offset  = cfh->raw.end = cfh->data.pos = 
	    cfh->data.offset = cfh->data.end = 0;
 	break;
 	
    case BZIP2_COMPRESSOR:
        cfh->data.size = CFILE_DEFAULT_BUFFER_SIZE;
	cfh->raw.size = CFILE_DEFAULT_BUFFER_SIZE;
	cfh->raw_fh_offset = fh_start;
	cfh->raw_total_len = fh_end - fh_start;
	cfh->data_fh_offset = 0;
	cfh->data_total_len = 0;
	if((cfh->bzs = (bz_stream *)
	    malloc(sizeof(bz_stream)))==NULL) {
	    abort();
	} else if((cfh->data.buff = (unsigned char *)malloc(cfh->data.size))==NULL) {
	    abort();
        } else if((cfh->raw.buff = (unsigned char *)malloc(cfh->raw.size))==NULL) {
	    abort();
        }
	cfh->bzs->bzalloc = NULL;
	cfh->bzs->bzfree =  NULL;
	cfh->bzs->opaque = NULL;
/*	if(cfh->access_flags & CFILE_WONLY)
	    BZ2_bzCompressInit(cfh->bzs, BZIP2_DEFAULT_COMPRESS_LEVEL, 
		BZIP2_VERBOSITY_LEVEL, BZIP2_DEFAULT_WORK_LEVEL);
	    cfh->bzs->next_in = cfh->data.buff;
	    cfh->bzs->next_out = cfh->raw.buff;
	else {*/
	    BZ2_bzDecompressInit(cfh->bzs, BZIP2_VERBOSITY_LEVEL, 0);
            cfh->bzs->next_in = cfh->raw.buff;
            cfh->bzs->next_out = cfh->data.buff;
            cfh->bzs->avail_in = cfh->bzs->avail_out = 0;
//        }
	cfh->raw.pos = cfh->raw.offset  = cfh->raw.end = cfh->data.pos = 
	    cfh->data.offset = cfh->data.end = 0;

	break;

    case GZIP_COMPRESSOR:
        cfh->data.size = CFILE_DEFAULT_BUFFER_SIZE;
	cfh->raw.size = CFILE_DEFAULT_BUFFER_SIZE;
	cfh->raw_fh_offset = fh_start;
	cfh->raw_total_len = fh_end - fh_start;
	cfh->data_fh_offset = 0;
	cfh->data_total_len = 0;
	if((cfh->zs = (z_stream *)malloc(sizeof(z_stream)))
	    ==NULL) { 
	    abort();
	} else if((cfh->data.buff = (unsigned char *)malloc(cfh->data.size))==NULL) {
	    abort();
        } else if((cfh->raw.buff = (unsigned char *)malloc(cfh->raw.size))==NULL) {
	    abort();
        }
	internal_gzopen(cfh);
	cfh->data.pos = cfh->data.offset = cfh->data.end = 0;
	break;
    }
    /* no longer in use.  leaving it as a reminder for updating when 
	switching over to the full/correct sub-window opening */
//    cfh->state_flags |= CFILE_SEEK_NEEDED;
    return 0;
}

unsigned int
internal_gzopen(cfile *cfh)
{
    unsigned int x, y, skip;
    dcprintf("internal gz_open called\n");
    assert(cfh->zs != NULL);
    cfh->zs->next_in = cfh->raw.buff;
    cfh->zs->next_out = cfh->data.buff;
    cfh->zs->avail_out = cfh->zs->avail_in = 0;
    cfh->zs->zalloc = NULL;
    cfh->zs->zfree = NULL;
    cfh->zs->opaque = NULL;
    cfh->raw.pos = cfh->raw.end = 0;
    /* skip the headers */
    cfh->raw.offset = 2;
	
    if(ENSURE_LSEEK_POSITION(cfh)) {
	abort();
    }
    x = read(cfh->raw_fh, cfh->raw.buff, MIN(cfh->raw.size, 
	cfh->raw_total_len -2));
    cfh->raw.end = x;
    if(inflateInit2(cfh->zs, -MAX_WBITS) != Z_OK) {
	abort();
    }

/* pulled straight out of zlib's gzio.c. */
#define GZ_RESERVED	0xE0
#define GZ_HEAD_CRC	0x02
#define GZ_EXTRA_FIELD	0x04
#define GZ_ORIG_NAME	0x08
#define GZ_COMMENT	0x10

    if(cfh->raw.buff[0]!= Z_DEFLATED || (cfh->raw.buff[1] & GZ_RESERVED)) {
	abort();
    }
    /* save flags, since it's possible the gzip header > cfh->raw.size */
    x = cfh->raw.buff[1];
    skip = 0;
    /* set position to after method,flags,time,xflags,os code */
    cfh->raw.pos = 8;
    if(x & GZ_EXTRA_FIELD) {
	cfh->raw.pos += ((cfh->raw.buff[7] << 8) | cfh->raw.buff[6]) + 4;
	if(cfh->raw.pos > cfh->raw.end) {
	    cfh->raw.offset += cfh->raw.pos;
	    cfh->raw.pos = cfh->raw.end;
	    if(raw_cseek(cfh)) {
		abort();
	    }
	}
    }
    if(x & GZ_ORIG_NAME)
	skip++;
    if(x & GZ_ORIG_NAME)
	skip++;
    if(x & GZ_COMMENT)
	skip++;
    while(skip) {
	while(cfh->raw.buff[cfh->raw.pos]==0) {
	    if(cfh->raw.end == cfh->raw.pos) {
		cfh->raw.offset += cfh->raw.end;
		y = read(cfh->raw_fh, cfh->raw.buff, MIN(cfh->raw.size, 
		    cfh->raw_total_len - cfh->raw.offset));
		cfh->raw.end = y;
		cfh->raw.pos = 0;
	    }
	}
	skip--;
    }
    if(x & GZ_HEAD_CRC) {
	cfh->raw.pos +=2;
	if(cfh->raw.pos >= cfh->raw.end) {
	    cfh->raw.offset += cfh->raw.pos;
	    cfh->raw.pos = cfh->raw.end = 0;
	}
    }
    cfh->zs->avail_in = cfh->raw.end - cfh->raw.pos;
    cfh->zs->next_in = cfh->raw.buff + cfh->raw.pos;
    return 0L;
}

unsigned int
cclose(cfile *cfh)
{
    if(cfh->access_flags & CFILE_WONLY) {
	cflush(cfh);
    }
    if(cfh->data.size)
	free(cfh->data.buff);
    if(cfh->raw.size)
	free(cfh->raw.buff);
    if(cfh->state_flags & CFILE_COMPUTE_MD5) {
	free(cfh->data_md5_ctx);
	if(cfh->state_flags & CFILE_MD5_FINALIZED)
	    free(cfh->data_md5);
	cfh->data_md5_pos = 0;
    }
    if(cfh->compressor_type == BZIP2_COMPRESSOR) {
	if(cfh->access_flags & CFILE_WONLY) {
	    BZ2_bzCompressEnd(cfh->bzs);
	} else {
	    BZ2_bzDecompressEnd(cfh->bzs);
	}
	free(cfh->bzs);
    }
    if(cfh->compressor_type == GZIP_COMPRESSOR) {
//	gzclose(cfh->gz_handle);
	//free(cfh->gz_handle);
    }
    cfh->raw.pos = cfh->raw.end = cfh->raw.size = cfh->raw.offset = 
	cfh->data.pos = cfh->data.end = cfh->data.size = cfh->data.offset = 
	cfh->raw_total_len = cfh->data_total_len = 0;
    return 0;
}

unsigned long
cread(cfile *cfh, unsigned char *buff, unsigned long len)
{
    unsigned long bytes_wrote=0;
    unsigned long x;
    while(bytes_wrote != len) {
	if(cfh->data.end==cfh->data.pos) {
	    if(0==crefill(cfh))
		return(bytes_wrote);
	}
	x = MIN(cfh->data.end - cfh->data.pos, len - bytes_wrote);
	/* possible to get stuck in a loop here, fix this */
	memcpy(buff + bytes_wrote, cfh->data.buff + cfh->data.pos, x);
	bytes_wrote += x;
	cfh->data.pos += x;
    }
    return bytes_wrote;
}

unsigned long
cwrite(cfile *cfh, unsigned char *buff, unsigned long len)
{
    unsigned long bytes_wrote=0, x;
    while(bytes_wrote < len) {
	if(cfh->data.size==cfh->data.pos) {
	    cflush(cfh);
	    /* was originally, assert(cfh->data.pos!=0);
	       don't understand why, since that condition *should* be
	       impossible ... */
	}
	x = MIN(cfh->data.size - cfh->data.pos, len - bytes_wrote);
	memcpy(cfh->data.buff + cfh->data.pos, buff + bytes_wrote, x);
	bytes_wrote += x;
	cfh->data.pos += x;
    }
    return bytes_wrote;
}

unsigned long
cseek(cfile *cfh, signed long offset, int offset_type)
{
    signed long data_offset;
    if(CSEEK_ABS==offset_type) 
	data_offset = abs(offset) - cfh->data_fh_offset;
    else if (CSEEK_CUR==offset_type)
	data_offset = cfh->data.offset + cfh->data.pos +
	    offset;
    else if (CSEEK_END==offset_type)
	data_offset = cfh->data_total_len + offset;
    else if (CSEEK_FSTART==offset_type)
	data_offset = offset;
    else
	abort(); /*lovely error handling eh? */

    assert(data_offset >= 0);

    /* see if the desired location is w/in the data buff */
    if(data_offset >= cfh->data.offset &&
	data_offset <  cfh->data.offset + cfh->data.size && 
	cfh->data.end > data_offset - cfh->data.offset) {

	dcprintf("cseek: buffered data, repositioning pos\n");
	cfh->data.pos = data_offset - cfh->data.offset;
	return (CSEEK_ABS==offset_type ? data_offset + cfh->data_fh_offset: 
	    data_offset);
    }
    switch(cfh->compressor_type) {
    case NO_COMPRESSOR:
	dcprintf("cseek: no_compressor, flagging it\n");
	FLAG_LSEEK_NEEDED(cfh);
	break;
    case GZIP_COMPRESSOR:
	if(data_offset < cfh->data.offset ) {
	    /* note this ain't optimal, but the alternative is modifying 
	       zlib to support seeking via buffers... */
	    dcprintf("cseek: zs data_offset < cfh->data.offset, resetting\n");
	    FLAG_LSEEK_NEEDED(cfh);
	    inflateEnd(cfh->zs);
	    cfh->state_flags &= ~CFILE_EOF;
	    internal_gzopen(cfh);	    
	    cfh->data.offset = cfh->raw.offset = cfh->raw.pos = 0;
	} else {
	    if(ENSURE_LSEEK_POSITION(cfh)) {
		abort();
	    }
	}
	dcprintf("cseek: zs: data_off(%lu), data.offset(%lu)\n", data_offset, cfh->data.offset);

	while(cfh->data.offset + cfh->data.end < data_offset) {
	    if(crefill(cfh)==0) {
		abort();
	    }
	}
	cfh->data.pos = data_offset - cfh->data.offset;

	return (CSEEK_ABS==offset_type ? data_offset + cfh->data_fh_offset : data_offset);
	break;
    case BZIP2_COMPRESSOR: 
	if(data_offset < cfh->data.offset ) {
	    /* note this ain't optimal, but the alternative is modifying 
	       bzlib to support seeking... */
	    dcprintf("cseek: bz2: data_offset < cfh->data.offset, resetting\n");
	    FLAG_LSEEK_NEEDED(cfh);
	    BZ2_bzDecompressEnd(cfh->bzs);
	    cfh->bzs->bzalloc = NULL;
	    cfh->bzs->bzfree =  NULL;
	    cfh->bzs->opaque = NULL;
	    cfh->state_flags &= ~CFILE_EOF;
	    BZ2_bzDecompressInit(cfh->bzs, BZIP2_VERBOSITY_LEVEL, 0);
	    cfh->bzs->next_in = cfh->raw.buff;
	    cfh->bzs->next_out = cfh->data.buff;
	    cfh->bzs->avail_in = cfh->bzs->avail_out = 0;
	    cfh->data.end = cfh->raw.end = cfh->data.pos = 
		cfh->data.offset = cfh->raw.offset = cfh->raw.pos = 0;
	}
	dcprintf("cseek: bz2: data_off(%lu), data.offset(%lu)\n", data_offset, cfh->data.offset);
	if(ENSURE_LSEEK_POSITION(cfh)) {
	    abort();
	}
	while(cfh->data.offset + cfh->data.end < data_offset) {
	    if(crefill(cfh)==0) {
		abort();
	    }
	}
	cfh->data.pos = data_offset - cfh->data.offset;

	/* note bzip2 doens't use the normal return */
	return (CSEEK_ABS==offset_type ? data_offset + cfh->data_fh_offset : data_offset);
    }
    cfh->data.offset = data_offset;
    cfh->data.pos = cfh->data.end = 0;
    return (CSEEK_ABS==offset_type ? data_offset + cfh->data_fh_offset : 
	data_offset);
}

unsigned long
raw_cseek(cfile *cfh)
{
    SET_LAST_LSEEKER(cfh);
    if(NO_COMPRESSOR == cfh->compressor_type) {
	return (lseek(cfh->raw_fh, cfh->data.offset + cfh->data_fh_offset +
	    cfh->data.end, SEEK_SET) != 
	    (cfh->data.offset + cfh->data_fh_offset + cfh->data.end));
    } else if(BZIP2_COMPRESSOR == cfh->compressor_type || 
	GZIP_COMPRESSOR == cfh->compressor_type) {
	return (lseek(cfh->raw_fh, cfh->raw.offset + cfh->raw_fh_offset + 
	    cfh->raw.end, SEEK_SET) != (cfh->raw.offset + 
	    cfh->raw_fh_offset + cfh->raw.end));
    }
    return 1L;

}

unsigned long
ctell(cfile *cfh, unsigned int tell_type)
{
    if(CSEEK_ABS==tell_type)
	return cfh->data_fh_offset + cfh->data.offset + cfh->data.pos;
    else if (CSEEK_FSTART==tell_type)
	return cfh->data.offset + cfh->data.pos;
    else if (CSEEK_END==tell_type)
	return cfh->data_total_len - 
	    (cfh->data.offset + cfh->data.pos);
    return 0;
}

unsigned long 
cflush(cfile *cfh)
{
    if(cfh->data.pos!=0) {
        switch(cfh->compressor_type) {
	case NO_COMPRESSOR:
	    if(cfh->data.pos != 
		write(cfh->raw_fh, cfh->data.buff, cfh->data.pos))
		abort();
	    cfh->data.offset += cfh->data.pos;
	    cfh->data.pos=0;
	    break;
/*	case BZIP2_COMPRESSOR:
	    // fairly raw, if working at all //
	    if(cfh->raw.pos == cfh->raw.end) {
		if(cfh->raw.pos != write(cfh->raw_fh, cfh->raw.buff, 
		    cfh->raw.size))
		    abort();
		cfh->raw.offset += cfh->raw.end;
		cfh->raw.pos = 0;
	    }
	    cfh->bz_stream->next_in = cfh->data.buff;
	    cfh->bz_stream->avail_in = cfh->data.end;
	    if(cfh->bz_stream->avail_out==0) {
		cfh->bz_stream->next_out = cfh->raw.buff;
		cfh->bz_stream->avail_out = cfh->raw.size;
	    }
	    if(BZ_RUN_OK != BZ2_bzCompress(cfh->bz_stream, BZ_RUN)) {
		abort();
	    }
	    break;*/
	case GZIP_COMPRESSOR:
	    if(cfh->data.pos != gzwrite(cfh->gz_handle, cfh->data.buff, 
		cfh->data.pos)) 
		abort();
	    cfh->data.offset += cfh->data.pos;
	    cfh->data.pos=0;
	    break;
	}
    }
    return 0;
}

unsigned long 
crefill(cfile *cfh)
{
    unsigned long x;
//    if(RAW_LSEEK_IS_NEEDED(cfh)) {
//	if(raw_lseek(cfh)) {
//	    abort();
//	}
//    }
//    if(RAW_LSEEK_IS_NEEDED(cfh) || (cfh->state_flags & CFILE_DATA_SEEK_NEEDED)) {
//	dcprintf("crefill: calling cseek (CFILE_SEEK_NEEDED)\n");
//	cseek(cfh, cfh->data.offset, CSEEK_FSTART);
//    }
    int err;
    switch(cfh->compressor_type) {
    case NO_COMPRESSOR:
	if(ENSURE_LSEEK_POSITION(cfh)) {
	    abort();
	}
	cfh->data.offset += cfh->data.end;
	x = read(cfh->raw_fh, cfh->data.buff, MIN(cfh->data.size, 
	    cfh->data_total_len - cfh->data.offset));
	cfh->data.end = x;
	cfh->data.pos = 0;
	dcprintf("crefill: no_compress, got %lu\n", x);
	break;
	
    case BZIP2_COMPRESSOR:
	assert(cfh->bzs->total_out_lo32 >= cfh->data.offset + cfh->data.end);
	if(cfh->state_flags & CFILE_EOF) {
	    dcprintf("crefill: bz2: CFILE_EOF flagged, returning 0\n");
	    cfh->data.offset += cfh->data.end;
	    cfh->data.end = cfh->data.pos = 0;
	} else {
	    cfh->data.offset += cfh->data.end;
	    do {
		if(0 == cfh->bzs->avail_in && (cfh->raw.offset + 
		    (cfh->raw.end - cfh->bzs->avail_in) < cfh->raw_total_len)) {
		    dcprintf("crefill: bz2, refilling raw: ");
		    if(ENSURE_LSEEK_POSITION(cfh)) {
			abort();
		    }
		    cfh->raw.offset += cfh->raw.end;
		    x = read(cfh->raw_fh, cfh->raw.buff, MIN(cfh->raw.size, 
			cfh->raw_total_len - cfh->raw.offset));
		    dcprintf("read %lu of possible %lu\n", x, cfh->raw.size);
		    cfh->bzs->avail_in = cfh->raw.end = x;
		    cfh->raw.pos = 0;
		    cfh->bzs->next_in = cfh->raw.buff;
		}
		dcprintf("crefill: bz2, refilling data\n");
		cfh->bzs->avail_out = cfh->data.size;
		cfh->bzs->next_out = cfh->data.buff;
		err = BZ2_bzDecompress(cfh->bzs);

		/* note, this doesn't handle BZ_DATA_ERROR/BZ_DATA_ERROR_MAGIC , 
		which should be handled (rather then aborting) */
		if(err != BZ_OK && err != BZ_STREAM_END) {
		    v0printf("hmm, bzip2 didn't return BZ_OK, borking cause of %i.\n", err);
		    abort();
		}
		if(err==BZ_STREAM_END) {
		    dcprintf("encountered stream_end\n");
		    /* this doesn't handle u64 yet, so make it do so at some point*/
		    cfh->data_total_len = MAX(cfh->bzs->total_out_lo32, 
			cfh->data_total_len);
		    cfh->state_flags |= CFILE_EOF;
	 	}
	    }while((!(cfh->state_flags & CFILE_EOF)) && cfh->bzs->avail_in==0 && cfh->bzs->avail_out==cfh->raw.size);
	    cfh->data.end = cfh->data.size - cfh->bzs->avail_out;
	    cfh->data.pos = 0;
	}
	break;

    case GZIP_COMPRESSOR:
	assert(cfh->zs->total_out >= cfh->data.offset + cfh->data.end);
	if(cfh->state_flags & CFILE_EOF) {
	    dcprintf("crefill: gz: CFILE_EOF flagged, returning 0\n");
	    cfh->data.offset += cfh->data.end;
	    cfh->data.end = cfh->data.pos = 0;
	} else {
	    cfh->data.offset += cfh->data.end;
	    do {
		if(0 == cfh->zs->avail_in && (cfh->raw.offset + 
		    (cfh->raw.end - cfh->zs->avail_in) < cfh->raw_total_len)) {
		    dcprintf("crefill: zs, refilling raw: ");
		    if(ENSURE_LSEEK_POSITION(cfh)) {
			abort();
		    }
		    cfh->raw.offset += cfh->raw.end;
		    x = read(cfh->raw_fh, cfh->raw.buff, MIN(cfh->raw.size, 
			cfh->raw_total_len - cfh->raw.offset));
		    dcprintf("read %lu of possible %lu\n", x, cfh->raw.size);
		    cfh->zs->avail_in = cfh->raw.end = x;
		    cfh->raw.pos = 0;
		    cfh->zs->next_in = cfh->raw.buff;
		}
		dcprintf("crefill: zs, refilling data\n");
		cfh->zs->avail_out = cfh->data.size;
		cfh->zs->next_out = cfh->data.buff;
		err = inflate(cfh->zs, Z_NO_FLUSH);

		if(err != Z_OK && err != Z_STREAM_END) {
		    v0printf("hmm, zlib didn't return Z_OK, borking cause of %i.\n", err);
		    abort();
		}
		if(err==Z_STREAM_END) {
		    dcprintf("encountered stream_end\n");
		    /* this doesn't handle u64 yet, so make it do so at some point*/
		    cfh->data_total_len = MAX(cfh->zs->total_out, 
			cfh->data_total_len);
		    cfh->state_flags |= CFILE_EOF;
	 	}
	    }while((!(cfh->state_flags & CFILE_EOF)) && cfh->zs->avail_in==0 && cfh->zs->avail_out==cfh->raw.size);
	    cfh->data.end = cfh->data.size - cfh->zs->avail_out;
	    cfh->data.pos = 0;
	}
	break;	
    }
    if((cfh->state_flags & CFILE_COMPUTE_MD5) &&
	((cfh->state_flags & CFILE_MD5_FINALIZED)==0) && 
	(cfh->data.offset == cfh->data_md5_pos)) {
	EVP_DigestUpdate(cfh->data_md5_ctx, cfh->data.buff, cfh->data.end);
	cfh->data_md5_pos += cfh->data.end;
    }
    return cfh->data.end;
}

inline unsigned long
cfile_len(cfile *cfh)
{
    return cfh->data_total_len;
}

inline unsigned long
cfile_start_offset(cfile *cfh)
{
    return cfh->data_fh_offset;
}


/* while I realize this may not *necessarily* belong in cfile, 
   eh, it's going here.
   deal with it.  */
unsigned long 
copy_cfile_block(cfile *out_cfh, cfile *in_cfh, unsigned long in_offset, 
    unsigned long len) 
{
    unsigned char buff[CFILE_DEFAULT_BUFFER_SIZE];
    unsigned int lb;
    unsigned long bytes_wrote=0;;
    if(in_offset!=cseek(in_cfh, in_offset, CSEEK_FSTART))
	abort();
    while(len) {
	lb = MIN(CFILE_DEFAULT_BUFFER_SIZE, len);
	if( (lb!=cread(in_cfh, buff, lb)) ||
	    (lb!=cwrite(out_cfh, buff, lb)) )
	    abort;
	len -= lb;
	bytes_wrote+=lb;
    }
    return bytes_wrote;
}

off_u64
copy_add_block(cfile *out_cfh, cfile *src_cfh, off_u64 src_offset, 
    off_u64 len, void *extra)
{
    unsigned char buff[CFILE_DEFAULT_BUFFER_SIZE];
    unsigned int lb;
    unsigned long bytes_wrote=0;;
    if(src_offset!=cseek(src_cfh, src_offset, CSEEK_FSTART))
	abort();
    while(len) {
	lb = MIN(CFILE_DEFAULT_BUFFER_SIZE, len);
	if( (lb!=cread(src_cfh, buff, lb)) ||
	    (lb!=cwrite(out_cfh, buff, lb)) )
	    abort;
	len -= lb;
	bytes_wrote+=lb;
    }
    return bytes_wrote;
}

/* it's here for the moment, move it once the necessary changes are finished. */


unsigned int
cfile_finalize_md5(cfile *cfh)
{
    unsigned int md5len;
    /* check to see if someone is being a bad monkey... */
    assert(cfh->state_flags & CFILE_COMPUTE_MD5);
    /* better handling needed... */
    if((cfh->data_md5 = (unsigned char *)malloc(16))==NULL)
	return 1;
    if(ctell(cfh, CSEEK_FSTART)!=cfh->data_md5_pos) 
	cseek(cfh, cfh->data_md5_pos, CSEEK_FSTART);

    /* basically read in all data needed. 
	since commiting of md5 data is done by crefill, call it till tis empty
	*/
    while(cfh->data_md5_pos != cfh->data_total_len) 
	crefill(cfh);
    EVP_DigestFinal(cfh->data_md5_ctx, cfh->data_md5, &md5len);
    cfh->state_flags |= CFILE_MD5_FINALIZED;
    return 0;
}

cfile_window *
expose_page(cfile *cfh)
{
    if(cfh->data.end==0) 
	crefill(cfh);
    return &cfh->data;
}

cfile_window *
next_page(cfile *cfh)
{
    crefill(cfh);
    return &cfh->data;
}

cfile_window *
prev_page(cfile *cfh)
{
    /* possibly due an error check or something here */
    if(cfh->data.offset ==0) {
	cfh->data.end=0;
	cfh->data.pos=0;
    } else {
	cseek(cfh, -cfh->data.size, CSEEK_CUR);
	crefill(cfh);
    }
    return &cfh->data;
}

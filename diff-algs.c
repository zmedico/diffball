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
#include <errno.h>
#include <string.h>
#include "adler32.h"
#include "diff-algs.h"
#include "primes.h"
#include "defs.h"
#include "bit-functions.h"

/* this is largely based on the algorithms detailed in randal burn's various papers.
   Obviously credit for the alg's go to him, although I'm the one who gets the dubious
   credit for bugs in the implementation of said algorithms... */

signed int
OneHalfPassCorrecting2(CommandBuffer *dcb, RefHash *rh, unsigned char rid, cfile *vcfh, unsigned char vid)
{
    ADLER32_SEED_CTX ads;
    off_u32		va, vs, vc, vm, rm, ver_len, len, ref_len, ver_start, ref_start;
    cfile_window	*vcfw, *rcfw;
    unsigned long bad_match=0, no_match=0, good_match=0;
    unsigned long hash_offset, x;
    int err;
    err = init_adler32_seed(&ads, rh->seed_len, 1);
    if(err)
    	return err;
    va = vs = vc = 0;
    ver_len = cfile_len(vcfh);
    ver_start = cfile_start_offset(vcfh);
    ref_len = cfile_len(rh->ref_cfh);
    ref_start = cfile_start_offset(rh->ref_cfh);

    if(0 != cseek(vcfh, 0, CSEEK_FSTART)) {
	return IO_ERROR;
    }
    vcfw = expose_page(vcfh);
    if(vcfw->end == 0 && vcfw->offset != ver_len)
	return(IO_ERROR);
    
    #define end_pos(x)	((x)->offset + (x)->end)
    while(vcfw->end != 0) {
    	if(va < vc) {
    	    va = vc;
    	}
	if(va >= end_pos(vcfw)){
	    if(va != cseek(vcfh, va, CSEEK_FSTART))
	    	return(IO_ERROR);
	    vcfw = expose_page(vcfh);
	    if(vcfw->end == 0 && vcfw->offset != ver_len)
	    	return IO_ERROR;
	}
	x = MIN(end_pos(vcfw) - va, vc + rh->seed_len - va);
	update_adler32_seed(&ads, vcfw->buff + va - vcfw->offset, x);
	va += x;
	if(vc + rh->seed_len > va) {
	   // loop back to get refilled from above.
	   continue;
	}
	// check the hash for a match
	hash_offset = lookup_offset(rh, &ads);
	if(hash_offset == 0) {
	    vc++;
	    no_match++;
	    continue;
	}
	if(hash_offset != cseek(rh->ref_cfh, hash_offset, CSEEK_FSTART)) {
	    v0printf("error seeking in ref file\n");
	    return IO_ERROR;
	}

	rcfw = expose_page(rh->ref_cfh);
	//verify we haven't hit checksum collision
	vm = vc;
	for(x=0; x < rh->seed_len; x++) {
	    if(rcfw->pos == rcfw->end) {
	        rcfw = next_page(rh->ref_cfh);
		if(rcfw->end == 0) {
	    	    return IO_ERROR;
	    	}
	    }
	    if(ads.seed_chars[(ads.tail + x) % ads.seed_len] != 
	    	rcfw->buff[rcfw->pos]) {
	    	bad_match++;
	    	vc++;
		break;
	    }
	    rcfw->pos++;
	}
	if(vc != vm)
	    continue;
	good_match++;
	//back matching
	vm = vc;
	rm = hash_offset;
	while(vm > 0 && rm > 0) {
//	    if(vm -1 < vcfw->offset) {
	    while(vm - 1 < vcfw->offset) {
		vcfw = prev_page(vcfh);
		if(vcfw->end == 0)
		    return IO_ERROR;
	    }
//	    if(rm -1 < rcfw->offset) {
	    while(rm - 1 < rcfw->offset) {
		rcfw = prev_page(rh->ref_cfh);
		if(rcfw->end == 0) 
		    return IO_ERROR;
	    }
	    if(vcfw->buff[vm - 1 - vcfw->offset] == rcfw->buff[rm - 1 - rcfw->offset]) {
	    	rm--;
	    	vm--;
	    } else {
	    	break;
	    }
	}
	len = vc + rh->seed_len - vm;

	//forward matching
	//first, reposition.
	if(vm + len != cseek(vcfh, vm + len, CSEEK_FSTART)) 
	    return IO_ERROR;
	vcfw = expose_page(vcfh);
	if(vcfw->end == 0 && vcfw->offset != ver_len)
	    return IO_ERROR;

	if(rm + len != cseek(rh->ref_cfh, rm + len, CSEEK_FSTART))
	    return IO_ERROR;
	rcfw = expose_page(rh->ref_cfh);
	if(rcfw->end == 0 && rcfw->offset != ref_len)
	    return IO_ERROR;

//	while(vcfw->end > 0 && rcfw->end > 0) {
	while(vm + len < ver_len && rm + len < ref_len) {
	    if(vm + len >= end_pos(vcfw)) {
	    	vcfw = next_page(vcfh);
	    	if(vcfw->end == 0) {
	    	    if(vcfw->offset != ver_len)
	    	    	return IO_ERROR;
		    break;
		}
	    }
	    assert(vm + len <  vcfw->offset + vcfw->end);
	    assert(vm + len >= vcfw->offset);

	    if(rm + len >= end_pos(rcfw)) {
	    	rcfw = next_page(rh->ref_cfh);
	    	if(rcfw->end == 0) {
	    	    if(rcfw->offset != ref_len)
	    	    	return IO_ERROR;
	    	    break;
	    	}
	    }
	    assert(rm + len < rcfw->offset+ rcfw->end);
	    assert(rm + len >= rcfw->offset);
	    
	    if(vcfw->buff[vm + len - vcfw->offset] == rcfw->buff[rm + len - rcfw->offset]) {
		len++;
	    } else {
	    	break;
	    }
	}
	if( vs <= vm) {
	    if (vs < vm) {
	    	DCB_add_add(dcb, ver_start + vs, vm - vs, vid);
	    }
	    DCB_add_copy(dcb, ref_start + rm, ver_start + vm, len, rid);
	} else {
	    DCB_truncate(dcb, vs -vm);
	    DCB_add_copy(dcb, ref_start + rm, ver_start + vm, len, rid);
	}
	vs = vc = vm + len;
    }
    if (vs != ver_len)
    	DCB_add_add(dcb, ver_start + vs, ver_len - vs, vid);
    free_adler32_seed(&ads);
    return 0;
}
	

signed int 
OneHalfPassCorrecting3(CommandBuffer *buffer, RefHash *rhash, unsigned char ref_id, cfile *ver_cfh, unsigned  char ver_id)
{
    off_u64 ver_len, ref_len;
    int err;
    unsigned long x, len;
    unsigned long no_match=0, bad_match=0, good_match=0;
    off_u64 vc, va, vs, vm, rm, hash_offset;
    unsigned int const rbuff_size = 50, vbuff_size = 50;
    unsigned char rbuff[rbuff_size], vbuff[vbuff_size];
    off_u64 rbuff_start=0, vbuff_start=0, rbuff_end=0, vbuff_end=0;
    ADLER32_SEED_CTX ads;

    err = init_adler32_seed(&ads, rhash->seed_len, 1);
    if(err) 
	return err;
    ref_len = cfile_len(rhash->ref_cfh);    
    ver_len = cfile_len(ver_cfh);
    
    va=vs =vc =0;
    vbuff_start = cseek(ver_cfh, 0, CSEEK_FSTART);

    vbuff_end=cread(ver_cfh, vbuff, MIN(vbuff_size, ver_len));
    v3printf("vbuff_start(%lu), vbuff_end(%lu)\n", vbuff_start, vbuff_end);
    while(vc + rhash->seed_len < ver_len) {
	if(vc + rhash->seed_len > vbuff_start + vbuff_end) {
	    v3printf("full refresh of vbuff at vbuff_start(%lu), vc(%lu), fstart(%lu), abs(%lu)\n", 
		vbuff_start, vc, ctell(ver_cfh, CSEEK_FSTART),
		ctell(ver_cfh, CSEEK_ABS));
	    //if(vc > vbuff_start + vbuff_end) {
		vbuff_start = cseek(ver_cfh, vc, CSEEK_FSTART);
		vbuff_end = cread(ver_cfh, vbuff, MIN(ver_len - vbuff_start,vbuff_size));
		v3printf("setting vbuff_start(%lu), vbuff_end(%lu), fstart(%lu)\n", 
		vbuff_start, vbuff_end, ctell(ver_cfh, CSEEK_FSTART));
	    /*} else {
		x = vbuff_size - (vc - vbuff_start);
		memmove(vbuff, vbuff + vbuff_size -x, x);
	    }*/	
	} else if (vc < vbuff_start) {
	    v3printf("partial refresh of vbuff at vbuff_start(%lu), vc(%lu), fstart(%lu), abs(%lu)\n", 
		vbuff_start, vc,ctell(ver_cfh, CSEEK_FSTART),
		    ctell(ver_cfh, CSEEK_ABS));
	    vbuff_start = cseek(ver_cfh, vc, CSEEK_FSTART);
	    vbuff_end = cread(ver_cfh, vbuff, MIN(ver_len - vbuff_start, vbuff_size));
	    v3printf("setting vbuff_start(%lu), vbuff_end(%lu), fstart(%lu)\n", 
		vbuff_start, vbuff_end, ctell(ver_cfh, CSEEK_FSTART));
	}
	if(va -vc >= rhash->seed_len) {
	    update_adler32_seed(&ads, vbuff + vc - vbuff_start, rhash->seed_len);
	} else {
	    assert(vc + rhash->seed_len - va < vbuff_end);
	    update_adler32_seed(&ads, vbuff + (va - vbuff_start), vc + rhash->seed_len -va);
	}
	va = vc + rhash->seed_len;
	hash_offset = lookup_offset(rhash, &ads);
	if(hash_offset) {
	    if(hash_offset != cseek(rhash->ref_cfh, 
		hash_offset, CSEEK_FSTART)) {

		v3printf("ctell(%lu), wanted(%lu)\n", ctell(rhash->ref_cfh, CSEEK_FSTART), 
		   hash_offset);
		return IO_ERROR;
	    } else {
		rbuff_start = hash_offset;
		rbuff_end = cread(rhash->ref_cfh, rbuff, rbuff_size);
	    }	
	    if(memcmp(rbuff, vbuff + vc - vbuff_start, rhash->seed_len)!=0){
		if(rhash->type & (RH_RMOD_HASH | RH_CMOD_HASH)) {
		    v2printf("bad match: vc(%lu), chk(%lx):i(%lu) chk(%lx):off(%lu)\n",
			vc + cfile_start_offset(ver_cfh),
			get_checksum(&ads),
			(get_checksum(&ads) % rhash->hr_size), 
			rhash->hash.chk[get_checksum(&ads) % 
			    rhash->hr_size].chksum, 
			rhash->hash.chk[get_checksum(&ads) % 
			    rhash->hr_size].offset);
		}
		bad_match++;
		vc++;
		continue;
	    }
	    good_match++;
	    x=0;
	    vm = vc;
	    rm = hash_offset;
	    /*backwards matching*/
	    if(vm-vbuff_start==0) {
		vbuff_start= cseek(ver_cfh, (vbuff_size > vbuff_start ? 0 : 
		    vbuff_start - vbuff_size), CSEEK_FSTART);
		vbuff_end=cread(ver_cfh, vbuff, vbuff_size);
		v3printf("back match moved vbuff to start(%lu), end(%lu)\n",
		    vbuff_start, vbuff_end);
	    }
	    if(rm-rbuff_start==0) {
		rbuff_start= cseek(rhash->ref_cfh, (rbuff_size > 
		    rbuff_start ? 0 : rbuff_start - rbuff_size), CSEEK_FSTART);
		rbuff_end=cread(rhash->ref_cfh, rbuff, rbuff_size);
		v3printf("back match moved rbuff to start(%lu), end(%lu)\n",
		    rbuff_start, rbuff_end);
	    }
	    while(vm > 0 && rm > 0 && vbuff[vm -vbuff_start-1] == 
		rbuff[rm -rbuff_start -1]) {
		vm--;
		rm--;
		if(vm-vbuff_start==0) {
		    vbuff_start= cseek(ver_cfh, (vbuff_size > 
			vbuff_start ? 0 : vbuff_start - vbuff_size), 
			CSEEK_FSTART);
		    vbuff_end=cread(ver_cfh, vbuff, vbuff_size);
		    v3printf("back match moved vbuff to start(%lu), end(%lu)\n",
			vbuff_start, vbuff_end);
		}
		if(rm-rbuff_start==0) {
		    rbuff_start= cseek(rhash->ref_cfh, (rbuff_size > 
			rbuff_start ? 0 : rbuff_start - rbuff_size), 
			    CSEEK_FSTART);
		    rbuff_end=cread(rhash->ref_cfh, rbuff, rbuff_size);
		    v3printf("back match moved rbuff to start(%lu), end(%lu)\n",
		        rbuff_start, rbuff_end);
		}
	    }
	    len=(vc -vm) + rhash->seed_len;

	    if(vm + len >= vbuff_start + vbuff_size) {
		vbuff_start=cseek(ver_cfh, vm+len , CSEEK_FSTART);
		vbuff_end=cread(ver_cfh, vbuff, MIN(vbuff_size, ver_len - 
		    vbuff_start));
		v3printf("forw match moved vbuff to start(%lu), end(%lu)\n",
		    vbuff_start, vbuff_end);
	    }
	    if(rm + len >= rbuff_start + rbuff_size) {
		rbuff_start=cseek(rhash->ref_cfh, rm + len, CSEEK_FSTART);
	    	rbuff_end=cread(rhash->ref_cfh, rbuff, MIN(rbuff_size, 
		    ref_len - rbuff_start));
		v3printf("forw match moved rbuff to start(%lu), end(%lu)\n",
		    rbuff_start, rbuff_end);
	    }
	    while(rm + len < ref_len && vm + len < ver_len &&
		rbuff[rm + len - rbuff_start] == vbuff[vm + len - 
		vbuff_start]) {
		len++;
		if(vm + len -vbuff_start==vbuff_size) {
		    vbuff_start += vbuff_end;
		    vbuff_end=cread(ver_cfh, vbuff, MIN(vbuff_size, ver_len - 
			vbuff_start));
		    	v3printf("forw match moved rbuff to start(%lu), end(%lu)\n",
		    	    vbuff_start, vbuff_end);
		}
		if(rm + len -rbuff_start==rbuff_size) {
		    rbuff_start += rbuff_end;
		    rbuff_end=cread(rhash->ref_cfh, rbuff, MIN(rbuff_size,
			ref_len -rbuff_start));
		    v3printf("forw match moved rbuff to start(%lu), end(%lu)\n",
		    	rbuff_start, rbuff_end);
		}
	    }

	    if (vs <= vm) {
		if (vs < vm) {
		    v2printf("\tadding vstart(%lu), len(%lu), vend(%lu): (vs < vm)\n",
			cfile_start_offset(ver_cfh) + vs, vm-vs, 
			cfile_start_offset(ver_cfh) + vm);
		    DCB_add_add(buffer, cfile_start_offset(ver_cfh) + vs, 
			vm -vs, ver_id);
		}
		v2printf("    copying src_offset(%lu), ver_offset(%lu), len(%lu), ver_end(%lu)\n", 
		    cfile_start_offset(rhash->ref_cfh) + rm, 
		    cfile_start_offset(ver_cfh) + vm, len, 
		    cfile_start_offset(ver_cfh) + vm + len);
		DCB_add_copy(buffer, cfile_start_offset(rhash->ref_cfh) +rm, 
		    cfile_start_offset(ver_cfh) + vm, len, ref_id);
	    } else {
		v2printf("    truncating(%lu) bytes: (vm < vs)\n", vs - vm);
		assert(vs -vm < cfile_len(ver_cfh));
		DCB_truncate(buffer, vs - vm);
		v2printf("    replacement copy: offset(%lu), len(%lu)\n", 
		    cfile_start_offset(rhash->ref_cfh) + rm, len);
		DCB_add_copy(buffer, cfile_start_offset(rhash->ref_cfh) + rm, 
		    cfile_start_offset(ver_cfh) + vm, len, ref_id);
	    }
	    vs = vm + len;
	    vc = vs -1;
	} else {
	    no_match++;
	}
	vc++;
    }
    if (vs != ver_len) {
	DCB_add_add(buffer, cfile_start_offset(ver_cfh) + vs, ver_len - vs, ver_id);
    }
    free_adler32_seed(&ads);
    return 0;
}


signed int
MultiPassAlg(CommandBuffer *buff, cfile *ref_cfh, unsigned char ref_id,
    cfile *ver_cfh, unsigned char ver_id, 
    unsigned long max_hash_size)
{
    int err;
    RefHash rhash;
    cfile ver_window;
    unsigned long hash_size=0, sample_rate=1;
    unsigned long int seed_len;
    unsigned long gap_req;
    unsigned long gap_total_len;
    unsigned char first_run=0;
    DCLoc dc;
    assert(buff->DCBtype & DCBUFFER_LLMATCHES_TYPE);
    err = DCB_insert(buff);
    if(err)
	return err;
    v1printf("multipass, hash_size(%lu)\n", hash_size);
    if(buff->DCB.llm.main_head == NULL) {
	seed_len = 512;
	first_run=1;
    } else {
	seed_len = 128;
    }
    for(/*seed_len = 512*/; seed_len >=16; seed_len /= 2) {
	gap_req = seed_len;// * MULTIPASS_GAP_KLUDGE;
	v1printf("\nseed size(%lu)...\n\n", seed_len);
	gap_total_len = 0;
	DCBufferReset(buff);
#ifdef DEBUG_DCBUFFER
	    assert(DCB_test_llm_main(buff));
#endif
	if(!first_run) {
	    while(DCB_get_next_gap(buff, gap_req, &dc)) {
		assert(dc.len <= buff->ver_size);
		v2printf("gap at %lu:%lu size %lu\n", dc.offset, dc.offset + 
		    dc.len, dc.len);
		gap_total_len += dc.len;
	    }
	    if(gap_total_len == 0) {
		v1printf("not worth taking this pass, skipping to next.\n");
#ifdef DEBUG_DCBUFFER
		assert(DCB_test_llm_main(buff));
#endif
		continue;
	    }
	    hash_size= max_hash_size;
	    //hash_size = MIN(max_hash_size, gap_total_len);
	    sample_rate = COMPUTE_SAMPLE_RATE(hash_size, gap_total_len);
	    v1printf("using hash_size(%lu), sample_rate(%lu)\n", 
		hash_size, sample_rate);
	    err = init_RefHash(&rhash, ref_cfh, seed_len, sample_rate, 
		hash_size, RH_RBUCKET_HASH);
	    if(err)
		return err;
	    DCBufferReset(buff);
	    v1printf("building hash array out of total_gap(%lu)\n",
		gap_total_len);
	    while(DCB_get_next_gap(buff, gap_req, &dc)) {
		RHash_insert_block(&rhash, ver_cfh, dc.offset, dc.len + 
		    dc.offset);
	    }
	    RHash_sort(&rhash);
	    v1printf("looking for matches in reference file\n");
	    err=RHash_find_matches(&rhash, ref_cfh);
	    if(err) {
		eprintf("error detected\n");
		return err;
	    }
	    v1printf("cleansing hash, to speed bsearch's\n");
	    RHash_cleanse(&rhash);
	    print_RefHash_stats(&rhash);
	    v1printf("beginning gap processing...\n");
	    DCBufferReset(buff);
	    while(DCB_get_next_gap(buff, gap_req, &dc)) {
		v2printf("handling gap %lu:%lu, size %lu\n", dc.offset, 
		    dc.offset + dc.len, dc.len);
		err=copen(&ver_window, ver_cfh->raw_fh, dc.offset, dc.len + 
		    dc.offset, NO_COMPRESSOR, CFILE_RONLY);
		if(err)
		    return err;
	        err = DCB_llm_init_buff(buff, 128);
		if(err)
		    return err;
	        err = OneHalfPassCorrecting2(buff, &rhash, ref_id, &ver_window, ver_id);
		if(err)
		    return err;
	        err = DCB_insert(buff);
		if(err)
		    return err;
	        cclose(&ver_window);
	    }
	} else {
	    first_run=0;
	    DCBufferReset(buff);
	    v1printf("first run\n");
	    hash_size = MIN(max_hash_size, cfile_len(ref_cfh));
	    sample_rate = COMPUTE_SAMPLE_RATE(hash_size, cfile_len(ref_cfh));
	    v1printf("using hash_size(%lu), sample_rate(%lu)\n", 
		hash_size, sample_rate);
	    err = init_RefHash(&rhash, ref_cfh, seed_len, sample_rate, 
		hash_size, RH_BUCKET_HASH);
	    if(err)
		return err;
	    err = RHash_insert_block(&rhash, ref_cfh, 0L, cfile_len(ref_cfh));
	    if(err)
		return err;
	    print_RefHash_stats(&rhash);
	    v1printf("making initial run...\n");
	    err = DCB_llm_init_buff(buff, 128);
	    if(err)
		return err;
	    err = OneHalfPassCorrecting2(buff, &rhash, ref_id, ver_cfh, ver_id);
	    if(err)
		return err;
	    err = DCB_insert(buff);
	    if(err)
		return err;
	}
	RHash_sort(&rhash);

#ifdef DEBUG_DCBUFFER
	assert(DCB_test_llm_main(buff));
#endif
	free_RefHash(&rhash);
    }
    return 0;
}

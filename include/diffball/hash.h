// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2015 Brian Harring <ferringb@gmail.com>
#ifndef _HEADER_HASH
#define _HEADER_HASH 1
#include <cfile.h>
#include <diffball/adler32.h>
#include <diffball/defs.h>

#define DEFAULT_MAX_HASH_COUNT (192000000 / sizeof(unsigned long))
#define RHASH_INDEX_MASK (0xffff)
#define DEFAULT_RHASH_SIZE (0x10000)
#define MIN_RHASH_SIZE (0x10000)
#define DEFAULT_RHASH_BUCKET_SIZE (0x400)
#define RH_BUCKET_HASH (0x20)
#define RH_RBUCKET_HASH (0x40)

#define RH_BUCKET_MIN_ALLOC (16)
#define RH_BUCKET_REALLOC_RATE (2)
#define SUCCESSFULL_HASH_INSERT_NOW_IS_FULL 2
#define SUCCESSFULL_HASH_INSERT 1
#define FAILED_HASH_INSERT 0

#define RH_IS_RLOOKUP_HASH(rh) \
	((rh)->type & (RH_RBUCKET_HASH | RH_RSORT_HASH | RH_RMOD_HASH))

#define RH_FINALIZED (0x1)
#define RH_SORTED (0x2)
#define RH_IS_REVLOOKUP (0x4)

typedef struct
{
	unsigned long chksum;
	off_u64 offset;
} chksum_ent;

typedef struct ll_chksum_ent ll_chksum_ent;
struct ll_chksum_ent
{
	chksum_ent ent;
	ll_chksum_ent *next;
};

typedef struct
{
	unsigned char *depth;
	unsigned short **chksum;
	off_u64 **offset;
	unsigned short max_depth;
} bucket;

typedef struct _RefHash *RefHash_ptr;

typedef signed int (*hash_insert_func)(RefHash_ptr, ADLER32_SEED_CTX *, off_u64);
typedef void (*free_hash_func)(RefHash_ptr);
typedef signed int (*cleanse_hash_func)(RefHash_ptr);
typedef cleanse_hash_func sort_hash_func;
typedef void (*reverse_lookups_hash_func)(RefHash_ptr, cfile *);
typedef off_u64 (*hash_lookup_offset_func)(RefHash_ptr, ADLER32_SEED_CTX *);

typedef struct _RefHash
{
	unsigned int seed_len;
	unsigned long hr_size;
	unsigned char type;
	unsigned char flags;
	hash_insert_func hash_insert;
	hash_insert_func insert_match;
	free_hash_func free_hash;
	cleanse_hash_func cleanse_hash;
	hash_lookup_offset_func lookup_offset;
	void *hash;
	unsigned int sample_rate;
	cfile *ref_cfh;
	unsigned long inserts;
	unsigned long duplicates;
} RefHash;

#define FIND_NEAREST_PRIME_HR(hr_size) \
	PRIME_CTX pctx;                    \
	init_primes(pctx);

signed int
init_RefHash(RefHash *rhash, cfile *ref_cfh,
			 unsigned int seed_len, unsigned int sample_rate,
			 unsigned long hr_size, unsigned int hash_type);

signed int
RHash_insert_block(RefHash *rhash, cfile *ref_cfh, off_u64 ref_start, off_u64 ref_end);

signed int
RHash_find_matches(RefHash *rhash, cfile *ref_cfh, off_u64 ref_start, off_u64 ref_end);

signed int RHash_cleanse(RefHash *rhash);
signed int free_RefHash(RefHash *rhash);
void print_RefHash_stats(RefHash *rhash);

signed int
RH_bucket_resize(bucket *hash, unsigned long index, unsigned short size);

//hash type initializations.
void common_init_RefHash(RefHash *, cfile *, unsigned int, unsigned int, unsigned int, hash_insert_func hif, free_hash_func fhf,
						 hash_lookup_offset_func hlof);

signed int
rh_rbucket_hash_init(RefHash *rhash, cfile *ref_cfh, unsigned int seed_len, unsigned int sample_rate, unsigned long hr_size);

signed int
rh_bucket_hash_init(RefHash *rhash, cfile *ref_cfh, unsigned int seed_len, unsigned int sample_rate, unsigned long hr_size);

#define lookup_offset(rh, ads) (rh)->lookup_offset((rh), (ads))

#endif

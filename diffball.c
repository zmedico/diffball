// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2015 Brian Harring <ferringb@gmail.com>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "tar.h"
#include <cfile.h>
#include <diffball/dcbuffer.h>
#include <diffball/hash.h>
#include <diffball/diff-algs.h>
#include <diffball/formats.h>
#include <diffball/defs.h>
#include <diffball/errors.h>
#include "options.h"

unsigned int src_common_len = 0, trg_common_len = 0;

int cmp_ver_tar_ent_to_src_tar_ent(const void *te1, const void *te2);
int cmp_tar_ents(const void *te1, const void *te2);

int error;
unsigned int patch_compressor = 0;

int main(int argc, char **argv)
{
	int out_fh;
	tar_entry *source = NULL;
	tar_entry **src_ptrs = NULL;
	tar_entry *target = NULL;
	tar_entry *tar_ptr = NULL;
	unsigned int output_to_stdout = 0;
	char *patch_format = NULL;
	void *vptr;
	signed err;
	signed int ref_id, ver_id;
	unsigned long source_count, target_count;
	signed long encode_result = 0;
	unsigned long x, patch_format_id;
	char src_common[512], trg_common[512], *p; /* common dir's... */
	long sample_rate = 0, seed_len = 0, hash_size = 0;

	cfile ref_full, ref_window, ver_window, ver_full, out_cfh;
	memset(&ref_full, 0, sizeof(cfile));
	memset(&ref_window, 0, sizeof(cfile));
	memset(&ver_window, 0, sizeof(cfile));
	memset(&ver_full, 0, sizeof(cfile));
	memset(&out_cfh, 0, sizeof(cfile));

	struct stat ref_stat, ver_stat;
	RefHash rhash_win;
	CommandBuffer dcbuff;

	int optr;
	char *src_file = NULL;
	char *trg_file = NULL;
	char *patch_name = NULL;

	static struct option long_opts[] = {
		STD_LONG_OPTIONS,
		DIFF_LONG_OPTIONS,
		FORMAT_LONG_OPTION("patch-format", 'f'),
		END_LONG_OPTS};

	static struct usage_options help_opts[] = {
		STD_HELP_OPTIONS,
		DIFF_HELP_OPTIONS,
		FORMAT_HELP_OPTION("patch-format", 'f', "specify the generated patches format"),
		USAGE_FLUFF("Diffball expects normally 3 args- the source file, the target file,\n"
					"and the name for the new patch.  If it's told to output to stdout, it will- in which\n"
					"case only 2 non-options arguements are allowed.\n"
					"Example usage: diffball linux-2.6.8.tar linux-2.6.9.tar linux-2.6.8-2.6.9.patch"),
		END_HELP_OPTS};

#define DUMP_USAGE(exit_code) \
	print_usage("diffball", "src_file trg_file [patch_file|or to stdout]", help_opts, exit_code)
	char short_opts[] = STD_SHORT_OPTIONS DIFF_SHORT_OPTIONS "f:";

	while ((optr = getopt_long(argc, argv, short_opts, long_opts, NULL)) != -1)
	{
		switch (optr)
		{
			OPTIONS_COMMON_ARGUMENTS("diffball");

		// this should be renamed to '-t' for --target
		case 'f':
			patch_format = optarg;
			break;
		case OSTDOUT:
			output_to_stdout = 1;
			break;
		case OSAMPLE:
			sample_rate = atol(optarg);
			if (sample_rate == 0 || sample_rate > MAX_SAMPLE_RATE)
				DUMP_USAGE(EXIT_USAGE);
			break;
		case OHASH:
			hash_size = atol(optarg);
			if (hash_size == 0 || hash_size > MAX_HASH_SIZE)
				DUMP_USAGE(EXIT_USAGE);
			break;
		case OSEED:
			seed_len = atol(optarg);
			if (seed_len == 0 || seed_len > MAX_SEED_LEN)
				DUMP_USAGE(EXIT_USAGE);
			break;
		default:
			dcb_lprintf(0, "invalid arg- %s\n", argv[optind]);
			DUMP_USAGE(EXIT_USAGE);
		}
	}
	if (((src_file = (char *)get_next_arg(argc, argv)) == NULL) ||
		(stat(src_file, &ref_stat)))
	{
		if (src_file)
		{
			dcb_lprintf(0, "%s not found!\n", src_file);
			exit(EXIT_USAGE);
		}
		DUMP_USAGE(EXIT_USAGE);
	}
	if (((trg_file = (char *)get_next_arg(argc, argv)) == NULL) ||
		(stat(trg_file, &ver_stat)))
	{
		if (trg_file)
		{
			dcb_lprintf(0, "%s not found!\n", trg_file);
			exit(EXIT_USAGE);
		}
		DUMP_USAGE(EXIT_USAGE);
	}
	if (patch_format == NULL)
	{
		patch_format_id = DEFAULT_PATCH_ID;
	}
	else
	{
		patch_format_id = check_for_format(patch_format, strlen(patch_format));
		if (patch_format_id == 0)
		{
			dcb_lprintf(0, "Unknown format '%s'\n", patch_format);
			exit(EXIT_USAGE);
		}
	}
	if (output_to_stdout != 0)
	{
		out_fh = 1;
	}
	else
	{
		if ((patch_name = (char *)get_next_arg(argc, argv)) == NULL)
			DUMP_USAGE(EXIT_USAGE);
		if ((out_fh = open(patch_name, O_WRONLY | O_TRUNC | O_CREAT, 0644)) == -1)
		{
			dcb_lprintf(0, "error creating patch file (open failed)\n");
			exit(1);
		}
	}
	if (NULL != get_next_arg(argc, argv))
	{
		DUMP_USAGE(EXIT_USAGE);
	}
	if (hash_size == 0)
	{
		hash_size = MIN(DEFAULT_MAX_HASH_COUNT, ref_stat.st_size);
	}

	if (copen_path(&ref_full, src_file, NO_COMPRESSOR, CFILE_RONLY) ||
		copen_path(&ver_full, trg_file, NO_COMPRESSOR, CFILE_RONLY))
	{
		dcb_lprintf(0, "error opening file; exiting\n");
		exit(1);
	}

	if (seed_len == 0)
	{
		seed_len = DEFAULT_SEED_LEN;
	}
	if (sample_rate == 0)
	{
		sample_rate = COMPUTE_SAMPLE_RATE(hash_size, cfile_len(&ref_full), seed_len);
	}

	dcb_lprintf(1, "using patch format %lu\n", patch_format_id);
	dcb_lprintf(1, "using seed_len(%lu), sample_rate(%lu), hash_size(%lu)\n",
				seed_len, sample_rate, hash_size);
	dcb_lprintf(1, "dcb verbosity level(%u)\n", diffball_get_logging_level());
	dcb_lprintf(1, "cfile logging level(%u)\n", cfile_get_logging_level());

	dcb_lprintf(1, "reading tar entries from src\n");
	if (read_fh_to_tar_entry(&ref_full, &source, &source_count))
		exit(EXIT_FAILURE);

	dcb_lprintf(2, "reading tar entries from trg\n");
	if (read_fh_to_tar_entry(&ver_full, &target, &target_count))
		exit(EXIT_FAILURE);

	dcb_lprintf(2, "source tarball's entry count=%lu\n", source_count);
	dcb_lprintf(2, "target tarball's entry count=%lu\n", target_count);

	dcb_lprintf(3, "qsorting\n");
	src_ptrs = (tar_entry **)malloc(sizeof(tar_entry *) * source_count);
	if (src_ptrs == NULL)
	{
		dcb_lprintf(0, "unable to allocate needed memory, bailing\n");
		exit(EXIT_FAILURE);
	}
	for (x = 0; x < source_count; x++)
		src_ptrs[x] = source + x;
	qsort(src_ptrs, source_count, sizeof(tar_entry *), cmp_tar_ents);
	dcb_lprintf(3, "qsort done\n");

	/* alg to basically figure out the common dir prefix... eg, if everything 
   is in dir debianutils-1.16.3; note, we want the slash, hence +1 */

	p = rindex((const char *)src_ptrs[0]->fullname, '/');
	if (p != NULL)
	{
		src_common_len = ((char *)p - (char *)src_ptrs[0]->fullname) + 1;
		strncpy((char *)src_common, (char *)src_ptrs[0]->fullname,
				src_common_len);
	}
	else
	{
		src_common_len = 0;
	}
	src_common[src_common_len] = '\0'; /*null delimit it */

	for (x = 0; x < source_count && src_common_len != 0; x++)
	{
		if (strncmp((const char *)src_common, (const char *)src_ptrs[x]->fullname, src_common_len) != 0)
		{
			char *p;

			/* null the / at src_common_len-1, and attempt rindex again. */

			src_common[src_common_len - 1] = '\0';
			if ((p = rindex(src_common, '/')) == NULL)
			{
				/*no common dir prefix. damn. */
				src_common_len = 0;
				src_common[0] = '\0';
			}
			else
			{
				/*include the / in the path again... */
				src_common_len = p - src_common + 1;
				src_common[src_common_len] = '\0';
			}
		}
	}
	dcb_lprintf(1, "final src_common='%.*s'\n", src_common_len, src_common);
	p = rindex((const char *)target[0].fullname, '/');
	if (p != NULL)
	{
		trg_common_len = ((char *)p - (char *)target[0].fullname) + 1;
		strncpy((char *)trg_common, (char *)target[0].fullname, trg_common_len);
	}
	else
	{
		trg_common_len = 0;
	}
	trg_common[trg_common_len] = '\0'; /* null delimit it */

	for (x = 0; x < target_count && trg_common_len != 0; x++)
	{
		if (strncmp((const char *)trg_common, (const char *)target[x].fullname,
					trg_common_len) != 0)
		{

			/* null the / at trg_common_len-1, and attempt rindex again. */

			trg_common[trg_common_len - 1] = '\0';
			if ((p = rindex(trg_common, '/')) == NULL)
			{
				trg_common_len = 0;
				trg_common[0] = '\0'; /*no common dir prefix. damn. */
			}
			else
			{
				trg_common_len = p - trg_common + 1; /*include the / again... */
				trg_common[trg_common_len] = '\0';
			}
		}
	}
	dcb_lprintf(1, "final trg_common='%.*s'\n", trg_common_len, trg_common);

	err = DCB_llm_init(&dcbuff, 4096, (unsigned long)ref_stat.st_size,
					   (unsigned long)ver_stat.st_size) ||
		  DCB_llm_init_buff(&dcbuff, 4096);

	check_return2(err, "DCBufferInit");

	dcb_lprintf(1, "looking for matching filenames in the archives...\n");

	ver_id = DCB_REGISTER_ADD_SRC(&dcbuff, &ver_full, NULL, 0);
	if (ver_id < 0)
	{
		check_return(ver_id, "DCB_REGISTER_ADD_SRC", "failed to register file handle");
	}

	ref_id = DCB_REGISTER_COPY_SRC(&dcbuff, &ref_full, NULL, 0);
	if (ref_id < 0)
	{
		check_return(ref_id, "DCB_REGISTER_COPY_SRC", "failed to register file handle");
	}

	for (x = 0; x < target_count; x++)
	{
		dcb_lprintf(1, "processing %lu of %lu\n", x + 1, target_count);
		tar_ptr = &target[x];
		vptr = bsearch(&tar_ptr, src_ptrs,
					   source_count, sizeof(tar_entry **), cmp_ver_tar_ent_to_src_tar_ent);
		if (vptr == NULL)
		{
			dcb_lprintf(1, "didn't find a match for %.255s, skipping\n",
						target[x].fullname);
		}
		else
		{
			tar_ptr = (tar_entry *)*((tar_entry **)vptr);
			dcb_lprintf(1, "found match between %.255s and %.255s\n", target[x].fullname,
						tar_ptr->fullname);
			dcb_lprintf(2, "differencing src(%llu:%llu) against trg(%llu:%llu)\n",
						(act_off_u64)tar_ptr->start, (act_off_u64)tar_ptr->end,
						(act_off_u64)target[x].start, (act_off_u64)target[x].end);

			copen_child_cfh(&ver_window, &ver_full, target[x].start, target[x].end,
							NO_COMPRESSOR, CFILE_RONLY | CFILE_BUFFER_ALL);

			copen_child_cfh(&ref_window, &ref_full, tar_ptr->start, tar_ptr->end,
							NO_COMPRESSOR, CFILE_RONLY | CFILE_BUFFER_ALL);

			err = rh_bucket_hash_init(&rhash_win, &ref_window, 24, 1, 0);
			check_return2(err, "init_RefHash");
			err = RHash_insert_block(&rhash_win, &ref_window, 0,
									 cfile_len(&ref_window));
			check_return2(err, "RHash_insert_block");
			err = RHash_cleanse(&rhash_win);
			check_return2(err, "RHash_cleanse");
			print_RefHash_stats(&rhash_win);
			err = OneHalfPassCorrecting(&dcbuff, &rhash_win, ref_id, &ver_window, ver_id);

			if (err)
			{
				/* not a graceful exit I realize... */
				dcb_lprintf(0, "OneHalfPassCorrecting returned an error process file %.255s and %.255s\n",
							target[x].fullname, tar_ptr->fullname);
				dcb_lprintf(0, "Quite likely this is a bug in diffball; error's should not occur at this point, exempting out of memory errors\n");
				dcb_lprintf(0, "please contact the author so this can be resolved.\n");
				check_return2(err, "OneHalfPassCorrecting");
			}
			err = free_RefHash(&rhash_win);
			check_return(err, "free_RefHash", "This shouldn't be happening...");
			cclose(&ver_window);
			cclose(&ref_window);
		}
	}

	/* cleanup */
	for (x = 0; x < source_count; x++)
		free(source[x].fullname);
	free(source);
	free(src_ptrs);

	for (x = 0; x < target_count; x++)
		free(target[x].fullname);
	free(target);

	dcb_lprintf(1, "beginning search for gaps, and unprocessed files\n");
	err = MultiPassAlg(&dcbuff, &ref_full, ref_id, &ver_full, ver_id, hash_size, 512);
	check_return(err, "MultiPassAlg", "final multipass run failed");
	err = DCB_finalize(&dcbuff);
	check_return2(err, "DCB_finalize");
	cclose(&ref_full);

	copen_dup_fd(&out_cfh, out_fh, 0, 0, NO_COMPRESSOR, CFILE_WONLY | CFILE_OPEN_FH);
	dcb_lprintf(1, "outputing patch...\n");
	if (GDIFF4_FORMAT == patch_format_id)
	{
		encode_result = gdiff4EncodeDCBuffer(&dcbuff, &out_cfh);
	}
	else if (GDIFF5_FORMAT == patch_format_id)
	{
		encode_result = gdiff5EncodeDCBuffer(&dcbuff, &out_cfh);
	}
	else if (BDIFF_FORMAT == patch_format_id)
	{
		encode_result = bdiffEncodeDCBuffer(&dcbuff, &out_cfh);
	}
	else if (SWITCHING_FORMAT == patch_format_id)
	{
		encode_result = switchingEncodeDCBuffer(&dcbuff, &out_cfh);
	}
	else if (BDELTA_FORMAT == patch_format_id)
	{
		encode_result = bdeltaEncodeDCBuffer(&dcbuff, &out_cfh);
	}
	dcb_lprintf(1, "encoding result was %ld\n", encode_result);
	DCBufferFree(&dcbuff);
	cclose(&ver_full);
	cclose(&out_cfh);
	close(out_fh);
	return 0;
}

int cmp_ver_tar_ent_to_src_tar_ent(const void *te1, const void *te2)
{
	return strcmp((const char *)(*((tar_entry **)te1))->fullname + trg_common_len,
				  (const char *)(*((tar_entry **)te2))->fullname + src_common_len);
}

int cmp_tar_ents(const void *te1, const void *te2)
{
	return strcmp(
		(const char *)(*((tar_entry **)te1))->fullname,
		(const char *)(*((tar_entry **)te2))->fullname);
}

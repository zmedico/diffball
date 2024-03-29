// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2005 Brian Harring <ferringb@gmail.com>
#ifndef _HEADER_OPTIONS
#define _HEADER_OPTIONS 1

#include <getopt.h>

//move this. but to where?
#define EXIT_USAGE -2
struct usage_options
{
	char short_arg;
	char *long_arg;
	char *description;
};

#define USAGE_FLUFF(fluff) \
	{                      \
		0, 0, fluff        \
	}

#define OVERSION 'V'
#define OVERBOSE 'v'
#define CFILE_OVERBOSE 1000
#define OUSAGE 'u'
#define OHELP 'h'
#define OSEED 'b'
#define OSAMPLE 's'
#define OHASH 'a'
#define OSTDOUT 'c'
#define OBZIP2 'j'
#define OGZIP 'z'

#define DIFF_SHORT_OPTIONS \
	"b:s:a:"

#define DIFF_LONG_OPTIONS               \
	{"seed-len", 1, 0, OSEED},          \
		{"sample-rate", 1, 0, OSAMPLE}, \
	{                                   \
		"hash-size", 1, 0, OHASH        \
	}

#define DIFF_HELP_OPTIONS                                \
	{OSEED, "seed-len", "set the seed len"},             \
		{OSAMPLE, "sample-rate", "set the sample rate"}, \
	{                                                    \
		OHASH, "hash-size", "set the hash size"          \
	}

#define STD_SHORT_OPTIONS \
	"Vvcuh"

#define STD_LONG_OPTIONS                         \
	{"version", 0, 0, OVERSION},                 \
		{"verbose", 0, 0, OVERBOSE},             \
		{"cfile-verbose", 0, 0, CFILE_OVERBOSE}, \
		{"to-stdout", 0, 0, OSTDOUT},            \
		{"usage", 0, 0, OUSAGE},                 \
	{                                            \
		"help", 0, 0, OHELP                      \
	}

#define STD_HELP_OPTIONS                             \
	{OVERSION, "version", "print version"},          \
		{OVERBOSE, "verbose", "increase verbosity"}, \
		{OSTDOUT, "to-stdout", "output to stdout"},  \
		{OUSAGE, "usage", "give this help"},         \
	{                                                \
		OHELP, "help", "give this help"              \
	}

//note no FORMAT_SHORT_OPTION

#define FORMAT_LONG_OPTION(long, short) \
	{                                   \
		long, 1, 0, short               \
	}

#define FORMAT_HELP_OPTION(long, short, description) \
	{                                                \
		short, long, description                     \
	}

#define END_HELP_OPTS \
	{                 \
		0, NULL, NULL \
	}
#define END_LONG_OPTS \
	{                 \
		0, 0, 0, 0    \
	}

// refresher for those who're going wtf, optind is an external (ab)used by getopt
char *get_next_arg(int argc, char **argv);
void print_version(const char *prog);
void print_usage(const char *prog, const char *usage_portion, struct usage_options *textq, int exit_code);

// just reuse dcbuffer's logging level.
#define lprintf(level, expr...) dcb_lprintf(level, expr)

#define OPTIONS_COMMON_ARGUMENTS(program)  \
	case OVERSION:                         \
		print_version(program);            \
		exit(0);                           \
	case OUSAGE:                           \
	case OHELP:                            \
		DUMP_USAGE(0);                     \
	case OVERBOSE:                         \
		diffball_increase_logging_level(); \
		break;                             \
	case CFILE_OVERBOSE:                   \
		printf("fuckity fuck\n");          \
		cfile_increase_logging_level();    \
		break;

#define OPTIONS_COMMON_PATCH_ARGUMENTS(program) \
	OPTIONS_COMMON_ARGUMENTS(program);          \
	case 'f':                                   \
		src_format = optarg;                    \
		break;                                  \
	case OSTDOUT:                               \
		output_to_stdout = 1;                   \
		break;

#endif

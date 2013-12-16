/*
  Copyright (C) 2013 Brian Harring

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
#include <diffball/defs.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <diffball/dcbuffer.h>
#include <diffball/defs.h>
#include <cfile.h>
#include <diffball/bit-functions.h>
#include <diffball/tree.h>
#include <diffball/switching.h>
#include <diffball/formats.h>
#include <diffball/api.h>

// Used only for the temp file machinery; get rid of this at that time.
#include <unistd.h>

void enforce_no_trailing_slash(char *ptr);


static int flush_file_content_delta(CommandBuffer *dcbuff, cfile *patchf);
static int encode_fs_entry(cfile *patchf, multifile_file_data *entry);
static int enforce_standard_attributes(const char *path, const struct stat *st);
static int enforce_directory(const char *path, const struct stat *st);
static int enforce_symlink(const char *path, const char *link_target, const struct stat *st);

static int consume_command_chain(const char *target_directory, const char *tmpspace, cfile *patchf,
	multifile_file_data **ref_files, char **final_paths, unsigned long ref_count, unsigned long *ref_pos,
	unsigned long command_count);

// used with fstatat if available
#ifndef AT_NO_AUTOMOUNT
#define AT_NO_AUTOMOUNT 0
#endif

static char *
concat_path(const char *directory, const char *frag)
{
	size_t dir_len = strlen(directory);
	size_t frag_len = strlen(frag);
	char *p = malloc(dir_len + frag_len + 1);
	if (p) {
		memcpy(p, directory, dir_len);
		memcpy(p + dir_len, frag, frag_len);
		p[dir_len + frag_len] = 0;
	}
	return p;
}

static int
cWriteUBytesLE(cfile *cfh, unsigned long value, unsigned int len)
{
	unsigned char buff[16];
	writeUBytesLE(buff, value, len);
	if (len != cwrite(cfh, buff, len)) {
		v0printf("Failed writing %i bytes\n", len);
		return IO_ERROR;
	}
	return 0;
}

unsigned int
check_tree_magic(cfile *patchf)
{
	unsigned char buff[TREE_MAGIC_LEN + 1];
	cseek(patchf, 0, CSEEK_FSTART);
	if(TREE_MAGIC_LEN != cread(patchf, buff, TREE_MAGIC_LEN)) {
		return 0;
	} else if (memcmp(buff, TREE_MAGIC, TREE_MAGIC_LEN)!=0) {
		return 0;
	}
	return 2;
}

static int
flush_file_manifest(cfile *patchf, multifile_file_data **fs, unsigned long fs_count, const char *manifest_name)
{
	int err;
	// Identify and output the # of files that will be created.
	unsigned long x, file_count;
	for(file_count = 0, x = 0; x < fs_count; x++) {
		if (S_ISREG(fs[x]->st->st_mode) && !fs[x]->link_target) {
			file_count++;
		}
	}
	// The format here is 4 bytes for the # of files in this manifest, then:
	//   null delimited string
	//   8 bytes for the file size for that file.
	//
	v3printf("Recording %lu files in the delta manifest\n", file_count);
	cWriteUBytesLE(patchf, file_count, 4);
	for(x=0; file_count > 0; x++) {
		if (S_ISREG(fs[x]->st->st_mode) && !fs[x]->link_target) {

			v3printf("Recording file %s length %zi in the %s manifest\n", fs[x]->filename, fs[x]->st->st_size, manifest_name);
			size_t len = strlen(fs[x]->filename) + 1;
			if (len != cwrite(patchf, fs[x]->filename, len)) {
				v0printf("Failed writing %s file manifest\n", manifest_name);
				return IO_ERROR;
			}
			err = cWriteUBytesLE(patchf, fs[x]->st->st_size, 8);
			if (err) {
				return err;
			}
			file_count--;
		}
	}
	return 0;
}

static int
read_file_manifest(cfile *patchf, multifile_file_data ***fs, unsigned long *fs_count, const char *manifest_name)
{
	unsigned char buff[16];
	if (4 != cread(patchf, buff, 4)) {
		eprintf("Failed reading %s manifest count\n", manifest_name);
		return PATCH_TRUNCATED;
	}
	unsigned long file_count = readUBytesLE(buff, 4);
	*fs_count = file_count;
	int err = 0;
	multifile_file_data **results = calloc(sizeof(multifile_file_data *), file_count);
	if (!results) {
		eprintf("Failed allocating internal array for %s file manifest: %lu entries.\n", manifest_name, file_count);
		return MEM_ERROR;
	}
	*fs = results;
	unsigned long x;
	size_t position = 0;
	for (x = 0; x < file_count; x++) {
		results[x] = calloc(sizeof(multifile_file_data), 1);
		if (!results[x]) {
			file_count = x;
			err = MEM_ERROR;
			goto cleanup;
		}
		results[x]->filename = (char *)cfile_read_null_string(patchf);
		if (!results[x]->filename) {
			file_count = x +1;
			err = MEM_ERROR;
			goto cleanup;
		}
		results[x]->start = position;
		if (8 != cread(patchf, buff, 8)) {
			eprintf("Failed reading %s manifest count\n", manifest_name);
			file_count = x +1;
			err = PATCH_TRUNCATED;
			goto cleanup;
		}
		results[x]->st = calloc(sizeof(struct stat), 1);
		if (!results[x]->st) {
			eprintf("Failed allocating memory\n");
			err = MEM_ERROR;
			file_count = x + 1;
			goto cleanup;
		}
		results[x]->st->st_size = readUBytesLE(buff, 8);
		results[x]->st->st_mode = S_IFREG | 0600;
		position = results[x]->end = position + results[x]->st->st_size;
		v3printf("adding to %s manifest: %s length %zu\n", manifest_name, results[x]->filename, position - results[x]->start);
	}
	return 0;

	cleanup:
	for (x=0; x < file_count; x++) {
		if (results[x]->filename) {
			free(results[x]->filename);
		}
		if (results[x]->st) {
			free(results[x]->st);
		}
		free(results[x]);
	}
	free(results);
	return err;
}

signed int 
treeEncodeDCBuffer(CommandBuffer *dcbuff, cfile *patchf)
{
	int err;
	cwrite(patchf, TREE_MAGIC, TREE_MAGIC_LEN);
	cWriteUBytesLE(patchf, TREE_VERSION, TREE_VERSION_LEN);

	multifile_file_data **src_files = NULL, **ref_files = NULL;
	unsigned long src_count = 0, ref_count = 0;
	cfile *src_cfh = DCB_EXPOSE_COPY_CFH(dcbuff);
	cfile *ref_cfh = DCB_EXPOSE_ADD_CFH(dcbuff);
	if (multifile_expose_content(src_cfh, &src_files, &src_count)) {
		v0printf("Failed accessing multifile content for src\n");
		return DATA_ERROR;
	}
	if (multifile_expose_content(ref_cfh, &ref_files, &ref_count)) {
		v0printf("Failed accessing multifile content for ref\n");
		return DATA_ERROR;
	}

	// Dump the list of source files needed, then dump the list of files generated by this patch.
	err = flush_file_manifest(patchf, src_files, src_count, "source");
	if (err) {
		eprintf("Failed flushing ref files content\n");
		return err;
	}

	err = flush_file_manifest(patchf, ref_files, ref_count, "target");
	if (err) {
		eprintf("Failed flushing ref files content\n");
		return err;
	}

	v3printf("Flushed delta manifest; writing the delta now\n");
	// TODO: Move to a size estimate implementation for each encoding- use that to get the size here.

	err = flush_file_content_delta(dcbuff, patchf);
	if (err) {
		return err;
	}

	v3printf("Flushed the file content delta.  Writing magic, magic then command stream\n");
	if (TREE_INTERFILE_MAGIC_LEN != cwrite(patchf, TREE_INTERFILE_MAGIC, TREE_INTERFILE_MAGIC_LEN)) {
		v0printf("Failed flushing interfile magic\n");
		return IO_ERROR;
	}

	// Flush the command count.  It's number of ref_count entries + # of unlinks commands.
	v3printf("Flushing command count %lu\n", ref_count);
	err = cWriteUBytesLE(patchf, ref_count, 4);
	if (err) {
		return err;
	}

	unsigned long x;
	for(x=0; x < ref_count; x++) {
		err = encode_fs_entry(patchf, ref_files[x]);
		if (err) {
			return err;
		}
	}

	return 0;
}

static int
encode_fs_entry(cfile *patchf, multifile_file_data *entry)
{
	#define write_or_return(value, len) {int err=cWriteUBytesLE(patchf, (value), (len)); if (err) { return err; }; }

	#define write_common_block(st) \
		write_or_return((st)->st_uid, TREE_COMMAND_UID_LEN); \
		write_or_return((st)->st_gid, TREE_COMMAND_GID_LEN); \
		write_or_return((st)->st_mode, TREE_COMMAND_MODE_LEN); \
		write_or_return((st)->st_ctime, TREE_COMMAND_TIME_LEN); \
		write_or_return((st)->st_mtime, TREE_COMMAND_TIME_LEN);

#define write_null_string(value) \
{ int len=strlen((value)) + 1; if (len != cwrite(patchf, (value), len)) { v0printf("Failed writing string len %i\n", len); return IO_ERROR; }; }

	if (S_ISREG(entry->st->st_mode)) {
		if (!entry->link_target) {
			v3printf("writing manifest command for regular %s\n", entry->filename);
			write_or_return(TREE_COMMAND_REG, TREE_COMMAND_LEN);
			write_common_block(entry->st);
			// xattrs
			return 0;
		}
		v3printf("writing manifest command for hardlink %s\n", entry->filename);
		write_or_return(TREE_COMMAND_HARDLINK, TREE_COMMAND_LEN);
		write_null_string(entry->filename);
		write_null_string(entry->link_target);

	} else if (S_ISDIR(entry->st->st_mode)) {
		v3printf("writing manifest command for directory %s\n", entry->filename);
		write_or_return(TREE_COMMAND_DIR, TREE_COMMAND_LEN);
		write_null_string(entry->filename);
		write_common_block(entry->st);

	} else if (S_ISLNK(entry->st->st_mode)) {
		v3printf("writing manifest command for symlink %s\n", entry->filename);
		write_or_return(TREE_COMMAND_SYM, TREE_COMMAND_LEN);
		write_null_string(entry->filename);
		assert(entry->link_target);
		write_null_string(entry->link_target);
		// Note: no mode...
		write_or_return(entry->st->st_uid, TREE_COMMAND_UID_LEN);
		write_or_return(entry->st->st_gid, TREE_COMMAND_GID_LEN);
		write_or_return(entry->st->st_ctime, TREE_COMMAND_TIME_LEN);
		write_or_return(entry->st->st_mtime, TREE_COMMAND_TIME_LEN);

	} else if (S_ISFIFO(entry->st->st_mode)) {
		v3printf("writing manifest command for fifo %s\n", entry->filename);
		write_or_return(TREE_COMMAND_FIFO, TREE_COMMAND_LEN);
		write_null_string(entry->filename);
		write_common_block(entry->st);

	} else if (S_ISCHR(entry->st->st_mode) || S_ISBLK(entry->st->st_mode)) {
		v3printf("writing manifest command for dev %s\n", entry->filename);
		write_or_return(TREE_COMMAND_DEV, TREE_COMMAND_LEN);
		write_null_string(entry->filename);
		write_common_block(entry->st);

	} else {
		v0printf("Somehow encountered an unknown fs entry: %s: %i\n", entry->filename, entry->st->st_mode);
		return DATA_ERROR;
	}
	return 0;

#undef write_null_string
#undef write_common_block
}

static int
flush_file_content_delta(CommandBuffer *dcbuff, cfile *patchf)
{
	cfile deltaf;
	memset(&deltaf, 0, sizeof(cfile));
	char tmpname[] = "/tmp/differ.XXXXXX";
	unsigned char buff[16];
	int tmp_fd = mkstemp(tmpname);
	if (tmp_fd < 0) {
		v0printf("Failed getting a temp file\n");
		return IO_ERROR;
	}

	int err = copen_dup_fd(&deltaf, tmp_fd, 0, 0, NO_COMPRESSOR, CFILE_WONLY);
	if (err) {
		v0printf("Failed opening cfile handle to the tmpfile\n");
		close(tmp_fd);
		return IO_ERROR;
	}

	v3printf("Invoking switching format to encode the delta\n");
	err = switchingEncodeDCBuffer(dcbuff, &deltaf);
	if (err) {
		goto ERR_CFILE;
	}

	err = cclose(&deltaf);
	if (err) {
		v0printf("Failed closing deltaf handle\n");
		goto ERR_FD;
	}
	struct stat st;
	fstat(tmp_fd, &st);

	v3printf("File content delta is %lu bytes\n", st.st_size);
	if (cWriteUBytesLE(patchf, st.st_size, 8)) {
		v0printf("Failed writing delta length\n");
		err = IO_ERROR;
		goto ERR_FD;
	}

	err = copen_dup_fd(&deltaf, tmp_fd, 0, st.st_size, NO_COMPRESSOR, CFILE_RONLY);
	if (err) {
		v0printf("Failed reopening the tmp handle for reading\n");
		goto ERR_FD;
	}

	v3printf("Flushing delta to the patch\n");
	if (st.st_size != copy_cfile_block(patchf, &deltaf, 0, st.st_size)) {
		v0printf("Failed flushing in memory delta to the patch file\n");
		err = IO_ERROR;
		goto ERR_CFILE;
	}
	err = 0;

	ERR_CFILE:
		cclose(&deltaf);
	ERR_FD:
		unlink(tmpname);
		close(tmp_fd);
	return err;
}

static int
enforce_recursive_unlink(DIR *directory)
{
	struct dirent *entry;
	int err = 0;
	while ((entry = readdir(directory))) {
		if (0 == strcmp(entry->d_name, ".") || 0 == strcmp(entry->d_name, "..")) {
			continue;
		}
		int is_dir = 0;
		if (entry->d_type == DT_DIR) {
			is_dir = 1;
		} else if (entry->d_type == DT_UNKNOWN) {
			struct stat st;
			if (fstatat(dirfd(directory), entry->d_name, &st, AT_NO_AUTOMOUNT|AT_SYMLINK_NOFOLLOW)) {
				closedir(directory);
				return IO_ERROR;
			}
			is_dir = S_ISDIR(st.st_mode);
		}
		if (is_dir) {
			int fd = openat(dirfd(directory), entry->d_name, O_DIRECTORY);
			if (fd >= 0) {
				DIR *subdir = fdopendir(fd);
				if (subdir) {
					err = enforce_recursive_unlink(subdir);
					closedir(subdir);
					if (!err) {
						err = unlinkat(dirfd(directory), entry->d_name, AT_REMOVEDIR);
					}
				} else {
					close(fd);
					err = IO_ERROR;
				}
			} else {
				err = IO_ERROR;
			}
		} else {
			err = unlinkat(dirfd(directory), entry->d_name, 0);
		}

		if (err) {
			break;
		}
	}
	return err;
}

static int
enforce_unlink(const char *path)
{
	v3printf("Removing content at %s\n", path);
	int err = unlink(path);
	if (-1 == err) {
		if (ENOENT == errno) {
			err = 0;
			errno = 0;
		} else if (EISDIR == errno) {
			DIR *directory = opendir(path);
			if (directory) {
				err = enforce_recursive_unlink(directory);
				closedir(directory);
				if (!err) {
					err = rmdir(path);
				}
			}
		}
	}
	return err;
}

static int
enforce_standard_attributes(const char *path, const struct stat *st)
{
	int err = lchown(path, st->st_uid, st->st_gid);
	if (!err) {
		struct timeval times[2] = {{st->st_ctime, 0}, {st->st_mtime, 0}};
		err = lutimes(path, times);
	}
	return err;
}

static int
enforce_directory(const char *path, const struct stat *st)
{
	v3printf("Creating directory at %s\n", path);
	int err = mkdir(path, st->st_mode);
	if (-1 == err) {
		if (EEXIST == errno) {
			struct stat ondisk_st;
			if (0 != lstat(path, &ondisk_st)) {
				eprintf("Race occurred checking %s: errno %i\n", path, errno);
				return IO_ERROR;
			} else if (!S_ISDIR(ondisk_st.st_mode)) {
				v3printf("Removing blocking content at %s\n", path);
				err = unlink(path);
				if (!err) {
					err = mkdir(path, st->st_mode);
				}
			} else {
				err = 0;
			}
		} else if (ENOTDIR == errno) {
			err = unlink(path);
			if (!err) {
				err = mkdir(path, st->st_mode);
			}
		}
	}

	if (!err) {
		// Note, this doesn't guarantee mtime if we go screwing around w/in a directory after the command.
		// Need to track/sort that somehow.
		err = enforce_standard_attributes(path, st);
	}
	return err;
}

static int
enforce_symlink(const char *path, const char *link_target, const struct stat *st)
{
	v3printf("Creating symlink at %s\n", path);
	int err = symlink(link_target, path);
	if (-1 == err && EEXIST == errno) {
		v3printf("Removing blocking content at %s\n", path);
		err = enforce_unlink(path);
		if (!err) {
			err = symlink(link_target, path);
		}
	}

	if (!err) {
		err = enforce_standard_attributes(path, st);
	}
	return err;
}

static int
enforce_file_move(const char *trg, const char *src, const struct stat *st)
{
	v3printf("Transferring reconstructed file %s to %s\n", src, trg);
	int err = rename(src, trg);
	if (!err) {
		err = enforce_standard_attributes(trg, st);
	}
	return err;
}

static int
enforce_hardlink(const char *path, const char *link_target)
{
	int err = link(path, link_target);
	if (-1 == err && EEXIST == errno) {
		errno = 0;
		err = enforce_unlink(path);
		if (!err) {
			int err = link(path, link_target);
		}
	}
	return err;
}

static int
enforce_trailing_slash(char **ptr)
{
	size_t len = strlen(*ptr);
	if (len == 0 || (*ptr)[len -1] != '/') {
		char *p = realloc(*ptr, len + 2);
		if (!p) {
			eprintf("Somehow encountered realloc failure for string of desired size %zi\n", len + 2);
			return MEM_ERROR;
		}
		(*ptr)[len] = '/';
		(*ptr)[len + 1] = 0;
		*ptr = p;
		return 0;
	}
	return 0;
}

void
enforce_no_trailing_slash(char *ptr)
{
	size_t len = strlen(ptr);
	while (len && ptr[len -1] == '/') {
		ptr[len -1] = 0;
		len--;
	}
}

static int
consume_command_chain(const char *target_directory, const char *tmpspace, cfile *patchf,
    multifile_file_data **ref_files, char **final_paths, unsigned long ref_count, unsigned long *ref_pos,
    unsigned long command_count)
{
	int err = 0;
	struct stat st;
	char *filename = NULL;
	char *abs_filepath = NULL;
	char *link_target = NULL;
	unsigned char buff[8];

	// This code assumes tree commands are a single byte; assert to catch if that ever changes.
	assert (TREE_COMMAND_LEN == 1);
	if(patchf->data.pos == patchf->data.end) {
		if(crefill(patchf) <= 0) {
			eprintf("Failed reading command %lu\n", command_count);
			return PATCH_TRUNCATED;
		}
	}
	#define read_string_or_return(value) \
		{ (value) = (char *)cfile_read_null_string(patchf); if (!(value)) { eprintf("Failed reading null string\n"); return PATCH_TRUNCATED; }; };

	#define read_or_return(value, len) \
		{ \
			if ((len) != cread(patchf, buff, (len))) { eprintf("Failed reading %i bytes\n", (len)); return PATCH_TRUNCATED; }; \
			(value) = readUBytesLE(buff, (len)); \
		}

	#define read_common_block(st) \
		read_or_return((st)->st_uid, TREE_COMMAND_UID_LEN); \
		read_or_return((st)->st_gid, TREE_COMMAND_GID_LEN); \
		read_or_return((st)->st_mode, TREE_COMMAND_MODE_LEN); \
		read_or_return((st)->st_ctime, TREE_COMMAND_TIME_LEN); \
		read_or_return((st)->st_mtime, TREE_COMMAND_TIME_LEN);

	#define enforce_or_fail(command, args...) \
		{ \
			abs_filepath = concat_path(target_directory, filename); \
			if (abs_filepath) { \
				err = command(abs_filepath, args); \
			} else { \
				eprintf("Failed allocating filepath.\n"); \
				err = MEM_ERROR; \
			} \
		}


	unsigned char command_type = patchf->data.buff[patchf->data.pos];
	patchf->data.pos++;
	switch (command_type) {
		case TREE_COMMAND_REG:
			v3printf("command %lu: regular file\n", command_count);
			if ((*ref_pos) == ref_count) {
				eprintf("Encountered a file command, but no more recontruction targets were defined by this patch.  Likely corruption or internal bug\n");
				return PATCH_CORRUPT_ERROR;
			}
			read_common_block(&st);
			char *src = concat_path(tmpspace, ref_files[*ref_pos]->filename);
			if (src) {
				enforce_or_fail(enforce_file_move, src, &st);
				free(src);
			} else {
				eprintf("Failed allocating memory for link target\n");
				err = MEM_ERROR;
			}

			(*ref_pos)++;
			break;

		case TREE_COMMAND_HARDLINK:
			v3printf("command %lu: hardlink\n", command_count);
			read_string_or_return(filename);
			read_string_or_return(link_target);
			char *abs_link_target = concat_path(target_directory, link_target);
			if (abs_link_target) {
				enforce_or_fail(enforce_hardlink, abs_link_target);
				free(abs_link_target);
			} else {
				eprintf("Failed allocating memory for link target\n");
				err = MEM_ERROR;
			}
			break;

		case TREE_COMMAND_DIR:
			v3printf("command %lu: create directory\n", command_count);
			read_string_or_return(filename);
			// Strip the trailing slash; if our enforce_directory needs to
			// resort to lstating, a trailing '/' results in the call incorrectly
			// failing w/ ENOTDIR .
			enforce_no_trailing_slash(filename);
			read_common_block(&st);
			enforce_or_fail(enforce_directory, &st);
			break;

		case TREE_COMMAND_SYM:
			v3printf("command %lu: create symlink\n", command_count);
			read_string_or_return(filename);
			read_string_or_return(link_target);

			// Note- no mode.
			read_or_return(st.st_uid, TREE_COMMAND_UID_LEN);
			read_or_return(st.st_gid, TREE_COMMAND_GID_LEN);
			read_or_return(st.st_ctime, TREE_COMMAND_TIME_LEN);
			read_or_return(st.st_mtime, TREE_COMMAND_TIME_LEN);

			enforce_or_fail(enforce_symlink, link_target, &st);
			break;

		case TREE_COMMAND_FIFO:
			v3printf("command %lu: create fifo\n", command_count);
			read_string_or_return(filename);
			read_common_block(&st);
			break;

		case TREE_COMMAND_DEV:
			v3printf("command %lu: mknod dev\n", command_count);
			read_string_or_return(filename);
			read_common_block(&st);
			break;

		case TREE_COMMAND_UNLINK:
			v3printf("command %lu: unlink\n", command_count);
			read_string_or_return(filename);

			abs_filepath = concat_path(target_directory, filename);
			if (abs_filepath) {
				err = enforce_unlink(abs_filepath);
			} else {
				eprintf("Failed allocating filepath.\n");
				err = MEM_ERROR;
			}

			break;

		default:
			eprintf("command %lu: unknown command: %i\n", command_count, patchf->data.buff[patchf->data.pos]);
			return PATCH_CORRUPT_ERROR;
	}

	if (abs_filepath) {
		free(abs_filepath);
	}

	if (filename) {
		free(filename);
	}

	if (link_target) {
		free(link_target);
	}

	return err;

	#undef enforce_or_fail
	#undef read_or_return
	#undef read_string_or_return
	#undef read_common_block
}

static char *
make_tempdir(const char *tmp_directory)
{
	if (NULL == tmp_directory) {
		tmp_directory = getenv("TMPDIR");
		if (!tmp_directory) {
			tmp_directory = "/tmp";
		}
	}
	const size_t tmp_len = strlen(tmp_directory);

	const char template_frag[] = "delta-XXXXXX";
	const size_t template_frag_len = strlen(template_frag);

	char *template = malloc(tmp_len + 3 + template_frag_len);
	if (!template) {
		return NULL;
	}
	memcpy(template, tmp_directory, tmp_len);
	template[strlen(tmp_directory)] = '/';
	memcpy(template + tmp_len + 1, template_frag, template_frag_len);
	template[tmp_len + template_frag_len + 1] = 0;
	char *result = mkdtemp(template);
	if (!result) {
		free(template);
	}
	// Tweak the results, enforcing a trailing '/';
	template[tmp_len + template_frag_len + 1] = '/';
	template[tmp_len + template_frag_len + 2] = 0;
	return template;
}

static int
build_and_swap_tmpspace_array(char ***final_paths_ptr, multifile_file_data **ref_files, unsigned long ref_count)
{
	char **final_paths = (char **)calloc(sizeof(char *), ref_count);
	if (!final_paths) {
		return 1;
	}
	*final_paths_ptr = final_paths;

	// Swap in the tmp pathways, building an array we use for moving files
	// as we encounter the command in the stream.
	size_t chars_needed = 1;
	unsigned long x;
	for (x = ref_count; x > 0; x /= 10) {
		chars_needed++;
	}

	char buf[chars_needed];

	for (x = 0; x < ref_count; x++) {
		int len = snprintf(buf, chars_needed, "%lu", x);
		assert (len <= chars_needed);
		char *p = strdup(buf);
		if (!p) {
			return 1;
		}

		final_paths[x] = ref_files[x]->filename;
		ref_files[x]->filename = p;
	}
	return 0;
}

static int
rebuild_files_from_delta(cfile *src_cfh, cfile *containing_patchf, cfile *out_cfh, size_t delta_start, size_t delta_length)
{
	cfile deltaf;
	memset(&deltaf, 0, sizeof(cfile));
	int err = copen_child_cfh(&deltaf, containing_patchf, delta_start, delta_start + delta_length, containing_patchf->compressor_type, CFILE_RONLY);
	if (err) {
		eprintf("Failed opening cfile for the embedded delta: window was %zu to %zu\n", delta_start, delta_start + delta_length);
		return err;
	}

	cfile *delta_array[1] = {&deltaf};
	err = simple_reconstruct(src_cfh, delta_array, 1, out_cfh, SWITCHING_FORMAT, 0xffff);
	cclose(&deltaf);
	if (!err) {
		cseek(containing_patchf, delta_start + delta_length, CSEEK_FSTART);
	}
	return err;
}

signed int 
treeReconstruct(const char *src_directory, cfile *patchf, const char *raw_directory, const char *tmp_directory)
{
	cfile src_cfh, trg_cfh;
	memset(&src_cfh, 0, sizeof(cfile));
	memset(&trg_cfh, 0, sizeof(cfile));
	int err = 0;

	unsigned char buff[16];
	char *target_directory = NULL;
	multifile_file_data **src_files = NULL, **ref_files = NULL;
	unsigned long src_count = 0, ref_count = 0;
	unsigned long x;
	char **final_paths = NULL;
	char *tmpspace = NULL;

	if(TREE_MAGIC_LEN != cseek(patchf, TREE_MAGIC_LEN, CSEEK_FSTART)) {
		eprintf("Failed seeking beyond the format magic\n");
		return PATCH_TRUNCATED;
	}
	if(TREE_VERSION_LEN != cread(patchf, buff, TREE_VERSION_LEN)) {
		eprintf("Failed reading version identifier\n");
		return PATCH_TRUNCATED;
	}
	unsigned int ver = readUBytesLE(buff, TREE_VERSION_LEN);
	v2printf("patch format ver=%u\n", ver);

//	add_id = DCB_REGISTER_VOLATILE_ADD_SRC(dcbuff, patchf, NULL, 0);
//	ref_id = src_id;
	v3printf("Reading src file manifest\n");

	err = read_file_manifest(patchf, &src_files, &src_count, "source");
	if (err) {
		return err;
	}

	err = copen_multifile(&src_cfh, src_directory, src_files, src_count, CFILE_RONLY);
	if (err) {
		eprintf("Failed open source directory %s: err %i\n", src_directory, err);
		goto cleanup;
	}

	err = multifile_ensure_files(&src_cfh, 0);
	if (err) {
		goto cleanup;
	}

	err = read_file_manifest(patchf, &ref_files, &ref_count, "target");
	if (err) {
		goto cleanup;
	}

	target_directory = strdup(raw_directory);
	if (!target_directory || enforce_trailing_slash(&target_directory)) {
		eprintf("allocation errors encountered\n");
		err = MEM_ERROR;
		goto cleanup;
	}

	tmpspace = make_tempdir(tmp_directory);
	if (!tmpspace) {
		eprintf("Failed creating tmpdir for reconstructed files\n");
		goto cleanup;
	}

	v3printf("Creating temp file array, and on disk files\n");
	if (build_and_swap_tmpspace_array(&final_paths, ref_files, ref_count)) {
		eprintf("Failed allocating tmpspace array\n");
		goto cleanup;
	}

	err = copen_multifile(&trg_cfh, tmpspace, ref_files, ref_count, CFILE_WONLY);
	if (err) {
		eprintf("Failed opening temp space directory %s: err %i\n", tmpspace, err);
		goto cleanup;
	}

	// Create our files now.
	err = multifile_ensure_files(&trg_cfh, 1);
	if (err) {
		eprintf("Failed creating temporary files for reconstruction\n");
		goto cleanup;
	}

	if (8 != cread(patchf, buff, 8)) {
		eprintf("Failed reading delta length\n");
		err = PATCH_TRUNCATED;
		goto cleanup;
	}

	unsigned long delta_size = readUBytesLE(buff, 8);
	size_t delta_start = ctell(patchf, CSEEK_FSTART);
	err = rebuild_files_from_delta(&src_cfh, patchf, &trg_cfh, delta_start, delta_size);
	if (err) {
		eprintf("Failed regenerating new files from the delta: err %i\n", err);
		goto cleanup;
	}

	// Flush the output handle; ensure all content is on disk.
	err = cflush(&trg_cfh);

	assert(TREE_INTERFILE_MAGIC_LEN < sizeof(buff));
	if (TREE_INTERFILE_MAGIC_LEN != cread(patchf, buff, TREE_INTERFILE_MAGIC_LEN)) {
		eprintf("Failed reading intrafile magic in patch file at position %zu\n", delta_size + delta_start);
		err = PATCH_TRUNCATED;
		goto cleanup;
	}
	if (memcmp(buff, TREE_INTERFILE_MAGIC, TREE_INTERFILE_MAGIC_LEN) != 0) {
		eprintf("Failed to verify intrafile magic in patch file at position %zu; likely corrupted\n", delta_size + delta_start);
		err = PATCH_CORRUPT_ERROR;
		goto cleanup;
	}
	v3printf("Starting tree command stream at %zu\n", ctell(patchf, CSEEK_FSTART));

	if (4 != cread(patchf, buff, 4)) {
		eprintf("Failed reading command count\n");
		err = PATCH_TRUNCATED;
		goto cleanup;
	}
	unsigned long command_count = readUBytesLE(buff, 4);
	v3printf("command stream is %lu commands\n", command_count);

	unsigned long file_pos = 0;
	for (x = 0; x < command_count; x++) {
		err = consume_command_chain(target_directory, tmpspace, patchf, ref_files, final_paths, ref_count, &file_pos, x);
		if (err) {
			goto cleanup;
		}
	}

	v1printf("Reconstruction completed successfully\n");
	err = 0;

	cleanup:
	if (target_directory) {
		free(target_directory);
	}

	if (final_paths) {
		for (x=0; x < ref_count; x++) {
			if (final_paths[x]) {
				free(final_paths[x]);
			}
		}
		free(final_paths);
	}

	if (tmpspace) {
		err = enforce_unlink(tmpspace);
		if (err) {
			eprintf("Failed cleaning up temp directory %s\n", tmpspace);
		}
		free(tmpspace);
	}

	if (cfile_is_open(&src_cfh)) {
		cclose(&src_cfh);
	} else if (src_files) {
		multifile_free_file_data_array(src_files, src_count);
		free(src_files);
	}

	if (cfile_is_open(&trg_cfh)) {
		cclose(&trg_cfh);
	} else if (ref_files) {
		multifile_free_file_data_array(ref_files, ref_count);
		free(ref_files);
	}

	return err;
}

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
#include <sys/stat.h>
#include <diffball/dcbuffer.h>
#include <diffball/defs.h>
#include <cfile.h>
#include <diffball/bit-functions.h>
#include <diffball/tree.h>
#include <diffball/switching.h>

// Used only for the temp file machinery; get rid of this at that time.
#include <unistd.h>

static int flush_file_content_delta(CommandBuffer *dcbuff, cfile *patchf);
static int encode_fs_entry(cfile *patchf, multifile_file_data *entry, unsigned long *files_count);

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

	// Identify and output the # of files that will be created.
	unsigned long x, file_count;
	for(file_count = 0, x = 0; x < ref_count; x++) {
		if (S_ISREG(ref_files[x]->st->st_mode) && !ref_files[x]->link_target) {
			file_count++;
		}
	}
	v3printf("Recording %lu files in the delta manifest\n", file_count);
	cWriteUBytesLE(patchf, file_count, 4);
	// Write the files.
	for(x=0; file_count > 0; x++) {
		if (S_ISREG(ref_files[x]->st->st_mode) && !ref_files[x]->link_target) {
			// We write the null too.
			v3printf("Recording file %s length %zi in the delta manifest\n", ref_files[x]->filename, ref_files[x]->st->st_size);
			size_t len = strlen(ref_files[x]->filename) + 1;
			if (len != cwrite(patchf, ref_files[x]->filename, len)) {
				v0printf("Failed writing file manifest\n");
				return IO_ERROR;
			}
			err = cWriteUBytesLE(patchf, ref_files[x]->st->st_size, 8);
			if (err) {
				return err;
			}
			file_count--;
		}
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

	for(x=0, file_count=0; x < ref_count; x++) {
		err = encode_fs_entry(patchf, ref_files[x], &file_count);
		if (err) {
			return err;
		}
	}

	return 0;
}

static int
encode_fs_entry(cfile *patchf, multifile_file_data *entry, unsigned long *files_count)
{
	#define write_or_return(value, len) {int err=cWriteUBytesLE(patchf, (value), (len)); if (err) { return err; }; }

#define write_common_block(st) \
	write_or_return((st)->st_uid, TREE_COMMAND_UID_LEN); \
	write_or_return((st)->st_gid, TREE_COMMAND_GID_LEN); \
	write_or_return((st)->st_mode, TREE_COMMAND_MODE_LEN); \
	write_or_return((st)->st_ctime, TREE_COMMAND_TIME_LEN); \
	write_or_return((st)->st_mtime, TREE_COMMAND_TIME_LEN);

#define write_null_string(value) \
{ int len=strlen((value)); if (len != cwrite(patchf, (value), len)) { v0printf("Failed writing string len %i\n", len); return IO_ERROR; }; }

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


signed int 
treeReconstructDCBuff(DCB_SRC_ID src_id, cfile *patchf, CommandBuffer *dcbuff)
{
	unsigned int int_size;
	#define BUFF_SIZE 12
	unsigned int ver;
	EDCB_SRC_ID ref_id, add_id;
	unsigned char buff[BUFF_SIZE];
	off_s64  copy_offset;
	off_u32  match_orig, matches, add_len, copy_len;
	off_u32  size1, size2, or_mask=0, neg_mask;
	off_u64  ver_pos = 0, add_pos;
	off_u64  processed_size = 0;
	off_u32  add_start;

	dcbuff->ver_size = 0;
//	assert(DCBUFFER_FULL_TYPE == dcbuff->DCBtype);
	if(3!=cseek(patchf, TREE_MAGIC_LEN, CSEEK_FSTART))
		goto truncated_patch;
	if(2!=cread(patchf, buff, TREE_VERSION_LEN))
		goto truncated_patch;
	ver = readUBytesLE(buff, 2);
	v2printf("ver=%u\n", ver);

//	add_id = DCB_REGISTER_VOLATILE_ADD_SRC(dcbuff, patchf, NULL, 0);
//	ref_id = src_id;
	dcbuff->ver_size = dcbuff->reconstruct_pos;
	v2printf("finished reading.  ver_pos=%llu, add_pos=%u\n",
		(act_off_u64)ver_pos, add_pos);
	return 0;

	truncated_patch:
	return PATCH_TRUNCATED;
}

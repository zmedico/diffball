// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2005 Brian Harring <ferringb@gmail.com>
#include <stdlib.h>
#include <diffball/defs.h>
#include <string.h>
#include <diffball/bdiff.h>
#include <diffball/bit-functions.h>

unsigned int
check_bdiff_magic(cfile *patchf)
{
	unsigned char buff[BDIFF_MAGIC_LEN + 1];
	cseek(patchf, 0, CSEEK_FSTART);
	if (BDIFF_MAGIC_LEN + 1 != cread(patchf, buff, BDIFF_MAGIC_LEN + 1))
	{
		return 0;
	}
	if (memcmp(buff, BDIFF_MAGIC, BDIFF_MAGIC_LEN) != 0)
	{
		return 0;
	}
	if (buff[BDIFF_MAGIC_LEN] == BDIFF_VERSION)
	{
		return 2;
	}
	return 1;
}

signed int
bdiffEncodeDCBuffer(CommandBuffer *buffer, cfile *out_cfh)
{
#define BUFFER_SIZE 1024
	unsigned char buff[BUFFER_SIZE];
	off_u32 delta_pos;
	off_u64 fh_pos;
	off_u32 lb;
	DCommand dc;

	DCBufferReset(buffer);
	memcpy(buff, BDIFF_MAGIC, BDIFF_MAGIC_LEN);
	buff[BDIFF_MAGIC_LEN] = BDIFF_VERSION;

	/* I haven't studied the author of bdiff's alg well enough too know what
	   MaxBlockSize is for.  Either way, throwing in the default. */

	writeUBytesBE(buff + BDIFF_MAGIC_LEN + 1,
				  (BDIFF_DEFAULT_MAXBLOCKSIZE), 4);

	cwrite(out_cfh, buff, BDIFF_MAGIC_LEN + 5);
	delta_pos = 10;
	fh_pos = 0;
	while (DCB_commands_remain(buffer))
	{
		DCB_get_next_command(buffer, &dc);
		if (DC_COPY == dc.type)
		{
			dcb_lprintf(2, "copy command, out_cfh(%u), fh_pos(%llu), offset(%llu), len(%u)\n",
						delta_pos, (act_off_u64)fh_pos, (act_off_u64)dc.data.src_pos, dc.data.len);
			fh_pos += dc.data.len;
			lb = 5;
			buff[0] = 0;
			writeUBytesBE(buff + 1, dc.data.src_pos, 4);
			if (dc.data.len > 5 && dc.data.len <= 5 + 0x3f)
			{
				buff[0] = dc.data.len - 5;
			}
			else
			{
				writeUBytesBE(buff + 5, dc.data.len, 4);
				lb += 4;
			}
			delta_pos += lb;
			cwrite(out_cfh, buff, lb);
		}
		else
		{
			dcb_lprintf(2, "add  command, out_cfh(%u), fh_pos(%llu), len(%u)\n",
						delta_pos, (act_off_u64)fh_pos, dc.data.len);
			fh_pos += dc.data.len;
			buff[0] = 0x80;
			lb = 1;
			if (dc.data.len > 5 && dc.data.len <= 5 + 0x3f)
			{
				buff[0] |= dc.data.len - 5;
			}
			else
			{
				writeUBytesBE(buff + 1, dc.data.len, 4);
				lb += 4;
			}
			delta_pos += lb + dc.data.len;
			cwrite(out_cfh, buff, lb);
			if (dc.data.len != copyDCB_add_src(buffer, &dc, out_cfh))
			{
				return EOF_ERROR;
			}
		}
	}
	return 0;
}

signed int
bdiffReconstructDCBuff(DCB_SRC_ID src_id, cfile *patchf, CommandBuffer *dcbuff)
{
	unsigned char src_md5[16], ver_md5[16], buff[17];
	off_u32 len, offset;
	off_u64 fh_pos;

	dcbuff->ver_size = 0;
	memset(src_md5, 0, 16);
	memset(ver_md5, 0, 16);
	/* skippping magic bdiff, and version char 'a' */
	cseek(patchf, 6, CSEEK_ABS);
	cread(patchf, buff, 4);
	/* Format specifies a 'maxlength', but isn't actually used to my knowledge; read 4 bytes either way.  */
	readUBytesBE(buff, 4);
	fh_pos = 0;
	EDCB_SRC_ID add_id = DCB_REGISTER_VOLATILE_ADD_SRC(dcbuff, patchf, NULL, 0);
	while (1 == cread(patchf, buff, 1))
	{
		dcb_lprintf(2, "got command(%u): ", buff[0]);
		if ((buff[0] >> 6) == 00)
		{
			buff[0] &= 0x3f;
			dcb_lprintf(2, "got a copy at %u, fh_pos(%llu): ",
						(off_u32)ctell(patchf, CSEEK_FSTART), (act_off_u64)fh_pos);
			if (4 != cread(patchf, buff + 1, 4))
			{
				return EOF_ERROR;
			}
			offset = readUBytesBE(buff + 1, 4);
			if (buff[0])
			{
				len = readUBytesBE(buff, 1) + 5;
			}
			else
			{
				if (4 != cread(patchf, buff, 4))
				{
					return EOF_ERROR;
				}
				len = readUBytesBE(buff, 4);
			}
			fh_pos += len;
			dcb_lprintf(2, " offset(%llu), len=%u\n", (act_off_u64)offset, len);
			DCB_add_copy(dcbuff, offset, 0, len, src_id);
		}
		else if ((buff[0] >> 6) == 2)
		{
			buff[0] &= 0x3f;
			dcb_lprintf(2, "got an add at %u, fh_pos(%llu):",
						(off_u32)ctell(patchf, CSEEK_FSTART), (act_off_u64)fh_pos);

			if (buff[0])
			{
				len = readUBytesBE(buff, 1) + 5;
			}
			else
			{
				if (4 != cread(patchf, buff, 4))
				{
					return EOF_ERROR;
				}
				len = readUBytesBE(buff, 4);
			}
			fh_pos += len;

			dcb_lprintf(2, " len=%u\n", len);

			DCB_add_add(dcbuff, ctell(patchf, CSEEK_FSTART), len, add_id);
			cseek(patchf, len, CSEEK_CUR);
		}
		else if ((buff[0] >> 6) == 1)
		{

			buff[0] &= 0x3f;
			dcb_lprintf(2, "got a checksum at %u\n", (off_u32)ctell(patchf, CSEEK_FSTART));

			if (buff[0] <= 1)
			{
				if (16 != cread(patchf, buff + 1, 16))
					return EOF_ERROR;
				if (buff[0] == 0)
					memcpy(src_md5, buff + 1, 16);
				else
					memcpy(ver_md5, buff + 1, 16);
			}
			else
			{
				return EOF_ERROR;
			}
		}
	}
	dcbuff->ver_size = dcbuff->reconstruct_pos;
	return 0;
}

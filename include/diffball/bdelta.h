/*
  Copyright (C) 2003-2005 Brian Harring

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
#ifndef _HEADER_BDELTA
#define _HEADER_BDELTA 1
#include <diffball/dcbuffer.h>
#include <cfile.h>

#define BDELTA_MAGIC				"BDT"
#define BDELTA_MAGIC_LEN		3
#define BDELTA_VERSION				0x1
#define BDELTA_VERSION_LEN		2

unsigned int check_bdelta_magic(cfile *patchf);
signed int bdeltaEncodeDCBuffer(CommandBuffer *dcbuff, 
			cfile *out_cfh);
signed int bdeltaReconstructDCBuff(DCB_SRC_ID src_id, cfile *patchf, 
		CommandBuffer *dcbuff);


/* BDELTA Notes
  command is 3 intsize (intsize locked to 4 in last version of bdelta)
  copy offset
  add len
  copy len

  Note, bdelta-0.1 *does not abide by it's own format*.
  If size2 != last match, it incorrectly outputs the last command as a copy 
  len, rather then an add.  It is an add command however.

  That one sucked to find/fix.  On the plus side, bdelta is consistent about 
  that screw up in it's patch generation, and reconstruction, so it's not a 
  prob for it.

  This is only a prob for reconstruct, encode will be written to always 
  output the full command buffer (it doesn't leave off trailing nulls).
*/

#endif

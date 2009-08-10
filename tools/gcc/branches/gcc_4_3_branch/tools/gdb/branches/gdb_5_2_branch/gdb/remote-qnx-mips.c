/* Remote target communications for serial-line targets in custom GDB protocol
   Copyright 1988, 1991, 1992, 1993, 1994, 1995, 1996 Free Software Foundation, Inc.
   Contributed by QNX Software Systems Limited.

This file is part of GDB.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

/* This file was derived from remote.c. It implements the MIPS specific
   portion of the Neutrino remote debug proctocol (see remote-qnx.c for
   the CPU independent portion). */

#include "defs.h"
#include "gdb_string.h"
#include <fcntl.h>
#include "frame.h"
#include "inferior.h"
#include "bfd.h"
#include "symfile.h"
#include "target.h"
#include "gdb_wait.h"
#include "gdbcmd.h"
#include "objfiles.h"
#include "gdb-stabs.h"
#include "gdbthread.h"
#include "dsmsgs.h"

#include "dcache.h"

#ifdef USG
#include <sys/types.h>
#endif

#include <signal.h>
#include "serial.h"

#include <nto_procfs.h>
#ifndef MIPS_PRID_IMPL
#define MIPS_PRID_IMPL(p)               (((p) >> 8) & 0xff)
#endif

/* Given the first and last register number, figure out the size/len
   of the Neutrino register save area to ask for/tell about. Also set
   the register set that's being dealt with in *subcmd. Watch out for
   the range crossing a register set boundry. 
*/
/* Added support for the altregs, for the tx79 128 bit registers.
   GP - Oct 29, 2001.
*/

extern char *mips_processor_type;
extern char *tmp_mips_processor_type;
extern void mips_set_processor_type_command PARAMS ((char *str, int from_tty));

void 
qnx_set_processor_type (uint32_t cpuid)
{
    if(MIPS_PRID_IMPL(cpuid) == 0x38)
        tmp_mips_processor_type = xstrdup("tx79");
    else
        tmp_mips_processor_type = xstrdup("r3051");
    mips_set_processor_type_command(tmp_mips_processor_type, 0);
    return;
}

/*
	Given a register return an id that represents the Neutrino regset it came from
	if reg == -1 update all regsets
	$MJC
 */

enum QNX_REGS {
	QNX_REGS_GP  = 0,
	QNX_REGS_FP  = 1,
	QNX_REGS_ALT = 2,
	QNX_REGS_END = 3
};

int qnx_get_regset_id( int regno )
{
	int regset = 0;

	if ( regno == -1 ){
		regset	= QNX_REGS_END;
	} else if( regno < FP0_REGNUM ){
		regset = QNX_REGS_GP;
	} else if( regno < FP_REGNUM ){
		regset =  QNX_REGS_FP;
	} else if( regno < FP_REGNUM + 1 ){
	/* Frame Pointer Psuedo-register */
		regset = QNX_REGS_GP;
#ifdef FIRST_COP0_REG
	} else if( mips_processor_type && !strcasecmp( "tx79", mips_processor_type ) ){
		if( regno < FIRST_COP0_REG) {
		  regset = QNX_REGS_ALT;
		}
#endif
	} else {
		regset =  3; /* lets motor thru for now more display reg bugs */
	}
	return regset;
}

static void regset_fetch_zero(
	int 	 endian,
	unsigned first,
	unsigned last )
{
    static unsigned  zero[2] = {0, 0 };
	for(; first <= last; ++first) {
		supply_register(first, (char *)zero);
	}
}

static void regset_fetch(
	int 	 endian,
	unsigned first,
	unsigned last,
	unsigned *data )
{
	if( endian && REGISTER_RAW_SIZE(first) == 4) data += 1; /* data in second word for big endian */
	for(; first <= last; ++first) {
		supply_register(first, (char *)data);
		data+=2;
	}
}

void qnx_cpu_supply_regset(
	int endian,
	int regset,
	void *data )
{
	unsigned first;
	unsigned last;
	
	switch( regset ){
	case QNX_REGS_GP:
		first = 0;
		last = FP0_REGNUM-1;
		break;
	case QNX_REGS_FP:
		first = FP0_REGNUM;
		last = FP_REGNUM-1;
		break;
	case QNX_REGS_ALT:
#ifdef FIRST_COP0_REG
		first = FP_REGNUM;
		last = FIRST_COP0_REG-1;
		break;
#endif
	default: /* do nothing for now */
		return;
	}
	if( data != NULL ){
		regset_fetch( endian, first, last, (unsigned *)data );
		if( regset == QNX_REGS_GP ){ /* lets not forget frame hack */
		    unsigned *reg = data;
		    reg = reg+SP_REGNUM*2;
		    if( endian && REGISTER_RAW_SIZE(FP_REGNUM) == 4) reg += 1;
		    supply_register( FP_REGNUM, (char *)reg );
	    }	
	}else{
	    regset_fetch_zero( endian, first, last );
#if 0
	    if( regset == QNX_REGS_FP ) {
		//display reg tries to display status regs this is a hack till it gets fixed
	        regset_fetch_zero( endian, first, last );
	    }else
			regset_invalid( first, last );
			if( regset == QNX_REGS_GP ){ /* frame hack */
		 	   register_valid[FP_REGNUM] = -1;
	    } 
#endif
	}
}
/* get regset characteristics */
unsigned qnx_get_regset_area( unsigned regset, char *subcmd )
{
	unsigned length;
	switch( regset ){
	case QNX_REGS_GP:
		*subcmd = DSMSG_REG_GENERAL;
		length = FP0_REGNUM * 8;
		break;
	case QNX_REGS_FP:
		*subcmd = DSMSG_REG_FLOAT;
		length = (FP_REGNUM-FP0_REGNUM) * 8;
		break;
	case QNX_REGS_ALT:
#ifdef FIRST_COP0_REG
		if(mips_processor_type && !strcasecmp("tx79", mips_processor_type)) {
			*subcmd = DSMSG_REG_ALT;
			length = (FIRST_COP0_REG-FP_REGNUM+1) * 8;
			break;
		}
#endif
	default:
		length = 0;
	}
	return length;
}
unsigned
qnx_cpu_register_area(first_regno, last_regno, subcmd, off, len)
    unsigned first_regno;
    unsigned last_regno;
    unsigned char *subcmd;
    unsigned *off;
    unsigned *len;
{
    if(first_regno < FP0_REGNUM) {
	if(last_regno >= FP0_REGNUM) last_regno = FP0_REGNUM-1;
	*subcmd = DSMSG_REG_GENERAL;
    } else if(first_regno < FP_REGNUM) {
	if(last_regno >= FP_REGNUM) last_regno = FP_REGNUM-1;
	*subcmd = DSMSG_REG_FLOAT;
    } else if(first_regno < FP_REGNUM + 1) {
	/* Frame Pointer Psuedo-register */
	*off = SP_REGNUM * 8;
	*len =  8;
	return FP_REGNUM;
#ifdef FIRST_COP0_REG
    } else if(mips_processor_type && !strcasecmp("tx79", mips_processor_type)) {
        if(first_regno < FIRST_COP0_REG) { 
          if(last_regno >= FIRST_COP0_REG) last_regno = FIRST_COP0_REG - 1;
          *subcmd = DSMSG_REG_ALT;
        }
#endif
    } else {
	*len = 0;
	return last_regno;
    }
    *off = first_regno * 8;
    *len = (last_regno - first_regno + 1) * 8;
    return last_regno;
}

/* Build the Neutrino register set info into the 'data' buffer */

int
qnx_cpu_register_store(endian, first_regno, last_regno, data)
    int endian;
    unsigned first_regno;
    unsigned last_regno;
    void *data;
{
    char	*dest;
    void	*regp;
    int		sign;
    unsigned	size;
    void	*sign_ext;

    if(first_regno >= FP_REGNUM) return 0;
    dest = data;
    while(first_regno <= last_regno) {
      	regp = &registers[REGISTER_BYTE(first_regno)];
        size = REGISTER_RAW_SIZE(first_regno);
	if(endian && REGISTER_RAW_SIZE(FP_REGNUM) == 4) {
	    data = dest + (8-size);
	    sign = *(signed char *)regp;
	    sign_ext = dest;
	} else {
	    data = dest;
	    sign = *((signed char *)regp + (size-1));
	    sign_ext = dest + size;
	}
	memcpy(data, regp, size);
	memset(sign_ext, (sign < 0) ? -1 : 0, 8 - size);
	dest += 8;
	++first_regno;
    }
    return 1;
}

typedef char nto_reg64[8];

void
nto_supply_gregset (char *data)
{
  int regi, off = 0;
  nto_reg64 *regs = (nto_reg64 *)data;

  /* on big endian, register data is in second word of Neutrino's 8 byte regs */
  if(TARGET_BYTE_ORDER == BFD_ENDIAN_BIG &&
     REGISTER_RAW_SIZE(ZERO_REGNUM) == 4)
          off = 4;

  for(regi = ZERO_REGNUM; regi < FP0_REGNUM; regi++)
    supply_register(regi, &regs[regi - ZERO_REGNUM][off]);

#if defined(FIRST_ALTREG) && defined(LAST_ALTREG) //tx79
  /* FIXME */
  if(mips_processor_type && !strcasecmp("tx79", mips_processor_type))
    qnx_supply_register(FIRST_ALTREG, LAST_ALTREG, (char *)gregsetp );
#endif
}

void
nto_supply_fpregset (char *data)
{
  int regi, off = 0;
  nto_reg64 *regs = (nto_reg64 *)data;
  
  if(TARGET_BYTE_ORDER == BFD_ENDIAN_BIG &&
     REGISTER_RAW_SIZE(ZERO_REGNUM) == 4)
          off = 4;
  
  for(regi = FP0_REGNUM; regi < FP0_REGNUM + 16; regi++)
    supply_register(regi, &regs[regi - FP0_REGNUM][off]);

  supply_register(FCRCS_REGNUM, data + 8 * 32);
}

#include "solib-svr4.h"		/* For struct link_map_offsets.  */

struct link_map_offsets *
mips_qnx_svr4_fetch_link_map_offsets (void)
{
  static struct link_map_offsets lmo;
  static struct link_map_offsets *lmp = NULL;

  if (lmp == NULL)
    {
      lmp = &lmo;

      lmo.r_debug_size = 8;	/* The actual size is 20 bytes, but
				   this is all we need.  */
      lmo.r_map_offset = 4;
      lmo.r_map_size   = 4;

      lmo.link_map_size = 20;	/* The actual size is 552 bytes, but
				   this is all we need.  */
      lmo.l_addr_offset = 0;
      lmo.l_addr_size   = 4;

      lmo.l_name_offset = 4;
      lmo.l_name_size   = 4;

      lmo.l_next_offset = 12;
      lmo.l_next_size   = 4;

      lmo.l_prev_offset = 16;
      lmo.l_prev_size   = 4;
    }

  return lmp;
}

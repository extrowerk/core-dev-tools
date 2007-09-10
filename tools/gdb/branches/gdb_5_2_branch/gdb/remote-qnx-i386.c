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
   portion of the Neutrino remote debug proctocol (see remote-nto.c for
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

/* Prototypes for i387_supply_fsave etc.  */
#include "i387-nat.h"

#ifndef FPC_REGNUM
#define FPC_REGNUM (FP0_REGNUM + 8)
#endif

#define NUM_GPREGS 13
static int gdb_to_nto[NUM_GPREGS] = { 7, 6, 5, 4, 11, 2, 1, 0, 8, 10, 9, 12, -1 };

#define GDB_TO_OS(x)	((x >= 0 && x < NUM_GPREGS) ? gdb_to_nto[x] : -1)

#ifndef X86_CPU_FXSR
#define X86_CPU_FXSR (1L << 12)
#endif

extern struct dscpuinfo qnx_cpuinfo;
extern int qnx_cpuinfo_valid;
extern int qnx_ostype, qnx_cputype;
/*
	Given a register return an id that represents the Neutrino regset it came from
	if reg == -1 update all regsets
	$MJC
 */

enum QNX_REGS {
	QNX_REGS_GP  = 0,
	QNX_REGS_FP  = 1,
	QNX_REGS_END = 2
};
unsigned qnx_get_regset_id( int regno )
{
	unsigned regset;
	if( regno == -1 ){/* return size so we can enumerate */
		regset = 2;
	} else if( regno < FP0_REGNUM) {
		regset = 0;
	} else if(regno < FPC_REGNUM){
		regset = 1;
	} else {
		regset = -1; /* error */
	}
	return regset;
}
/* Tell GDB about the regset contained in the 'data' buffer  */
static void gp_supply_regset( char *data )
{
    unsigned first_regno;
    	for( first_regno = 0; first_regno < FP0_REGNUM; first_regno++) {
	    	int regno = GDB_TO_OS(first_regno);
		int const minusone = -1;

		if ( data == NULL || regno == -1) {
			supply_register(first_regno, (char *)&minusone);
		}else {
			supply_register(first_regno, &data[regno*4]);
		}
	}
}
// get 8087 data
static void fp_supply_regset( char * data )
{
	if(qnx_cpuinfo_valid && qnx_cpuinfo.cpuflags | X86_CPU_FXSR)
		i387_supply_fxsave(data);
	else
		i387_supply_fsave(data);
}

void qnx_cpu_supply_regset( int endian, int regset, void *data)
{
	endian = endian; /* x86, don't care about endian */

	switch( regset ){
	case QNX_REGS_GP: /* QNX has different ordering of GP regs GDB */
		gp_supply_regset( (char *)data );
		break;
	case QNX_REGS_FP:
	    	fp_supply_regset( (char *)data );
		break;
	default: /* do nothing for now */
	}
}

/* get regset characteristics */
unsigned qnx_get_regset_area( unsigned regset, char *subcmd )
{
	unsigned length = 0;
	switch( regset ){
	case QNX_REGS_GP:
		*subcmd = DSMSG_REG_GENERAL;
		length = NUM_GPREGS*sizeof(unsigned); 
		break;
	case QNX_REGS_FP:
		*subcmd = DSMSG_REG_FLOAT;
		/* FIXME: should we calculate based on fxsave/fsave? */
		length = 512;
		break;
	default:
		length = 0;
	}
	return length;
}

/*-
	Given the first and last register number, figure out the size/len
	of the Neutrino register save area to ask for/tell about. Also set
	the register set that's being dealt with in *subcmd. Watch out for
	the range crossing a register set boundry.
 */

unsigned
qnx_cpu_register_area(first_regno, last_regno, subcmd, off, len)
    unsigned first_regno;
    unsigned last_regno;
    unsigned char *subcmd;
    unsigned *off;
    unsigned *len;
{
    int regno = -1;

    if(first_regno < FP0_REGNUM) {
	if(last_regno >= FP0_REGNUM) last_regno = FP0_REGNUM-1;
	*subcmd = DSMSG_REG_GENERAL;
	regno = GDB_TO_OS(first_regno);
	*off = regno * sizeof(unsigned);
	if (regno == -1)
	    *len = 0;
	else
	    *len = (last_regno - first_regno + 1) * sizeof(unsigned);
    } else if (first_regno == FP_REGNUM) {
	/* Frame Pointer Psuedo-register */
	*off = SP_REGNUM * sizeof(unsigned);
	*len =  sizeof(unsigned);
	return FP_REGNUM;
    } else if(first_regno >= FP0_REGNUM && first_regno < FPC_REGNUM ) {
	unsigned off_adjust, regsize;
	
	if(qnx_cpuinfo_valid && qnx_cpuinfo.cpuflags | X86_CPU_FXSR){
		off_adjust = 32;
		regsize = 16;
	}
	else{
		off_adjust = 28;
		regsize = 10;
	}
	
	if(last_regno >= FPC_REGNUM) last_regno = FPC_REGNUM-1;
	*subcmd = DSMSG_REG_FLOAT;
	*off = (first_regno - FP0_REGNUM) * regsize + off_adjust;
	*len = (last_regno - first_regno + 1) * 10;
	/* Why 10?  GDB only stores 10 bytes per FP register so if we're
	 * sending a register back to the target, we only want pdebug to write
	 * 10 bytes so as not to clobber the reserved 6 bytes in the fxsave
	 * structure.  The astute reader will note that this will fail if we
	 * try to send a range of fpregs rather than 1 at a time but, as far
	 * as I know, there is no way to send more than one fpreg at a time
	 * anyway.  If this turns out to be wrong, we may need to put more code
	 * in pdebug to deal with this - perhaps by masking off part of the
	 * register when it writes it in.*/
    } else {
	*len = 0;
	return last_regno;
    }
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
    for (;first_regno <= last_regno; first_regno++) {
	if (first_regno < FP0_REGNUM) {
	    int regno = GDB_TO_OS(first_regno);

	    if (regno == -1)
		    continue;
	    memcpy(data, &registers[REGISTER_BYTE(first_regno)], REGISTER_RAW_SIZE(first_regno));
	    (char *)data += sizeof(unsigned);
	} else /* we have an fp reg */ {
	    memcpy(data, &registers[REGISTER_BYTE(first_regno)], REGISTER_RAW_SIZE(first_regno));
	    (char *)data += REGISTER_RAW_SIZE(first_regno);
	}
    }
    return 1;
}

/* nto_supply_* are used by nto_procfs.c and qnx_core-regset.c */
void
nto_supply_gregset (gregsetp)
     nto_gregset_t *gregsetp;
{
  register int regi, i = 0;
  register uint32_t *regp = (uint32_t *) gregsetp;

  for (regi = 0 ; regi < (NUM_REGS - FP0_REGNUM) ; regi++)
    {
	i = GDB_TO_OS(regi);
	supply_register (regi, (char *) &regp[i] );
    }
}

void
nto_supply_fpregset (fpregsetp)
     nto_fpregset_t *fpregsetp;
{
	if(qnx_cpuinfo_valid && qnx_cpuinfo.cpuflags | X86_CPU_FXSR)
		i387_supply_fxsave ((char *) fpregsetp);
	else
		i387_supply_fsave ((char *) fpregsetp);
}

extern void qnx_read_ioport_8 PARAMS ((char *, int));
extern void qnx_read_ioport_16 PARAMS ((char *, int));
extern void qnx_read_ioport_32 PARAMS ((char *, int));
extern void qnx_write_ioport_8 PARAMS ((char *, int));
extern void qnx_write_ioport_16 PARAMS ((char *, int));
extern void qnx_write_ioport_32 PARAMS ((char *, int));

void
_initialize_nto_i386()
{
#define IO_PORT_HACKS
#ifdef IO_PORT_HACKS
	add_com( "in8", class_maintenance, qnx_read_ioport_8, "read a 8 bit value from an ioport" );
	add_com( "in16", class_maintenance, qnx_read_ioport_16, "read a 16 bit value from an ioport" );
	add_com( "in32", class_maintenance, qnx_read_ioport_32, "read a 32 bit value from an ioport" );
	add_com( "out8", class_maintenance, qnx_write_ioport_8, "write a 8 bit value to an ioport" );
	add_com( "out16", class_maintenance, qnx_write_ioport_16, "write a 16 bit value to an ioport" );
	add_com( "out32", class_maintenance, qnx_write_ioport_32, "write a 32 bit value to an ioport" );
#endif
}

#include "solib-svr4.h"		/* For struct link_map_offsets.  */

/* Fetch (and possibly build) an appropriate link_map_offsets
   structure for native GNU/Linux x86 targets using the struct offsets
   defined in link.h (but without actual reference to that file).

   This makes it possible to access GNU/Linux x86 shared libraries
   from a GDB that was not built on an GNU/Linux x86 host (for cross
   debugging).  */

struct link_map_offsets *
i386_qnx_svr4_fetch_link_map_offsets (void)
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

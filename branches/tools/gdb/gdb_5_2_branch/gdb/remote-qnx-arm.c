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

/* This file was derived from remote.c. It implements the ARM specific
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

#include "arm-tdep.h"

#ifdef USG
#include <sys/types.h>
#endif

#include <signal.h>
#include "serial.h"

#include <nto_procfs.h>

/* Given the first and last register number, figure out the size/len
   of the Neutrino register save area to ask for/tell about. Also set
   the register set that's being dealt with in *subcmd. Watch out for
   the range crossing a register set boundry. */

unsigned
qnx_cpu_register_area(first_regno, last_regno, subcmd, off, len)
    unsigned first_regno;
    unsigned last_regno;
    unsigned char *subcmd;
    unsigned *off;
    unsigned *len;
{
	if (last_regno > ARM_PS_REGNUM) {
		last_regno = ARM_PS_REGNUM;
	}

	*subcmd = DSMSG_REG_GENERAL;

	if (first_regno < ARM_F0_REGNUM) {
		*off = first_regno * 4;

		/*
		 * Nto has psr immediately after general registers
		 */
		if (last_regno == ARM_PS_REGNUM) {
			*len = (17 - first_regno) * 4;
		} else {
			*len = (last_regno - first_regno + 1) * 4;
		}
		return last_regno;
	}
	if (first_regno == ARM_PS_REGNUM) {
		*off = 16 * 4;
		*len = 4;
		return last_regno;
	}

	/*
	 * We don't support the FPA registers.
	 * FIXME: this needs revisiting once we implement VFP etc.
	 */
	*len = 0;
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
	unsigned	*dst = data;
	void		*reg;


	while (first_regno <= last_regno) {
		reg = &registers[REGISTER_BYTE(first_regno)];

		/*
		 * FIXME: endian?
		 */
		memcpy(dst, reg, REGISTER_RAW_SIZE(first_regno));

		/*
		 * Nto puts psr immediately after the general registers.
		 * FIXME: this needs revisiting once we implement VFP etc.
		 */
		if (++first_regno == ARM_F0_REGNUM) {
			first_regno = ARM_PS_REGNUM;
		}
		++dst;
	}
}

void
nto_supply_gregset (gregsetp)
     nto_gregset_t *gregsetp;
{
  ARM_CPU_REGISTERS *regp = &gregsetp->arm;
  int regi;

  for(regi = ARM_A1_REGNUM; regi < ARM_F0_REGNUM; regi++)
    supply_register(regi, (char *)&regp->gpr[regi]);

  supply_register(ARM_PS_REGNUM, (char *)&regp->spsr);
}

void
nto_supply_fpregset (fpregsetp)
     nto_fpregset_t *fpregsetp;
{
/* NYI in OS */
}

enum QNX_REGS { QNX_REGS_GP = 0, QNX_REGS_FP, QNX_REGS_ALT, QNX_REGS_END };

unsigned
qnx_get_regset_id( int regno )
{
  if( regno == -1 )
    return QNX_REGS_END;
  else if (regno < ARM_F0_REGNUM || regno == ARM_FPS_REGNUM || regno == ARM_PS_REGNUM)
    return QNX_REGS_GP;
  else if (regno >= ARM_F0_REGNUM && regno <= ARM_F7_REGNUM)
    return QNX_REGS_FP;
  else if (0) /* I don't think this version can ask for alt regs yet */
    return QNX_REGS_ALT;
  return -1;
}

void
qnx_cpu_supply_regset( int endian, int regset, void *data )
{
  switch( regset ){
  case QNX_REGS_GP:
    nto_supply_gregset( (nto_gregset_t *)data );
    break;
  case QNX_REGS_FP:
    nto_supply_fpregset( (nto_fpregset_t *)data );
    break;
  case QNX_REGS_ALT:
    break;
  default:
  }
}

unsigned
qnx_get_regset_area( unsigned regset, char *subcmd )
{
  switch( regset ){
  case QNX_REGS_GP:
    *subcmd = DSMSG_REG_GENERAL;
    return sizeof(ARM_CPU_REGISTERS);
    break;
  case QNX_REGS_FP:
    *subcmd = DSMSG_REG_FLOAT;
    return sizeof(ARM_FPU_REGISTERS);
    break;
  case QNX_REGS_ALT:
    *subcmd = DSMSG_REG_ALT;
    return sizeof(ARM_ALT_REGISTERS);
    break;
  default:
    return 0;
  }
}

#include "solib-svr4.h"		/* For struct link_map_offsets.  */

struct link_map_offsets *
arm_qnx_svr4_fetch_link_map_offsets (void)
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

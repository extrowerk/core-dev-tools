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

/* This file was derived from remote.c. It implements the PPC specific
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
#include <stddef.h>
#include "ppc-tdep.h"
#include "regcache.h"

#include <nto_procfs.h>

#include "dcache.h"

#ifdef USG
#include <sys/types.h>
#endif

#include <signal.h>
#include "serial.h"


/* Given the first and last register number, figure out the size/len
   of the Neutrino register save area to ask for/tell about. Also set
   the register set that's being dealt with in *subcmd. Watch out for
   the range crossing a register set boundry. */

unsigned qnx_cpu_register_area(first_regno, last_regno, subcmd, off, len)
unsigned first_regno;
unsigned last_regno;
unsigned char *subcmd;
unsigned *off;
unsigned *len;
{
	int size;
	struct gdbarch_tdep *tdep = gdbarch_tdep(current_gdbarch);

	if (first_regno >= tdep->ppc_gp0_regnum && first_regno < FP0_REGNUM) {
		if (last_regno >= FP0_REGNUM)
			last_regno= FP0_REGNUM - 1;
		if (subcmd)
			*subcmd= DSMSG_REG_GENERAL;
		size= sizeof(unsigned);
	} else if (first_regno >= FP0_REGNUM && first_regno <= FPLAST_REGNUM) {
		if (last_regno > FPLAST_REGNUM)
			last_regno= FPLAST_REGNUM;
		if (subcmd)
			*subcmd= DSMSG_REG_FLOAT;
		size= sizeof(double);
	} else if (first_regno >= FIRST_UISA_SP_REGNUM && first_regno <= LAST_UISA_SP_REGNUM) {
		if (subcmd)
			*subcmd= DSMSG_REG_GENERAL;
		*len= sizeof(unsigned);

		if(first_regno == gdbarch_pc_regnum(current_gdbarch)){
			*off= offsetof(PPC_CPU_REGISTERS, iar);
		}
		else if(first_regno == tdep->ppc_ps_regnum){
			*off= offsetof(PPC_CPU_REGISTERS, msr);
		}
		else if(first_regno == tdep->ppc_cr_regnum){
			*off= offsetof(PPC_CPU_REGISTERS, cr);
		}
		else if(first_regno == tdep->ppc_lr_regnum){
			*off= offsetof(PPC_CPU_REGISTERS, lr);
		}
		else if(first_regno == tdep->ppc_ctr_regnum){
			*off= offsetof(PPC_CPU_REGISTERS, ctr);
		}
		else if(first_regno == tdep->ppc_xer_regnum){
			*off= offsetof(PPC_CPU_REGISTERS, xer);
		}
		else if(first_regno == tdep->ppc_mq_regnum){
			*off= offsetof(PPC_CPU_REGISTERS, mq);
		}
		else{
			*len= 0;
		}
		return first_regno;
	} else {
		*len= 0;
		return last_regno;
	}
	if (first_regno >= FP0_REGNUM && first_regno <= FPLAST_REGNUM) {
		*off= (first_regno - FP0_REGNUM) * size;
	} else {
		*off= first_regno * size;
	}
	*len= (last_regno - first_regno + 1) * size;
	return last_regno;
}

/*-
	build the neutrino register set info into the 'data' buffer
	(this is for gdb sending registers to the neutrino target)
 */
int qnx_cpu_register_store(endian, first_regno, last_regno, data)
int endian;
unsigned first_regno;
unsigned last_regno;
void *data;
{
	char		*dest;

	dest = data;
	if (first_regno >= FP0_REGNUM && first_regno <= FPLAST_REGNUM) {
		return 0;
	}
	for (; first_regno <= last_regno; first_regno++) {
		regcache_collect(first_regno, dest);
		qnx_swap32(dest);
		dest += 4;
	}
	return 1;
}

enum QNX_REGS { QNX_REGS_GP = 0, QNX_REGS_FP, QNX_REGS_ALT, QNX_REGS_END };

void
nto_supply_gregset (gregsetp)
     nto_gregset_t *gregsetp;
{
	struct gdbarch_tdep *tdep = gdbarch_tdep(current_gdbarch);
	PPC_CPU_REGISTERS *regp = &gregsetp->ppc;
	int regi;

	for (regi = tdep->ppc_gp0_regnum; regi <= tdep->ppc_gplast_regnum; regi++)
		supply_register(regi, (char *)(&regp->gpr[regi]));

	/* fill in between FIRST_UISA_SP_REGNUM and LAST_UISA_SP_REGNUM */
	supply_register (tdep->ppc_ctr_regnum, (char *) (&regp->ctr));
	supply_register (tdep->ppc_lr_regnum, (char *) (&regp->lr));
	supply_register (tdep->ppc_ps_regnum, (char *) (&regp->msr));
	supply_register (PC_REGNUM, (char *) (&regp->iar));
	supply_register (tdep->ppc_cr_regnum, (char *) (&regp->cr));
	supply_register (tdep->ppc_xer_regnum, (char *) (&regp->xer));
	/* supply_register (tdep->???, (char *) (&regp->ear)); */
	/* FIXME: mq is only on the 601 - should we check? */
	if(tdep->ppc_mq_regnum != -1)
		supply_register (tdep->ppc_mq_regnum, (char *) (&regp->mq));
	if(tdep->ppc_vrsave_regnum != -1)
		supply_register (tdep->ppc_vrsave_regnum, (char *) (&regp->vrsave));
}

void
nto_supply_fpregset (fpregsetp)
     nto_fpregset_t *fpregsetp;
{
  int regi;
  PPC_FPU_REGISTERS *regp = &fpregsetp->ppc;

  for (regi = 0; regi < 32; regi++)
    supply_register (FP0_REGNUM + regi, (char *) (&regp->fpr[regi]));
  /* FIXME: do we need to do something with fpscr? */
}

unsigned
qnx_get_regset_id( int regno )
{
  struct gdbarch_tdep *tdep = gdbarch_tdep(current_gdbarch);

  if( regno == -1 )
    return QNX_REGS_END;
  else if (regno >= FP0_REGNUM && regno <= FPLAST_REGNUM)
    return QNX_REGS_FP;
  else if (regno >= tdep->ppc_vr0_regnum && regno < tdep->ppc_vrsave_regnum)
    return QNX_REGS_ALT;
  else if (regno <= tdep->ppc_gplast_regnum || ( regno >= FIRST_UISA_SP_REGNUM && regno <= LAST_UISA_SP_REGNUM))
    return QNX_REGS_GP;
  return -1;
}

/* get regset characteristics */
unsigned qnx_get_regset_area( unsigned regset, char *subcmd )
{
  unsigned length = 0;
  switch( regset ){
    case QNX_REGS_GP:
      *subcmd = DSMSG_REG_GENERAL;
      length = sizeof(PPC_CPU_REGISTERS); 
      break;
    case QNX_REGS_FP:
      *subcmd = DSMSG_REG_FLOAT;
      length = sizeof(PPC_FPU_REGISTERS);
      break;
    case QNX_REGS_ALT:
      *subcmd = DSMSG_REG_ALT;
      length = sizeof(PPC_ALT_REGISTERS);
    default:
      length = 0;
  }
  return length;
}

void
qnx_cpu_supply_regset(int endian, int regset, void *data)
{
	endian = endian; /* ppc is always be */
	
	switch( regset ){
	case QNX_REGS_GP:
		nto_supply_gregset((nto_gregset_t *)data);
		break;
	case QNX_REGS_FP:
		nto_supply_fpregset((nto_fpregset_t *)data);
		break;
	case QNX_REGS_ALT:
		break;
	default: /* do nothing */
	}
}

#include "solib-svr4.h"		/* For struct link_map_offsets.  */

struct link_map_offsets *
ppc_qnx_svr4_fetch_link_map_offsets (void)
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

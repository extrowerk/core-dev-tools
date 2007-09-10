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

/* This file was derived from remote.c. It implements the SH specific
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

#include <assert.h>
#include <nto_procfs.h>

#include "dcache.h"

#ifdef USG
#include <sys/types.h>
#endif

#include <signal.h>
#include "serial.h"

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

int qnx_get_regset_id( int regno )
{
	int regset;
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch); 
	
	if ( regno == -1 ) {
	    regset  = QNX_REGS_END;
	} else if( regno <= tdep->SR_REGNUM ) {
		regset = QNX_REGS_GP;
	} else if( regno >= tdep->FPUL_REGNUM && regno <= tdep->FP_LAST_REGNUM){
		regset =  QNX_REGS_FP;
	} else {
		regset =  -1; /* error */
	}
	return regset;
}

static void gp_supply_regset( SH_CPU_REGISTERS *data )
{
    unsigned first_regno;
    int minusone = -1;
    	for( first_regno = 0; first_regno <  16; first_regno++) {
	    	supply_register( first_regno, (char*)&data->gr[first_regno]);
	}
	supply_register( SR_REGNUM, (char *)&data->sr );
	supply_register( gdbarch_pc_regnum(current_gdbarch), (char*)&data->pc );
	supply_register( GBR_REGNUM, (char*)&data->gbr );
	supply_register( MACH_REGNUM, (char*)&data->mach );
	supply_register( MACL_REGNUM, (char*)&data->macl);
	supply_register( PR_REGNUM, (char*)&data->pr );
	supply_register( VBR_REGNUM, (char*)&minusone );
}
/* get fpu state and regs */
static void fp_supply_regset( SH_FPU_REGISTERS *data )
{
    unsigned first_regno;
    int minusone = -1;
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch); 
    
    for( first_regno = 0; first_regno < 16; first_regno++) {
		supply_register( tdep->FPSCR_REGNUM + 1 + first_regno,
				(char *)&data->fpr_bank0[first_regno]);
	}
    	supply_register( tdep->FPUL_REGNUM, (char *)&data->fpul );
    	supply_register( tdep->FPSCR_REGNUM, (char*)&data->fpscr );
}

void qnx_cpu_supply_regset( int endian, int regset, void *data)
{
	endian = endian;

	switch( regset ){
	case QNX_REGS_GP: /* QNX has dififerent ordering of GP regs GDB */
		gp_supply_regset( (SH_CPU_REGISTERS *)data );
		break;
	case QNX_REGS_FP:
		fp_supply_regset( (SH_FPU_REGISTERS *)data );
		break;
	default: /* do nothing for now */
	}
}
/* get regset characteristics */
unsigned qnx_get_regset_area( unsigned regset, char *subcmd )
{
	unsigned length;
	switch( regset ){
	case QNX_REGS_GP:
		*subcmd = DSMSG_REG_GENERAL;
		length = sizeof(SH_CPU_REGISTERS); 
		break;
	case QNX_REGS_FP:
		*subcmd = DSMSG_REG_FLOAT;
		length = sizeof(SH_FPU_REGISTERS);
		break;
	default:
		length = 0;
	}
	return length;
}

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
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch); 


/* @@@@ FIXME!!! - how are the R0B0 things mapped with gdbarch? */
    if ( first_regno >= R0_REGNUM && first_regno < gdbarch_pc_regnum(current_gdbarch) /*||
	    (first_regno >= R0B0_REGNUM && first_regno < R0B1_REGNUM)*/ ) {
	if ( last_regno >= gdbarch_pc_regnum(current_gdbarch) )
	    last_regno = gdbarch_pc_regnum(current_gdbarch)-1;
	if (subcmd)
	    *subcmd= DSMSG_REG_GENERAL;
	size = sizeof(shint);
    }
    else if ( first_regno >= gdbarch_pc_regnum(current_gdbarch) && first_regno < tdep->FPUL_REGNUM ) {
	if (subcmd)
	    *subcmd= DSMSG_REG_GENERAL;
	*len = sizeof(shint);
	switch(first_regno) {
	case 16: /* PC_REGNUM FIXME!!! */ 
	    *off = offsetof(SH_CPU_REGISTERS, pc);
	    break;
	case PR_REGNUM:
	    *off = offsetof(SH_CPU_REGISTERS, pr);
	    break;
	case GBR_REGNUM:
	    *off = offsetof(SH_CPU_REGISTERS, gbr);
	    break;
	case MACH_REGNUM:
	    *off = offsetof(SH_CPU_REGISTERS, mach);
	    break;
	case MACL_REGNUM:
	    *off = offsetof(SH_CPU_REGISTERS, macl);
	    break;
	case SR_REGNUM:
	    *off = offsetof(SH_CPU_REGISTERS, sr);
	    break;
	case VBR_REGNUM:
	default:
	    *len = 0;
	    return first_regno;
	}
	return first_regno;
    }
    else { /* no FP support yet */
	    *len = 0;
	    return last_regno;
    }
    *off = first_regno * size;
    *len = (last_regno - first_regno + 1) * size;
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
void		*regp;
char		*dest;
unsigned	size;
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch); 

	dest = data;
	if (first_regno >= tdep->FPUL_REGNUM /* FIXME CB && first_regno < R0B0_REGNUM */) {
		return 0;
	}
/* FIXME CB 	if ( first_regno >= R0B1_REGNUM )
		return 0;*/
	for (; first_regno <= last_regno; first_regno++) {
		regp= &registers[REGISTER_BYTE(first_regno)];
		size= REGISTER_RAW_SIZE(first_regno);

		memcpy(dest, regp, size);
		qnx_swap32(dest);
		dest += 4;
	}
	return 1;
}

void
nto_supply_gregset (gregsetp)
     nto_gregset_t *gregsetp;
{
  /*
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch); 
	qnx_supply_register(R0_REGNUM, VBR_REGNUM-1, (char *)gregsetp );
	qnx_supply_register(MACH_REGNUM, tdep->FPUL_REGNUM-1, (char *)gregsetp );
  */
  gp_supply_regset(&gregsetp->sh);
}

void
nto_supply_fpregset (fpregsetp)
     nto_fpregset_t *fpregsetp;
{
  /*
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch); 
  qnx_supply_register(gdbarch_fp_regnum(current_gdbarch), tdep->FP_LAST_REGNUM, (char *)fpregsetp);
  */
  fp_supply_regset(&fpregsetp->sh);
}

void
nto_sh_frame_find_saved_regs (fi, fsr)
     struct frame_info *fi;
     struct frame_saved_regs *fsr;
{
	/* DANGER!  This is ONLY going to work if the char buffer format of
	the saved registers is byte-for-byte identical to the
	CORE_ADDR regs[NUM_REGS] format used by struct frame_saved_regs! */

	/* This is a hack to see if we can return an empty set of regs 
	   when we are not connected, otherwise call the regular fun'n */

//	if((inferior_pid == NULL) || (fi->pc == 0))
//	if(target_has_execution)
//		sh_frame_find_saved_regs (fi, fsr);
	return;
}

#include "solib-svr4.h"		/* For struct link_map_offsets.  */

struct link_map_offsets *
sh_qnx_svr4_fetch_link_map_offsets (void)
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

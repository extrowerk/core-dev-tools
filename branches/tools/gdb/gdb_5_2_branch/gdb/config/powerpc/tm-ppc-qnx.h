/* Macro definitions for Power PC running embedded ABI.
   Copyright 1995 Free Software Foundation, Inc.

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

#ifndef TM_PPC_QNX_H
#define TM_PPC_QNX_H

#define SOLIB_PROCESSOR "ppcbe"

/* Include generic QNX NTO header file.  Undef SKIP_TRAMPOLINE_CODE before
   including tm-rs6000.h, as we want the rs6000 definition, not the generic 
   one from tm-sysv4.h, included by tm-qnxnto.h. */
#include "tm-qnxnto.h"
#undef SKIP_TRAMPOLINE_CODE

/* Use generic RS6000 definitions. */
#include "rs6000/tm-rs6000.h"

#undef  FRAME_CHAIN_VALID
#define FRAME_CHAIN_VALID(FP, FRAME) generic_func_frame_chain_valid (FP, FRAME)
extern int generic_func_frame_chain_valid PARAMS ((CORE_ADDR, struct frame_info *));

/* Use target_specific function to define link map offsets.  */
extern struct link_map_offsets *ppc_qnx_svr4_fetch_link_map_offsets (void);
#define SVR4_FETCH_LINK_MAP_OFFSETS() ppc_qnx_svr4_fetch_link_map_offsets ()

#define HANDLE_SVR4_EXEC_EMULATORS 1
#include "solib.h"

#define __QNXTARGET__
#define QNX_TARGET_CPUTYPE CPUTYPE_PPC

#undef	DEFAULT_LR_SAVE
#define	DEFAULT_LR_SAVE 4	/* eabi saves LR at 4 off of SP */

#define GDB_TARGET_POWERPC
#undef IBM6000_TARGET

#undef PC_LOAD_SEGMENT
#undef PROCESS_LINENUMBER_HOOK

#undef TEXT_SEGMENT_BASE
#define TEXT_SEGMENT_BASE 1

#undef NO_SINGLE_STEP

/* We can single step on NTO */
#undef  SOFTWARE_SINGLE_STEP
#define SOFTWARE_SINGLE_STEP(p,q) abort() /* Will never execute! */
#undef  SOFTWARE_SINGLE_STEP_P
#define SOFTWARE_SINGLE_STEP_P() 0

/* Neutrino supports the PowerPC hardware debugging registers?  */

#define TARGET_HAS_HARDWARE_WATCHPOINTS

#define TARGET_CAN_USE_HARDWARE_WATCHPOINT(type, cnt, ot) 1

/* After a watchpoint trap, the PC points to the instruction after
   the one that caused the trap.  Therefore we don't need to step over it.
   But we do need to reset the status register to avoid another trap.  */
#define HAVE_CONTINUABLE_WATCHPOINT

#define STOPPED_BY_WATCHPOINT(W)  \
  qnx_stopped_by_watchpoint()

/* Use these macros for watchpoint insertion/removal.  */

#define target_insert_watchpoint(addr, len, type)  \
  qnx_insert_hw_watchpoint ( addr, len, type)

#define target_remove_watchpoint(addr, len, type)  \
  qnx_remove_hw_watchpoint ( addr, len, type)

/* Use these macros for watchpoint insertion/removal.  */

#define target_remove_hw_breakpoint(ADDR,SHADOW) \
  qnx_remove_hw_breakpoint(ADDR,SHADOW)

#define target_insert_hw_breakpoint(ADDR,SHADOW) \
  qnx_insert_hw_breakpoint(ADDR,SHADOW)

#define target_pid_to_str(PTID) \
	qnx_pid_to_str(PTID)
extern char *qnx_pid_to_str PARAMS ((ptid_t ptid));

#define FIND_NEW_THREADS qnx_find_new_threads
void qnx_find_new_threads PARAMS ((void));

/* Use .ntoppc-gdbinit */
#define EXTRA_GDBINIT_FILENAME ".ntoppc-gdbinit"

#endif /* TM_PPC_QNX_H */

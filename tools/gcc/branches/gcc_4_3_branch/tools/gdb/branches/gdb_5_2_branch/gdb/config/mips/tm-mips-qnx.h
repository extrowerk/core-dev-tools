/* Copyright (C) 1993 Free Software Foundation, Inc.

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

#ifndef TM_MIPS_QNX_H
#define TM_MIPS_QNX_H

#define QNX_TARGET_CPUTYPE CPUTYPE_MIPS

#define TARGET_BYTE_ORDER_SELECTABLE

#include "mips/tm-tx79.h"

#include "nto_inttypes.h"
#undef IN_SOLIB_CALL_TRAMPOLINE
#undef SKIP_TRAMPOLINE_CODE

/* Include the common Neutrino target definitions. */
#include "tm-qnxnto.h"
#include "regcache.h"

#undef R5900_128BIT_GPR_HACK

/* We want QNX's mips GDB to default to the tx79, which has the largest
   register set and reg sizes, so we will have allocated large enough
   data structures, regardless of which one we switch to after that.  */

#undef DEFAULT_MIPS_TYPE
#define DEFAULT_MIPS_TYPE "tx79"

#define QNX_SET_PROCESSOR_TYPE(cpuid) qnx_set_processor_type(cpuid)
extern void qnx_set_processor_type PARAMS((uint32_t cpuid));

#if 0
/* Use the alternate method of determining valid frame chains. */
#undef  FRAME_CHAIN_VALID
#define FRAME_CHAIN_VALID(FP, FRAME) func_frame_chain_valid (FP, FRAME)
#endif

/* Define DO_REGISTERS_INFO() to do machine-specific formatting
   of register dumps. */

#undef DO_REGISTERS_INFO
#define DO_REGISTERS_INFO(_regnum, fp) mips_do_registers_info(_regnum, fp)
extern void mips_do_registers_info PARAMS ((int, int));

#define NUM_TX79_REGS 150
#undef NUM_REGS
#define NUM_REGS mips_num_regs()
extern int mips_num_regs();

#undef GDB_TARGET_IS_MIPS64 
#define GDB_TARGET_IS_MIPS64 mips_gdb_target_is_mips64()
extern int mips_gdb_target_is_mips64();

#undef REGISTER_VIRTUAL_TYPE
#define REGISTER_VIRTUAL_TYPE(N) mips_register_virtual_type(N)
struct type *mips_register_virtual_type();

#undef MIPS_REGSIZE
#define MIPS_REGSIZE mips_reg_size()
extern int mips_reg_size();

#undef MAX_REGISTER_RAW_SIZE
#define MAX_REGISTER_RAW_SIZE mips_max_register_raw_size()
#undef MAX_REGISTER_VIRTUAL_SIZE
#define MAX_REGISTER_VIRTUAL_SIZE mips_max_register_raw_size()
extern int mips_max_register_raw_size();

/* Use .ntomips-gdbinit */
#define EXTRA_GDBINIT_FILENAME ".ntomips-gdbinit"

#define SOLIB_PROCESSOR_BE "mipsbe"
#define SOLIB_PROCESSOR_LE "mipsle"

/* Use target_specific function to define link map offsets.  */

extern struct link_map_offsets *mips_qnx_svr4_fetch_link_map_offsets (void);
#define SVR4_FETCH_LINK_MAP_OFFSETS() \
  mips_qnx_svr4_fetch_link_map_offsets ()
#define HANDLE_SVR4_EXEC_EMULATORS 1
#include "solib.h"

#endif //TM_MIPS_QNX_H

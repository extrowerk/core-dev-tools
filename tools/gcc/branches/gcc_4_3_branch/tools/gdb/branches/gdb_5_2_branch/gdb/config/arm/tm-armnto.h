/* Target definitions for QNX/Neutrino on ARM, for GDB.
   Copyright 1999, 2000 Free Software Foundation, Inc.

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
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#ifndef TM_NTOARM_H
#define TM_NTOARM_H

#define QNX_TARGET_CPUTYPE CPUTYPE_ARM

#define TARGET_BYTE_ORDER_SELECTABLE

/* Include the common ARM target definitions.  */
#include "arm/tm-arm.h"

/* Include the common Neutrino target definitions. */
#include "regcache.h"
#include "tm-qnxnto.h"

/* Use the alternate method of determining valid frame chains. */
#define FRAME_CHAIN_VALID(fp,fi) func_frame_chain_valid (fp, fi)

/* Use target_specific function to define link map offsets.  */
extern struct link_map_offsets *arm_qnx_svr4_fetch_link_map_offsets (void);
#define SVR4_FETCH_LINK_MAP_OFFSETS() arm_qnx_svr4_fetch_link_map_offsets ()

#define SOLIB_PROCESSOR_LE "armle"
#define SOLIB_PROCESSOR_BE "armbe"
#define HANDLE_SVR4_EXEC_EMULATORS 1
#include "solib.h"

/* Use .ntoarm-gdbinit */
#define EXTRA_GDBINIT_FILENAME ".ntoarm-gdbinit"

/* Define this so that we don't use the double_littlebyte_bigword conversion
 * functions */
#define ARM_FLOAT_MODEL ARM_FLOAT_SOFT_VFP

#endif /* TM_NTOARM_H */


/* Target-specific definition for a Hitachi Super-H.
   Copyright (C) 1993 Free Software Foundation, Inc.

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

/* Contributed by Steve Chamberlain sac@cygnus.com */

#ifndef TM_SH_NTO_H
#define TM_SH_NTO_H

/* Use target_specific function to define link map offsets.  */
extern struct link_map_offsets *sh_qnx_svr4_fetch_link_map_offsets (void);
#define SVR4_FETCH_LINK_MAP_OFFSETS() sh_qnx_svr4_fetch_link_map_offsets ()

#define TARGET_BYTE_ORDER_SELECTABLE
#define SOLIB_PROCESSOR_LE "shle"
#define SOLIB_PROCESSOR_BE "shbe"
#define HANDLE_SVR4_EXEC_EMULATORS 1
#include "solib.h"

#include "sh/tm-sh.h"
#include "regcache.h"

/* Include the common Neutrino target definitions. */
#include "tm-qnxnto.h"

#define QNX_TARGET_CPUTYPE CPUTYPE_SH

#undef  FRAME_CHAIN_VALID
#define FRAME_CHAIN_VALID(FP, FRAME) generic_func_frame_chain_valid (FP, FRAME)
extern int generic_func_frame_chain_valid PARAMS ((CORE_ADDR, struct frame_info *));

#undef BIG_REMOTE_BREAKPOINT
#define BIG_REMOTE_BREAKPOINT    { 0xff, 0xfd }
#undef LITTLE_REMOTE_BREAKPOINT
#define LITTLE_REMOTE_BREAKPOINT { 0xfd, 0xff }

/* Use .ntosh-gdbinit as well as .gdbinit. */
#define EXTRA_GDBINIT_FILENAME ".ntosh-gdbinit"

#endif /* TM_SH_NTO_H */


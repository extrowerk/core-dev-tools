/* Target machine sub-description for QNX Neutrino version 6.
   This is included by other tm-*.h files to specify nto specific
   stuff.  Copyright 2002 Free Software Foundation, Inc.

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

#define __QNXTARGET__
#include "tm-sysv4.h"

#define FIND_NEW_THREADS qnx_find_new_threads
void qnx_find_new_threads PARAMS ((void));

#define target_pid_to_str(PTID) \
	qnx_pid_to_str(PTID)
extern char *qnx_pid_to_str PARAMS ((ptid_t ptid));

/* Setup the valid realtime signal range. */
#define REALTIME_LO 41
#define REALTIME_HI 56

/* Set up the undefined useable signals. */
#define RAW_SIGNAL_LO 32
#define RAW_SIGNAL_HI (REALTIME_LO - 1)

#define TARGET_SIGNAL_RAW_VALUES \
TARGET_SIGNAL_RAW0, \
TARGET_SIGNAL_RAW1, \
TARGET_SIGNAL_RAW2, \
TARGET_SIGNAL_RAW3, \
TARGET_SIGNAL_RAW4, \
TARGET_SIGNAL_RAW5, \
TARGET_SIGNAL_RAW6, \
TARGET_SIGNAL_RAW7, \
TARGET_SIGNAL_RAW8

#define TARGET_SIGNAL_RAW_TABLE \
{"SIGNAL32", "Signal 32"}, \
{"SIGNAL33", "Signal 33"}, \
{"SIGNAL34", "Signal 34"}, \
{"SIGNAL35", "Signal 35"}, \
{"SIGNAL36", "Signal 36"}, \
{"SIGNAL37", "Signal 37"}, \
{"SIGNAL38", "Signal 38"}, \
{"SIGNAL39", "Signal 39"}, \
{"SIGNAL40", "Signal 40"}

/* See solib-svr4.c
 * This works around a problem with static binaries.  An internal breakpoint
 * is set on _start but for whatever reason, gdb doesn't handle it properly
 * and claims a SIGTRAP has been raised on the inferior.  If we move the
 * shlib_event breakpoint ahead to main (this already happened by default
 * on sh for some reason), then it works.  FIXME: we need to discover why
 * gdb isn't handling breakpoints properly at the early stages of execution. */
#define SOLIB_BKPT_NAME "main"

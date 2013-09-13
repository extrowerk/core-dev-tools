/* nto-signals.c - QNX Neutrino signal translation.

   Copyright (C) 2009 Free Software Foundation, Inc.

   Contributed by QNX Software Systems Ltd.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* Nto signal to gdb's enum target_signal translation. */

/* On hosts other than neutrino, signals may differ. */

#include "defs.h"
#include "nto-signals.h"
#include "target.h"

/* For checking 1-1 mapping. */
#define NTO_SIGSEGV     11  /* segmentation violation */
#define NTO_SIGSELECT   57


/* Convert nto signal to gdb signal.  */
enum gdb_signal
gdb_signal_from_nto(struct gdbarch *gdbarch, int sig)
{
  /* 1-1 mapping via qnx_signals.def */
  gdb_assert (NTO_SIGSEGV == GDB_SIGNAL_SEGV
	      && NTO_SIGSELECT == GDB_SIGNAL_SELECT);
  if (sig == 0)
    return GDB_SIGNAL_0;
  if (sig > GDB_SIGNAL_LAST)
    {
      warning (_("Signal %d:%s does not exist on this system."),
	       sig, gdb_signal_to_name (sig));
      return 0;
    }
  else
    return sig; /* 1-1 maping via qnx_signals.def */
}

/* Convert gdb signal to nto signal.  */

int
gdb_signal_to_nto(struct gdbarch *gdbarch, enum gdb_signal sig)
{
  int i;
  if (sig == 0)
    return 0;

  return sig; /* 1-1 mapping via qnx_signals.def */
}


/* nto-signals.h - QNX Neutrino signal translation.

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

enum gdb_signal gdb_signal_from_nto(struct gdbarch *gdbarch, int sig);

int gdb_signal_to_nto(struct gdbarch *gdbarch, enum gdb_signal sig);


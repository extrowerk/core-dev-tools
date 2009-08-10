/* MI Command Set - breakpoint and watchpoint commands.
   Copyright 2000, 2001, 2002 Free Software Foundation, Inc.
   Contributed by Cygnus Solutions (a Red Hat company).

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

#include "defs.h"
#include "mi-cmds.h"
#include "mi-getopt.h"
#include "ui-out.h"
#include "symtab.h"
#include "source.h"

/* Return the current source file for listing and next line to list.
   NOTE: The returned sal pc and end fields are not valid. */
   
struct symtab_and_line
get_current_source_symtab_and_line (void)
{
  struct symtab_and_line cursal;

  cursal.symtab = current_source_symtab;
  cursal.line = current_source_line;
  cursal.pc = 0;
  cursal.end = 0;
  
  return cursal;
}

/* If the current source file for listing is not set, try and get a default.
   Usually called before get_current_source_symtab_and_line() is called.
   It may err out if a default cannot be determined.
   We must be cautious about where it is called, as it can recurse as the
   process of determining a new default may call the caller!
   Use get_current_source_symtab_and_line only to get whatever
   we have without erroring out or trying to get a default. */
   
void
set_default_source_symtab_and_line (void)
{
  struct symtab_and_line cursal;

  if (!have_full_symbols () && !have_partial_symbols ())
    error ("No symbol table is loaded.  Use the \"file\" command.");

  /* Pull in a current source symtab if necessary */
  if (current_source_symtab == 0)
    select_source_symtab (0);
}

/* Return to the client the absolute path and line number of the 
   current file being executed. */

enum mi_cmd_result
mi_cmd_file_list_exec_source_file(char *command, char **argv, int argc)
{
  struct symtab_and_line st;
  int optind = 0;
  char *optarg;
  
  /* Set the default file and line, also get them */
  set_default_source_symtab_and_line();
  st = get_current_source_symtab_and_line();

  /* We should always get a symtab. 
     Apparently, filename does not need to be tested for NULL.
     The documentation in symtab.h suggests it will always be correct */
  if (!st.symtab)
    error ("mi_cmd_file_list_exec_source_file: No symtab");

  /* Extract the fullname if it is not known yet */
  if (st.symtab->fullname == NULL)
    symtab_to_filename (st.symtab);

  /* We may not be able to open the file (not available). */
  if (st.symtab->fullname == NULL)
    error ("mi_cmd_file_list_exec_source_file: File not found");

  /* Print to the user the line, filename and fullname */
  ui_out_field_int (uiout, "line", st.line);
  ui_out_field_string (uiout, "file", st.symtab->filename);
  ui_out_field_string (uiout, "fullname", st.symtab->fullname);

  return MI_CMD_DONE;
}

/* Disassemble support for GDB.

   Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005, 2007, 2008, 2009
   Free Software Foundation, Inc.

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

#include "defs.h"
#include "target.h"
#include "value.h"
#include "ui-out.h"
#include "gdb_string.h"
#include "disasm.h"
#include "gdbcore.h"
#include "dis-asm.h"

/* Disassemble functions.
   FIXME: We should get rid of all the duplicate code in gdb that does
   the same thing: disassemble_command() and the gdbtk variation. */

/* This Structure is used to store line number information.
   We need a different sort of line table from the normal one cuz we can't
   depend upon implicit line-end pc's for lines to do the
   reordering in this function.  */

struct dis_line_entry
{
  int line;
  CORE_ADDR start_pc;
  CORE_ADDR end_pc;
};

/* Like target_read_memory, but slightly different parameters.  */
static int
dis_asm_read_memory (bfd_vma memaddr, gdb_byte *myaddr, unsigned int len,
		     struct disassemble_info *info)
{
  return target_read_memory (memaddr, myaddr, len);
}

/* Like memory_error with slightly different parameters.  */
static void
dis_asm_memory_error (int status, bfd_vma memaddr,
		      struct disassemble_info *info)
{
  memory_error (status, memaddr);
}

/* Like print_address with slightly different parameters.  */
static void
dis_asm_print_address (bfd_vma addr, struct disassemble_info *info)
{
  struct gdbarch *gdbarch = info->application_data;
  print_address (gdbarch, addr, info->stream);
}

static int
compare_lines (const void *mle1p, const void *mle2p)
{
  struct dis_line_entry *mle1, *mle2;
  int val;

  mle1 = (struct dis_line_entry *) mle1p;
  mle2 = (struct dis_line_entry *) mle2p;

  val = mle1->line - mle2->line;

  if (val != 0)
    return val;

  return mle1->start_pc - mle2->start_pc;
}

static int
dump_insns (struct gdbarch *gdbarch, struct ui_out *uiout,
	    struct disassemble_info * di,
	    CORE_ADDR low, CORE_ADDR high,
	    int how_many, int flags, struct ui_stream *stb)
{
  int num_displayed = 0;
  CORE_ADDR pc;

  /* parts of the symbolic representation of the address */
  int unmapped;
  int offset;
  int line;
  struct cleanup *ui_out_chain;

  for (pc = low; pc < high;)
    {
      char *filename = NULL;
      char *name = NULL;

      QUIT;
      if (how_many >= 0)
	{
	  if (num_displayed >= how_many)
	    break;
	  else
	    num_displayed++;
	}
      ui_out_chain = make_cleanup_ui_out_tuple_begin_end (uiout, NULL);
      ui_out_field_core_addr (uiout, "address", gdbarch, pc);

      if (!build_address_symbolic (pc, 0, &name, &offset, &filename,
				   &line, &unmapped))
	{
	  /* We don't care now about line, filename and
	     unmapped. But we might in the future. */
	  ui_out_text (uiout, " <");
	  ui_out_field_string (uiout, "func-name", name);
	  ui_out_text (uiout, "+");
	  ui_out_field_int (uiout, "offset", offset);
	  ui_out_text (uiout, ">:\t");
	}
      else
	ui_out_text (uiout, ":\t");

      if (filename != NULL)
	xfree (filename);
      if (name != NULL)
	xfree (name);

      ui_file_rewind (stb->stream);
      if (flags & DISASSEMBLY_RAW_INSN)
        {
          CORE_ADDR old_pc = pc;
          bfd_byte data;
          int status;
          pc += gdbarch_print_insn (gdbarch, pc, di);
          for (;old_pc < pc; old_pc++)
            {
              status = (*di->read_memory_func) (old_pc, &data, 1, di);
              if (status != 0)
                (*di->memory_error_func) (status, old_pc, di);
              ui_out_message (uiout, 0, " %02x", (unsigned)data);
            }
          ui_out_text (uiout, "\t");
        }
      else
        pc += gdbarch_print_insn (gdbarch, pc, di);
      ui_out_field_stream (uiout, "inst", stb);
      ui_file_rewind (stb->stream);
      do_cleanups (ui_out_chain);
      ui_out_text (uiout, "\n");
    }
  return num_displayed;
}

/* The idea here is to present a source-O-centric view of a
   function to the user.  This means that things are presented
   in source order, with (possibly) out of order assembly
   immediately following.  */
static void
do_mixed_source_and_assembly (struct gdbarch *gdbarch, struct ui_out *uiout,
			      struct disassemble_info *di, int nlines,
			      struct linetable_entry *le,
			      CORE_ADDR low, CORE_ADDR high,
			      struct symtab *symtab,
			      int how_many, int flags, struct ui_stream *stb)
{
  int newlines = 0;
  struct dis_line_entry *mle;
  struct symtab_and_line sal;
  int i;
  int out_of_order = 0;
  int next_line = 0;
  int num_displayed = 0;
  struct cleanup *ui_out_chain;
  struct cleanup *ui_out_tuple_chain = make_cleanup (null_cleanup, 0);
  struct cleanup *ui_out_list_chain = make_cleanup (null_cleanup, 0);

  mle = (struct dis_line_entry *) alloca (nlines
					  * sizeof (struct dis_line_entry));

  /* Copy linetable entries for this function into our data
     structure, creating end_pc's and setting out_of_order as
     appropriate.  */

  /* First, skip all the preceding functions.  */

  for (i = 0; i < nlines - 1 && le[i].pc < low; i++);

  /* Now, copy all entries before the end of this function.  */

  for (; i < nlines - 1 && le[i].pc < high; i++)
    {
      if (le[i].line == le[i + 1].line && le[i].pc == le[i + 1].pc)
	continue;		/* Ignore duplicates */

      /* Skip any end-of-function markers.  */
      if (le[i].line == 0)
	continue;

      mle[newlines].line = le[i].line;
      if (le[i].line > le[i + 1].line)
	out_of_order = 1;
      mle[newlines].start_pc = le[i].pc;
      mle[newlines].end_pc = le[i + 1].pc;
      newlines++;
    }

  /* If we're on the last line, and it's part of the function,
     then we need to get the end pc in a special way.  */

  if (i == nlines - 1 && le[i].pc < high)
    {
      mle[newlines].line = le[i].line;
      mle[newlines].start_pc = le[i].pc;
      sal = find_pc_line (le[i].pc, 0);
      mle[newlines].end_pc = sal.end;
      newlines++;
    }

  /* Now, sort mle by line #s (and, then by addresses within
     lines). */

  if (out_of_order)
    qsort (mle, newlines, sizeof (struct dis_line_entry), compare_lines);

  /* Now, for each line entry, emit the specified lines (unless
     they have been emitted before), followed by the assembly code
     for that line.  */

  ui_out_chain = make_cleanup_ui_out_list_begin_end (uiout, "asm_insns");

  for (i = 0; i < newlines; i++)
    {
      /* Print out everything from next_line to the current line.  */
      if (mle[i].line >= next_line)
	{
	  if (next_line != 0)
	    {
	      /* Just one line to print. */
	      if (next_line == mle[i].line)
		{
		  ui_out_tuple_chain
		    = make_cleanup_ui_out_tuple_begin_end (uiout,
							   "src_and_asm_line");
		  print_source_lines (symtab, next_line, mle[i].line + 1, 0);
		}
	      else
		{
		  /* Several source lines w/o asm instructions associated. */
		  for (; next_line < mle[i].line; next_line++)
		    {
		      struct cleanup *ui_out_list_chain_line;
		      struct cleanup *ui_out_tuple_chain_line;
		      
		      ui_out_tuple_chain_line
			= make_cleanup_ui_out_tuple_begin_end (uiout,
							       "src_and_asm_line");
		      print_source_lines (symtab, next_line, next_line + 1,
					  0);
		      ui_out_list_chain_line
			= make_cleanup_ui_out_list_begin_end (uiout,
							      "line_asm_insn");
		      do_cleanups (ui_out_list_chain_line);
		      do_cleanups (ui_out_tuple_chain_line);
		    }
		  /* Print the last line and leave list open for
		     asm instructions to be added. */
		  ui_out_tuple_chain
		    = make_cleanup_ui_out_tuple_begin_end (uiout,
							   "src_and_asm_line");
		  print_source_lines (symtab, next_line, mle[i].line + 1, 0);
		}
	    }
	  else
	    {
	      ui_out_tuple_chain
		= make_cleanup_ui_out_tuple_begin_end (uiout, "src_and_asm_line");
	      print_source_lines (symtab, mle[i].line, mle[i].line + 1, 0);
	    }

	  next_line = mle[i].line + 1;
	  ui_out_list_chain
	    = make_cleanup_ui_out_list_begin_end (uiout, "line_asm_insn");
	}

      num_displayed += dump_insns (gdbarch, uiout, di,
				   mle[i].start_pc, mle[i].end_pc,
				   how_many, flags, stb);

      /* When we've reached the end of the mle array, or we've seen the last
         assembly range for this source line, close out the list/tuple.  */
      if (i == (newlines - 1) || mle[i + 1].line > mle[i].line)
	{
	  do_cleanups (ui_out_list_chain);
	  do_cleanups (ui_out_tuple_chain);
	  ui_out_tuple_chain = make_cleanup (null_cleanup, 0);
	  ui_out_list_chain = make_cleanup (null_cleanup, 0);
	  ui_out_text (uiout, "\n");
	}
      if (how_many >= 0 && num_displayed >= how_many)
	break;
    }
  do_cleanups (ui_out_chain);
}


static void
do_assembly_only (struct gdbarch *gdbarch, struct ui_out *uiout,
		  struct disassemble_info * di,
		  CORE_ADDR low, CORE_ADDR high,
		  int how_many, int flags, struct ui_stream *stb)
{
  int num_displayed = 0;
  struct cleanup *ui_out_chain;

  ui_out_chain = make_cleanup_ui_out_list_begin_end (uiout, "asm_insns");

  num_displayed = dump_insns (gdbarch, uiout, di, low, high, how_many,
                              flags, stb);

  do_cleanups (ui_out_chain);
}

/* Initialize the disassemble info struct ready for the specified
   stream.  */

static int ATTR_FORMAT (printf, 2, 3)
fprintf_disasm (void *stream, const char *format, ...)
{
  va_list args;
  va_start (args, format);
  vfprintf_filtered (stream, format, args);
  va_end (args);
  /* Something non -ve.  */
  return 0;
}

static struct disassemble_info
gdb_disassemble_info (struct gdbarch *gdbarch, struct ui_file *file)
{
  struct disassemble_info di;
  init_disassemble_info (&di, file, fprintf_disasm);
  di.flavour = bfd_target_unknown_flavour;
  di.memory_error_func = dis_asm_memory_error;
  di.print_address_func = dis_asm_print_address;
  /* NOTE: cagney/2003-04-28: The original code, from the old Insight
     disassembler had a local optomization here.  By default it would
     access the executable file, instead of the target memory (there
     was a growing list of exceptions though).  Unfortunately, the
     heuristic was flawed.  Commands like "disassemble &variable"
     didn't work as they relied on the access going to the target.
     Further, it has been supperseeded by trust-read-only-sections
     (although that should be superseeded by target_trust..._p()).  */
  di.read_memory_func = dis_asm_read_memory;
  di.arch = gdbarch_bfd_arch_info (gdbarch)->arch;
  di.mach = gdbarch_bfd_arch_info (gdbarch)->mach;
  di.endian = gdbarch_byte_order (gdbarch);
  di.endian_code = gdbarch_byte_order_for_code (gdbarch);
  di.application_data = gdbarch;
  disassemble_init_for_target (&di);
  return di;
}

void
gdb_disassembly (struct gdbarch *gdbarch, struct ui_out *uiout,
		char *file_string,
		int flags,
		int how_many, CORE_ADDR low, CORE_ADDR high)
{
  struct ui_stream *stb = ui_out_stream_new (uiout);
  struct cleanup *cleanups = make_cleanup_ui_out_stream_delete (stb);
  struct disassemble_info di = gdb_disassemble_info (gdbarch, stb->stream);
  /* To collect the instruction outputted from opcodes. */
  struct symtab *symtab = NULL;
  struct linetable_entry *le = NULL;
  int nlines = -1;

  /* Assume symtab is valid for whole PC range */
  symtab = find_pc_symtab (low);

  if (symtab != NULL && symtab->linetable != NULL)
    {
      /* Convert the linetable to a bunch of my_line_entry's.  */
      le = symtab->linetable->item;
      nlines = symtab->linetable->nitems;
    }

  if (!(flags & DISASSEMBLY_SOURCE) || nlines <= 0
      || symtab == NULL || symtab->linetable == NULL)
    do_assembly_only (gdbarch, uiout, &di, low, high, how_many, flags, stb);

  else if (flags & DISASSEMBLY_SOURCE)
    do_mixed_source_and_assembly (gdbarch, uiout, &di, nlines, le, low,
				  high, symtab, how_many, flags, stb);

  do_cleanups (cleanups);
  gdb_flush (gdb_stdout);
}

/* Print the instruction at address MEMADDR in debugged memory,
   on STREAM.  Returns the length of the instruction, in bytes,
   and, if requested, the number of branch delay slot instructions.  */

int
gdb_print_insn (struct gdbarch *gdbarch, CORE_ADDR memaddr,
		struct ui_file *stream, int *branch_delay_insns)
{
  struct disassemble_info di;
  int length;

  di = gdb_disassemble_info (gdbarch, stream);
  length = gdbarch_print_insn (gdbarch, memaddr, &di);
  if (branch_delay_insns)
    {
      if (di.insn_info_valid)
	*branch_delay_insns = di.branch_delay_insns;
      else
	*branch_delay_insns = 0;
    }
  return length;
}

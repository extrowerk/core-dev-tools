/* Do various things to symbol tables (other than lookup), for GDB.

   Copyright (C) 1986, 1987, 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995,
   1996, 1997, 1998, 1999, 2000, 2002, 2003, 2004, 2007, 2008, 2009
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
#include "symtab.h"
#include "gdbtypes.h"
#include "bfd.h"
#include "symfile.h"
#include "objfiles.h"
#include "breakpoint.h"
#include "command.h"
#include "gdb_obstack.h"
#include "exceptions.h"
#include "language.h"
#include "bcache.h"
#include "block.h"
#include "gdb_regex.h"
#include "gdb_stat.h"
#include "dictionary.h"

#include "gdb_string.h"
#include "readline/readline.h"

#ifndef DEV_TTY
#define DEV_TTY "/dev/tty"
#endif

/* Unfortunately for debugging, stderr is usually a macro.  This is painful
   when calling functions that take FILE *'s from the debugger.
   So we make a variable which has the same value and which is accessible when
   debugging GDB with itself.  Because stdin et al need not be constants,
   we initialize them in the _initialize_symmisc function at the bottom
   of the file.  */
FILE *std_in;
FILE *std_out;
FILE *std_err;

/* Prototypes for local functions */

static void dump_symtab (struct objfile *, struct symtab *,
			 struct ui_file *);

static void dump_psymtab (struct objfile *, struct partial_symtab *,
			  struct ui_file *);

static void dump_msymbols (struct objfile *, struct ui_file *);

static void dump_objfile (struct objfile *);

static int block_depth (struct block *);

static void print_partial_symbols (struct gdbarch *,
				   struct partial_symbol **, int,
				   char *, struct ui_file *);

void _initialize_symmisc (void);

struct print_symbol_args
  {
    struct gdbarch *gdbarch;
    struct symbol *symbol;
    int depth;
    struct ui_file *outfile;
  };

static int print_symbol (void *);

/* Free all the storage associated with the struct symtab <- S.
   Note that some symtabs have contents that all live inside one big block of
   memory, and some share the contents of another symbol table and so you
   should not free the contents on their behalf (except sometimes the
   linetable, which maybe per symtab even when the rest is not).
   It is s->free_code that says which alternative to use.  */

void
free_symtab (struct symtab *s)
{
  switch (s->free_code)
    {
    case free_nothing:
      /* All the contents are part of a big block of memory (an obstack),
         and some other symtab is in charge of freeing that block.
         Therefore, do nothing.  */
      break;

    case free_linetable:
      /* Everything will be freed either by our `free_func'
         or by some other symtab, except for our linetable.
         Free that now.  */
      if (LINETABLE (s))
	xfree (LINETABLE (s));
      break;
    }

  /* If there is a single block of memory to free, free it.  */
  if (s->free_func != NULL)
    s->free_func (s);

  /* Free source-related stuff */
  if (s->line_charpos != NULL)
    xfree (s->line_charpos);
  if (s->fullname != NULL)
    xfree (s->fullname);
  if (s->debugformat != NULL)
    xfree (s->debugformat);
  xfree (s);
}

void
print_symbol_bcache_statistics (void)
{
  struct objfile *objfile;

  immediate_quit++;
  ALL_OBJFILES (objfile)
  {
    printf_filtered (_("Byte cache statistics for '%s':\n"), objfile->name);
    print_bcache_statistics (objfile->psymbol_cache, "partial symbol cache");
    print_bcache_statistics (objfile->macro_cache, "preprocessor macro cache");
  }
  immediate_quit--;
}

void
print_objfile_statistics (void)
{
  struct objfile *objfile;
  struct symtab *s;
  struct partial_symtab *ps;
  int i, linetables, blockvectors;

  immediate_quit++;
  ALL_OBJFILES (objfile)
  {
    printf_filtered (_("Statistics for '%s':\n"), objfile->name);
    if (OBJSTAT (objfile, n_stabs) > 0)
      printf_filtered (_("  Number of \"stab\" symbols read: %d\n"),
		       OBJSTAT (objfile, n_stabs));
    if (OBJSTAT (objfile, n_minsyms) > 0)
      printf_filtered (_("  Number of \"minimal\" symbols read: %d\n"),
		       OBJSTAT (objfile, n_minsyms));
    if (OBJSTAT (objfile, n_psyms) > 0)
      printf_filtered (_("  Number of \"partial\" symbols read: %d\n"),
		       OBJSTAT (objfile, n_psyms));
    if (OBJSTAT (objfile, n_syms) > 0)
      printf_filtered (_("  Number of \"full\" symbols read: %d\n"),
		       OBJSTAT (objfile, n_syms));
    if (OBJSTAT (objfile, n_types) > 0)
      printf_filtered (_("  Number of \"types\" defined: %d\n"),
		       OBJSTAT (objfile, n_types));
    i = 0;
    ALL_OBJFILE_PSYMTABS (objfile, ps)
      {
        if (ps->readin == 0)
          i++;
      }
    printf_filtered (_("  Number of psym tables (not yet expanded): %d\n"), i);
    i = linetables = blockvectors = 0;
    ALL_OBJFILE_SYMTABS (objfile, s)
      {
        i++;
        if (s->linetable != NULL)
          linetables++;
        if (s->primary == 1)
          blockvectors++;
      }
    printf_filtered (_("  Number of symbol tables: %d\n"), i);
    printf_filtered (_("  Number of symbol tables with line tables: %d\n"), 
                     linetables);
    printf_filtered (_("  Number of symbol tables with blockvectors: %d\n"), 
                     blockvectors);
    
    if (OBJSTAT (objfile, sz_strtab) > 0)
      printf_filtered (_("  Space used by a.out string tables: %d\n"),
		       OBJSTAT (objfile, sz_strtab));
    printf_filtered (_("  Total memory used for objfile obstack: %d\n"),
		     obstack_memory_used (&objfile->objfile_obstack));
    printf_filtered (_("  Total memory used for psymbol cache: %d\n"),
		     bcache_memory_used (objfile->psymbol_cache));
    printf_filtered (_("  Total memory used for macro cache: %d\n"),
		     bcache_memory_used (objfile->macro_cache));
  }
  immediate_quit--;
}

static void
dump_objfile (struct objfile *objfile)
{
  struct symtab *symtab;
  struct partial_symtab *psymtab;

  printf_filtered ("\nObject file %s:  ", objfile->name);
  printf_filtered ("Objfile at ");
  gdb_print_host_address (objfile, gdb_stdout);
  printf_filtered (", bfd at ");
  gdb_print_host_address (objfile->obfd, gdb_stdout);
  printf_filtered (", %d minsyms\n\n",
		   objfile->minimal_symbol_count);

  if (objfile->psymtabs)
    {
      printf_filtered ("Psymtabs:\n");
      for (psymtab = objfile->psymtabs;
	   psymtab != NULL;
	   psymtab = psymtab->next)
	{
	  printf_filtered ("%s at ",
			   psymtab->filename);
	  gdb_print_host_address (psymtab, gdb_stdout);
	  printf_filtered (", ");
	  if (psymtab->objfile != objfile)
	    {
	      printf_filtered ("NOT ON CHAIN!  ");
	    }
	  wrap_here ("  ");
	}
      printf_filtered ("\n\n");
    }

  if (objfile->symtabs)
    {
      printf_filtered ("Symtabs:\n");
      for (symtab = objfile->symtabs;
	   symtab != NULL;
	   symtab = symtab->next)
	{
	  printf_filtered ("%s at ", symtab->filename);
	  gdb_print_host_address (symtab, gdb_stdout);
	  printf_filtered (", ");
	  if (symtab->objfile != objfile)
	    {
	      printf_filtered ("NOT ON CHAIN!  ");
	    }
	  wrap_here ("  ");
	}
      printf_filtered ("\n\n");
    }
}

/* Print minimal symbols from this objfile.  */

static void
dump_msymbols (struct objfile *objfile, struct ui_file *outfile)
{
  struct gdbarch *gdbarch = get_objfile_arch (objfile);
  struct minimal_symbol *msymbol;
  int index;
  char ms_type;

  fprintf_filtered (outfile, "\nObject file %s:\n\n", objfile->name);
  if (objfile->minimal_symbol_count == 0)
    {
      fprintf_filtered (outfile, "No minimal symbols found.\n");
      return;
    }
  index = 0;
  ALL_OBJFILE_MSYMBOLS (objfile, msymbol)
    {
      struct obj_section *section = SYMBOL_OBJ_SECTION (msymbol);

      switch (MSYMBOL_TYPE (msymbol))
	{
	case mst_unknown:
	  ms_type = 'u';
	  break;
	case mst_text:
	  ms_type = 'T';
	  break;
	case mst_solib_trampoline:
	  ms_type = 'S';
	  break;
	case mst_data:
	  ms_type = 'D';
	  break;
	case mst_bss:
	  ms_type = 'B';
	  break;
	case mst_abs:
	  ms_type = 'A';
	  break;
	case mst_file_text:
	  ms_type = 't';
	  break;
	case mst_file_data:
	  ms_type = 'd';
	  break;
	case mst_file_bss:
	  ms_type = 'b';
	  break;
	default:
	  ms_type = '?';
	  break;
	}
      fprintf_filtered (outfile, "[%2d] %c ", index, ms_type);
      fputs_filtered (paddress (gdbarch, SYMBOL_VALUE_ADDRESS (msymbol)),
		      outfile);
      fprintf_filtered (outfile, " %s", SYMBOL_LINKAGE_NAME (msymbol));
      if (section)
	fprintf_filtered (outfile, " section %s",
			  bfd_section_name (objfile->obfd,
					    section->the_bfd_section));
      if (SYMBOL_DEMANGLED_NAME (msymbol) != NULL)
	{
	  fprintf_filtered (outfile, "  %s", SYMBOL_DEMANGLED_NAME (msymbol));
	}
      if (msymbol->filename)
	fprintf_filtered (outfile, "  %s", msymbol->filename);
      fputs_filtered ("\n", outfile);
      index++;
    }
  if (objfile->minimal_symbol_count != index)
    {
      warning (_("internal error:  minimal symbol count %d != %d"),
	       objfile->minimal_symbol_count, index);
    }
  fprintf_filtered (outfile, "\n");
}

static void
dump_psymtab (struct objfile *objfile, struct partial_symtab *psymtab,
	      struct ui_file *outfile)
{
  struct gdbarch *gdbarch = get_objfile_arch (objfile);
  int i;

  fprintf_filtered (outfile, "\nPartial symtab for source file %s ",
		    psymtab->filename);
  fprintf_filtered (outfile, "(object ");
  gdb_print_host_address (psymtab, outfile);
  fprintf_filtered (outfile, ")\n\n");
  fprintf_unfiltered (outfile, "  Read from object file %s (",
		      objfile->name);
  gdb_print_host_address (objfile, outfile);
  fprintf_unfiltered (outfile, ")\n");

  if (psymtab->readin)
    {
      fprintf_filtered (outfile,
			"  Full symtab was read (at ");
      gdb_print_host_address (psymtab->symtab, outfile);
      fprintf_filtered (outfile, " by function at ");
      gdb_print_host_address (psymtab->read_symtab, outfile);
      fprintf_filtered (outfile, ")\n");
    }

  fprintf_filtered (outfile, "  Relocate symbols by ");
  for (i = 0; i < psymtab->objfile->num_sections; ++i)
    {
      if (i != 0)
	fprintf_filtered (outfile, ", ");
      wrap_here ("    ");
      fputs_filtered (paddress (gdbarch,
				ANOFFSET (psymtab->section_offsets, i)),
		      outfile);
    }
  fprintf_filtered (outfile, "\n");

  fprintf_filtered (outfile, "  Symbols cover text addresses ");
  fputs_filtered (paddress (gdbarch, psymtab->textlow), outfile);
  fprintf_filtered (outfile, "-");
  fputs_filtered (paddress (gdbarch, psymtab->texthigh), outfile);
  fprintf_filtered (outfile, "\n");
  fprintf_filtered (outfile, "  Depends on %d other partial symtabs.\n",
		    psymtab->number_of_dependencies);
  for (i = 0; i < psymtab->number_of_dependencies; i++)
    {
      fprintf_filtered (outfile, "    %d ", i);
      gdb_print_host_address (psymtab->dependencies[i], outfile);
      fprintf_filtered (outfile, " %s\n",
			psymtab->dependencies[i]->filename);
    }
  if (psymtab->n_global_syms > 0)
    {
      print_partial_symbols (gdbarch,
			     objfile->global_psymbols.list
			     + psymtab->globals_offset,
			     psymtab->n_global_syms, "Global", outfile);
    }
  if (psymtab->n_static_syms > 0)
    {
      print_partial_symbols (gdbarch,
			     objfile->static_psymbols.list
			     + psymtab->statics_offset,
			     psymtab->n_static_syms, "Static", outfile);
    }
  fprintf_filtered (outfile, "\n");
}

static void
dump_symtab_1 (struct objfile *objfile, struct symtab *symtab,
	       struct ui_file *outfile)
{
  struct gdbarch *gdbarch = get_objfile_arch (objfile);
  int i;
  struct dict_iterator iter;
  int len;
  struct linetable *l;
  struct blockvector *bv;
  struct symbol *sym;
  struct block *b;
  int depth;

  fprintf_filtered (outfile, "\nSymtab for file %s\n", symtab->filename);
  if (symtab->dirname)
    fprintf_filtered (outfile, "Compilation directory is %s\n",
		      symtab->dirname);
  fprintf_filtered (outfile, "Read from object file %s (", objfile->name);
  gdb_print_host_address (objfile, outfile);
  fprintf_filtered (outfile, ")\n");
  fprintf_filtered (outfile, "Language: %s\n", language_str (symtab->language));

  /* First print the line table.  */
  l = LINETABLE (symtab);
  if (l)
    {
      fprintf_filtered (outfile, "\nLine table:\n\n");
      len = l->nitems;
      for (i = 0; i < len; i++)
	{
	  fprintf_filtered (outfile, " line %d at ", l->item[i].line);
	  fputs_filtered (paddress (gdbarch, l->item[i].pc), outfile);
	  fprintf_filtered (outfile, "\n");
	}
    }
  /* Now print the block info, but only for primary symtabs since we will
     print lots of duplicate info otherwise. */
  if (symtab->primary)
    {
      fprintf_filtered (outfile, "\nBlockvector:\n\n");
      bv = BLOCKVECTOR (symtab);
      len = BLOCKVECTOR_NBLOCKS (bv);
      for (i = 0; i < len; i++)
	{
	  b = BLOCKVECTOR_BLOCK (bv, i);
	  depth = block_depth (b) * 2;
	  print_spaces (depth, outfile);
	  fprintf_filtered (outfile, "block #%03d, object at ", i);
	  gdb_print_host_address (b, outfile);
	  if (BLOCK_SUPERBLOCK (b))
	    {
	      fprintf_filtered (outfile, " under ");
	      gdb_print_host_address (BLOCK_SUPERBLOCK (b), outfile);
	    }
	  /* drow/2002-07-10: We could save the total symbols count
	     even if we're using a hashtable, but nothing else but this message
	     wants it.  */
	  fprintf_filtered (outfile, ", %d syms/buckets in ",
			    dict_size (BLOCK_DICT (b)));
	  fputs_filtered (paddress (gdbarch, BLOCK_START (b)), outfile);
	  fprintf_filtered (outfile, "..");
	  fputs_filtered (paddress (gdbarch, BLOCK_END (b)), outfile);
	  if (BLOCK_FUNCTION (b))
	    {
	      fprintf_filtered (outfile, ", function %s",
				SYMBOL_LINKAGE_NAME (BLOCK_FUNCTION (b)));
	      if (SYMBOL_DEMANGLED_NAME (BLOCK_FUNCTION (b)) != NULL)
		{
		  fprintf_filtered (outfile, ", %s",
				SYMBOL_DEMANGLED_NAME (BLOCK_FUNCTION (b)));
		}
	    }
	  fprintf_filtered (outfile, "\n");
	  /* Now print each symbol in this block (in no particular order, if
	     we're using a hashtable).  */
	  ALL_BLOCK_SYMBOLS (b, iter, sym)
	    {
	      struct print_symbol_args s;
	      s.gdbarch = gdbarch;
	      s.symbol = sym;
	      s.depth = depth + 1;
	      s.outfile = outfile;
	      catch_errors (print_symbol, &s, "Error printing symbol:\n",
			    RETURN_MASK_ERROR);
	    }
	}
      fprintf_filtered (outfile, "\n");
    }
  else
    {
      fprintf_filtered (outfile, "\nBlockvector same as previous symtab\n\n");
    }
}

static void
dump_symtab (struct objfile *objfile, struct symtab *symtab,
	     struct ui_file *outfile)
{
  /* Set the current language to the language of the symtab we're dumping
     because certain routines used during dump_symtab() use the current
     language to print an image of the symbol.  We'll restore it later.
     But use only real languages, not placeholders.  */
  if (symtab->language != language_unknown
      && symtab->language != language_auto)
    {
      enum language saved_lang;

      saved_lang = set_language (symtab->language);

      dump_symtab_1 (objfile, symtab, outfile);

      set_language (saved_lang);
    }
  else
    dump_symtab_1 (objfile, symtab, outfile);
}

void
maintenance_print_symbols (char *args, int from_tty)
{
  char **argv;
  struct ui_file *outfile;
  struct cleanup *cleanups;
  char *symname = NULL;
  char *filename = DEV_TTY;
  struct objfile *objfile;
  struct symtab *s;

  dont_repeat ();

  if (args == NULL)
    {
      error (_("\
Arguments missing: an output file name and an optional symbol file name"));
    }
  argv = gdb_buildargv (args);
  cleanups = make_cleanup_freeargv (argv);

  if (argv[0] != NULL)
    {
      filename = argv[0];
      /* If a second arg is supplied, it is a source file name to match on */
      if (argv[1] != NULL)
	{
	  symname = argv[1];
	}
    }

  filename = tilde_expand (filename);
  make_cleanup (xfree, filename);

  outfile = gdb_fopen (filename, FOPEN_WT);
  if (outfile == 0)
    perror_with_name (filename);
  make_cleanup_ui_file_delete (outfile);

  immediate_quit++;
  ALL_SYMTABS (objfile, s)
    if (symname == NULL || strcmp (symname, s->filename) == 0)
    dump_symtab (objfile, s, outfile);
  immediate_quit--;
  do_cleanups (cleanups);
}

/* Print symbol ARGS->SYMBOL on ARGS->OUTFILE.  ARGS->DEPTH says how
   far to indent.  ARGS is really a struct print_symbol_args *, but is
   declared as char * to get it past catch_errors.  Returns 0 for error,
   1 for success.  */

static int
print_symbol (void *args)
{
  struct gdbarch *gdbarch = ((struct print_symbol_args *) args)->gdbarch;
  struct symbol *symbol = ((struct print_symbol_args *) args)->symbol;
  int depth = ((struct print_symbol_args *) args)->depth;
  struct ui_file *outfile = ((struct print_symbol_args *) args)->outfile;
  struct obj_section *section = SYMBOL_OBJ_SECTION (symbol);

  print_spaces (depth, outfile);
  if (SYMBOL_DOMAIN (symbol) == LABEL_DOMAIN)
    {
      fprintf_filtered (outfile, "label %s at ", SYMBOL_PRINT_NAME (symbol));
      fputs_filtered (paddress (gdbarch, SYMBOL_VALUE_ADDRESS (symbol)),
		      outfile);
      if (section)
	fprintf_filtered (outfile, " section %s\n",
			  bfd_section_name (section->the_bfd_section->owner,
					    section->the_bfd_section));
      else
	fprintf_filtered (outfile, "\n");
      return 1;
    }
  if (SYMBOL_DOMAIN (symbol) == STRUCT_DOMAIN)
    {
      if (TYPE_TAG_NAME (SYMBOL_TYPE (symbol)))
	{
	  LA_PRINT_TYPE (SYMBOL_TYPE (symbol), "", outfile, 1, depth);
	}
      else
	{
	  fprintf_filtered (outfile, "%s %s = ",
			 (TYPE_CODE (SYMBOL_TYPE (symbol)) == TYPE_CODE_ENUM
			  ? "enum"
		     : (TYPE_CODE (SYMBOL_TYPE (symbol)) == TYPE_CODE_STRUCT
			? "struct" : "union")),
			    SYMBOL_LINKAGE_NAME (symbol));
	  LA_PRINT_TYPE (SYMBOL_TYPE (symbol), "", outfile, 1, depth);
	}
      fprintf_filtered (outfile, ";\n");
    }
  else
    {
      if (SYMBOL_CLASS (symbol) == LOC_TYPEDEF)
	fprintf_filtered (outfile, "typedef ");
      if (SYMBOL_TYPE (symbol))
	{
	  /* Print details of types, except for enums where it's clutter.  */
	  LA_PRINT_TYPE (SYMBOL_TYPE (symbol), SYMBOL_PRINT_NAME (symbol),
			 outfile,
			 TYPE_CODE (SYMBOL_TYPE (symbol)) != TYPE_CODE_ENUM,
			 depth);
	  fprintf_filtered (outfile, "; ");
	}
      else
	fprintf_filtered (outfile, "%s ", SYMBOL_PRINT_NAME (symbol));

      switch (SYMBOL_CLASS (symbol))
	{
	case LOC_CONST:
	  fprintf_filtered (outfile, "const %ld (0x%lx)",
			    SYMBOL_VALUE (symbol),
			    SYMBOL_VALUE (symbol));
	  break;

	case LOC_CONST_BYTES:
	  {
	    unsigned i;
	    struct type *type = check_typedef (SYMBOL_TYPE (symbol));
	    fprintf_filtered (outfile, "const %u hex bytes:",
			      TYPE_LENGTH (type));
	    for (i = 0; i < TYPE_LENGTH (type); i++)
	      fprintf_filtered (outfile, " %02x",
				(unsigned) SYMBOL_VALUE_BYTES (symbol)[i]);
	  }
	  break;

	case LOC_STATIC:
	  fprintf_filtered (outfile, "static at ");
	  fputs_filtered (paddress (gdbarch, SYMBOL_VALUE_ADDRESS (symbol)),
			  outfile);
	  if (section)
	    fprintf_filtered (outfile, " section %s",
			      bfd_section_name (section->the_bfd_section->owner,
						section->the_bfd_section));
	  break;

	case LOC_REGISTER:
	  if (SYMBOL_IS_ARGUMENT (symbol))
	    fprintf_filtered (outfile, "parameter register %ld",
			      SYMBOL_VALUE (symbol));
	  else
	    fprintf_filtered (outfile, "register %ld", SYMBOL_VALUE (symbol));
	  break;

	case LOC_ARG:
	  fprintf_filtered (outfile, "arg at offset 0x%lx",
			    SYMBOL_VALUE (symbol));
	  break;

	case LOC_REF_ARG:
	  fprintf_filtered (outfile, "reference arg at 0x%lx", SYMBOL_VALUE (symbol));
	  break;

	case LOC_REGPARM_ADDR:
	  fprintf_filtered (outfile, "address parameter register %ld", SYMBOL_VALUE (symbol));
	  break;

	case LOC_LOCAL:
	  fprintf_filtered (outfile, "local at offset 0x%lx",
			    SYMBOL_VALUE (symbol));
	  break;

	case LOC_TYPEDEF:
	  break;

	case LOC_LABEL:
	  fprintf_filtered (outfile, "label at ");
	  fputs_filtered (paddress (gdbarch, SYMBOL_VALUE_ADDRESS (symbol)),
			  outfile);
	  if (section)
	    fprintf_filtered (outfile, " section %s",
			      bfd_section_name (section->the_bfd_section->owner,
						section->the_bfd_section));
	  break;

	case LOC_BLOCK:
	  fprintf_filtered (outfile, "block object ");
	  gdb_print_host_address (SYMBOL_BLOCK_VALUE (symbol), outfile);
	  fprintf_filtered (outfile, ", ");
	  fputs_filtered (paddress (gdbarch,
				    BLOCK_START (SYMBOL_BLOCK_VALUE (symbol))),
			  outfile);
	  fprintf_filtered (outfile, "..");
	  fputs_filtered (paddress (gdbarch,
				    BLOCK_END (SYMBOL_BLOCK_VALUE (symbol))),
			  outfile);
	  if (section)
	    fprintf_filtered (outfile, " section %s",
			      bfd_section_name (section->the_bfd_section->owner,
						section->the_bfd_section));
	  break;

	case LOC_COMPUTED:
	  fprintf_filtered (outfile, "computed at runtime");
	  break;

	case LOC_UNRESOLVED:
	  fprintf_filtered (outfile, "unresolved");
	  break;

	case LOC_OPTIMIZED_OUT:
	  fprintf_filtered (outfile, "optimized out");
	  break;

	default:
	  fprintf_filtered (outfile, "botched symbol class %x",
			    SYMBOL_CLASS (symbol));
	  break;
	}
    }
  fprintf_filtered (outfile, "\n");
  return 1;
}

void
maintenance_print_psymbols (char *args, int from_tty)
{
  char **argv;
  struct ui_file *outfile;
  struct cleanup *cleanups;
  char *symname = NULL;
  char *filename = DEV_TTY;
  struct objfile *objfile;
  struct partial_symtab *ps;

  dont_repeat ();

  if (args == NULL)
    {
      error (_("print-psymbols takes an output file name and optional symbol file name"));
    }
  argv = gdb_buildargv (args);
  cleanups = make_cleanup_freeargv (argv);

  if (argv[0] != NULL)
    {
      filename = argv[0];
      /* If a second arg is supplied, it is a source file name to match on */
      if (argv[1] != NULL)
	{
	  symname = argv[1];
	}
    }

  filename = tilde_expand (filename);
  make_cleanup (xfree, filename);

  outfile = gdb_fopen (filename, FOPEN_WT);
  if (outfile == 0)
    perror_with_name (filename);
  make_cleanup_ui_file_delete (outfile);

  immediate_quit++;
  ALL_PSYMTABS (objfile, ps)
    if (symname == NULL || strcmp (symname, ps->filename) == 0)
    dump_psymtab (objfile, ps, outfile);
  immediate_quit--;
  do_cleanups (cleanups);
}

static void
print_partial_symbols (struct gdbarch *gdbarch,
		       struct partial_symbol **p, int count, char *what,
		       struct ui_file *outfile)
{
  fprintf_filtered (outfile, "  %s partial symbols:\n", what);
  while (count-- > 0)
    {
      fprintf_filtered (outfile, "    `%s'", SYMBOL_LINKAGE_NAME (*p));
      if (SYMBOL_DEMANGLED_NAME (*p) != NULL)
	{
	  fprintf_filtered (outfile, "  `%s'", SYMBOL_DEMANGLED_NAME (*p));
	}
      fputs_filtered (", ", outfile);
      switch (SYMBOL_DOMAIN (*p))
	{
	case UNDEF_DOMAIN:
	  fputs_filtered ("undefined domain, ", outfile);
	  break;
	case VAR_DOMAIN:
	  /* This is the usual thing -- don't print it */
	  break;
	case STRUCT_DOMAIN:
	  fputs_filtered ("struct domain, ", outfile);
	  break;
	case LABEL_DOMAIN:
	  fputs_filtered ("label domain, ", outfile);
	  break;
	default:
	  fputs_filtered ("<invalid domain>, ", outfile);
	  break;
	}
      switch (SYMBOL_CLASS (*p))
	{
	case LOC_UNDEF:
	  fputs_filtered ("undefined", outfile);
	  break;
	case LOC_CONST:
	  fputs_filtered ("constant int", outfile);
	  break;
	case LOC_STATIC:
	  fputs_filtered ("static", outfile);
	  break;
	case LOC_REGISTER:
	  fputs_filtered ("register", outfile);
	  break;
	case LOC_ARG:
	  fputs_filtered ("pass by value", outfile);
	  break;
	case LOC_REF_ARG:
	  fputs_filtered ("pass by reference", outfile);
	  break;
	case LOC_REGPARM_ADDR:
	  fputs_filtered ("register address parameter", outfile);
	  break;
	case LOC_LOCAL:
	  fputs_filtered ("stack parameter", outfile);
	  break;
	case LOC_TYPEDEF:
	  fputs_filtered ("type", outfile);
	  break;
	case LOC_LABEL:
	  fputs_filtered ("label", outfile);
	  break;
	case LOC_BLOCK:
	  fputs_filtered ("function", outfile);
	  break;
	case LOC_CONST_BYTES:
	  fputs_filtered ("constant bytes", outfile);
	  break;
	case LOC_UNRESOLVED:
	  fputs_filtered ("unresolved", outfile);
	  break;
	case LOC_OPTIMIZED_OUT:
	  fputs_filtered ("optimized out", outfile);
	  break;
	case LOC_COMPUTED:
	  fputs_filtered ("computed at runtime", outfile);
	  break;
	default:
	  fputs_filtered ("<invalid location>", outfile);
	  break;
	}
      fputs_filtered (", ", outfile);
      fputs_filtered (paddress (gdbarch, SYMBOL_VALUE_ADDRESS (*p)), outfile);
      fprintf_filtered (outfile, "\n");
      p++;
    }
}

void
maintenance_print_msymbols (char *args, int from_tty)
{
  char **argv;
  struct ui_file *outfile;
  struct cleanup *cleanups;
  char *filename = DEV_TTY;
  char *symname = NULL;
  struct objfile *objfile;

  struct stat sym_st, obj_st;

  dont_repeat ();

  if (args == NULL)
    {
      error (_("print-msymbols takes an output file name and optional symbol file name"));
    }
  argv = gdb_buildargv (args);
  cleanups = make_cleanup_freeargv (argv);

  if (argv[0] != NULL)
    {
      filename = argv[0];
      /* If a second arg is supplied, it is a source file name to match on */
      if (argv[1] != NULL)
	{
	  symname = xfullpath (argv[1]);
	  make_cleanup (xfree, symname);
	  if (symname && stat (symname, &sym_st))
	    perror_with_name (symname);
	}
    }

  filename = tilde_expand (filename);
  make_cleanup (xfree, filename);

  outfile = gdb_fopen (filename, FOPEN_WT);
  if (outfile == 0)
    perror_with_name (filename);
  make_cleanup_ui_file_delete (outfile);

  immediate_quit++;
  ALL_OBJFILES (objfile)
    if (symname == NULL
	|| (!stat (objfile->name, &obj_st) && sym_st.st_ino == obj_st.st_ino))
      dump_msymbols (objfile, outfile);
  immediate_quit--;
  fprintf_filtered (outfile, "\n\n");
  do_cleanups (cleanups);
}

void
maintenance_print_objfiles (char *ignore, int from_tty)
{
  struct objfile *objfile;

  dont_repeat ();

  immediate_quit++;
  ALL_OBJFILES (objfile)
    dump_objfile (objfile);
  immediate_quit--;
}


/* List all the symbol tables whose names match REGEXP (optional).  */
void
maintenance_info_symtabs (char *regexp, int from_tty)
{
  struct objfile *objfile;

  if (regexp)
    re_comp (regexp);

  ALL_OBJFILES (objfile)
    {
      struct symtab *symtab;
      
      /* We don't want to print anything for this objfile until we
         actually find a symtab whose name matches.  */
      int printed_objfile_start = 0;

      ALL_OBJFILE_SYMTABS (objfile, symtab)
	{
	  QUIT;

	  if (! regexp
	      || re_exec (symtab->filename))
	    {
	      if (! printed_objfile_start)
		{
		  printf_filtered ("{ objfile %s ", objfile->name);
		  wrap_here ("  ");
		  printf_filtered ("((struct objfile *) %s)\n", 
				   host_address_to_string (objfile));
		  printed_objfile_start = 1;
		}

	      printf_filtered ("	{ symtab %s ", symtab->filename);
	      wrap_here ("    ");
	      printf_filtered ("((struct symtab *) %s)\n", 
			       host_address_to_string (symtab));
	      printf_filtered ("	  dirname %s\n",
			       symtab->dirname ? symtab->dirname : "(null)");
	      printf_filtered ("	  fullname %s\n",
			       symtab->fullname ? symtab->fullname : "(null)");
	      printf_filtered ("	  blockvector ((struct blockvector *) %s)%s\n",
			       host_address_to_string (symtab->blockvector),
			       symtab->primary ? " (primary)" : "");
	      printf_filtered ("	  linetable ((struct linetable *) %s)\n",
			       host_address_to_string (symtab->linetable));
	      printf_filtered ("	  debugformat %s\n", symtab->debugformat);
	      printf_filtered ("	}\n");
	    }
	}

      if (printed_objfile_start)
        printf_filtered ("}\n");
    }
}


/* List all the partial symbol tables whose names match REGEXP (optional).  */
void
maintenance_info_psymtabs (char *regexp, int from_tty)
{
  struct objfile *objfile;

  if (regexp)
    re_comp (regexp);

  ALL_OBJFILES (objfile)
    {
      struct gdbarch *gdbarch = get_objfile_arch (objfile);
      struct partial_symtab *psymtab;

      /* We don't want to print anything for this objfile until we
         actually find a symtab whose name matches.  */
      int printed_objfile_start = 0;

      ALL_OBJFILE_PSYMTABS (objfile, psymtab)
	{
	  QUIT;

	  if (! regexp
	      || re_exec (psymtab->filename))
	    {
	      if (! printed_objfile_start)
		{
		  printf_filtered ("{ objfile %s ", objfile->name);
		  wrap_here ("  ");
		  printf_filtered ("((struct objfile *) %s)\n", 
				   host_address_to_string (objfile));
		  printed_objfile_start = 1;
		}

	      printf_filtered ("  { psymtab %s ", psymtab->filename);
	      wrap_here ("    ");
	      printf_filtered ("((struct partial_symtab *) %s)\n", 
			       host_address_to_string (psymtab));

	      printf_filtered ("    readin %s\n",
			       psymtab->readin ? "yes" : "no");
	      printf_filtered ("    fullname %s\n",
			       psymtab->fullname ? psymtab->fullname : "(null)");
	      printf_filtered ("    text addresses ");
	      fputs_filtered (paddress (gdbarch, psymtab->textlow),
			      gdb_stdout);
	      printf_filtered (" -- ");
	      fputs_filtered (paddress (gdbarch, psymtab->texthigh),
			      gdb_stdout);
	      printf_filtered ("\n");
	      printf_filtered ("    globals ");
	      if (psymtab->n_global_syms)
		{
		  printf_filtered ("(* (struct partial_symbol **) %s @ %d)\n",
				   host_address_to_string (psymtab->objfile->global_psymbols.list
				    + psymtab->globals_offset),
				   psymtab->n_global_syms);
		}
	      else
		printf_filtered ("(none)\n");
	      printf_filtered ("    statics ");
	      if (psymtab->n_static_syms)
		{
		  printf_filtered ("(* (struct partial_symbol **) %s @ %d)\n",
				   host_address_to_string (psymtab->objfile->static_psymbols.list
				    + psymtab->statics_offset),
				   psymtab->n_static_syms);
		}
	      else
		printf_filtered ("(none)\n");
	      printf_filtered ("    dependencies ");
	      if (psymtab->number_of_dependencies)
		{
		  int i;

		  printf_filtered ("{\n");
		  for (i = 0; i < psymtab->number_of_dependencies; i++)
		    {
		      struct partial_symtab *dep = psymtab->dependencies[i];

		      /* Note the string concatenation there --- no comma.  */
		      printf_filtered ("      psymtab %s "
				       "((struct partial_symtab *) %s)\n",
				       dep->filename, 
				       host_address_to_string (dep));
		    }
		  printf_filtered ("    }\n");
		}
	      else
		printf_filtered ("(none)\n");
	      printf_filtered ("  }\n");
	    }
	}

      if (printed_objfile_start)
        printf_filtered ("}\n");
    }
}


/* Check consistency of psymtabs and symtabs.  */

void
maintenance_check_symtabs (char *ignore, int from_tty)
{
  struct symbol *sym;
  struct partial_symbol **psym;
  struct symtab *s = NULL;
  struct partial_symtab *ps;
  struct blockvector *bv;
  struct objfile *objfile;
  struct block *b;
  int length;

  ALL_PSYMTABS (objfile, ps)
  {
    struct gdbarch *gdbarch = get_objfile_arch (objfile);
    s = PSYMTAB_TO_SYMTAB (ps);
    if (s == NULL)
      continue;
    bv = BLOCKVECTOR (s);
    b = BLOCKVECTOR_BLOCK (bv, STATIC_BLOCK);
    psym = ps->objfile->static_psymbols.list + ps->statics_offset;
    length = ps->n_static_syms;
    while (length--)
      {
	sym = lookup_block_symbol (b, SYMBOL_LINKAGE_NAME (*psym),
				   NULL, SYMBOL_DOMAIN (*psym));
	if (!sym)
	  {
	    printf_filtered ("Static symbol `");
	    puts_filtered (SYMBOL_LINKAGE_NAME (*psym));
	    printf_filtered ("' only found in ");
	    puts_filtered (ps->filename);
	    printf_filtered (" psymtab\n");
	  }
	psym++;
      }
    b = BLOCKVECTOR_BLOCK (bv, GLOBAL_BLOCK);
    psym = ps->objfile->global_psymbols.list + ps->globals_offset;
    length = ps->n_global_syms;
    while (length--)
      {
	sym = lookup_block_symbol (b, SYMBOL_LINKAGE_NAME (*psym),
				   NULL, SYMBOL_DOMAIN (*psym));
	if (!sym)
	  {
	    printf_filtered ("Global symbol `");
	    puts_filtered (SYMBOL_LINKAGE_NAME (*psym));
	    printf_filtered ("' only found in ");
	    puts_filtered (ps->filename);
	    printf_filtered (" psymtab\n");
	  }
	psym++;
      }
    if (ps->texthigh < ps->textlow)
      {
	printf_filtered ("Psymtab ");
	puts_filtered (ps->filename);
	printf_filtered (" covers bad range ");
	fputs_filtered (paddress (gdbarch, ps->textlow), gdb_stdout);
	printf_filtered (" - ");
	fputs_filtered (paddress (gdbarch, ps->texthigh), gdb_stdout);
	printf_filtered ("\n");
	continue;
      }
    if (ps->texthigh == 0)
      continue;
    if (ps->textlow < BLOCK_START (b) || ps->texthigh > BLOCK_END (b))
      {
	printf_filtered ("Psymtab ");
	puts_filtered (ps->filename);
	printf_filtered (" covers ");
	fputs_filtered (paddress (gdbarch, ps->textlow), gdb_stdout);
	printf_filtered (" - ");
	fputs_filtered (paddress (gdbarch, ps->texthigh), gdb_stdout);
	printf_filtered (" but symtab covers only ");
	fputs_filtered (paddress (gdbarch, BLOCK_START (b)), gdb_stdout);
	printf_filtered (" - ");
	fputs_filtered (paddress (gdbarch, BLOCK_END (b)), gdb_stdout);
	printf_filtered ("\n");
      }
  }
}


/* Return the nexting depth of a block within other blocks in its symtab.  */

static int
block_depth (struct block *block)
{
  int i = 0;
  while ((block = BLOCK_SUPERBLOCK (block)) != NULL)
    {
      i++;
    }
  return i;
}


/* Increase the space allocated for LISTP, which is probably
   global_psymbols or static_psymbols. This space will eventually
   be freed in free_objfile().  */

void
extend_psymbol_list (struct psymbol_allocation_list *listp,
		     struct objfile *objfile)
{
  int new_size;
  if (listp->size == 0)
    {
      new_size = 255;
      listp->list = (struct partial_symbol **)
	xmalloc (new_size * sizeof (struct partial_symbol *));
    }
  else
    {
      new_size = listp->size * 2;
      listp->list = (struct partial_symbol **)
	xrealloc ((char *) listp->list,
		  new_size * sizeof (struct partial_symbol *));
    }
  /* Next assumes we only went one over.  Should be good if
     program works correctly */
  listp->next = listp->list + listp->size;
  listp->size = new_size;
}


/* Do early runtime initializations. */
void
_initialize_symmisc (void)
{
  std_in = stdin;
  std_out = stdout;
  std_err = stderr;
}

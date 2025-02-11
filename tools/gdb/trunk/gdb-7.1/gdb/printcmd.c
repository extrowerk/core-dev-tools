/* Print values for GNU debugger GDB.

   Copyright (C) 1986, 1987, 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995,
   1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007,
   2008, 2009, 2010 Free Software Foundation, Inc.

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
#include "gdb_string.h"
#include "frame.h"
#include "symtab.h"
#include "gdbtypes.h"
#include "value.h"
#include "language.h"
#include "expression.h"
#include "gdbcore.h"
#include "gdbcmd.h"
#include "target.h"
#include "breakpoint.h"
#include "demangle.h"
#include "valprint.h"
#include "annotate.h"
#include "symfile.h"		/* for overlay functions */
#include "objfiles.h"		/* ditto */
#include "completer.h"		/* for completion functions */
#include "ui-out.h"
#include "gdb_assert.h"
#include "block.h"
#include "disasm.h"
#include "dfp.h"
#include "valprint.h"
#include "exceptions.h"
#include "observer.h"
#include "solist.h"
#include "parser-defs.h"
#include "charset.h"
#include "arch-utils.h"

#ifdef TUI
#include "tui/tui.h"		/* For tui_active et.al.   */
#endif

#if defined(__MINGW32__) && !defined(PRINTF_HAS_LONG_LONG)
# define USE_PRINTF_I64 1
# define PRINTF_HAS_LONG_LONG
#else
# define USE_PRINTF_I64 0
#endif

extern int asm_demangle;	/* Whether to demangle syms in asm printouts */

struct format_data
  {
    int count;
    char format;
    char size;

    /* True if the value should be printed raw -- that is, bypassing
       python-based formatters.  */
    unsigned char raw;
  };

/* Last specified output format.  */

static char last_format = 0;

/* Last specified examination size.  'b', 'h', 'w' or `q'.  */

static char last_size = 'w';

/* Default address to examine next, and associated architecture.  */

static struct gdbarch *next_gdbarch;
static CORE_ADDR next_address;

/* Number of delay instructions following current disassembled insn.  */

static int branch_delay_insns;

/* Last address examined.  */

static CORE_ADDR last_examine_address;

/* Contents of last address examined.
   This is not valid past the end of the `x' command!  */

static struct value *last_examine_value;

/* Largest offset between a symbolic value and an address, that will be
   printed as `0x1234 <symbol+offset>'.  */

static unsigned int max_symbolic_offset = UINT_MAX;
static void
show_max_symbolic_offset (struct ui_file *file, int from_tty,
			  struct cmd_list_element *c, const char *value)
{
  fprintf_filtered (file, _("\
The largest offset that will be printed in <symbol+1234> form is %s.\n"),
		    value);
}

/* Append the source filename and linenumber of the symbol when
   printing a symbolic value as `<symbol at filename:linenum>' if set.  */
static int print_symbol_filename = 0;
static void
show_print_symbol_filename (struct ui_file *file, int from_tty,
			    struct cmd_list_element *c, const char *value)
{
  fprintf_filtered (file, _("\
Printing of source filename and line number with <symbol> is %s.\n"),
		    value);
}

/* Number of auto-display expression currently being displayed.
   So that we can disable it if we get an error or a signal within it.
   -1 when not doing one.  */

int current_display_number;

struct display
  {
    /* Chain link to next auto-display item.  */
    struct display *next;

    /* The expression as the user typed it.  */
    char *exp_string;

    /* Expression to be evaluated and displayed.  */
    struct expression *exp;

    /* Item number of this auto-display item.  */
    int number;

    /* Display format specified.  */
    struct format_data format;

    /* Program space associated with `block'.  */
    struct program_space *pspace;

    /* Innermost block required by this expression when evaluated */
    struct block *block;

    /* Status of this display (enabled or disabled) */
    int enabled_p;
  };

/* Chain of expressions whose values should be displayed
   automatically each time the program stops.  */

static struct display *display_chain;

static int display_number;

/* Prototypes for exported functions. */

void output_command (char *, int);

void _initialize_printcmd (void);

/* Prototypes for local functions. */

static void do_one_display (struct display *);


/* Decode a format specification.  *STRING_PTR should point to it.
   OFORMAT and OSIZE are used as defaults for the format and size
   if none are given in the format specification.
   If OSIZE is zero, then the size field of the returned value
   should be set only if a size is explicitly specified by the
   user.
   The structure returned describes all the data
   found in the specification.  In addition, *STRING_PTR is advanced
   past the specification and past all whitespace following it.  */

static struct format_data
decode_format (char **string_ptr, int oformat, int osize)
{
  struct format_data val;
  char *p = *string_ptr;

  val.format = '?';
  val.size = '?';
  val.count = 1;
  val.raw = 0;

  if (*p >= '0' && *p <= '9')
    val.count = atoi (p);
  while (*p >= '0' && *p <= '9')
    p++;

  /* Now process size or format letters that follow.  */

  while (1)
    {
      if (*p == 'b' || *p == 'h' || *p == 'w' || *p == 'g')
	val.size = *p++;
      else if (*p == 'r')
	{
	  val.raw = 1;
	  p++;
	}
      else if (*p >= 'a' && *p <= 'z')
	val.format = *p++;
      else
	break;
    }

  while (*p == ' ' || *p == '\t')
    p++;
  *string_ptr = p;

  /* Set defaults for format and size if not specified.  */
  if (val.format == '?')
    {
      if (val.size == '?')
	{
	  /* Neither has been specified.  */
	  val.format = oformat;
	  val.size = osize;
	}
      else
	/* If a size is specified, any format makes a reasonable
	   default except 'i'.  */
	val.format = oformat == 'i' ? 'x' : oformat;
    }
  else if (val.size == '?')
    switch (val.format)
      {
      case 'a':
	/* Pick the appropriate size for an address.  This is deferred
	   until do_examine when we know the actual architecture to use.
	   A special size value of 'a' is used to indicate this case.  */
	val.size = osize ? 'a' : osize;
	break;
      case 'f':
	/* Floating point has to be word or giantword.  */
	if (osize == 'w' || osize == 'g')
	  val.size = osize;
	else
	  /* Default it to giantword if the last used size is not
	     appropriate.  */
	  val.size = osize ? 'g' : osize;
	break;
      case 'c':
	/* Characters default to one byte.  */
	val.size = osize ? 'b' : osize;
	break;
      case 's':
	/* Display strings with byte size chars unless explicitly specified.  */
	val.size = '\0';
	break;

      default:
	/* The default is the size most recently specified.  */
	val.size = osize;
      }

  return val;
}

/* Print value VAL on stream according to OPTIONS.
   Do not end with a newline.
   SIZE is the letter for the size of datum being printed.
   This is used to pad hex numbers so they line up.  SIZE is 0
   for print / output and set for examine.  */

static void
print_formatted (struct value *val, int size,
		 const struct value_print_options *options,
		 struct ui_file *stream)
{
  struct type *type = check_typedef (value_type (val));
  int len = TYPE_LENGTH (type);

  if (VALUE_LVAL (val) == lval_memory)
    next_address = value_address (val) + len;

  if (size)
    {
      switch (options->format)
	{
	case 's':
	  {
	    struct type *elttype = value_type (val);

	    next_address = (value_address (val)
			    + val_print_string (elttype,
						value_address (val), -1,
						stream, options) * len);
	  }
	  return;

	case 'i':
	  /* We often wrap here if there are long symbolic names.  */
	  wrap_here ("    ");
	  next_address = (value_address (val)
			  + gdb_print_insn (get_type_arch (type),
					    value_address (val), stream,
					    &branch_delay_insns));
	  return;
	}
    }

  if (options->format == 0 || options->format == 's'
      || TYPE_CODE (type) == TYPE_CODE_REF
      || TYPE_CODE (type) == TYPE_CODE_ARRAY
      || TYPE_CODE (type) == TYPE_CODE_STRING
      || TYPE_CODE (type) == TYPE_CODE_STRUCT
      || TYPE_CODE (type) == TYPE_CODE_UNION
      || TYPE_CODE (type) == TYPE_CODE_NAMESPACE)
    value_print (val, stream, options);
  else
    /* User specified format, so don't look to the the type to
       tell us what to do.  */
    print_scalar_formatted (value_contents (val), type,
			    options, size, stream);
}

/* Return builtin floating point type of same length as TYPE.
   If no such type is found, return TYPE itself.  */
static struct type *
float_type_from_length (struct type *type)
{
  struct gdbarch *gdbarch = get_type_arch (type);
  const struct builtin_type *builtin = builtin_type (gdbarch);
  unsigned int len = TYPE_LENGTH (type);

  if (len == TYPE_LENGTH (builtin->builtin_float))
    type = builtin->builtin_float;
  else if (len == TYPE_LENGTH (builtin->builtin_double))
    type = builtin->builtin_double;
  else if (len == TYPE_LENGTH (builtin->builtin_long_double))
    type = builtin->builtin_long_double;

  return type;
}

/* Print a scalar of data of type TYPE, pointed to in GDB by VALADDR,
   according to OPTIONS and SIZE on STREAM.
   Formats s and i are not supported at this level.

   This is how the elements of an array or structure are printed
   with a format.  */

void
print_scalar_formatted (const void *valaddr, struct type *type,
			const struct value_print_options *options,
			int size, struct ui_file *stream)
{
  struct gdbarch *gdbarch = get_type_arch (type);
  LONGEST val_long = 0;
  unsigned int len = TYPE_LENGTH (type);
  enum bfd_endian byte_order = gdbarch_byte_order (gdbarch);

  /* If we get here with a string format, try again without it.  Go
     all the way back to the language printers, which may call us
     again.  */
  if (options->format == 's')
    {
      struct value_print_options opts = *options;
      opts.format = 0;
      opts.deref_ref = 0;
      val_print (type, valaddr, 0, 0, stream, 0, &opts,
		 current_language);
      return;
    }

  if (len > sizeof(LONGEST) &&
      (TYPE_CODE (type) == TYPE_CODE_INT
       || TYPE_CODE (type) == TYPE_CODE_ENUM))
    {
      switch (options->format)
	{
	case 'o':
	  print_octal_chars (stream, valaddr, len, byte_order);
	  return;
	case 'u':
	case 'd':
	  print_decimal_chars (stream, valaddr, len, byte_order);
	  return;
	case 't':
	  print_binary_chars (stream, valaddr, len, byte_order);
	  return;
	case 'x':
	  print_hex_chars (stream, valaddr, len, byte_order);
	  return;
	case 'c':
	  print_char_chars (stream, type, valaddr, len, byte_order);
	  return;
	default:
	  break;
	};
    }

  if (options->format != 'f')
    val_long = unpack_long (type, valaddr);

  /* If the value is a pointer, and pointers and addresses are not the
     same, then at this point, the value's length (in target bytes) is
     gdbarch_addr_bit/TARGET_CHAR_BIT, not TYPE_LENGTH (type).  */
  if (TYPE_CODE (type) == TYPE_CODE_PTR)
    len = gdbarch_addr_bit (gdbarch) / TARGET_CHAR_BIT;

  /* If we are printing it as unsigned, truncate it in case it is actually
     a negative signed value (e.g. "print/u (short)-1" should print 65535
     (if shorts are 16 bits) instead of 4294967295).  */
  if (options->format != 'd' || TYPE_UNSIGNED (type))
    {
      if (len < sizeof (LONGEST))
	val_long &= ((LONGEST) 1 << HOST_CHAR_BIT * len) - 1;
    }

  switch (options->format)
    {
    case 'x':
      if (!size)
	{
	  /* No size specified, like in print.  Print varying # of digits.  */
	  print_longest (stream, 'x', 1, val_long);
	}
      else
	switch (size)
	  {
	  case 'b':
	  case 'h':
	  case 'w':
	  case 'g':
	    print_longest (stream, size, 1, val_long);
	    break;
	  default:
	    error (_("Undefined output size \"%c\"."), size);
	  }
      break;

    case 'd':
      print_longest (stream, 'd', 1, val_long);
      break;

    case 'u':
      print_longest (stream, 'u', 0, val_long);
      break;

    case 'o':
      if (val_long)
	print_longest (stream, 'o', 1, val_long);
      else
	fprintf_filtered (stream, "0");
      break;

    case 'a':
      {
	CORE_ADDR addr = unpack_pointer (type, valaddr);

	print_address (gdbarch, addr, stream);
      }
      break;

    case 'c':
      {
	struct value_print_options opts = *options;

	opts.format = 0;
	if (TYPE_UNSIGNED (type))
	  type = builtin_type (gdbarch)->builtin_true_unsigned_char;
 	else
	  type = builtin_type (gdbarch)->builtin_true_char;

	value_print (value_from_longest (type, val_long), stream, &opts);
      }
      break;

    case 'f':
      type = float_type_from_length (type);
      print_floating (valaddr, type, stream);
      break;

    case 0:
      internal_error (__FILE__, __LINE__,
		      _("failed internal consistency check"));

    case 't':
      /* Binary; 't' stands for "two".  */
      {
	char bits[8 * (sizeof val_long) + 1];
	char buf[8 * (sizeof val_long) + 32];
	char *cp = bits;
	int width;

	if (!size)
	  width = 8 * (sizeof val_long);
	else
	  switch (size)
	    {
	    case 'b':
	      width = 8;
	      break;
	    case 'h':
	      width = 16;
	      break;
	    case 'w':
	      width = 32;
	      break;
	    case 'g':
	      width = 64;
	      break;
	    default:
	      error (_("Undefined output size \"%c\"."), size);
	    }

	bits[width] = '\0';
	while (width-- > 0)
	  {
	    bits[width] = (val_long & 1) ? '1' : '0';
	    val_long >>= 1;
	  }
	if (!size)
	  {
	    while (*cp && *cp == '0')
	      cp++;
	    if (*cp == '\0')
	      cp--;
	  }
	strcpy (buf, cp);
	fputs_filtered (buf, stream);
      }
      break;

    default:
      error (_("Undefined output format \"%c\"."), options->format);
    }
}

/* Specify default address for `x' command.
   The `info lines' command uses this.  */

void
set_next_address (struct gdbarch *gdbarch, CORE_ADDR addr)
{
  struct type *ptr_type = builtin_type (gdbarch)->builtin_data_ptr;

  next_gdbarch = gdbarch;
  next_address = addr;

  /* Make address available to the user as $_.  */
  set_internalvar (lookup_internalvar ("_"),
		   value_from_pointer (ptr_type, addr));
}

/* Optionally print address ADDR symbolically as <SYMBOL+OFFSET> on STREAM,
   after LEADIN.  Print nothing if no symbolic name is found nearby.
   Optionally also print source file and line number, if available.
   DO_DEMANGLE controls whether to print a symbol in its native "raw" form,
   or to interpret it as a possible C++ name and convert it back to source
   form.  However note that DO_DEMANGLE can be overridden by the specific
   settings of the demangle and asm_demangle variables.  */

void
print_address_symbolic (struct gdbarch *gdbarch, CORE_ADDR addr,
			struct ui_file *stream,
			int do_demangle, char *leadin)
{
  char *name = NULL;
  char *filename = NULL;
  int unmapped = 0;
  int offset = 0;
  int line = 0;

  /* Throw away both name and filename.  */
  struct cleanup *cleanup_chain = make_cleanup (free_current_contents, &name);
  make_cleanup (free_current_contents, &filename);

  if (build_address_symbolic (gdbarch, addr, do_demangle, &name, &offset,
			      &filename, &line, &unmapped))
    {
      do_cleanups (cleanup_chain);
      return;
    }

  fputs_filtered (leadin, stream);
  if (unmapped)
    fputs_filtered ("<*", stream);
  else
    fputs_filtered ("<", stream);
  fputs_filtered (name, stream);
  if (offset != 0)
    fprintf_filtered (stream, "+%u", (unsigned int) offset);

  /* Append source filename and line number if desired.  Give specific
     line # of this addr, if we have it; else line # of the nearest symbol.  */
  if (print_symbol_filename && filename != NULL)
    {
      if (line != -1)
	fprintf_filtered (stream, " at %s:%d", filename, line);
      else
	fprintf_filtered (stream, " in %s", filename);
    }
  if (unmapped)
    fputs_filtered ("*>", stream);
  else
    fputs_filtered (">", stream);

  do_cleanups (cleanup_chain);
}

/* Given an address ADDR return all the elements needed to print the
   address in a symbolic form. NAME can be mangled or not depending
   on DO_DEMANGLE (and also on the asm_demangle global variable,
   manipulated via ''set print asm-demangle''). Return 0 in case of
   success, when all the info in the OUT paramters is valid. Return 1
   otherwise. */
int
build_address_symbolic (struct gdbarch *gdbarch,
			CORE_ADDR addr,  /* IN */
			int do_demangle, /* IN */
			char **name,     /* OUT */
			int *offset,     /* OUT */
			char **filename, /* OUT */
			int *line,       /* OUT */
			int *unmapped)   /* OUT */
{
  struct minimal_symbol *msymbol;
  struct symbol *symbol;
  CORE_ADDR name_location = 0;
  struct obj_section *section = NULL;
  char *name_temp = "";
  
  /* Let's say it is mapped (not unmapped).  */
  *unmapped = 0;

  /* Determine if the address is in an overlay, and whether it is
     mapped.  */
  if (overlay_debugging)
    {
      section = find_pc_overlay (addr);
      if (pc_in_unmapped_range (addr, section))
	{
	  *unmapped = 1;
	  addr = overlay_mapped_address (addr, section);
	}
    }

  /* First try to find the address in the symbol table, then
     in the minsyms.  Take the closest one.  */

  /* This is defective in the sense that it only finds text symbols.  So
     really this is kind of pointless--we should make sure that the
     minimal symbols have everything we need (by changing that we could
     save some memory, but for many debug format--ELF/DWARF or
     anything/stabs--it would be inconvenient to eliminate those minimal
     symbols anyway).  */
  msymbol = lookup_minimal_symbol_by_pc_section (addr, section);
  symbol = find_pc_sect_function (addr, section);

  if (symbol)
    {
      /* If this is a function (i.e. a code address), strip out any
	 non-address bits.  For instance, display a pointer to the
	 first instruction of a Thumb function as <function>; the
	 second instruction will be <function+2>, even though the
	 pointer is <function+3>.  This matches the ISA behavior.  */
      addr = gdbarch_addr_bits_remove (gdbarch, addr);

      name_location = BLOCK_START (SYMBOL_BLOCK_VALUE (symbol));
      if (do_demangle || asm_demangle)
	name_temp = SYMBOL_PRINT_NAME (symbol);
      else
	name_temp = SYMBOL_LINKAGE_NAME (symbol);
    }

  if (msymbol != NULL)
    {
      if (SYMBOL_VALUE_ADDRESS (msymbol) > name_location || symbol == NULL)
	{
	  /* The msymbol is closer to the address than the symbol;
	     use the msymbol instead.  */
	  symbol = 0;
	  name_location = SYMBOL_VALUE_ADDRESS (msymbol);
	  if (do_demangle || asm_demangle)
	    name_temp = SYMBOL_PRINT_NAME (msymbol);
	  else
	    name_temp = SYMBOL_LINKAGE_NAME (msymbol);
	}
    }
  if (symbol == NULL && msymbol == NULL)
    return 1;

  /* If the nearest symbol is too far away, don't print anything symbolic.  */

  /* For when CORE_ADDR is larger than unsigned int, we do math in
     CORE_ADDR.  But when we detect unsigned wraparound in the
     CORE_ADDR math, we ignore this test and print the offset,
     because addr+max_symbolic_offset has wrapped through the end
     of the address space back to the beginning, giving bogus comparison.  */
  if (addr > name_location + max_symbolic_offset
      && name_location + max_symbolic_offset > name_location)
    return 1;

  *offset = addr - name_location;

  *name = xstrdup (name_temp);

  if (print_symbol_filename)
    {
      struct symtab_and_line sal;

      sal = find_pc_sect_line (addr, section, 0);

      if (sal.symtab)
	{
	  *filename = xstrdup (sal.symtab->filename);
	  *line = sal.line;
	}
    }
  return 0;
}


/* Print address ADDR symbolically on STREAM.
   First print it as a number.  Then perhaps print
   <SYMBOL + OFFSET> after the number.  */

void
print_address (struct gdbarch *gdbarch,
	       CORE_ADDR addr, struct ui_file *stream)
{
  fputs_filtered (paddress (gdbarch, addr), stream);
  print_address_symbolic (gdbarch, addr, stream, asm_demangle, " ");
}

/* Return a prefix for instruction address:
   "=> " for current instruction, else "   ".  */

const char *
pc_prefix (CORE_ADDR addr)
{
  if (has_stack_frames ())
    {
      struct frame_info *frame;
      CORE_ADDR pc;

      frame = get_selected_frame (NULL);
      pc = get_frame_pc (frame);

      if (pc == addr)
	return "=> ";
    }
  return "   ";
}

/* Print address ADDR symbolically on STREAM.  Parameter DEMANGLE
   controls whether to print the symbolic name "raw" or demangled.
   Global setting "addressprint" controls whether to print hex address
   or not.  */

void
print_address_demangle (struct gdbarch *gdbarch, CORE_ADDR addr,
			struct ui_file *stream, int do_demangle)
{
  struct value_print_options opts;

  get_user_print_options (&opts);
  if (addr == 0)
    {
      fprintf_filtered (stream, "0");
    }
  else if (opts.addressprint)
    {
      fputs_filtered (paddress (gdbarch, addr), stream);
      print_address_symbolic (gdbarch, addr, stream, do_demangle, " ");
    }
  else
    {
      print_address_symbolic (gdbarch, addr, stream, do_demangle, "");
    }
}


/* Examine data at address ADDR in format FMT.
   Fetch it from memory and print on gdb_stdout.  */

static void
do_examine (struct format_data fmt, struct gdbarch *gdbarch, CORE_ADDR addr)
{
  char format = 0;
  char size;
  int count = 1;
  struct type *val_type = NULL;
  int i;
  int maxelts;
  struct value_print_options opts;

  format = fmt.format;
  size = fmt.size;
  count = fmt.count;
  next_gdbarch = gdbarch;
  next_address = addr;

  /* Instruction format implies fetch single bytes
     regardless of the specified size.
     The case of strings is handled in decode_format, only explicit
     size operator are not changed to 'b'.  */
  if (format == 'i')
    size = 'b';

  if (size == 'a')
    {
      /* Pick the appropriate size for an address.  */
      if (gdbarch_ptr_bit (next_gdbarch) == 64)
	size = 'g';
      else if (gdbarch_ptr_bit (next_gdbarch) == 32)
	size = 'w';
      else if (gdbarch_ptr_bit (next_gdbarch) == 16)
	size = 'h';
      else
	/* Bad value for gdbarch_ptr_bit.  */
	internal_error (__FILE__, __LINE__,
			_("failed internal consistency check"));
    }

  if (size == 'b')
    val_type = builtin_type (next_gdbarch)->builtin_int8;
  else if (size == 'h')
    val_type = builtin_type (next_gdbarch)->builtin_int16;
  else if (size == 'w')
    val_type = builtin_type (next_gdbarch)->builtin_int32;
  else if (size == 'g')
    val_type = builtin_type (next_gdbarch)->builtin_int64;

  if (format == 's')
    {
      struct type *char_type = NULL;

      /* Search for "char16_t"  or "char32_t" types or fall back to 8-bit char
	 if type is not found.  */
      if (size == 'h')
	char_type = builtin_type (next_gdbarch)->builtin_char16;
      else if (size == 'w')
	char_type = builtin_type (next_gdbarch)->builtin_char32;
      if (char_type)
        val_type = char_type;
      else
        {
	  if (size != '\0' && size != 'b')
	    warning (_("Unable to display strings with size '%c', using 'b' \
instead."), size);
	  size = 'b';
	  val_type = builtin_type (next_gdbarch)->builtin_int8;
        }
    }

  maxelts = 8;
  if (size == 'w')
    maxelts = 4;
  if (size == 'g')
    maxelts = 2;
  if (format == 's' || format == 'i')
    maxelts = 1;

  get_formatted_print_options (&opts, format);

  /* Print as many objects as specified in COUNT, at most maxelts per line,
     with the address of the next one at the start of each line.  */

  while (count > 0)
    {
      QUIT;
      if (format == 'i')
	fputs_filtered (pc_prefix (next_address), gdb_stdout);
      print_address (next_gdbarch, next_address, gdb_stdout);
      printf_filtered (":");
      for (i = maxelts;
	   i > 0 && count > 0;
	   i--, count--)
	{
	  printf_filtered ("\t");
	  /* Note that print_formatted sets next_address for the next
	     object.  */
	  last_examine_address = next_address;

	  if (last_examine_value)
	    value_free (last_examine_value);

	  /* The value to be displayed is not fetched greedily.
	     Instead, to avoid the possibility of a fetched value not
	     being used, its retrieval is delayed until the print code
	     uses it.  When examining an instruction stream, the
	     disassembler will perform its own memory fetch using just
	     the address stored in LAST_EXAMINE_VALUE.  FIXME: Should
	     the disassembler be modified so that LAST_EXAMINE_VALUE
	     is left with the byte sequence from the last complete
	     instruction fetched from memory? */
	  last_examine_value = value_at_lazy (val_type, next_address);

	  if (last_examine_value)
	    release_value (last_examine_value);

	  print_formatted (last_examine_value, size, &opts, gdb_stdout);

	  /* Display any branch delay slots following the final insn.  */
	  if (format == 'i' && count == 1)
	    count += branch_delay_insns;
	}
      printf_filtered ("\n");
      gdb_flush (gdb_stdout);
    }
}

static void
validate_format (struct format_data fmt, char *cmdname)
{
  if (fmt.size != 0)
    error (_("Size letters are meaningless in \"%s\" command."), cmdname);
  if (fmt.count != 1)
    error (_("Item count other than 1 is meaningless in \"%s\" command."),
	   cmdname);
  if (fmt.format == 'i')
    error (_("Format letter \"%c\" is meaningless in \"%s\" command."),
	   fmt.format, cmdname);
}

/* Evaluate string EXP as an expression in the current language and
   print the resulting value.  EXP may contain a format specifier as the
   first argument ("/x myvar" for example, to print myvar in hex).  */

static void
print_command_1 (char *exp, int inspect, int voidprint)
{
  struct expression *expr;
  struct cleanup *old_chain = 0;
  char format = 0;
  struct value *val;
  struct format_data fmt;
  int cleanup = 0;

  if (exp && *exp == '/')
    {
      exp++;
      fmt = decode_format (&exp, last_format, 0);
      validate_format (fmt, "print");
      last_format = format = fmt.format;
    }
  else
    {
      fmt.count = 1;
      fmt.format = 0;
      fmt.size = 0;
      fmt.raw = 0;
    }

  if (exp && *exp)
    {
      expr = parse_expression (exp);
      old_chain = make_cleanup (free_current_contents, &expr);
      cleanup = 1;
      val = evaluate_expression (expr);
    }
  else
    val = access_value_history (0);

  if (voidprint || (val && value_type (val) &&
		    TYPE_CODE (value_type (val)) != TYPE_CODE_VOID))
    {
      struct value_print_options opts;
      int histindex = record_latest_value (val);

      if (histindex >= 0)
	annotate_value_history_begin (histindex, value_type (val));
      else
	annotate_value_begin (value_type (val));

      if (inspect)
	printf_unfiltered ("\031(gdb-makebuffer \"%s\"  %d '(\"",
			   exp, histindex);
      else if (histindex >= 0)
	printf_filtered ("$%d = ", histindex);

      if (histindex >= 0)
	annotate_value_history_value ();

      get_formatted_print_options (&opts, format);
      opts.inspect_it = inspect;
      opts.raw = fmt.raw;

      print_formatted (val, fmt.size, &opts, gdb_stdout);
      printf_filtered ("\n");

      if (histindex >= 0)
	annotate_value_history_end ();
      else
	annotate_value_end ();

      if (inspect)
	printf_unfiltered ("\") )\030");
    }

  if (cleanup)
    do_cleanups (old_chain);
}

static void
print_command (char *exp, int from_tty)
{
  print_command_1 (exp, 0, 1);
}

/* Same as print, except in epoch, it gets its own window.  */
static void
inspect_command (char *exp, int from_tty)
{
  extern int epoch_interface;

  print_command_1 (exp, epoch_interface, 1);
}

/* Same as print, except it doesn't print void results.  */
static void
call_command (char *exp, int from_tty)
{
  print_command_1 (exp, 0, 0);
}

void
output_command (char *exp, int from_tty)
{
  struct expression *expr;
  struct cleanup *old_chain;
  char format = 0;
  struct value *val;
  struct format_data fmt;
  struct value_print_options opts;

  fmt.size = 0;
  fmt.raw = 0;

  if (exp && *exp == '/')
    {
      exp++;
      fmt = decode_format (&exp, 0, 0);
      validate_format (fmt, "output");
      format = fmt.format;
    }

  expr = parse_expression (exp);
  old_chain = make_cleanup (free_current_contents, &expr);

  val = evaluate_expression (expr);

  annotate_value_begin (value_type (val));

  get_formatted_print_options (&opts, format);
  opts.raw = fmt.raw;
  print_formatted (val, fmt.size, &opts, gdb_stdout);

  annotate_value_end ();

  wrap_here ("");
  gdb_flush (gdb_stdout);

  do_cleanups (old_chain);
}

static void
set_command (char *exp, int from_tty)
{
  struct expression *expr = parse_expression (exp);
  struct cleanup *old_chain =
    make_cleanup (free_current_contents, &expr);

  evaluate_expression (expr);
  do_cleanups (old_chain);
}

static void
sym_info (char *arg, int from_tty)
{
  struct minimal_symbol *msymbol;
  struct objfile *objfile;
  struct obj_section *osect;
  CORE_ADDR addr, sect_addr;
  int matches = 0;
  unsigned int offset;

  if (!arg)
    error_no_arg (_("address"));

  addr = parse_and_eval_address (arg);
  ALL_OBJSECTIONS (objfile, osect)
  {
    /* Only process each object file once, even if there's a separate
       debug file.  */
    if (objfile->separate_debug_objfile_backlink)
      continue;

    sect_addr = overlay_mapped_address (addr, osect);

    if (obj_section_addr (osect) <= sect_addr
	&& sect_addr < obj_section_endaddr (osect)
	&& (msymbol = lookup_minimal_symbol_by_pc_section (sect_addr, osect)))
      {
	const char *obj_name, *mapped, *sec_name, *msym_name;
	char *loc_string;
	struct cleanup *old_chain;

	matches = 1;
	offset = sect_addr - SYMBOL_VALUE_ADDRESS (msymbol);
	mapped = section_is_mapped (osect) ? _("mapped") : _("unmapped");
	sec_name = osect->the_bfd_section->name;
	msym_name = SYMBOL_PRINT_NAME (msymbol);

	/* Don't print the offset if it is zero.
	   We assume there's no need to handle i18n of "sym + offset".  */
	if (offset)
	  loc_string = xstrprintf ("%s + %u", msym_name, offset);
	else
	  loc_string = xstrprintf ("%s", msym_name);

	/* Use a cleanup to free loc_string in case the user quits
	   a pagination request inside printf_filtered.  */
	old_chain = make_cleanup (xfree, loc_string);

	gdb_assert (osect->objfile && osect->objfile->name);
	obj_name = osect->objfile->name;

	if (MULTI_OBJFILE_P ())
	  if (pc_in_unmapped_range (addr, osect))
	    if (section_is_overlay (osect))
	      printf_filtered (_("%s in load address range of "
				 "%s overlay section %s of %s\n"),
			       loc_string, mapped, sec_name, obj_name);
	    else
	      printf_filtered (_("%s in load address range of "
				 "section %s of %s\n"),
			       loc_string, sec_name, obj_name);
	  else
	    if (section_is_overlay (osect))
	      printf_filtered (_("%s in %s overlay section %s of %s\n"),
			       loc_string, mapped, sec_name, obj_name);
	    else
	      printf_filtered (_("%s in section %s of %s\n"),
			       loc_string, sec_name, obj_name);
	else
	  if (pc_in_unmapped_range (addr, osect))
	    if (section_is_overlay (osect))
	      printf_filtered (_("%s in load address range of %s overlay "
				 "section %s\n"),
			       loc_string, mapped, sec_name);
	    else
	      printf_filtered (_("%s in load address range of section %s\n"),
			       loc_string, sec_name);
	  else
	    if (section_is_overlay (osect))
	      printf_filtered (_("%s in %s overlay section %s\n"),
			       loc_string, mapped, sec_name);
	    else
	      printf_filtered (_("%s in section %s\n"),
			       loc_string, sec_name);

	do_cleanups (old_chain);
      }
  }
  if (matches == 0)
    printf_filtered (_("No symbol matches %s.\n"), arg);
}

static void
address_info (char *exp, int from_tty)
{
  struct gdbarch *gdbarch;
  int regno;
  struct symbol *sym;
  struct minimal_symbol *msymbol;
  long val;
  struct obj_section *section;
  CORE_ADDR load_addr, context_pc = 0;
  int is_a_field_of_this;	/* C++: lookup_symbol sets this to nonzero
				   if exp is a field of `this'. */

  if (exp == 0)
    error (_("Argument required."));

  sym = lookup_symbol (exp, get_selected_block (&context_pc), VAR_DOMAIN,
		       &is_a_field_of_this);
  if (sym == NULL)
    {
      if (is_a_field_of_this)
	{
	  printf_filtered ("Symbol \"");
	  fprintf_symbol_filtered (gdb_stdout, exp,
				   current_language->la_language, DMGL_ANSI);
	  printf_filtered ("\" is a field of the local class variable ");
	  if (current_language->la_language == language_objc)
	    printf_filtered ("`self'\n");	/* ObjC equivalent of "this" */
	  else
	    printf_filtered ("`this'\n");
	  return;
	}

      msymbol = lookup_minimal_symbol (exp, NULL, NULL);

      if (msymbol != NULL)
	{
	  gdbarch = get_objfile_arch (msymbol_objfile (msymbol));
	  load_addr = SYMBOL_VALUE_ADDRESS (msymbol);

	  printf_filtered ("Symbol \"");
	  fprintf_symbol_filtered (gdb_stdout, exp,
				   current_language->la_language, DMGL_ANSI);
	  printf_filtered ("\" is at ");
	  fputs_filtered (paddress (gdbarch, load_addr), gdb_stdout);
	  printf_filtered (" in a file compiled without debugging");
	  section = SYMBOL_OBJ_SECTION (msymbol);
	  if (section_is_overlay (section))
	    {
	      load_addr = overlay_unmapped_address (load_addr, section);
	      printf_filtered (",\n -- loaded at ");
	      fputs_filtered (paddress (gdbarch, load_addr), gdb_stdout);
	      printf_filtered (" in overlay section %s",
			       section->the_bfd_section->name);
	    }
	  printf_filtered (".\n");
	}
      else
	error (_("No symbol \"%s\" in current context."), exp);
      return;
    }

  printf_filtered ("Symbol \"");
  fprintf_symbol_filtered (gdb_stdout, SYMBOL_PRINT_NAME (sym),
			   current_language->la_language, DMGL_ANSI);
  printf_filtered ("\" is ");
  val = SYMBOL_VALUE (sym);
  section = SYMBOL_OBJ_SECTION (sym);
  gdbarch = get_objfile_arch (SYMBOL_SYMTAB (sym)->objfile);

  switch (SYMBOL_CLASS (sym))
    {
    case LOC_CONST:
    case LOC_CONST_BYTES:
      printf_filtered ("constant");
      break;

    case LOC_LABEL:
      printf_filtered ("a label at address ");
      load_addr = SYMBOL_VALUE_ADDRESS (sym);
      fputs_filtered (paddress (gdbarch, load_addr), gdb_stdout);
      if (section_is_overlay (section))
	{
	  load_addr = overlay_unmapped_address (load_addr, section);
	  printf_filtered (",\n -- loaded at ");
	  fputs_filtered (paddress (gdbarch, load_addr), gdb_stdout);
	  printf_filtered (" in overlay section %s",
			   section->the_bfd_section->name);
	}
      break;

    case LOC_COMPUTED:
      /* FIXME: cagney/2004-01-26: It should be possible to
	 unconditionally call the SYMBOL_COMPUTED_OPS method when available.
	 Unfortunately DWARF 2 stores the frame-base (instead of the
	 function) location in a function's symbol.  Oops!  For the
	 moment enable this when/where applicable.  */
      SYMBOL_COMPUTED_OPS (sym)->describe_location (sym, context_pc, gdb_stdout);
      break;

    case LOC_REGISTER:
      /* GDBARCH is the architecture associated with the objfile the symbol
	 is defined in; the target architecture may be different, and may
	 provide additional registers.  However, we do not know the target
	 architecture at this point.  We assume the objfile architecture
	 will contain all the standard registers that occur in debug info
	 in that objfile.  */
      regno = SYMBOL_REGISTER_OPS (sym)->register_number (sym, gdbarch);

      if (SYMBOL_IS_ARGUMENT (sym))
	printf_filtered (_("an argument in register %s"),
			 gdbarch_register_name (gdbarch, regno));
      else
	printf_filtered (_("a variable in register %s"),
			 gdbarch_register_name (gdbarch, regno));
      break;

    case LOC_STATIC:
      printf_filtered (_("static storage at address "));
      load_addr = SYMBOL_VALUE_ADDRESS (sym);
      fputs_filtered (paddress (gdbarch, load_addr), gdb_stdout);
      if (section_is_overlay (section))
	{
	  load_addr = overlay_unmapped_address (load_addr, section);
	  printf_filtered (_(",\n -- loaded at "));
	  fputs_filtered (paddress (gdbarch, load_addr), gdb_stdout);
	  printf_filtered (_(" in overlay section %s"),
			   section->the_bfd_section->name);
	}
      break;

    case LOC_REGPARM_ADDR:
      /* Note comment at LOC_REGISTER.  */
      regno = SYMBOL_REGISTER_OPS (sym)->register_number (sym, gdbarch);
      printf_filtered (_("address of an argument in register %s"),
		       gdbarch_register_name (gdbarch, regno));
      break;

    case LOC_ARG:
      printf_filtered (_("an argument at offset %ld"), val);
      break;

    case LOC_LOCAL:
      printf_filtered (_("a local variable at frame offset %ld"), val);
      break;

    case LOC_REF_ARG:
      printf_filtered (_("a reference argument at offset %ld"), val);
      break;

    case LOC_TYPEDEF:
      printf_filtered (_("a typedef"));
      break;

    case LOC_BLOCK:
      printf_filtered (_("a function at address "));
      load_addr = BLOCK_START (SYMBOL_BLOCK_VALUE (sym));
      fputs_filtered (paddress (gdbarch, load_addr), gdb_stdout);
      if (section_is_overlay (section))
	{
	  load_addr = overlay_unmapped_address (load_addr, section);
	  printf_filtered (_(",\n -- loaded at "));
	  fputs_filtered (paddress (gdbarch, load_addr), gdb_stdout);
	  printf_filtered (_(" in overlay section %s"),
			   section->the_bfd_section->name);
	}
      break;

    case LOC_UNRESOLVED:
      {
	struct minimal_symbol *msym;

	msym = lookup_minimal_symbol (SYMBOL_LINKAGE_NAME (sym), NULL, NULL);
	if (msym == NULL)
	  printf_filtered ("unresolved");
	else
	  {
	    section = SYMBOL_OBJ_SECTION (msym);
	    load_addr = SYMBOL_VALUE_ADDRESS (msym);

	    if (section
		&& (section->the_bfd_section->flags & SEC_THREAD_LOCAL) != 0)
	      printf_filtered (_("a thread-local variable at offset %s "
				 "in the thread-local storage for `%s'"),
			       paddress (gdbarch, load_addr),
			       section->objfile->name);
	    else
	      {
		printf_filtered (_("static storage at address "));
		fputs_filtered (paddress (gdbarch, load_addr), gdb_stdout);
		if (section_is_overlay (section))
		  {
		    load_addr = overlay_unmapped_address (load_addr, section);
		    printf_filtered (_(",\n -- loaded at "));
		    fputs_filtered (paddress (gdbarch, load_addr), gdb_stdout);
		    printf_filtered (_(" in overlay section %s"),
				     section->the_bfd_section->name);
		  }
	      }
	  }
      }
      break;

    case LOC_OPTIMIZED_OUT:
      printf_filtered (_("optimized out"));
      break;

    default:
      printf_filtered (_("of unknown (botched) type"));
      break;
    }
  printf_filtered (".\n");
}


static void
x_command (char *exp, int from_tty)
{
  struct expression *expr;
  struct format_data fmt;
  struct cleanup *old_chain;
  struct value *val;

  fmt.format = last_format ? last_format : 'x';
  fmt.size = last_size;
  fmt.count = 1;
  fmt.raw = 0;

  if (exp && *exp == '/')
    {
      exp++;
      fmt = decode_format (&exp, last_format, last_size);
    }

  /* If we have an expression, evaluate it and use it as the address.  */

  if (exp != 0 && *exp != 0)
    {
      expr = parse_expression (exp);
      /* Cause expression not to be there any more if this command is
         repeated with Newline.  But don't clobber a user-defined
         command's definition.  */
      if (from_tty)
	*exp = 0;
      old_chain = make_cleanup (free_current_contents, &expr);
      val = evaluate_expression (expr);
      if (TYPE_CODE (value_type (val)) == TYPE_CODE_REF)
	val = coerce_ref (val);
      /* In rvalue contexts, such as this, functions are coerced into
         pointers to functions.  This makes "x/i main" work.  */
      if (/* last_format == 'i'  && */ 
	  TYPE_CODE (value_type (val)) == TYPE_CODE_FUNC
	   && VALUE_LVAL (val) == lval_memory)
	next_address = value_address (val);
      else
	next_address = value_as_address (val);

      next_gdbarch = expr->gdbarch;
      do_cleanups (old_chain);
    }

  if (!next_gdbarch)
    error_no_arg (_("starting display address"));

  do_examine (fmt, next_gdbarch, next_address);

  /* If the examine succeeds, we remember its size and format for next
     time.  Set last_size to 'b' for strings.  */
  if (fmt.format == 's')
    last_size = 'b';
  else
    last_size = fmt.size;
  last_format = fmt.format;

  /* Set a couple of internal variables if appropriate. */
  if (last_examine_value)
    {
      /* Make last address examined available to the user as $_.  Use
         the correct pointer type.  */
      struct type *pointer_type
	= lookup_pointer_type (value_type (last_examine_value));
      set_internalvar (lookup_internalvar ("_"),
		       value_from_pointer (pointer_type,
					   last_examine_address));

      /* Make contents of last address examined available to the user
	 as $__.  If the last value has not been fetched from memory
	 then don't fetch it now; instead mark it by voiding the $__
	 variable.  */
      if (value_lazy (last_examine_value))
	clear_internalvar (lookup_internalvar ("__"));
      else
	set_internalvar (lookup_internalvar ("__"), last_examine_value);
    }
}


/* Add an expression to the auto-display chain.
   Specify the expression.  */

static void
display_command (char *exp, int from_tty)
{
  struct format_data fmt;
  struct expression *expr;
  struct display *new;
  int display_it = 1;

#if defined(TUI)
  /* NOTE: cagney/2003-02-13 The `tui_active' was previously
     `tui_version'.  */
  if (tui_active && exp != NULL && *exp == '$')
    display_it = (tui_set_layout_for_display_command (exp) == TUI_FAILURE);
#endif

  if (display_it)
    {
      if (exp == 0)
	{
	  do_displays ();
	  return;
	}

      if (*exp == '/')
	{
	  exp++;
	  fmt = decode_format (&exp, 0, 0);
	  if (fmt.size && fmt.format == 0)
	    fmt.format = 'x';
	  if (fmt.format == 'i' || fmt.format == 's')
	    fmt.size = 'b';
	}
      else
	{
	  fmt.format = 0;
	  fmt.size = 0;
	  fmt.count = 0;
	  fmt.raw = 0;
	}

      innermost_block = NULL;
      expr = parse_expression (exp);

      new = (struct display *) xmalloc (sizeof (struct display));

      new->exp_string = xstrdup (exp);
      new->exp = expr;
      new->block = innermost_block;
      new->pspace = current_program_space;
      new->next = display_chain;
      new->number = ++display_number;
      new->format = fmt;
      new->enabled_p = 1;
      display_chain = new;

      if (from_tty && target_has_execution)
	do_one_display (new);

      dont_repeat ();
    }
}

static void
free_display (struct display *d)
{
  xfree (d->exp_string);
  xfree (d->exp);
  xfree (d);
}

/* Clear out the display_chain.  Done when new symtabs are loaded,
   since this invalidates the types stored in many expressions.  */

void
clear_displays (void)
{
  struct display *d;

  while ((d = display_chain) != NULL)
    {
      display_chain = d->next;
      free_display (d);
    }
}

/* Delete the auto-display number NUM.  */

static void
delete_display (int num)
{
  struct display *d1, *d;

  if (!display_chain)
    error (_("No display number %d."), num);

  if (display_chain->number == num)
    {
      d1 = display_chain;
      display_chain = d1->next;
      free_display (d1);
    }
  else
    for (d = display_chain;; d = d->next)
      {
	if (d->next == 0)
	  error (_("No display number %d."), num);
	if (d->next->number == num)
	  {
	    d1 = d->next;
	    d->next = d1->next;
	    free_display (d1);
	    break;
	  }
      }
}

/* Delete some values from the auto-display chain.
   Specify the element numbers.  */

static void
undisplay_command (char *args, int from_tty)
{
  char *p = args;
  char *p1;
  int num;

  if (args == 0)
    {
      if (query (_("Delete all auto-display expressions? ")))
	clear_displays ();
      dont_repeat ();
      return;
    }

  while (*p)
    {
      p1 = p;
      while (*p1 >= '0' && *p1 <= '9')
	p1++;
      if (*p1 && *p1 != ' ' && *p1 != '\t')
	error (_("Arguments must be display numbers."));

      num = atoi (p);

      delete_display (num);

      p = p1;
      while (*p == ' ' || *p == '\t')
	p++;
    }
  dont_repeat ();
}

/* Display a single auto-display.  
   Do nothing if the display cannot be printed in the current context,
   or if the display is disabled. */

static void
do_one_display (struct display *d)
{
  int within_current_scope;

  if (d->enabled_p == 0)
    return;

  /* The expression carries the architecture that was used at parse time.
     This is a problem if the expression depends on architecture features
     (e.g. register numbers), and the current architecture is now different.
     For example, a display statement like "display/i $pc" is expected to
     display the PC register of the current architecture, not the arch at
     the time the display command was given.  Therefore, we re-parse the
     expression if the current architecture has changed.  */
  if (d->exp != NULL && d->exp->gdbarch != get_current_arch ())
    {
      xfree (d->exp);
      d->exp = NULL;
      d->block = NULL;
    }

  if (d->exp == NULL)
    {
      volatile struct gdb_exception ex;

      TRY_CATCH (ex, RETURN_MASK_ALL)
	{
	  innermost_block = NULL;
	  d->exp = parse_expression (d->exp_string);
	  d->block = innermost_block;
	}
      if (ex.reason < 0)
	{
	  /* Can't re-parse the expression.  Disable this display item.  */
	  d->enabled_p = 0;
	  warning (_("Unable to display \"%s\": %s"),
		   d->exp_string, ex.message);
	  return;
	}
    }

  if (d->block)
    {
      if (d->pspace == current_program_space)
	within_current_scope = contained_in (get_selected_block (0), d->block);
      else
	within_current_scope = 0;
    }
  else
    within_current_scope = 1;
  if (!within_current_scope)
    return;

  current_display_number = d->number;

  annotate_display_begin ();
  printf_filtered ("%d", d->number);
  annotate_display_number_end ();
  printf_filtered (": ");
  if (d->format.size)
    {
      CORE_ADDR addr;
      struct value *val;

      annotate_display_format ();

      printf_filtered ("x/");
      if (d->format.count != 1)
	printf_filtered ("%d", d->format.count);
      printf_filtered ("%c", d->format.format);
      if (d->format.format != 'i' && d->format.format != 's')
	printf_filtered ("%c", d->format.size);
      printf_filtered (" ");

      annotate_display_expression ();

      puts_filtered (d->exp_string);
      annotate_display_expression_end ();

      if (d->format.count != 1 || d->format.format == 'i')
	printf_filtered ("\n");
      else
	printf_filtered ("  ");

      val = evaluate_expression (d->exp);
      addr = value_as_address (val);
      if (d->format.format == 'i')
	addr = gdbarch_addr_bits_remove (d->exp->gdbarch, addr);

      annotate_display_value ();

      do_examine (d->format, d->exp->gdbarch, addr);
    }
  else
    {
      struct value_print_options opts;

      annotate_display_format ();

      if (d->format.format)
	printf_filtered ("/%c ", d->format.format);

      annotate_display_expression ();

      puts_filtered (d->exp_string);
      annotate_display_expression_end ();

      printf_filtered (" = ");

      annotate_display_expression ();

      get_formatted_print_options (&opts, d->format.format);
      opts.raw = d->format.raw;
      print_formatted (evaluate_expression (d->exp),
		       d->format.size, &opts, gdb_stdout);
      printf_filtered ("\n");
    }

  annotate_display_end ();

  gdb_flush (gdb_stdout);
  current_display_number = -1;
}

/* Display all of the values on the auto-display chain which can be
   evaluated in the current scope.  */

void
do_displays (void)
{
  struct display *d;

  for (d = display_chain; d; d = d->next)
    do_one_display (d);
}

/* Delete the auto-display which we were in the process of displaying.
   This is done when there is an error or a signal.  */

void
disable_display (int num)
{
  struct display *d;

  for (d = display_chain; d; d = d->next)
    if (d->number == num)
      {
	d->enabled_p = 0;
	return;
      }
  printf_unfiltered (_("No display number %d.\n"), num);
}

void
disable_current_display (void)
{
  if (current_display_number >= 0)
    {
      disable_display (current_display_number);
      fprintf_unfiltered (gdb_stderr, _("\
Disabling display %d to avoid infinite recursion.\n"),
			  current_display_number);
    }
  current_display_number = -1;
}

static void
display_info (char *ignore, int from_tty)
{
  struct display *d;

  if (!display_chain)
    printf_unfiltered (_("There are no auto-display expressions now.\n"));
  else
    printf_filtered (_("Auto-display expressions now in effect:\n\
Num Enb Expression\n"));

  for (d = display_chain; d; d = d->next)
    {
      printf_filtered ("%d:   %c  ", d->number, "ny"[(int) d->enabled_p]);
      if (d->format.size)
	printf_filtered ("/%d%c%c ", d->format.count, d->format.size,
			 d->format.format);
      else if (d->format.format)
	printf_filtered ("/%c ", d->format.format);
      puts_filtered (d->exp_string);
      if (d->block && !contained_in (get_selected_block (0), d->block))
	printf_filtered (_(" (cannot be evaluated in the current context)"));
      printf_filtered ("\n");
      gdb_flush (gdb_stdout);
    }
}

static void
enable_display (char *args, int from_tty)
{
  char *p = args;
  char *p1;
  int num;
  struct display *d;

  if (p == 0)
    {
      for (d = display_chain; d; d = d->next)
	d->enabled_p = 1;
    }
  else
    while (*p)
      {
	p1 = p;
	while (*p1 >= '0' && *p1 <= '9')
	  p1++;
	if (*p1 && *p1 != ' ' && *p1 != '\t')
	  error (_("Arguments must be display numbers."));

	num = atoi (p);

	for (d = display_chain; d; d = d->next)
	  if (d->number == num)
	    {
	      d->enabled_p = 1;
	      goto win;
	    }
	printf_unfiltered (_("No display number %d.\n"), num);
      win:
	p = p1;
	while (*p == ' ' || *p == '\t')
	  p++;
      }
}

static void
disable_display_command (char *args, int from_tty)
{
  char *p = args;
  char *p1;
  struct display *d;

  if (p == 0)
    {
      for (d = display_chain; d; d = d->next)
	d->enabled_p = 0;
    }
  else
    while (*p)
      {
	p1 = p;
	while (*p1 >= '0' && *p1 <= '9')
	  p1++;
	if (*p1 && *p1 != ' ' && *p1 != '\t')
	  error (_("Arguments must be display numbers."));

	disable_display (atoi (p));

	p = p1;
	while (*p == ' ' || *p == '\t')
	  p++;
      }
}

/* display_chain items point to blocks and expressions.  Some expressions in
   turn may point to symbols.
   Both symbols and blocks are obstack_alloc'd on objfile_stack, and are
   obstack_free'd when a shared library is unloaded.
   Clear pointers that are about to become dangling.
   Both .exp and .block fields will be restored next time we need to display
   an item by re-parsing .exp_string field in the new execution context.  */

static void
clear_dangling_display_expressions (struct so_list *solib)
{
  struct objfile *objfile = solib->objfile;
  struct display *d;

  /* With no symbol file we cannot have a block or expression from it.  */
  if (objfile == NULL)
    return;
  if (objfile->separate_debug_objfile_backlink)
    objfile = objfile->separate_debug_objfile_backlink;
  gdb_assert (objfile->pspace == solib->pspace);

  for (d = display_chain; d != NULL; d = d->next)
    {
      if (d->pspace != solib->pspace)
	continue;

      if (lookup_objfile_from_block (d->block) == objfile
	  || (d->exp && exp_uses_objfile (d->exp, objfile)))
      {
	xfree (d->exp);
	d->exp = NULL;
	d->block = NULL;
      }
    }
}


/* Print the value in stack frame FRAME of a variable specified by a
   struct symbol.  NAME is the name to print; if NULL then VAR's print
   name will be used.  STREAM is the ui_file on which to print the
   value.  INDENT specifies the number of indent levels to print
   before printing the variable name.  */

void
print_variable_and_value (const char *name, struct symbol *var,
			  struct frame_info *frame,
			  struct ui_file *stream, int indent)
{
  struct value *val;
  struct value_print_options opts;

  if (!name)
    name = SYMBOL_PRINT_NAME (var);

  fprintf_filtered (stream, "%s%s = ", n_spaces (2 * indent), name);

  val = read_var_value (var, frame);
  get_user_print_options (&opts);
  common_val_print (val, stream, indent, &opts, current_language);
  fprintf_filtered (stream, "\n");
}

static void
printf_command (char *arg, int from_tty)
{
  char *f = NULL;
  char *s = arg;
  char *string = NULL;
  struct value **val_args;
  char *substrings;
  char *current_substring;
  int nargs = 0;
  int allocated_args = 20;
  struct cleanup *old_cleanups;

  val_args = xmalloc (allocated_args * sizeof (struct value *));
  old_cleanups = make_cleanup (free_current_contents, &val_args);

  if (s == 0)
    error_no_arg (_("format-control string and values to print"));

  /* Skip white space before format string */
  while (*s == ' ' || *s == '\t')
    s++;

  /* A format string should follow, enveloped in double quotes.  */
  if (*s++ != '"')
    error (_("Bad format string, missing '\"'."));

  /* Parse the format-control string and copy it into the string STRING,
     processing some kinds of escape sequence.  */

  f = string = (char *) alloca (strlen (s) + 1);

  while (*s != '"')
    {
      int c = *s++;
      switch (c)
	{
	case '\0':
	  error (_("Bad format string, non-terminated '\"'."));

	case '\\':
	  switch (c = *s++)
	    {
	    case '\\':
	      *f++ = '\\';
	      break;
	    case 'a':
	      *f++ = '\a';
	      break;
	    case 'b':
	      *f++ = '\b';
	      break;
	    case 'f':
	      *f++ = '\f';
	      break;
	    case 'n':
	      *f++ = '\n';
	      break;
	    case 'r':
	      *f++ = '\r';
	      break;
	    case 't':
	      *f++ = '\t';
	      break;
	    case 'v':
	      *f++ = '\v';
	      break;
	    case '"':
	      *f++ = '"';
	      break;
	    default:
	      /* ??? TODO: handle other escape sequences */
	      error (_("Unrecognized escape character \\%c in format string."),
		     c);
	    }
	  break;

	default:
	  *f++ = c;
	}
    }

  /* Skip over " and following space and comma.  */
  s++;
  *f++ = '\0';
  while (*s == ' ' || *s == '\t')
    s++;

  if (*s != ',' && *s != 0)
    error (_("Invalid argument syntax"));

  if (*s == ',')
    s++;
  while (*s == ' ' || *s == '\t')
    s++;

  /* Need extra space for the '\0's.  Doubling the size is sufficient.  */
  substrings = alloca (strlen (string) * 2);
  current_substring = substrings;

  {
    /* Now scan the string for %-specs and see what kinds of args they want.
       argclass[I] classifies the %-specs so we can give printf_filtered
       something of the right size.  */

    enum argclass
      {
	int_arg, long_arg, long_long_arg, ptr_arg,
	string_arg, wide_string_arg, wide_char_arg,
	double_arg, long_double_arg, decfloat_arg
      };
    enum argclass *argclass;
    enum argclass this_argclass;
    char *last_arg;
    int nargs_wanted;
    int i;

    argclass = (enum argclass *) alloca (strlen (s) * sizeof *argclass);
    nargs_wanted = 0;
    f = string;
    last_arg = string;
    while (*f)
      if (*f++ == '%')
	{
	  int seen_hash = 0, seen_zero = 0, lcount = 0, seen_prec = 0;
	  int seen_space = 0, seen_plus = 0;
	  int seen_big_l = 0, seen_h = 0, seen_big_h = 0;
	  int seen_big_d = 0, seen_double_big_d = 0;
	  int bad = 0;

	  /* Check the validity of the format specifier, and work
	     out what argument it expects.  We only accept C89
	     format strings, with the exception of long long (which
	     we autoconf for).  */

	  /* Skip over "%%".  */
	  if (*f == '%')
	    {
	      f++;
	      continue;
	    }

	  /* The first part of a format specifier is a set of flag
	     characters.  */
	  while (strchr ("0-+ #", *f))
	    {
	      if (*f == '#')
		seen_hash = 1;
	      else if (*f == '0')
		seen_zero = 1;
	      else if (*f == ' ')
		seen_space = 1;
	      else if (*f == '+')
		seen_plus = 1;
	      f++;
	    }

	  /* The next part of a format specifier is a width.  */
	  while (strchr ("0123456789", *f))
	    f++;

	  /* The next part of a format specifier is a precision.  */
	  if (*f == '.')
	    {
	      seen_prec = 1;
	      f++;
	      while (strchr ("0123456789", *f))
		f++;
	    }

	  /* The next part of a format specifier is a length modifier.  */
	  if (*f == 'h')
	    {
	      seen_h = 1;
	      f++;
	    }
	  else if (*f == 'l')
	    {
	      f++;
	      lcount++;
	      if (*f == 'l')
		{
		  f++;
		  lcount++;
		}
	    }
	  else if (*f == 'L')
	    {
	      seen_big_l = 1;
	      f++;
	    }
	  /* Decimal32 modifier.  */
	  else if (*f == 'H')
	    {
	      seen_big_h = 1;
	      f++;
	    }
	  /* Decimal64 and Decimal128 modifiers.  */
	  else if (*f == 'D')
	    {
	      f++;

	      /* Check for a Decimal128.  */
	      if (*f == 'D')
		{
		  f++;
		  seen_double_big_d = 1;
		}
	      else
		seen_big_d = 1;
	    }

	  switch (*f)
	    {
	    case 'u':
	      if (seen_hash)
		bad = 1;
	      /* FALLTHROUGH */

	    case 'o':
	    case 'x':
	    case 'X':
	      if (seen_space || seen_plus)
		bad = 1;
	      /* FALLTHROUGH */

	    case 'd':
	    case 'i':
	      if (lcount == 0)
		this_argclass = int_arg;
	      else if (lcount == 1)
		this_argclass = long_arg;
	      else
		this_argclass = long_long_arg;

	      if (seen_big_l)
		bad = 1;
	      break;

	    case 'c':
	      this_argclass = lcount == 0 ? int_arg : wide_char_arg;
	      if (lcount > 1 || seen_h || seen_big_l)
		bad = 1;
	      if (seen_prec || seen_zero || seen_space || seen_plus)
		bad = 1;
	      break;

	    case 'p':
	      this_argclass = ptr_arg;
	      if (lcount || seen_h || seen_big_l)
		bad = 1;
	      if (seen_prec || seen_zero || seen_space || seen_plus)
		bad = 1;
	      break;

	    case 's':
	      this_argclass = lcount == 0 ? string_arg : wide_string_arg;
	      if (lcount > 1 || seen_h || seen_big_l)
		bad = 1;
	      if (seen_zero || seen_space || seen_plus)
		bad = 1;
	      break;

	    case 'e':
	    case 'f':
	    case 'g':
	    case 'E':
	    case 'G':
	      if (seen_big_h || seen_big_d || seen_double_big_d)
		this_argclass = decfloat_arg;
	      else if (seen_big_l)
		this_argclass = long_double_arg;
	      else
		this_argclass = double_arg;

	      if (lcount || seen_h)
		bad = 1;
	      break;

	    case '*':
	      error (_("`*' not supported for precision or width in printf"));

	    case 'n':
	      error (_("Format specifier `n' not supported in printf"));

	    case '\0':
	      error (_("Incomplete format specifier at end of format string"));

	    default:
	      error (_("Unrecognized format specifier '%c' in printf"), *f);
	    }

	  if (bad)
	    error (_("Inappropriate modifiers to format specifier '%c' in printf"),
		   *f);

	  f++;

	  if (lcount > 1 && USE_PRINTF_I64)
	    {
	      /* Windows' printf does support long long, but not the usual way.
		 Convert %lld to %I64d.  */
	      int length_before_ll = f - last_arg - 1 - lcount;

	      strncpy (current_substring, last_arg, length_before_ll);
	      strcpy (current_substring + length_before_ll, "I64");
	      current_substring[length_before_ll + 3] =
		last_arg[length_before_ll + lcount];
	      current_substring += length_before_ll + 4;
	    }
	  else if (this_argclass == wide_string_arg
		   || this_argclass == wide_char_arg)
	    {
	      /* Convert %ls or %lc to %s.  */
	      int length_before_ls = f - last_arg - 2;

	      strncpy (current_substring, last_arg, length_before_ls);
	      strcpy (current_substring + length_before_ls, "s");
	      current_substring += length_before_ls + 2;
	    }
	  else
	    {
	      strncpy (current_substring, last_arg, f - last_arg);
	      current_substring += f - last_arg;
	    }
	  *current_substring++ = '\0';
	  last_arg = f;
	  argclass[nargs_wanted++] = this_argclass;
	}

    /* Now, parse all arguments and evaluate them.
       Store the VALUEs in VAL_ARGS.  */

    while (*s != '\0')
      {
	char *s1;

	if (nargs == allocated_args)
	  val_args = (struct value **) xrealloc ((char *) val_args,
						 (allocated_args *= 2)
						 * sizeof (struct value *));
	s1 = s;
	val_args[nargs] = parse_to_comma_and_eval (&s1);

	nargs++;
	s = s1;
	if (*s == ',')
	  s++;
      }

    if (nargs != nargs_wanted)
      error (_("Wrong number of arguments for specified format-string"));

    /* Now actually print them.  */
    current_substring = substrings;
    for (i = 0; i < nargs; i++)
      {
	switch (argclass[i])
	  {
	  case string_arg:
	    {
	      gdb_byte *str;
	      CORE_ADDR tem;
	      int j;

	      tem = value_as_address (val_args[i]);

	      /* This is a %s argument.  Find the length of the string.  */
	      for (j = 0;; j++)
		{
		  gdb_byte c;

		  QUIT;
		  read_memory (tem + j, &c, 1);
		  if (c == 0)
		    break;
		}

	      /* Copy the string contents into a string inside GDB.  */
	      str = (gdb_byte *) alloca (j + 1);
	      if (j != 0)
		read_memory (tem, str, j);
	      str[j] = 0;

	      printf_filtered (current_substring, (char *) str);
	    }
	    break;
	  case wide_string_arg:
	    {
	      gdb_byte *str;
	      CORE_ADDR tem;
	      int j;
	      struct gdbarch *gdbarch
		= get_type_arch (value_type (val_args[i]));
	      enum bfd_endian byte_order = gdbarch_byte_order (gdbarch);
	      struct type *wctype = lookup_typename (current_language, gdbarch,
						     "wchar_t", NULL, 0);
	      int wcwidth = TYPE_LENGTH (wctype);
	      gdb_byte *buf = alloca (wcwidth);
	      struct obstack output;
	      struct cleanup *inner_cleanup;

	      tem = value_as_address (val_args[i]);

	      /* This is a %s argument.  Find the length of the string.  */
	      for (j = 0;; j += wcwidth)
		{
		  QUIT;
		  read_memory (tem + j, buf, wcwidth);
		  if (extract_unsigned_integer (buf, wcwidth, byte_order) == 0)
		    break;
		}

	      /* Copy the string contents into a string inside GDB.  */
	      str = (gdb_byte *) alloca (j + wcwidth);
	      if (j != 0)
		read_memory (tem, str, j);
	      memset (&str[j], 0, wcwidth);

	      obstack_init (&output);
	      inner_cleanup = make_cleanup_obstack_free (&output);

	      convert_between_encodings (target_wide_charset (gdbarch),
					 host_charset (),
					 str, j, wcwidth,
					 &output, translit_char);
	      obstack_grow_str0 (&output, "");

	      printf_filtered (current_substring, obstack_base (&output));
	      do_cleanups (inner_cleanup);
	    }
	    break;
	  case wide_char_arg:
	    {
	      struct gdbarch *gdbarch
		= get_type_arch (value_type (val_args[i]));
	      struct type *wctype = lookup_typename (current_language, gdbarch,
						     "wchar_t", NULL, 0);
	      struct type *valtype;
	      struct obstack output;
	      struct cleanup *inner_cleanup;
	      const gdb_byte *bytes;

	      valtype = value_type (val_args[i]);
	      if (TYPE_LENGTH (valtype) != TYPE_LENGTH (wctype)
		  || TYPE_CODE (valtype) != TYPE_CODE_INT)
		error (_("expected wchar_t argument for %%lc"));

	      bytes = value_contents (val_args[i]);

	      obstack_init (&output);
	      inner_cleanup = make_cleanup_obstack_free (&output);

	      convert_between_encodings (target_wide_charset (gdbarch),
					 host_charset (),
					 bytes, TYPE_LENGTH (valtype),
					 TYPE_LENGTH (valtype),
					 &output, translit_char);
	      obstack_grow_str0 (&output, "");

	      printf_filtered (current_substring, obstack_base (&output));
	      do_cleanups (inner_cleanup);
	    }
	    break;
	  case double_arg:
	    {
	      struct type *type = value_type (val_args[i]);
	      DOUBLEST val;
	      int inv;

	      /* If format string wants a float, unchecked-convert the value
		 to floating point of the same size.  */
	      type = float_type_from_length (type);
	      val = unpack_double (type, value_contents (val_args[i]), &inv);
	      if (inv)
		error (_("Invalid floating value found in program."));

	      printf_filtered (current_substring, (double) val);
	      break;
	    }
	  case long_double_arg:
#ifdef HAVE_LONG_DOUBLE
	    {
	      struct type *type = value_type (val_args[i]);
	      DOUBLEST val;
	      int inv;

	      /* If format string wants a float, unchecked-convert the value
		 to floating point of the same size.  */
	      type = float_type_from_length (type);
	      val = unpack_double (type, value_contents (val_args[i]), &inv);
	      if (inv)
		error (_("Invalid floating value found in program."));

	      printf_filtered (current_substring, (long double) val);
	      break;
	    }
#else
	    error (_("long double not supported in printf"));
#endif
	  case long_long_arg:
#if defined (CC_HAS_LONG_LONG) && defined (PRINTF_HAS_LONG_LONG)
	    {
	      long long val = value_as_long (val_args[i]);

	      printf_filtered (current_substring, val);
	      break;
	    }
#else
	    error (_("long long not supported in printf"));
#endif
	  case int_arg:
	    {
	      int val = value_as_long (val_args[i]);

	      printf_filtered (current_substring, val);
	      break;
	    }
	  case long_arg:
	    {
	      long val = value_as_long (val_args[i]);

	      printf_filtered (current_substring, val);
	      break;
	    }

	  /* Handles decimal floating values.  */
	case decfloat_arg:
	    {
	      const gdb_byte *param_ptr = value_contents (val_args[i]);

#if defined (PRINTF_HAS_DECFLOAT)
	      /* If we have native support for Decimal floating
		 printing, handle it here.  */
	      printf_filtered (current_substring, param_ptr);
#else

	      /* As a workaround until vasprintf has native support for DFP
	       we convert the DFP values to string and print them using
	       the %s format specifier.  */

	      char *eos, *sos;
	      int nnull_chars = 0;

	      /* Parameter data.  */
	      struct type *param_type = value_type (val_args[i]);
	      unsigned int param_len = TYPE_LENGTH (param_type);
	      struct gdbarch *gdbarch = get_type_arch (param_type);
	      enum bfd_endian byte_order = gdbarch_byte_order (gdbarch);

	      /* DFP output data.  */
	      struct value *dfp_value = NULL;
	      gdb_byte *dfp_ptr;
	      int dfp_len = 16;
	      gdb_byte dec[16];
	      struct type *dfp_type = NULL;
	      char decstr[MAX_DECIMAL_STRING];

	      /* Points to the end of the string so that we can go back
		 and check for DFP length modifiers.  */
	      eos = current_substring + strlen (current_substring);

	      /* Look for the float/double format specifier.  */
	      while (*eos != 'f' && *eos != 'e' && *eos != 'E'
		     && *eos != 'g' && *eos != 'G')
		  eos--;

	      sos = eos;

	      /* Search for the '%' char and extract the size and type of
		 the output decimal value based on its modifiers
		 (%Hf, %Df, %DDf).  */
	      while (*--sos != '%')
		{
		  if (*sos == 'H')
		    {
		      dfp_len = 4;
		      dfp_type = builtin_type (gdbarch)->builtin_decfloat;
		    }
		  else if (*sos == 'D' && *(sos - 1) == 'D')
		    {
		      dfp_len = 16;
		      dfp_type = builtin_type (gdbarch)->builtin_declong;
		      sos--;
		    }
		  else
		    {
		      dfp_len = 8;
		      dfp_type = builtin_type (gdbarch)->builtin_decdouble;
		    }
		}

	      /* Replace %Hf, %Df and %DDf with %s's.  */
	      *++sos = 's';

	      /* Go through the whole format string and pull the correct
		 number of chars back to compensate for the change in the
		 format specifier.  */
	      while (nnull_chars < nargs - i)
		{
		  if (*eos == '\0')
		    nnull_chars++;

		  *++sos = *++eos;
		}

	      /* Conversion between different DFP types.  */
	      if (TYPE_CODE (param_type) == TYPE_CODE_DECFLOAT)
		decimal_convert (param_ptr, param_len, byte_order,
				 dec, dfp_len, byte_order);
	      else
		/* If this is a non-trivial conversion, just output 0.
		   A correct converted value can be displayed by explicitly
		   casting to a DFP type.  */
		decimal_from_string (dec, dfp_len, byte_order, "0");

	      dfp_value = value_from_decfloat (dfp_type, dec);

	      dfp_ptr = (gdb_byte *) value_contents (dfp_value);

	      decimal_to_string (dfp_ptr, dfp_len, byte_order, decstr);

	      /* Print the DFP value.  */
	      printf_filtered (current_substring, decstr);

	      break;
#endif
	    }

	  case ptr_arg:
	    {
	      /* We avoid the host's %p because pointers are too
		 likely to be the wrong size.  The only interesting
		 modifier for %p is a width; extract that, and then
		 handle %p as glibc would: %#x or a literal "(nil)".  */

	      char *p, *fmt, *fmt_p;
#if defined (CC_HAS_LONG_LONG) && defined (PRINTF_HAS_LONG_LONG)
	      long long val = value_as_long (val_args[i]);
#else
	      long val = value_as_long (val_args[i]);
#endif

	      fmt = alloca (strlen (current_substring) + 5);

	      /* Copy up to the leading %.  */
	      p = current_substring;
	      fmt_p = fmt;
	      while (*p)
		{
		  int is_percent = (*p == '%');

		  *fmt_p++ = *p++;
		  if (is_percent)
		    {
		      if (*p == '%')
			*fmt_p++ = *p++;
		      else
			break;
		    }
		}

	      if (val != 0)
		*fmt_p++ = '#';

	      /* Copy any width.  */
	      while (*p >= '0' && *p < '9')
		*fmt_p++ = *p++;

	      gdb_assert (*p == 'p' && *(p + 1) == '\0');
	      if (val != 0)
		{
#if defined (CC_HAS_LONG_LONG) && defined (PRINTF_HAS_LONG_LONG)
		  *fmt_p++ = 'l';
#endif
		  *fmt_p++ = 'l';
		  *fmt_p++ = 'x';
		  *fmt_p++ = '\0';
		  printf_filtered (fmt, val);
		}
	      else
		{
		  *fmt_p++ = 's';
		  *fmt_p++ = '\0';
		  printf_filtered (fmt, "(nil)");
		}

	      break;
	    }
	  default:
	    internal_error (__FILE__, __LINE__,
			    _("failed internal consistency check"));
	  }
	/* Skip to the next substring.  */
	current_substring += strlen (current_substring) + 1;
      }
    /* Print the portion of the format string after the last argument.
       Note that this will not include any ordinary %-specs, but it
       might include "%%".  That is why we use printf_filtered and not
       puts_filtered here.  Also, we pass a dummy argument because
       some platforms have modified GCC to include -Wformat-security
       by default, which will warn here if there is no argument.  */
    printf_filtered (last_arg, 0);
  }
  do_cleanups (old_cleanups);
}

void
_initialize_printcmd (void)
{
  struct cmd_list_element *c;

  current_display_number = -1;

  observer_attach_solib_unloaded (clear_dangling_display_expressions);

  add_info ("address", address_info,
	    _("Describe where symbol SYM is stored."));

  add_info ("symbol", sym_info, _("\
Describe what symbol is at location ADDR.\n\
Only for symbols with fixed locations (global or static scope)."));

  add_com ("x", class_vars, x_command, _("\
Examine memory: x/FMT ADDRESS.\n\
ADDRESS is an expression for the memory address to examine.\n\
FMT is a repeat count followed by a format letter and a size letter.\n\
Format letters are o(octal), x(hex), d(decimal), u(unsigned decimal),\n\
  t(binary), f(float), a(address), i(instruction), c(char) and s(string).\n\
Size letters are b(byte), h(halfword), w(word), g(giant, 8 bytes).\n\
The specified number of objects of the specified size are printed\n\
according to the format.\n\n\
Defaults for format and size letters are those previously used.\n\
Default count is 1.  Default address is following last thing printed\n\
with this command or \"print\"."));

#if 0
  add_com ("whereis", class_vars, whereis_command,
	   _("Print line number and file of definition of variable."));
#endif

  add_info ("display", display_info, _("\
Expressions to display when program stops, with code numbers."));

  add_cmd ("undisplay", class_vars, undisplay_command, _("\
Cancel some expressions to be displayed when program stops.\n\
Arguments are the code numbers of the expressions to stop displaying.\n\
No argument means cancel all automatic-display expressions.\n\
\"delete display\" has the same effect as this command.\n\
Do \"info display\" to see current list of code numbers."),
	   &cmdlist);

  add_com ("display", class_vars, display_command, _("\
Print value of expression EXP each time the program stops.\n\
/FMT may be used before EXP as in the \"print\" command.\n\
/FMT \"i\" or \"s\" or including a size-letter is allowed,\n\
as in the \"x\" command, and then EXP is used to get the address to examine\n\
and examining is done as in the \"x\" command.\n\n\
With no argument, display all currently requested auto-display expressions.\n\
Use \"undisplay\" to cancel display requests previously made."));

  add_cmd ("display", class_vars, enable_display, _("\
Enable some expressions to be displayed when program stops.\n\
Arguments are the code numbers of the expressions to resume displaying.\n\
No argument means enable all automatic-display expressions.\n\
Do \"info display\" to see current list of code numbers."), &enablelist);

  add_cmd ("display", class_vars, disable_display_command, _("\
Disable some expressions to be displayed when program stops.\n\
Arguments are the code numbers of the expressions to stop displaying.\n\
No argument means disable all automatic-display expressions.\n\
Do \"info display\" to see current list of code numbers."), &disablelist);

  add_cmd ("display", class_vars, undisplay_command, _("\
Cancel some expressions to be displayed when program stops.\n\
Arguments are the code numbers of the expressions to stop displaying.\n\
No argument means cancel all automatic-display expressions.\n\
Do \"info display\" to see current list of code numbers."), &deletelist);

  add_com ("printf", class_vars, printf_command, _("\
printf \"printf format string\", arg1, arg2, arg3, ..., argn\n\
This is useful for formatted output in user-defined commands."));

  add_com ("output", class_vars, output_command, _("\
Like \"print\" but don't put in value history and don't print newline.\n\
This is useful in user-defined commands."));

  add_prefix_cmd ("set", class_vars, set_command, _("\
Evaluate expression EXP and assign result to variable VAR, using assignment\n\
syntax appropriate for the current language (VAR = EXP or VAR := EXP for\n\
example).  VAR may be a debugger \"convenience\" variable (names starting\n\
with $), a register (a few standard names starting with $), or an actual\n\
variable in the program being debugged.  EXP is any valid expression.\n\
Use \"set variable\" for variables with names identical to set subcommands.\n\
\n\
With a subcommand, this command modifies parts of the gdb environment.\n\
You can see these environment settings with the \"show\" command."),
		  &setlist, "set ", 1, &cmdlist);
  if (dbx_commands)
    add_com ("assign", class_vars, set_command, _("\
Evaluate expression EXP and assign result to variable VAR, using assignment\n\
syntax appropriate for the current language (VAR = EXP or VAR := EXP for\n\
example).  VAR may be a debugger \"convenience\" variable (names starting\n\
with $), a register (a few standard names starting with $), or an actual\n\
variable in the program being debugged.  EXP is any valid expression.\n\
Use \"set variable\" for variables with names identical to set subcommands.\n\
\nWith a subcommand, this command modifies parts of the gdb environment.\n\
You can see these environment settings with the \"show\" command."));

  /* "call" is the same as "set", but handy for dbx users to call fns. */
  c = add_com ("call", class_vars, call_command, _("\
Call a function in the program.\n\
The argument is the function name and arguments, in the notation of the\n\
current working language.  The result is printed and saved in the value\n\
history, if it is not void."));
  set_cmd_completer (c, expression_completer);

  add_cmd ("variable", class_vars, set_command, _("\
Evaluate expression EXP and assign result to variable VAR, using assignment\n\
syntax appropriate for the current language (VAR = EXP or VAR := EXP for\n\
example).  VAR may be a debugger \"convenience\" variable (names starting\n\
with $), a register (a few standard names starting with $), or an actual\n\
variable in the program being debugged.  EXP is any valid expression.\n\
This may usually be abbreviated to simply \"set\"."),
	   &setlist);

  c = add_com ("print", class_vars, print_command, _("\
Print value of expression EXP.\n\
Variables accessible are those of the lexical environment of the selected\n\
stack frame, plus all those whose scope is global or an entire file.\n\
\n\
$NUM gets previous value number NUM.  $ and $$ are the last two values.\n\
$$NUM refers to NUM'th value back from the last one.\n\
Names starting with $ refer to registers (with the values they would have\n\
if the program were to return to the stack frame now selected, restoring\n\
all registers saved by frames farther in) or else to debugger\n\
\"convenience\" variables (any such name not a known register).\n\
Use assignment expressions to give values to convenience variables.\n\
\n\
{TYPE}ADREXP refers to a datum of data type TYPE, located at address ADREXP.\n\
@ is a binary operator for treating consecutive data objects\n\
anywhere in memory as an array.  FOO@NUM gives an array whose first\n\
element is FOO, whose second element is stored in the space following\n\
where FOO is stored, etc.  FOO must be an expression whose value\n\
resides in memory.\n\
\n\
EXP may be preceded with /FMT, where FMT is a format letter\n\
but no count or size letter (see \"x\" command)."));
  set_cmd_completer (c, expression_completer);
  add_com_alias ("p", "print", class_vars, 1);

  c = add_com ("inspect", class_vars, inspect_command, _("\
Same as \"print\" command, except that if you are running in the epoch\n\
environment, the value is printed in its own window."));
  set_cmd_completer (c, expression_completer);

  add_setshow_uinteger_cmd ("max-symbolic-offset", no_class,
			    &max_symbolic_offset, _("\
Set the largest offset that will be printed in <symbol+1234> form."), _("\
Show the largest offset that will be printed in <symbol+1234> form."), NULL,
			    NULL,
			    show_max_symbolic_offset,
			    &setprintlist, &showprintlist);
  add_setshow_boolean_cmd ("symbol-filename", no_class,
			   &print_symbol_filename, _("\
Set printing of source filename and line number with <symbol>."), _("\
Show printing of source filename and line number with <symbol>."), NULL,
			   NULL,
			   show_print_symbol_filename,
			   &setprintlist, &showprintlist);
}

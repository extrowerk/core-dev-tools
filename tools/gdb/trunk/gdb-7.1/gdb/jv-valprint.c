/* Support for printing Java values for GDB, the GNU debugger.

   Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2007,
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
#include "symtab.h"
#include "gdbtypes.h"
#include "gdbcore.h"
#include "expression.h"
#include "value.h"
#include "demangle.h"
#include "valprint.h"
#include "language.h"
#include "jv-lang.h"
#include "c-lang.h"
#include "annotate.h"
#include "gdb_string.h"

/* Local functions */

int
java_value_print (struct value *val, struct ui_file *stream, 
		  const struct value_print_options *options)
{
  struct gdbarch *gdbarch = get_type_arch (value_type (val));
  enum bfd_endian byte_order = gdbarch_byte_order (gdbarch);
  struct type *type;
  CORE_ADDR address;
  int i;
  char *name;
  struct value_print_options opts;

  type = value_type (val);
  address = value_address (val);

  if (is_object_type (type))
    {
      CORE_ADDR obj_addr;

      /* Get the run-time type, and cast the object into that */

      obj_addr = unpack_pointer (type, value_contents (val));

      if (obj_addr != 0)
	{
	  type = type_from_class (gdbarch, java_class_from_object (val));
	  type = lookup_pointer_type (type);

	  val = value_at (type, address);
	}
    }

  if (TYPE_CODE (type) == TYPE_CODE_PTR && !value_logical_not (val))
    type_print (TYPE_TARGET_TYPE (type), "", stream, -1);

  name = TYPE_TAG_NAME (type);
  if (TYPE_CODE (type) == TYPE_CODE_STRUCT && name != NULL
      && (i = strlen (name), name[i - 1] == ']'))
    {
      gdb_byte buf4[4];
      long length;
      unsigned int things_printed = 0;
      int reps;
      struct type *el_type
	= java_primitive_type_from_name (gdbarch, name, i - 2);

      i = 0;
      read_memory (address + get_java_object_header_size (gdbarch), buf4, 4);

      length = (long) extract_signed_integer (buf4, 4, byte_order);
      fprintf_filtered (stream, "{length: %ld", length);

      if (el_type == NULL)
	{
	  CORE_ADDR element;
	  CORE_ADDR next_element = -1; /* dummy initial value */

	  /* Skip object header and length. */
	  address += get_java_object_header_size (gdbarch) + 4;

	  while (i < length && things_printed < options->print_max)
	    {
	      gdb_byte *buf;

	      buf = alloca (gdbarch_ptr_bit (gdbarch) / HOST_CHAR_BIT);
	      fputs_filtered (", ", stream);
	      wrap_here (n_spaces (2));

	      if (i > 0)
		element = next_element;
	      else
		{
		  read_memory (address, buf, sizeof (buf));
		  address += gdbarch_ptr_bit (gdbarch) / HOST_CHAR_BIT;
		  /* FIXME: cagney/2003-05-24: Bogus or what.  It
                     pulls a host sized pointer out of the target and
                     then extracts that as an address (while assuming
                     that the address is unsigned)!  */
		  element = extract_unsigned_integer (buf, sizeof (buf),
						      byte_order);
		}

	      for (reps = 1; i + reps < length; reps++)
		{
		  read_memory (address, buf, sizeof (buf));
		  address += gdbarch_ptr_bit (gdbarch) / HOST_CHAR_BIT;
		  /* FIXME: cagney/2003-05-24: Bogus or what.  It
                     pulls a host sized pointer out of the target and
                     then extracts that as an address (while assuming
                     that the address is unsigned)!  */
		  next_element = extract_unsigned_integer (buf, sizeof (buf),
							   byte_order);
		  if (next_element != element)
		    break;
		}

	      if (reps == 1)
		fprintf_filtered (stream, "%d: ", i);
	      else
		fprintf_filtered (stream, "%d..%d: ", i, i + reps - 1);

	      if (element == 0)
		fprintf_filtered (stream, "null");
	      else
		fprintf_filtered (stream, "@%s", paddress (gdbarch, element));

	      things_printed++;
	      i += reps;
	    }
	}
      else
	{
	  struct value *v = allocate_value (el_type);
	  struct value *next_v = allocate_value (el_type);

	  set_value_address (v, (address
				 + get_java_object_header_size (gdbarch) + 4));
	  set_value_address (next_v, value_raw_address (v));

	  while (i < length && things_printed < options->print_max)
	    {
	      fputs_filtered (", ", stream);
	      wrap_here (n_spaces (2));

	      if (i > 0)
		{
		  struct value *tmp;

		  tmp = next_v;
		  next_v = v;
		  v = tmp;
		}
	      else
		{
		  set_value_lazy (v, 1);
		  set_value_offset (v, 0);
		}

	      set_value_offset (next_v, value_offset (v));

	      for (reps = 1; i + reps < length; reps++)
		{
		  set_value_lazy (next_v, 1);
		  set_value_offset (next_v, value_offset (next_v) + TYPE_LENGTH (el_type));
		  if (memcmp (value_contents (v), value_contents (next_v),
			      TYPE_LENGTH (el_type)) != 0)
		    break;
		}

	      if (reps == 1)
		fprintf_filtered (stream, "%d: ", i);
	      else
		fprintf_filtered (stream, "%d..%d: ", i, i + reps - 1);

	      opts = *options;
	      opts.deref_ref = 1;
	      common_val_print (v, stream, 1, &opts, current_language);

	      things_printed++;
	      i += reps;
	    }
	}

      if (i < length)
	fprintf_filtered (stream, "...");

      fprintf_filtered (stream, "}");

      return 0;
    }

  /* If it's type String, print it */

  if (TYPE_CODE (type) == TYPE_CODE_PTR
      && TYPE_TARGET_TYPE (type)
      && TYPE_TAG_NAME (TYPE_TARGET_TYPE (type))
      && strcmp (TYPE_TAG_NAME (TYPE_TARGET_TYPE (type)),
		 "java.lang.String") == 0
      && (options->format == 0 || options->format == 's')
      && address != 0
      && value_as_address (val) != 0)
    {
      struct type *char_type;
      struct value *data_val;
      CORE_ADDR data;
      struct value *boffset_val;
      unsigned long boffset;
      struct value *count_val;
      unsigned long count;
      struct value *mark;

      mark = value_mark ();	/* Remember start of new values */

      data_val = value_struct_elt (&val, NULL, "data", NULL, NULL);
      data = value_as_address (data_val);

      boffset_val = value_struct_elt (&val, NULL, "boffset", NULL, NULL);
      boffset = value_as_address (boffset_val);

      count_val = value_struct_elt (&val, NULL, "count", NULL, NULL);
      count = value_as_address (count_val);

      value_free_to_mark (mark);	/* Release unnecessary values */

      char_type = builtin_java_type (gdbarch)->builtin_char;
      val_print_string (char_type, data + boffset, count, stream, options);

      return 0;
    }

  opts = *options;
  opts.deref_ref = 1;
  return common_val_print (val, stream, 0, &opts, current_language);
}

/* TYPE, VALADDR, ADDRESS, STREAM, RECURSE, and OPTIONS have the
   same meanings as in cp_print_value and c_val_print.

   DONT_PRINT is an array of baseclass types that we
   should not print, or zero if called from top level.  */

static void
java_print_value_fields (struct type *type, const gdb_byte *valaddr,
			 CORE_ADDR address, struct ui_file *stream,
			 int recurse,
			 const struct value_print_options *options)
{
  int i, len, n_baseclasses;

  CHECK_TYPEDEF (type);

  fprintf_filtered (stream, "{");
  len = TYPE_NFIELDS (type);
  n_baseclasses = TYPE_N_BASECLASSES (type);

  if (n_baseclasses > 0)
    {
      int i, n_baseclasses = TYPE_N_BASECLASSES (type);

      for (i = 0; i < n_baseclasses; i++)
	{
	  int boffset;
	  struct type *baseclass = check_typedef (TYPE_BASECLASS (type, i));
	  char *basename = TYPE_NAME (baseclass);
	  const gdb_byte *base_valaddr;

	  if (BASETYPE_VIA_VIRTUAL (type, i))
	    continue;

	  if (basename != NULL && strcmp (basename, "java.lang.Object") == 0)
	    continue;

	  boffset = 0;

	  if (options->pretty)
	    {
	      fprintf_filtered (stream, "\n");
	      print_spaces_filtered (2 * (recurse + 1), stream);
	    }
	  fputs_filtered ("<", stream);
	  /* Not sure what the best notation is in the case where there is no
	     baseclass name.  */
	  fputs_filtered (basename ? basename : "", stream);
	  fputs_filtered ("> = ", stream);

	  base_valaddr = valaddr;

	  java_print_value_fields (baseclass, base_valaddr, address + boffset,
				   stream, recurse + 1, options);
	  fputs_filtered (", ", stream);
	}

    }

  if (!len && n_baseclasses == 1)
    fprintf_filtered (stream, "<No data fields>");
  else
    {
      int fields_seen = 0;

      for (i = n_baseclasses; i < len; i++)
	{
	  /* If requested, skip printing of static fields.  */
	  if (field_is_static (&TYPE_FIELD (type, i)))
	    {
	      char *name = TYPE_FIELD_NAME (type, i);

	      if (!options->static_field_print)
		continue;
	      if (name != NULL && strcmp (name, "class") == 0)
		continue;
	    }
	  if (fields_seen)
	    fprintf_filtered (stream, ", ");
	  else if (n_baseclasses > 0)
	    {
	      if (options->pretty)
		{
		  fprintf_filtered (stream, "\n");
		  print_spaces_filtered (2 + 2 * recurse, stream);
		  fputs_filtered ("members of ", stream);
		  fputs_filtered (type_name_no_tag (type), stream);
		  fputs_filtered (": ", stream);
		}
	    }
	  fields_seen = 1;

	  if (options->pretty)
	    {
	      fprintf_filtered (stream, "\n");
	      print_spaces_filtered (2 + 2 * recurse, stream);
	    }
	  else
	    {
	      wrap_here (n_spaces (2 + 2 * recurse));
	    }
	  if (options->inspect_it)
	    {
	      if (TYPE_CODE (TYPE_FIELD_TYPE (type, i)) == TYPE_CODE_PTR)
		fputs_filtered ("\"( ptr \"", stream);
	      else
		fputs_filtered ("\"( nodef \"", stream);
	      if (field_is_static (&TYPE_FIELD (type, i)))
		fputs_filtered ("static ", stream);
	      fprintf_symbol_filtered (stream, TYPE_FIELD_NAME (type, i),
				       language_cplus,
				       DMGL_PARAMS | DMGL_ANSI);
	      fputs_filtered ("\" \"", stream);
	      fprintf_symbol_filtered (stream, TYPE_FIELD_NAME (type, i),
				       language_cplus,
				       DMGL_PARAMS | DMGL_ANSI);
	      fputs_filtered ("\") \"", stream);
	    }
	  else
	    {
	      annotate_field_begin (TYPE_FIELD_TYPE (type, i));

	      if (field_is_static (&TYPE_FIELD (type, i)))
		fputs_filtered ("static ", stream);
	      fprintf_symbol_filtered (stream, TYPE_FIELD_NAME (type, i),
				       language_cplus,
				       DMGL_PARAMS | DMGL_ANSI);
	      annotate_field_name_end ();
	      fputs_filtered (": ", stream);
	      annotate_field_value ();
	    }

	  if (!field_is_static (&TYPE_FIELD (type, i))
	      && TYPE_FIELD_PACKED (type, i))
	    {
	      struct value *v;

	      /* Bitfields require special handling, especially due to byte
	         order problems.  */
	      if (TYPE_FIELD_IGNORE (type, i))
		{
		  fputs_filtered ("<optimized out or zero length>", stream);
		}
	      else
		{
		  struct value_print_options opts;

		  v = value_from_longest (TYPE_FIELD_TYPE (type, i),
				   unpack_field_as_long (type, valaddr, i));

		  opts = *options;
		  opts.deref_ref = 0;
		  common_val_print (v, stream, recurse + 1,
				    &opts, current_language);
		}
	    }
	  else
	    {
	      if (TYPE_FIELD_IGNORE (type, i))
		{
		  fputs_filtered ("<optimized out or zero length>", stream);
		}
	      else if (field_is_static (&TYPE_FIELD (type, i)))
		{
		  struct value *v = value_static_field (type, i);

		  if (v == NULL)
		    fputs_filtered ("<optimized out>", stream);
		  else
		    {
		      struct value_print_options opts;
		      struct type *t = check_typedef (value_type (v));

		      if (TYPE_CODE (t) == TYPE_CODE_STRUCT)
			v = value_addr (v);
		      opts = *options;
		      opts.deref_ref = 0;
		      common_val_print (v, stream, recurse + 1,
					&opts, current_language);
		    }
		}
	      else if (TYPE_FIELD_TYPE (type, i) == NULL)
		fputs_filtered ("<unknown type>", stream);
	      else
		{
		  struct value_print_options opts = *options;

		  opts.deref_ref = 0;
		  val_print (TYPE_FIELD_TYPE (type, i),
			     valaddr + TYPE_FIELD_BITPOS (type, i) / 8, 0,
			     address + TYPE_FIELD_BITPOS (type, i) / 8,
			     stream, recurse + 1, &opts,
			     current_language);
		}
	    }
	  annotate_field_end ();
	}

      if (options->pretty)
	{
	  fprintf_filtered (stream, "\n");
	  print_spaces_filtered (2 * recurse, stream);
	}
    }
  fprintf_filtered (stream, "}");
}

/* Print data of type TYPE located at VALADDR (within GDB), which came from
   the inferior at address ADDRESS, onto stdio stream STREAM according to
   OPTIONS.  The data at VALADDR is in target byte order.

   If the data are a string pointer, returns the number of string characters
   printed.  */

int
java_val_print (struct type *type, const gdb_byte *valaddr,
		int embedded_offset, CORE_ADDR address,
		struct ui_file *stream, int recurse,
		const struct value_print_options *options)
{
  struct gdbarch *gdbarch = get_type_arch (type);
  unsigned int i = 0;	/* Number of characters printed */
  struct type *target_type;
  CORE_ADDR addr;

  CHECK_TYPEDEF (type);
  switch (TYPE_CODE (type))
    {
    case TYPE_CODE_PTR:
      if (options->format && options->format != 's')
	{
	  print_scalar_formatted (valaddr, type, options, 0, stream);
	  break;
	}
#if 0
      if (options->vtblprint && cp_is_vtbl_ptr_type (type))
	{
	  /* Print the unmangled name if desired.  */
	  /* Print vtable entry - we only get here if we ARE using
	     -fvtable_thunks.  (Otherwise, look under TYPE_CODE_STRUCT.) */
	  /* Extract an address, assume that it is unsigned.  */
	  print_address_demangle (gdbarch,
				  extract_unsigned_integer (valaddr, TYPE_LENGTH (type)),
				  stream, demangle);
	  break;
	}
#endif
      addr = unpack_pointer (type, valaddr);
      if (addr == 0)
	{
	  fputs_filtered ("null", stream);
	  return i;
	}
      target_type = check_typedef (TYPE_TARGET_TYPE (type));

      if (TYPE_CODE (target_type) == TYPE_CODE_FUNC)
	{
	  /* Try to print what function it points to.  */
	  print_address_demangle (gdbarch, addr, stream, demangle);
	  /* Return value is irrelevant except for string pointers.  */
	  return (0);
	}

      if (options->addressprint && options->format != 's')
	{
	  fputs_filtered ("@", stream);
	  print_longest (stream, 'x', 0, (ULONGEST) addr);
	}

      return i;

    case TYPE_CODE_CHAR:
    case TYPE_CODE_INT:
      /* Can't just call c_val_print because that prints bytes as C
	 chars.  */
      if (options->format || options->output_format)
	{
	  struct value_print_options opts = *options;

	  opts.format = (options->format ? options->format
			 : options->output_format);
	  print_scalar_formatted (valaddr, type, &opts, 0, stream);
	}
      else if (TYPE_CODE (type) == TYPE_CODE_CHAR
	       || (TYPE_CODE (type) == TYPE_CODE_INT
		   && TYPE_LENGTH (type) == 2
		   && strcmp (TYPE_NAME (type), "char") == 0))
	LA_PRINT_CHAR ((int) unpack_long (type, valaddr), type, stream);
      else
	val_print_type_code_int (type, valaddr, stream);
      break;

    case TYPE_CODE_STRUCT:
      java_print_value_fields (type, valaddr, address, stream, recurse,
			       options);
      break;

    default:
      return c_val_print (type, valaddr, embedded_offset, address, stream,
			  recurse, options);
    }

  return 0;
}

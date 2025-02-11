/* Scheme/Guile language support routines for GDB, the GNU debugger.

   Copyright (C) 1995, 1996, 1998, 2000, 2001, 2002, 2003, 2004, 2005, 2007,
   2008, 2009 Free Software Foundation, Inc.

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
#include "expression.h"
#include "parser-defs.h"
#include "language.h"
#include "value.h"
#include "c-lang.h"
#include "scm-lang.h"
#include "scm-tags.h"
#include "source.h"
#include "gdb_string.h"
#include "gdbcore.h"
#include "infcall.h"
#include "objfiles.h"

extern void _initialize_scheme_language (void);
static struct value *scm_lookup_name (struct gdbarch *, char *);
static int in_eval_c (void);

void
scm_printchar (int c, struct type *type, struct ui_file *stream)
{
  fprintf_filtered (stream, "#\\%c", c);
}

static void
scm_printstr (struct ui_file *stream, struct type *type, const gdb_byte *string,
	      unsigned int length, int force_ellipses,
	      const struct value_print_options *options)
{
  fprintf_filtered (stream, "\"%s\"", string);
}

int
is_scmvalue_type (struct type *type)
{
  if (TYPE_NAME (type) && strcmp (TYPE_NAME (type), "SCM") == 0)
    {
      return 1;
    }
  return 0;
}

/* Get the INDEX'th SCM value, assuming SVALUE is the address
   of the 0'th one.  */

LONGEST
scm_get_field (LONGEST svalue, int index, int size,
	       enum bfd_endian byte_order)
{
  gdb_byte buffer[20];
  read_memory (SCM2PTR (svalue) + index * size, buffer, size);
  return extract_signed_integer (buffer, size, byte_order);
}

/* Unpack a value of type TYPE in buffer VALADDR as an integer
   (if CONTEXT == TYPE_CODE_IN), a pointer (CONTEXT == TYPE_CODE_PTR),
   or Boolean (CONTEXT == TYPE_CODE_BOOL).  */

LONGEST
scm_unpack (struct type *type, const gdb_byte *valaddr, enum type_code context)
{
  if (is_scmvalue_type (type))
    {
      enum bfd_endian byte_order = gdbarch_byte_order (get_type_arch (type));
      LONGEST svalue
	= extract_signed_integer (valaddr, TYPE_LENGTH (type), byte_order);

      if (context == TYPE_CODE_BOOL)
	{
	  if (svalue == SCM_BOOL_F)
	    return 0;
	  else
	    return 1;
	}
      switch (7 & (int) svalue)
	{
	case 2:
	case 6:		/* fixnum */
	  return svalue >> 2;
	case 4:		/* other immediate value */
	  if (SCM_ICHRP (svalue))	/* character */
	    return SCM_ICHR (svalue);
	  else if (SCM_IFLAGP (svalue))
	    {
	      switch ((int) svalue)
		{
#ifndef SICP
		case SCM_EOL:
#endif
		case SCM_BOOL_F:
		  return 0;
		case SCM_BOOL_T:
		  return 1;
		}
	    }
	  error (_("Value can't be converted to integer."));
	default:
	  return svalue;
	}
    }
  else
    return unpack_long (type, valaddr);
}

/* True if we're correctly in Guile's eval.c (the evaluator and apply). */

static int
in_eval_c (void)
{
  struct symtab_and_line cursal = get_current_source_symtab_and_line ();
  
  if (cursal.symtab && cursal.symtab->filename)
    {
      char *filename = cursal.symtab->filename;
      int len = strlen (filename);
      if (len >= 6 && strcmp (filename + len - 6, "eval.c") == 0)
	return 1;
    }
  return 0;
}

/* Lookup a value for the variable named STR.
   First lookup in Scheme context (using the scm_lookup_cstr inferior
   function), then try lookup_symbol for compiled variables. */

static struct value *
scm_lookup_name (struct gdbarch *gdbarch, char *str)
{
  struct value *args[3];
  int len = strlen (str);
  struct value *func;
  struct value *val;
  struct symbol *sym;

  func = find_function_in_inferior ("scm_lookup_cstr", NULL);

  args[0] = value_allocate_space_in_inferior (len);
  args[1] = value_from_longest (builtin_type (gdbarch)->builtin_int, len);
  write_memory (value_as_long (args[0]), (gdb_byte *) str, len);

  if (in_eval_c ()
      && (sym = lookup_symbol ("env",
			       expression_context_block,
			       VAR_DOMAIN, (int *) NULL)) != NULL)
    args[2] = value_of_variable (sym, expression_context_block);
  else
    /* FIXME in this case, we should try lookup_symbol first */
    args[2] = value_from_longest (builtin_scm_type (gdbarch)->builtin_scm,
				  SCM_EOL);

  val = call_function_by_hand (func, 3, args);
  if (!value_logical_not (val))
    return value_ind (val);

  sym = lookup_symbol (str,
		       expression_context_block,
		       VAR_DOMAIN, (int *) NULL);
  if (sym)
    return value_of_variable (sym, NULL);
  error (_("No symbol \"%s\" in current context."), str);
}

struct value *
scm_evaluate_string (char *str, int len)
{
  struct value *func;
  struct value *addr = value_allocate_space_in_inferior (len + 1);
  LONGEST iaddr = value_as_long (addr);
  write_memory (iaddr, (gdb_byte *) str, len);
  /* FIXME - should find and pass env */
  write_memory (iaddr + len, (gdb_byte *) "", 1);
  func = find_function_in_inferior ("scm_evstr", NULL);
  return call_function_by_hand (func, 1, &addr);
}

static struct value *
evaluate_exp (struct type *expect_type, struct expression *exp,
	      int *pos, enum noside noside)
{
  enum exp_opcode op = exp->elts[*pos].opcode;
  int len, pc;
  char *str;
  switch (op)
    {
    case OP_NAME:
      pc = (*pos)++;
      len = longest_to_int (exp->elts[pc + 1].longconst);
      (*pos) += 3 + BYTES_TO_EXP_ELEM (len + 1);
      if (noside == EVAL_SKIP)
	goto nosideret;
      str = &exp->elts[pc + 2].string;
      return scm_lookup_name (exp->gdbarch, str);
    case OP_STRING:
      pc = (*pos)++;
      len = longest_to_int (exp->elts[pc + 1].longconst);
      (*pos) += 3 + BYTES_TO_EXP_ELEM (len + 1);
      if (noside == EVAL_SKIP)
	goto nosideret;
      str = &exp->elts[pc + 2].string;
      return scm_evaluate_string (str, len);
    default:;
    }
  return evaluate_subexp_standard (expect_type, exp, pos, noside);
nosideret:
  return value_from_longest (builtin_type (exp->gdbarch)->builtin_int, 1);
}

const struct exp_descriptor exp_descriptor_scm = 
{
  print_subexp_standard,
  operator_length_standard,
  op_name_standard,
  dump_subexp_body_standard,
  evaluate_exp
};

const struct language_defn scm_language_defn =
{
  "scheme",			/* Language name */
  language_scm,
  range_check_off,
  type_check_off,
  case_sensitive_off,
  array_row_major,
  macro_expansion_no,
  &exp_descriptor_scm,
  scm_parse,
  c_error,
  null_post_parser,
  scm_printchar,		/* Print a character constant */
  scm_printstr,			/* Function to print string constant */
  NULL,				/* Function to print a single character */
  c_print_type,			/* Print a type using appropriate syntax */
  default_print_typedef,	/* Print a typedef using appropriate syntax */
  scm_val_print,		/* Print a value using appropriate syntax */
  scm_value_print,		/* Print a top-level value */
  NULL,				/* Language specific skip_trampoline */
  NULL,	                        /* name_of_this */
  basic_lookup_symbol_nonlocal,	/* lookup_symbol_nonlocal */
  basic_lookup_transparent_type,/* lookup_transparent_type */
  NULL,				/* Language specific symbol demangler */
  NULL,				/* Language specific class_name_from_physname */
  NULL,				/* expression operators for printing */
  1,				/* c-style arrays */
  0,				/* String lower bound */
  default_word_break_characters,
  default_make_symbol_completion_list,
  c_language_arch_info,
  default_print_array_index,
  default_pass_by_reference,
  default_get_string,
  LANG_MAGIC
};

static void *
build_scm_types (struct gdbarch *gdbarch)
{
  struct builtin_scm_type *builtin_scm_type
    = GDBARCH_OBSTACK_ZALLOC (gdbarch, struct builtin_scm_type);

  builtin_scm_type->builtin_scm
    = arch_integer_type (gdbarch, gdbarch_long_bit (gdbarch), 0, "SCM");

  return builtin_scm_type;
}

static struct gdbarch_data *scm_type_data;

const struct builtin_scm_type *
builtin_scm_type (struct gdbarch *gdbarch)
{
  return gdbarch_data (gdbarch, scm_type_data);
}

void
_initialize_scheme_language (void)
{
  scm_type_data = gdbarch_data_register_post_init (build_scm_types);

  add_language (&scm_language_defn);
}

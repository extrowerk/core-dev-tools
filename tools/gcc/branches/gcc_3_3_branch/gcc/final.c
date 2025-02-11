/* Convert RTL to assembler code and output it, for GNU compiler.
   Copyright (C) 1987, 1988, 1989, 1992, 1993, 1994, 1995, 1996, 1997,
   1998, 1999, 2000, 2001, 2002 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 59 Temple Place - Suite 330, Boston, MA
02111-1307, USA.  */

/* This is the final pass of the compiler.
   It looks at the rtl code for a function and outputs assembler code.

   Call `final_start_function' to output the assembler code for function entry,
   `final' to output assembler code for some RTL code,
   `final_end_function' to output assembler code for function exit.
   If a function is compiled in several pieces, each piece is
   output separately with `final'.

   Some optimizations are also done at this level.
   Move instructions that were made unnecessary by good register allocation
   are detected and omitted from the output.  (Though most of these
   are removed by the last jump pass.)

   Instructions to set the condition codes are omitted when it can be
   seen that the condition codes already had the desired values.

   In some cases it is sufficient if the inherited condition codes
   have related values, but this may require the following insn
   (the one that tests the condition codes) to be modified.

   The code for the function prologue and epilogue are generated
   directly in assembler by the target functions function_prologue and
   function_epilogue.  Those instructions never exist as rtl.  */

#include "config.h"
#include "system.h"

#include "tree.h"
#include "rtl.h"
#include "tm_p.h"
#include "regs.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "recog.h"
#include "conditions.h"
#include "flags.h"
#include "real.h"
#include "hard-reg-set.h"
#include "output.h"
#include "except.h"
#include "function.h"
#include "toplev.h"
#include "reload.h"
#include "intl.h"
#include "basic-block.h"
#include "target.h"
#include "debug.h"
#include "expr.h"
#include "profile.h"
#include "cfglayout.h"

#ifdef XCOFF_DEBUGGING_INFO
#include "xcoffout.h"		/* Needed for external data
				   declarations for e.g. AIX 4.x.  */
#endif

#if defined (DWARF2_UNWIND_INFO) || defined (DWARF2_DEBUGGING_INFO)
#include "dwarf2out.h"
#endif

/* If we aren't using cc0, CC_STATUS_INIT shouldn't exist.  So define a
   null default for it to save conditionalization later.  */
#ifndef CC_STATUS_INIT
#define CC_STATUS_INIT
#endif

/* How to start an assembler comment.  */
#ifndef ASM_COMMENT_START
#define ASM_COMMENT_START ";#"
#endif

/* Is the given character a logical line separator for the assembler?  */
#ifndef IS_ASM_LOGICAL_LINE_SEPARATOR
#define IS_ASM_LOGICAL_LINE_SEPARATOR(C) ((C) == ';')
#endif

#ifndef JUMP_TABLES_IN_TEXT_SECTION
#define JUMP_TABLES_IN_TEXT_SECTION 0
#endif

#if defined(READONLY_DATA_SECTION) || defined(READONLY_DATA_SECTION_ASM_OP)
#define HAVE_READONLY_DATA_SECTION 1
#else
#define HAVE_READONLY_DATA_SECTION 0
#endif

/* Last insn processed by final_scan_insn.  */
static rtx debug_insn;
rtx current_output_insn;

/* Line number of last NOTE.  */
static int last_linenum;

/* Highest line number in current block.  */
static int high_block_linenum;

/* Likewise for function.  */
static int high_function_linenum;

/* Filename of last NOTE.  */
static const char *last_filename;

extern int length_unit_log; /* This is defined in insn-attrtab.c.  */

/* Nonzero while outputting an `asm' with operands.
   This means that inconsistencies are the user's fault, so don't abort.
   The precise value is the insn being output, to pass to error_for_asm.  */
rtx this_is_asm_operands;

/* Number of operands of this insn, for an `asm' with operands.  */
static unsigned int insn_noperands;

/* Compare optimization flag.  */

static rtx last_ignored_compare = 0;

/* Flag indicating this insn is the start of a new basic block.  */

static int new_block = 1;

/* Assign a unique number to each insn that is output.
   This can be used to generate unique local labels.  */

static int insn_counter = 0;

#ifdef HAVE_cc0
/* This variable contains machine-dependent flags (defined in tm.h)
   set and examined by output routines
   that describe how to interpret the condition codes properly.  */

CC_STATUS cc_status;

/* During output of an insn, this contains a copy of cc_status
   from before the insn.  */

CC_STATUS cc_prev_status;
#endif

/* Indexed by hardware reg number, is 1 if that register is ever
   used in the current function.

   In life_analysis, or in stupid_life_analysis, this is set
   up to record the hard regs used explicitly.  Reload adds
   in the hard regs used for holding pseudo regs.  Final uses
   it to generate the code in the function prologue and epilogue
   to save and restore registers as needed.  */

char regs_ever_live[FIRST_PSEUDO_REGISTER];

/* Nonzero means current function must be given a frame pointer.
   Initialized in function.c to 0.  Set only in reload1.c as per
   the needs of the function.  */

int frame_pointer_needed;

/* Number of unmatched NOTE_INSN_BLOCK_BEG notes we have seen.  */

static int block_depth;

/* Nonzero if have enabled APP processing of our assembler output.  */

static int app_on;

/* If we are outputting an insn sequence, this contains the sequence rtx.
   Zero otherwise.  */

rtx final_sequence;

#ifdef ASSEMBLER_DIALECT

/* Number of the assembler dialect to use, starting at 0.  */
static int dialect_number;
#endif

/* Indexed by line number, nonzero if there is a note for that line.  */

static char *line_note_exists;

#ifdef HAVE_conditional_execution
/* Nonnull if the insn currently being emitted was a COND_EXEC pattern.  */
rtx current_insn_predicate;
#endif

struct function_list
{
  struct function_list *next; 	/* next function */
  const char *name; 		/* function name */
  long cfg_checksum;		/* function checksum */
  long count_edges;		/* number of intrumented edges in this function */
};

static struct function_list *functions_head = 0;
static struct function_list **functions_tail = &functions_head;

#ifdef HAVE_ATTR_length
static int asm_insn_count	PARAMS ((rtx));
#endif
static void profile_function	PARAMS ((FILE *));
static void profile_after_prologue PARAMS ((FILE *));
static void notice_source_line	PARAMS ((rtx));
static rtx walk_alter_subreg	PARAMS ((rtx *));
static void output_asm_name	PARAMS ((void));
static void output_alternate_entry_point PARAMS ((FILE *, rtx));
static tree get_mem_expr_from_op	PARAMS ((rtx, int *));
static void output_asm_operand_names PARAMS ((rtx *, int *, int));
static void output_operand	PARAMS ((rtx, int));
#ifdef LEAF_REGISTERS
static void leaf_renumber_regs	PARAMS ((rtx));
#endif
#ifdef HAVE_cc0
static int alter_cond		PARAMS ((rtx));
#endif
#ifndef ADDR_VEC_ALIGN
static int final_addr_vec_align PARAMS ((rtx));
#endif
#ifdef HAVE_ATTR_length
static int align_fuzz		PARAMS ((rtx, rtx, int, unsigned));
#endif

/* Initialize data in final at the beginning of a compilation.  */

void
init_final (filename)
     const char *filename ATTRIBUTE_UNUSED;
{
  app_on = 0;
  final_sequence = 0;

#ifdef ASSEMBLER_DIALECT
  dialect_number = ASSEMBLER_DIALECT;
#endif
}

/* Called at end of source file,
   to output the arc-profiling table for this entire compilation.  */

void
end_final (filename)
     const char *filename;
{
  if (profile_arc_flag && profile_info.count_instrumented_edges)
    {
      char name[20];
      tree string_type, string_cst;
      tree structure_decl, structure_value, structure_pointer_type;
      tree field_decl, decl_chain, value_chain;
      tree sizeof_field_value, domain_type;

      /* Build types.  */
      string_type = build_pointer_type (char_type_node);

      /* Libgcc2 bb structure.  */
      structure_decl = make_node (RECORD_TYPE);
      structure_pointer_type = build_pointer_type (structure_decl);

      /* Output the main header, of 7 words:
         0:  1 if this file is initialized, else 0.
         1:  address of file name (LPBX1).
         2:  address of table of counts (LPBX2).
         3:  number of counts in the table.
         4:  always 0, libgcc2 uses this as a pointer to next ``struct bb''

         The following are GNU extensions:

         5:  Number of bytes in this header.
         6:  address of table of function checksums (LPBX7).  */

      /* The zero word.  */
      decl_chain =
	build_decl (FIELD_DECL, get_identifier ("zero_word"),
		    long_integer_type_node);
      value_chain = build_tree_list (decl_chain,
				     convert (long_integer_type_node,
					      integer_zero_node));

      /* Address of filename.  */
      {
	char *cwd, *da_filename;
	int da_filename_len;

	field_decl =
	  build_decl (FIELD_DECL, get_identifier ("filename"), string_type);
	TREE_CHAIN (field_decl) = decl_chain;
	decl_chain = field_decl;
	cwd = getpwd ();
	da_filename_len = strlen (filename) + strlen (cwd) + 4 + 1;
	da_filename = (char *) alloca (da_filename_len);
	strcpy (da_filename, cwd);
	strcat (da_filename, "/");
	strcat (da_filename, filename);
	strcat (da_filename, ".da");
	da_filename_len = strlen (da_filename);
	string_cst = build_string (da_filename_len + 1, da_filename);
	domain_type = build_index_type (build_int_2 (da_filename_len, 0));
	TREE_TYPE (string_cst)
	  = build_array_type (char_type_node, domain_type);
	value_chain = tree_cons (field_decl,
				 build1 (ADDR_EXPR, string_type, string_cst),
				 value_chain);
      }

      /* Table of counts.  */
      {
	tree gcov_type_type = make_unsigned_type (GCOV_TYPE_SIZE);
	tree gcov_type_pointer_type = build_pointer_type (gcov_type_type);
	tree domain_tree
	  = build_index_type (build_int_2 (profile_info.
					   count_instrumented_edges - 1, 0));
	tree gcov_type_array_type
	  = build_array_type (gcov_type_type, domain_tree);
	tree gcov_type_array_pointer_type
	  = build_pointer_type (gcov_type_array_type);
	tree counts_table;

	field_decl =
	  build_decl (FIELD_DECL, get_identifier ("counts"),
		      gcov_type_pointer_type);
	TREE_CHAIN (field_decl) = decl_chain;
	decl_chain = field_decl;

	/* No values.  */
	counts_table
	  = build (VAR_DECL, gcov_type_array_type, NULL_TREE, NULL_TREE);
	TREE_STATIC (counts_table) = 1;
	ASM_GENERATE_INTERNAL_LABEL (name, "LPBX", 2);
	DECL_NAME (counts_table) = get_identifier (name);
	assemble_variable (counts_table, 0, 0, 0);

	value_chain = tree_cons (field_decl,
				 build1 (ADDR_EXPR,
					 gcov_type_array_pointer_type,
					 counts_table), value_chain);
      }

      /* Count of the # of instrumented arcs.  */
      field_decl
	= build_decl (FIELD_DECL, get_identifier ("ncounts"),
		      long_integer_type_node);
      TREE_CHAIN (field_decl) = decl_chain;
      decl_chain = field_decl;

      value_chain = tree_cons (field_decl,
			       convert (long_integer_type_node,
					build_int_2 (profile_info.
						     count_instrumented_edges,
						     0)), value_chain);
      /* Pointer to the next bb.  */
      field_decl
	= build_decl (FIELD_DECL, get_identifier ("next"),
		      structure_pointer_type);
      TREE_CHAIN (field_decl) = decl_chain;
      decl_chain = field_decl;

      value_chain = tree_cons (field_decl, null_pointer_node, value_chain);

      /* sizeof(struct bb).  We'll set this after entire structure
	 is laid out.  */
      field_decl
	= build_decl (FIELD_DECL, get_identifier ("sizeof_bb"),
		      long_integer_type_node);
      TREE_CHAIN (field_decl) = decl_chain;
      decl_chain = field_decl;

      sizeof_field_value = tree_cons (field_decl, NULL, value_chain);
      value_chain = sizeof_field_value;

      /* struct bb_function [].  */
      {
	struct function_list *item;
	int num_nodes;
	tree checksum_field, arc_count_field, name_field;
	tree domain;
	tree array_value_chain = NULL_TREE;
	tree bb_fn_struct_type;
	tree bb_fn_struct_array_type;
	tree bb_fn_struct_array_pointer_type;
	tree bb_fn_struct_pointer_type;
	tree field_value, field_value_chain;

	bb_fn_struct_type = make_node (RECORD_TYPE);

	checksum_field = build_decl (FIELD_DECL, get_identifier ("checksum"),
				     long_integer_type_node);

	arc_count_field
	  = build_decl (FIELD_DECL, get_identifier ("arc_count"),
		        integer_type_node);
	TREE_CHAIN (checksum_field) = arc_count_field;

	name_field
	  = build_decl (FIELD_DECL, get_identifier ("name"), string_type);
	TREE_CHAIN (arc_count_field) = name_field;

	TYPE_FIELDS (bb_fn_struct_type) = checksum_field;

	num_nodes = 0;

	for (item = functions_head; item != 0; item = item->next)
	  num_nodes++;

	/* Note that the array contains a terminator, hence no - 1.  */
	domain = build_index_type (build_int_2 (num_nodes, 0));

	bb_fn_struct_pointer_type = build_pointer_type (bb_fn_struct_type);
	bb_fn_struct_array_type
	  = build_array_type (bb_fn_struct_type, domain);
	bb_fn_struct_array_pointer_type
	  = build_pointer_type (bb_fn_struct_array_type);

	layout_type (bb_fn_struct_type);
	layout_type (bb_fn_struct_pointer_type);
	layout_type (bb_fn_struct_array_type);
	layout_type (bb_fn_struct_array_pointer_type);

	for (item = functions_head; item != 0; item = item->next)
	  {
	    size_t name_len;

	    /* create constructor for structure.  */
	    field_value_chain
	      = build_tree_list (checksum_field,
				 convert (long_integer_type_node,
					  build_int_2 (item->cfg_checksum, 0)));
	    field_value_chain
	      = tree_cons (arc_count_field,
			   convert (integer_type_node,
				    build_int_2 (item->count_edges, 0)),
			   field_value_chain);

	    name_len = strlen (item->name);
	    string_cst = build_string (name_len + 1, item->name);
	    domain_type = build_index_type (build_int_2 (name_len, 0));
	    TREE_TYPE (string_cst)
	      = build_array_type (char_type_node, domain_type);
	    field_value_chain = tree_cons (name_field,
					   build1 (ADDR_EXPR, string_type,
						   string_cst),
					   field_value_chain);

	    /* Add to chain.  */
	    array_value_chain
	      = tree_cons (NULL_TREE, build (CONSTRUCTOR,
					     bb_fn_struct_type, NULL_TREE,
					     nreverse (field_value_chain)),
			   array_value_chain);
	  }

	/* Add terminator.  */
	field_value = build_tree_list (arc_count_field,
				       convert (integer_type_node,
						build_int_2 (-1, 0)));

	array_value_chain = tree_cons (NULL_TREE,
				       build (CONSTRUCTOR, bb_fn_struct_type,
					      NULL_TREE, field_value),
				       array_value_chain);


	/* Create constructor for array.  */
	field_decl
	  = build_decl (FIELD_DECL, get_identifier ("function_infos"),
		        bb_fn_struct_pointer_type);
	value_chain = tree_cons (field_decl,
				 build1 (ADDR_EXPR,
					 bb_fn_struct_array_pointer_type,
					 build (CONSTRUCTOR,
						bb_fn_struct_array_type,
						NULL_TREE,
						nreverse
						(array_value_chain))),
				 value_chain);
	TREE_CHAIN (field_decl) = decl_chain;
	decl_chain = field_decl;
      }

      /* Finish structure.  */
      TYPE_FIELDS (structure_decl) = nreverse (decl_chain);
      layout_type (structure_decl);

      structure_value
	= build (VAR_DECL, structure_decl, NULL_TREE, NULL_TREE);
      DECL_INITIAL (structure_value)
	= build (CONSTRUCTOR, structure_decl, NULL_TREE,
	         nreverse (value_chain));
      TREE_STATIC (structure_value) = 1;
      ASM_GENERATE_INTERNAL_LABEL (name, "LPBX", 0);
      DECL_NAME (structure_value) = get_identifier (name);

      /* Size of this structure.  */
      TREE_VALUE (sizeof_field_value)
	= convert (long_integer_type_node,
		   build_int_2 (int_size_in_bytes (structure_decl), 0));

      /* Build structure.  */
      assemble_variable (structure_value, 0, 0, 0);
    }
}

/* Default target function prologue and epilogue assembler output.

   If not overridden for epilogue code, then the function body itself
   contains return instructions wherever needed.  */
void
default_function_pro_epilogue (file, size)
     FILE *file ATTRIBUTE_UNUSED;
     HOST_WIDE_INT size ATTRIBUTE_UNUSED;
{
}

/* Default target hook that outputs nothing to a stream.  */
void
no_asm_to_stream (file)
     FILE *file ATTRIBUTE_UNUSED;
{
}

/* Enable APP processing of subsequent output.
   Used before the output from an `asm' statement.  */

void
app_enable ()
{
  if (! app_on)
    {
      fputs (ASM_APP_ON, asm_out_file);
      app_on = 1;
    }
}

/* Disable APP processing of subsequent output.
   Called from varasm.c before most kinds of output.  */

void
app_disable ()
{
  if (app_on)
    {
      fputs (ASM_APP_OFF, asm_out_file);
      app_on = 0;
    }
}

/* Return the number of slots filled in the current
   delayed branch sequence (we don't count the insn needing the
   delay slot).   Zero if not in a delayed branch sequence.  */

#ifdef DELAY_SLOTS
int
dbr_sequence_length ()
{
  if (final_sequence != 0)
    return XVECLEN (final_sequence, 0) - 1;
  else
    return 0;
}
#endif

/* The next two pages contain routines used to compute the length of an insn
   and to shorten branches.  */

/* Arrays for insn lengths, and addresses.  The latter is referenced by
   `insn_current_length'.  */

static int *insn_lengths;

varray_type insn_addresses_;

/* Max uid for which the above arrays are valid.  */
static int insn_lengths_max_uid;

/* Address of insn being processed.  Used by `insn_current_length'.  */
int insn_current_address;

/* Address of insn being processed in previous iteration.  */
int insn_last_address;

/* known invariant alignment of insn being processed.  */
int insn_current_align;

/* After shorten_branches, for any insn, uid_align[INSN_UID (insn)]
   gives the next following alignment insn that increases the known
   alignment, or NULL_RTX if there is no such insn.
   For any alignment obtained this way, we can again index uid_align with
   its uid to obtain the next following align that in turn increases the
   alignment, till we reach NULL_RTX; the sequence obtained this way
   for each insn we'll call the alignment chain of this insn in the following
   comments.  */

struct label_alignment
{
  short alignment;
  short max_skip;
};

static rtx *uid_align;
static int *uid_shuid;
static struct label_alignment *label_align;

/* Indicate that branch shortening hasn't yet been done.  */

void
init_insn_lengths ()
{
  if (uid_shuid)
    {
      free (uid_shuid);
      uid_shuid = 0;
    }
  if (insn_lengths)
    {
      free (insn_lengths);
      insn_lengths = 0;
      insn_lengths_max_uid = 0;
    }
#ifdef HAVE_ATTR_length
  INSN_ADDRESSES_FREE ();
#endif
  if (uid_align)
    {
      free (uid_align);
      uid_align = 0;
    }
}

/* Obtain the current length of an insn.  If branch shortening has been done,
   get its actual length.  Otherwise, get its maximum length.  */

int
get_attr_length (insn)
     rtx insn ATTRIBUTE_UNUSED;
{
#ifdef HAVE_ATTR_length
  rtx body;
  int i;
  int length = 0;

  if (insn_lengths_max_uid > INSN_UID (insn))
    return insn_lengths[INSN_UID (insn)];
  else
    switch (GET_CODE (insn))
      {
      case NOTE:
      case BARRIER:
      case CODE_LABEL:
	return 0;

      case CALL_INSN:
	length = insn_default_length (insn);
	break;

      case JUMP_INSN:
	body = PATTERN (insn);
	if (GET_CODE (body) == ADDR_VEC || GET_CODE (body) == ADDR_DIFF_VEC)
	  {
	    /* Alignment is machine-dependent and should be handled by
	       ADDR_VEC_ALIGN.  */
	  }
	else
	  length = insn_default_length (insn);
	break;

      case INSN:
	body = PATTERN (insn);
	if (GET_CODE (body) == USE || GET_CODE (body) == CLOBBER)
	  return 0;

	else if (GET_CODE (body) == ASM_INPUT || asm_noperands (body) >= 0)
	  length = asm_insn_count (body) * insn_default_length (insn);
	else if (GET_CODE (body) == SEQUENCE)
	  for (i = 0; i < XVECLEN (body, 0); i++)
	    length += get_attr_length (XVECEXP (body, 0, i));
	else
	  length = insn_default_length (insn);
	break;

      default:
	break;
      }

#ifdef ADJUST_INSN_LENGTH
  ADJUST_INSN_LENGTH (insn, length);
#endif
  return length;
#else /* not HAVE_ATTR_length */
  return 0;
#endif /* not HAVE_ATTR_length */
}

/* Code to handle alignment inside shorten_branches.  */

/* Here is an explanation how the algorithm in align_fuzz can give
   proper results:

   Call a sequence of instructions beginning with alignment point X
   and continuing until the next alignment point `block X'.  When `X'
   is used in an expression, it means the alignment value of the
   alignment point.

   Call the distance between the start of the first insn of block X, and
   the end of the last insn of block X `IX', for the `inner size of X'.
   This is clearly the sum of the instruction lengths.

   Likewise with the next alignment-delimited block following X, which we
   shall call block Y.

   Call the distance between the start of the first insn of block X, and
   the start of the first insn of block Y `OX', for the `outer size of X'.

   The estimated padding is then OX - IX.

   OX can be safely estimated as

           if (X >= Y)
                   OX = round_up(IX, Y)
           else
                   OX = round_up(IX, X) + Y - X

   Clearly est(IX) >= real(IX), because that only depends on the
   instruction lengths, and those being overestimated is a given.

   Clearly round_up(foo, Z) >= round_up(bar, Z) if foo >= bar, so
   we needn't worry about that when thinking about OX.

   When X >= Y, the alignment provided by Y adds no uncertainty factor
   for branch ranges starting before X, so we can just round what we have.
   But when X < Y, we don't know anything about the, so to speak,
   `middle bits', so we have to assume the worst when aligning up from an
   address mod X to one mod Y, which is Y - X.  */

#ifndef LABEL_ALIGN
#define LABEL_ALIGN(LABEL) align_labels_log
#endif

#ifndef LABEL_ALIGN_MAX_SKIP
#define LABEL_ALIGN_MAX_SKIP align_labels_max_skip
#endif

#ifndef LOOP_ALIGN
#define LOOP_ALIGN(LABEL) align_loops_log
#endif

#ifndef LOOP_ALIGN_MAX_SKIP
#define LOOP_ALIGN_MAX_SKIP align_loops_max_skip
#endif

#ifndef LABEL_ALIGN_AFTER_BARRIER
#define LABEL_ALIGN_AFTER_BARRIER(LABEL) 0
#endif

#ifndef LABEL_ALIGN_AFTER_BARRIER_MAX_SKIP
#define LABEL_ALIGN_AFTER_BARRIER_MAX_SKIP 0
#endif

#ifndef JUMP_ALIGN
#define JUMP_ALIGN(LABEL) align_jumps_log
#endif

#ifndef JUMP_ALIGN_MAX_SKIP
#define JUMP_ALIGN_MAX_SKIP align_jumps_max_skip
#endif

#ifndef ADDR_VEC_ALIGN
static int
final_addr_vec_align (addr_vec)
     rtx addr_vec;
{
  int align = GET_MODE_SIZE (GET_MODE (PATTERN (addr_vec)));

  if (align > BIGGEST_ALIGNMENT / BITS_PER_UNIT)
    align = BIGGEST_ALIGNMENT / BITS_PER_UNIT;
  return exact_log2 (align);

}

#define ADDR_VEC_ALIGN(ADDR_VEC) final_addr_vec_align (ADDR_VEC)
#endif

#ifndef INSN_LENGTH_ALIGNMENT
#define INSN_LENGTH_ALIGNMENT(INSN) length_unit_log
#endif

#define INSN_SHUID(INSN) (uid_shuid[INSN_UID (INSN)])

static int min_labelno, max_labelno;

#define LABEL_TO_ALIGNMENT(LABEL) \
  (label_align[CODE_LABEL_NUMBER (LABEL) - min_labelno].alignment)

#define LABEL_TO_MAX_SKIP(LABEL) \
  (label_align[CODE_LABEL_NUMBER (LABEL) - min_labelno].max_skip)

/* For the benefit of port specific code do this also as a function.  */

int
label_to_alignment (label)
     rtx label;
{
  return LABEL_TO_ALIGNMENT (label);
}

#ifdef HAVE_ATTR_length
/* The differences in addresses
   between a branch and its target might grow or shrink depending on
   the alignment the start insn of the range (the branch for a forward
   branch or the label for a backward branch) starts out on; if these
   differences are used naively, they can even oscillate infinitely.
   We therefore want to compute a 'worst case' address difference that
   is independent of the alignment the start insn of the range end
   up on, and that is at least as large as the actual difference.
   The function align_fuzz calculates the amount we have to add to the
   naively computed difference, by traversing the part of the alignment
   chain of the start insn of the range that is in front of the end insn
   of the range, and considering for each alignment the maximum amount
   that it might contribute to a size increase.

   For casesi tables, we also want to know worst case minimum amounts of
   address difference, in case a machine description wants to introduce
   some common offset that is added to all offsets in a table.
   For this purpose, align_fuzz with a growth argument of 0 computes the
   appropriate adjustment.  */

/* Compute the maximum delta by which the difference of the addresses of
   START and END might grow / shrink due to a different address for start
   which changes the size of alignment insns between START and END.
   KNOWN_ALIGN_LOG is the alignment known for START.
   GROWTH should be ~0 if the objective is to compute potential code size
   increase, and 0 if the objective is to compute potential shrink.
   The return value is undefined for any other value of GROWTH.  */

static int
align_fuzz (start, end, known_align_log, growth)
     rtx start, end;
     int known_align_log;
     unsigned growth;
{
  int uid = INSN_UID (start);
  rtx align_label;
  int known_align = 1 << known_align_log;
  int end_shuid = INSN_SHUID (end);
  int fuzz = 0;

  for (align_label = uid_align[uid]; align_label; align_label = uid_align[uid])
    {
      int align_addr, new_align;

      uid = INSN_UID (align_label);
      align_addr = INSN_ADDRESSES (uid) - insn_lengths[uid];
      if (uid_shuid[uid] > end_shuid)
	break;
      known_align_log = LABEL_TO_ALIGNMENT (align_label);
      new_align = 1 << known_align_log;
      if (new_align < known_align)
	continue;
      fuzz += (-align_addr ^ growth) & (new_align - known_align);
      known_align = new_align;
    }
  return fuzz;
}

/* Compute a worst-case reference address of a branch so that it
   can be safely used in the presence of aligned labels.  Since the
   size of the branch itself is unknown, the size of the branch is
   not included in the range.  I.e. for a forward branch, the reference
   address is the end address of the branch as known from the previous
   branch shortening pass, minus a value to account for possible size
   increase due to alignment.  For a backward branch, it is the start
   address of the branch as known from the current pass, plus a value
   to account for possible size increase due to alignment.
   NB.: Therefore, the maximum offset allowed for backward branches needs
   to exclude the branch size.  */

int
insn_current_reference_address (branch)
     rtx branch;
{
  rtx dest, seq;
  int seq_uid;

  if (! INSN_ADDRESSES_SET_P ())
    return 0;

  seq = NEXT_INSN (PREV_INSN (branch));
  seq_uid = INSN_UID (seq);
  if (GET_CODE (branch) != JUMP_INSN)
    /* This can happen for example on the PA; the objective is to know the
       offset to address something in front of the start of the function.
       Thus, we can treat it like a backward branch.
       We assume here that FUNCTION_BOUNDARY / BITS_PER_UNIT is larger than
       any alignment we'd encounter, so we skip the call to align_fuzz.  */
    return insn_current_address;
  dest = JUMP_LABEL (branch);

  /* BRANCH has no proper alignment chain set, so use SEQ.
     BRANCH also has no INSN_SHUID.  */
  if (INSN_SHUID (seq) < INSN_SHUID (dest))
    {
      /* Forward branch.  */
      return (insn_last_address + insn_lengths[seq_uid]
	      - align_fuzz (seq, dest, length_unit_log, ~0));
    }
  else
    {
      /* Backward branch.  */
      return (insn_current_address
	      + align_fuzz (dest, seq, length_unit_log, ~0));
    }
}
#endif /* HAVE_ATTR_length */

void
compute_alignments ()
{
  int log, max_skip, max_log;
  basic_block bb;

  if (label_align)
    {
      free (label_align);
      label_align = 0;
    }

  max_labelno = max_label_num ();
  min_labelno = get_first_label_num ();
  label_align = (struct label_alignment *)
    xcalloc (max_labelno - min_labelno + 1, sizeof (struct label_alignment));

  /* If not optimizing or optimizing for size, don't assign any alignments.  */
  if (! optimize || optimize_size)
    return;

  FOR_EACH_BB (bb)
    {
      rtx label = bb->head;
      int fallthru_frequency = 0, branch_frequency = 0, has_fallthru = 0;
      edge e;

      if (GET_CODE (label) != CODE_LABEL)
	continue;
      max_log = LABEL_ALIGN (label);
      max_skip = LABEL_ALIGN_MAX_SKIP;

      for (e = bb->pred; e; e = e->pred_next)
	{
	  if (e->flags & EDGE_FALLTHRU)
	    has_fallthru = 1, fallthru_frequency += EDGE_FREQUENCY (e);
	  else
	    branch_frequency += EDGE_FREQUENCY (e);
	}

      /* There are two purposes to align block with no fallthru incoming edge:
	 1) to avoid fetch stalls when branch destination is near cache boundary
	 2) to improve cache efficiency in case the previous block is not executed
	    (so it does not need to be in the cache).

	 We to catch first case, we align frequently executed blocks.
	 To catch the second, we align blocks that are executed more frequently
	 than the predecessor and the predecessor is likely to not be executed
	 when function is called.  */

      if (!has_fallthru
	  && (branch_frequency > BB_FREQ_MAX / 10
	      || (bb->frequency > bb->prev_bb->frequency * 10
		  && (bb->prev_bb->frequency
		      <= ENTRY_BLOCK_PTR->frequency / 2))))
	{
	  log = JUMP_ALIGN (label);
	  if (max_log < log)
	    {
	      max_log = log;
	      max_skip = JUMP_ALIGN_MAX_SKIP;
	    }
	}
      /* In case block is frequent and reached mostly by non-fallthru edge,
	 align it.  It is most likely a first block of loop.  */
      if (has_fallthru
	  && branch_frequency + fallthru_frequency > BB_FREQ_MAX / 10
	  && branch_frequency > fallthru_frequency * 2)
	{
	  log = LOOP_ALIGN (label);
	  if (max_log < log)
	    {
	      max_log = log;
	      max_skip = LOOP_ALIGN_MAX_SKIP;
	    }
	}
      LABEL_TO_ALIGNMENT (label) = max_log;
      LABEL_TO_MAX_SKIP (label) = max_skip;
    }
}

/* Make a pass over all insns and compute their actual lengths by shortening
   any branches of variable length if possible.  */

/* Give a default value for the lowest address in a function.  */

#ifndef FIRST_INSN_ADDRESS
#define FIRST_INSN_ADDRESS 0
#endif

/* shorten_branches might be called multiple times:  for example, the SH
   port splits out-of-range conditional branches in MACHINE_DEPENDENT_REORG.
   In order to do this, it needs proper length information, which it obtains
   by calling shorten_branches.  This cannot be collapsed with
   shorten_branches itself into a single pass unless we also want to integrate
   reorg.c, since the branch splitting exposes new instructions with delay
   slots.  */

void
shorten_branches (first)
     rtx first ATTRIBUTE_UNUSED;
{
  rtx insn;
  int max_uid;
  int i;
  int max_log;
  int max_skip;
#ifdef HAVE_ATTR_length
#define MAX_CODE_ALIGN 16
  rtx seq;
  int something_changed = 1;
  char *varying_length;
  rtx body;
  int uid;
  rtx align_tab[MAX_CODE_ALIGN];

#endif

  /* Compute maximum UID and allocate label_align / uid_shuid.  */
  max_uid = get_max_uid ();

  uid_shuid = (int *) xmalloc (max_uid * sizeof *uid_shuid);

  if (max_labelno != max_label_num ())
    {
      int old = max_labelno;
      int n_labels;
      int n_old_labels;

      max_labelno = max_label_num ();

      n_labels = max_labelno - min_labelno + 1;
      n_old_labels = old - min_labelno + 1;

      label_align = (struct label_alignment *) xrealloc
	(label_align, n_labels * sizeof (struct label_alignment));

      /* Range of labels grows monotonically in the function.  Abort here
         means that the initialization of array got lost.  */
      if (n_old_labels > n_labels)
	abort ();

      memset (label_align + n_old_labels, 0,
	      (n_labels - n_old_labels) * sizeof (struct label_alignment));
    }

  /* Initialize label_align and set up uid_shuid to be strictly
     monotonically rising with insn order.  */
  /* We use max_log here to keep track of the maximum alignment we want to
     impose on the next CODE_LABEL (or the current one if we are processing
     the CODE_LABEL itself).  */

  max_log = 0;
  max_skip = 0;

  for (insn = get_insns (), i = 1; insn; insn = NEXT_INSN (insn))
    {
      int log;

      INSN_SHUID (insn) = i++;
      if (INSN_P (insn))
	{
	  /* reorg might make the first insn of a loop being run once only,
             and delete the label in front of it.  Then we want to apply
             the loop alignment to the new label created by reorg, which
             is separated by the former loop start insn from the
	     NOTE_INSN_LOOP_BEG.  */
	}
      else if (GET_CODE (insn) == CODE_LABEL)
	{
	  rtx next;

	  /* Merge in alignments computed by compute_alignments.  */
	  log = LABEL_TO_ALIGNMENT (insn);
	  if (max_log < log)
	    {
	      max_log = log;
	      max_skip = LABEL_TO_MAX_SKIP (insn);
	    }

	  log = LABEL_ALIGN (insn);
	  if (max_log < log)
	    {
	      max_log = log;
	      max_skip = LABEL_ALIGN_MAX_SKIP;
	    }
	  next = NEXT_INSN (insn);
	  /* ADDR_VECs only take room if read-only data goes into the text
	     section.  */
	  if (JUMP_TABLES_IN_TEXT_SECTION || !HAVE_READONLY_DATA_SECTION)
	    if (next && GET_CODE (next) == JUMP_INSN)
	      {
		rtx nextbody = PATTERN (next);
		if (GET_CODE (nextbody) == ADDR_VEC
		    || GET_CODE (nextbody) == ADDR_DIFF_VEC)
		  {
		    log = ADDR_VEC_ALIGN (next);
		    if (max_log < log)
		      {
			max_log = log;
			max_skip = LABEL_ALIGN_MAX_SKIP;
		      }
		  }
	      }
	  LABEL_TO_ALIGNMENT (insn) = max_log;
	  LABEL_TO_MAX_SKIP (insn) = max_skip;
	  max_log = 0;
	  max_skip = 0;
	}
      else if (GET_CODE (insn) == BARRIER)
	{
	  rtx label;

	  for (label = insn; label && ! INSN_P (label);
	       label = NEXT_INSN (label))
	    if (GET_CODE (label) == CODE_LABEL)
	      {
		log = LABEL_ALIGN_AFTER_BARRIER (insn);
		if (max_log < log)
		  {
		    max_log = log;
		    max_skip = LABEL_ALIGN_AFTER_BARRIER_MAX_SKIP;
		  }
		break;
	      }
	}
    }
#ifdef HAVE_ATTR_length

  /* Allocate the rest of the arrays.  */
  insn_lengths = (int *) xmalloc (max_uid * sizeof (*insn_lengths));
  insn_lengths_max_uid = max_uid;
  /* Syntax errors can lead to labels being outside of the main insn stream.
     Initialize insn_addresses, so that we get reproducible results.  */
  INSN_ADDRESSES_ALLOC (max_uid);

  varying_length = (char *) xcalloc (max_uid, sizeof (char));

  /* Initialize uid_align.  We scan instructions
     from end to start, and keep in align_tab[n] the last seen insn
     that does an alignment of at least n+1, i.e. the successor
     in the alignment chain for an insn that does / has a known
     alignment of n.  */
  uid_align = (rtx *) xcalloc (max_uid, sizeof *uid_align);

  for (i = MAX_CODE_ALIGN; --i >= 0;)
    align_tab[i] = NULL_RTX;
  seq = get_last_insn ();
  for (; seq; seq = PREV_INSN (seq))
    {
      int uid = INSN_UID (seq);
      int log;
      log = (GET_CODE (seq) == CODE_LABEL ? LABEL_TO_ALIGNMENT (seq) : 0);
      uid_align[uid] = align_tab[0];
      if (log)
	{
	  /* Found an alignment label.  */
	  uid_align[uid] = align_tab[log];
	  for (i = log - 1; i >= 0; i--)
	    align_tab[i] = seq;
	}
    }
#ifdef CASE_VECTOR_SHORTEN_MODE
  if (optimize)
    {
      /* Look for ADDR_DIFF_VECs, and initialize their minimum and maximum
         label fields.  */

      int min_shuid = INSN_SHUID (get_insns ()) - 1;
      int max_shuid = INSN_SHUID (get_last_insn ()) + 1;
      int rel;

      for (insn = first; insn != 0; insn = NEXT_INSN (insn))
	{
	  rtx min_lab = NULL_RTX, max_lab = NULL_RTX, pat;
	  int len, i, min, max, insn_shuid;
	  int min_align;
	  addr_diff_vec_flags flags;

	  if (GET_CODE (insn) != JUMP_INSN
	      || GET_CODE (PATTERN (insn)) != ADDR_DIFF_VEC)
	    continue;
	  pat = PATTERN (insn);
	  len = XVECLEN (pat, 1);
	  if (len <= 0)
	    abort ();
	  min_align = MAX_CODE_ALIGN;
	  for (min = max_shuid, max = min_shuid, i = len - 1; i >= 0; i--)
	    {
	      rtx lab = XEXP (XVECEXP (pat, 1, i), 0);
	      int shuid = INSN_SHUID (lab);
	      if (shuid < min)
		{
		  min = shuid;
		  min_lab = lab;
		}
	      if (shuid > max)
		{
		  max = shuid;
		  max_lab = lab;
		}
	      if (min_align > LABEL_TO_ALIGNMENT (lab))
		min_align = LABEL_TO_ALIGNMENT (lab);
	    }
	  XEXP (pat, 2) = gen_rtx_LABEL_REF (VOIDmode, min_lab);
	  XEXP (pat, 3) = gen_rtx_LABEL_REF (VOIDmode, max_lab);
	  insn_shuid = INSN_SHUID (insn);
	  rel = INSN_SHUID (XEXP (XEXP (pat, 0), 0));
	  flags.min_align = min_align;
	  flags.base_after_vec = rel > insn_shuid;
	  flags.min_after_vec  = min > insn_shuid;
	  flags.max_after_vec  = max > insn_shuid;
	  flags.min_after_base = min > rel;
	  flags.max_after_base = max > rel;
	  ADDR_DIFF_VEC_FLAGS (pat) = flags;
	}
    }
#endif /* CASE_VECTOR_SHORTEN_MODE */

  /* Compute initial lengths, addresses, and varying flags for each insn.  */
  for (insn_current_address = FIRST_INSN_ADDRESS, insn = first;
       insn != 0;
       insn_current_address += insn_lengths[uid], insn = NEXT_INSN (insn))
    {
      uid = INSN_UID (insn);

      insn_lengths[uid] = 0;

      if (GET_CODE (insn) == CODE_LABEL)
	{
	  int log = LABEL_TO_ALIGNMENT (insn);
	  if (log)
	    {
	      int align = 1 << log;
	      int new_address = (insn_current_address + align - 1) & -align;
	      insn_lengths[uid] = new_address - insn_current_address;
	    }
	}

      INSN_ADDRESSES (uid) = insn_current_address + insn_lengths[uid];

      if (GET_CODE (insn) == NOTE || GET_CODE (insn) == BARRIER
	  || GET_CODE (insn) == CODE_LABEL)
	continue;
      if (INSN_DELETED_P (insn))
	continue;

      body = PATTERN (insn);
      if (GET_CODE (body) == ADDR_VEC || GET_CODE (body) == ADDR_DIFF_VEC)
	{
	  /* This only takes room if read-only data goes into the text
	     section.  */
	  if (JUMP_TABLES_IN_TEXT_SECTION || !HAVE_READONLY_DATA_SECTION)
	    insn_lengths[uid] = (XVECLEN (body,
					  GET_CODE (body) == ADDR_DIFF_VEC)
				 * GET_MODE_SIZE (GET_MODE (body)));
	  /* Alignment is handled by ADDR_VEC_ALIGN.  */
	}
      else if (GET_CODE (body) == ASM_INPUT || asm_noperands (body) >= 0)
	insn_lengths[uid] = asm_insn_count (body) * insn_default_length (insn);
      else if (GET_CODE (body) == SEQUENCE)
	{
	  int i;
	  int const_delay_slots;
#ifdef DELAY_SLOTS
	  const_delay_slots = const_num_delay_slots (XVECEXP (body, 0, 0));
#else
	  const_delay_slots = 0;
#endif
	  /* Inside a delay slot sequence, we do not do any branch shortening
	     if the shortening could change the number of delay slots
	     of the branch.  */
	  for (i = 0; i < XVECLEN (body, 0); i++)
	    {
	      rtx inner_insn = XVECEXP (body, 0, i);
	      int inner_uid = INSN_UID (inner_insn);
	      int inner_length;

	      if (GET_CODE (body) == ASM_INPUT
		  || asm_noperands (PATTERN (XVECEXP (body, 0, i))) >= 0)
		inner_length = (asm_insn_count (PATTERN (inner_insn))
				* insn_default_length (inner_insn));
	      else
		inner_length = insn_default_length (inner_insn);

	      insn_lengths[inner_uid] = inner_length;
	      if (const_delay_slots)
		{
		  if ((varying_length[inner_uid]
		       = insn_variable_length_p (inner_insn)) != 0)
		    varying_length[uid] = 1;
		  INSN_ADDRESSES (inner_uid) = (insn_current_address
						+ insn_lengths[uid]);
		}
	      else
		varying_length[inner_uid] = 0;
	      insn_lengths[uid] += inner_length;
	    }
	}
      else if (GET_CODE (body) != USE && GET_CODE (body) != CLOBBER)
	{
	  insn_lengths[uid] = insn_default_length (insn);
	  varying_length[uid] = insn_variable_length_p (insn);
	}

      /* If needed, do any adjustment.  */
#ifdef ADJUST_INSN_LENGTH
      ADJUST_INSN_LENGTH (insn, insn_lengths[uid]);
      if (insn_lengths[uid] < 0)
	fatal_insn ("negative insn length", insn);
#endif
    }

  /* Now loop over all the insns finding varying length insns.  For each,
     get the current insn length.  If it has changed, reflect the change.
     When nothing changes for a full pass, we are done.  */

  while (something_changed)
    {
      something_changed = 0;
      insn_current_align = MAX_CODE_ALIGN - 1;
      for (insn_current_address = FIRST_INSN_ADDRESS, insn = first;
	   insn != 0;
	   insn = NEXT_INSN (insn))
	{
	  int new_length;
#ifdef ADJUST_INSN_LENGTH
	  int tmp_length;
#endif
	  int length_align;

	  uid = INSN_UID (insn);

	  if (GET_CODE (insn) == CODE_LABEL)
	    {
	      int log = LABEL_TO_ALIGNMENT (insn);
	      if (log > insn_current_align)
		{
		  int align = 1 << log;
		  int new_address= (insn_current_address + align - 1) & -align;
		  insn_lengths[uid] = new_address - insn_current_address;
		  insn_current_align = log;
		  insn_current_address = new_address;
		}
	      else
		insn_lengths[uid] = 0;
	      INSN_ADDRESSES (uid) = insn_current_address;
	      continue;
	    }

	  length_align = INSN_LENGTH_ALIGNMENT (insn);
	  if (length_align < insn_current_align)
	    insn_current_align = length_align;

	  insn_last_address = INSN_ADDRESSES (uid);
	  INSN_ADDRESSES (uid) = insn_current_address;

#ifdef CASE_VECTOR_SHORTEN_MODE
	  if (optimize && GET_CODE (insn) == JUMP_INSN
	      && GET_CODE (PATTERN (insn)) == ADDR_DIFF_VEC)
	    {
	      rtx body = PATTERN (insn);
	      int old_length = insn_lengths[uid];
	      rtx rel_lab = XEXP (XEXP (body, 0), 0);
	      rtx min_lab = XEXP (XEXP (body, 2), 0);
	      rtx max_lab = XEXP (XEXP (body, 3), 0);
	      int rel_addr = INSN_ADDRESSES (INSN_UID (rel_lab));
	      int min_addr = INSN_ADDRESSES (INSN_UID (min_lab));
	      int max_addr = INSN_ADDRESSES (INSN_UID (max_lab));
	      rtx prev;
	      int rel_align = 0;
	      addr_diff_vec_flags flags;

	      /* Avoid automatic aggregate initialization.  */
	      flags = ADDR_DIFF_VEC_FLAGS (body);

	      /* Try to find a known alignment for rel_lab.  */
	      for (prev = rel_lab;
		   prev
		   && ! insn_lengths[INSN_UID (prev)]
		   && ! (varying_length[INSN_UID (prev)] & 1);
		   prev = PREV_INSN (prev))
		if (varying_length[INSN_UID (prev)] & 2)
		  {
		    rel_align = LABEL_TO_ALIGNMENT (prev);
		    break;
		  }

	      /* See the comment on addr_diff_vec_flags in rtl.h for the
		 meaning of the flags values.  base: REL_LAB   vec: INSN  */
	      /* Anything after INSN has still addresses from the last
		 pass; adjust these so that they reflect our current
		 estimate for this pass.  */
	      if (flags.base_after_vec)
		rel_addr += insn_current_address - insn_last_address;
	      if (flags.min_after_vec)
		min_addr += insn_current_address - insn_last_address;
	      if (flags.max_after_vec)
		max_addr += insn_current_address - insn_last_address;
	      /* We want to know the worst case, i.e. lowest possible value
		 for the offset of MIN_LAB.  If MIN_LAB is after REL_LAB,
		 its offset is positive, and we have to be wary of code shrink;
		 otherwise, it is negative, and we have to be vary of code
		 size increase.  */
	      if (flags.min_after_base)
		{
		  /* If INSN is between REL_LAB and MIN_LAB, the size
		     changes we are about to make can change the alignment
		     within the observed offset, therefore we have to break
		     it up into two parts that are independent.  */
		  if (! flags.base_after_vec && flags.min_after_vec)
		    {
		      min_addr -= align_fuzz (rel_lab, insn, rel_align, 0);
		      min_addr -= align_fuzz (insn, min_lab, 0, 0);
		    }
		  else
		    min_addr -= align_fuzz (rel_lab, min_lab, rel_align, 0);
		}
	      else
		{
		  if (flags.base_after_vec && ! flags.min_after_vec)
		    {
		      min_addr -= align_fuzz (min_lab, insn, 0, ~0);
		      min_addr -= align_fuzz (insn, rel_lab, 0, ~0);
		    }
		  else
		    min_addr -= align_fuzz (min_lab, rel_lab, 0, ~0);
		}
	      /* Likewise, determine the highest lowest possible value
		 for the offset of MAX_LAB.  */
	      if (flags.max_after_base)
		{
		  if (! flags.base_after_vec && flags.max_after_vec)
		    {
		      max_addr += align_fuzz (rel_lab, insn, rel_align, ~0);
		      max_addr += align_fuzz (insn, max_lab, 0, ~0);
		    }
		  else
		    max_addr += align_fuzz (rel_lab, max_lab, rel_align, ~0);
		}
	      else
		{
		  if (flags.base_after_vec && ! flags.max_after_vec)
		    {
		      max_addr += align_fuzz (max_lab, insn, 0, 0);
		      max_addr += align_fuzz (insn, rel_lab, 0, 0);
		    }
		  else
		    max_addr += align_fuzz (max_lab, rel_lab, 0, 0);
		}
	      PUT_MODE (body, CASE_VECTOR_SHORTEN_MODE (min_addr - rel_addr,
							max_addr - rel_addr,
							body));
	      if (JUMP_TABLES_IN_TEXT_SECTION || !HAVE_READONLY_DATA_SECTION)
		{
		  insn_lengths[uid]
		    = (XVECLEN (body, 1) * GET_MODE_SIZE (GET_MODE (body)));
		  insn_current_address += insn_lengths[uid];
		  if (insn_lengths[uid] != old_length)
		    something_changed = 1;
		}

	      continue;
	    }
#endif /* CASE_VECTOR_SHORTEN_MODE */

	  if (! (varying_length[uid]))
	    {
	      if (GET_CODE (insn) == INSN
		  && GET_CODE (PATTERN (insn)) == SEQUENCE)
		{
		  int i;

		  body = PATTERN (insn);
		  for (i = 0; i < XVECLEN (body, 0); i++)
		    {
		      rtx inner_insn = XVECEXP (body, 0, i);
		      int inner_uid = INSN_UID (inner_insn);

		      INSN_ADDRESSES (inner_uid) = insn_current_address;

		      insn_current_address += insn_lengths[inner_uid];
		    }
		}
	      else
		insn_current_address += insn_lengths[uid];

	      continue;
	    }

	  if (GET_CODE (insn) == INSN && GET_CODE (PATTERN (insn)) == SEQUENCE)
	    {
	      int i;

	      body = PATTERN (insn);
	      new_length = 0;
	      for (i = 0; i < XVECLEN (body, 0); i++)
		{
		  rtx inner_insn = XVECEXP (body, 0, i);
		  int inner_uid = INSN_UID (inner_insn);
		  int inner_length;

		  INSN_ADDRESSES (inner_uid) = insn_current_address;

		  /* insn_current_length returns 0 for insns with a
		     non-varying length.  */
		  if (! varying_length[inner_uid])
		    inner_length = insn_lengths[inner_uid];
		  else
		    inner_length = insn_current_length (inner_insn);

		  if (inner_length != insn_lengths[inner_uid])
		    {
		      insn_lengths[inner_uid] = inner_length;
		      something_changed = 1;
		    }
		  insn_current_address += insn_lengths[inner_uid];
		  new_length += inner_length;
		}
	    }
	  else
	    {
	      new_length = insn_current_length (insn);
	      insn_current_address += new_length;
	    }

#ifdef ADJUST_INSN_LENGTH
	  /* If needed, do any adjustment.  */
	  tmp_length = new_length;
	  ADJUST_INSN_LENGTH (insn, new_length);
	  insn_current_address += (new_length - tmp_length);
#endif

	  if (new_length != insn_lengths[uid])
	    {
	      insn_lengths[uid] = new_length;
	      something_changed = 1;
	    }
	}
      /* For a non-optimizing compile, do only a single pass.  */
      if (!optimize)
	break;
    }

  free (varying_length);

#endif /* HAVE_ATTR_length */
}

#ifdef HAVE_ATTR_length
/* Given the body of an INSN known to be generated by an ASM statement, return
   the number of machine instructions likely to be generated for this insn.
   This is used to compute its length.  */

static int
asm_insn_count (body)
     rtx body;
{
  const char *template;
  int count = 1;

  if (GET_CODE (body) == ASM_INPUT)
    template = XSTR (body, 0);
  else
    template = decode_asm_operands (body, NULL, NULL, NULL, NULL);

  for (; *template; template++)
    if (IS_ASM_LOGICAL_LINE_SEPARATOR (*template) || *template == '\n')
      count++;

  return count;
}
#endif

/* Output assembler code for the start of a function,
   and initialize some of the variables in this file
   for the new function.  The label for the function and associated
   assembler pseudo-ops have already been output in `assemble_start_function'.

   FIRST is the first insn of the rtl for the function being compiled.
   FILE is the file to write assembler code to.
   OPTIMIZE is nonzero if we should eliminate redundant
     test and compare insns.  */

void
final_start_function (first, file, optimize)
     rtx first;
     FILE *file;
     int optimize ATTRIBUTE_UNUSED;
{
  block_depth = 0;

  this_is_asm_operands = 0;

#ifdef NON_SAVING_SETJMP
  /* A function that calls setjmp should save and restore all the
     call-saved registers on a system where longjmp clobbers them.  */
  if (NON_SAVING_SETJMP && current_function_calls_setjmp)
    {
      int i;

      for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
	if (!call_used_regs[i])
	  regs_ever_live[i] = 1;
    }
#endif

  if (NOTE_LINE_NUMBER (first) != NOTE_INSN_DELETED)
    notice_source_line (first);
  high_block_linenum = high_function_linenum = last_linenum;

  (*debug_hooks->begin_prologue) (last_linenum, last_filename);

#if defined (DWARF2_UNWIND_INFO) || defined (IA64_UNWIND_INFO)
  if (write_symbols != DWARF2_DEBUG && write_symbols != VMS_AND_DWARF2_DEBUG)
    dwarf2out_begin_prologue (0, NULL);
#endif

#ifdef LEAF_REG_REMAP
  if (current_function_uses_only_leaf_regs)
    leaf_renumber_regs (first);
#endif

  /* The Sun386i and perhaps other machines don't work right
     if the profiling code comes after the prologue.  */
#ifdef PROFILE_BEFORE_PROLOGUE
  if (current_function_profile)
    profile_function (file);
#endif /* PROFILE_BEFORE_PROLOGUE */

#if defined (DWARF2_UNWIND_INFO) && defined (HAVE_prologue)
  if (dwarf2out_do_frame ())
    dwarf2out_frame_debug (NULL_RTX);
#endif

  /* If debugging, assign block numbers to all of the blocks in this
     function.  */
  if (write_symbols)
    {
      remove_unnecessary_notes ();
      scope_to_insns_finalize ();
      number_blocks (current_function_decl);
      /* We never actually put out begin/end notes for the top-level
	 block in the function.  But, conceptually, that block is
	 always needed.  */
      TREE_ASM_WRITTEN (DECL_INITIAL (current_function_decl)) = 1;
    }

  /* First output the function prologue: code to set up the stack frame.  */
  (*targetm.asm_out.function_prologue) (file, get_frame_size ());

  /* If the machine represents the prologue as RTL, the profiling code must
     be emitted when NOTE_INSN_PROLOGUE_END is scanned.  */
#ifdef HAVE_prologue
  if (! HAVE_prologue)
#endif
    profile_after_prologue (file);
}

static void
profile_after_prologue (file)
     FILE *file ATTRIBUTE_UNUSED;
{
#ifndef PROFILE_BEFORE_PROLOGUE
  if (current_function_profile)
    profile_function (file);
#endif /* not PROFILE_BEFORE_PROLOGUE */
}

static void
profile_function (file)
     FILE *file ATTRIBUTE_UNUSED;
{
#ifndef NO_PROFILE_COUNTERS
  int align = MIN (BIGGEST_ALIGNMENT, LONG_TYPE_SIZE);
#endif
#if defined(ASM_OUTPUT_REG_PUSH)
#if defined(STRUCT_VALUE_INCOMING_REGNUM) || defined(STRUCT_VALUE_REGNUM)
  int sval = current_function_returns_struct;
#endif
#if defined(STATIC_CHAIN_INCOMING_REGNUM) || defined(STATIC_CHAIN_REGNUM)
  int cxt = current_function_needs_context;
#endif
#endif /* ASM_OUTPUT_REG_PUSH */

#ifndef NO_PROFILE_COUNTERS
  data_section ();
  ASM_OUTPUT_ALIGN (file, floor_log2 (align / BITS_PER_UNIT));
  ASM_OUTPUT_INTERNAL_LABEL (file, "LP", current_function_funcdef_no);
  assemble_integer (const0_rtx, LONG_TYPE_SIZE / BITS_PER_UNIT, align, 1);
#endif

  function_section (current_function_decl);

#if defined(STRUCT_VALUE_INCOMING_REGNUM) && defined(ASM_OUTPUT_REG_PUSH)
  if (sval)
    ASM_OUTPUT_REG_PUSH (file, STRUCT_VALUE_INCOMING_REGNUM);
#else
#if defined(STRUCT_VALUE_REGNUM) && defined(ASM_OUTPUT_REG_PUSH)
  if (sval)
    {
      ASM_OUTPUT_REG_PUSH (file, STRUCT_VALUE_REGNUM);
    }
#endif
#endif

#if defined(STATIC_CHAIN_INCOMING_REGNUM) && defined(ASM_OUTPUT_REG_PUSH)
  if (cxt)
    ASM_OUTPUT_REG_PUSH (file, STATIC_CHAIN_INCOMING_REGNUM);
#else
#if defined(STATIC_CHAIN_REGNUM) && defined(ASM_OUTPUT_REG_PUSH)
  if (cxt)
    {
      ASM_OUTPUT_REG_PUSH (file, STATIC_CHAIN_REGNUM);
    }
#endif
#endif

  FUNCTION_PROFILER (file, current_function_funcdef_no);

#if defined(STATIC_CHAIN_INCOMING_REGNUM) && defined(ASM_OUTPUT_REG_PUSH)
  if (cxt)
    ASM_OUTPUT_REG_POP (file, STATIC_CHAIN_INCOMING_REGNUM);
#else
#if defined(STATIC_CHAIN_REGNUM) && defined(ASM_OUTPUT_REG_PUSH)
  if (cxt)
    {
      ASM_OUTPUT_REG_POP (file, STATIC_CHAIN_REGNUM);
    }
#endif
#endif

#if defined(STRUCT_VALUE_INCOMING_REGNUM) && defined(ASM_OUTPUT_REG_PUSH)
  if (sval)
    ASM_OUTPUT_REG_POP (file, STRUCT_VALUE_INCOMING_REGNUM);
#else
#if defined(STRUCT_VALUE_REGNUM) && defined(ASM_OUTPUT_REG_PUSH)
  if (sval)
    {
      ASM_OUTPUT_REG_POP (file, STRUCT_VALUE_REGNUM);
    }
#endif
#endif
}

/* Output assembler code for the end of a function.
   For clarity, args are same as those of `final_start_function'
   even though not all of them are needed.  */

void
final_end_function ()
{
  app_disable ();

  (*debug_hooks->end_function) (high_function_linenum);

  /* Finally, output the function epilogue:
     code to restore the stack frame and return to the caller.  */
  (*targetm.asm_out.function_epilogue) (asm_out_file, get_frame_size ());

  /* And debug output.  */
  (*debug_hooks->end_epilogue) (last_linenum, last_filename);

#if defined (DWARF2_UNWIND_INFO)
  if (write_symbols != DWARF2_DEBUG && write_symbols != VMS_AND_DWARF2_DEBUG
      && dwarf2out_do_frame ())
    dwarf2out_end_epilogue (last_linenum, last_filename);
#endif
}

/* Output assembler code for some insns: all or part of a function.
   For description of args, see `final_start_function', above.

   PRESCAN is 1 if we are not really outputting,
     just scanning as if we were outputting.
   Prescanning deletes and rearranges insns just like ordinary output.
   PRESCAN is -2 if we are outputting after having prescanned.
   In this case, don't try to delete or rearrange insns
   because that has already been done.
   Prescanning is done only on certain machines.  */

void
final (first, file, optimize, prescan)
     rtx first;
     FILE *file;
     int optimize;
     int prescan;
{
  rtx insn;
  int max_line = 0;
  int max_uid = 0;

  last_ignored_compare = 0;
  new_block = 1;

  /* Make a map indicating which line numbers appear in this function.
     When producing SDB debugging info, delete troublesome line number
     notes from inlined functions in other files as well as duplicate
     line number notes.  */
#ifdef SDB_DEBUGGING_INFO
  if (write_symbols == SDB_DEBUG)
    {
      rtx last = 0;
      for (insn = first; insn; insn = NEXT_INSN (insn))
	if (GET_CODE (insn) == NOTE && NOTE_LINE_NUMBER (insn) > 0)
	  {
	    if ((RTX_INTEGRATED_P (insn)
		 && strcmp (NOTE_SOURCE_FILE (insn), main_input_filename) != 0)
		 || (last != 0
		     && NOTE_LINE_NUMBER (insn) == NOTE_LINE_NUMBER (last)
		     && NOTE_SOURCE_FILE (insn) == NOTE_SOURCE_FILE (last)))
	      {
		delete_insn (insn);	/* Use delete_note.  */
		continue;
	      }
	    last = insn;
	    if (NOTE_LINE_NUMBER (insn) > max_line)
	      max_line = NOTE_LINE_NUMBER (insn);
	  }
    }
  else
#endif
    {
      for (insn = first; insn; insn = NEXT_INSN (insn))
	if (GET_CODE (insn) == NOTE && NOTE_LINE_NUMBER (insn) > max_line)
	  max_line = NOTE_LINE_NUMBER (insn);
    }

  line_note_exists = (char *) xcalloc (max_line + 1, sizeof (char));

  for (insn = first; insn; insn = NEXT_INSN (insn))
    {
      if (INSN_UID (insn) > max_uid)       /* find largest UID */
	max_uid = INSN_UID (insn);
      if (GET_CODE (insn) == NOTE && NOTE_LINE_NUMBER (insn) > 0)
	line_note_exists[NOTE_LINE_NUMBER (insn)] = 1;
#ifdef HAVE_cc0
      /* If CC tracking across branches is enabled, record the insn which
	 jumps to each branch only reached from one place.  */
      if (optimize && GET_CODE (insn) == JUMP_INSN)
	{
	  rtx lab = JUMP_LABEL (insn);
	  if (lab && LABEL_NUSES (lab) == 1)
	    {
	      LABEL_REFS (lab) = insn;
	    }
	}
#endif
    }

  init_recog ();

  CC_STATUS_INIT;

  /* Output the insns.  */
  for (insn = NEXT_INSN (first); insn;)
    {
#ifdef HAVE_ATTR_length
      if ((unsigned) INSN_UID (insn) >= INSN_ADDRESSES_SIZE ())
	{
	  /* This can be triggered by bugs elsewhere in the compiler if
	     new insns are created after init_insn_lengths is called.  */
	  if (GET_CODE (insn) == NOTE)
	    insn_current_address = -1;
	  else
	    abort ();
	}
      else
	insn_current_address = INSN_ADDRESSES (INSN_UID (insn));
#endif /* HAVE_ATTR_length */

      insn = final_scan_insn (insn, file, optimize, prescan, 0);
    }

  /* Store function names for edge-profiling.  */
  /* ??? Probably should re-use the existing struct function.  */

  if (cfun->arc_profile)
    {
      struct function_list *new_item = xmalloc (sizeof (struct function_list));

      *functions_tail = new_item;
      functions_tail = &new_item->next;

      new_item->next = 0;
      new_item->name = xstrdup (IDENTIFIER_POINTER
				 (DECL_ASSEMBLER_NAME (current_function_decl)));
      new_item->cfg_checksum = profile_info.current_function_cfg_checksum;
      new_item->count_edges = profile_info.count_edges_instrumented_now;
    }

  free (line_note_exists);
  line_note_exists = NULL;
}

const char *
get_insn_template (code, insn)
     int code;
     rtx insn;
{
  const void *output = insn_data[code].output;
  switch (insn_data[code].output_format)
    {
    case INSN_OUTPUT_FORMAT_SINGLE:
      return (const char *) output;
    case INSN_OUTPUT_FORMAT_MULTI:
      return ((const char *const *) output)[which_alternative];
    case INSN_OUTPUT_FORMAT_FUNCTION:
      if (insn == NULL)
	abort ();
      return (*(insn_output_fn) output) (recog_data.operand, insn);

    default:
      abort ();
    }
}

/* Emit the appropriate declaration for an alternate-entry-point
   symbol represented by INSN, to FILE.  INSN is a CODE_LABEL with
   LABEL_KIND != LABEL_NORMAL.

   The case fall-through in this function is intentional.  */
static void
output_alternate_entry_point (file, insn)
     FILE *file;
     rtx insn;
{
  const char *name = LABEL_NAME (insn);

  switch (LABEL_KIND (insn))
    {
    case LABEL_WEAK_ENTRY:
#ifdef ASM_WEAKEN_LABEL
      ASM_WEAKEN_LABEL (file, name);
#endif
    case LABEL_GLOBAL_ENTRY:
      (*targetm.asm_out.globalize_label) (file, name);
    case LABEL_STATIC_ENTRY:
#ifdef ASM_OUTPUT_TYPE_DIRECTIVE
      ASM_OUTPUT_TYPE_DIRECTIVE (file, name, "function");
#endif
      ASM_OUTPUT_LABEL (file, name);
      break;

    case LABEL_NORMAL:
    default:
      abort ();
    }
}

/* The final scan for one insn, INSN.
   Args are same as in `final', except that INSN
   is the insn being scanned.
   Value returned is the next insn to be scanned.

   NOPEEPHOLES is the flag to disallow peephole processing (currently
   used for within delayed branch sequence output).  */

rtx
final_scan_insn (insn, file, optimize, prescan, nopeepholes)
     rtx insn;
     FILE *file;
     int optimize ATTRIBUTE_UNUSED;
     int prescan;
     int nopeepholes ATTRIBUTE_UNUSED;
{
#ifdef HAVE_cc0
  rtx set;
#endif

  insn_counter++;

  /* Ignore deleted insns.  These can occur when we split insns (due to a
     template of "#") while not optimizing.  */
  if (INSN_DELETED_P (insn))
    return NEXT_INSN (insn);

  switch (GET_CODE (insn))
    {
    case NOTE:
      if (prescan > 0)
	break;

      switch (NOTE_LINE_NUMBER (insn))
	{
	case NOTE_INSN_DELETED:
	case NOTE_INSN_LOOP_BEG:
	case NOTE_INSN_LOOP_END:
	case NOTE_INSN_LOOP_END_TOP_COND:
	case NOTE_INSN_LOOP_CONT:
	case NOTE_INSN_LOOP_VTOP:
	case NOTE_INSN_FUNCTION_END:
	case NOTE_INSN_REPEATED_LINE_NUMBER:
	case NOTE_INSN_EXPECTED_VALUE:
	  break;

	case NOTE_INSN_BASIC_BLOCK:
#ifdef IA64_UNWIND_INFO
	  IA64_UNWIND_EMIT (asm_out_file, insn);
#endif
	  if (flag_debug_asm)
	    fprintf (asm_out_file, "\t%s basic block %d\n",
		     ASM_COMMENT_START, NOTE_BASIC_BLOCK (insn)->index);
	  break;

	case NOTE_INSN_EH_REGION_BEG:
	  ASM_OUTPUT_DEBUG_LABEL (asm_out_file, "LEHB",
				  NOTE_EH_HANDLER (insn));
	  break;

	case NOTE_INSN_EH_REGION_END:
	  ASM_OUTPUT_DEBUG_LABEL (asm_out_file, "LEHE",
				  NOTE_EH_HANDLER (insn));
	  break;

	case NOTE_INSN_PROLOGUE_END:
	  (*targetm.asm_out.function_end_prologue) (file);
	  profile_after_prologue (file);
	  break;

	case NOTE_INSN_EPILOGUE_BEG:
	  (*targetm.asm_out.function_begin_epilogue) (file);
	  break;

	case NOTE_INSN_FUNCTION_BEG:
	  app_disable ();
	  (*debug_hooks->end_prologue) (last_linenum, last_filename);
	  break;

	case NOTE_INSN_BLOCK_BEG:
	  if (debug_info_level == DINFO_LEVEL_NORMAL
	      || debug_info_level == DINFO_LEVEL_VERBOSE
	      || write_symbols == DWARF_DEBUG
	      || write_symbols == DWARF2_DEBUG
	      || write_symbols == VMS_AND_DWARF2_DEBUG
	      || write_symbols == VMS_DEBUG)
	    {
	      int n = BLOCK_NUMBER (NOTE_BLOCK (insn));

	      app_disable ();
	      ++block_depth;
	      high_block_linenum = last_linenum;

	      /* Output debugging info about the symbol-block beginning.  */
	      (*debug_hooks->begin_block) (last_linenum, n);

	      /* Mark this block as output.  */
	      TREE_ASM_WRITTEN (NOTE_BLOCK (insn)) = 1;
	    }
	  break;

	case NOTE_INSN_BLOCK_END:
	  if (debug_info_level == DINFO_LEVEL_NORMAL
	      || debug_info_level == DINFO_LEVEL_VERBOSE
	      || write_symbols == DWARF_DEBUG
	      || write_symbols == DWARF2_DEBUG
	      || write_symbols == VMS_AND_DWARF2_DEBUG
	      || write_symbols == VMS_DEBUG)
	    {
	      int n = BLOCK_NUMBER (NOTE_BLOCK (insn));

	      app_disable ();

	      /* End of a symbol-block.  */
	      --block_depth;
	      if (block_depth < 0)
		abort ();

	      (*debug_hooks->end_block) (high_block_linenum, n);
	    }
	  break;

	case NOTE_INSN_DELETED_LABEL:
	  /* Emit the label.  We may have deleted the CODE_LABEL because
	     the label could be proved to be unreachable, though still
	     referenced (in the form of having its address taken.  */
	  ASM_OUTPUT_DEBUG_LABEL (file, "L", CODE_LABEL_NUMBER (insn));
	  break;

	case 0:
	  break;

	default:
	  if (NOTE_LINE_NUMBER (insn) <= 0)
	    abort ();

	  /* This note is a line-number.  */
	  {
	    rtx note;
	    int note_after = 0;

	    /* If there is anything real after this note, output it.
	       If another line note follows, omit this one.  */
	    for (note = NEXT_INSN (insn); note; note = NEXT_INSN (note))
	      {
		if (GET_CODE (note) != NOTE && GET_CODE (note) != CODE_LABEL)
		  break;

		/* These types of notes can be significant
		   so make sure the preceding line number stays.  */
		else if (GET_CODE (note) == NOTE
			 && (NOTE_LINE_NUMBER (note) == NOTE_INSN_BLOCK_BEG
			     || NOTE_LINE_NUMBER (note) == NOTE_INSN_BLOCK_END
			     || NOTE_LINE_NUMBER (note) == NOTE_INSN_FUNCTION_BEG))
		  break;
		else if (GET_CODE (note) == NOTE && NOTE_LINE_NUMBER (note) > 0)
		  {
		    /* Another line note follows; we can delete this note
		       if no intervening line numbers have notes elsewhere.  */
		    int num;
		    for (num = NOTE_LINE_NUMBER (insn) + 1;
		         num < NOTE_LINE_NUMBER (note);
		         num++)
		      if (line_note_exists[num])
			break;

		    if (num >= NOTE_LINE_NUMBER (note))
		      note_after = 1;
		    break;
		  }
	      }

	    /* Output this line note if it is the first or the last line
	       note in a row.  */
	    if (!note_after)
	      {
		notice_source_line (insn);
		(*debug_hooks->source_line) (last_linenum, last_filename);
	      }
	  }
	  break;
	}
      break;

    case BARRIER:
#if defined (DWARF2_UNWIND_INFO)
      if (dwarf2out_do_frame ())
	dwarf2out_frame_debug (insn);
#endif
      break;

    case CODE_LABEL:
      /* The target port might emit labels in the output function for
	 some insn, e.g. sh.c output_branchy_insn.  */
      if (CODE_LABEL_NUMBER (insn) <= max_labelno)
	{
	  int align = LABEL_TO_ALIGNMENT (insn);
#ifdef ASM_OUTPUT_MAX_SKIP_ALIGN
	  int max_skip = LABEL_TO_MAX_SKIP (insn);
#endif

	  if (align && NEXT_INSN (insn))
	    {
#ifdef ASM_OUTPUT_MAX_SKIP_ALIGN
	      ASM_OUTPUT_MAX_SKIP_ALIGN (file, align, max_skip);
#else
#ifdef ASM_OUTPUT_ALIGN_WITH_NOP
              ASM_OUTPUT_ALIGN_WITH_NOP (file, align);
#else
	      ASM_OUTPUT_ALIGN (file, align);
#endif
#endif
	    }
	}
#ifdef HAVE_cc0
      CC_STATUS_INIT;
      /* If this label is reached from only one place, set the condition
	 codes from the instruction just before the branch.  */

      /* Disabled because some insns set cc_status in the C output code
	 and NOTICE_UPDATE_CC alone can set incorrect status.  */
      if (0 /* optimize && LABEL_NUSES (insn) == 1*/)
	{
	  rtx jump = LABEL_REFS (insn);
	  rtx barrier = prev_nonnote_insn (insn);
	  rtx prev;
	  /* If the LABEL_REFS field of this label has been set to point
	     at a branch, the predecessor of the branch is a regular
	     insn, and that branch is the only way to reach this label,
	     set the condition codes based on the branch and its
	     predecessor.  */
	  if (barrier && GET_CODE (barrier) == BARRIER
	      && jump && GET_CODE (jump) == JUMP_INSN
	      && (prev = prev_nonnote_insn (jump))
	      && GET_CODE (prev) == INSN)
	    {
	      NOTICE_UPDATE_CC (PATTERN (prev), prev);
	      NOTICE_UPDATE_CC (PATTERN (jump), jump);
	    }
	}
#endif
      if (prescan > 0)
	break;
      new_block = 1;

#ifdef FINAL_PRESCAN_LABEL
      FINAL_PRESCAN_INSN (insn, NULL, 0);
#endif

      if (LABEL_NAME (insn))
	(*debug_hooks->label) (insn);

      if (app_on)
	{
	  fputs (ASM_APP_OFF, file);
	  app_on = 0;
	}
      if (NEXT_INSN (insn) != 0
	  && GET_CODE (NEXT_INSN (insn)) == JUMP_INSN)
	{
	  rtx nextbody = PATTERN (NEXT_INSN (insn));

	  /* If this label is followed by a jump-table,
	     make sure we put the label in the read-only section.  Also
	     possibly write the label and jump table together.  */

	  if (GET_CODE (nextbody) == ADDR_VEC
	      || GET_CODE (nextbody) == ADDR_DIFF_VEC)
	    {
#if defined(ASM_OUTPUT_ADDR_VEC) || defined(ASM_OUTPUT_ADDR_DIFF_VEC)
	      /* In this case, the case vector is being moved by the
		 target, so don't output the label at all.  Leave that
		 to the back end macros.  */
#else
	      if (! JUMP_TABLES_IN_TEXT_SECTION)
		{
		  int log_align;

		  readonly_data_section ();

#ifdef ADDR_VEC_ALIGN
		  log_align = ADDR_VEC_ALIGN (NEXT_INSN (insn));
#else
		  log_align = exact_log2 (BIGGEST_ALIGNMENT / BITS_PER_UNIT);
#endif
		  ASM_OUTPUT_ALIGN (file, log_align);
		}
	      else
		function_section (current_function_decl);

#ifdef ASM_OUTPUT_CASE_LABEL
	      ASM_OUTPUT_CASE_LABEL (file, "L", CODE_LABEL_NUMBER (insn),
				     NEXT_INSN (insn));
#else
	      ASM_OUTPUT_INTERNAL_LABEL (file, "L", CODE_LABEL_NUMBER (insn));
#endif
#endif
	      break;
	    }
	}
      if (LABEL_ALT_ENTRY_P (insn))
	output_alternate_entry_point (file, insn);
      else
	ASM_OUTPUT_INTERNAL_LABEL (file, "L", CODE_LABEL_NUMBER (insn));
      break;

    default:
      {
	rtx body = PATTERN (insn);
	int insn_code_number;
	const char *template;
	rtx note;

	/* An INSN, JUMP_INSN or CALL_INSN.
	   First check for special kinds that recog doesn't recognize.  */

	if (GET_CODE (body) == USE /* These are just declarations */
	    || GET_CODE (body) == CLOBBER)
	  break;

#ifdef HAVE_cc0
	/* If there is a REG_CC_SETTER note on this insn, it means that
	   the setting of the condition code was done in the delay slot
	   of the insn that branched here.  So recover the cc status
	   from the insn that set it.  */

	note = find_reg_note (insn, REG_CC_SETTER, NULL_RTX);
	if (note)
	  {
	    NOTICE_UPDATE_CC (PATTERN (XEXP (note, 0)), XEXP (note, 0));
	    cc_prev_status = cc_status;
	  }
#endif

	/* Detect insns that are really jump-tables
	   and output them as such.  */

	if (GET_CODE (body) == ADDR_VEC || GET_CODE (body) == ADDR_DIFF_VEC)
	  {
#if !(defined(ASM_OUTPUT_ADDR_VEC) || defined(ASM_OUTPUT_ADDR_DIFF_VEC))
	    int vlen, idx;
#endif

	    if (prescan > 0)
	      break;

	    if (app_on)
	      {
		fputs (ASM_APP_OFF, file);
		app_on = 0;
	      }

#if defined(ASM_OUTPUT_ADDR_VEC) || defined(ASM_OUTPUT_ADDR_DIFF_VEC)
	    if (GET_CODE (body) == ADDR_VEC)
	      {
#ifdef ASM_OUTPUT_ADDR_VEC
		ASM_OUTPUT_ADDR_VEC (PREV_INSN (insn), body);
#else
		abort ();
#endif
	      }
	    else
	      {
#ifdef ASM_OUTPUT_ADDR_DIFF_VEC
		ASM_OUTPUT_ADDR_DIFF_VEC (PREV_INSN (insn), body);
#else
		abort ();
#endif
	      }
#else
	    vlen = XVECLEN (body, GET_CODE (body) == ADDR_DIFF_VEC);
	    for (idx = 0; idx < vlen; idx++)
	      {
		if (GET_CODE (body) == ADDR_VEC)
		  {
#ifdef ASM_OUTPUT_ADDR_VEC_ELT
		    ASM_OUTPUT_ADDR_VEC_ELT
		      (file, CODE_LABEL_NUMBER (XEXP (XVECEXP (body, 0, idx), 0)));
#else
		    abort ();
#endif
		  }
		else
		  {
#ifdef ASM_OUTPUT_ADDR_DIFF_ELT
		    ASM_OUTPUT_ADDR_DIFF_ELT
		      (file,
		       body,
		       CODE_LABEL_NUMBER (XEXP (XVECEXP (body, 1, idx), 0)),
		       CODE_LABEL_NUMBER (XEXP (XEXP (body, 0), 0)));
#else
		    abort ();
#endif
		  }
	      }
#ifdef ASM_OUTPUT_CASE_END
	    ASM_OUTPUT_CASE_END (file,
				 CODE_LABEL_NUMBER (PREV_INSN (insn)),
				 insn);
#endif
#endif

	    function_section (current_function_decl);

	    break;
	  }

	if (GET_CODE (body) == ASM_INPUT)
	  {
	    const char *string = XSTR (body, 0);

	    /* There's no telling what that did to the condition codes.  */
	    CC_STATUS_INIT;
	    if (prescan > 0)
	      break;

	    if (string[0])
	      {
		if (! app_on)
		  {
		    fputs (ASM_APP_ON, file);
		    app_on = 1;
		  }
		fprintf (asm_out_file, "\t%s\n", string);
	      }
	    break;
	  }

	/* Detect `asm' construct with operands.  */
	if (asm_noperands (body) >= 0)
	  {
	    unsigned int noperands = asm_noperands (body);
	    rtx *ops = (rtx *) alloca (noperands * sizeof (rtx));
	    const char *string;

	    /* There's no telling what that did to the condition codes.  */
	    CC_STATUS_INIT;
	    if (prescan > 0)
	      break;

	    /* Get out the operand values.  */
	    string = decode_asm_operands (body, ops, NULL, NULL, NULL);
	    /* Inhibit aborts on what would otherwise be compiler bugs.  */
	    insn_noperands = noperands;
	    this_is_asm_operands = insn;

	    /* Output the insn using them.  */
	    if (string[0])
	      {
		if (! app_on)
		  {
		    fputs (ASM_APP_ON, file);
		    app_on = 1;
		  }
	        output_asm_insn (string, ops);
	      }

	    this_is_asm_operands = 0;
	    break;
	  }

	if (prescan <= 0 && app_on)
	  {
	    fputs (ASM_APP_OFF, file);
	    app_on = 0;
	  }

	if (GET_CODE (body) == SEQUENCE)
	  {
	    /* A delayed-branch sequence */
	    int i;
	    rtx next;

	    if (prescan > 0)
	      break;
	    final_sequence = body;

	    /* The first insn in this SEQUENCE might be a JUMP_INSN that will
	       force the restoration of a comparison that was previously
	       thought unnecessary.  If that happens, cancel this sequence
	       and cause that insn to be restored.  */

	    next = final_scan_insn (XVECEXP (body, 0, 0), file, 0, prescan, 1);
	    if (next != XVECEXP (body, 0, 1))
	      {
		final_sequence = 0;
		return next;
	      }

	    for (i = 1; i < XVECLEN (body, 0); i++)
	      {
		rtx insn = XVECEXP (body, 0, i);
		rtx next = NEXT_INSN (insn);
		/* We loop in case any instruction in a delay slot gets
		   split.  */
		do
		  insn = final_scan_insn (insn, file, 0, prescan, 1);
		while (insn != next);
	      }
#ifdef DBR_OUTPUT_SEQEND
	    DBR_OUTPUT_SEQEND (file);
#endif
	    final_sequence = 0;

	    /* If the insn requiring the delay slot was a CALL_INSN, the
	       insns in the delay slot are actually executed before the
	       called function.  Hence we don't preserve any CC-setting
	       actions in these insns and the CC must be marked as being
	       clobbered by the function.  */
	    if (GET_CODE (XVECEXP (body, 0, 0)) == CALL_INSN)
	      {
		CC_STATUS_INIT;
	      }
	    break;
	  }

	/* We have a real machine instruction as rtl.  */

	body = PATTERN (insn);

#ifdef HAVE_cc0
	set = single_set (insn);

	/* Check for redundant test and compare instructions
	   (when the condition codes are already set up as desired).
	   This is done only when optimizing; if not optimizing,
	   it should be possible for the user to alter a variable
	   with the debugger in between statements
	   and the next statement should reexamine the variable
	   to compute the condition codes.  */

	if (optimize)
	  {
#if 0
	    rtx set = single_set (insn);
#endif

	    if (set
		&& GET_CODE (SET_DEST (set)) == CC0
		&& insn != last_ignored_compare)
	      {
		if (GET_CODE (SET_SRC (set)) == SUBREG)
		  SET_SRC (set) = alter_subreg (&SET_SRC (set));
		else if (GET_CODE (SET_SRC (set)) == COMPARE)
		  {
		    if (GET_CODE (XEXP (SET_SRC (set), 0)) == SUBREG)
		      XEXP (SET_SRC (set), 0)
			= alter_subreg (&XEXP (SET_SRC (set), 0));
		    if (GET_CODE (XEXP (SET_SRC (set), 1)) == SUBREG)
		      XEXP (SET_SRC (set), 1)
			= alter_subreg (&XEXP (SET_SRC (set), 1));
		  }
		if ((cc_status.value1 != 0
		     && rtx_equal_p (SET_SRC (set), cc_status.value1))
		    || (cc_status.value2 != 0
			&& rtx_equal_p (SET_SRC (set), cc_status.value2)))
		  {
		    /* Don't delete insn if it has an addressing side-effect.  */
		    if (! FIND_REG_INC_NOTE (insn, NULL_RTX)
			/* or if anything in it is volatile.  */
			&& ! volatile_refs_p (PATTERN (insn)))
		      {
			/* We don't really delete the insn; just ignore it.  */
			last_ignored_compare = insn;
			break;
		      }
		  }
	      }
	  }
#endif

#ifndef STACK_REGS
	/* Don't bother outputting obvious no-ops, even without -O.
	   This optimization is fast and doesn't interfere with debugging.
	   Don't do this if the insn is in a delay slot, since this
	   will cause an improper number of delay insns to be written.  */
	if (final_sequence == 0
	    && prescan >= 0
	    && GET_CODE (insn) == INSN && GET_CODE (body) == SET
	    && GET_CODE (SET_SRC (body)) == REG
	    && GET_CODE (SET_DEST (body)) == REG
	    && REGNO (SET_SRC (body)) == REGNO (SET_DEST (body)))
	  break;
#endif

#ifdef HAVE_cc0
	/* If this is a conditional branch, maybe modify it
	   if the cc's are in a nonstandard state
	   so that it accomplishes the same thing that it would
	   do straightforwardly if the cc's were set up normally.  */

	if (cc_status.flags != 0
	    && GET_CODE (insn) == JUMP_INSN
	    && GET_CODE (body) == SET
	    && SET_DEST (body) == pc_rtx
	    && GET_CODE (SET_SRC (body)) == IF_THEN_ELSE
	    && GET_RTX_CLASS (GET_CODE (XEXP (SET_SRC (body), 0))) == '<'
	    && XEXP (XEXP (SET_SRC (body), 0), 0) == cc0_rtx
	    /* This is done during prescan; it is not done again
	       in final scan when prescan has been done.  */
	    && prescan >= 0)
	  {
	    /* This function may alter the contents of its argument
	       and clear some of the cc_status.flags bits.
	       It may also return 1 meaning condition now always true
	       or -1 meaning condition now always false
	       or 2 meaning condition nontrivial but altered.  */
	    int result = alter_cond (XEXP (SET_SRC (body), 0));
	    /* If condition now has fixed value, replace the IF_THEN_ELSE
	       with its then-operand or its else-operand.  */
	    if (result == 1)
	      SET_SRC (body) = XEXP (SET_SRC (body), 1);
	    if (result == -1)
	      SET_SRC (body) = XEXP (SET_SRC (body), 2);

	    /* The jump is now either unconditional or a no-op.
	       If it has become a no-op, don't try to output it.
	       (It would not be recognized.)  */
	    if (SET_SRC (body) == pc_rtx)
	      {
	        delete_insn (insn);
		break;
	      }
	    else if (GET_CODE (SET_SRC (body)) == RETURN)
	      /* Replace (set (pc) (return)) with (return).  */
	      PATTERN (insn) = body = SET_SRC (body);

	    /* Rerecognize the instruction if it has changed.  */
	    if (result != 0)
	      INSN_CODE (insn) = -1;
	  }

	/* Make same adjustments to instructions that examine the
	   condition codes without jumping and instructions that
	   handle conditional moves (if this machine has either one).  */

	if (cc_status.flags != 0
	    && set != 0)
	  {
	    rtx cond_rtx, then_rtx, else_rtx;

	    if (GET_CODE (insn) != JUMP_INSN
		&& GET_CODE (SET_SRC (set)) == IF_THEN_ELSE)
	      {
		cond_rtx = XEXP (SET_SRC (set), 0);
		then_rtx = XEXP (SET_SRC (set), 1);
		else_rtx = XEXP (SET_SRC (set), 2);
	      }
	    else
	      {
		cond_rtx = SET_SRC (set);
		then_rtx = const_true_rtx;
		else_rtx = const0_rtx;
	      }

	    switch (GET_CODE (cond_rtx))
	      {
	      case GTU:
	      case GT:
	      case LTU:
	      case LT:
	      case GEU:
	      case GE:
	      case LEU:
	      case LE:
	      case EQ:
	      case NE:
		{
		  int result;
		  if (XEXP (cond_rtx, 0) != cc0_rtx)
		    break;
		  result = alter_cond (cond_rtx);
		  if (result == 1)
		    validate_change (insn, &SET_SRC (set), then_rtx, 0);
		  else if (result == -1)
		    validate_change (insn, &SET_SRC (set), else_rtx, 0);
		  else if (result == 2)
		    INSN_CODE (insn) = -1;
		  if (SET_DEST (set) == SET_SRC (set))
		    delete_insn (insn);
		}
		break;

	      default:
		break;
	      }
	  }

#endif

#ifdef HAVE_peephole
	/* Do machine-specific peephole optimizations if desired.  */

	if (optimize && !flag_no_peephole && !nopeepholes)
	  {
	    rtx next = peephole (insn);
	    /* When peepholing, if there were notes within the peephole,
	       emit them before the peephole.  */
	    if (next != 0 && next != NEXT_INSN (insn))
	      {
		rtx prev = PREV_INSN (insn);

		for (note = NEXT_INSN (insn); note != next;
		     note = NEXT_INSN (note))
		  final_scan_insn (note, file, optimize, prescan, nopeepholes);

		/* In case this is prescan, put the notes
		   in proper position for later rescan.  */
		note = NEXT_INSN (insn);
		PREV_INSN (note) = prev;
		NEXT_INSN (prev) = note;
		NEXT_INSN (PREV_INSN (next)) = insn;
		PREV_INSN (insn) = PREV_INSN (next);
		NEXT_INSN (insn) = next;
		PREV_INSN (next) = insn;
	      }

	    /* PEEPHOLE might have changed this.  */
	    body = PATTERN (insn);
	  }
#endif

	/* Try to recognize the instruction.
	   If successful, verify that the operands satisfy the
	   constraints for the instruction.  Crash if they don't,
	   since `reload' should have changed them so that they do.  */

	insn_code_number = recog_memoized (insn);
	cleanup_subreg_operands (insn);

	/* Dump the insn in the assembly for debugging.  */
	if (flag_dump_rtl_in_asm)
	  {
	    print_rtx_head = ASM_COMMENT_START;
	    print_rtl_single (asm_out_file, insn);
	    print_rtx_head = "";
	  }

	if (! constrain_operands_cached (1))
	  fatal_insn_not_found (insn);

	/* Some target machines need to prescan each insn before
	   it is output.  */

#ifdef FINAL_PRESCAN_INSN
	FINAL_PRESCAN_INSN (insn, recog_data.operand, recog_data.n_operands);
#endif

#ifdef HAVE_conditional_execution
	if (GET_CODE (PATTERN (insn)) == COND_EXEC)
	  current_insn_predicate = COND_EXEC_TEST (PATTERN (insn));
	else
	  current_insn_predicate = NULL_RTX;
#endif

#ifdef HAVE_cc0
	cc_prev_status = cc_status;

	/* Update `cc_status' for this instruction.
	   The instruction's output routine may change it further.
	   If the output routine for a jump insn needs to depend
	   on the cc status, it should look at cc_prev_status.  */

	NOTICE_UPDATE_CC (body, insn);
#endif

	current_output_insn = debug_insn = insn;

#if defined (DWARF2_UNWIND_INFO)
	if (GET_CODE (insn) == CALL_INSN && dwarf2out_do_frame ())
	  dwarf2out_frame_debug (insn);
#endif

	/* Find the proper template for this insn.  */
	template = get_insn_template (insn_code_number, insn);

	/* If the C code returns 0, it means that it is a jump insn
	   which follows a deleted test insn, and that test insn
	   needs to be reinserted.  */
	if (template == 0)
	  {
	    rtx prev;

	    if (prev_nonnote_insn (insn) != last_ignored_compare)
	      abort ();
	    new_block = 0;

	    /* We have already processed the notes between the setter and
	       the user.  Make sure we don't process them again, this is
	       particularly important if one of the notes is a block
	       scope note or an EH note.  */
	    for (prev = insn;
		 prev != last_ignored_compare;
		 prev = PREV_INSN (prev))
	      {
		if (GET_CODE (prev) == NOTE)
		  delete_insn (prev);	/* Use delete_note.  */
	      }

	    return prev;
	  }

	/* If the template is the string "#", it means that this insn must
	   be split.  */
	if (template[0] == '#' && template[1] == '\0')
	  {
	    rtx new = try_split (body, insn, 0);

	    /* If we didn't split the insn, go away.  */
	    if (new == insn && PATTERN (new) == body)
	      fatal_insn ("could not split insn", insn);

#ifdef HAVE_ATTR_length
	    /* This instruction should have been split in shorten_branches,
	       to ensure that we would have valid length info for the
	       splitees.  */
	    abort ();
#endif

	    new_block = 0;
	    return new;
	  }

	if (prescan > 0)
	  break;

#ifdef IA64_UNWIND_INFO
	IA64_UNWIND_EMIT (asm_out_file, insn);
#endif
	/* Output assembler code from the template.  */

	output_asm_insn (template, recog_data.operand);

#if defined (DWARF2_UNWIND_INFO)
#if defined (HAVE_prologue)
	if (GET_CODE (insn) == INSN && dwarf2out_do_frame ())
	  dwarf2out_frame_debug (insn);
#else
	if (!ACCUMULATE_OUTGOING_ARGS
	    && GET_CODE (insn) == INSN
	    && dwarf2out_do_frame ())
	  dwarf2out_frame_debug (insn);
#endif
#endif

#if 0
	/* It's not at all clear why we did this and doing so interferes
	   with tests we'd like to do to use REG_WAS_0 notes, so let's try
	   with this out.  */

	/* Mark this insn as having been output.  */
	INSN_DELETED_P (insn) = 1;
#endif

	/* Emit information for vtable gc.  */
	note = find_reg_note (insn, REG_VTABLE_REF, NULL_RTX);
	if (note)
	  assemble_vtable_entry (XEXP (XEXP (note, 0), 0),
				 INTVAL (XEXP (XEXP (note, 0), 1)));

	current_output_insn = debug_insn = 0;
      }
    }
  return NEXT_INSN (insn);
}

/* Output debugging info to the assembler file FILE
   based on the NOTE-insn INSN, assumed to be a line number.  */

static void
notice_source_line (insn)
     rtx insn;
{
  const char *filename = NOTE_SOURCE_FILE (insn);

  last_filename = filename;
  last_linenum = NOTE_LINE_NUMBER (insn);
  high_block_linenum = MAX (last_linenum, high_block_linenum);
  high_function_linenum = MAX (last_linenum, high_function_linenum);
}

/* For each operand in INSN, simplify (subreg (reg)) so that it refers
   directly to the desired hard register.  */

void
cleanup_subreg_operands (insn)
     rtx insn;
{
  int i;
  extract_insn_cached (insn);
  for (i = 0; i < recog_data.n_operands; i++)
    {
      /* The following test cannot use recog_data.operand when tesing
	 for a SUBREG: the underlying object might have been changed
	 already if we are inside a match_operator expression that
	 matches the else clause.  Instead we test the underlying
	 expression directly.  */
      if (GET_CODE (*recog_data.operand_loc[i]) == SUBREG)
	recog_data.operand[i] = alter_subreg (recog_data.operand_loc[i]);
      else if (GET_CODE (recog_data.operand[i]) == PLUS
	       || GET_CODE (recog_data.operand[i]) == MULT
	       || GET_CODE (recog_data.operand[i]) == MEM)
	recog_data.operand[i] = walk_alter_subreg (recog_data.operand_loc[i]);
    }

  for (i = 0; i < recog_data.n_dups; i++)
    {
      if (GET_CODE (*recog_data.dup_loc[i]) == SUBREG)
	*recog_data.dup_loc[i] = alter_subreg (recog_data.dup_loc[i]);
      else if (GET_CODE (*recog_data.dup_loc[i]) == PLUS
	       || GET_CODE (*recog_data.dup_loc[i]) == MULT
	       || GET_CODE (*recog_data.dup_loc[i]) == MEM)
	*recog_data.dup_loc[i] = walk_alter_subreg (recog_data.dup_loc[i]);
    }
}

/* If X is a SUBREG, replace it with a REG or a MEM,
   based on the thing it is a subreg of.  */

rtx
alter_subreg (xp)
     rtx *xp;
{
  rtx x = *xp;
  rtx y = SUBREG_REG (x);

  /* simplify_subreg does not remove subreg from volatile references.
     We are required to.  */
  if (GET_CODE (y) == MEM)
    *xp = adjust_address (y, GET_MODE (x), SUBREG_BYTE (x));
  else
    {
      rtx new = simplify_subreg (GET_MODE (x), y, GET_MODE (y),
				 SUBREG_BYTE (x));

      if (new != 0)
	*xp = new;
      /* Simplify_subreg can't handle some REG cases, but we have to.  */
      else if (GET_CODE (y) == REG)
	{
	  unsigned int regno = subreg_hard_regno (x, 1);
	  PUT_CODE (x, REG);
	  REGNO (x) = regno;
	  ORIGINAL_REGNO (x) = ORIGINAL_REGNO (y);
	  /* This field has a different meaning for REGs and SUBREGs.  Make
	     sure to clear it!  */
	  RTX_FLAG (x, used) = 0;
	}
      else
	abort ();
    }

  return *xp;
}

/* Do alter_subreg on all the SUBREGs contained in X.  */

static rtx
walk_alter_subreg (xp)
     rtx *xp;
{
  rtx x = *xp;
  switch (GET_CODE (x))
    {
    case PLUS:
    case MULT:
      XEXP (x, 0) = walk_alter_subreg (&XEXP (x, 0));
      XEXP (x, 1) = walk_alter_subreg (&XEXP (x, 1));
      break;

    case MEM:
      XEXP (x, 0) = walk_alter_subreg (&XEXP (x, 0));
      break;

    case SUBREG:
      return alter_subreg (xp);

    default:
      break;
    }

  return *xp;
}

#ifdef HAVE_cc0

/* Given BODY, the body of a jump instruction, alter the jump condition
   as required by the bits that are set in cc_status.flags.
   Not all of the bits there can be handled at this level in all cases.

   The value is normally 0.
   1 means that the condition has become always true.
   -1 means that the condition has become always false.
   2 means that COND has been altered.  */

static int
alter_cond (cond)
     rtx cond;
{
  int value = 0;

  if (cc_status.flags & CC_REVERSED)
    {
      value = 2;
      PUT_CODE (cond, swap_condition (GET_CODE (cond)));
    }

  if (cc_status.flags & CC_INVERTED)
    {
      value = 2;
      PUT_CODE (cond, reverse_condition (GET_CODE (cond)));
    }

  if (cc_status.flags & CC_NOT_POSITIVE)
    switch (GET_CODE (cond))
      {
      case LE:
      case LEU:
      case GEU:
	/* Jump becomes unconditional.  */
	return 1;

      case GT:
      case GTU:
      case LTU:
	/* Jump becomes no-op.  */
	return -1;

      case GE:
	PUT_CODE (cond, EQ);
	value = 2;
	break;

      case LT:
	PUT_CODE (cond, NE);
	value = 2;
	break;

      default:
	break;
      }

  if (cc_status.flags & CC_NOT_NEGATIVE)
    switch (GET_CODE (cond))
      {
      case GE:
      case GEU:
	/* Jump becomes unconditional.  */
	return 1;

      case LT:
      case LTU:
	/* Jump becomes no-op.  */
	return -1;

      case LE:
      case LEU:
	PUT_CODE (cond, EQ);
	value = 2;
	break;

      case GT:
      case GTU:
	PUT_CODE (cond, NE);
	value = 2;
	break;

      default:
	break;
      }

  if (cc_status.flags & CC_NO_OVERFLOW)
    switch (GET_CODE (cond))
      {
      case GEU:
	/* Jump becomes unconditional.  */
	return 1;

      case LEU:
	PUT_CODE (cond, EQ);
	value = 2;
	break;

      case GTU:
	PUT_CODE (cond, NE);
	value = 2;
	break;

      case LTU:
	/* Jump becomes no-op.  */
	return -1;

      default:
	break;
      }

  if (cc_status.flags & (CC_Z_IN_NOT_N | CC_Z_IN_N))
    switch (GET_CODE (cond))
      {
      default:
	abort ();

      case NE:
	PUT_CODE (cond, cc_status.flags & CC_Z_IN_N ? GE : LT);
	value = 2;
	break;

      case EQ:
	PUT_CODE (cond, cc_status.flags & CC_Z_IN_N ? LT : GE);
	value = 2;
	break;
      }

  if (cc_status.flags & CC_NOT_SIGNED)
    /* The flags are valid if signed condition operators are converted
       to unsigned.  */
    switch (GET_CODE (cond))
      {
      case LE:
	PUT_CODE (cond, LEU);
	value = 2;
	break;

      case LT:
	PUT_CODE (cond, LTU);
	value = 2;
	break;

      case GT:
	PUT_CODE (cond, GTU);
	value = 2;
	break;

      case GE:
	PUT_CODE (cond, GEU);
	value = 2;
	break;

      default:
	break;
      }

  return value;
}
#endif

/* Report inconsistency between the assembler template and the operands.
   In an `asm', it's the user's fault; otherwise, the compiler's fault.  */

void
output_operand_lossage VPARAMS ((const char *msgid, ...))
{
  char *fmt_string;
  char *new_message;
  const char *pfx_str;
  VA_OPEN (ap, msgid);
  VA_FIXEDARG (ap, const char *, msgid);

  pfx_str = this_is_asm_operands ? _("invalid `asm': ") : "output_operand: ";
  asprintf (&fmt_string, "%s%s", pfx_str, _(msgid));
  vasprintf (&new_message, fmt_string, ap);

  if (this_is_asm_operands)
    error_for_asm (this_is_asm_operands, "%s", new_message);
  else
    internal_error ("%s", new_message);

  free (fmt_string);
  free (new_message);
  VA_CLOSE (ap);
}

/* Output of assembler code from a template, and its subroutines.  */

/* Annotate the assembly with a comment describing the pattern and
   alternative used.  */

static void
output_asm_name ()
{
  if (debug_insn)
    {
      int num = INSN_CODE (debug_insn);
      fprintf (asm_out_file, "\t%s %d\t%s",
	       ASM_COMMENT_START, INSN_UID (debug_insn),
	       insn_data[num].name);
      if (insn_data[num].n_alternatives > 1)
	fprintf (asm_out_file, "/%d", which_alternative + 1);
#ifdef HAVE_ATTR_length
      fprintf (asm_out_file, "\t[length = %d]",
	       get_attr_length (debug_insn));
#endif
      /* Clear this so only the first assembler insn
	 of any rtl insn will get the special comment for -dp.  */
      debug_insn = 0;
    }
}

/* If OP is a REG or MEM and we can find a MEM_EXPR corresponding to it
   or its address, return that expr .  Set *PADDRESSP to 1 if the expr
   corresponds to the address of the object and 0 if to the object.  */

static tree
get_mem_expr_from_op (op, paddressp)
     rtx op;
     int *paddressp;
{
  tree expr;
  int inner_addressp;

  *paddressp = 0;

  if (op == NULL)
    return 0;

  if (GET_CODE (op) == REG && ORIGINAL_REGNO (op) >= FIRST_PSEUDO_REGISTER)
    return REGNO_DECL (ORIGINAL_REGNO (op));
  else if (GET_CODE (op) != MEM)
    return 0;

  if (MEM_EXPR (op) != 0)
    return MEM_EXPR (op);

  /* Otherwise we have an address, so indicate it and look at the address.  */
  *paddressp = 1;
  op = XEXP (op, 0);

  /* First check if we have a decl for the address, then look at the right side
     if it is a PLUS.  Otherwise, strip off arithmetic and keep looking.
     But don't allow the address to itself be indirect.  */
  if ((expr = get_mem_expr_from_op (op, &inner_addressp)) && ! inner_addressp)
    return expr;
  else if (GET_CODE (op) == PLUS
	   && (expr = get_mem_expr_from_op (XEXP (op, 1), &inner_addressp)))
    return expr;

  while (GET_RTX_CLASS (GET_CODE (op)) == '1'
	 || GET_RTX_CLASS (GET_CODE (op)) == '2')
    op = XEXP (op, 0);

  expr = get_mem_expr_from_op (op, &inner_addressp);
  return inner_addressp ? 0 : expr;
}

/* Output operand names for assembler instructions.  OPERANDS is the
   operand vector, OPORDER is the order to write the operands, and NOPS
   is the number of operands to write.  */

static void
output_asm_operand_names (operands, oporder, nops)
     rtx *operands;
     int *oporder;
     int nops;
{
  int wrote = 0;
  int i;

  for (i = 0; i < nops; i++)
    {
      int addressp;
      tree expr = get_mem_expr_from_op (operands[oporder[i]], &addressp);

      if (expr)
	{
	  fprintf (asm_out_file, "%c%s %s",
		   wrote ? ',' : '\t', wrote ? "" : ASM_COMMENT_START,
		   addressp ? "*" : "");
	  print_mem_expr (asm_out_file, expr);
	  wrote = 1;
	}
    }
}

/* Output text from TEMPLATE to the assembler output file,
   obeying %-directions to substitute operands taken from
   the vector OPERANDS.

   %N (for N a digit) means print operand N in usual manner.
   %lN means require operand N to be a CODE_LABEL or LABEL_REF
      and print the label name with no punctuation.
   %cN means require operand N to be a constant
      and print the constant expression with no punctuation.
   %aN means expect operand N to be a memory address
      (not a memory reference!) and print a reference
      to that address.
   %nN means expect operand N to be a constant
      and print a constant expression for minus the value
      of the operand, with no other punctuation.  */

void
output_asm_insn (template, operands)
     const char *template;
     rtx *operands;
{
  const char *p;
  int c;
#ifdef ASSEMBLER_DIALECT
  int dialect = 0;
#endif
  int oporder[MAX_RECOG_OPERANDS];
  char opoutput[MAX_RECOG_OPERANDS];
  int ops = 0;

  /* An insn may return a null string template
     in a case where no assembler code is needed.  */
  if (*template == 0)
    return;

  memset (opoutput, 0, sizeof opoutput);
  p = template;
  putc ('\t', asm_out_file);

#ifdef ASM_OUTPUT_OPCODE
  ASM_OUTPUT_OPCODE (asm_out_file, p);
#endif

  while ((c = *p++))
    switch (c)
      {
      case '\n':
	if (flag_verbose_asm)
	  output_asm_operand_names (operands, oporder, ops);
	if (flag_print_asm_name)
	  output_asm_name ();

	ops = 0;
	memset (opoutput, 0, sizeof opoutput);

	putc (c, asm_out_file);
#ifdef ASM_OUTPUT_OPCODE
	while ((c = *p) == '\t')
	  {
	    putc (c, asm_out_file);
	    p++;
	  }
	ASM_OUTPUT_OPCODE (asm_out_file, p);
#endif
	break;

#ifdef ASSEMBLER_DIALECT
      case '{':
	{
	  int i;

	  if (dialect)
	    output_operand_lossage ("nested assembly dialect alternatives");
	  else
	    dialect = 1;

	  /* If we want the first dialect, do nothing.  Otherwise, skip
	     DIALECT_NUMBER of strings ending with '|'.  */
	  for (i = 0; i < dialect_number; i++)
	    {
	      while (*p && *p != '}' && *p++ != '|')
		;
	      if (*p == '}')
		break;
	      if (*p == '|')
		p++;
	    }

	  if (*p == '\0')
	    output_operand_lossage ("unterminated assembly dialect alternative");
	}
	break;

      case '|':
	if (dialect)
	  {
	    /* Skip to close brace.  */
	    do
	      {
		if (*p == '\0')
		  {
		    output_operand_lossage ("unterminated assembly dialect alternative");
		    break;
		  }
	      }
	    while (*p++ != '}');
	    dialect = 0;
	  }
	else
	  putc (c, asm_out_file);
	break;

      case '}':
	if (! dialect)
	  putc (c, asm_out_file);
	dialect = 0;
	break;
#endif

      case '%':
	/* %% outputs a single %.  */
	if (*p == '%')
	  {
	    p++;
	    putc (c, asm_out_file);
	  }
	/* %= outputs a number which is unique to each insn in the entire
	   compilation.  This is useful for making local labels that are
	   referred to more than once in a given insn.  */
	else if (*p == '=')
	  {
	    p++;
	    fprintf (asm_out_file, "%d", insn_counter);
	  }
	/* % followed by a letter and some digits
	   outputs an operand in a special way depending on the letter.
	   Letters `acln' are implemented directly.
	   Other letters are passed to `output_operand' so that
	   the PRINT_OPERAND macro can define them.  */
	else if (ISALPHA (*p))
	  {
	    int letter = *p++;
	    c = atoi (p);

	    if (! ISDIGIT (*p))
	      output_operand_lossage ("operand number missing after %%-letter");
	    else if (this_is_asm_operands
		     && (c < 0 || (unsigned int) c >= insn_noperands))
	      output_operand_lossage ("operand number out of range");
	    else if (letter == 'l')
	      output_asm_label (operands[c]);
	    else if (letter == 'a')
	      output_address (operands[c]);
	    else if (letter == 'c')
	      {
		if (CONSTANT_ADDRESS_P (operands[c]))
		  output_addr_const (asm_out_file, operands[c]);
		else
		  output_operand (operands[c], 'c');
	      }
	    else if (letter == 'n')
	      {
		if (GET_CODE (operands[c]) == CONST_INT)
		  fprintf (asm_out_file, HOST_WIDE_INT_PRINT_DEC,
			   - INTVAL (operands[c]));
		else
		  {
		    putc ('-', asm_out_file);
		    output_addr_const (asm_out_file, operands[c]);
		  }
	      }
	    else
	      output_operand (operands[c], letter);

	    if (!opoutput[c])
	      oporder[ops++] = c;
	    opoutput[c] = 1;

	    while (ISDIGIT (c = *p))
	      p++;
	  }
	/* % followed by a digit outputs an operand the default way.  */
	else if (ISDIGIT (*p))
	  {
	    c = atoi (p);
	    if (this_is_asm_operands
		&& (c < 0 || (unsigned int) c >= insn_noperands))
	      output_operand_lossage ("operand number out of range");
	    else
	      output_operand (operands[c], 0);

	    if (!opoutput[c])
	      oporder[ops++] = c;
	    opoutput[c] = 1;

	    while (ISDIGIT (c = *p))
	      p++;
	  }
	/* % followed by punctuation: output something for that
	   punctuation character alone, with no operand.
	   The PRINT_OPERAND macro decides what is actually done.  */
#ifdef PRINT_OPERAND_PUNCT_VALID_P
	else if (PRINT_OPERAND_PUNCT_VALID_P ((unsigned char) *p))
	  output_operand (NULL_RTX, *p++);
#endif
	else
	  output_operand_lossage ("invalid %%-code");
	break;

      default:
	putc (c, asm_out_file);
      }

  /* Write out the variable names for operands, if we know them.  */
  if (flag_verbose_asm)
    output_asm_operand_names (operands, oporder, ops);
  if (flag_print_asm_name)
    output_asm_name ();

  putc ('\n', asm_out_file);
}

/* Output a LABEL_REF, or a bare CODE_LABEL, as an assembler symbol.  */

void
output_asm_label (x)
     rtx x;
{
  char buf[256];

  if (GET_CODE (x) == LABEL_REF)
    x = XEXP (x, 0);
  if (GET_CODE (x) == CODE_LABEL
      || (GET_CODE (x) == NOTE
	  && NOTE_LINE_NUMBER (x) == NOTE_INSN_DELETED_LABEL))
    ASM_GENERATE_INTERNAL_LABEL (buf, "L", CODE_LABEL_NUMBER (x));
  else
    output_operand_lossage ("`%%l' operand isn't a label");

  assemble_name (asm_out_file, buf);
}

/* Print operand X using machine-dependent assembler syntax.
   The macro PRINT_OPERAND is defined just to control this function.
   CODE is a non-digit that preceded the operand-number in the % spec,
   such as 'z' if the spec was `%z3'.  CODE is 0 if there was no char
   between the % and the digits.
   When CODE is a non-letter, X is 0.

   The meanings of the letters are machine-dependent and controlled
   by PRINT_OPERAND.  */

static void
output_operand (x, code)
     rtx x;
     int code ATTRIBUTE_UNUSED;
{
  if (x && GET_CODE (x) == SUBREG)
    x = alter_subreg (&x);

  /* If X is a pseudo-register, abort now rather than writing trash to the
     assembler file.  */

  if (x && GET_CODE (x) == REG && REGNO (x) >= FIRST_PSEUDO_REGISTER)
    abort ();

  PRINT_OPERAND (asm_out_file, x, code);
}

/* Print a memory reference operand for address X
   using machine-dependent assembler syntax.
   The macro PRINT_OPERAND_ADDRESS exists just to control this function.  */

void
output_address (x)
     rtx x;
{
  walk_alter_subreg (&x);
  PRINT_OPERAND_ADDRESS (asm_out_file, x);
}

/* Print an integer constant expression in assembler syntax.
   Addition and subtraction are the only arithmetic
   that may appear in these expressions.  */

void
output_addr_const (file, x)
     FILE *file;
     rtx x;
{
  char buf[256];

 restart:
  switch (GET_CODE (x))
    {
    case PC:
      putc ('.', file);
      break;

    case SYMBOL_REF:
#ifdef ASM_OUTPUT_SYMBOL_REF
      ASM_OUTPUT_SYMBOL_REF (file, x);
#else
      assemble_name (file, XSTR (x, 0));
#endif
      break;

    case LABEL_REF:
      x = XEXP (x, 0);
      /* Fall through.  */
    case CODE_LABEL:
      ASM_GENERATE_INTERNAL_LABEL (buf, "L", CODE_LABEL_NUMBER (x));
#ifdef ASM_OUTPUT_LABEL_REF
      ASM_OUTPUT_LABEL_REF (file, buf);
#else
      assemble_name (file, buf);
#endif
      break;

    case CONST_INT:
      fprintf (file, HOST_WIDE_INT_PRINT_DEC, INTVAL (x));
      break;

    case CONST:
      /* This used to output parentheses around the expression,
	 but that does not work on the 386 (either ATT or BSD assembler).  */
      output_addr_const (file, XEXP (x, 0));
      break;

    case CONST_DOUBLE:
      if (GET_MODE (x) == VOIDmode)
	{
	  /* We can use %d if the number is one word and positive.  */
	  if (CONST_DOUBLE_HIGH (x))
	    fprintf (file, HOST_WIDE_INT_PRINT_DOUBLE_HEX,
		     CONST_DOUBLE_HIGH (x), CONST_DOUBLE_LOW (x));
	  else if (CONST_DOUBLE_LOW (x) < 0)
	    fprintf (file, HOST_WIDE_INT_PRINT_HEX, CONST_DOUBLE_LOW (x));
	  else
	    fprintf (file, HOST_WIDE_INT_PRINT_DEC, CONST_DOUBLE_LOW (x));
	}
      else
	/* We can't handle floating point constants;
	   PRINT_OPERAND must handle them.  */
	output_operand_lossage ("floating constant misused");
      break;

    case PLUS:
      /* Some assemblers need integer constants to appear last (eg masm).  */
      if (GET_CODE (XEXP (x, 0)) == CONST_INT)
	{
	  output_addr_const (file, XEXP (x, 1));
	  if (INTVAL (XEXP (x, 0)) >= 0)
	    fprintf (file, "+");
	  output_addr_const (file, XEXP (x, 0));
	}
      else
	{
	  output_addr_const (file, XEXP (x, 0));
	  if (GET_CODE (XEXP (x, 1)) != CONST_INT
	      || INTVAL (XEXP (x, 1)) >= 0)
	    fprintf (file, "+");
	  output_addr_const (file, XEXP (x, 1));
	}
      break;

    case MINUS:
      /* Avoid outputting things like x-x or x+5-x,
	 since some assemblers can't handle that.  */
      x = simplify_subtraction (x);
      if (GET_CODE (x) != MINUS)
	goto restart;

      output_addr_const (file, XEXP (x, 0));
      fprintf (file, "-");
      if ((GET_CODE (XEXP (x, 1)) == CONST_INT && INTVAL (XEXP (x, 1)) >= 0)
	  || GET_CODE (XEXP (x, 1)) == PC
	  || GET_CODE (XEXP (x, 1)) == SYMBOL_REF)
	output_addr_const (file, XEXP (x, 1));
      else
	{
	  fputs (targetm.asm_out.open_paren, file);
	  output_addr_const (file, XEXP (x, 1));
	  fputs (targetm.asm_out.close_paren, file);
	}
      break;

    case ZERO_EXTEND:
    case SIGN_EXTEND:
    case SUBREG:
      output_addr_const (file, XEXP (x, 0));
      break;

    default:
#ifdef OUTPUT_ADDR_CONST_EXTRA
      OUTPUT_ADDR_CONST_EXTRA (file, x, fail);
      break;

    fail:
#endif
      output_operand_lossage ("invalid expression as operand");
    }
}

/* A poor man's fprintf, with the added features of %I, %R, %L, and %U.
   %R prints the value of REGISTER_PREFIX.
   %L prints the value of LOCAL_LABEL_PREFIX.
   %U prints the value of USER_LABEL_PREFIX.
   %I prints the value of IMMEDIATE_PREFIX.
   %O runs ASM_OUTPUT_OPCODE to transform what follows in the string.
   Also supported are %d, %x, %s, %e, %f, %g and %%.

   We handle alternate assembler dialects here, just like output_asm_insn.  */

void
asm_fprintf VPARAMS ((FILE *file, const char *p, ...))
{
  char buf[10];
  char *q, c;

  VA_OPEN (argptr, p);
  VA_FIXEDARG (argptr, FILE *, file);
  VA_FIXEDARG (argptr, const char *, p);

  buf[0] = '%';

  while ((c = *p++))
    switch (c)
      {
#ifdef ASSEMBLER_DIALECT
      case '{':
	{
	  int i;

	  /* If we want the first dialect, do nothing.  Otherwise, skip
	     DIALECT_NUMBER of strings ending with '|'.  */
	  for (i = 0; i < dialect_number; i++)
	    {
	      while (*p && *p++ != '|')
		;

	      if (*p == '|')
		p++;
	    }
	}
	break;

      case '|':
	/* Skip to close brace.  */
	while (*p && *p++ != '}')
	  ;
	break;

      case '}':
	break;
#endif

      case '%':
	c = *p++;
	q = &buf[1];
	while (ISDIGIT (c) || c == '.')
	  {
	    *q++ = c;
	    c = *p++;
	  }
	switch (c)
	  {
	  case '%':
	    fprintf (file, "%%");
	    break;

	  case 'd':  case 'i':  case 'u':
	  case 'x':  case 'p':  case 'X':
	  case 'o':
	    *q++ = c;
	    *q = 0;
	    fprintf (file, buf, va_arg (argptr, int));
	    break;

	  case 'w':
	    /* This is a prefix to the 'd', 'i', 'u', 'x', 'p', and 'X' cases,
	       but we do not check for those cases.  It means that the value
	       is a HOST_WIDE_INT, which may be either `int' or `long'.  */

#if HOST_BITS_PER_WIDE_INT == HOST_BITS_PER_INT
#else
#if HOST_BITS_PER_WIDE_INT == HOST_BITS_PER_LONG
	    *q++ = 'l';
#else
	    *q++ = 'l';
	    *q++ = 'l';
#endif
#endif

	    *q++ = *p++;
	    *q = 0;
	    fprintf (file, buf, va_arg (argptr, HOST_WIDE_INT));
	    break;

	  case 'l':
	    *q++ = c;
	    *q++ = *p++;
	    *q = 0;
	    fprintf (file, buf, va_arg (argptr, long));
	    break;

	  case 'e':
	  case 'f':
	  case 'g':
	    *q++ = c;
	    *q = 0;
	    fprintf (file, buf, va_arg (argptr, double));
	    break;

	  case 's':
	    *q++ = c;
	    *q = 0;
	    fprintf (file, buf, va_arg (argptr, char *));
	    break;

	  case 'O':
#ifdef ASM_OUTPUT_OPCODE
	    ASM_OUTPUT_OPCODE (asm_out_file, p);
#endif
	    break;

	  case 'R':
#ifdef REGISTER_PREFIX
	    fprintf (file, "%s", REGISTER_PREFIX);
#endif
	    break;

	  case 'I':
#ifdef IMMEDIATE_PREFIX
	    fprintf (file, "%s", IMMEDIATE_PREFIX);
#endif
	    break;

	  case 'L':
#ifdef LOCAL_LABEL_PREFIX
	    fprintf (file, "%s", LOCAL_LABEL_PREFIX);
#endif
	    break;

	  case 'U':
	    fputs (user_label_prefix, file);
	    break;

#ifdef ASM_FPRINTF_EXTENSIONS
	    /* Upper case letters are reserved for general use by asm_fprintf
	       and so are not available to target specific code.  In order to
	       prevent the ASM_FPRINTF_EXTENSIONS macro from using them then,
	       they are defined here.  As they get turned into real extensions
	       to asm_fprintf they should be removed from this list.  */
	  case 'A': case 'B': case 'C': case 'D': case 'E':
	  case 'F': case 'G': case 'H': case 'J': case 'K':
	  case 'M': case 'N': case 'P': case 'Q': case 'S':
	  case 'T': case 'V': case 'W': case 'Y': case 'Z':
	    break;

	  ASM_FPRINTF_EXTENSIONS (file, argptr, p)
#endif
	  default:
	    abort ();
	  }
	break;

      default:
	fputc (c, file);
      }
  VA_CLOSE (argptr);
}

/* Split up a CONST_DOUBLE or integer constant rtx
   into two rtx's for single words,
   storing in *FIRST the word that comes first in memory in the target
   and in *SECOND the other.  */

void
split_double (value, first, second)
     rtx value;
     rtx *first, *second;
{
  if (GET_CODE (value) == CONST_INT)
    {
      if (HOST_BITS_PER_WIDE_INT >= (2 * BITS_PER_WORD))
	{
	  /* In this case the CONST_INT holds both target words.
	     Extract the bits from it into two word-sized pieces.
	     Sign extend each half to HOST_WIDE_INT.  */
	  unsigned HOST_WIDE_INT low, high;
	  unsigned HOST_WIDE_INT mask, sign_bit, sign_extend;

	  /* Set sign_bit to the most significant bit of a word.  */
	  sign_bit = 1;
	  sign_bit <<= BITS_PER_WORD - 1;

	  /* Set mask so that all bits of the word are set.  We could
	     have used 1 << BITS_PER_WORD instead of basing the
	     calculation on sign_bit.  However, on machines where
	     HOST_BITS_PER_WIDE_INT == BITS_PER_WORD, it could cause a
	     compiler warning, even though the code would never be
	     executed.  */
	  mask = sign_bit << 1;
	  mask--;

	  /* Set sign_extend as any remaining bits.  */
	  sign_extend = ~mask;

	  /* Pick the lower word and sign-extend it.  */
	  low = INTVAL (value);
	  low &= mask;
	  if (low & sign_bit)
	    low |= sign_extend;

	  /* Pick the higher word, shifted to the least significant
	     bits, and sign-extend it.  */
	  high = INTVAL (value);
	  high >>= BITS_PER_WORD - 1;
	  high >>= 1;
	  high &= mask;
	  if (high & sign_bit)
	    high |= sign_extend;

	  /* Store the words in the target machine order.  */
	  if (WORDS_BIG_ENDIAN)
	    {
	      *first = GEN_INT (high);
	      *second = GEN_INT (low);
	    }
	  else
	    {
	      *first = GEN_INT (low);
	      *second = GEN_INT (high);
	    }
	}
      else
	{
	  /* The rule for using CONST_INT for a wider mode
	     is that we regard the value as signed.
	     So sign-extend it.  */
	  rtx high = (INTVAL (value) < 0 ? constm1_rtx : const0_rtx);
	  if (WORDS_BIG_ENDIAN)
	    {
	      *first = high;
	      *second = value;
	    }
	  else
	    {
	      *first = value;
	      *second = high;
	    }
	}
    }
  else if (GET_CODE (value) != CONST_DOUBLE)
    {
      if (WORDS_BIG_ENDIAN)
	{
	  *first = const0_rtx;
	  *second = value;
	}
      else
	{
	  *first = value;
	  *second = const0_rtx;
	}
    }
  else if (GET_MODE (value) == VOIDmode
	   /* This is the old way we did CONST_DOUBLE integers.  */
	   || GET_MODE_CLASS (GET_MODE (value)) == MODE_INT)
    {
      /* In an integer, the words are defined as most and least significant.
	 So order them by the target's convention.  */
      if (WORDS_BIG_ENDIAN)
	{
	  *first = GEN_INT (CONST_DOUBLE_HIGH (value));
	  *second = GEN_INT (CONST_DOUBLE_LOW (value));
	}
      else
	{
	  *first = GEN_INT (CONST_DOUBLE_LOW (value));
	  *second = GEN_INT (CONST_DOUBLE_HIGH (value));
	}
    }
  else
    {
      REAL_VALUE_TYPE r;
      long l[2];
      REAL_VALUE_FROM_CONST_DOUBLE (r, value);

      /* Note, this converts the REAL_VALUE_TYPE to the target's
	 format, splits up the floating point double and outputs
	 exactly 32 bits of it into each of l[0] and l[1] --
	 not necessarily BITS_PER_WORD bits.  */
      REAL_VALUE_TO_TARGET_DOUBLE (r, l);

      /* If 32 bits is an entire word for the target, but not for the host,
	 then sign-extend on the host so that the number will look the same
	 way on the host that it would on the target.  See for instance
	 simplify_unary_operation.  The #if is needed to avoid compiler
	 warnings.  */

#if HOST_BITS_PER_LONG > 32
      if (BITS_PER_WORD < HOST_BITS_PER_LONG && BITS_PER_WORD == 32)
	{
	  if (l[0] & ((long) 1 << 31))
	    l[0] |= ((long) (-1) << 32);
	  if (l[1] & ((long) 1 << 31))
	    l[1] |= ((long) (-1) << 32);
	}
#endif

      *first = GEN_INT ((HOST_WIDE_INT) l[0]);
      *second = GEN_INT ((HOST_WIDE_INT) l[1]);
    }
}

/* Return nonzero if this function has no function calls.  */

int
leaf_function_p ()
{
  rtx insn;
  rtx link;

  if (current_function_profile || profile_arc_flag)
    return 0;

  for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
    {
      if (GET_CODE (insn) == CALL_INSN
	  && ! SIBLING_CALL_P (insn))
	return 0;
      if (GET_CODE (insn) == INSN
	  && GET_CODE (PATTERN (insn)) == SEQUENCE
	  && GET_CODE (XVECEXP (PATTERN (insn), 0, 0)) == CALL_INSN
	  && ! SIBLING_CALL_P (XVECEXP (PATTERN (insn), 0, 0)))
	return 0;
    }
  for (link = current_function_epilogue_delay_list;
       link;
       link = XEXP (link, 1))
    {
      insn = XEXP (link, 0);

      if (GET_CODE (insn) == CALL_INSN
	  && ! SIBLING_CALL_P (insn))
	return 0;
      if (GET_CODE (insn) == INSN
	  && GET_CODE (PATTERN (insn)) == SEQUENCE
	  && GET_CODE (XVECEXP (PATTERN (insn), 0, 0)) == CALL_INSN
	  && ! SIBLING_CALL_P (XVECEXP (PATTERN (insn), 0, 0)))
	return 0;
    }

  return 1;
}

/* Return 1 if branch is a forward branch.
   Uses insn_shuid array, so it works only in the final pass.  May be used by
   output templates to customary add branch prediction hints.
 */
int
final_forward_branch_p (insn)
     rtx insn;
{
  int insn_id, label_id;
  if (!uid_shuid)
    abort ();
  insn_id = INSN_SHUID (insn);
  label_id = INSN_SHUID (JUMP_LABEL (insn));
  /* We've hit some insns that does not have id information available.  */
  if (!insn_id || !label_id)
    abort ();
  return insn_id < label_id;
}

/* On some machines, a function with no call insns
   can run faster if it doesn't create its own register window.
   When output, the leaf function should use only the "output"
   registers.  Ordinarily, the function would be compiled to use
   the "input" registers to find its arguments; it is a candidate
   for leaf treatment if it uses only the "input" registers.
   Leaf function treatment means renumbering so the function
   uses the "output" registers instead.  */

#ifdef LEAF_REGISTERS

/* Return 1 if this function uses only the registers that can be
   safely renumbered.  */

int
only_leaf_regs_used ()
{
  int i;
  char *permitted_reg_in_leaf_functions = LEAF_REGISTERS;

  for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
    if ((regs_ever_live[i] || global_regs[i])
	&& ! permitted_reg_in_leaf_functions[i])
      return 0;

  if (current_function_uses_pic_offset_table
      && pic_offset_table_rtx != 0
      && GET_CODE (pic_offset_table_rtx) == REG
      && ! permitted_reg_in_leaf_functions[REGNO (pic_offset_table_rtx)])
    return 0;

  return 1;
}

/* Scan all instructions and renumber all registers into those
   available in leaf functions.  */

static void
leaf_renumber_regs (first)
     rtx first;
{
  rtx insn;

  /* Renumber only the actual patterns.
     The reg-notes can contain frame pointer refs,
     and renumbering them could crash, and should not be needed.  */
  for (insn = first; insn; insn = NEXT_INSN (insn))
    if (INSN_P (insn))
      leaf_renumber_regs_insn (PATTERN (insn));
  for (insn = current_function_epilogue_delay_list;
       insn;
       insn = XEXP (insn, 1))
    if (INSN_P (XEXP (insn, 0)))
      leaf_renumber_regs_insn (PATTERN (XEXP (insn, 0)));
}

/* Scan IN_RTX and its subexpressions, and renumber all regs into those
   available in leaf functions.  */

void
leaf_renumber_regs_insn (in_rtx)
     rtx in_rtx;
{
  int i, j;
  const char *format_ptr;

  if (in_rtx == 0)
    return;

  /* Renumber all input-registers into output-registers.
     renumbered_regs would be 1 for an output-register;
     they  */

  if (GET_CODE (in_rtx) == REG)
    {
      int newreg;

      /* Don't renumber the same reg twice.  */
      if (in_rtx->used)
	return;

      newreg = REGNO (in_rtx);
      /* Don't try to renumber pseudo regs.  It is possible for a pseudo reg
	 to reach here as part of a REG_NOTE.  */
      if (newreg >= FIRST_PSEUDO_REGISTER)
	{
	  in_rtx->used = 1;
	  return;
	}
      newreg = LEAF_REG_REMAP (newreg);
      if (newreg < 0)
	abort ();
      regs_ever_live[REGNO (in_rtx)] = 0;
      regs_ever_live[newreg] = 1;
      REGNO (in_rtx) = newreg;
      in_rtx->used = 1;
    }

  if (INSN_P (in_rtx))
    {
      /* Inside a SEQUENCE, we find insns.
	 Renumber just the patterns of these insns,
	 just as we do for the top-level insns.  */
      leaf_renumber_regs_insn (PATTERN (in_rtx));
      return;
    }

  format_ptr = GET_RTX_FORMAT (GET_CODE (in_rtx));

  for (i = 0; i < GET_RTX_LENGTH (GET_CODE (in_rtx)); i++)
    switch (*format_ptr++)
      {
      case 'e':
	leaf_renumber_regs_insn (XEXP (in_rtx, i));
	break;

      case 'E':
	if (NULL != XVEC (in_rtx, i))
	  {
	    for (j = 0; j < XVECLEN (in_rtx, i); j++)
	      leaf_renumber_regs_insn (XVECEXP (in_rtx, i, j));
	  }
	break;

      case 'S':
      case 's':
      case '0':
      case 'i':
      case 'w':
      case 'n':
      case 'u':
	break;

      default:
	abort ();
      }
}
#endif

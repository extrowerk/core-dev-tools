/* Tracing functionality for remote targets in custom GDB protocol

   Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006,
   2007, 2008, 2009, 2010 Free Software Foundation, Inc.

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
#include "arch-utils.h"
#include "symtab.h"
#include "frame.h"
#include "gdbtypes.h"
#include "expression.h"
#include "gdbcmd.h"
#include "value.h"
#include "target.h"
#include "language.h"
#include "gdb_string.h"
#include "inferior.h"
#include "breakpoint.h"
#include "tracepoint.h"
#include "linespec.h"
#include "regcache.h"
#include "completer.h"
#include "block.h"
#include "dictionary.h"
#include "observer.h"
#include "user-regs.h"
#include "valprint.h"
#include "gdbcore.h"
#include "objfiles.h"
#include "filenames.h"
#include "gdbthread.h"
#include "stack.h"
#include "gdbcore.h"
#include "remote.h"

#include "ax.h"
#include "ax-gdb.h"

/* readline include files */
#include "readline/readline.h"
#include "readline/history.h"

/* readline defines this.  */
#undef savestring

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif

extern int hex2bin (const char *hex, gdb_byte *bin, int count);
extern int bin2hex (const gdb_byte *bin, char *hex, int count);

extern void stop_tracing ();

/* Maximum length of an agent aexpression.
   This accounts for the fact that packets are limited to 400 bytes
   (which includes everything -- including the checksum), and assumes
   the worst case of maximum length for each of the pieces of a
   continuation packet.

   NOTE: expressions get mem2hex'ed otherwise this would be twice as
   large.  (400 - 31)/2 == 184 */
#define MAX_AGENT_EXPR_LEN	184

/* A hook used to notify the UI of tracepoint operations.  */

void (*deprecated_trace_find_hook) (char *arg, int from_tty);
void (*deprecated_trace_start_stop_hook) (int start, int from_tty);

extern void (*deprecated_readline_begin_hook) (char *, ...);
extern char *(*deprecated_readline_hook) (char *);
extern void (*deprecated_readline_end_hook) (void);

/* GDB commands implemented in other modules:
 */  

extern void output_command (char *, int);

/* 
   Tracepoint.c:

   This module defines the following debugger commands:
   trace            : set a tracepoint on a function, line, or address.
   info trace       : list all debugger-defined tracepoints.
   delete trace     : delete one or more tracepoints.
   enable trace     : enable one or more tracepoints.
   disable trace    : disable one or more tracepoints.
   actions          : specify actions to be taken at a tracepoint.
   passcount        : specify a pass count for a tracepoint.
   tstart           : start a trace experiment.
   tstop            : stop a trace experiment.
   tstatus          : query the status of a trace experiment.
   tfind            : find a trace frame in the trace buffer.
   tdump            : print everything collected at the current tracepoint.
   save-tracepoints : write tracepoint setup into a file.

   This module defines the following user-visible debugger variables:
   $trace_frame : sequence number of trace frame currently being debugged.
   $trace_line  : source line of trace frame currently being debugged.
   $trace_file  : source file of trace frame currently being debugged.
   $tracepoint  : tracepoint number of trace frame currently being debugged.
 */


/* ======= Important global variables: ======= */

/* The list of all trace state variables.  We don't retain pointers to
   any of these for any reason - API is by name or number only - so it
   works to have a vector of objects.  */

typedef struct trace_state_variable tsv_s;
DEF_VEC_O(tsv_s);

static VEC(tsv_s) *tvariables;

/* The next integer to assign to a variable.  */

static int next_tsv_number = 1;

/* Number of last traceframe collected.  */
static int traceframe_number;

/* Tracepoint for last traceframe collected.  */
static int tracepoint_number;

/* Symbol for function for last traceframe collected */
static struct symbol *traceframe_fun;

/* Symtab and line for last traceframe collected */
static struct symtab_and_line traceframe_sal;

/* Tracing command lists */
static struct cmd_list_element *tfindlist;

/* List of expressions to collect by default at each tracepoint hit.  */
char *default_collect = "";

static int disconnected_tracing;

/* This variable controls whether we ask the target for a linear or
   circular trace buffer.  */

static int circular_trace_buffer;

/* ======= Important command functions: ======= */
static void trace_actions_command (char *, int);
static void trace_start_command (char *, int);
static void trace_stop_command (char *, int);
static void trace_status_command (char *, int);
static void trace_find_command (char *, int);
static void trace_find_pc_command (char *, int);
static void trace_find_tracepoint_command (char *, int);
static void trace_find_line_command (char *, int);
static void trace_find_range_command (char *, int);
static void trace_find_outside_command (char *, int);
static void trace_dump_command (char *, int);

/* support routines */

struct collection_list;
static void add_aexpr (struct collection_list *, struct agent_expr *);
static char *mem2hex (gdb_byte *, char *, int);
static void add_register (struct collection_list *collection,
			  unsigned int regno);

extern void send_disconnected_tracing_value (int value);

static void free_uploaded_tps (struct uploaded_tp **utpp);
static void free_uploaded_tsvs (struct uploaded_tsv **utsvp);


extern void _initialize_tracepoint (void);

static struct trace_status trace_status;

char *stop_reason_names[] = {
  "tunknown",
  "tnotrun",
  "tstop",
  "tfull",
  "tdisconnected",
  "tpasscount",
  "terror"
};

struct trace_status *
current_trace_status ()
{
  return &trace_status;
}

/* Set traceframe number to NUM.  */
static void
set_traceframe_num (int num)
{
  traceframe_number = num;
  set_internalvar_integer (lookup_internalvar ("trace_frame"), num);
}

/* Set tracepoint number to NUM.  */
static void
set_tracepoint_num (int num)
{
  tracepoint_number = num;
  set_internalvar_integer (lookup_internalvar ("tracepoint"), num);
}

/* Set externally visible debug variables for querying/printing
   the traceframe context (line, function, file) */

static void
set_traceframe_context (struct frame_info *trace_frame)
{
  CORE_ADDR trace_pc;

  if (trace_frame == NULL)		/* Cease debugging any trace buffers.  */
    {
      traceframe_fun = 0;
      traceframe_sal.pc = traceframe_sal.line = 0;
      traceframe_sal.symtab = NULL;
      clear_internalvar (lookup_internalvar ("trace_func"));
      clear_internalvar (lookup_internalvar ("trace_file"));
      set_internalvar_integer (lookup_internalvar ("trace_line"), -1);
      return;
    }

  /* Save as globals for internal use.  */
  trace_pc = get_frame_pc (trace_frame);
  traceframe_sal = find_pc_line (trace_pc, 0);
  traceframe_fun = find_pc_function (trace_pc);

  /* Save linenumber as "$trace_line", a debugger variable visible to
     users.  */
  set_internalvar_integer (lookup_internalvar ("trace_line"),
			   traceframe_sal.line);

  /* Save func name as "$trace_func", a debugger variable visible to
     users.  */
  if (traceframe_fun == NULL
      || SYMBOL_LINKAGE_NAME (traceframe_fun) == NULL)
    clear_internalvar (lookup_internalvar ("trace_func"));
  else
    set_internalvar_string (lookup_internalvar ("trace_func"),
			    SYMBOL_LINKAGE_NAME (traceframe_fun));

  /* Save file name as "$trace_file", a debugger variable visible to
     users.  */
  if (traceframe_sal.symtab == NULL
      || traceframe_sal.symtab->filename == NULL)
    clear_internalvar (lookup_internalvar ("trace_file"));
  else
    set_internalvar_string (lookup_internalvar ("trace_file"),
			    traceframe_sal.symtab->filename);
}

/* Create a new trace state variable with the given name.  */

struct trace_state_variable *
create_trace_state_variable (const char *name)
{
  struct trace_state_variable tsv;

  memset (&tsv, 0, sizeof (tsv));
  tsv.name = xstrdup (name);
  tsv.number = next_tsv_number++;
  return VEC_safe_push (tsv_s, tvariables, &tsv);
}

/* Look for a trace state variable of the given name.  */

struct trace_state_variable *
find_trace_state_variable (const char *name)
{
  struct trace_state_variable *tsv;
  int ix;

  for (ix = 0; VEC_iterate (tsv_s, tvariables, ix, tsv); ++ix)
    if (strcmp (name, tsv->name) == 0)
      return tsv;

  return NULL;
}

void
delete_trace_state_variable (const char *name)
{
  struct trace_state_variable *tsv;
  int ix;

  for (ix = 0; VEC_iterate (tsv_s, tvariables, ix, tsv); ++ix)
    if (strcmp (name, tsv->name) == 0)
      {
	xfree ((void *)tsv->name);
	VEC_unordered_remove (tsv_s, tvariables, ix);
	return;
      }

  warning (_("No trace variable named \"$%s\", not deleting"), name);
}

/* The 'tvariable' command collects a name and optional expression to
   evaluate into an initial value.  */

void
trace_variable_command (char *args, int from_tty)
{
  struct expression *expr;
  struct cleanup *old_chain;
  struct internalvar *intvar = NULL;
  LONGEST initval = 0;
  struct trace_state_variable *tsv;

  if (!args || !*args)
    error_no_arg (_("trace state variable name"));

  /* All the possible valid arguments are expressions.  */
  expr = parse_expression (args);
  old_chain = make_cleanup (free_current_contents, &expr);

  if (expr->nelts == 0)
    error (_("No expression?"));

  /* Only allow two syntaxes; "$name" and "$name=value".  */
  if (expr->elts[0].opcode == OP_INTERNALVAR)
    {
      intvar = expr->elts[1].internalvar;
    }
  else if (expr->elts[0].opcode == BINOP_ASSIGN
	   && expr->elts[1].opcode == OP_INTERNALVAR)
    {
      intvar = expr->elts[2].internalvar;
      initval = value_as_long (evaluate_subexpression_type (expr, 4));
    }
  else
    error (_("Syntax must be $NAME [ = EXPR ]"));

  if (!intvar)
    error (_("No name given"));

  if (strlen (internalvar_name (intvar)) <= 0)
    error (_("Must supply a non-empty variable name"));

  /* If the variable already exists, just change its initial value.  */
  tsv = find_trace_state_variable (internalvar_name (intvar));
  if (tsv)
    {
      tsv->initial_value = initval;
      printf_filtered (_("Trace state variable $%s now has initial value %s.\n"),
		       tsv->name, plongest (tsv->initial_value));
      do_cleanups (old_chain);
      return;
    }

  /* Create a new variable.  */
  tsv = create_trace_state_variable (internalvar_name (intvar));
  tsv->initial_value = initval;

  printf_filtered (_("Trace state variable $%s created, with initial value %s.\n"),
		   tsv->name, plongest (tsv->initial_value));

  do_cleanups (old_chain);
}

void
delete_trace_variable_command (char *args, int from_tty)
{
  int ix;
  char **argv;
  struct cleanup *back_to;

  if (args == NULL)
    {
      if (query (_("Delete all trace state variables? ")))
	VEC_free (tsv_s, tvariables);
      dont_repeat ();
      return;
    }

  argv = gdb_buildargv (args);
  back_to = make_cleanup_freeargv (argv);

  for (ix = 0; argv[ix] != NULL; ix++)
    {
      if (*argv[ix] == '$')
	delete_trace_state_variable (argv[ix] + 1);
      else
	warning (_("Name \"%s\" not prefixed with '$', ignoring"), argv[ix]);
    }

  do_cleanups (back_to);

  dont_repeat ();
}

void
tvariables_info_1 (void)
{
  struct trace_state_variable *tsv;
  int ix;
  int count = 0;
  struct cleanup *back_to;

  if (VEC_length (tsv_s, tvariables) == 0 && !ui_out_is_mi_like_p (uiout))
    {
      printf_filtered (_("No trace state variables.\n"));
      return;
    }

  /* Try to acquire values from the target.  */
  for (ix = 0; VEC_iterate (tsv_s, tvariables, ix, tsv); ++ix, ++count)
    tsv->value_known = target_get_trace_state_variable_value (tsv->number,
							      &(tsv->value));

  back_to = make_cleanup_ui_out_table_begin_end (uiout, 3,
                                                 count, "trace-variables");
  ui_out_table_header (uiout, 15, ui_left, "name", "Name");
  ui_out_table_header (uiout, 11, ui_left, "initial", "Initial");
  ui_out_table_header (uiout, 11, ui_left, "current", "Current");

  ui_out_table_body (uiout);

  for (ix = 0; VEC_iterate (tsv_s, tvariables, ix, tsv); ++ix)
    {
      struct cleanup *back_to2;
      char *c;
      char *name;

      back_to2 = make_cleanup_ui_out_tuple_begin_end (uiout, "variable");

      name = concat ("$", tsv->name, (char *) NULL);
      make_cleanup (xfree, name);
      ui_out_field_string (uiout, "name", name);
      ui_out_field_string (uiout, "initial", plongest (tsv->initial_value));

      if (tsv->value_known)
        c = plongest (tsv->value);
      else if (ui_out_is_mi_like_p (uiout))
        /* For MI, we prefer not to use magic string constants, but rather
           omit the field completely.  The difference between unknown and
           undefined does not seem important enough to represent.  */
        c = NULL;
      else if (current_trace_status ()->running || traceframe_number >= 0)
	/* The value is/was defined, but we don't have it.  */
        c = "<unknown>";
      else
	/* It is not meaningful to ask about the value.  */
        c = "<undefined>";
      if (c)
        ui_out_field_string (uiout, "current", c);
      ui_out_text (uiout, "\n");

      do_cleanups (back_to2);
    }

  do_cleanups (back_to);
}

/* List all the trace state variables.  */

static void
tvariables_info (char *args, int from_tty)
{
  tvariables_info_1 ();
}

/* Stash definitions of tsvs into the given file.  */

void
save_trace_state_variables (struct ui_file *fp)
{
  struct trace_state_variable *tsv;
  int ix;

  for (ix = 0; VEC_iterate (tsv_s, tvariables, ix, tsv); ++ix)
    {
      fprintf_unfiltered (fp, "tvariable $%s", tsv->name);
      if (tsv->initial_value)
	fprintf_unfiltered (fp, " = %s", plongest (tsv->initial_value));
      fprintf_unfiltered (fp, "\n");
    }
}

/* ACTIONS functions: */

/* The three functions:
   collect_pseudocommand, 
   while_stepping_pseudocommand, and 
   end_actions_pseudocommand
   are placeholders for "commands" that are actually ONLY to be used
   within a tracepoint action list.  If the actual function is ever called,
   it means that somebody issued the "command" at the top level,
   which is always an error.  */

void
end_actions_pseudocommand (char *args, int from_tty)
{
  error (_("This command cannot be used at the top level."));
}

void
while_stepping_pseudocommand (char *args, int from_tty)
{
  error (_("This command can only be used in a tracepoint actions list."));
}

static void
collect_pseudocommand (char *args, int from_tty)
{
  error (_("This command can only be used in a tracepoint actions list."));
}

static void
teval_pseudocommand (char *args, int from_tty)
{
  error (_("This command can only be used in a tracepoint actions list."));
}

/* Enter a list of actions for a tracepoint.  */
static void
trace_actions_command (char *args, int from_tty)
{
  struct breakpoint *t;
  struct command_line *l;

  t = get_tracepoint_by_number (&args, 0, 1);
  if (t)
    {
      char *tmpbuf =
	xstrprintf ("Enter actions for tracepoint %d, one per line.",
		    t->number);
      struct cleanup *cleanups = make_cleanup (xfree, tmpbuf);

      l = read_command_lines (tmpbuf, from_tty, 1, check_tracepoint_command, t);
      do_cleanups (cleanups);
      breakpoint_set_commands (t, l);
    }
  /* else just return */
}

/* Report the results of checking the agent expression, as errors or
   internal errors.  */

static void
report_agent_reqs_errors (struct agent_expr *aexpr)
{
  /* All of the "flaws" are serious bytecode generation issues that
     should never occur.  */
  if (aexpr->flaw != agent_flaw_none)
    internal_error (__FILE__, __LINE__, _("expression is malformed"));

  /* If analysis shows a stack underflow, GDB must have done something
     badly wrong in its bytecode generation.  */
  if (aexpr->min_height < 0)
    internal_error (__FILE__, __LINE__,
		    _("expression has min height < 0"));

  /* Issue this error if the stack is predicted to get too deep.  The
     limit is rather arbitrary; a better scheme might be for the
     target to report how much stack it will have available.  The
     depth roughly corresponds to parenthesization, so a limit of 20
     amounts to 20 levels of expression nesting, which is actually
     a pretty big hairy expression.  */
  if (aexpr->max_height > 20)
    error (_("Expression is too complicated."));
}

/* worker function */
void
validate_actionline (char **line, struct breakpoint *t)
{
  struct cmd_list_element *c;
  struct expression *exp = NULL;
  struct cleanup *old_chain = NULL;
  char *p, *tmp_p;
  struct bp_location *loc;
  struct agent_expr *aexpr;

  /* if EOF is typed, *line is NULL */
  if (*line == NULL)
    return;

  for (p = *line; isspace ((int) *p);)
    p++;

  /* Symbol lookup etc.  */
  if (*p == '\0')	/* empty line: just prompt for another line.  */
    return;

  if (*p == '#')		/* comment line */
    return;

  c = lookup_cmd (&p, cmdlist, "", -1, 1);
  if (c == 0)
    error (_("`%s' is not a tracepoint action, or is ambiguous."), p);

  if (cmd_cfunc_eq (c, collect_pseudocommand))
    {
      do
	{			/* repeat over a comma-separated list */
	  QUIT;			/* allow user to bail out with ^C */
	  while (isspace ((int) *p))
	    p++;

	  if (*p == '$')	/* look for special pseudo-symbols */
	    {
	      if ((0 == strncasecmp ("reg", p + 1, 3)) ||
		  (0 == strncasecmp ("arg", p + 1, 3)) ||
		  (0 == strncasecmp ("loc", p + 1, 3)))
		{
		  p = strchr (p, ',');
		  continue;
		}
	      /* else fall thru, treat p as an expression and parse it!  */
	    }
	  tmp_p = p;
	  for (loc = t->loc; loc; loc = loc->next)
	    {
	      p = tmp_p;
	      exp = parse_exp_1 (&p, block_for_pc (loc->address), 1);
	      old_chain = make_cleanup (free_current_contents, &exp);

	      if (exp->elts[0].opcode == OP_VAR_VALUE)
		{
		  if (SYMBOL_CLASS (exp->elts[2].symbol) == LOC_CONST)
		    {
		      error (_("constant `%s' (value %ld) will not be collected."),
			     SYMBOL_PRINT_NAME (exp->elts[2].symbol),
			     SYMBOL_VALUE (exp->elts[2].symbol));
		    }
		  else if (SYMBOL_CLASS (exp->elts[2].symbol) == LOC_OPTIMIZED_OUT)
		    {
		      error (_("`%s' is optimized away and cannot be collected."),
			     SYMBOL_PRINT_NAME (exp->elts[2].symbol));
		    }
		}

	      /* We have something to collect, make sure that the expr to
		 bytecode translator can handle it and that it's not too
		 long.  */
	      aexpr = gen_trace_for_expr (loc->address, exp);
	      make_cleanup_free_agent_expr (aexpr);

	      if (aexpr->len > MAX_AGENT_EXPR_LEN)
		error (_("Expression is too complicated."));

	      ax_reqs (aexpr);

	      report_agent_reqs_errors (aexpr);

	      do_cleanups (old_chain);
	    }
	}
      while (p && *p++ == ',');
    }

  else if (cmd_cfunc_eq (c, teval_pseudocommand))
    {
      do
	{			/* repeat over a comma-separated list */
	  QUIT;			/* allow user to bail out with ^C */
	  while (isspace ((int) *p))
	    p++;

	  tmp_p = p;
	  for (loc = t->loc; loc; loc = loc->next)
	    {
	      p = tmp_p;
	      /* Only expressions are allowed for this action.  */
	      exp = parse_exp_1 (&p, block_for_pc (loc->address), 1);
	      old_chain = make_cleanup (free_current_contents, &exp);

	      /* We have something to evaluate, make sure that the expr to
		 bytecode translator can handle it and that it's not too
		 long.  */
	      aexpr = gen_eval_for_expr (loc->address, exp);
	      make_cleanup_free_agent_expr (aexpr);

	      if (aexpr->len > MAX_AGENT_EXPR_LEN)
		error (_("Expression is too complicated."));

	      ax_reqs (aexpr);
	      report_agent_reqs_errors (aexpr);

	      do_cleanups (old_chain);
	    }
	}
      while (p && *p++ == ',');
    }

  else if (cmd_cfunc_eq (c, while_stepping_pseudocommand))
    {
      char *steparg;		/* in case warning is necessary */

      while (isspace ((int) *p))
	p++;
      steparg = p;

      if (*p == '\0' || (t->step_count = strtol (p, &p, 0)) == 0)
	error (_("while-stepping step count `%s' is malformed."), *line);
    }

  else if (cmd_cfunc_eq (c, end_actions_pseudocommand))
    ;

  else
    error (_("`%s' is not a supported tracepoint action."), *line);
}

enum {
  memrange_absolute = -1
};

struct memrange
{
  int type;		/* memrange_absolute for absolute memory range,
                           else basereg number */
  bfd_signed_vma start;
  bfd_signed_vma end;
};

struct collection_list
  {
    unsigned char regs_mask[32];	/* room for up to 256 regs */
    long listsize;
    long next_memrange;
    struct memrange *list;
    long aexpr_listsize;	/* size of array pointed to by expr_list elt */
    long next_aexpr_elt;
    struct agent_expr **aexpr_list;

  }
tracepoint_list, stepping_list;

/* MEMRANGE functions: */

static int memrange_cmp (const void *, const void *);

/* compare memranges for qsort */
static int
memrange_cmp (const void *va, const void *vb)
{
  const struct memrange *a = va, *b = vb;

  if (a->type < b->type)
    return -1;
  if (a->type > b->type)
    return 1;
  if (a->type == memrange_absolute)
    {
      if ((bfd_vma) a->start < (bfd_vma) b->start)
	return -1;
      if ((bfd_vma) a->start > (bfd_vma) b->start)
	return 1;
    }
  else
    {
      if (a->start < b->start)
	return -1;
      if (a->start > b->start)
	return 1;
    }
  return 0;
}

/* Sort the memrange list using qsort, and merge adjacent memranges.  */
static void
memrange_sortmerge (struct collection_list *memranges)
{
  int a, b;

  qsort (memranges->list, memranges->next_memrange,
	 sizeof (struct memrange), memrange_cmp);
  if (memranges->next_memrange > 0)
    {
      for (a = 0, b = 1; b < memranges->next_memrange; b++)
	{
	  if (memranges->list[a].type == memranges->list[b].type &&
	      memranges->list[b].start - memranges->list[a].end <=
	      MAX_REGISTER_SIZE)
	    {
	      /* memrange b starts before memrange a ends; merge them.  */
	      if (memranges->list[b].end > memranges->list[a].end)
		memranges->list[a].end = memranges->list[b].end;
	      continue;		/* next b, same a */
	    }
	  a++;			/* next a */
	  if (a != b)
	    memcpy (&memranges->list[a], &memranges->list[b],
		    sizeof (struct memrange));
	}
      memranges->next_memrange = a + 1;
    }
}

/* Add a register to a collection list.  */
static void
add_register (struct collection_list *collection, unsigned int regno)
{
  if (info_verbose)
    printf_filtered ("collect register %d\n", regno);
  if (regno >= (8 * sizeof (collection->regs_mask)))
    error (_("Internal: register number %d too large for tracepoint"),
	   regno);
  collection->regs_mask[regno / 8] |= 1 << (regno % 8);
}

/* Add a memrange to a collection list */
static void
add_memrange (struct collection_list *memranges, 
	      int type, bfd_signed_vma base,
	      unsigned long len)
{
  if (info_verbose)
    {
      printf_filtered ("(%d,", type);
      printf_vma (base);
      printf_filtered (",%ld)\n", len);
    }

  /* type: memrange_absolute == memory, other n == basereg */
  memranges->list[memranges->next_memrange].type = type;
  /* base: addr if memory, offset if reg relative.  */
  memranges->list[memranges->next_memrange].start = base;
  /* len: we actually save end (base + len) for convenience */
  memranges->list[memranges->next_memrange].end = base + len;
  memranges->next_memrange++;
  if (memranges->next_memrange >= memranges->listsize)
    {
      memranges->listsize *= 2;
      memranges->list = xrealloc (memranges->list,
				  memranges->listsize);
    }

  if (type != memrange_absolute)		/* Better collect the base register!  */
    add_register (memranges, type);
}

/* Add a symbol to a collection list.  */
static void
collect_symbol (struct collection_list *collect, 
		struct symbol *sym,
		struct gdbarch *gdbarch,
		long frame_regno, long frame_offset,
		CORE_ADDR scope)
{
  unsigned long len;
  unsigned int reg;
  bfd_signed_vma offset;
  int treat_as_expr = 0;

  len = TYPE_LENGTH (check_typedef (SYMBOL_TYPE (sym)));
  switch (SYMBOL_CLASS (sym))
    {
    default:
      printf_filtered ("%s: don't know symbol class %d\n",
		       SYMBOL_PRINT_NAME (sym),
		       SYMBOL_CLASS (sym));
      break;
    case LOC_CONST:
      printf_filtered ("constant %s (value %ld) will not be collected.\n",
		       SYMBOL_PRINT_NAME (sym), SYMBOL_VALUE (sym));
      break;
    case LOC_STATIC:
      offset = SYMBOL_VALUE_ADDRESS (sym);
      if (info_verbose)
	{
	  char tmp[40];

	  sprintf_vma (tmp, offset);
	  printf_filtered ("LOC_STATIC %s: collect %ld bytes at %s.\n",
			   SYMBOL_PRINT_NAME (sym), len,
			   tmp /* address */);
	}
      /* A struct may be a C++ class with static fields, go to general
	 expression handling.  */
      if (TYPE_CODE (SYMBOL_TYPE (sym)) == TYPE_CODE_STRUCT)
	treat_as_expr = 1;
      else
	add_memrange (collect, memrange_absolute, offset, len);
      break;
    case LOC_REGISTER:
      reg = SYMBOL_REGISTER_OPS (sym)->register_number (sym, gdbarch);
      if (info_verbose)
	printf_filtered ("LOC_REG[parm] %s: ", 
			 SYMBOL_PRINT_NAME (sym));
      add_register (collect, reg);
      /* Check for doubles stored in two registers.  */
      /* FIXME: how about larger types stored in 3 or more regs?  */
      if (TYPE_CODE (SYMBOL_TYPE (sym)) == TYPE_CODE_FLT &&
	  len > register_size (gdbarch, reg))
	add_register (collect, reg + 1);
      break;
    case LOC_REF_ARG:
      printf_filtered ("Sorry, don't know how to do LOC_REF_ARG yet.\n");
      printf_filtered ("       (will not collect %s)\n",
		       SYMBOL_PRINT_NAME (sym));
      break;
    case LOC_ARG:
      reg = frame_regno;
      offset = frame_offset + SYMBOL_VALUE (sym);
      if (info_verbose)
	{
	  printf_filtered ("LOC_LOCAL %s: Collect %ld bytes at offset ",
			   SYMBOL_PRINT_NAME (sym), len);
	  printf_vma (offset);
	  printf_filtered (" from frame ptr reg %d\n", reg);
	}
      add_memrange (collect, reg, offset, len);
      break;
    case LOC_REGPARM_ADDR:
      reg = SYMBOL_VALUE (sym);
      offset = 0;
      if (info_verbose)
	{
	  printf_filtered ("LOC_REGPARM_ADDR %s: Collect %ld bytes at offset ",
			   SYMBOL_PRINT_NAME (sym), len);
	  printf_vma (offset);
	  printf_filtered (" from reg %d\n", reg);
	}
      add_memrange (collect, reg, offset, len);
      break;
    case LOC_LOCAL:
      reg = frame_regno;
      offset = frame_offset + SYMBOL_VALUE (sym);
      if (info_verbose)
	{
	  printf_filtered ("LOC_LOCAL %s: Collect %ld bytes at offset ",
			   SYMBOL_PRINT_NAME (sym), len);
	  printf_vma (offset);
	  printf_filtered (" from frame ptr reg %d\n", reg);
	}
      add_memrange (collect, reg, offset, len);
      break;

    case LOC_UNRESOLVED:
      treat_as_expr = 1;
      break;

    case LOC_OPTIMIZED_OUT:
      printf_filtered ("%s has been optimized out of existence.\n",
		       SYMBOL_PRINT_NAME (sym));
      break;

    case LOC_COMPUTED:
      treat_as_expr = 1;
      break;
    }

  /* Expressions are the most general case.  */
  if (treat_as_expr)
    {
      struct agent_expr *aexpr;
      struct cleanup *old_chain1 = NULL;

      aexpr = gen_trace_for_var (scope, gdbarch, sym);

      /* It can happen that the symbol is recorded as a computed
	 location, but it's been optimized away and doesn't actually
	 have a location expression.  */
      if (!aexpr)
	{
	  printf_filtered ("%s has been optimized out of existence.\n",
			   SYMBOL_PRINT_NAME (sym));
	  return;
	}

      old_chain1 = make_cleanup_free_agent_expr (aexpr);

      ax_reqs (aexpr);

      report_agent_reqs_errors (aexpr);

      discard_cleanups (old_chain1);
      add_aexpr (collect, aexpr);

      /* take care of the registers */
      if (aexpr->reg_mask_len > 0)
	{
	  int ndx1, ndx2;

	  for (ndx1 = 0; ndx1 < aexpr->reg_mask_len; ndx1++)
	    {
	      QUIT;	/* allow user to bail out with ^C */
	      if (aexpr->reg_mask[ndx1] != 0)
		{
		  /* assume chars have 8 bits */
		  for (ndx2 = 0; ndx2 < 8; ndx2++)
		    if (aexpr->reg_mask[ndx1] & (1 << ndx2))
		      /* it's used -- record it */
		      add_register (collect, ndx1 * 8 + ndx2);
		}
	    }
	}
    }
}

/* Data to be passed around in the calls to the locals and args
   iterators.  */

struct add_local_symbols_data
{
  struct collection_list *collect;
  struct gdbarch *gdbarch;
  CORE_ADDR pc;
  long frame_regno;
  long frame_offset;
  int count;
};

/* The callback for the locals and args iterators  */

static void
do_collect_symbol (const char *print_name,
		   struct symbol *sym,
		   void *cb_data)
{
  struct add_local_symbols_data *p = cb_data;

  collect_symbol (p->collect, sym, p->gdbarch, p->frame_regno,
		  p->frame_offset, p->pc);
  p->count++;
}

/* Add all locals (or args) symbols to collection list */
static void
add_local_symbols (struct collection_list *collect,
		   struct gdbarch *gdbarch, CORE_ADDR pc,
		   long frame_regno, long frame_offset, int type)
{
  struct block *block;
  struct add_local_symbols_data cb_data;

  cb_data.collect = collect;
  cb_data.gdbarch = gdbarch;
  cb_data.pc = pc;
  cb_data.frame_regno = frame_regno;
  cb_data.frame_offset = frame_offset;
  cb_data.count = 0;

  if (type == 'L')
    {
      block = block_for_pc (pc);
      if (block == NULL)
	{
	  warning (_("Can't collect locals; "
		     "no symbol table info available.\n"));
	  return;
	}

      iterate_over_block_local_vars (block, do_collect_symbol, &cb_data);
      if (cb_data.count == 0)
	warning (_("No locals found in scope."));
    }
  else
    {
      pc = get_pc_function_start (pc);
      block = block_for_pc (pc);
      if (block == NULL)
	{
	  warning (_("Can't collect args; no symbol table info available.\n"));
	  return;
	}

      iterate_over_block_arg_vars (block, do_collect_symbol, &cb_data);
      if (cb_data.count == 0)
	warning (_("No args found in scope."));
    }
}

/* worker function */
static void
clear_collection_list (struct collection_list *list)
{
  int ndx;

  list->next_memrange = 0;
  for (ndx = 0; ndx < list->next_aexpr_elt; ndx++)
    {
      free_agent_expr (list->aexpr_list[ndx]);
      list->aexpr_list[ndx] = NULL;
    }
  list->next_aexpr_elt = 0;
  memset (list->regs_mask, 0, sizeof (list->regs_mask));
}

/* reduce a collection list to string form (for gdb protocol) */
static char **
stringify_collection_list (struct collection_list *list, char *string)
{
  char temp_buf[2048];
  char tmp2[40];
  int count;
  int ndx = 0;
  char *(*str_list)[];
  char *end;
  long i;

  count = 1 + list->next_memrange + list->next_aexpr_elt + 1;
  str_list = (char *(*)[]) xmalloc (count * sizeof (char *));

  for (i = sizeof (list->regs_mask) - 1; i > 0; i--)
    if (list->regs_mask[i] != 0)	/* skip leading zeroes in regs_mask */
      break;
  if (list->regs_mask[i] != 0)	/* prepare to send regs_mask to the stub */
    {
      if (info_verbose)
	printf_filtered ("\nCollecting registers (mask): 0x");
      end = temp_buf;
      *end++ = 'R';
      for (; i >= 0; i--)
	{
	  QUIT;			/* allow user to bail out with ^C */
	  if (info_verbose)
	    printf_filtered ("%02X", list->regs_mask[i]);
	  sprintf (end, "%02X", list->regs_mask[i]);
	  end += 2;
	}
      (*str_list)[ndx] = xstrdup (temp_buf);
      ndx++;
    }
  if (info_verbose)
    printf_filtered ("\n");
  if (list->next_memrange > 0 && info_verbose)
    printf_filtered ("Collecting memranges: \n");
  for (i = 0, count = 0, end = temp_buf; i < list->next_memrange; i++)
    {
      QUIT;			/* allow user to bail out with ^C */
      sprintf_vma (tmp2, list->list[i].start);
      if (info_verbose)
	{
	  printf_filtered ("(%d, %s, %ld)\n", 
			   list->list[i].type, 
			   tmp2, 
			   (long) (list->list[i].end - list->list[i].start));
	}
      if (count + 27 > MAX_AGENT_EXPR_LEN)
	{
	  (*str_list)[ndx] = savestring (temp_buf, count);
	  ndx++;
	  count = 0;
	  end = temp_buf;
	}

      {
        bfd_signed_vma length = list->list[i].end - list->list[i].start;

        /* The "%X" conversion specifier expects an unsigned argument,
           so passing -1 (memrange_absolute) to it directly gives you
           "FFFFFFFF" (or more, depending on sizeof (unsigned)).
           Special-case it.  */
        if (list->list[i].type == memrange_absolute)
          sprintf (end, "M-1,%s,%lX", tmp2, (long) length);
        else
          sprintf (end, "M%X,%s,%lX", list->list[i].type, tmp2, (long) length);
      }

      count += strlen (end);
      end = temp_buf + count;
    }

  for (i = 0; i < list->next_aexpr_elt; i++)
    {
      QUIT;			/* allow user to bail out with ^C */
      if ((count + 10 + 2 * list->aexpr_list[i]->len) > MAX_AGENT_EXPR_LEN)
	{
	  (*str_list)[ndx] = savestring (temp_buf, count);
	  ndx++;
	  count = 0;
	  end = temp_buf;
	}
      sprintf (end, "X%08X,", list->aexpr_list[i]->len);
      end += 10;		/* 'X' + 8 hex digits + ',' */
      count += 10;

      end = mem2hex (list->aexpr_list[i]->buf, 
		     end, list->aexpr_list[i]->len);
      count += 2 * list->aexpr_list[i]->len;
    }

  if (count != 0)
    {
      (*str_list)[ndx] = savestring (temp_buf, count);
      ndx++;
      count = 0;
      end = temp_buf;
    }
  (*str_list)[ndx] = NULL;

  if (ndx == 0)
    {
      xfree (str_list);
      return NULL;
    }
  else
    return *str_list;
}


static void
encode_actions_1 (struct command_line *action,
		  struct breakpoint *t,
		  struct bp_location *tloc,
		  int frame_reg,
		  LONGEST frame_offset,
		  struct collection_list *collect,
		  struct collection_list *stepping_list)
{
  char *action_exp;
  struct expression *exp = NULL;
  int i;
  struct value *tempval;
  struct cmd_list_element *cmd;
  struct agent_expr *aexpr;

  for (; action; action = action->next)
    {
      QUIT;			/* allow user to bail out with ^C */
      action_exp = action->line;
      while (isspace ((int) *action_exp))
	action_exp++;

      cmd = lookup_cmd (&action_exp, cmdlist, "", -1, 1);
      if (cmd == 0)
	error (_("Bad action list item: %s"), action_exp);

      if (cmd_cfunc_eq (cmd, collect_pseudocommand))
	{
	  do
	    {			/* repeat over a comma-separated list */
	      QUIT;		/* allow user to bail out with ^C */
	      while (isspace ((int) *action_exp))
		action_exp++;

	      if (0 == strncasecmp ("$reg", action_exp, 4))
		{
		  for (i = 0; i < gdbarch_num_regs (t->gdbarch); i++)
		    add_register (collect, i);
		  action_exp = strchr (action_exp, ',');	/* more? */
		}
	      else if (0 == strncasecmp ("$arg", action_exp, 4))
		{
		  add_local_symbols (collect,
				     t->gdbarch,
				     tloc->address,
				     frame_reg,
				     frame_offset,
				     'A');
		  action_exp = strchr (action_exp, ',');	/* more? */
		}
	      else if (0 == strncasecmp ("$loc", action_exp, 4))
		{
		  add_local_symbols (collect,
				     t->gdbarch,
				     tloc->address,
				     frame_reg,
				     frame_offset,
				     'L');
		  action_exp = strchr (action_exp, ',');	/* more? */
		}
	      else
		{
		  unsigned long addr, len;
		  struct cleanup *old_chain = NULL;
		  struct cleanup *old_chain1 = NULL;

		  exp = parse_exp_1 (&action_exp, 
				     block_for_pc (tloc->address), 1);
		  old_chain = make_cleanup (free_current_contents, &exp);

		  switch (exp->elts[0].opcode)
		    {
		    case OP_REGISTER:
		      {
			const char *name = &exp->elts[2].string;

			i = user_reg_map_name_to_regnum (t->gdbarch,
							 name, strlen (name));
			if (i == -1)
			  internal_error (__FILE__, __LINE__,
					  _("Register $%s not available"),
					  name);
			if (info_verbose)
			  printf_filtered ("OP_REGISTER: ");
			add_register (collect, i);
			break;
		      }

		    case UNOP_MEMVAL:
		      /* safe because we know it's a simple expression */
		      tempval = evaluate_expression (exp);
		      addr = value_address (tempval);
		      len = TYPE_LENGTH (check_typedef (exp->elts[1].type));
		      add_memrange (collect, memrange_absolute, addr, len);
		      break;

		    case OP_VAR_VALUE:
		      collect_symbol (collect,
				      exp->elts[2].symbol,
				      t->gdbarch,
				      frame_reg,
				      frame_offset,
				      tloc->address);
		      break;

		    default:	/* full-fledged expression */
		      aexpr = gen_trace_for_expr (tloc->address, exp);

		      old_chain1 = make_cleanup_free_agent_expr (aexpr);

		      ax_reqs (aexpr);

		      report_agent_reqs_errors (aexpr);

		      discard_cleanups (old_chain1);
		      add_aexpr (collect, aexpr);

		      /* take care of the registers */
		      if (aexpr->reg_mask_len > 0)
			{
			  int ndx1;
			  int ndx2;

			  for (ndx1 = 0; ndx1 < aexpr->reg_mask_len; ndx1++)
			    {
			      QUIT;	/* allow user to bail out with ^C */
			      if (aexpr->reg_mask[ndx1] != 0)
				{
				  /* assume chars have 8 bits */
				  for (ndx2 = 0; ndx2 < 8; ndx2++)
				    if (aexpr->reg_mask[ndx1] & (1 << ndx2))
				      /* it's used -- record it */
				      add_register (collect, 
						    ndx1 * 8 + ndx2);
				}
			    }
			}
		      break;
		    }		/* switch */
		  do_cleanups (old_chain);
		}		/* do */
	    }
	  while (action_exp && *action_exp++ == ',');
	}			/* if */
      else if (cmd_cfunc_eq (cmd, teval_pseudocommand))
	{
	  do
	    {			/* repeat over a comma-separated list */
	      QUIT;		/* allow user to bail out with ^C */
	      while (isspace ((int) *action_exp))
		action_exp++;

		{
		  struct cleanup *old_chain = NULL;
		  struct cleanup *old_chain1 = NULL;

		  exp = parse_exp_1 (&action_exp, 
				     block_for_pc (tloc->address), 1);
		  old_chain = make_cleanup (free_current_contents, &exp);

		  aexpr = gen_eval_for_expr (tloc->address, exp);
		  old_chain1 = make_cleanup_free_agent_expr (aexpr);

		  ax_reqs (aexpr);
		  report_agent_reqs_errors (aexpr);

		  discard_cleanups (old_chain1);
		  /* Even though we're not officially collecting, add
		     to the collect list anyway.  */
		  add_aexpr (collect, aexpr);

		  do_cleanups (old_chain);
		}		/* do */
	    }
	  while (action_exp && *action_exp++ == ',');
	}			/* if */
      else if (cmd_cfunc_eq (cmd, while_stepping_pseudocommand))
	{
	  /* We check against nested while-stepping when setting
	     breakpoint action, so no way to run into nested
	     here.  */
	  gdb_assert (stepping_list);

	  encode_actions_1 (action->body_list[0], t, tloc, frame_reg,
			    frame_offset, stepping_list, NULL);
	}
      else
	error (_("Invalid tracepoint command '%s'"), action->line);
    }				/* for */
}

/* Render all actions into gdb protocol.  */
/*static*/ void
encode_actions (struct breakpoint *t, struct bp_location *tloc,
		char ***tdp_actions, char ***stepping_actions)
{
  static char tdp_buff[2048], step_buff[2048];
  char *default_collect_line = NULL;
  struct command_line *actions;
  struct command_line *default_collect_action = NULL;
  int frame_reg;
  LONGEST frame_offset;
  struct cleanup *back_to;

  back_to = make_cleanup (null_cleanup, NULL);

  clear_collection_list (&tracepoint_list);
  clear_collection_list (&stepping_list);

  *tdp_actions = NULL;
  *stepping_actions = NULL;

  gdbarch_virtual_frame_pointer (t->gdbarch,
				 t->loc->address, &frame_reg, &frame_offset);

  actions = breakpoint_commands (t);

  /* If there are default expressions to collect, make up a collect
     action and prepend to the action list to encode.  Note that since
     validation is per-tracepoint (local var "xyz" might be valid for
     one tracepoint and not another, etc), we make up the action on
     the fly, and don't cache it.  */
  if (*default_collect)
    {
      char *line;

      default_collect_line =  xstrprintf ("collect %s", default_collect);
      make_cleanup (xfree, default_collect_line);

      line = default_collect_line;
      validate_actionline (&line, t);

      default_collect_action = xmalloc (sizeof (struct command_line));
      make_cleanup (xfree, default_collect_action);
      default_collect_action->next = actions;
      default_collect_action->line = line;
      actions = default_collect_action;
    }
  encode_actions_1 (actions, t, tloc, frame_reg, frame_offset,
		    &tracepoint_list, &stepping_list);

  memrange_sortmerge (&tracepoint_list);
  memrange_sortmerge (&stepping_list);

  *tdp_actions = stringify_collection_list (&tracepoint_list,
					    tdp_buff);
  *stepping_actions = stringify_collection_list (&stepping_list,
						 step_buff);

  do_cleanups (back_to);
}

static void
add_aexpr (struct collection_list *collect, struct agent_expr *aexpr)
{
  if (collect->next_aexpr_elt >= collect->aexpr_listsize)
    {
      collect->aexpr_list =
	xrealloc (collect->aexpr_list,
		  2 * collect->aexpr_listsize * sizeof (struct agent_expr *));
      collect->aexpr_listsize *= 2;
    }
  collect->aexpr_list[collect->next_aexpr_elt] = aexpr;
  collect->next_aexpr_elt++;
}


void
start_tracing (void)
{
  VEC(breakpoint_p) *tp_vec = NULL;
  int ix;
  struct breakpoint *t;
  struct trace_state_variable *tsv;
  int any_enabled = 0;
  
  tp_vec = all_tracepoints ();

  /* No point in tracing without any tracepoints... */
  if (VEC_length (breakpoint_p, tp_vec) == 0)
    {
      VEC_free (breakpoint_p, tp_vec);
      error (_("No tracepoints defined, not starting trace"));
    }

  for (ix = 0; VEC_iterate (breakpoint_p, tp_vec, ix, t); ix++)
    {
      if (t->enable_state == bp_enabled)
	{
	  any_enabled = 1;
	  break;
	}
    }

  /* No point in tracing with only disabled tracepoints.  */
  if (!any_enabled)
    {
      VEC_free (breakpoint_p, tp_vec);
      error (_("No tracepoints enabled, not starting trace"));
    }

  target_trace_init ();

  for (ix = 0; VEC_iterate (breakpoint_p, tp_vec, ix, t); ix++)
    {
      t->number_on_target = 0;
      target_download_tracepoint (t);
      t->number_on_target = t->number;
    }
  VEC_free (breakpoint_p, tp_vec);

  /* Send down all the trace state variables too.  */
  for (ix = 0; VEC_iterate (tsv_s, tvariables, ix, tsv); ++ix)
    {
      target_download_trace_state_variable (tsv);
    }
  
  /* Tell target to treat text-like sections as transparent.  */
  target_trace_set_readonly_regions ();
  /* Set some mode flags.  */
  target_set_disconnected_tracing (disconnected_tracing);
  target_set_circular_trace_buffer (circular_trace_buffer);

  /* Now insert traps and begin collecting data.  */
  target_trace_start ();

  /* Reset our local state.  */
  set_traceframe_num (-1);
  set_tracepoint_num (-1);
  set_traceframe_context (NULL);
  current_trace_status()->running = 1;
}

/* tstart command:

   Tell target to clear any previous trace experiment.
   Walk the list of tracepoints, and send them (and their actions)
   to the target.  If no errors,
   Tell target to start a new trace experiment.  */

static void
trace_start_command (char *args, int from_tty)
{
  dont_repeat ();	/* Like "run", dangerous to repeat accidentally.  */

  if (current_trace_status ()->running)
    {
      if (from_tty
	  && !query (_("A trace is running already.  Start a new run? ")))
	error (_("New trace run not started."));
    }

  start_tracing ();
}

/* tstop command */
static void
trace_stop_command (char *args, int from_tty)
{
  if (!current_trace_status ()->running)
    error (_("Trace is not running."));

  stop_tracing ();
}

void
stop_tracing (void)
{
  target_trace_stop ();
  /* should change in response to reply? */
  current_trace_status ()->running = 0;
}

/* tstatus command */
static void
trace_status_command (char *args, int from_tty)
{
  struct trace_status *ts = current_trace_status ();
  int status;
  
  status = target_get_trace_status (ts);

  if (status == -1)
    {
      if (ts->from_file)
	printf_filtered (_("Using a trace file.\n"));
      else
	{
	  printf_filtered (_("Trace can not be run on this target.\n"));
	  return;
	}
    }

  if (!ts->running_known)
    {
      printf_filtered (_("Run/stop status is unknown.\n"));
    }
  else if (ts->running)
    {
      printf_filtered (_("Trace is running on the target.\n"));
    }
  else
    {
      switch (ts->stop_reason)
	{
	case trace_never_run:
	  printf_filtered (_("No trace has been run on the target.\n"));
	  break;
	case tstop_command:
	  printf_filtered (_("Trace stopped by a tstop command.\n"));
	  break;
	case trace_buffer_full:
	  printf_filtered (_("Trace stopped because the buffer was full.\n"));
	  break;
	case trace_disconnected:
	  printf_filtered (_("Trace stopped because of disconnection.\n"));
	  break;
	case tracepoint_passcount:
	  printf_filtered (_("Trace stopped by tracepoint %d.\n"),
			   ts->stopping_tracepoint);
	  break;
	case tracepoint_error:
	  if (ts->stopping_tracepoint)
	    printf_filtered (_("Trace stopped by an error (%s, tracepoint %d).\n"),
			     ts->error_desc, ts->stopping_tracepoint);
	  else
	    printf_filtered (_("Trace stopped by an error (%s).\n"),
			     ts->error_desc);
	  break;
	case trace_stop_reason_unknown:
	  printf_filtered (_("Trace stopped for an unknown reason.\n"));
	  break;
	default:
	  printf_filtered (_("Trace stopped for some other reason (%d).\n"),
			   ts->stop_reason);
	  break;
	}
    }

  if (ts->traceframes_created >= 0
      && ts->traceframe_count != ts->traceframes_created)
    {
      printf_filtered (_("Buffer contains %d trace frames (of %d created total).\n"),
		       ts->traceframe_count, ts->traceframes_created);
    }
  else if (ts->traceframe_count >= 0)
    {
      printf_filtered (_("Collected %d trace frames.\n"),
		       ts->traceframe_count);
    }

  if (ts->buffer_free >= 0)
    {
      if (ts->buffer_size >= 0)
	{
	  printf_filtered (_("Trace buffer has %d bytes of %d bytes free"),
			   ts->buffer_free, ts->buffer_size);
	  if (ts->buffer_size > 0)
	    printf_filtered (_(" (%d%% full)"),
			     ((int) ((((long long) (ts->buffer_size
						    - ts->buffer_free)) * 100)
				     / ts->buffer_size)));
	  printf_filtered (_(".\n"));
	}
      else
	printf_filtered (_("Trace buffer has %d bytes free.\n"),
			 ts->buffer_free);
    }

  if (ts->disconnected_tracing)
    printf_filtered (_("Trace will continue if GDB disconnects.\n"));
  else
    printf_filtered (_("Trace will stop if GDB disconnects.\n"));

  if (ts->circular_buffer)
    printf_filtered (_("Trace buffer is circular.\n"));

  /* Now report on what we're doing with tfind.  */
  if (traceframe_number >= 0)
    printf_filtered (_("Looking at trace frame %d, tracepoint %d.\n"),
		     traceframe_number, tracepoint_number);
  else
    printf_filtered (_("Not looking at any trace frame.\n"));
}

/* Report the trace status to uiout, in a way suitable for MI, and not
   suitable for CLI.  If ON_STOP is true, suppress a few fields that
   are not meaningful in the -trace-stop response.

   The implementation is essentially parallel to trace_status_command, but
   merging them will result in unreadable code.  */
void
trace_status_mi (int on_stop)
{
  struct trace_status *ts = current_trace_status ();
  int status;

  status = target_get_trace_status (ts);

  if (status == -1 && !ts->from_file)
    {
      ui_out_field_string (uiout, "supported", "0");
      return;
    }

  if (ts->from_file)
    ui_out_field_string (uiout, "supported", "file");
  else if (!on_stop)
    ui_out_field_string (uiout, "supported", "1");

  gdb_assert (ts->running_known);

  if (ts->running)
    {
      ui_out_field_string (uiout, "running", "1");

      /* Unlike CLI, do not show the state of 'disconnected-tracing' variable.
	 Given that the frontend gets the status either on -trace-stop, or from
	 -trace-status after re-connection, it does not seem like this
	 information is necessary for anything.  It is not necessary for either
	 figuring the vital state of the target nor for navigation of trace
	 frames.  If the frontend wants to show the current state is some
	 configure dialog, it can request the value when such dialog is
	 invoked by the user.  */
    }
  else
    {
      char *stop_reason = NULL;
      int stopping_tracepoint = -1;

      if (!on_stop)
	ui_out_field_string (uiout, "running", "0");

      if (ts->stop_reason != trace_stop_reason_unknown)
	{
	  switch (ts->stop_reason)
	    {
	    case tstop_command:
	      stop_reason = "request";
	      break;
	    case trace_buffer_full:
	      stop_reason = "overflow";
	      break;
	    case trace_disconnected:
	      stop_reason = "disconnection";
	      break;
	    case tracepoint_passcount:
	      stop_reason = "passcount";
	      stopping_tracepoint = ts->stopping_tracepoint;
	      break;
	    case tracepoint_error:
	      stop_reason = "error";
	      stopping_tracepoint = ts->stopping_tracepoint;
	      break;
	    }
	  
	  if (stop_reason)
	    {
	      ui_out_field_string (uiout, "stop-reason", stop_reason);
	      if (stopping_tracepoint != -1)
		ui_out_field_int (uiout, "stopping-tracepoint",
				  stopping_tracepoint);
	      if (ts->stop_reason == tracepoint_error)
		ui_out_field_string (uiout, "error-description",
				     ts->error_desc);
	    }
	}
    }

  if (ts->traceframe_count != -1)
    ui_out_field_int (uiout, "frames", ts->traceframe_count);
  if (ts->traceframes_created != -1)
    ui_out_field_int (uiout, "frames-created", ts->traceframes_created);
  if (ts->buffer_size != -1)
    ui_out_field_int (uiout, "buffer-size", ts->buffer_size);
  if (ts->buffer_free != -1)
    ui_out_field_int (uiout, "buffer-free", ts->buffer_free);

  ui_out_field_int (uiout, "disconnected",  ts->disconnected_tracing);
  ui_out_field_int (uiout, "circular",  ts->circular_buffer);
}

/* This function handles the details of what to do about an ongoing
   tracing run if the user has asked to detach or otherwise disconnect
   from the target.  */
void
disconnect_tracing (int from_tty)
{
  /* It can happen that the target that was tracing went away on its
     own, and we didn't notice.  Get a status update, and if the
     current target doesn't even do tracing, then assume it's not
     running anymore.  */
  if (target_get_trace_status (current_trace_status ()) < 0)
    current_trace_status ()->running = 0;

  /* If running interactively, give the user the option to cancel and
     then decide what to do differently with the run.  Scripts are
     just going to disconnect and let the target deal with it,
     according to how it's been instructed previously via
     disconnected-tracing.  */
  if (current_trace_status ()->running && from_tty)
    {
      if (current_trace_status ()->disconnected_tracing)
	{
	  if (!query (_("Trace is running and will continue after detach; detach anyway? ")))
	    error (_("Not confirmed."));
	}
      else
	{
	  if (!query (_("Trace is running but will stop on detach; detach anyway? ")))
	    error (_("Not confirmed."));
	}
    }

  /* Also we want to be out of tfind mode, otherwise things can get
     confusing upon reconnection.  Just use these calls instead of
     full tfind_1 behavior because we're in the middle of detaching,
     and there's no point to updating current stack frame etc.  */
  set_traceframe_number (-1);
  set_traceframe_context (NULL);
}

/* Worker function for the various flavors of the tfind command.  */
void
tfind_1 (enum trace_find_type type, int num,
	 ULONGEST addr1, ULONGEST addr2,
	 int from_tty)
{
  int target_frameno = -1, target_tracept = -1;
  struct frame_id old_frame_id = null_frame_id;
  struct breakpoint *tp;

  /* Only try to get the current stack frame if we have a chance of
     succeeding.  In particular, if we're trying to get a first trace
     frame while all threads are running, it's not going to succeed,
     so leave it with a default value and let the frame comparison
     below (correctly) decide to print out the source location of the
     trace frame.  */
  if (!(type == tfind_number && num == -1)
      && (has_stack_frames () || traceframe_number >= 0))
    old_frame_id = get_frame_id (get_current_frame ());

  target_frameno = target_trace_find (type, num, addr1, addr2,
				      &target_tracept);
  
  if (type == tfind_number
      && num == -1
      && target_frameno == -1)
    {
      /* We told the target to get out of tfind mode, and it did.  */
    }
  else if (target_frameno == -1)
    {
      /* A request for a non-existent trace frame has failed.
	 Our response will be different, depending on FROM_TTY:

	 If FROM_TTY is true, meaning that this command was 
	 typed interactively by the user, then give an error
	 and DO NOT change the state of traceframe_number etc.

	 However if FROM_TTY is false, meaning that we're either
	 in a script, a loop, or a user-defined command, then 
	 DON'T give an error, but DO change the state of
	 traceframe_number etc. to invalid.

	 The rationalle is that if you typed the command, you
	 might just have committed a typo or something, and you'd
	 like to NOT lose your current debugging state.  However
	 if you're in a user-defined command or especially in a
	 loop, then you need a way to detect that the command
	 failed WITHOUT aborting.  This allows you to write
	 scripts that search thru the trace buffer until the end,
	 and then continue on to do something else.  */
  
      if (from_tty)
	error (_("Target failed to find requested trace frame."));
      else
	{
	  if (info_verbose)
	    printf_filtered ("End of trace buffer.\n");
#if 0 /* dubious now? */
	  /* The following will not recurse, since it's
	     special-cased.  */
	  trace_find_command ("-1", from_tty);
#endif
	}
    }
  
  tp = get_tracepoint_by_number_on_target (target_tracept);

  reinit_frame_cache ();
  registers_changed ();
  target_dcache_invalidate ();
  set_traceframe_num (target_frameno);
  set_tracepoint_num (tp ? tp->number : target_tracept);
  if (target_frameno == -1)
    set_traceframe_context (NULL);
  else
    set_traceframe_context (get_current_frame ());

  if (traceframe_number >= 0)
    {
      /* Use different branches for MI and CLI to make CLI messages
	 i18n-eable.  */
      if (ui_out_is_mi_like_p (uiout))
	{
	  ui_out_field_string (uiout, "found", "1");
	  ui_out_field_int (uiout, "tracepoint", tracepoint_number);
	  ui_out_field_int (uiout, "traceframe", traceframe_number);
	}
      else
	{
	  printf_unfiltered (_("Found trace frame %d, tracepoint %d\n"),
			     traceframe_number, tracepoint_number);
	}
    }
  else
    {
      if (ui_out_is_mi_like_p (uiout))
	ui_out_field_string (uiout, "found", "0");
      else if (type == tfind_number && num == -1)
	printf_unfiltered (_("No longer looking at any trace frame\n"));
      else /* this case may never occur, check */
	printf_unfiltered (_("No trace frame found\n"));
    }

  /* If we're in nonstop mode and getting out of looking at trace
     frames, there won't be any current frame to go back to and
     display.  */
  if (from_tty
      && (has_stack_frames () || traceframe_number >= 0))
    {
      enum print_what print_what;

      /* NOTE: in imitation of the step command, try to determine
         whether we have made a transition from one function to
         another.  If so, we'll print the "stack frame" (ie. the new
         function and it's arguments) -- otherwise we'll just show the
         new source line.  */

      if (frame_id_eq (old_frame_id,
		       get_frame_id (get_current_frame ())))
	print_what = SRC_LINE;
      else
	print_what = SRC_AND_LOC;

      print_stack_frame (get_selected_frame (NULL), 1, print_what);
      do_displays ();
    }
}

/* trace_find_command takes a trace frame number n, 
   sends "QTFrame:<n>" to the target, 
   and accepts a reply that may contain several optional pieces
   of information: a frame number, a tracepoint number, and an
   indication of whether this is a trap frame or a stepping frame.

   The minimal response is just "OK" (which indicates that the 
   target does not give us a frame number or a tracepoint number).
   Instead of that, the target may send us a string containing
   any combination of:
   F<hexnum>    (gives the selected frame number)
   T<hexnum>    (gives the selected tracepoint number)
 */

/* tfind command */
static void
trace_find_command (char *args, int from_tty)
{ /* this should only be called with a numeric argument */
  int frameno = -1;

  if (current_trace_status ()->running && !current_trace_status ()->from_file)
    error ("May not look at trace frames while trace is running.");
  
  if (args == 0 || *args == 0)
    { /* TFIND with no args means find NEXT trace frame.  */
      if (traceframe_number == -1)
	frameno = 0;	/* "next" is first one */
        else
	frameno = traceframe_number + 1;
    }
  else if (0 == strcmp (args, "-"))
    {
      if (traceframe_number == -1)
	error (_("not debugging trace buffer"));
      else if (from_tty && traceframe_number == 0)
	error (_("already at start of trace buffer"));
      
      frameno = traceframe_number - 1;
      }
  /* A hack to work around eval's need for fp to have been collected.  */
  else if (0 == strcmp (args, "-1"))
    frameno = -1;
  else
    frameno = parse_and_eval_long (args);

  if (frameno < -1)
    error (_("invalid input (%d is less than zero)"), frameno);

  tfind_1 (tfind_number, frameno, 0, 0, from_tty);
}

/* tfind end */
static void
trace_find_end_command (char *args, int from_tty)
{
  trace_find_command ("-1", from_tty);
}

/* tfind none */
static void
trace_find_none_command (char *args, int from_tty)
{
  trace_find_command ("-1", from_tty);
}

/* tfind start */
static void
trace_find_start_command (char *args, int from_tty)
{
  trace_find_command ("0", from_tty);
}

/* tfind pc command */
static void
trace_find_pc_command (char *args, int from_tty)
{
  CORE_ADDR pc;

  if (current_trace_status ()->running && !current_trace_status ()->from_file)
    error ("May not look at trace frames while trace is running.");

  if (args == 0 || *args == 0)
    pc = regcache_read_pc (get_current_regcache ());
  else
    pc = parse_and_eval_address (args);

  tfind_1 (tfind_pc, 0, pc, 0, from_tty);
}

/* tfind tracepoint command */
static void
trace_find_tracepoint_command (char *args, int from_tty)
{
  int tdp;
  struct breakpoint *tp;

  if (current_trace_status ()->running && !current_trace_status ()->from_file)
    error ("May not look at trace frames while trace is running.");

  if (args == 0 || *args == 0)
    {
      if (tracepoint_number == -1)
	error (_("No current tracepoint -- please supply an argument."));
      else
	tdp = tracepoint_number;	/* default is current TDP */
    }
  else
    tdp = parse_and_eval_long (args);

  /* If we have the tracepoint on hand, use the number that the
     target knows about (which may be different if we disconnected
     and reconnected).  */
  tp = get_tracepoint (tdp);
  if (tp)
    tdp = tp->number_on_target;

  tfind_1 (tfind_tp, tdp, 0, 0, from_tty);
}

/* TFIND LINE command:

   This command will take a sourceline for argument, just like BREAK
   or TRACE (ie. anything that "decode_line_1" can handle).

   With no argument, this command will find the next trace frame 
   corresponding to a source line OTHER THAN THE CURRENT ONE.  */

static void
trace_find_line_command (char *args, int from_tty)
{
  static CORE_ADDR start_pc, end_pc;
  struct symtabs_and_lines sals;
  struct symtab_and_line sal;
  struct cleanup *old_chain;

  if (current_trace_status ()->running && !current_trace_status ()->from_file)
    error ("May not look at trace frames while trace is running.");

  if (args == 0 || *args == 0)
    {
      sal = find_pc_line (get_frame_pc (get_current_frame ()), 0);
      sals.nelts = 1;
      sals.sals = (struct symtab_and_line *)
	xmalloc (sizeof (struct symtab_and_line));
      sals.sals[0] = sal;
    }
  else
    {
      sals = decode_line_spec (args, 1);
      sal = sals.sals[0];
    }
  
  old_chain = make_cleanup (xfree, sals.sals);
  if (sal.symtab == 0)
    error (_("No line number information available."));

  if (sal.line > 0 && find_line_pc_range (sal, &start_pc, &end_pc))
    {
      if (start_pc == end_pc)
  	{
	  printf_filtered ("Line %d of \"%s\"",
			   sal.line, sal.symtab->filename);
	  wrap_here ("  ");
	  printf_filtered (" is at address ");
	  print_address (get_current_arch (), start_pc, gdb_stdout);
	  wrap_here ("  ");
	  printf_filtered (" but contains no code.\n");
	  sal = find_pc_line (start_pc, 0);
	  if (sal.line > 0
	      && find_line_pc_range (sal, &start_pc, &end_pc)
	      && start_pc != end_pc)
	    printf_filtered ("Attempting to find line %d instead.\n",
			     sal.line);
  	  else
	    error (_("Cannot find a good line."));
  	}
      }
    else
    /* Is there any case in which we get here, and have an address
       which the user would want to see?  If we have debugging
       symbols and no line numbers?  */
    error (_("Line number %d is out of range for \"%s\"."),
	   sal.line, sal.symtab->filename);

  /* Find within range of stated line.  */
  if (args && *args)
    tfind_1 (tfind_range, 0, start_pc, end_pc - 1, from_tty);
  else
    tfind_1 (tfind_outside, 0, start_pc, end_pc - 1, from_tty);
  do_cleanups (old_chain);
}

/* tfind range command */
static void
trace_find_range_command (char *args, int from_tty)
{
  static CORE_ADDR start, stop;
  char *tmp;

  if (current_trace_status ()->running && !current_trace_status ()->from_file)
    error ("May not look at trace frames while trace is running.");

  if (args == 0 || *args == 0)
    { /* XXX FIXME: what should default behavior be?  */
      printf_filtered ("Usage: tfind range <startaddr>,<endaddr>\n");
      return;
    }

  if (0 != (tmp = strchr (args, ',')))
    {
      *tmp++ = '\0';	/* terminate start address */
      while (isspace ((int) *tmp))
	tmp++;
      start = parse_and_eval_address (args);
      stop = parse_and_eval_address (tmp);
    }
  else
    {			/* no explicit end address? */
      start = parse_and_eval_address (args);
      stop = start + 1;	/* ??? */
    }

  tfind_1 (tfind_range, 0, start, stop, from_tty);
}

/* tfind outside command */
static void
trace_find_outside_command (char *args, int from_tty)
{
  CORE_ADDR start, stop;
  char *tmp;

  if (current_trace_status ()->running && !current_trace_status ()->from_file)
    error ("May not look at trace frames while trace is running.");

  if (args == 0 || *args == 0)
    { /* XXX FIXME: what should default behavior be? */
      printf_filtered ("Usage: tfind outside <startaddr>,<endaddr>\n");
      return;
    }

  if (0 != (tmp = strchr (args, ',')))
    {
      *tmp++ = '\0';	/* terminate start address */
      while (isspace ((int) *tmp))
	tmp++;
      start = parse_and_eval_address (args);
      stop = parse_and_eval_address (tmp);
    }
  else
    {			/* no explicit end address? */
      start = parse_and_eval_address (args);
      stop = start + 1;	/* ??? */
    }

  tfind_1 (tfind_outside, 0, start, stop, from_tty);
}

/* info scope command: list the locals for a scope.  */
static void
scope_info (char *args, int from_tty)
{
  struct symtabs_and_lines sals;
  struct symbol *sym;
  struct minimal_symbol *msym;
  struct block *block;
  char **canonical, *symname, *save_args = args;
  struct dict_iterator iter;
  int j, count = 0;
  struct gdbarch *gdbarch;
  int regno;

  if (args == 0 || *args == 0)
    error (_("requires an argument (function, line or *addr) to define a scope"));

  sals = decode_line_1 (&args, 1, NULL, 0, &canonical, NULL);
  if (sals.nelts == 0)
    return;		/* presumably decode_line_1 has already warned */

  /* Resolve line numbers to PC */
  resolve_sal_pc (&sals.sals[0]);
  block = block_for_pc (sals.sals[0].pc);

  while (block != 0)
    {
      QUIT;			/* allow user to bail out with ^C */
      ALL_BLOCK_SYMBOLS (block, iter, sym)
	{
	  QUIT;			/* allow user to bail out with ^C */
	  if (count == 0)
	    printf_filtered ("Scope for %s:\n", save_args);
	  count++;

	  symname = SYMBOL_PRINT_NAME (sym);
	  if (symname == NULL || *symname == '\0')
	    continue;		/* probably botched, certainly useless */

	  gdbarch = get_objfile_arch (SYMBOL_SYMTAB (sym)->objfile);

	  printf_filtered ("Symbol %s is ", symname);
	  switch (SYMBOL_CLASS (sym))
	    {
	    default:
	    case LOC_UNDEF:	/* messed up symbol? */
	      printf_filtered ("a bogus symbol, class %d.\n",
			       SYMBOL_CLASS (sym));
	      count--;		/* don't count this one */
	      continue;
	    case LOC_CONST:
	      printf_filtered ("a constant with value %ld (0x%lx)",
			       SYMBOL_VALUE (sym), SYMBOL_VALUE (sym));
	      break;
	    case LOC_CONST_BYTES:
	      printf_filtered ("constant bytes: ");
	      if (SYMBOL_TYPE (sym))
		for (j = 0; j < TYPE_LENGTH (SYMBOL_TYPE (sym)); j++)
		  fprintf_filtered (gdb_stdout, " %02x",
				    (unsigned) SYMBOL_VALUE_BYTES (sym)[j]);
	      break;
	    case LOC_STATIC:
	      printf_filtered ("in static storage at address ");
	      printf_filtered ("%s", paddress (gdbarch,
					       SYMBOL_VALUE_ADDRESS (sym)));
	      break;
	    case LOC_REGISTER:
	      /* GDBARCH is the architecture associated with the objfile
		 the symbol is defined in; the target architecture may be
		 different, and may provide additional registers.  However,
		 we do not know the target architecture at this point.
		 We assume the objfile architecture will contain all the
		 standard registers that occur in debug info in that
		 objfile.  */
	      regno = SYMBOL_REGISTER_OPS (sym)->register_number (sym, gdbarch);

	      if (SYMBOL_IS_ARGUMENT (sym))
		printf_filtered ("an argument in register $%s",
				 gdbarch_register_name (gdbarch, regno));
	      else
		printf_filtered ("a local variable in register $%s",
				 gdbarch_register_name (gdbarch, regno));
	      break;
	    case LOC_ARG:
	      printf_filtered ("an argument at stack/frame offset %ld",
			       SYMBOL_VALUE (sym));
	      break;
	    case LOC_LOCAL:
	      printf_filtered ("a local variable at frame offset %ld",
			       SYMBOL_VALUE (sym));
	      break;
	    case LOC_REF_ARG:
	      printf_filtered ("a reference argument at offset %ld",
			       SYMBOL_VALUE (sym));
	      break;
	    case LOC_REGPARM_ADDR:
	      /* Note comment at LOC_REGISTER.  */
	      regno = SYMBOL_REGISTER_OPS (sym)->register_number (sym, gdbarch);
	      printf_filtered ("the address of an argument, in register $%s",
			       gdbarch_register_name (gdbarch, regno));
	      break;
	    case LOC_TYPEDEF:
	      printf_filtered ("a typedef.\n");
	      continue;
	    case LOC_LABEL:
	      printf_filtered ("a label at address ");
	      printf_filtered ("%s", paddress (gdbarch,
					       SYMBOL_VALUE_ADDRESS (sym)));
	      break;
	    case LOC_BLOCK:
	      printf_filtered ("a function at address ");
	      printf_filtered ("%s",
		paddress (gdbarch, BLOCK_START (SYMBOL_BLOCK_VALUE (sym))));
	      break;
	    case LOC_UNRESOLVED:
	      msym = lookup_minimal_symbol (SYMBOL_LINKAGE_NAME (sym),
					    NULL, NULL);
	      if (msym == NULL)
		printf_filtered ("Unresolved Static");
	      else
		{
		  printf_filtered ("static storage at address ");
		  printf_filtered ("%s",
		    paddress (gdbarch, SYMBOL_VALUE_ADDRESS (msym)));
		}
	      break;
	    case LOC_OPTIMIZED_OUT:
	      printf_filtered ("optimized out.\n");
	      continue;
	    case LOC_COMPUTED:
	      SYMBOL_COMPUTED_OPS (sym)->describe_location (sym,
							    BLOCK_START (block),
							    gdb_stdout);
	      break;
	    }
	  if (SYMBOL_TYPE (sym))
	    printf_filtered (", length %d.\n",
			     TYPE_LENGTH (check_typedef (SYMBOL_TYPE (sym))));
	}
      if (BLOCK_FUNCTION (block))
	break;
      else
	block = BLOCK_SUPERBLOCK (block);
    }
  if (count <= 0)
    printf_filtered ("Scope for %s contains no locals or arguments.\n",
		     save_args);
}

/* worker function (cleanup) */
static void
replace_comma (void *data)
{
  char *comma = data;
  *comma = ',';
}


/* Helper for trace_dump_command.  Dump the action list starting at
   ACTION.  STEPPING_ACTIONS is true if we're iterating over the
   actions of the body of a while-stepping action.  STEPPING_FRAME is
   set if the current traceframe was determined to be a while-stepping
   traceframe.  */

static void
trace_dump_actions (struct command_line *action,
		    int stepping_actions, int stepping_frame,
		    int from_tty)
{
  char *action_exp, *next_comma;

  for (; action != NULL; action = action->next)
    {
      struct cmd_list_element *cmd;

      QUIT;			/* allow user to bail out with ^C */
      action_exp = action->line;
      while (isspace ((int) *action_exp))
	action_exp++;

      /* The collection actions to be done while stepping are
         bracketed by the commands "while-stepping" and "end".  */

      if (*action_exp == '#')	/* comment line */
	continue;

      cmd = lookup_cmd (&action_exp, cmdlist, "", -1, 1);
      if (cmd == 0)
	error (_("Bad action list item: %s"), action_exp);

      if (cmd_cfunc_eq (cmd, while_stepping_pseudocommand))
	{
	  int i;

	  for (i = 0; i < action->body_count; ++i)
	    trace_dump_actions (action->body_list[i],
				1, stepping_frame, from_tty);
	}
      else if (cmd_cfunc_eq (cmd, collect_pseudocommand))
	{
	  /* Display the collected data.
	     For the trap frame, display only what was collected at
	     the trap.  Likewise for stepping frames, display only
	     what was collected while stepping.  This means that the
	     two boolean variables, STEPPING_FRAME and
	     STEPPING_ACTIONS should be equal.  */
	  if (stepping_frame == stepping_actions)
	    {
	      do
		{		/* repeat over a comma-separated list */
		  QUIT;		/* allow user to bail out with ^C */
		  if (*action_exp == ',')
		    action_exp++;
		  while (isspace ((int) *action_exp))
		    action_exp++;

		  next_comma = strchr (action_exp, ',');

		  if (0 == strncasecmp (action_exp, "$reg", 4))
		    registers_info (NULL, from_tty);
		  else if (0 == strncasecmp (action_exp, "$loc", 4))
		    locals_info (NULL, from_tty);
		  else if (0 == strncasecmp (action_exp, "$arg", 4))
		    args_info (NULL, from_tty);
		  else
		    {		/* variable */
		      if (next_comma)
			{
			  make_cleanup (replace_comma, next_comma);
			  *next_comma = '\0';
			}
		      printf_filtered ("%s = ", action_exp);
		      output_command (action_exp, from_tty);
		      printf_filtered ("\n");
		    }
		  if (next_comma)
		    *next_comma = ',';
		  action_exp = next_comma;
		}
	      while (action_exp && *action_exp == ',');
	    }
	}
    }
}

/* The tdump command.  */

static void
trace_dump_command (char *args, int from_tty)
{
  struct regcache *regcache;
  struct breakpoint *t;
  int stepping_frame = 0;
  struct bp_location *loc;
  char *line, *default_collect_line = NULL;
  struct command_line *actions, *default_collect_action = NULL;
  struct cleanup *old_chain = NULL;

  if (tracepoint_number == -1)
    {
      warning (_("No current trace frame."));
      return;
    }

  t = get_tracepoint (tracepoint_number);

  if (t == NULL)
    error (_("No known tracepoint matches 'current' tracepoint #%d."),
	   tracepoint_number);

  printf_filtered ("Data collected at tracepoint %d, trace frame %d:\n",
		   tracepoint_number, traceframe_number);

  /* The current frame is a trap frame if the frame PC is equal
     to the tracepoint PC.  If not, then the current frame was
     collected during single-stepping.  */

  regcache = get_current_regcache ();

  /* If the traceframe's address matches any of the tracepoint's
     locations, assume it is a direct hit rather than a while-stepping
     frame.  (FIXME this is not reliable, should record each frame's
     type.)  */
  stepping_frame = 1;
  for (loc = t->loc; loc; loc = loc->next)
    if (loc->address == regcache_read_pc (regcache))
      stepping_frame = 0;

  actions = breakpoint_commands (t);

  /* If there is a default-collect list, make up a collect command,
     prepend to the tracepoint's commands, and pass the whole mess to
     the trace dump scanner.  We need to validate because
     default-collect might have been junked since the trace run.  */
  if (*default_collect)
    {
      default_collect_line = xstrprintf ("collect %s", default_collect);
      old_chain = make_cleanup (xfree, default_collect_line);
      line = default_collect_line;
      validate_actionline (&line, t);
      default_collect_action = xmalloc (sizeof (struct command_line));
      make_cleanup (xfree, default_collect_action);
      default_collect_action->next = actions;
      default_collect_action->line = line;
      actions = default_collect_action;
    }

  trace_dump_actions (actions, 0, stepping_frame, from_tty);

  if (*default_collect)
    do_cleanups (old_chain);
}

/* Encode a piece of a tracepoint's source-level definition in a form
   that is suitable for both protocol and saving in files.  */
/* This version does not do multiple encodes for long strings; it should
   return an offset to the next piece to encode.  FIXME  */

extern int
encode_source_string (int tpnum, ULONGEST addr,
		      char *srctype, char *src, char *buf, int buf_size)
{
  if (80 + strlen (srctype) > buf_size)
    error (_("Buffer too small for source encoding"));
  sprintf (buf, "%x:%s:%s:%x:%x:",
	   tpnum, phex_nz (addr, sizeof (addr)), srctype, 0, (int) strlen (src));
  if (strlen (buf) + strlen (src) * 2 >= buf_size)
    error (_("Source string too long for buffer"));
  bin2hex (src, buf + strlen (buf), 0);
  return -1;
}

extern int trace_regblock_size;

/* Save tracepoint data to file named FILENAME.  If TARGET_DOES_SAVE is
   non-zero, the save is performed on the target, otherwise GDB obtains all
   trace data and saves it locally.  */

void
trace_save (const char *filename, int target_does_save)
{
  struct cleanup *cleanup;
  char *pathname;
  struct trace_status *ts = current_trace_status ();
  int err, status;
  FILE *fp;
  struct uploaded_tp *uploaded_tps = NULL, *utp;
  struct uploaded_tsv *uploaded_tsvs = NULL, *utsv;
  int a;
  char *act;
  LONGEST gotten = 0;
  ULONGEST offset = 0;
#define MAX_TRACE_UPLOAD 2000
  gdb_byte buf[MAX_TRACE_UPLOAD];
  int written;

  /* If the target is to save the data to a file on its own, then just
     send the command and be done with it.  */
  if (target_does_save)
    {
      err = target_save_trace_data (filename);
      if (err < 0)
	error (_("Target failed to save trace data to '%s'."),
	       filename);
      return;
    }

  /* Get the trace status first before opening the file, so if the
     target is losing, we can get out without touching files.  */
  status = target_get_trace_status (ts);

  pathname = tilde_expand (filename);
  cleanup = make_cleanup (xfree, pathname);

  fp = fopen (pathname, "wb");
  if (!fp)
    error (_("Unable to open file '%s' for saving trace data (%s)"),
	   filename, safe_strerror (errno));
  make_cleanup_fclose (fp);

  /* Write a file header, with a high-bit-set char to indicate a
     binary file, plus a hint as what this file is, and a version
     number in case of future needs.  */
  written = fwrite ("\x7fTRACE0\n", 8, 1, fp);
  if (written < 1)
    perror_with_name (pathname);

  /* Write descriptive info.  */

  /* Write out the size of a register block.  */
  fprintf (fp, "R %x\n", trace_regblock_size);

  /* Write out status of the tracing run (aka "tstatus" info).  */
  fprintf (fp, "status %c;%s",
	   (ts->running ? '1' : '0'), stop_reason_names[ts->stop_reason]);
  if (ts->stop_reason == tracepoint_error)
    {
      char *buf = (char *) alloca (strlen (ts->error_desc) * 2 + 1);

      bin2hex ((gdb_byte *) ts->error_desc, buf, 0);
      fprintf (fp, ":%s", buf);
    }
  fprintf (fp, ":%x", ts->stopping_tracepoint);
  if (ts->traceframe_count >= 0)
    fprintf (fp, ";tframes:%x", ts->traceframe_count);
  if (ts->traceframes_created >= 0)
    fprintf (fp, ";tcreated:%x", ts->traceframes_created);
  if (ts->buffer_free >= 0)
    fprintf (fp, ";tfree:%x", ts->buffer_free);
  if (ts->buffer_size >= 0)
    fprintf (fp, ";tsize:%x", ts->buffer_size);
  if (ts->disconnected_tracing)
    fprintf (fp, ";disconn:%x", ts->disconnected_tracing);
  if (ts->circular_buffer)
    fprintf (fp, ";circular:%x", ts->circular_buffer);
  fprintf (fp, "\n");

  /* Note that we want to upload tracepoints and save those, rather
     than simply writing out the local ones, because the user may have
     changed tracepoints in GDB in preparation for a future tracing
     run, or maybe just mass-deleted all types of breakpoints as part
     of cleaning up.  So as not to contaminate the session, leave the
     data in its uploaded form, don't make into real tracepoints.  */

  /* Get trace state variables first, they may be checked when parsing
     uploaded commands.  */

  target_upload_trace_state_variables (&uploaded_tsvs);

  for (utsv = uploaded_tsvs; utsv; utsv = utsv->next)
    {
      char *buf = "";

      if (utsv->name)
	{
	  buf = (char *) xmalloc (strlen (utsv->name) * 2 + 1);
	  bin2hex ((gdb_byte *) (utsv->name), buf, 0);
	}

      fprintf (fp, "tsv %x:%s:%x:%s\n",
	       utsv->number, phex_nz (utsv->initial_value, 8),
	       utsv->builtin, buf);

      if (utsv->name)
	xfree (buf);
    }

  free_uploaded_tsvs (&uploaded_tsvs);

  target_upload_tracepoints (&uploaded_tps);

  for (utp = uploaded_tps; utp; utp = utp->next)
    {
      fprintf (fp, "tp T%x:%s:%c:%x:%x",
	       utp->number, phex_nz (utp->addr, sizeof (utp->addr)),
	       (utp->enabled ? 'E' : 'D'), utp->step, utp->pass);
      if (utp->type == bp_fast_tracepoint)
	fprintf (fp, ":F%x", utp->orig_size);
      if (utp->cond)
	fprintf (fp, ":X%x,%s", (unsigned int) strlen (utp->cond) / 2,
		 utp->cond);
      fprintf (fp, "\n");
      for (a = 0; VEC_iterate (char_ptr, utp->actions, a, act); ++a)
	fprintf (fp, "tp A%x:%s:%s\n",
		 utp->number, phex_nz (utp->addr, sizeof (utp->addr)), act);
      for (a = 0; VEC_iterate (char_ptr, utp->actions, a, act); ++a)
	fprintf (fp, "tp S%x:%s:%s\n",
		 utp->number, phex_nz (utp->addr, sizeof (utp->addr)), act);
      if (utp->at_string)
	{
	  encode_source_string (utp->number, utp->addr,
				"at", utp->at_string, buf, MAX_TRACE_UPLOAD);
	  fprintf (fp, "tp Z%s\n", buf);
	}
      if (utp->cond_string)
	{
	  encode_source_string (utp->number, utp->addr,
				"cond", utp->cond_string, buf, MAX_TRACE_UPLOAD);
	  fprintf (fp, "tp Z%s\n", buf);
	}
      for (a = 0; VEC_iterate (char_ptr, utp->cmd_strings, a, act); ++a)
	{
	  encode_source_string (utp->number, utp->addr, "cmd", act,
				buf, MAX_TRACE_UPLOAD);
	  fprintf (fp, "tp Z%s\n", buf);
	}
    }

  free_uploaded_tps (&uploaded_tps);

  /* Mark the end of the definition section.  */
  fprintf (fp, "\n");

  /* Get and write the trace data proper.  We ask for big blocks, in
     the hopes of efficiency, but will take less if the target has
     packet size limitations or some such.  */
  while (1)
    {
      gotten = target_get_raw_trace_data (buf, offset, MAX_TRACE_UPLOAD);
      if (gotten < 0)
	error (_("Failure to get requested trace buffer data"));
      /* No more data is forthcoming, we're done.  */
      if (gotten == 0)
	break;
      written = fwrite (buf, gotten, 1, fp);
      if (written < 1)
	perror_with_name (pathname);
      offset += gotten;
    }

  /* Mark the end of trace data.  (We know that gotten is 0 at this point.)  */
  written = fwrite (&gotten, 4, 1, fp);
  if (written < 1)
    perror_with_name (pathname);

  do_cleanups (cleanup);
}

static void
trace_save_command (char *args, int from_tty)
{
  int target_does_save = 0;
  char **argv;
  char *filename = NULL;
  struct cleanup *back_to;

  if (args == NULL)
    error_no_arg (_("file in which to save trace data"));

  argv = gdb_buildargv (args);
  back_to = make_cleanup_freeargv (argv);

  for (; *argv; ++argv)
    {
      if (strcmp (*argv, "-r") == 0)
	target_does_save = 1;
      else if (**argv == '-')
	error (_("unknown option `%s'"), *argv);
      else
	filename = *argv;
    }

  if (!filename)
    error_no_arg (_("file in which to save trace data"));

  trace_save (filename, target_does_save);

  if (from_tty)
    printf_filtered (_("Trace data saved to file '%s'.\n"), args);

  do_cleanups (back_to);
}

/* Tell the target what to do with an ongoing tracing run if GDB
   disconnects for some reason.  */

void
send_disconnected_tracing_value (int value)
{
  target_set_disconnected_tracing (value);
}

static void
set_disconnected_tracing (char *args, int from_tty,
			  struct cmd_list_element *c)
{
  send_disconnected_tracing_value (disconnected_tracing);
}

static void
set_circular_trace_buffer (char *args, int from_tty,
			   struct cmd_list_element *c)
{
  target_set_circular_trace_buffer (circular_trace_buffer);
}

/* Convert the memory pointed to by mem into hex, placing result in buf.
 * Return a pointer to the last char put in buf (null)
 * "stolen" from sparc-stub.c
 */

static const char hexchars[] = "0123456789abcdef";

static char *
mem2hex (gdb_byte *mem, char *buf, int count)
{
  gdb_byte ch;

  while (count-- > 0)
    {
      ch = *mem++;

      *buf++ = hexchars[ch >> 4];
      *buf++ = hexchars[ch & 0xf];
    }

  *buf = 0;

  return buf;
}

int
get_traceframe_number (void)
{
  return traceframe_number;
}

/* Make the traceframe NUM be the current trace frame.  Does nothing
   if NUM is already current.  */

void
set_traceframe_number (int num)
{
  int newnum;

  if (traceframe_number == num)
    {
      /* Nothing to do.  */
      return;
    }

  newnum = target_trace_find (tfind_number, num, 0, 0, NULL);

  if (newnum != num)
    warning (_("could not change traceframe"));

  traceframe_number = newnum;

  /* Changing the traceframe changes our view of registers and of the
     frame chain.  */
  registers_changed ();
}

/* A cleanup used when switching away and back from tfind mode.  */

struct current_traceframe_cleanup
{
  /* The traceframe we were inspecting.  */
  int traceframe_number;
};

static void
do_restore_current_traceframe_cleanup (void *arg)
{
  struct current_traceframe_cleanup *old = arg;

  set_traceframe_number (old->traceframe_number);
}

static void
restore_current_traceframe_cleanup_dtor (void *arg)
{
  struct current_traceframe_cleanup *old = arg;

  xfree (old);
}

struct cleanup *
make_cleanup_restore_current_traceframe (void)
{
  struct current_traceframe_cleanup *old;

  old = xmalloc (sizeof (struct current_traceframe_cleanup));
  old->traceframe_number = traceframe_number;

  return make_cleanup_dtor (do_restore_current_traceframe_cleanup, old,
			    restore_current_traceframe_cleanup_dtor);
}

/* Given a number and address, return an uploaded tracepoint with that
   number, creating if necessary.  */

struct uploaded_tp *
get_uploaded_tp (int num, ULONGEST addr, struct uploaded_tp **utpp)
{
  struct uploaded_tp *utp;

  for (utp = *utpp; utp; utp = utp->next)
    if (utp->number == num && utp->addr == addr)
      return utp;
  utp = (struct uploaded_tp *) xmalloc (sizeof (struct uploaded_tp));
  memset (utp, 0, sizeof (struct uploaded_tp));
  utp->number = num;
  utp->addr = addr;
  utp->actions = NULL;
  utp->step_actions = NULL;
  utp->cmd_strings = NULL;
  utp->next = *utpp;
  *utpp = utp;
  return utp;
}

static void
free_uploaded_tps (struct uploaded_tp **utpp)
{
  struct uploaded_tp *next_one;

  while (*utpp)
    {
      next_one = (*utpp)->next;
      xfree (*utpp);
      *utpp = next_one;
    }
}

/* Given a number and address, return an uploaded tracepoint with that
   number, creating if necessary.  */

struct uploaded_tsv *
get_uploaded_tsv (int num, struct uploaded_tsv **utsvp)
{
  struct uploaded_tsv *utsv;

  for (utsv = *utsvp; utsv; utsv = utsv->next)
    if (utsv->number == num)
      return utsv;
  utsv = (struct uploaded_tsv *) xmalloc (sizeof (struct uploaded_tsv));
  memset (utsv, 0, sizeof (struct uploaded_tsv));
  utsv->number = num;
  utsv->next = *utsvp;
  *utsvp = utsv;
  return utsv;
}

static void
free_uploaded_tsvs (struct uploaded_tsv **utsvp)
{
  struct uploaded_tsv *next_one;

  while (*utsvp)
    {
      next_one = (*utsvp)->next;
      xfree (*utsvp);
      *utsvp = next_one;
    }
}

/* Look for an existing tracepoint that seems similar enough to the
   uploaded one.  Enablement isn't compared, because the user can
   toggle that freely, and may have done so in anticipation of the
   next trace run.  */

struct breakpoint *
find_matching_tracepoint (struct uploaded_tp *utp)
{
  VEC(breakpoint_p) *tp_vec = all_tracepoints ();
  int ix;
  struct breakpoint *t;
  struct bp_location *loc;

  for (ix = 0; VEC_iterate (breakpoint_p, tp_vec, ix, t); ix++)
    {
      if (t->type == utp->type
	  && t->step_count == utp->step
	  && t->pass_count == utp->pass
	  /* FIXME also test conditionals and actions */
	  )
	{
	  /* Scan the locations for an address match.  */
	  for (loc = t->loc; loc; loc = loc->next)
	    {
	      if (loc->address == utp->addr)
		return t;
	    }
	}
    }
  return NULL;
}

/* Given a list of tracepoints uploaded from a target, attempt to
   match them up with existing tracepoints, and create new ones if not
   found.  */

void
merge_uploaded_tracepoints (struct uploaded_tp **uploaded_tps)
{
  struct uploaded_tp *utp;
  struct breakpoint *t;

  /* Look for GDB tracepoints that match up with our uploaded versions.  */
  for (utp = *uploaded_tps; utp; utp = utp->next)
    {
      t = find_matching_tracepoint (utp);
      if (t)
	printf_filtered (_("Assuming tracepoint %d is same as target's tracepoint %d at %s.\n"),
			 t->number, utp->number, paddress (get_current_arch (), utp->addr));
      else
	{
	  t = create_tracepoint_from_upload (utp);
	  if (t)
	    printf_filtered (_("Created tracepoint %d for target's tracepoint %d at %s.\n"),
			     t->number, utp->number, paddress (get_current_arch (), utp->addr));
	  else
	    printf_filtered (_("Failed to create tracepoint for target's tracepoint %d at %s, skipping it.\n"),
			     utp->number, paddress (get_current_arch (), utp->addr));
	}
      /* Whether found or created, record the number used by the
	 target, to help with mapping target tracepoints back to their
	 counterparts here.  */
      if (t)
	t->number_on_target = utp->number;
    }

  free_uploaded_tps (uploaded_tps);
}

/* Trace state variables don't have much to identify them beyond their
   name, so just use that to detect matches.  */

struct trace_state_variable *
find_matching_tsv (struct uploaded_tsv *utsv)
{
  if (!utsv->name)
    return NULL;

  return find_trace_state_variable (utsv->name);
}

struct trace_state_variable *
create_tsv_from_upload (struct uploaded_tsv *utsv)
{
  const char *namebase;
  char buf[20];
  int try_num = 0;
  struct trace_state_variable *tsv;

  if (utsv->name)
    {
      namebase = utsv->name;
      sprintf (buf, "%s", namebase);
    }
  else
    {
      namebase = "__tsv";
      sprintf (buf, "%s_%d", namebase, try_num++);
    }

  /* Fish for a name that is not in use.  */
  /* (should check against all internal vars?) */
  while (find_trace_state_variable (buf))
    sprintf (buf, "%s_%d", namebase, try_num++);

  /* We have an available name, create the variable.  */
  tsv = create_trace_state_variable (xstrdup (buf));
  tsv->initial_value = utsv->initial_value;
  tsv->builtin = utsv->builtin;

  return tsv;
}

/* Given a list of uploaded trace state variables, try to match them
   up with existing variables, or create additional ones.  */

void
merge_uploaded_trace_state_variables (struct uploaded_tsv **uploaded_tsvs)
{
  int ix;
  struct uploaded_tsv *utsv;
  struct trace_state_variable *tsv;
  int highest;

  /* Most likely some numbers will have to be reassigned as part of
     the merge, so clear them all in anticipation.  */
  for (ix = 0; VEC_iterate (tsv_s, tvariables, ix, tsv); ++ix)
    tsv->number = 0;

  for (utsv = *uploaded_tsvs; utsv; utsv = utsv->next)
    {
      tsv = find_matching_tsv (utsv);
      if (tsv)
	printf_filtered (_("Assuming trace state variable $%s is same as target's variable %d.\n"),
			 tsv->name, utsv->number);
      else
	{
	  tsv = create_tsv_from_upload (utsv);
	  printf_filtered (_("Created trace state variable $%s for target's variable %d.\n"),
			   tsv->name, utsv->number);
	}
      /* Give precedence to numberings that come from the target.  */
      if (tsv)
	tsv->number = utsv->number;
    }

  /* Renumber everything that didn't get a target-assigned number.  */
  highest = 0;
  for (ix = 0; VEC_iterate (tsv_s, tvariables, ix, tsv); ++ix)
    if (tsv->number > highest)
      highest = tsv->number;

  ++highest;
  for (ix = 0; VEC_iterate (tsv_s, tvariables, ix, tsv); ++ix)
    if (tsv->number == 0)
      tsv->number = highest++;

  free_uploaded_tsvs (uploaded_tsvs);
}

/* target tfile command */

struct target_ops tfile_ops;

/* Fill in tfile_ops with its defined operations and properties.  */

#define TRACE_HEADER_SIZE 8

char *trace_filename;
int trace_fd = -1;
off_t trace_frames_offset;
off_t cur_offset;
int cur_data_size;
int trace_regblock_size;

static void tfile_interp_line (char *line,
			       struct uploaded_tp **utpp,
			       struct uploaded_tsv **utsvp);

static void
tfile_open (char *filename, int from_tty)
{
  char *temp;
  struct cleanup *old_chain;
  int flags;
  int scratch_chan;
  char header[TRACE_HEADER_SIZE];
  char linebuf[1000]; /* should be max remote packet size or so */
  char byte;
  int bytes, i, gotten;
  struct trace_status *ts;
  struct uploaded_tp *uploaded_tps = NULL;
  struct uploaded_tsv *uploaded_tsvs = NULL;

  target_preopen (from_tty);
  if (!filename)
    error (_("No trace file specified."));

  filename = tilde_expand (filename);
  if (!IS_ABSOLUTE_PATH(filename))
    {
      temp = concat (current_directory, "/", filename, (char *) NULL);
      xfree (filename);
      filename = temp;
    }

  old_chain = make_cleanup (xfree, filename);

  flags = O_BINARY | O_LARGEFILE;
  flags |= O_RDONLY;
  scratch_chan = open (filename, flags, 0);
  if (scratch_chan < 0)
    perror_with_name (filename);

  /* Looks semi-reasonable.  Toss the old trace file and work on the new.  */

  discard_cleanups (old_chain);	/* Don't free filename any more */
  unpush_target (&tfile_ops);

  push_target (&tfile_ops);

  trace_filename = xstrdup (filename);
  trace_fd = scratch_chan;

  bytes = 0;
  /* Read the file header and test for validity.  */
  gotten = read (trace_fd, &header, TRACE_HEADER_SIZE);
  if (gotten < 0)
    perror_with_name (trace_filename);
  else if (gotten < TRACE_HEADER_SIZE)
    error (_("Premature end of file while reading trace file"));

  bytes += TRACE_HEADER_SIZE;
  if (!(header[0] == 0x7f
	&& (strncmp (header + 1, "TRACE0\n", 7) == 0)))
    error (_("File is not a valid trace file."));

  trace_regblock_size = 0;
  ts = current_trace_status ();
  /* We know we're working with a file.  */
  ts->from_file = 1;
  /* Set defaults in case there is no status line.  */
  ts->running_known = 0;
  ts->stop_reason = trace_stop_reason_unknown;
  ts->traceframe_count = -1;
  ts->buffer_free = 0;
  ts->disconnected_tracing = 0;
  ts->circular_buffer = 0;

  /* Read through a section of newline-terminated lines that
     define things like tracepoints.  */
  i = 0;
  while (1)
    {
      gotten = read (trace_fd, &byte, 1);
      if (gotten < 0)
	perror_with_name (trace_filename);
      else if (gotten < 1)
	error (_("Premature end of file while reading trace file"));

      ++bytes;
      if (byte == '\n')
	{
	  /* Empty line marks end of the definition section.  */
	  if (i == 0)
	    break;
	  linebuf[i] = '\0';
	  i = 0;
	  tfile_interp_line (linebuf, &uploaded_tps, &uploaded_tsvs);
	}
      else
	linebuf[i++] = byte;
      if (i >= 1000)
	error (_("Excessively long lines in trace file"));
    }

  /* Add the file's tracepoints and variables into the current mix.  */

  /* Get trace state variables first, they may be checked when parsing
     uploaded commands.  */
  merge_uploaded_trace_state_variables (&uploaded_tsvs);

  merge_uploaded_tracepoints (&uploaded_tps);

  /* Record the starting offset of the binary trace data.  */
  trace_frames_offset = bytes;

  /* If we don't have a blocksize, we can't interpret the
     traceframes.  */
  if (trace_regblock_size == 0)
    error (_("No register block size recorded in trace file"));
  if (ts->traceframe_count <= 0)
    {
      warning ("No traceframes present in this file.");
      return;
    }

#define TFILE_PID (1)
  inferior_appeared (current_inferior (), TFILE_PID);
  inferior_ptid = pid_to_ptid (TFILE_PID);
  add_thread_silent (inferior_ptid);

  post_create_inferior (&tfile_ops, from_tty);

#if 0
  /* FIXME this will get defined in MI patch submission */
  tfind_1 (tfind_number, 0, 0, 0, 0);
#endif
}

/* Interpret the given line from the definitions part of the trace
   file.  */

static void
tfile_interp_line (char *line,
		   struct uploaded_tp **utpp, struct uploaded_tsv **utsvp)
{
  char *p = line;

  if (strncmp (p, "R ", strlen ("R ")) == 0)
    {
      p += strlen ("R ");
      trace_regblock_size = strtol (p, &p, 16);
    }
  else if (strncmp (p, "status ", strlen ("status ")) == 0)
    {
      p += strlen ("status ");
      parse_trace_status (p, current_trace_status ());
    }
  else if (strncmp (p, "tp ", strlen ("tp ")) == 0)
    {
      p += strlen ("tp ");
      parse_tracepoint_definition (p, utpp);
    }
  else if (strncmp (p, "tsv ", strlen ("tsv ")) == 0)
    {
      p += strlen ("tsv ");
      parse_tsv_definition (p, utsvp);
    }
  else
    warning ("Ignoring trace file definition \"%s\"", line);
}

/* Parse the part of trace status syntax that is shared between
   the remote protocol and the trace file reader.  */

void
parse_trace_status (char *line, struct trace_status *ts)
{
  char *p = line, *p1, *p2, *p_temp;
  ULONGEST val;

  ts->running_known = 1;
  ts->running = (*p++ == '1');
  ts->stop_reason = trace_stop_reason_unknown;
  xfree (ts->error_desc);
  ts->error_desc = NULL;
  ts->traceframe_count = -1;
  ts->traceframes_created = -1;
  ts->buffer_free = -1;
  ts->buffer_size = -1;
  ts->disconnected_tracing = 0;
  ts->circular_buffer = 0;

  while (*p++)
    {
      p1 = strchr (p, ':');
      if (p1 == NULL)
	error (_("Malformed trace status, at %s\n\
Status line: '%s'\n"), p, line);
      if (strncmp (p, stop_reason_names[trace_buffer_full], p1 - p) == 0)
	{
	  p = unpack_varlen_hex (++p1, &val);
	  ts->stop_reason = trace_buffer_full;
	}
      else if (strncmp (p, stop_reason_names[trace_never_run], p1 - p) == 0)
	{
	  p = unpack_varlen_hex (++p1, &val);
	  ts->stop_reason = trace_never_run;
	}
      else if (strncmp (p, stop_reason_names[tracepoint_passcount], p1 - p) == 0)
	{
	  p = unpack_varlen_hex (++p1, &val);
	  ts->stop_reason = tracepoint_passcount;
	  ts->stopping_tracepoint = val;
	}
      else if (strncmp (p, stop_reason_names[tstop_command], p1 - p) == 0)
	{
	  p = unpack_varlen_hex (++p1, &val);
	  ts->stop_reason = tstop_command;
	}
      else if (strncmp (p, stop_reason_names[trace_disconnected], p1 - p) == 0)
	{
	  p = unpack_varlen_hex (++p1, &val);
	  ts->stop_reason = trace_disconnected;
	}
      else if (strncmp (p, stop_reason_names[tracepoint_error], p1 - p) == 0)
	{
	  p2 = strchr (++p1, ':');
	  if (p2 != p1)
	    {
	      int end;

	      ts->error_desc = xmalloc ((p2 - p1) / 2 + 1);
	      end = hex2bin (p1, ts->error_desc, (p2 - p1) / 2);
	      ts->error_desc[end] = '\0';
	    }
	  else
	    ts->error_desc = xstrdup ("");

	  p = unpack_varlen_hex (++p2, &val);
	  ts->stopping_tracepoint = val;
	  ts->stop_reason = tracepoint_error;
	}
      else if (strncmp (p, "tframes", p1 - p) == 0)
	{
	  p = unpack_varlen_hex (++p1, &val);
	  ts->traceframe_count = val;
	}
      else if (strncmp (p, "tcreated", p1 - p) == 0)
	{
	  p = unpack_varlen_hex (++p1, &val);
	  ts->traceframes_created = val;
	}
      else if (strncmp (p, "tfree", p1 - p) == 0)
	{
	  p = unpack_varlen_hex (++p1, &val);
	  ts->buffer_free = val;
	}
      else if (strncmp (p, "tsize", p1 - p) == 0)
	{
	  p = unpack_varlen_hex (++p1, &val);
	  ts->buffer_size = val;
	}
      else if (strncmp (p, "disconn", p1 - p) == 0)
	{
	  p = unpack_varlen_hex (++p1, &val);
	  ts->disconnected_tracing = val;
	}
      else if (strncmp (p, "circular", p1 - p) == 0)
	{
	  p = unpack_varlen_hex (++p1, &val);
	  ts->circular_buffer = val;
	}
      else
	{
	  /* Silently skip unknown optional info.  */
	  p_temp = strchr (p1 + 1, ';');
	  if (p_temp)
	    p = p_temp;
	  else
	    /* Must be at the end.  */
	    break;
	}
    }
}

/* Given a line of text defining a part of a tracepoint, parse it into
   an "uploaded tracepoint".  */

void
parse_tracepoint_definition (char *line, struct uploaded_tp **utpp)
{
  char *p;
  char piece;
  ULONGEST num, addr, step, pass, orig_size, xlen, start;
  int enabled, end;
  enum bptype type;
  char *cond, *srctype, *buf;
  struct uploaded_tp *utp = NULL;

  p = line;
  /* Both tracepoint and action definitions start with the same number
     and address sequence.  */
  piece = *p++;
  p = unpack_varlen_hex (p, &num);
  p++;  /* skip a colon */
  p = unpack_varlen_hex (p, &addr);
  p++;  /* skip a colon */
  if (piece == 'T')
    {
      enabled = (*p++ == 'E');
      p++;  /* skip a colon */
      p = unpack_varlen_hex (p, &step);
      p++;  /* skip a colon */
      p = unpack_varlen_hex (p, &pass);
      type = bp_tracepoint;
      cond = NULL;
      /* Thumb through optional fields.  */
      while (*p == ':')
	{
	  p++;  /* skip a colon */
	  if (*p == 'F')
	    {
	      type = bp_fast_tracepoint;
	      p++;
	      p = unpack_varlen_hex (p, &orig_size);
	    }
	  else if (*p == 'X')
	    {
	      p++;
	      p = unpack_varlen_hex (p, &xlen);
	      p++;  /* skip a comma */
	      cond = (char *) xmalloc (2 * xlen + 1);
	      strncpy (cond, p, 2 * xlen);
	      cond[2 * xlen] = '\0';
	      p += 2 * xlen;
	    }
	  else
	    warning (_("Unrecognized char '%c' in tracepoint definition, skipping rest"), *p);
	}
      utp = get_uploaded_tp (num, addr, utpp);
      utp->type = type;
      utp->enabled = enabled;
      utp->step = step;
      utp->pass = pass;
      utp->cond = cond;
    }
  else if (piece == 'A')
    {
      utp = get_uploaded_tp (num, addr, utpp);
      VEC_safe_push (char_ptr, utp->actions, xstrdup (p));
    }
  else if (piece == 'S')
    {
      utp = get_uploaded_tp (num, addr, utpp);
      VEC_safe_push (char_ptr, utp->step_actions, xstrdup (p));
    }
  else if (piece == 'Z')
    {
      /* Parse a chunk of source form definition.  */
      utp = get_uploaded_tp (num, addr, utpp);
      srctype = p;
      p = strchr (p, ':');
      p++;  /* skip a colon */
      p = unpack_varlen_hex (p, &start);
      p++;  /* skip a colon */
      p = unpack_varlen_hex (p, &xlen);
      p++;  /* skip a colon */

      buf = alloca (strlen (line));

      end = hex2bin (p, (gdb_byte *) buf, strlen (p) / 2);
      buf[end] = '\0';

      if (strncmp (srctype, "at:", strlen ("at:")) == 0)
	utp->at_string = xstrdup (buf);
      else if (strncmp (srctype, "cond:", strlen ("cond:")) == 0)
	utp->cond_string = xstrdup (buf);
      else if (strncmp (srctype, "cmd:", strlen ("cmd:")) == 0)
	VEC_safe_push (char_ptr, utp->cmd_strings, xstrdup (buf));
    }
  else
    {
      /* Don't error out, the target might be sending us optional
	 info that we don't care about.  */
      warning (_("Unrecognized tracepoint piece '%c', ignoring"), piece);
    }
}

/* Convert a textual description of a trace state variable into an
   uploaded object.  */

void
parse_tsv_definition (char *line, struct uploaded_tsv **utsvp)
{
  char *p, *buf;
  ULONGEST num, initval, builtin;
  int end;
  struct uploaded_tsv *utsv = NULL;

  buf = alloca (strlen (line));

  p = line;
  p = unpack_varlen_hex (p, &num);
  p++; /* skip a colon */
  p = unpack_varlen_hex (p, &initval);
  p++; /* skip a colon */
  p = unpack_varlen_hex (p, &builtin);
  p++; /* skip a colon */
  end = hex2bin (p, (gdb_byte *) buf, strlen (p) / 2);
  buf[end] = '\0';

  utsv = get_uploaded_tsv (num, utsvp);
  utsv->initial_value = initval;
  utsv->builtin = builtin;
  utsv->name = xstrdup (buf);
}

/* Close the trace file and generally clean up.  */

static void
tfile_close (int quitting)
{
  int pid;

  if (trace_fd < 0)
    return;

  pid = ptid_get_pid (inferior_ptid);
  inferior_ptid = null_ptid;	/* Avoid confusion from thread stuff */
  exit_inferior_silent (pid);

  close (trace_fd);
  trace_fd = -1;
  if (trace_filename)
    xfree (trace_filename);
}

static void
tfile_files_info (struct target_ops *t)
{
  /* (it would be useful to mention the name of the file) */
  printf_filtered ("Looking at a trace file.\n");
}

/* The trace status for a file is that tracing can never be run.  */

static int
tfile_get_trace_status (struct trace_status *ts)
{
  /* Other bits of trace status were collected as part of opening the
     trace files, so nothing to do here.  */

  return -1;
}

/* Given the position of a traceframe in the file, figure out what
   address the frame was collected at.  This would normally be the
   value of a collected PC register, but if not available, we
   improvise.  */

static ULONGEST
tfile_get_traceframe_address (off_t tframe_offset)
{
  ULONGEST addr = 0;
  short tpnum;
  struct breakpoint *tp;
  off_t saved_offset = cur_offset;
  int gotten;

  /* FIXME dig pc out of collected registers */

  /* Fall back to using tracepoint address.  */
  lseek (trace_fd, tframe_offset, SEEK_SET);
  gotten = read (trace_fd, &tpnum, 2);
  if (gotten < 0)
    perror_with_name (trace_filename);
  else if (gotten < 2)
    error (_("Premature end of file while reading trace file"));

  tp = get_tracepoint_by_number_on_target (tpnum);
  /* FIXME this is a poor heuristic if multiple locations */
  if (tp && tp->loc)
    addr = tp->loc->address;

  /* Restore our seek position.  */
  cur_offset = saved_offset;
  lseek (trace_fd, cur_offset, SEEK_SET);
  return addr;
}

/* Given a type of search and some parameters, scan the collection of
   traceframes in the file looking for a match.  When found, return
   both the traceframe and tracepoint number, otherwise -1 for
   each.  */

static int
tfile_trace_find (enum trace_find_type type, int num,
		  ULONGEST addr1, ULONGEST addr2, int *tpp)
{
  short tpnum;
  int tfnum = 0, found = 0, gotten;
  int data_size;
  struct breakpoint *tp;
  off_t offset, tframe_offset;
  ULONGEST tfaddr;

  lseek (trace_fd, trace_frames_offset, SEEK_SET);
  offset = trace_frames_offset;
  while (1)
    {
      tframe_offset = offset;
      gotten = read (trace_fd, &tpnum, 2);
      if (gotten < 0)
	perror_with_name (trace_filename);
      else if (gotten < 2)
	error (_("Premature end of file while reading trace file"));
      offset += 2;
      if (tpnum == 0)
	break;
      gotten = read (trace_fd, &data_size, 4);	
      if (gotten < 0)
	perror_with_name (trace_filename);
      else if (gotten < 4)
	error (_("Premature end of file while reading trace file"));
      offset += 4;
      switch (type)
	{
	case tfind_number:
	  if (tfnum == num)
	    found = 1;
	  break;
	case tfind_pc:
	  tfaddr = tfile_get_traceframe_address (tframe_offset);
	  if (tfaddr == addr1)
	    found = 1;
	  break;
	case tfind_tp:
	  tp = get_tracepoint (num);
	  if (tp && tpnum == tp->number_on_target)
	    found = 1;
	  break;
	case tfind_range:
	  tfaddr = tfile_get_traceframe_address (tframe_offset);
	  if (addr1 <= tfaddr && tfaddr <= addr2)
	    found = 1;
	  break;
	case tfind_outside:
	  tfaddr = tfile_get_traceframe_address (tframe_offset);
	  if (!(addr1 <= tfaddr && tfaddr <= addr2))
	    found = 1;
	  break;
	default:
	  internal_error (__FILE__, __LINE__, _("unknown tfind type"));
	}
      if (found)
	{
	  if (tpp)
	    *tpp = tpnum;
	  cur_offset = offset;
	  cur_data_size = data_size;
	  return tfnum;
	}
      /* Skip past the traceframe's data.  */
      lseek (trace_fd, data_size, SEEK_CUR);
      offset += data_size;
      /* Update our own count of traceframes.  */
      ++tfnum;
    }
  /* Did not find what we were looking for.  */
  if (tpp)
    *tpp = -1;
  return -1;
}

/* Look for a block of saved registers in the traceframe, and get the
   requested register from it.  */

static void
tfile_fetch_registers (struct target_ops *ops,
		       struct regcache *regcache, int regno)
{
  struct gdbarch *gdbarch = get_regcache_arch (regcache);
  char block_type;
  int pos, offset, regn, regsize, gotten, pc_regno;
  unsigned short mlen;
  char *regs;

  /* An uninitialized reg size says we're not going to be
     successful at getting register blocks.  */
  if (!trace_regblock_size)
    return;

  regs = alloca (trace_regblock_size);

  lseek (trace_fd, cur_offset, SEEK_SET);
  pos = 0;
  while (pos < cur_data_size)
    {
      gotten = read (trace_fd, &block_type, 1);
      if (gotten < 0)
	perror_with_name (trace_filename);
      else if (gotten < 1)
	error (_("Premature end of file while reading trace file"));

      ++pos;
      switch (block_type)
	{
	case 'R':
	  gotten = read (trace_fd, regs, trace_regblock_size);
	  if (gotten < 0)
	    perror_with_name (trace_filename);
	  else if (gotten < trace_regblock_size)
	    error (_("Premature end of file while reading trace file"));

	  /* Assume the block is laid out in GDB register number order,
	     each register with the size that it has in GDB.  */
	  offset = 0;
	  for (regn = 0; regn < gdbarch_num_regs (gdbarch); regn++)
	    {
	      regsize = register_size (gdbarch, regn);
	      /* Make sure we stay within block bounds.  */
	      if (offset + regsize >= trace_regblock_size)
		break;
	      if (!regcache_valid_p (regcache, regn))
		{
		  if (regno == regn)
		    {
		      regcache_raw_supply (regcache, regno, regs + offset);
		      break;
		    }
		  else if (regno == -1)
		    {
		      regcache_raw_supply (regcache, regn, regs + offset);
		    }
		}
	      offset += regsize;
	    }
	  return;
	case 'M':
	  lseek (trace_fd, 8, SEEK_CUR);
	  gotten = read (trace_fd, &mlen, 2);
	  if (gotten < 0)
	    perror_with_name (trace_filename);
	  else if (gotten < 2)
	    error (_("Premature end of file while reading trace file"));
	  lseek (trace_fd, mlen, SEEK_CUR);
	  pos += (8 + 2 + mlen);
	  break;
	case 'V':
	  lseek (trace_fd, 4 + 8, SEEK_CUR);
	  pos += (4 + 8);
	  break;
	default:
	  error ("Unknown block type '%c' (0x%x) in trace frame",
		 block_type, block_type);
	  break;
	}
    }

  /* We get here if no register data has been found.  Although we
     don't like making up numbers, GDB has all manner of troubles when
     the target says some register is not available.  Filling in with
     zeroes is a reasonable fallback.  */
  for (regn = 0; regn < gdbarch_num_regs (gdbarch); regn++)
    regcache_raw_supply (regcache, regn, NULL);

  /* We can often usefully guess that the PC is going to be the same
     as the address of the tracepoint.  */
  pc_regno = gdbarch_pc_regnum (gdbarch);
  if (pc_regno >= 0 && (regno == -1 || regno == pc_regno))
    {
      struct breakpoint *tp = get_tracepoint (tracepoint_number);

      if (tp && tp->loc)
	{
	  /* But don't try to guess if tracepoint is multi-location...  */
	  if (tp->loc->next)
	    {
	      warning ("Tracepoint %d has multiple locations, cannot infer $pc",
		       tp->number);
	      return;
	    }
	  /* ... or does while-stepping.  */
	  if (tp->step_count > 0)
	    {
	      warning ("Tracepoint %d does while-stepping, cannot infer $pc",
		       tp->number);
	      return;
	    }

	  store_unsigned_integer (regs, register_size (gdbarch, pc_regno),
				  gdbarch_byte_order (gdbarch),
				  tp->loc->address);
	  regcache_raw_supply (regcache, pc_regno, regs);
	}
    }
}

static LONGEST
tfile_xfer_partial (struct target_ops *ops, enum target_object object,
		    const char *annex, gdb_byte *readbuf,
		    const gdb_byte *writebuf, ULONGEST offset, LONGEST len)
{
  char block_type;
  int pos, gotten;
  ULONGEST maddr, amt;
  unsigned short mlen;

  /* We're only doing regular memory for now.  */
  if (object != TARGET_OBJECT_MEMORY)
    return -1;

  if (readbuf == NULL)
    error ("tfile_xfer_partial: trace file is read-only");

  lseek (trace_fd, cur_offset, SEEK_SET);
  pos = 0;
  while (pos < cur_data_size)
    {
      gotten = read (trace_fd, &block_type, 1);
      if (gotten < 0)
	perror_with_name (trace_filename);
      else if (gotten < 1)
	error (_("Premature end of file while reading trace file"));
      ++pos;
      switch (block_type)
	{
	case 'R':
	  lseek (trace_fd, trace_regblock_size, SEEK_CUR);
	  pos += trace_regblock_size;
	  break;
	case 'M':
	  gotten = read (trace_fd, &maddr, 8);
	  if (gotten < 0)
	    perror_with_name (trace_filename);
	  else if (gotten < 8)
	    error (_("Premature end of file while reading trace file"));

	  gotten = read (trace_fd, &mlen, 2);
	  if (gotten < 0)
	    perror_with_name (trace_filename);
	  else if (gotten < 2)
	    error (_("Premature end of file while reading trace file"));
	  /* If the block includes the first part of the desired
	     range, return as much it has; GDB will re-request the
	     remainder, which might be in a different block of this
	     trace frame.  */
	  if (maddr <= offset && offset < (maddr + mlen))
  	    {
	      amt = (maddr + mlen) - offset;
	      if (amt > len)
		amt = len;

	      gotten = read (trace_fd, readbuf, amt);
	      if (gotten < 0)
		perror_with_name (trace_filename);
	      /* While it's acceptable to return less than was
		 originally asked for, it's not acceptable to return
		 less than what this block claims to contain.  */
	      else if (gotten < amt)
		error (_("Premature end of file while reading trace file"));
	      return amt;
  	    }
	  lseek (trace_fd, mlen, SEEK_CUR);
	  pos += (8 + 2 + mlen);
	  break;
	case 'V':
	  lseek (trace_fd, 4 + 8, SEEK_CUR);
	  pos += (4 + 8);
	  break;
	default:
	  error ("Unknown block type '%c' (0x%x) in traceframe",
		 block_type, block_type);
	  break;
	}
    }

  /* It's unduly pedantic to refuse to look at the executable for
     read-only pieces; so do the equivalent of readonly regions aka
     QTro packet.  */
  /* FIXME account for relocation at some point */
  if (exec_bfd)
    {
      asection *s;
      bfd_size_type size;
      bfd_vma lma;

      for (s = exec_bfd->sections; s; s = s->next)
	{
	  if ((s->flags & SEC_LOAD) == 0 ||
	      (s->flags & SEC_READONLY) == 0)
	    continue;

	  lma = s->lma;
	  size = bfd_get_section_size (s);
	  if (lma <= offset && offset < (lma + size))
	    {
	      amt = (lma + size) - offset;
	      if (amt > len)
		amt = len;

	      amt = bfd_get_section_contents (exec_bfd, s,
					      readbuf, offset - lma, amt);
	      return amt;
	    }
	}
    }

  /* Indicate failure to find the requested memory block.  */
  return -1;
}

/* Iterate through the blocks of a trace frame, looking for a 'V'
   block with a matching tsv number.  */

static int
tfile_get_trace_state_variable_value (int tsvnum, LONGEST *val)
{
  char block_type;
  int pos, vnum, gotten;
  unsigned short mlen;

  lseek (trace_fd, cur_offset, SEEK_SET);
  pos = 0;
  while (pos < cur_data_size)
    {
      gotten = read (trace_fd, &block_type, 1);
      if (gotten < 0)
	perror_with_name (trace_filename);
      else if (gotten < 1)
	error (_("Premature end of file while reading trace file"));
      ++pos;
      switch (block_type)
	{
	case 'R':
	  lseek (trace_fd, trace_regblock_size, SEEK_CUR);
	  pos += trace_regblock_size;
	  break;
	case 'M':
	  lseek (trace_fd, 8, SEEK_CUR);
	  gotten = read (trace_fd, &mlen, 2);
	  if (gotten < 0)
	    perror_with_name (trace_filename);
	  else if (gotten < 2)
	    error (_("Premature end of file while reading trace file"));
	  lseek (trace_fd, mlen, SEEK_CUR);
	  pos += (8 + 2 + mlen);
	  break;
	case 'V':
	  gotten = read (trace_fd, &vnum, 4);
	  if (gotten < 0)
	    perror_with_name (trace_filename);
	  else if (gotten < 4)
	    error (_("Premature end of file while reading trace file"));
	  if (tsvnum == vnum)
	    {
	      gotten = read (trace_fd, val, 8);
	      if (gotten < 0)
		perror_with_name (trace_filename);
	      else if (gotten < 8)
		error (_("Premature end of file while reading trace file"));
	      return 1;
	    }
	  lseek (trace_fd, 8, SEEK_CUR);
	  pos += (4 + 8);
	  break;
	default:
	  error ("Unknown block type '%c' (0x%x) in traceframe",
		 block_type, block_type);
	  break;
	}
    }
  /* Didn't find anything.  */
  return 0;
}

static int
tfile_has_all_memory (struct target_ops *ops)
{
  return 1;
}

static int
tfile_has_memory (struct target_ops *ops)
{
  return 1;
}

static int
tfile_has_stack (struct target_ops *ops)
{
  return 1;
}

static int
tfile_has_registers (struct target_ops *ops)
{
  return 1;
}

static void
init_tfile_ops (void)
{
  tfile_ops.to_shortname = "tfile";
  tfile_ops.to_longname = "Local trace dump file";
  tfile_ops.to_doc =
    "Use a trace file as a target.  Specify the filename of the trace file.";
  tfile_ops.to_open = tfile_open;
  tfile_ops.to_close = tfile_close;
  tfile_ops.to_fetch_registers = tfile_fetch_registers;
  tfile_ops.to_xfer_partial = tfile_xfer_partial;
  tfile_ops.to_files_info = tfile_files_info;
  tfile_ops.to_get_trace_status = tfile_get_trace_status;
  tfile_ops.to_trace_find = tfile_trace_find;
  tfile_ops.to_get_trace_state_variable_value = tfile_get_trace_state_variable_value;
  /* core_stratum might seem more logical, but GDB doesn't like having
     more than one core_stratum vector.  */
  tfile_ops.to_stratum = process_stratum;
  tfile_ops.to_has_all_memory = tfile_has_all_memory;
  tfile_ops.to_has_memory = tfile_has_memory;
  tfile_ops.to_has_stack = tfile_has_stack;
  tfile_ops.to_has_registers = tfile_has_registers;
  tfile_ops.to_magic = OPS_MAGIC;
}

/* module initialization */
void
_initialize_tracepoint (void)
{
  struct cmd_list_element *c;

  traceframe_number = -1;
  tracepoint_number = -1;

  if (tracepoint_list.list == NULL)
    {
      tracepoint_list.listsize = 128;
      tracepoint_list.list = xmalloc
	(tracepoint_list.listsize * sizeof (struct memrange));
    }
  if (tracepoint_list.aexpr_list == NULL)
    {
      tracepoint_list.aexpr_listsize = 128;
      tracepoint_list.aexpr_list = xmalloc
	(tracepoint_list.aexpr_listsize * sizeof (struct agent_expr *));
    }

  if (stepping_list.list == NULL)
    {
      stepping_list.listsize = 128;
      stepping_list.list = xmalloc
	(stepping_list.listsize * sizeof (struct memrange));
    }

  if (stepping_list.aexpr_list == NULL)
    {
      stepping_list.aexpr_listsize = 128;
      stepping_list.aexpr_list = xmalloc
	(stepping_list.aexpr_listsize * sizeof (struct agent_expr *));
    }

  add_info ("scope", scope_info,
	    _("List the variables local to a scope"));

  add_cmd ("tracepoints", class_trace, NULL,
	   _("Tracing of program execution without stopping the program."),
	   &cmdlist);

  add_com ("tdump", class_trace, trace_dump_command,
	   _("Print everything collected at the current tracepoint."));

  add_com ("tsave", class_trace, trace_save_command, _("\
Save the trace data to a file.\n\
Use the '-r' option to direct the target to save directly to the file,\n\
using its own filesystem."));

  c = add_com ("tvariable", class_trace, trace_variable_command,_("\
Define a trace state variable.\n\
Argument is a $-prefixed name, optionally followed\n\
by '=' and an expression that sets the initial value\n\
at the start of tracing."));
  set_cmd_completer (c, expression_completer);

  add_cmd ("tvariable", class_trace, delete_trace_variable_command, _("\
Delete one or more trace state variables.\n\
Arguments are the names of the variables to delete.\n\
If no arguments are supplied, delete all variables."), &deletelist);
  /* FIXME add a trace variable completer */

  add_info ("tvariables", tvariables_info, _("\
Status of trace state variables and their values.\n\
"));

  add_prefix_cmd ("tfind", class_trace, trace_find_command, _("\
Select a trace frame;\n\
No argument means forward by one frame; '-' means backward by one frame."),
		  &tfindlist, "tfind ", 1, &cmdlist);

  add_cmd ("outside", class_trace, trace_find_outside_command, _("\
Select a trace frame whose PC is outside the given range (exclusive).\n\
Usage: tfind outside addr1, addr2"),
	   &tfindlist);

  add_cmd ("range", class_trace, trace_find_range_command, _("\
Select a trace frame whose PC is in the given range (inclusive).\n\
Usage: tfind range addr1,addr2"),
	   &tfindlist);

  add_cmd ("line", class_trace, trace_find_line_command, _("\
Select a trace frame by source line.\n\
Argument can be a line number (with optional source file), \n\
a function name, or '*' followed by an address.\n\
Default argument is 'the next source line that was traced'."),
	   &tfindlist);

  add_cmd ("tracepoint", class_trace, trace_find_tracepoint_command, _("\
Select a trace frame by tracepoint number.\n\
Default is the tracepoint for the current trace frame."),
	   &tfindlist);

  add_cmd ("pc", class_trace, trace_find_pc_command, _("\
Select a trace frame by PC.\n\
Default is the current PC, or the PC of the current trace frame."),
	   &tfindlist);

  add_cmd ("end", class_trace, trace_find_end_command, _("\
Synonym for 'none'.\n\
De-select any trace frame and resume 'live' debugging."),
	   &tfindlist);

  add_cmd ("none", class_trace, trace_find_none_command,
	   _("De-select any trace frame and resume 'live' debugging."),
	   &tfindlist);

  add_cmd ("start", class_trace, trace_find_start_command,
	   _("Select the first trace frame in the trace buffer."),
	   &tfindlist);

  add_com ("tstatus", class_trace, trace_status_command,
	   _("Display the status of the current trace data collection."));

  add_com ("tstop", class_trace, trace_stop_command,
	   _("Stop trace data collection."));

  add_com ("tstart", class_trace, trace_start_command,
	   _("Start trace data collection."));

  add_com ("end", class_trace, end_actions_pseudocommand, _("\
Ends a list of commands or actions.\n\
Several GDB commands allow you to enter a list of commands or actions.\n\
Entering \"end\" on a line by itself is the normal way to terminate\n\
such a list.\n\n\
Note: the \"end\" command cannot be used at the gdb prompt."));

  add_com ("while-stepping", class_trace, while_stepping_pseudocommand, _("\
Specify single-stepping behavior at a tracepoint.\n\
Argument is number of instructions to trace in single-step mode\n\
following the tracepoint.  This command is normally followed by\n\
one or more \"collect\" commands, to specify what to collect\n\
while single-stepping.\n\n\
Note: this command can only be used in a tracepoint \"actions\" list."));

  add_com_alias ("ws", "while-stepping", class_alias, 0);
  add_com_alias ("stepping", "while-stepping", class_alias, 0);

  add_com ("collect", class_trace, collect_pseudocommand, _("\
Specify one or more data items to be collected at a tracepoint.\n\
Accepts a comma-separated list of (one or more) expressions.  GDB will\n\
collect all data (variables, registers) referenced by that expression.\n\
Also accepts the following special arguments:\n\
    $regs   -- all registers.\n\
    $args   -- all function arguments.\n\
    $locals -- all variables local to the block/function scope.\n\
Note: this command can only be used in a tracepoint \"actions\" list."));

  add_com ("teval", class_trace, teval_pseudocommand, _("\
Specify one or more expressions to be evaluated at a tracepoint.\n\
Accepts a comma-separated list of (one or more) expressions.\n\
The result of each evaluation will be discarded.\n\
Note: this command can only be used in a tracepoint \"actions\" list."));

  add_com ("actions", class_trace, trace_actions_command, _("\
Specify the actions to be taken at a tracepoint.\n\
Tracepoint actions may include collecting of specified data, \n\
single-stepping, or enabling/disabling other tracepoints, \n\
depending on target's capabilities."));

  default_collect = xstrdup ("");
  add_setshow_string_cmd ("default-collect", class_trace,
			  &default_collect, _("\
Set the list of expressions to collect by default"), _("\
Show the list of expressions to collect by default"), NULL,
			  NULL, NULL,
			  &setlist, &showlist);

  add_setshow_boolean_cmd ("disconnected-tracing", no_class,
			   &disconnected_tracing, _("\
Set whether tracing continues after GDB disconnects."), _("\
Show whether tracing continues after GDB disconnects."), _("\
Use this to continue a tracing run even if GDB disconnects\n\
or detaches from the target.  You can reconnect later and look at\n\
trace data collected in the meantime."),
			   set_disconnected_tracing,
			   NULL,
			   &setlist,
			   &showlist);

  add_setshow_boolean_cmd ("circular-trace-buffer", no_class,
			   &circular_trace_buffer, _("\
Set target's use of circular trace buffer."), _("\
Show target's use of circular trace buffer."), _("\
Use this to make the trace buffer into a circular buffer,\n\
which will discard traceframes (oldest first) instead of filling\n\
up and stopping the trace run."),
			   set_circular_trace_buffer,
			   NULL,
			   &setlist,
			   &showlist);

  init_tfile_ops ();

  add_target (&tfile_ops);
}

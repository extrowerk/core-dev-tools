/* General utility routines for GDB, the GNU debugger.

   Copyright (C) 1986, 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996,
   1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008,
   2009 Free Software Foundation, Inc.

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
#include "gdb_assert.h"
#include <ctype.h>
#include "gdb_string.h"
#include "event-top.h"
#include "exceptions.h"
#include "gdbthread.h"

#ifdef TUI
#include "tui/tui.h"		/* For tui_get_command_dimension.   */
#endif

#ifdef __GO32__
#include <pc.h>
#endif

/* SunOS's curses.h has a '#define reg register' in it.  Thank you Sun. */
#ifdef reg
#undef reg
#endif

#include <signal.h>
#include "gdbcmd.h"
#include "serial.h"
#include "bfd.h"
#include "target.h"
#include "demangle.h"
#include "expression.h"
#include "language.h"
#include "charset.h"
#include "annotate.h"
#include "filenames.h"
#include "symfile.h"
#include "gdb_obstack.h"
#include "gdbcore.h"
#include "top.h"

#include "inferior.h"		/* for signed_pointer_to_address */

#include <sys/param.h>		/* For MAXPATHLEN */

#include "gdb_curses.h"

#include "readline/readline.h"

#include <sys/time.h>
#include <time.h>

#include "gdb_usleep.h"

#if !HAVE_DECL_MALLOC
extern PTR malloc ();		/* ARI: PTR */
#endif
#if !HAVE_DECL_REALLOC
extern PTR realloc ();		/* ARI: PTR */
#endif
#if !HAVE_DECL_FREE
extern void free ();
#endif

/* readline defines this.  */
#undef savestring

void (*deprecated_error_begin_hook) (void);

/* Prototypes for local functions */

static void vfprintf_maybe_filtered (struct ui_file *, const char *,
				     va_list, int) ATTR_FORMAT (printf, 2, 0);

static void fputs_maybe_filtered (const char *, struct ui_file *, int);

static void do_my_cleanups (struct cleanup **, struct cleanup *);

static void prompt_for_continue (void);

static void set_screen_size (void);
static void set_width (void);

/* A flag indicating whether to timestamp debugging messages.  */

static int debug_timestamp = 0;

/* Chain of cleanup actions established with make_cleanup,
   to be executed if an error happens.  */

static struct cleanup *cleanup_chain;	/* cleaned up after a failed command */
static struct cleanup *final_cleanup_chain;	/* cleaned up when gdb exits */

/* Nonzero if we have job control. */

int job_control;

/* Nonzero means a quit has been requested.  */

int quit_flag;

/* Nonzero means quit immediately if Control-C is typed now, rather
   than waiting until QUIT is executed.  Be careful in setting this;
   code which executes with immediate_quit set has to be very careful
   about being able to deal with being interrupted at any time.  It is
   almost always better to use QUIT; the only exception I can think of
   is being able to quit out of a system call (using EINTR loses if
   the SIGINT happens between the previous QUIT and the system call).
   To immediately quit in the case in which a SIGINT happens between
   the previous QUIT and setting immediate_quit (desirable anytime we
   expect to block), call QUIT after setting immediate_quit.  */

int immediate_quit;

/* Nonzero means that encoded C++/ObjC names should be printed out in their
   C++/ObjC form rather than raw.  */

int demangle = 1;
static void
show_demangle (struct ui_file *file, int from_tty,
	       struct cmd_list_element *c, const char *value)
{
  fprintf_filtered (file, _("\
Demangling of encoded C++/ObjC names when displaying symbols is %s.\n"),
		    value);
}

/* Nonzero means that encoded C++/ObjC names should be printed out in their
   C++/ObjC form even in assembler language displays.  If this is set, but
   DEMANGLE is zero, names are printed raw, i.e. DEMANGLE controls.  */

int asm_demangle = 0;
static void
show_asm_demangle (struct ui_file *file, int from_tty,
		   struct cmd_list_element *c, const char *value)
{
  fprintf_filtered (file, _("\
Demangling of C++/ObjC names in disassembly listings is %s.\n"),
		    value);
}

/* Nonzero means that strings with character values >0x7F should be printed
   as octal escapes.  Zero means just print the value (e.g. it's an
   international character, and the terminal or window can cope.)  */

int sevenbit_strings = 0;
static void
show_sevenbit_strings (struct ui_file *file, int from_tty,
		       struct cmd_list_element *c, const char *value)
{
  fprintf_filtered (file, _("\
Printing of 8-bit characters in strings as \\nnn is %s.\n"),
		    value);
}

/* String to be printed before error messages, if any.  */

char *error_pre_print;

/* String to be printed before quit messages, if any.  */

char *quit_pre_print;

/* String to be printed before warning messages, if any.  */

char *warning_pre_print = "\nwarning: ";

int pagination_enabled = 1;
static void
show_pagination_enabled (struct ui_file *file, int from_tty,
			 struct cmd_list_element *c, const char *value)
{
  fprintf_filtered (file, _("State of pagination is %s.\n"), value);
}



/* Add a new cleanup to the cleanup_chain,
   and return the previous chain pointer
   to be passed later to do_cleanups or discard_cleanups.
   Args are FUNCTION to clean up with, and ARG to pass to it.  */

struct cleanup *
make_cleanup (make_cleanup_ftype *function, void *arg)
{
  return make_my_cleanup (&cleanup_chain, function, arg);
}

struct cleanup *
make_cleanup_dtor (make_cleanup_ftype *function, void *arg,
		   void (*dtor) (void *))
{
  return make_my_cleanup2 (&cleanup_chain,
			   function, arg, dtor);
}

struct cleanup *
make_final_cleanup (make_cleanup_ftype *function, void *arg)
{
  return make_my_cleanup (&final_cleanup_chain, function, arg);
}

static void
do_freeargv (void *arg)
{
  freeargv ((char **) arg);
}

struct cleanup *
make_cleanup_freeargv (char **arg)
{
  return make_my_cleanup (&cleanup_chain, do_freeargv, arg);
}

static void
do_bfd_close_cleanup (void *arg)
{
  bfd_close (arg);
}

struct cleanup *
make_cleanup_bfd_close (bfd *abfd)
{
  return make_cleanup (do_bfd_close_cleanup, abfd);
}

static void
do_close_cleanup (void *arg)
{
  int *fd = arg;
  close (*fd);
}

struct cleanup *
make_cleanup_close (int fd)
{
  int *saved_fd = xmalloc (sizeof (fd));
  *saved_fd = fd;
  return make_cleanup_dtor (do_close_cleanup, saved_fd, xfree);
}

/* Helper function which does the work for make_cleanup_fclose.  */

static void
do_fclose_cleanup (void *arg)
{
  fclose (arg);
}

/* Return a new cleanup that closes FILE.  */

struct cleanup *
make_cleanup_fclose (FILE *file)
{
  return make_cleanup (do_fclose_cleanup, file);
}

/* Helper function which does the work for make_cleanup_obstack_free.  */

static void
do_obstack_free (void *arg)
{
  struct obstack *ob = arg;
  obstack_free (ob, NULL);
}

/* Return a new cleanup that frees OBSTACK.  */

struct cleanup *
make_cleanup_obstack_free (struct obstack *obstack)
{
  return make_cleanup (do_obstack_free, obstack);
}

static void
do_ui_file_delete (void *arg)
{
  ui_file_delete (arg);
}

struct cleanup *
make_cleanup_ui_file_delete (struct ui_file *arg)
{
  return make_my_cleanup (&cleanup_chain, do_ui_file_delete, arg);
}

static void
do_free_section_addr_info (void *arg)
{
  free_section_addr_info (arg);
}

struct cleanup *
make_cleanup_free_section_addr_info (struct section_addr_info *addrs)
{
  return make_my_cleanup (&cleanup_chain, do_free_section_addr_info, addrs);
}

struct restore_integer_closure
{
  int *variable;
  int value;
};

static void
restore_integer (void *p)
{
  struct restore_integer_closure *closure = p;
  *(closure->variable) = closure->value;
}

/* Remember the current value of *VARIABLE and make it restored when the cleanup
   is run.  */
struct cleanup *
make_cleanup_restore_integer (int *variable)
{
  struct restore_integer_closure *c =
    xmalloc (sizeof (struct restore_integer_closure));
  c->variable = variable;
  c->value = *variable;

  return make_my_cleanup2 (&cleanup_chain, restore_integer, (void *)c,
			   xfree);
}

struct cleanup *
make_my_cleanup2 (struct cleanup **pmy_chain, make_cleanup_ftype *function,
		  void *arg,  void (*free_arg) (void *))
{
  struct cleanup *new
    = (struct cleanup *) xmalloc (sizeof (struct cleanup));
  struct cleanup *old_chain = *pmy_chain;

  new->next = *pmy_chain;
  new->function = function;
  new->free_arg = free_arg;
  new->arg = arg;
  *pmy_chain = new;

  return old_chain;
}

struct cleanup *
make_my_cleanup (struct cleanup **pmy_chain, make_cleanup_ftype *function,
		 void *arg)
{
  return make_my_cleanup2 (pmy_chain, function, arg, NULL);
}

/* Discard cleanups and do the actions they describe
   until we get back to the point OLD_CHAIN in the cleanup_chain.  */

void
do_cleanups (struct cleanup *old_chain)
{
  do_my_cleanups (&cleanup_chain, old_chain);
}

void
do_final_cleanups (struct cleanup *old_chain)
{
  do_my_cleanups (&final_cleanup_chain, old_chain);
}

static void
do_my_cleanups (struct cleanup **pmy_chain,
		struct cleanup *old_chain)
{
  struct cleanup *ptr;
  while ((ptr = *pmy_chain) != old_chain)
    {
      *pmy_chain = ptr->next;	/* Do this first incase recursion */
      (*ptr->function) (ptr->arg);
      if (ptr->free_arg)
	(*ptr->free_arg) (ptr->arg);
      xfree (ptr);
    }
}

/* Discard cleanups, not doing the actions they describe,
   until we get back to the point OLD_CHAIN in the cleanup_chain.  */

void
discard_cleanups (struct cleanup *old_chain)
{
  discard_my_cleanups (&cleanup_chain, old_chain);
}

void
discard_final_cleanups (struct cleanup *old_chain)
{
  discard_my_cleanups (&final_cleanup_chain, old_chain);
}

void
discard_my_cleanups (struct cleanup **pmy_chain,
		     struct cleanup *old_chain)
{
  struct cleanup *ptr;
  while ((ptr = *pmy_chain) != old_chain)
    {
      *pmy_chain = ptr->next;
      if (ptr->free_arg)
	(*ptr->free_arg) (ptr->arg);
      xfree (ptr);
    }
}

/* Set the cleanup_chain to 0, and return the old cleanup chain.  */
struct cleanup *
save_cleanups (void)
{
  return save_my_cleanups (&cleanup_chain);
}

struct cleanup *
save_final_cleanups (void)
{
  return save_my_cleanups (&final_cleanup_chain);
}

struct cleanup *
save_my_cleanups (struct cleanup **pmy_chain)
{
  struct cleanup *old_chain = *pmy_chain;

  *pmy_chain = 0;
  return old_chain;
}

/* Restore the cleanup chain from a previously saved chain.  */
void
restore_cleanups (struct cleanup *chain)
{
  restore_my_cleanups (&cleanup_chain, chain);
}

void
restore_final_cleanups (struct cleanup *chain)
{
  restore_my_cleanups (&final_cleanup_chain, chain);
}

void
restore_my_cleanups (struct cleanup **pmy_chain, struct cleanup *chain)
{
  *pmy_chain = chain;
}

/* This function is useful for cleanups.
   Do

   foo = xmalloc (...);
   old_chain = make_cleanup (free_current_contents, &foo);

   to arrange to free the object thus allocated.  */

void
free_current_contents (void *ptr)
{
  void **location = ptr;
  if (location == NULL)
    internal_error (__FILE__, __LINE__,
		    _("free_current_contents: NULL pointer"));
  if (*location != NULL)
    {
      xfree (*location);
      *location = NULL;
    }
}

/* Provide a known function that does nothing, to use as a base for
   for a possibly long chain of cleanups.  This is useful where we
   use the cleanup chain for handling normal cleanups as well as dealing
   with cleanups that need to be done as a result of a call to error().
   In such cases, we may not be certain where the first cleanup is, unless
   we have a do-nothing one to always use as the base. */

void
null_cleanup (void *arg)
{
}

/* Continuations are implemented as cleanups internally.  Inherit from
   cleanups.  */
struct continuation
{
  struct cleanup base;
};

/* Add a continuation to the continuation list of THREAD.  The new
   continuation will be added at the front.  */
void
add_continuation (struct thread_info *thread,
		  void (*continuation_hook) (void *), void *args,
		  void (*continuation_free_args) (void *))
{
  struct cleanup *as_cleanup = &thread->continuations->base;
  make_cleanup_ftype *continuation_hook_fn = continuation_hook;

  make_my_cleanup2 (&as_cleanup,
		    continuation_hook_fn,
		    args,
		    continuation_free_args);

  thread->continuations = (struct continuation *) as_cleanup;
}

/* Add a continuation to the continuation list of INFERIOR.  The new
   continuation will be added at the front.  */

void
add_inferior_continuation (void (*continuation_hook) (void *), void *args,
			   void (*continuation_free_args) (void *))
{
  struct inferior *inf = current_inferior ();
  struct cleanup *as_cleanup = &inf->continuations->base;
  make_cleanup_ftype *continuation_hook_fn = continuation_hook;

  make_my_cleanup2 (&as_cleanup,
		    continuation_hook_fn,
		    args,
		    continuation_free_args);

  inf->continuations = (struct continuation *) as_cleanup;
}

/* Do all continuations of the current inferior.  */

void
do_all_inferior_continuations (void)
{
  struct cleanup *as_cleanup;
  struct inferior *inf = current_inferior ();

  if (inf->continuations == NULL)
    return;

  /* Copy the list header into another pointer, and set the global
     list header to null, so that the global list can change as a side
     effect of invoking the continuations and the processing of the
     preexisting continuations will not be affected.  */

  as_cleanup = &inf->continuations->base;
  inf->continuations = NULL;

  /* Work now on the list we have set aside.  */
  do_my_cleanups (&as_cleanup, NULL);
}

/* Get rid of all the inferior-wide continuations of INF.  */

void
discard_all_inferior_continuations (struct inferior *inf)
{
  struct cleanup *continuation_ptr = &inf->continuations->base;
  discard_my_cleanups (&continuation_ptr, NULL);
  inf->continuations = NULL;
}

static void
restore_thread_cleanup (void *arg)
{
  ptid_t *ptid_p = arg;
  switch_to_thread (*ptid_p);
}

/* Walk down the continuation list of PTID, and execute all the
   continuations.  There is a problem though.  In some cases new
   continuations may be added while we are in the middle of this loop.
   If this happens they will be added in the front, and done before we
   have a chance of exhausting those that were already there.  We need
   to then save the beginning of the list in a pointer and do the
   continuations from there on, instead of using the global beginning
   of list as our iteration pointer.  */
static void
do_all_continuations_ptid (ptid_t ptid,
			   struct continuation **continuations_p)
{
  struct cleanup *old_chain;
  ptid_t current_thread;
  struct cleanup *as_cleanup;

  if (*continuations_p == NULL)
    return;

  current_thread = inferior_ptid;

  /* Restore selected thread on exit.  Don't try to restore the frame
     as well, because:

    - When running continuations, the selected frame is always #0.

    - The continuations may trigger symbol file loads, which may
      change the frame layout (frame ids change), which would trigger
      a warning if we used make_cleanup_restore_current_thread.  */

  old_chain = make_cleanup (restore_thread_cleanup, &current_thread);

  /* Let the continuation see this thread as selected.  */
  switch_to_thread (ptid);

  /* Copy the list header into another pointer, and set the global
     list header to null, so that the global list can change as a side
     effect of invoking the continuations and the processing of the
     preexisting continuations will not be affected.  */

  as_cleanup = &(*continuations_p)->base;
  *continuations_p = NULL;

  /* Work now on the list we have set aside.  */
  do_my_cleanups (&as_cleanup, NULL);

  do_cleanups (old_chain);
}

/* Callback for iterate over threads.  */
static int
do_all_continuations_thread_callback (struct thread_info *thread, void *data)
{
  do_all_continuations_ptid (thread->ptid, &thread->continuations);
  return 0;
}

/* Do all continuations of thread THREAD.  */
void
do_all_continuations_thread (struct thread_info *thread)
{
  do_all_continuations_thread_callback (thread, NULL);
}

/* Do all continuations of all threads.  */
void
do_all_continuations (void)
{
  iterate_over_threads (do_all_continuations_thread_callback, NULL);
}

/* Callback for iterate over threads.  */
static int
discard_all_continuations_thread_callback (struct thread_info *thread,
					   void *data)
{
  struct cleanup *continuation_ptr = &thread->continuations->base;
  discard_my_cleanups (&continuation_ptr, NULL);
  thread->continuations = NULL;
  return 0;
}

/* Get rid of all the continuations of THREAD.  */
void
discard_all_continuations_thread (struct thread_info *thread)
{
  discard_all_continuations_thread_callback (thread, NULL);
}

/* Get rid of all the continuations of all threads.  */
void
discard_all_continuations (void)
{
  iterate_over_threads (discard_all_continuations_thread_callback, NULL);
}


/* Add a continuation to the intermediate continuation list of THREAD.
   The new continuation will be added at the front.  */
void
add_intermediate_continuation (struct thread_info *thread,
			       void (*continuation_hook)
			       (void *), void *args,
			       void (*continuation_free_args) (void *))
{
  struct cleanup *as_cleanup = &thread->intermediate_continuations->base;
  make_cleanup_ftype *continuation_hook_fn = continuation_hook;

  make_my_cleanup2 (&as_cleanup,
		    continuation_hook_fn,
		    args,
		    continuation_free_args);

  thread->intermediate_continuations = (struct continuation *) as_cleanup;
}

/* Walk down the cmd_continuation list, and execute all the
   continuations. There is a problem though. In some cases new
   continuations may be added while we are in the middle of this
   loop. If this happens they will be added in the front, and done
   before we have a chance of exhausting those that were already
   there. We need to then save the beginning of the list in a pointer
   and do the continuations from there on, instead of using the
   global beginning of list as our iteration pointer.*/
static int
do_all_intermediate_continuations_thread_callback (struct thread_info *thread,
						   void *data)
{
  do_all_continuations_ptid (thread->ptid,
			     &thread->intermediate_continuations);
  return 0;
}

/* Do all intermediate continuations of thread THREAD.  */
void
do_all_intermediate_continuations_thread (struct thread_info *thread)
{
  do_all_intermediate_continuations_thread_callback (thread, NULL);
}

/* Do all intermediate continuations of all threads.  */
void
do_all_intermediate_continuations (void)
{
  iterate_over_threads (do_all_intermediate_continuations_thread_callback, NULL);
}

/* Callback for iterate over threads.  */
static int
discard_all_intermediate_continuations_thread_callback (struct thread_info *thread,
							void *data)
{
  struct cleanup *continuation_ptr = &thread->intermediate_continuations->base;
  discard_my_cleanups (&continuation_ptr, NULL);
  thread->intermediate_continuations = NULL;
  return 0;
}

/* Get rid of all the intermediate continuations of THREAD.  */
void
discard_all_intermediate_continuations_thread (struct thread_info *thread)
{
  discard_all_intermediate_continuations_thread_callback (thread, NULL);
}

/* Get rid of all the intermediate continuations of all threads.  */
void
discard_all_intermediate_continuations (void)
{
  iterate_over_threads (discard_all_intermediate_continuations_thread_callback, NULL);
}



/* Print a warning message.  The first argument STRING is the warning
   message, used as an fprintf format string, the second is the
   va_list of arguments for that string.  A warning is unfiltered (not
   paginated) so that the user does not need to page through each
   screen full of warnings when there are lots of them.  */

void
vwarning (const char *string, va_list args)
{
  if (deprecated_warning_hook)
    (*deprecated_warning_hook) (string, args);
  else
    {
      target_terminal_ours ();
      wrap_here ("");		/* Force out any buffered output */
      gdb_flush (gdb_stdout);
      if (warning_pre_print)
	fputs_unfiltered (warning_pre_print, gdb_stderr);
      vfprintf_unfiltered (gdb_stderr, string, args);
      fprintf_unfiltered (gdb_stderr, "\n");
      va_end (args);
    }
}

/* Print a warning message.
   The first argument STRING is the warning message, used as a fprintf string,
   and the remaining args are passed as arguments to it.
   The primary difference between warnings and errors is that a warning
   does not force the return to command level.  */

void
warning (const char *string, ...)
{
  va_list args;
  va_start (args, string);
  vwarning (string, args);
  va_end (args);
}

/* Print an error message and return to command level.
   The first argument STRING is the error message, used as a fprintf string,
   and the remaining args are passed as arguments to it.  */

NORETURN void
verror (const char *string, va_list args)
{
  throw_verror (GENERIC_ERROR, string, args);
}

NORETURN void
error (const char *string, ...)
{
  va_list args;
  va_start (args, string);
  throw_verror (GENERIC_ERROR, string, args);
  va_end (args);
}

/* Print an error message and quit.
   The first argument STRING is the error message, used as a fprintf string,
   and the remaining args are passed as arguments to it.  */

NORETURN void
vfatal (const char *string, va_list args)
{
  throw_vfatal (string, args);
}

NORETURN void
fatal (const char *string, ...)
{
  va_list args;
  va_start (args, string);
  throw_vfatal (string, args);
  va_end (args);
}

NORETURN void
error_stream (struct ui_file *stream)
{
  long len;
  char *message = ui_file_xstrdup (stream, &len);
  make_cleanup (xfree, message);
  error (("%s"), message);
}

/* Allow the user to configure the debugger behavior with respect to
   what to do when an internal problem is detected.  */

const char internal_problem_ask[] = "ask";
const char internal_problem_yes[] = "yes";
const char internal_problem_no[] = "no";
static const char *internal_problem_modes[] =
{
  internal_problem_ask,
  internal_problem_yes,
  internal_problem_no,
  NULL
};

/* Print a message reporting an internal error/warning. Ask the user
   if they want to continue, dump core, or just exit.  Return
   something to indicate a quit.  */

struct internal_problem
{
  const char *name;
  const char *should_quit;
  const char *should_dump_core;
};

/* Report a problem, internal to GDB, to the user.  Once the problem
   has been reported, and assuming GDB didn't quit, the caller can
   either allow execution to resume or throw an error.  */

static void ATTR_FORMAT (printf, 4, 0)
internal_vproblem (struct internal_problem *problem,
		   const char *file, int line, const char *fmt, va_list ap)
{
  static int dejavu;
  int quit_p;
  int dump_core_p;
  char *reason;

  /* Don't allow infinite error/warning recursion.  */
  {
    static char msg[] = "Recursive internal problem.\n";
    switch (dejavu)
      {
      case 0:
	dejavu = 1;
	break;
      case 1:
	dejavu = 2;
	fputs_unfiltered (msg, gdb_stderr);
	abort ();	/* NOTE: GDB has only four calls to abort().  */
      default:
	dejavu = 3;
        /* Newer GLIBC versions put the warn_unused_result attribute
           on write, but this is one of those rare cases where
           ignoring the return value is correct.  Casting to (void)
           does not fix this problem.  This is the solution suggested
           at http://gcc.gnu.org/bugzilla/show_bug.cgi?id=25509.  */
	if (write (STDERR_FILENO, msg, sizeof (msg)) != sizeof (msg))
          abort (); /* NOTE: GDB has only four calls to abort().  */
	exit (1);
      }
  }

  /* Try to get the message out and at the start of a new line.  */
  target_terminal_ours ();
  begin_line ();

  /* Create a string containing the full error/warning message.  Need
     to call query with this full string, as otherwize the reason
     (error/warning) and question become separated.  Format using a
     style similar to a compiler error message.  Include extra detail
     so that the user knows that they are living on the edge.  */
  {
    char *msg;
    msg = xstrvprintf (fmt, ap);
    reason = xstrprintf ("\
%s:%d: %s: %s\n\
A problem internal to GDB has been detected,\n\
further debugging may prove unreliable.", file, line, problem->name, msg);
    xfree (msg);
    make_cleanup (xfree, reason);
  }

  if (problem->should_quit == internal_problem_ask)
    {
      /* Default (yes/batch case) is to quit GDB.  When in batch mode
	 this lessens the likelihood of GDB going into an infinite
	 loop.  */
      if (caution == 0)
        {
          /* Emit the message and quit.  */
          fputs_unfiltered (reason, gdb_stderr);
          fputs_unfiltered ("\n", gdb_stderr);
          quit_p = 1;
        }
      else
        quit_p = query (_("%s\nQuit this debugging session? "), reason);
    }
  else if (problem->should_quit == internal_problem_yes)
    quit_p = 1;
  else if (problem->should_quit == internal_problem_no)
    quit_p = 0;
  else
    internal_error (__FILE__, __LINE__, _("bad switch"));

  if (problem->should_dump_core == internal_problem_ask)
    {
      /* Default (yes/batch case) is to dump core.  This leaves a GDB
         `dropping' so that it is easier to see that something went
         wrong in GDB.  */
      dump_core_p = query (_("%s\nCreate a core file of GDB? "), reason);
    }
  else if (problem->should_dump_core == internal_problem_yes)
    dump_core_p = 1;
  else if (problem->should_dump_core == internal_problem_no)
    dump_core_p = 0;
  else
    internal_error (__FILE__, __LINE__, _("bad switch"));

  if (quit_p)
    {
      if (dump_core_p)
	abort ();		/* NOTE: GDB has only four calls to abort().  */
      else
	exit (1);
    }
  else
    {
      if (dump_core_p)
	{
#ifdef HAVE_WORKING_FORK
	  if (fork () == 0)
	    abort ();		/* NOTE: GDB has only four calls to abort().  */
#endif
	}
    }

  dejavu = 0;
}

static struct internal_problem internal_error_problem = {
  "internal-error", internal_problem_ask, internal_problem_ask
};

NORETURN void
internal_verror (const char *file, int line, const char *fmt, va_list ap)
{
  internal_vproblem (&internal_error_problem, file, line, fmt, ap);
  deprecated_throw_reason (RETURN_ERROR);
}

NORETURN void
internal_error (const char *file, int line, const char *string, ...)
{
  va_list ap;
  va_start (ap, string);
  internal_verror (file, line, string, ap);
  va_end (ap);
}

static struct internal_problem internal_warning_problem = {
  "internal-warning", internal_problem_ask, internal_problem_ask
};

void
internal_vwarning (const char *file, int line, const char *fmt, va_list ap)
{
  internal_vproblem (&internal_warning_problem, file, line, fmt, ap);
}

void
internal_warning (const char *file, int line, const char *string, ...)
{
  va_list ap;
  va_start (ap, string);
  internal_vwarning (file, line, string, ap);
  va_end (ap);
}

/* Dummy functions to keep add_prefix_cmd happy.  */

static void
set_internal_problem_cmd (char *args, int from_tty)
{
}

static void
show_internal_problem_cmd (char *args, int from_tty)
{
}

/* When GDB reports an internal problem (error or warning) it gives
   the user the opportunity to quit GDB and/or create a core file of
   the current debug session.  This function registers a few commands
   that make it possible to specify that GDB should always or never
   quit or create a core file, without asking.  The commands look
   like:

   maint set PROBLEM-NAME quit ask|yes|no
   maint show PROBLEM-NAME quit
   maint set PROBLEM-NAME corefile ask|yes|no
   maint show PROBLEM-NAME corefile

   Where PROBLEM-NAME is currently "internal-error" or
   "internal-warning".  */

static void
add_internal_problem_command (struct internal_problem *problem)
{
  struct cmd_list_element **set_cmd_list;
  struct cmd_list_element **show_cmd_list;
  char *set_doc;
  char *show_doc;

  set_cmd_list = xmalloc (sizeof (*set_cmd_list));
  show_cmd_list = xmalloc (sizeof (*set_cmd_list));
  *set_cmd_list = NULL;
  *show_cmd_list = NULL;

  set_doc = xstrprintf (_("Configure what GDB does when %s is detected."),
			problem->name);

  show_doc = xstrprintf (_("Show what GDB does when %s is detected."),
			 problem->name);

  add_prefix_cmd ((char*) problem->name,
		  class_maintenance, set_internal_problem_cmd, set_doc,
		  set_cmd_list,
		  concat ("maintenance set ", problem->name, " ", NULL),
		  0/*allow-unknown*/, &maintenance_set_cmdlist);

  add_prefix_cmd ((char*) problem->name,
		  class_maintenance, show_internal_problem_cmd, show_doc,
		  show_cmd_list,
		  concat ("maintenance show ", problem->name, " ", NULL),
		  0/*allow-unknown*/, &maintenance_show_cmdlist);

  set_doc = xstrprintf (_("\
Set whether GDB should quit when an %s is detected"),
			problem->name);
  show_doc = xstrprintf (_("\
Show whether GDB will quit when an %s is detected"),
			 problem->name);
  add_setshow_enum_cmd ("quit", class_maintenance,
			internal_problem_modes,
			&problem->should_quit,
			set_doc,
			show_doc,
			NULL, /* help_doc */
			NULL, /* setfunc */
			NULL, /* showfunc */
			set_cmd_list,
			show_cmd_list);

  set_doc = xstrprintf (_("\
Set whether GDB should create a core file of GDB when %s is detected"),
			problem->name);
  show_doc = xstrprintf (_("\
Show whether GDB will create a core file of GDB when %s is detected"),
			 problem->name);
  add_setshow_enum_cmd ("corefile", class_maintenance,
			internal_problem_modes,
			&problem->should_dump_core,
			set_doc,
			show_doc,
			NULL, /* help_doc */
			NULL, /* setfunc */
			NULL, /* showfunc */
			set_cmd_list,
			show_cmd_list);
}

/* Print the system error message for errno, and also mention STRING
   as the file name for which the error was encountered.
   Then return to command level.  */

NORETURN void
perror_with_name (const char *string)
{
  char *err;
  char *combined;

  err = safe_strerror (errno);
  combined = (char *) alloca (strlen (err) + strlen (string) + 3);
  strcpy (combined, string);
  strcat (combined, ": ");
  strcat (combined, err);

  /* I understand setting these is a matter of taste.  Still, some people
     may clear errno but not know about bfd_error.  Doing this here is not
     unreasonable. */
  bfd_set_error (bfd_error_no_error);
  errno = 0;

  error (_("%s."), combined);
}

/* Print the system error message for ERRCODE, and also mention STRING
   as the file name for which the error was encountered.  */

void
print_sys_errmsg (const char *string, int errcode)
{
  char *err;
  char *combined;

  err = safe_strerror (errcode);
  combined = (char *) alloca (strlen (err) + strlen (string) + 3);
  strcpy (combined, string);
  strcat (combined, ": ");
  strcat (combined, err);

  /* We want anything which was printed on stdout to come out first, before
     this message.  */
  gdb_flush (gdb_stdout);
  fprintf_unfiltered (gdb_stderr, "%s.\n", combined);
}

/* Control C eventually causes this to be called, at a convenient time.  */

void
quit (void)
{
#ifdef __MSDOS__
  /* No steenking SIGINT will ever be coming our way when the
     program is resumed.  Don't lie.  */
  fatal ("Quit");
#else
  if (job_control
      /* If there is no terminal switching for this target, then we can't
         possibly get screwed by the lack of job control.  */
      || current_target.to_terminal_ours == NULL)
    fatal ("Quit");
  else
    fatal ("Quit (expect signal SIGINT when the program is resumed)");
#endif
}


/* Called when a memory allocation fails, with the number of bytes of
   memory requested in SIZE. */

NORETURN void
nomem (long size)
{
  if (size > 0)
    {
      internal_error (__FILE__, __LINE__,
		      _("virtual memory exhausted: can't allocate %ld bytes."),
		      size);
    }
  else
    {
      internal_error (__FILE__, __LINE__, _("virtual memory exhausted."));
    }
}

/* The xmalloc() (libiberty.h) family of memory management routines.

   These are like the ISO-C malloc() family except that they implement
   consistent semantics and guard against typical memory management
   problems.  */

/* NOTE: These are declared using PTR to ensure consistency with
   "libiberty.h".  xfree() is GDB local.  */

PTR				/* ARI: PTR */
xmalloc (size_t size)
{
  void *val;

  /* See libiberty/xmalloc.c.  This function need's to match that's
     semantics.  It never returns NULL.  */
  if (size == 0)
    size = 1;

  val = malloc (size);		/* ARI: malloc */
  if (val == NULL)
    nomem (size);

  return (val);
}

void *
xzalloc (size_t size)
{
  return xcalloc (1, size);
}

PTR				/* ARI: PTR */
xrealloc (PTR ptr, size_t size)	/* ARI: PTR */
{
  void *val;

  /* See libiberty/xmalloc.c.  This function need's to match that's
     semantics.  It never returns NULL.  */
  if (size == 0)
    size = 1;

  if (ptr != NULL)
    val = realloc (ptr, size);	/* ARI: realloc */
  else
    val = malloc (size);		/* ARI: malloc */
  if (val == NULL)
    nomem (size);

  return (val);
}

PTR				/* ARI: PTR */
xcalloc (size_t number, size_t size)
{
  void *mem;

  /* See libiberty/xmalloc.c.  This function need's to match that's
     semantics.  It never returns NULL.  */
  if (number == 0 || size == 0)
    {
      number = 1;
      size = 1;
    }

  mem = calloc (number, size);		/* ARI: xcalloc */
  if (mem == NULL)
    nomem (number * size);

  return mem;
}

void
xfree (void *ptr)
{
  if (ptr != NULL)
    free (ptr);		/* ARI: free */
}


/* Like asprintf/vasprintf but get an internal_error if the call
   fails. */

char *
xstrprintf (const char *format, ...)
{
  char *ret;
  va_list args;
  va_start (args, format);
  ret = xstrvprintf (format, args);
  va_end (args);
  return ret;
}

void
xasprintf (char **ret, const char *format, ...)
{
  va_list args;
  va_start (args, format);
  (*ret) = xstrvprintf (format, args);
  va_end (args);
}

void
xvasprintf (char **ret, const char *format, va_list ap)
{
  (*ret) = xstrvprintf (format, ap);
}

char *
xstrvprintf (const char *format, va_list ap)
{
  char *ret = NULL;
  int status = vasprintf (&ret, format, ap);
  /* NULL is returned when there was a memory allocation problem, or
     any other error (for instance, a bad format string).  A negative
     status (the printed length) with a non-NULL buffer should never
     happen, but just to be sure.  */
  if (ret == NULL || status < 0)
    internal_error (__FILE__, __LINE__, _("vasprintf call failed"));
  return ret;
}

int
xsnprintf (char *str, size_t size, const char *format, ...)
{
  va_list args;
  int ret;

  va_start (args, format);
  ret = vsnprintf (str, size, format, args);
  gdb_assert (ret < size);
  va_end (args);

  return ret;
}

/* My replacement for the read system call.
   Used like `read' but keeps going if `read' returns too soon.  */

int
myread (int desc, char *addr, int len)
{
  int val;
  int orglen = len;

  while (len > 0)
    {
      val = read (desc, addr, len);
      if (val < 0)
	return val;
      if (val == 0)
	return orglen - len;
      len -= val;
      addr += val;
    }
  return orglen;
}

/* Make a copy of the string at PTR with SIZE characters
   (and add a null character at the end in the copy).
   Uses malloc to get the space.  Returns the address of the copy.  */

char *
savestring (const char *ptr, size_t size)
{
  char *p = (char *) xmalloc (size + 1);
  memcpy (p, ptr, size);
  p[size] = 0;
  return p;
}

void
print_spaces (int n, struct ui_file *file)
{
  fputs_unfiltered (n_spaces (n), file);
}

/* Print a host address.  */

void
gdb_print_host_address (const void *addr, struct ui_file *stream)
{
  fprintf_filtered (stream, "%s", host_address_to_string (addr));
}


/* This function supports the query, nquery, and yquery functions.
   Ask user a y-or-n question and return 0 if answer is no, 1 if
   answer is yes, or default the answer to the specified default
   (for yquery or nquery).  DEFCHAR may be 'y' or 'n' to provide a
   default answer, or '\0' for no default.
   CTLSTR is the control string and should end in "? ".  It should
   not say how to answer, because we do that.
   ARGS are the arguments passed along with the CTLSTR argument to
   printf.  */

static int ATTR_FORMAT (printf, 1, 0)
defaulted_query (const char *ctlstr, const char defchar, va_list args)
{
  int answer;
  int ans2;
  int retval;
  int def_value;
  char def_answer, not_def_answer;
  char *y_string, *n_string, *question;

  /* Set up according to which answer is the default.  */
  if (defchar == '\0')
    {
      def_value = 1;
      def_answer = 'Y';
      not_def_answer = 'N';
      y_string = "y";
      n_string = "n";
    }
  else if (defchar == 'y')
    {
      def_value = 1;
      def_answer = 'Y';
      not_def_answer = 'N';
      y_string = "[y]";
      n_string = "n";
    }
  else
    {
      def_value = 0;
      def_answer = 'N';
      not_def_answer = 'Y';
      y_string = "y";
      n_string = "[n]";
    }

  /* Automatically answer the default value if the user did not want
     prompts.  */
  if (! caution)
    return def_value;

  /* If input isn't coming from the user directly, just say what
     question we're asking, and then answer the default automatically.  This
     way, important error messages don't get lost when talking to GDB
     over a pipe.  */
  if (! input_from_terminal_p ())
    {
      wrap_here ("");
      vfprintf_filtered (gdb_stdout, ctlstr, args);

      printf_filtered (_("(%s or %s) [answered %c; input not from terminal]\n"),
		       y_string, n_string, def_answer);
      gdb_flush (gdb_stdout);

      return def_value;
    }

  if (deprecated_query_hook)
    {
      return deprecated_query_hook (ctlstr, args);
    }

  /* Format the question outside of the loop, to avoid reusing args.  */
  question = xstrvprintf (ctlstr, args);

  while (1)
    {
      wrap_here ("");		/* Flush any buffered output */
      gdb_flush (gdb_stdout);

      if (annotation_level > 1)
	printf_filtered (("\n\032\032pre-query\n"));

      fputs_filtered (question, gdb_stdout);
      printf_filtered (_("(%s or %s) "), y_string, n_string);

      if (annotation_level > 1)
	printf_filtered (("\n\032\032query\n"));

      wrap_here ("");
      gdb_flush (gdb_stdout);

      answer = fgetc (stdin);

      /* We expect fgetc to block until a character is read.  But
         this may not be the case if the terminal was opened with
         the NONBLOCK flag.  In that case, if there is nothing to
         read on stdin, fgetc returns EOF, but also sets the error
         condition flag on stdin and errno to EAGAIN.  With a true
         EOF, stdin's error condition flag is not set.

         A situation where this behavior was observed is a pseudo
         terminal on AIX.  */
      while (answer == EOF && ferror (stdin) && errno == EAGAIN)
        {
          /* Not a real EOF.  Wait a little while and try again until
             we read something.  */
          clearerr (stdin);
          gdb_usleep (10000);
          answer = fgetc (stdin);
        }

      clearerr (stdin);		/* in case of C-d */
      if (answer == EOF)	/* C-d */
	{
	  printf_filtered ("EOF [assumed %c]\n", def_answer);
	  retval = def_value;
	  break;
	}
      /* Eat rest of input line, to EOF or newline */
      if (answer != '\n')
	do
	  {
	    ans2 = fgetc (stdin);
	    clearerr (stdin);
	  }
	while (ans2 != EOF && ans2 != '\n' && ans2 != '\r');

      if (answer >= 'a')
	answer -= 040;
      /* Check answer.  For the non-default, the user must specify
         the non-default explicitly.  */
      if (answer == not_def_answer)
	{
	  retval = !def_value;
	  break;
	}
      /* Otherwise, if a default was specified, the user may either
         specify the required input or have it default by entering
         nothing.  */
      if (answer == def_answer
	  || (defchar != '\0' &&
	      (answer == '\n' || answer == '\r' || answer == EOF)))
	{
	  retval = def_value;
	  break;
	}
      /* Invalid entries are not defaulted and require another selection.  */
      printf_filtered (_("Please answer %s or %s.\n"),
		       y_string, n_string);
    }

  xfree (question);
  if (annotation_level > 1)
    printf_filtered (("\n\032\032post-query\n"));
  return retval;
}


/* Ask user a y-or-n question and return 0 if answer is no, 1 if
   answer is yes, or 0 if answer is defaulted.
   Takes three args which are given to printf to print the question.
   The first, a control string, should end in "? ".
   It should not say how to answer, because we do that.  */

int
nquery (const char *ctlstr, ...)
{
  va_list args;

  va_start (args, ctlstr);
  return defaulted_query (ctlstr, 'n', args);
  va_end (args);
}

/* Ask user a y-or-n question and return 0 if answer is no, 1 if
   answer is yes, or 1 if answer is defaulted.
   Takes three args which are given to printf to print the question.
   The first, a control string, should end in "? ".
   It should not say how to answer, because we do that.  */

int
yquery (const char *ctlstr, ...)
{
  va_list args;

  va_start (args, ctlstr);
  return defaulted_query (ctlstr, 'y', args);
  va_end (args);
}

/* Ask user a y-or-n question and return 1 iff answer is yes.
   Takes three args which are given to printf to print the question.
   The first, a control string, should end in "? ".
   It should not say how to answer, because we do that.  */

int
query (const char *ctlstr, ...)
{
  va_list args;

  va_start (args, ctlstr);
  return defaulted_query (ctlstr, '\0', args);
  va_end (args);
}

/* A helper for parse_escape that converts a host character to a
   target character.  C is the host character.  If conversion is
   possible, then the target character is stored in *TARGET_C and the
   function returns 1.  Otherwise, the function returns 0.  */

static int
host_char_to_target (int c, int *target_c)
{
  struct obstack host_data;
  char the_char = c;
  struct cleanup *cleanups;
  int result = 0;

  obstack_init (&host_data);
  cleanups = make_cleanup_obstack_free (&host_data);

  convert_between_encodings (target_charset (), host_charset (),
			     (gdb_byte *)&the_char, 1, 1, &host_data,
			     translit_none);

  if (obstack_object_size (&host_data) == 1)
    {
      result = 1;
      *target_c = *(char *) obstack_base (&host_data);
    }

  do_cleanups (cleanups);
  return result;
}

/* Parse a C escape sequence.  STRING_PTR points to a variable
   containing a pointer to the string to parse.  That pointer
   should point to the character after the \.  That pointer
   is updated past the characters we use.  The value of the
   escape sequence is returned.

   A negative value means the sequence \ newline was seen,
   which is supposed to be equivalent to nothing at all.

   If \ is followed by a null character, we return a negative
   value and leave the string pointer pointing at the null character.

   If \ is followed by 000, we return 0 and leave the string pointer
   after the zeros.  A value of 0 does not mean end of string.  */

int
parse_escape (char **string_ptr)
{
  int target_char = -2;	/* initialize to avoid GCC warnings */
  int c = *(*string_ptr)++;
  switch (c)
    {
      case '\n':
	return -2;
      case 0:
	(*string_ptr)--;
	return 0;

      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
	{
	  int i = host_hex_value (c);
	  int count = 0;
	  while (++count < 3)
	    {
	      c = (**string_ptr);
	      if (isdigit (c) && c != '8' && c != '9')
		{
		  (*string_ptr)++;
		  i *= 8;
		  i += host_hex_value (c);
		}
	      else
		{
		  break;
		}
	    }
	  return i;
	}

    case 'a':
      c = '\a';
      break;
    case 'b':
      c = '\b';
      break;
    case 'f':
      c = '\f';
      break;
    case 'n':
      c = '\n';
      break;
    case 'r':
      c = '\r';
      break;
    case 't':
      c = '\t';
      break;
    case 'v':
      c = '\v';
      break;

    default:
      break;
    }

  if (!host_char_to_target (c, &target_char))
    error
      ("The escape sequence `\%c' is equivalent to plain `%c', which"
       " has no equivalent\n" "in the `%s' character set.", c, c,
       target_charset ());
  return target_char;
}

/* Print the character C on STREAM as part of the contents of a literal
   string whose delimiter is QUOTER.  Note that this routine should only
   be call for printing things which are independent of the language
   of the program being debugged. */

static void
printchar (int c, void (*do_fputs) (const char *, struct ui_file *),
	   void (*do_fprintf) (struct ui_file *, const char *, ...)
	   ATTRIBUTE_FPTR_PRINTF_2, struct ui_file *stream, int quoter)
{

  c &= 0xFF;			/* Avoid sign bit follies */

  if (c < 0x20 ||		/* Low control chars */
      (c >= 0x7F && c < 0xA0) ||	/* DEL, High controls */
      (sevenbit_strings && c >= 0x80))
    {				/* high order bit set */
      switch (c)
	{
	case '\n':
	  do_fputs ("\\n", stream);
	  break;
	case '\b':
	  do_fputs ("\\b", stream);
	  break;
	case '\t':
	  do_fputs ("\\t", stream);
	  break;
	case '\f':
	  do_fputs ("\\f", stream);
	  break;
	case '\r':
	  do_fputs ("\\r", stream);
	  break;
	case '\033':
	  do_fputs ("\\e", stream);
	  break;
	case '\007':
	  do_fputs ("\\a", stream);
	  break;
	default:
	  do_fprintf (stream, "\\%.3o", (unsigned int) c);
	  break;
	}
    }
  else
    {
      if (c == '\\' || c == quoter)
	do_fputs ("\\", stream);
      do_fprintf (stream, "%c", c);
    }
}

/* Print the character C on STREAM as part of the contents of a
   literal string whose delimiter is QUOTER.  Note that these routines
   should only be call for printing things which are independent of
   the language of the program being debugged. */

void
fputstr_filtered (const char *str, int quoter, struct ui_file *stream)
{
  while (*str)
    printchar (*str++, fputs_filtered, fprintf_filtered, stream, quoter);
}

void
fputstr_unfiltered (const char *str, int quoter, struct ui_file *stream)
{
  while (*str)
    printchar (*str++, fputs_unfiltered, fprintf_unfiltered, stream, quoter);
}

void
fputstrn_filtered (const char *str, int n, int quoter,
		   struct ui_file *stream)
{
  int i;
  for (i = 0; i < n; i++)
    printchar (str[i], fputs_filtered, fprintf_filtered, stream, quoter);
}

void
fputstrn_unfiltered (const char *str, int n, int quoter,
		     struct ui_file *stream)
{
  int i;
  for (i = 0; i < n; i++)
    printchar (str[i], fputs_unfiltered, fprintf_unfiltered, stream, quoter);
}


/* Number of lines per page or UINT_MAX if paging is disabled.  */
static unsigned int lines_per_page;
static void
show_lines_per_page (struct ui_file *file, int from_tty,
		     struct cmd_list_element *c, const char *value)
{
  fprintf_filtered (file, _("\
Number of lines gdb thinks are in a page is %s.\n"),
		    value);
}

/* Number of chars per line or UINT_MAX if line folding is disabled.  */
static unsigned int chars_per_line;
static void
show_chars_per_line (struct ui_file *file, int from_tty,
		     struct cmd_list_element *c, const char *value)
{
  fprintf_filtered (file, _("\
Number of characters gdb thinks are in a line is %s.\n"),
		    value);
}

/* Current count of lines printed on this page, chars on this line.  */
static unsigned int lines_printed, chars_printed;

/* Buffer and start column of buffered text, for doing smarter word-
   wrapping.  When someone calls wrap_here(), we start buffering output
   that comes through fputs_filtered().  If we see a newline, we just
   spit it out and forget about the wrap_here().  If we see another
   wrap_here(), we spit it out and remember the newer one.  If we see
   the end of the line, we spit out a newline, the indent, and then
   the buffered output.  */

/* Malloc'd buffer with chars_per_line+2 bytes.  Contains characters which
   are waiting to be output (they have already been counted in chars_printed).
   When wrap_buffer[0] is null, the buffer is empty.  */
static char *wrap_buffer;

/* Pointer in wrap_buffer to the next character to fill.  */
static char *wrap_pointer;

/* String to indent by if the wrap occurs.  Must not be NULL if wrap_column
   is non-zero.  */
static char *wrap_indent;

/* Column number on the screen where wrap_buffer begins, or 0 if wrapping
   is not in effect.  */
static int wrap_column;


/* Inialize the number of lines per page and chars per line.  */

void
init_page_info (void)
{
#if defined(TUI)
  if (!tui_get_command_dimension (&chars_per_line, &lines_per_page))
#endif
    {
      int rows, cols;

#if defined(__GO32__)
      rows = ScreenRows ();
      cols = ScreenCols ();
      lines_per_page = rows;
      chars_per_line = cols;
#else
      /* Make sure Readline has initialized its terminal settings.  */
      rl_reset_terminal (NULL);

      /* Get the screen size from Readline.  */
      rl_get_screen_size (&rows, &cols);
      lines_per_page = rows;
      chars_per_line = cols;

      /* Readline should have fetched the termcap entry for us.  */
      if (tgetnum ("li") < 0 || getenv ("EMACS"))
	{
	  /* The number of lines per page is not mentioned in the
	     terminal description.  This probably means that paging is
	     not useful (e.g. emacs shell window), so disable paging.  */
	  lines_per_page = UINT_MAX;
	}

      /* FIXME: Get rid of this junk.  */
#if defined(SIGWINCH) && defined(SIGWINCH_HANDLER)
      SIGWINCH_HANDLER (SIGWINCH);
#endif

      /* If the output is not a terminal, don't paginate it.  */
      if (!ui_file_isatty (gdb_stdout))
	lines_per_page = UINT_MAX;
#endif
    }

  set_screen_size ();
  set_width ();
}

/* Set the screen size based on LINES_PER_PAGE and CHARS_PER_LINE.  */

static void
set_screen_size (void)
{
  int rows = lines_per_page;
  int cols = chars_per_line;

  if (rows <= 0)
    rows = INT_MAX;

  if (cols <= 0)
    cols = INT_MAX;

  /* Update Readline's idea of the terminal size.  */
  rl_set_screen_size (rows, cols);
}

/* Reinitialize WRAP_BUFFER according to the current value of
   CHARS_PER_LINE.  */

static void
set_width (void)
{
  if (chars_per_line == 0)
    init_page_info ();

  if (!wrap_buffer)
    {
      wrap_buffer = (char *) xmalloc (chars_per_line + 2);
      wrap_buffer[0] = '\0';
    }
  else
    wrap_buffer = (char *) xrealloc (wrap_buffer, chars_per_line + 2);
  wrap_pointer = wrap_buffer;	/* Start it at the beginning.  */
}

static void
set_width_command (char *args, int from_tty, struct cmd_list_element *c)
{
  set_screen_size ();
  set_width ();
}

static void
set_height_command (char *args, int from_tty, struct cmd_list_element *c)
{
  set_screen_size ();
}

/* Wait, so the user can read what's on the screen.  Prompt the user
   to continue by pressing RETURN.  */

static void
prompt_for_continue (void)
{
  char *ignore;
  char cont_prompt[120];

  if (annotation_level > 1)
    printf_unfiltered (("\n\032\032pre-prompt-for-continue\n"));

  strcpy (cont_prompt,
	  "---Type <return> to continue, or q <return> to quit---");
  if (annotation_level > 1)
    strcat (cont_prompt, "\n\032\032prompt-for-continue\n");

  /* We must do this *before* we call gdb_readline, else it will eventually
     call us -- thinking that we're trying to print beyond the end of the 
     screen.  */
  reinitialize_more_filter ();

  immediate_quit++;
  /* On a real operating system, the user can quit with SIGINT.
     But not on GO32.

     'q' is provided on all systems so users don't have to change habits
     from system to system, and because telling them what to do in
     the prompt is more user-friendly than expecting them to think of
     SIGINT.  */
  /* Call readline, not gdb_readline, because GO32 readline handles control-C
     whereas control-C to gdb_readline will cause the user to get dumped
     out to DOS.  */
  ignore = gdb_readline_wrapper (cont_prompt);

  if (annotation_level > 1)
    printf_unfiltered (("\n\032\032post-prompt-for-continue\n"));

  if (ignore)
    {
      char *p = ignore;
      while (*p == ' ' || *p == '\t')
	++p;
      if (p[0] == 'q')
	async_request_quit (0);
      xfree (ignore);
    }
  immediate_quit--;

  /* Now we have to do this again, so that GDB will know that it doesn't
     need to save the ---Type <return>--- line at the top of the screen.  */
  reinitialize_more_filter ();

  dont_repeat ();		/* Forget prev cmd -- CR won't repeat it. */
}

/* Reinitialize filter; ie. tell it to reset to original values.  */

void
reinitialize_more_filter (void)
{
  lines_printed = 0;
  chars_printed = 0;
}

/* Indicate that if the next sequence of characters overflows the line,
   a newline should be inserted here rather than when it hits the end. 
   If INDENT is non-null, it is a string to be printed to indent the
   wrapped part on the next line.  INDENT must remain accessible until
   the next call to wrap_here() or until a newline is printed through
   fputs_filtered().

   If the line is already overfull, we immediately print a newline and
   the indentation, and disable further wrapping.

   If we don't know the width of lines, but we know the page height,
   we must not wrap words, but should still keep track of newlines
   that were explicitly printed.

   INDENT should not contain tabs, as that will mess up the char count
   on the next line.  FIXME.

   This routine is guaranteed to force out any output which has been
   squirreled away in the wrap_buffer, so wrap_here ((char *)0) can be
   used to force out output from the wrap_buffer.  */

void
wrap_here (char *indent)
{
  /* This should have been allocated, but be paranoid anyway. */
  if (!wrap_buffer)
    internal_error (__FILE__, __LINE__, _("failed internal consistency check"));

  if (wrap_buffer[0])
    {
      *wrap_pointer = '\0';
      fputs_unfiltered (wrap_buffer, gdb_stdout);
    }
  wrap_pointer = wrap_buffer;
  wrap_buffer[0] = '\0';
  if (chars_per_line == UINT_MAX)	/* No line overflow checking */
    {
      wrap_column = 0;
    }
  else if (chars_printed >= chars_per_line)
    {
      puts_filtered ("\n");
      if (indent != NULL)
	puts_filtered (indent);
      wrap_column = 0;
    }
  else
    {
      wrap_column = chars_printed;
      if (indent == NULL)
	wrap_indent = "";
      else
	wrap_indent = indent;
    }
}

/* Print input string to gdb_stdout, filtered, with wrap, 
   arranging strings in columns of n chars. String can be
   right or left justified in the column.  Never prints 
   trailing spaces.  String should never be longer than
   width.  FIXME: this could be useful for the EXAMINE 
   command, which currently doesn't tabulate very well */

void
puts_filtered_tabular (char *string, int width, int right)
{
  int spaces = 0;
  int stringlen;
  char *spacebuf;

  gdb_assert (chars_per_line > 0);
  if (chars_per_line == UINT_MAX)
    {
      fputs_filtered (string, gdb_stdout);
      fputs_filtered ("\n", gdb_stdout);
      return;
    }

  if (((chars_printed - 1) / width + 2) * width >= chars_per_line)
    fputs_filtered ("\n", gdb_stdout);

  if (width >= chars_per_line)
    width = chars_per_line - 1;

  stringlen = strlen (string);

  if (chars_printed > 0)
    spaces = width - (chars_printed - 1) % width - 1;
  if (right)
    spaces += width - stringlen;

  spacebuf = alloca (spaces + 1);
  spacebuf[spaces] = '\0';
  while (spaces--)
    spacebuf[spaces] = ' ';

  fputs_filtered (spacebuf, gdb_stdout);
  fputs_filtered (string, gdb_stdout);
}


/* Ensure that whatever gets printed next, using the filtered output
   commands, starts at the beginning of the line.  I.E. if there is
   any pending output for the current line, flush it and start a new
   line.  Otherwise do nothing. */

void
begin_line (void)
{
  if (chars_printed > 0)
    {
      puts_filtered ("\n");
    }
}


/* Like fputs but if FILTER is true, pause after every screenful.

   Regardless of FILTER can wrap at points other than the final
   character of a line.

   Unlike fputs, fputs_maybe_filtered does not return a value.
   It is OK for LINEBUFFER to be NULL, in which case just don't print
   anything.

   Note that a longjmp to top level may occur in this routine (only if
   FILTER is true) (since prompt_for_continue may do so) so this
   routine should not be called when cleanups are not in place.  */

static void
fputs_maybe_filtered (const char *linebuffer, struct ui_file *stream,
		      int filter)
{
  const char *lineptr;

  if (linebuffer == 0)
    return;

  /* Don't do any filtering if it is disabled.  */
  if ((stream != gdb_stdout) || !pagination_enabled
      || (lines_per_page == UINT_MAX && chars_per_line == UINT_MAX))
    {
      fputs_unfiltered (linebuffer, stream);
      return;
    }

  /* Go through and output each character.  Show line extension
     when this is necessary; prompt user for new page when this is
     necessary.  */

  lineptr = linebuffer;
  while (*lineptr)
    {
      /* Possible new page.  */
      if (filter && (lines_printed >= lines_per_page - 1))
	prompt_for_continue ();

      while (*lineptr && *lineptr != '\n')
	{
	  /* Print a single line.  */
	  if (*lineptr == '\t')
	    {
	      if (wrap_column)
		*wrap_pointer++ = '\t';
	      else
		fputc_unfiltered ('\t', stream);
	      /* Shifting right by 3 produces the number of tab stops
	         we have already passed, and then adding one and
	         shifting left 3 advances to the next tab stop.  */
	      chars_printed = ((chars_printed >> 3) + 1) << 3;
	      lineptr++;
	    }
	  else
	    {
	      if (wrap_column)
		*wrap_pointer++ = *lineptr;
	      else
		fputc_unfiltered (*lineptr, stream);
	      chars_printed++;
	      lineptr++;
	    }

	  if (chars_printed >= chars_per_line)
	    {
	      unsigned int save_chars = chars_printed;

	      chars_printed = 0;
	      lines_printed++;
	      /* If we aren't actually wrapping, don't output newline --
	         if chars_per_line is right, we probably just overflowed
	         anyway; if it's wrong, let us keep going.  */
	      if (wrap_column)
		fputc_unfiltered ('\n', stream);

	      /* Possible new page.  */
	      if (lines_printed >= lines_per_page - 1)
		prompt_for_continue ();

	      /* Now output indentation and wrapped string */
	      if (wrap_column)
		{
		  fputs_unfiltered (wrap_indent, stream);
		  *wrap_pointer = '\0';	/* Null-terminate saved stuff */
		  fputs_unfiltered (wrap_buffer, stream);	/* and eject it */
		  /* FIXME, this strlen is what prevents wrap_indent from
		     containing tabs.  However, if we recurse to print it
		     and count its chars, we risk trouble if wrap_indent is
		     longer than (the user settable) chars_per_line. 
		     Note also that this can set chars_printed > chars_per_line
		     if we are printing a long string.  */
		  chars_printed = strlen (wrap_indent)
		    + (save_chars - wrap_column);
		  wrap_pointer = wrap_buffer;	/* Reset buffer */
		  wrap_buffer[0] = '\0';
		  wrap_column = 0;	/* And disable fancy wrap */
		}
	    }
	}

      if (*lineptr == '\n')
	{
	  chars_printed = 0;
	  wrap_here ((char *) 0);	/* Spit out chars, cancel further wraps */
	  lines_printed++;
	  fputc_unfiltered ('\n', stream);
	  lineptr++;
	}
    }
}

void
fputs_filtered (const char *linebuffer, struct ui_file *stream)
{
  fputs_maybe_filtered (linebuffer, stream, 1);
}

int
putchar_unfiltered (int c)
{
  char buf = c;
  ui_file_write (gdb_stdout, &buf, 1);
  return c;
}

/* Write character C to gdb_stdout using GDB's paging mechanism and return C.
   May return nonlocally.  */

int
putchar_filtered (int c)
{
  return fputc_filtered (c, gdb_stdout);
}

int
fputc_unfiltered (int c, struct ui_file *stream)
{
  char buf = c;
  ui_file_write (stream, &buf, 1);
  return c;
}

int
fputc_filtered (int c, struct ui_file *stream)
{
  char buf[2];

  buf[0] = c;
  buf[1] = 0;
  fputs_filtered (buf, stream);
  return c;
}

/* puts_debug is like fputs_unfiltered, except it prints special
   characters in printable fashion.  */

void
puts_debug (char *prefix, char *string, char *suffix)
{
  int ch;

  /* Print prefix and suffix after each line.  */
  static int new_line = 1;
  static int return_p = 0;
  static char *prev_prefix = "";
  static char *prev_suffix = "";

  if (*string == '\n')
    return_p = 0;

  /* If the prefix is changing, print the previous suffix, a new line,
     and the new prefix.  */
  if ((return_p || (strcmp (prev_prefix, prefix) != 0)) && !new_line)
    {
      fputs_unfiltered (prev_suffix, gdb_stdlog);
      fputs_unfiltered ("\n", gdb_stdlog);
      fputs_unfiltered (prefix, gdb_stdlog);
    }

  /* Print prefix if we printed a newline during the previous call.  */
  if (new_line)
    {
      new_line = 0;
      fputs_unfiltered (prefix, gdb_stdlog);
    }

  prev_prefix = prefix;
  prev_suffix = suffix;

  /* Output characters in a printable format.  */
  while ((ch = *string++) != '\0')
    {
      switch (ch)
	{
	default:
	  if (isprint (ch))
	    fputc_unfiltered (ch, gdb_stdlog);

	  else
	    fprintf_unfiltered (gdb_stdlog, "\\x%02x", ch & 0xff);
	  break;

	case '\\':
	  fputs_unfiltered ("\\\\", gdb_stdlog);
	  break;
	case '\b':
	  fputs_unfiltered ("\\b", gdb_stdlog);
	  break;
	case '\f':
	  fputs_unfiltered ("\\f", gdb_stdlog);
	  break;
	case '\n':
	  new_line = 1;
	  fputs_unfiltered ("\\n", gdb_stdlog);
	  break;
	case '\r':
	  fputs_unfiltered ("\\r", gdb_stdlog);
	  break;
	case '\t':
	  fputs_unfiltered ("\\t", gdb_stdlog);
	  break;
	case '\v':
	  fputs_unfiltered ("\\v", gdb_stdlog);
	  break;
	}

      return_p = ch == '\r';
    }

  /* Print suffix if we printed a newline.  */
  if (new_line)
    {
      fputs_unfiltered (suffix, gdb_stdlog);
      fputs_unfiltered ("\n", gdb_stdlog);
    }
}


/* Print a variable number of ARGS using format FORMAT.  If this
   information is going to put the amount written (since the last call
   to REINITIALIZE_MORE_FILTER or the last page break) over the page size,
   call prompt_for_continue to get the users permision to continue.

   Unlike fprintf, this function does not return a value.

   We implement three variants, vfprintf (takes a vararg list and stream),
   fprintf (takes a stream to write on), and printf (the usual).

   Note also that a longjmp to top level may occur in this routine
   (since prompt_for_continue may do so) so this routine should not be
   called when cleanups are not in place.  */

static void
vfprintf_maybe_filtered (struct ui_file *stream, const char *format,
			 va_list args, int filter)
{
  char *linebuffer;
  struct cleanup *old_cleanups;

  linebuffer = xstrvprintf (format, args);
  old_cleanups = make_cleanup (xfree, linebuffer);
  fputs_maybe_filtered (linebuffer, stream, filter);
  do_cleanups (old_cleanups);
}


void
vfprintf_filtered (struct ui_file *stream, const char *format, va_list args)
{
  vfprintf_maybe_filtered (stream, format, args, 1);
}

void
vfprintf_unfiltered (struct ui_file *stream, const char *format, va_list args)
{
  char *linebuffer;
  struct cleanup *old_cleanups;

  linebuffer = xstrvprintf (format, args);
  old_cleanups = make_cleanup (xfree, linebuffer);
  if (debug_timestamp && stream == gdb_stdlog)
    {
      struct timeval tm;
      char *timestamp;
      int len, need_nl;

      gettimeofday (&tm, NULL);

      len = strlen (linebuffer);
      need_nl = (len > 0 && linebuffer[len - 1] != '\n');

      timestamp = xstrprintf ("%ld:%ld %s%s",
			      (long) tm.tv_sec, (long) tm.tv_usec,
			      linebuffer,
			      need_nl ? "\n": "");
      make_cleanup (xfree, timestamp);
      fputs_unfiltered (timestamp, stream);
    }
  else
    fputs_unfiltered (linebuffer, stream);
  do_cleanups (old_cleanups);
}

void
vprintf_filtered (const char *format, va_list args)
{
  vfprintf_maybe_filtered (gdb_stdout, format, args, 1);
}

void
vprintf_unfiltered (const char *format, va_list args)
{
  vfprintf_unfiltered (gdb_stdout, format, args);
}

void
fprintf_filtered (struct ui_file *stream, const char *format, ...)
{
  va_list args;
  va_start (args, format);
  vfprintf_filtered (stream, format, args);
  va_end (args);
}

void
fprintf_unfiltered (struct ui_file *stream, const char *format, ...)
{
  va_list args;
  va_start (args, format);
  vfprintf_unfiltered (stream, format, args);
  va_end (args);
}

/* Like fprintf_filtered, but prints its result indented.
   Called as fprintfi_filtered (spaces, stream, format, ...);  */

void
fprintfi_filtered (int spaces, struct ui_file *stream, const char *format,
		   ...)
{
  va_list args;
  va_start (args, format);
  print_spaces_filtered (spaces, stream);

  vfprintf_filtered (stream, format, args);
  va_end (args);
}


void
printf_filtered (const char *format, ...)
{
  va_list args;
  va_start (args, format);
  vfprintf_filtered (gdb_stdout, format, args);
  va_end (args);
}


void
printf_unfiltered (const char *format, ...)
{
  va_list args;
  va_start (args, format);
  vfprintf_unfiltered (gdb_stdout, format, args);
  va_end (args);
}

/* Like printf_filtered, but prints it's result indented.
   Called as printfi_filtered (spaces, format, ...);  */

void
printfi_filtered (int spaces, const char *format, ...)
{
  va_list args;
  va_start (args, format);
  print_spaces_filtered (spaces, gdb_stdout);
  vfprintf_filtered (gdb_stdout, format, args);
  va_end (args);
}

/* Easy -- but watch out!

   This routine is *not* a replacement for puts()!  puts() appends a newline.
   This one doesn't, and had better not!  */

void
puts_filtered (const char *string)
{
  fputs_filtered (string, gdb_stdout);
}

void
puts_unfiltered (const char *string)
{
  fputs_unfiltered (string, gdb_stdout);
}

/* Return a pointer to N spaces and a null.  The pointer is good
   until the next call to here.  */
char *
n_spaces (int n)
{
  char *t;
  static char *spaces = 0;
  static int max_spaces = -1;

  if (n > max_spaces)
    {
      if (spaces)
	xfree (spaces);
      spaces = (char *) xmalloc (n + 1);
      for (t = spaces + n; t != spaces;)
	*--t = ' ';
      spaces[n] = '\0';
      max_spaces = n;
    }

  return spaces + max_spaces - n;
}

/* Print N spaces.  */
void
print_spaces_filtered (int n, struct ui_file *stream)
{
  fputs_filtered (n_spaces (n), stream);
}

/* C++/ObjC demangler stuff.  */

/* fprintf_symbol_filtered attempts to demangle NAME, a symbol in language
   LANG, using demangling args ARG_MODE, and print it filtered to STREAM.
   If the name is not mangled, or the language for the name is unknown, or
   demangling is off, the name is printed in its "raw" form. */

void
fprintf_symbol_filtered (struct ui_file *stream, char *name,
			 enum language lang, int arg_mode)
{
  char *demangled;

  if (name != NULL)
    {
      /* If user wants to see raw output, no problem.  */
      if (!demangle)
	{
	  fputs_filtered (name, stream);
	}
      else
	{
	  demangled = language_demangle (language_def (lang), name, arg_mode);
	  fputs_filtered (demangled ? demangled : name, stream);
	  if (demangled != NULL)
	    {
	      xfree (demangled);
	    }
	}
    }
}

/* Do a strcmp() type operation on STRING1 and STRING2, ignoring any
   differences in whitespace.  Returns 0 if they match, non-zero if they
   don't (slightly different than strcmp()'s range of return values).

   As an extra hack, string1=="FOO(ARGS)" matches string2=="FOO".
   This "feature" is useful when searching for matching C++ function names
   (such as if the user types 'break FOO', where FOO is a mangled C++
   function). */

int
strcmp_iw (const char *string1, const char *string2)
{
  while ((*string1 != '\0') && (*string2 != '\0'))
    {
      while (isspace (*string1))
	{
	  string1++;
	}
      while (isspace (*string2))
	{
	  string2++;
	}
      if (*string1 != *string2)
	{
	  break;
	}
      if (*string1 != '\0')
	{
	  string1++;
	  string2++;
	}
    }
  return (*string1 != '\0' && *string1 != '(') || (*string2 != '\0');
}

/* This is like strcmp except that it ignores whitespace and treats
   '(' as the first non-NULL character in terms of ordering.  Like
   strcmp (and unlike strcmp_iw), it returns negative if STRING1 <
   STRING2, 0 if STRING2 = STRING2, and positive if STRING1 > STRING2
   according to that ordering.

   If a list is sorted according to this function and if you want to
   find names in the list that match some fixed NAME according to
   strcmp_iw(LIST_ELT, NAME), then the place to start looking is right
   where this function would put NAME.

   Here are some examples of why using strcmp to sort is a bad idea:

   Whitespace example:

   Say your partial symtab contains: "foo<char *>", "goo".  Then, if
   we try to do a search for "foo<char*>", strcmp will locate this
   after "foo<char *>" and before "goo".  Then lookup_partial_symbol
   will start looking at strings beginning with "goo", and will never
   see the correct match of "foo<char *>".

   Parenthesis example:

   In practice, this is less like to be an issue, but I'll give it a
   shot.  Let's assume that '$' is a legitimate character to occur in
   symbols.  (Which may well even be the case on some systems.)  Then
   say that the partial symbol table contains "foo$" and "foo(int)".
   strcmp will put them in this order, since '$' < '('.  Now, if the
   user searches for "foo", then strcmp will sort "foo" before "foo$".
   Then lookup_partial_symbol will notice that strcmp_iw("foo$",
   "foo") is false, so it won't proceed to the actual match of
   "foo(int)" with "foo".  */

int
strcmp_iw_ordered (const char *string1, const char *string2)
{
  while ((*string1 != '\0') && (*string2 != '\0'))
    {
      while (isspace (*string1))
	{
	  string1++;
	}
      while (isspace (*string2))
	{
	  string2++;
	}
      if (*string1 != *string2)
	{
	  break;
	}
      if (*string1 != '\0')
	{
	  string1++;
	  string2++;
	}
    }

  switch (*string1)
    {
      /* Characters are non-equal unless they're both '\0'; we want to
	 make sure we get the comparison right according to our
	 comparison in the cases where one of them is '\0' or '('.  */
    case '\0':
      if (*string2 == '\0')
	return 0;
      else
	return -1;
    case '(':
      if (*string2 == '\0')
	return 1;
      else
	return -1;
    default:
      if (*string2 == '(')
	return 1;
      else
	return *string1 - *string2;
    }
}

/* A simple comparison function with opposite semantics to strcmp.  */

int
streq (const char *lhs, const char *rhs)
{
  return !strcmp (lhs, rhs);
}


/*
   ** subset_compare()
   **    Answer whether string_to_compare is a full or partial match to
   **    template_string.  The partial match must be in sequence starting
   **    at index 0.
 */
int
subset_compare (char *string_to_compare, char *template_string)
{
  int match;
  if (template_string != (char *) NULL && string_to_compare != (char *) NULL
      && strlen (string_to_compare) <= strlen (template_string))
    match =
      (strncmp
       (template_string, string_to_compare, strlen (string_to_compare)) == 0);
  else
    match = 0;
  return match;
}

static void
pagination_on_command (char *arg, int from_tty)
{
  pagination_enabled = 1;
}

static void
pagination_off_command (char *arg, int from_tty)
{
  pagination_enabled = 0;
}

static void
show_debug_timestamp (struct ui_file *file, int from_tty,
		      struct cmd_list_element *c, const char *value)
{
  fprintf_filtered (file, _("Timestamping debugging messages is %s.\n"), value);
}


void
initialize_utils (void)
{
  add_setshow_uinteger_cmd ("width", class_support, &chars_per_line, _("\
Set number of characters gdb thinks are in a line."), _("\
Show number of characters gdb thinks are in a line."), NULL,
			    set_width_command,
			    show_chars_per_line,
			    &setlist, &showlist);

  add_setshow_uinteger_cmd ("height", class_support, &lines_per_page, _("\
Set number of lines gdb thinks are in a page."), _("\
Show number of lines gdb thinks are in a page."), NULL,
			    set_height_command,
			    show_lines_per_page,
			    &setlist, &showlist);

  init_page_info ();

  add_setshow_boolean_cmd ("demangle", class_support, &demangle, _("\
Set demangling of encoded C++/ObjC names when displaying symbols."), _("\
Show demangling of encoded C++/ObjC names when displaying symbols."), NULL,
			   NULL,
			   show_demangle,
			   &setprintlist, &showprintlist);

  add_setshow_boolean_cmd ("pagination", class_support,
			   &pagination_enabled, _("\
Set state of pagination."), _("\
Show state of pagination."), NULL,
			   NULL,
			   show_pagination_enabled,
			   &setlist, &showlist);

  if (xdb_commands)
    {
      add_com ("am", class_support, pagination_on_command,
	       _("Enable pagination"));
      add_com ("sm", class_support, pagination_off_command,
	       _("Disable pagination"));
    }

  add_setshow_boolean_cmd ("sevenbit-strings", class_support,
			   &sevenbit_strings, _("\
Set printing of 8-bit characters in strings as \\nnn."), _("\
Show printing of 8-bit characters in strings as \\nnn."), NULL,
			   NULL,
			   show_sevenbit_strings,
			   &setprintlist, &showprintlist);

  add_setshow_boolean_cmd ("asm-demangle", class_support, &asm_demangle, _("\
Set demangling of C++/ObjC names in disassembly listings."), _("\
Show demangling of C++/ObjC names in disassembly listings."), NULL,
			   NULL,
			   show_asm_demangle,
			   &setprintlist, &showprintlist);

  add_setshow_boolean_cmd ("timestamp", class_maintenance,
			    &debug_timestamp, _("\
Set timestamping of debugging messages."), _("\
Show timestamping of debugging messages."), _("\
When set, debugging messages will be marked with seconds and microseconds."),
			   NULL,
			   show_debug_timestamp,
			   &setdebuglist, &showdebuglist);
}

/* Machine specific function to handle SIGWINCH signal. */

#ifdef  SIGWINCH_HANDLER_BODY
SIGWINCH_HANDLER_BODY
#endif
/* print routines to handle variable size regs, etc. */
/* temporary storage using circular buffer */
#define NUMCELLS 16
#define CELLSIZE 50
static char *
get_cell (void)
{
  static char buf[NUMCELLS][CELLSIZE];
  static int cell = 0;
  if (++cell >= NUMCELLS)
    cell = 0;
  return buf[cell];
}

const char *
paddress (struct gdbarch *gdbarch, CORE_ADDR addr)
{
  /* Truncate address to the size of a target address, avoiding shifts
     larger or equal than the width of a CORE_ADDR.  The local
     variable ADDR_BIT stops the compiler reporting a shift overflow
     when it won't occur. */
  /* NOTE: This assumes that the significant address information is
     kept in the least significant bits of ADDR - the upper bits were
     either zero or sign extended.  Should gdbarch_address_to_pointer or
     some ADDRESS_TO_PRINTABLE() be used to do the conversion?  */

  int addr_bit = gdbarch_addr_bit (gdbarch);

  if (addr_bit < (sizeof (CORE_ADDR) * HOST_CHAR_BIT))
    addr &= ((CORE_ADDR) 1 << addr_bit) - 1;
  return hex_string (addr);
}

static char *
decimal2str (char *sign, ULONGEST addr, int width)
{
  /* Steal code from valprint.c:print_decimal().  Should this worry
     about the real size of addr as the above does? */
  unsigned long temp[3];
  char *str = get_cell ();

  int i = 0;
  do
    {
      temp[i] = addr % (1000 * 1000 * 1000);
      addr /= (1000 * 1000 * 1000);
      i++;
      width -= 9;
    }
  while (addr != 0 && i < (sizeof (temp) / sizeof (temp[0])));

  width += 9;
  if (width < 0)
    width = 0;

  switch (i)
    {
    case 1:
      xsnprintf (str, CELLSIZE, "%s%0*lu", sign, width, temp[0]);
      break;
    case 2:
      xsnprintf (str, CELLSIZE, "%s%0*lu%09lu", sign, width,
		 temp[1], temp[0]);
      break;
    case 3:
      xsnprintf (str, CELLSIZE, "%s%0*lu%09lu%09lu", sign, width,
		 temp[2], temp[1], temp[0]);
      break;
    default:
      internal_error (__FILE__, __LINE__,
		      _("failed internal consistency check"));
    }

  return str;
}

static char *
octal2str (ULONGEST addr, int width)
{
  unsigned long temp[3];
  char *str = get_cell ();

  int i = 0;
  do
    {
      temp[i] = addr % (0100000 * 0100000);
      addr /= (0100000 * 0100000);
      i++;
      width -= 10;
    }
  while (addr != 0 && i < (sizeof (temp) / sizeof (temp[0])));

  width += 10;
  if (width < 0)
    width = 0;

  switch (i)
    {
    case 1:
      if (temp[0] == 0)
	xsnprintf (str, CELLSIZE, "%*o", width, 0);
      else
	xsnprintf (str, CELLSIZE, "0%0*lo", width, temp[0]);
      break;
    case 2:
      xsnprintf (str, CELLSIZE, "0%0*lo%010lo", width, temp[1], temp[0]);
      break;
    case 3:
      xsnprintf (str, CELLSIZE, "0%0*lo%010lo%010lo", width,
		 temp[2], temp[1], temp[0]);
      break;
    default:
      internal_error (__FILE__, __LINE__,
		      _("failed internal consistency check"));
    }

  return str;
}

char *
pulongest (ULONGEST u)
{
  return decimal2str ("", u, 0);
}

char *
plongest (LONGEST l)
{
  if (l < 0)
    return decimal2str ("-", -l, 0);
  else
    return decimal2str ("", l, 0);
}

/* Eliminate warning from compiler on 32-bit systems.  */
static int thirty_two = 32;

char *
phex (ULONGEST l, int sizeof_l)
{
  char *str;

  switch (sizeof_l)
    {
    case 8:
      str = get_cell ();
      xsnprintf (str, CELLSIZE, "%08lx%08lx",
		 (unsigned long) (l >> thirty_two),
		 (unsigned long) (l & 0xffffffff));
      break;
    case 4:
      str = get_cell ();
      xsnprintf (str, CELLSIZE, "%08lx", (unsigned long) l);
      break;
    case 2:
      str = get_cell ();
      xsnprintf (str, CELLSIZE, "%04x", (unsigned short) (l & 0xffff));
      break;
    default:
      str = phex (l, sizeof (l));
      break;
    }

  return str;
}

char *
phex_nz (ULONGEST l, int sizeof_l)
{
  char *str;

  switch (sizeof_l)
    {
    case 8:
      {
	unsigned long high = (unsigned long) (l >> thirty_two);
	str = get_cell ();
	if (high == 0)
	  xsnprintf (str, CELLSIZE, "%lx",
		     (unsigned long) (l & 0xffffffff));
	else
	  xsnprintf (str, CELLSIZE, "%lx%08lx", high,
		     (unsigned long) (l & 0xffffffff));
	break;
      }
    case 4:
      str = get_cell ();
      xsnprintf (str, CELLSIZE, "%lx", (unsigned long) l);
      break;
    case 2:
      str = get_cell ();
      xsnprintf (str, CELLSIZE, "%x", (unsigned short) (l & 0xffff));
      break;
    default:
      str = phex_nz (l, sizeof (l));
      break;
    }

  return str;
}

/* Converts a LONGEST to a C-format hexadecimal literal and stores it
   in a static string.  Returns a pointer to this string.  */
char *
hex_string (LONGEST num)
{
  char *result = get_cell ();
  xsnprintf (result, CELLSIZE, "0x%s", phex_nz (num, sizeof (num)));
  return result;
}

/* Converts a LONGEST number to a C-format hexadecimal literal and
   stores it in a static string.  Returns a pointer to this string
   that is valid until the next call.  The number is padded on the
   left with 0s to at least WIDTH characters.  */
char *
hex_string_custom (LONGEST num, int width)
{
  char *result = get_cell ();
  char *result_end = result + CELLSIZE - 1;
  const char *hex = phex_nz (num, sizeof (num));
  int hex_len = strlen (hex);

  if (hex_len > width)
    width = hex_len;
  if (width + 2 >= CELLSIZE)
    internal_error (__FILE__, __LINE__,
		    _("hex_string_custom: insufficient space to store result"));

  strcpy (result_end - width - 2, "0x");
  memset (result_end - width, '0', width);
  strcpy (result_end - hex_len, hex);
  return result_end - width - 2;
}

/* Convert VAL to a numeral in the given radix.  For
 * radix 10, IS_SIGNED may be true, indicating a signed quantity;
 * otherwise VAL is interpreted as unsigned.  If WIDTH is supplied, 
 * it is the minimum width (0-padded if needed).  USE_C_FORMAT means
 * to use C format in all cases.  If it is false, then 'x' 
 * and 'o' formats do not include a prefix (0x or leading 0). */

char *
int_string (LONGEST val, int radix, int is_signed, int width, 
	    int use_c_format)
{
  switch (radix) 
    {
    case 16:
      {
	char *result;
	if (width == 0)
	  result = hex_string (val);
	else
	  result = hex_string_custom (val, width);
	if (! use_c_format)
	  result += 2;
	return result;
      }
    case 10:
      {
	if (is_signed && val < 0)
	  return decimal2str ("-", -val, width);
	else
	  return decimal2str ("", val, width);
      }
    case 8:
      {
	char *result = octal2str (val, width);
	if (use_c_format || val == 0)
	  return result;
	else
	  return result + 1;
      }
    default:
      internal_error (__FILE__, __LINE__,
		      _("failed internal consistency check"));
    }
}	

/* Convert a CORE_ADDR into a string.  */
const char *
core_addr_to_string (const CORE_ADDR addr)
{
  char *str = get_cell ();
  strcpy (str, "0x");
  strcat (str, phex (addr, sizeof (addr)));
  return str;
}

const char *
core_addr_to_string_nz (const CORE_ADDR addr)
{
  char *str = get_cell ();
  strcpy (str, "0x");
  strcat (str, phex_nz (addr, sizeof (addr)));
  return str;
}

/* Convert a string back into a CORE_ADDR.  */
CORE_ADDR
string_to_core_addr (const char *my_string)
{
  CORE_ADDR addr = 0;

  if (my_string[0] == '0' && tolower (my_string[1]) == 'x')
    {
      /* Assume that it is in hex.  */
      int i;
      for (i = 2; my_string[i] != '\0'; i++)
	{
	  if (isdigit (my_string[i]))
	    addr = (my_string[i] - '0') + (addr * 16);
	  else if (isxdigit (my_string[i]))
	    addr = (tolower (my_string[i]) - 'a' + 0xa) + (addr * 16);
	  else
	    error (_("invalid hex \"%s\""), my_string);
	}
    }
  else
    {
      /* Assume that it is in decimal.  */
      int i;
      for (i = 0; my_string[i] != '\0'; i++)
	{
	  if (isdigit (my_string[i]))
	    addr = (my_string[i] - '0') + (addr * 10);
	  else
	    error (_("invalid decimal \"%s\""), my_string);
	}
    }

  return addr;
}

const char *
host_address_to_string (const void *addr)
{
  char *str = get_cell ();

  xsnprintf (str, CELLSIZE, "0x%s", phex_nz ((uintptr_t) addr, sizeof (addr)));
  return str;
}

char *
gdb_realpath (const char *filename)
{
  /* Method 1: The system has a compile time upper bound on a filename
     path.  Use that and realpath() to canonicalize the name.  This is
     the most common case.  Note that, if there isn't a compile time
     upper bound, you want to avoid realpath() at all costs.  */
#if defined(HAVE_REALPATH)
  {
# if defined (PATH_MAX)
    char buf[PATH_MAX];
#  define USE_REALPATH
# elif defined (MAXPATHLEN)
    char buf[MAXPATHLEN];
#  define USE_REALPATH
# endif
# if defined (USE_REALPATH)
    const char *rp = realpath (filename, buf);
    if (rp == NULL)
      rp = filename;
    return xstrdup (rp);
# endif
  }
#endif /* HAVE_REALPATH */

  /* Method 2: The host system (i.e., GNU) has the function
     canonicalize_file_name() which malloc's a chunk of memory and
     returns that, use that.  */
#if defined(HAVE_CANONICALIZE_FILE_NAME)
  {
    char *rp = canonicalize_file_name (filename);
    if (rp == NULL)
      return xstrdup (filename);
    else
      return rp;
  }
#endif

  /* FIXME: cagney/2002-11-13:

     Method 2a: Use realpath() with a NULL buffer.  Some systems, due
     to the problems described in in method 3, have modified their
     realpath() implementation so that it will allocate a buffer when
     NULL is passed in.  Before this can be used, though, some sort of
     configure time test would need to be added.  Otherwize the code
     will likely core dump.  */

  /* Method 3: Now we're getting desperate!  The system doesn't have a
     compile time buffer size and no alternative function.  Query the
     OS, using pathconf(), for the buffer limit.  Care is needed
     though, some systems do not limit PATH_MAX (return -1 for
     pathconf()) making it impossible to pass a correctly sized buffer
     to realpath() (it could always overflow).  On those systems, we
     skip this.  */
#if defined (HAVE_REALPATH) && defined (HAVE_UNISTD_H) && defined(HAVE_ALLOCA)
  {
    /* Find out the max path size.  */
    long path_max = pathconf ("/", _PC_PATH_MAX);
    if (path_max > 0)
      {
	/* PATH_MAX is bounded.  */
	char *buf = alloca (path_max);
	char *rp = realpath (filename, buf);
	return xstrdup (rp ? rp : filename);
      }
  }
#endif

  /* This system is a lost cause, just dup the buffer.  */
  return xstrdup (filename);
}

/* Return a copy of FILENAME, with its directory prefix canonicalized
   by gdb_realpath.  */

char *
xfullpath (const char *filename)
{
  const char *base_name = lbasename (filename);
  char *dir_name;
  char *real_path;
  char *result;

  /* Extract the basename of filename, and return immediately 
     a copy of filename if it does not contain any directory prefix. */
  if (base_name == filename)
    return xstrdup (filename);

  dir_name = alloca ((size_t) (base_name - filename + 2));
  /* Allocate enough space to store the dir_name + plus one extra
     character sometimes needed under Windows (see below), and
     then the closing \000 character */
  strncpy (dir_name, filename, base_name - filename);
  dir_name[base_name - filename] = '\000';

#ifdef HAVE_DOS_BASED_FILE_SYSTEM
  /* We need to be careful when filename is of the form 'd:foo', which
     is equivalent of d:./foo, which is totally different from d:/foo.  */
  if (strlen (dir_name) == 2 && isalpha (dir_name[0]) && dir_name[1] == ':')
    {
      dir_name[2] = '.';
      dir_name[3] = '\000';
    }
#endif

  /* Canonicalize the directory prefix, and build the resulting
     filename. If the dirname realpath already contains an ending
     directory separator, avoid doubling it.  */
  real_path = gdb_realpath (dir_name);
  if (IS_DIR_SEPARATOR (real_path[strlen (real_path) - 1]))
    result = concat (real_path, base_name, (char *)NULL);
  else
    result = concat (real_path, SLASH_STRING, base_name, (char *)NULL);

  xfree (real_path);
  return result;
}


/* This is the 32-bit CRC function used by the GNU separate debug
   facility.  An executable may contain a section named
   .gnu_debuglink, which holds the name of a separate executable file
   containing its debug info, and a checksum of that file's contents,
   computed using this function.  */
unsigned long
gnu_debuglink_crc32 (unsigned long crc, unsigned char *buf, size_t len)
{
  static const unsigned long crc32_table[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419,
    0x706af48f, 0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4,
    0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07,
    0x90bf1d91, 0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de,
    0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7, 0x136c9856,
    0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
    0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4,
    0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
    0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3,
    0x45df5c75, 0xdcd60dcf, 0xabd13d59, 0x26d930ac, 0x51de003a,
    0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599,
    0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190,
    0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f,
    0x9fbfe4a5, 0xe8b8d433, 0x7807c9a2, 0x0f00f934, 0x9609a88e,
    0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
    0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed,
    0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
    0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3,
    0xfbd44c65, 0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2,
    0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a,
    0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5,
    0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa, 0xbe0b1010,
    0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17,
    0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6,
    0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615,
    0x73dc1683, 0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8,
    0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1, 0xf00f9344,
    0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
    0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a,
    0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
    0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1,
    0xa6bc5767, 0x3fb506dd, 0x48b2364b, 0xd80d2bda, 0xaf0a1b4c,
    0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef,
    0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe,
    0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31,
    0x2cd99e8b, 0x5bdeae1d, 0x9b64c2b0, 0xec63f226, 0x756aa39c,
    0x026d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
    0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b,
    0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
    0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1,
    0x18b74777, 0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c,
    0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45, 0xa00ae278,
    0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7,
    0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc, 0x40df0b66,
    0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605,
    0xcdd70693, 0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8,
    0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b,
    0x2d02ef8d
  };
  unsigned char *end;

  crc = ~crc & 0xffffffff;
  for (end = buf + len; buf < end; ++buf)
    crc = crc32_table[(crc ^ *buf) & 0xff] ^ (crc >> 8);
  return ~crc & 0xffffffff;;
}

ULONGEST
align_up (ULONGEST v, int n)
{
  /* Check that N is really a power of two.  */
  gdb_assert (n && (n & (n-1)) == 0);
  return (v + n - 1) & -n;
}

ULONGEST
align_down (ULONGEST v, int n)
{
  /* Check that N is really a power of two.  */
  gdb_assert (n && (n & (n-1)) == 0);
  return (v & -n);
}

/* Allocation function for the libiberty hash table which uses an
   obstack.  The obstack is passed as DATA.  */

void *
hashtab_obstack_allocate (void *data, size_t size, size_t count)
{
  unsigned int total = size * count;
  void *ptr = obstack_alloc ((struct obstack *) data, total);
  memset (ptr, 0, total);
  return ptr;
}

/* Trivial deallocation function for the libiberty splay tree and hash
   table - don't deallocate anything.  Rely on later deletion of the
   obstack.  DATA will be the obstack, although it is not needed
   here.  */

void
dummy_obstack_deallocate (void *object, void *data)
{
  return;
}

/* The bit offset of the highest byte in a ULONGEST, for overflow
   checking.  */

#define HIGH_BYTE_POSN ((sizeof (ULONGEST) - 1) * HOST_CHAR_BIT)

/* True (non-zero) iff DIGIT is a valid digit in radix BASE,
   where 2 <= BASE <= 36.  */

static int
is_digit_in_base (unsigned char digit, int base)
{
  if (!isalnum (digit))
    return 0;
  if (base <= 10)
    return (isdigit (digit) && digit < base + '0');
  else
    return (isdigit (digit) || tolower (digit) < base - 10 + 'a');
}

static int
digit_to_int (unsigned char c)
{
  if (isdigit (c))
    return c - '0';
  else
    return tolower (c) - 'a' + 10;
}

/* As for strtoul, but for ULONGEST results.  */

ULONGEST
strtoulst (const char *num, const char **trailer, int base)
{
  unsigned int high_part;
  ULONGEST result;
  int minus = 0;
  int i = 0;

  /* Skip leading whitespace.  */
  while (isspace (num[i]))
    i++;

  /* Handle prefixes.  */
  if (num[i] == '+')
    i++;
  else if (num[i] == '-')
    {
      minus = 1;
      i++;
    }

  if (base == 0 || base == 16)
    {
      if (num[i] == '0' && (num[i + 1] == 'x' || num[i + 1] == 'X'))
	{
	  i += 2;
	  if (base == 0)
	    base = 16;
	}
    }

  if (base == 0 && num[i] == '0')
    base = 8;

  if (base == 0)
    base = 10;

  if (base < 2 || base > 36)
    {
      errno = EINVAL;
      return 0;
    }

  result = high_part = 0;
  for (; is_digit_in_base (num[i], base); i += 1)
    {
      result = result * base + digit_to_int (num[i]);
      high_part = high_part * base + (unsigned int) (result >> HIGH_BYTE_POSN);
      result &= ((ULONGEST) 1 << HIGH_BYTE_POSN) - 1;
      if (high_part > 0xff)
	{
	  errno = ERANGE;
	  result = ~ (ULONGEST) 0;
	  high_part = 0;
	  minus = 0;
	  break;
	}
    }

  if (trailer != NULL)
    *trailer = &num[i];

  result = result + ((ULONGEST) high_part << HIGH_BYTE_POSN);
  if (minus)
    return -result;
  else
    return result;
}

/* Simple, portable version of dirname that does not modify its
   argument.  */

char *
ldirname (const char *filename)
{
  const char *base = lbasename (filename);
  char *dirname;

  while (base > filename && IS_DIR_SEPARATOR (base[-1]))
    --base;

  if (base == filename)
    return NULL;

  dirname = xmalloc (base - filename + 2);
  memcpy (dirname, filename, base - filename);

  /* On DOS based file systems, convert "d:foo" to "d:.", so that we
     create "d:./bar" later instead of the (different) "d:/bar".  */
  if (base - filename == 2 && IS_ABSOLUTE_PATH (base)
      && !IS_DIR_SEPARATOR (filename[0]))
    dirname[base++ - filename] = '.';

  dirname[base - filename] = '\0';
  return dirname;
}

/* Call libiberty's buildargv, and return the result.
   If buildargv fails due to out-of-memory, call nomem.
   Therefore, the returned value is guaranteed to be non-NULL,
   unless the parameter itself is NULL.  */

char **
gdb_buildargv (const char *s)
{
  char **argv = buildargv (s);
  if (s != NULL && argv == NULL)
    nomem (0);
  return argv;
}

/* Provide a prototype to silence -Wmissing-prototypes.  */
extern initialize_file_ftype _initialize_utils;

void
_initialize_utils (void)
{
  add_internal_problem_command (&internal_error_problem);
  add_internal_problem_command (&internal_warning_problem);
}

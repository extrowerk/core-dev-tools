/* Top level stuff for GDB, the GNU debugger.

   Copyright (C) 1999, 2000, 2001, 2002, 2004, 2005, 2007, 2008, 2009, 2010
   Free Software Foundation, Inc.

   Written by Elena Zannoni <ezannoni@cygnus.com> of Cygnus Solutions.

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
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include "defs.h"
#include "top.h"
#include "inferior.h"
#include "target.h"
#include "terminal.h"		/* for job_control */
#include "event-loop.h"
#include "event-top.h"
#include "interps.h"
#include <signal.h>
#include "exceptions.h"
#include "cli/cli-script.h"     /* for reset_command_nest_depth */
#include "main.h"
#include "gdbthread.h"

/* For dont_repeat() */
#include "gdbcmd.h"

/* readline include files */
#include "readline/readline.h"
#include "readline/history.h"

/* readline defines this.  */
#undef savestring

static void rl_callback_read_char_wrapper (gdb_client_data client_data);
static void command_line_handler (char *rl);
static void change_line_handler (void);
static void change_annotation_level (void);
static void command_handler (char *command);

/* Signal handlers. */
#ifdef SIGQUIT
static void handle_sigquit (int sig);
#endif
#ifdef SIGHUP
static void handle_sighup (int sig);
#endif
static void handle_sigfpe (int sig);
#if defined(SIGWINCH) && defined(SIGWINCH_HANDLER)
static void handle_sigwinch (int sig);
#endif

/* Functions to be invoked by the event loop in response to
   signals. */
#if defined (SIGQUIT) || defined (SIGHUP)
static void async_do_nothing (gdb_client_data);
#endif
#ifdef SIGHUP
static void async_disconnect (gdb_client_data);
#endif
static void async_float_handler (gdb_client_data);
#ifdef STOP_SIGNAL
static void async_stop_sig (gdb_client_data);
#endif

/* Readline offers an alternate interface, via callback
   functions. These are all included in the file callback.c in the
   readline distribution.  This file provides (mainly) a function, which
   the event loop uses as callback (i.e. event handler) whenever an event
   is detected on the standard input file descriptor.
   readline_callback_read_char is called (by the GDB event loop) whenever
   there is a new character ready on the input stream. This function
   incrementally builds a buffer internal to readline where it
   accumulates the line read up to the point of invocation.  In the
   special case in which the character read is newline, the function
   invokes a GDB supplied callback routine, which does the processing of
   a full command line.  This latter routine is the asynchronous analog
   of the old command_line_input in gdb. Instead of invoking (and waiting
   for) readline to read the command line and pass it back to
   command_loop for processing, the new command_line_handler function has
   the command line already available as its parameter.  INPUT_HANDLER is
   to be set to the function that readline will invoke when a complete
   line of input is ready.  CALL_READLINE is to be set to the function
   that readline offers as callback to the event_loop. */

void (*input_handler) (char *);
void (*call_readline) (gdb_client_data);

/* Important variables for the event loop. */

/* This is used to determine if GDB is using the readline library or
   its own simplified form of readline. It is used by the asynchronous
   form of the set editing command.
   ezannoni: as of 1999-04-29 I expect that this
   variable will not be used after gdb is changed to use the event
   loop as default engine, and event-top.c is merged into top.c. */
int async_command_editing_p;

/* This variable contains the new prompt that the user sets with the
   set prompt command. */
char *new_async_prompt;

/* This is the annotation suffix that will be used when the
   annotation_level is 2. */
char *async_annotation_suffix;

/* This is used to display the notification of the completion of an
   asynchronous execution command. */
int exec_done_display_p = 0;

/* This is the file descriptor for the input stream that GDB uses to
   read commands from. */
int input_fd;

/* This is the prompt stack. Prompts will be pushed on the stack as
   needed by the different 'kinds' of user inputs GDB is asking
   for. See event-loop.h. */
struct prompts the_prompts;

/* signal handling variables */
/* Each of these is a pointer to a function that the event loop will
   invoke if the corresponding signal has received. The real signal
   handlers mark these functions as ready to be executed and the event
   loop, in a later iteration, calls them. See the function
   invoke_async_signal_handler. */
void *sigint_token;
#ifdef SIGHUP
void *sighup_token;
#endif
#ifdef SIGQUIT
void *sigquit_token;
#endif
void *sigfpe_token;
#if defined(SIGWINCH) && defined(SIGWINCH_HANDLER)
void *sigwinch_token;
#endif
#ifdef STOP_SIGNAL
void *sigtstp_token;
#endif

/* Structure to save a partially entered command.  This is used when
   the user types '\' at the end of a command line. This is necessary
   because each line of input is handled by a different call to
   command_line_handler, and normally there is no state retained
   between different calls. */
int more_to_come = 0;

struct readline_input_state
  {
    char *linebuffer;
    char *linebuffer_ptr;
  }
readline_input_state;

/* This hook is called by rl_callback_read_char_wrapper after each
   character is processed.  */
void (*after_char_processing_hook) ();


/* Wrapper function for calling into the readline library. The event
   loop expects the callback function to have a paramter, while readline 
   expects none. */
static void
rl_callback_read_char_wrapper (gdb_client_data client_data)
{
  rl_callback_read_char ();
  if (after_char_processing_hook)
    (*after_char_processing_hook) ();
}

/* Initialize all the necessary variables, start the event loop,
   register readline, and stdin, start the loop. */
void
cli_command_loop (void)
{
  /* If we are using readline, set things up and display the first
     prompt, otherwise just print the prompt. */
  if (async_command_editing_p)
    {
      int length;
      char *a_prompt;
      char *gdb_prompt = get_prompt ();

      /* Tell readline what the prompt to display is and what function it
         will need to call after a whole line is read. This also displays
         the first prompt. */
      length = strlen (PREFIX (0)) 
	+ strlen (gdb_prompt) + strlen (SUFFIX (0)) + 1;
      a_prompt = (char *) alloca (length);
      strcpy (a_prompt, PREFIX (0));
      strcat (a_prompt, gdb_prompt);
      strcat (a_prompt, SUFFIX (0));
      rl_callback_handler_install (a_prompt, input_handler);
    }
  else
    display_gdb_prompt (0);

  /* Now it's time to start the event loop. */
  start_event_loop ();
}

/* Change the function to be invoked every time there is a character
   ready on stdin. This is used when the user sets the editing off,
   therefore bypassing readline, and letting gdb handle the input
   itself, via gdb_readline2. Also it is used in the opposite case in
   which the user sets editing on again, by restoring readline
   handling of the input. */
static void
change_line_handler (void)
{
  /* NOTE: this operates on input_fd, not instream. If we are reading
     commands from a file, instream will point to the file. However in
     async mode, we always read commands from a file with editing
     off. This means that the 'set editing on/off' will have effect
     only on the interactive session. */

  if (async_command_editing_p)
    {
      /* Turn on editing by using readline. */
      call_readline = rl_callback_read_char_wrapper;
      input_handler = command_line_handler;
    }
  else
    {
      /* Turn off editing by using gdb_readline2. */
      rl_callback_handler_remove ();
      call_readline = gdb_readline2;

      /* Set up the command handler as well, in case we are called as
         first thing from .gdbinit. */
      input_handler = command_line_handler;
    }
}

/* Displays the prompt. The prompt that is displayed is the current
   top of the prompt stack, if the argument NEW_PROMPT is
   0. Otherwise, it displays whatever NEW_PROMPT is. This is used
   after each gdb command has completed, and in the following cases:
   1. when the user enters a command line which is ended by '\'
   indicating that the command will continue on the next line.
   In that case the prompt that is displayed is the empty string.
   2. When the user is entering 'commands' for a breakpoint, or
   actions for a tracepoint. In this case the prompt will be '>'
   3. Other????
   FIXME: 2. & 3. not implemented yet for async. */
void
display_gdb_prompt (char *new_prompt)
{
  int prompt_length = 0;
  char *gdb_prompt = get_prompt ();

  /* Reset the nesting depth used when trace-commands is set.  */
  reset_command_nest_depth ();

  /* Each interpreter has its own rules on displaying the command
     prompt.  */
  if (!current_interp_display_prompt_p ())
    return;

  if (sync_execution && is_running (inferior_ptid))
    {
      /* This is to trick readline into not trying to display the
         prompt.  Even though we display the prompt using this
         function, readline still tries to do its own display if we
         don't call rl_callback_handler_install and
         rl_callback_handler_remove (which readline detects because a
         global variable is not set). If readline did that, it could
         mess up gdb signal handlers for SIGINT.  Readline assumes
         that between calls to rl_set_signals and rl_clear_signals gdb
         doesn't do anything with the signal handlers. Well, that's
         not the case, because when the target executes we change the
         SIGINT signal handler. If we allowed readline to display the
         prompt, the signal handler change would happen exactly
         between the calls to the above two functions.
         Calling rl_callback_handler_remove(), does the job. */

      rl_callback_handler_remove ();
      return;
    }

  if (!new_prompt)
    {
      /* Just use the top of the prompt stack. */
      prompt_length = strlen (PREFIX (0)) +
	strlen (SUFFIX (0)) +
	strlen (gdb_prompt) + 1;

      new_prompt = (char *) alloca (prompt_length);

      /* Prefix needs to have new line at end. */
      strcpy (new_prompt, PREFIX (0));
      strcat (new_prompt, gdb_prompt);
      /* Suffix needs to have a new line at end and \032 \032 at
         beginning. */
      strcat (new_prompt, SUFFIX (0));
    }

  if (async_command_editing_p)
    {
      rl_callback_handler_remove ();
      rl_callback_handler_install (new_prompt, input_handler);
    }
  /* new_prompt at this point can be the top of the stack or the one passed in */
  else if (new_prompt)
    {
      /* Don't use a _filtered function here.  It causes the assumed
         character position to be off, since the newline we read from
         the user is not accounted for.  */
      fputs_unfiltered (new_prompt, gdb_stdout);
      gdb_flush (gdb_stdout);
    }
}

/* Used when the user requests a different annotation level, with
   'set annotate'. It pushes a new prompt (with prefix and suffix) on top
   of the prompt stack, if the annotation level desired is 2, otherwise
   it pops the top of the prompt stack when we want the annotation level
   to be the normal ones (1 or 0). */
static void
change_annotation_level (void)
{
  char *prefix, *suffix;

  if (!PREFIX (0) || !PROMPT (0) || !SUFFIX (0))
    {
      /* The prompt stack has not been initialized to "", we are
         using gdb w/o the --async switch */
      warning (_("Command has same effect as set annotate"));
      return;
    }

  if (annotation_level > 1)
    {
      if (!strcmp (PREFIX (0), "") && !strcmp (SUFFIX (0), ""))
	{
	  /* Push a new prompt if the previous annotation_level was not >1. */
	  prefix = (char *) alloca (strlen (async_annotation_suffix) + 10);
	  strcpy (prefix, "\n\032\032pre-");
	  strcat (prefix, async_annotation_suffix);
	  strcat (prefix, "\n");

	  suffix = (char *) alloca (strlen (async_annotation_suffix) + 6);
	  strcpy (suffix, "\n\032\032");
	  strcat (suffix, async_annotation_suffix);
	  strcat (suffix, "\n");

	  push_prompt (prefix, (char *) 0, suffix);
	}
    }
  else
    {
      if (strcmp (PREFIX (0), "") && strcmp (SUFFIX (0), ""))
	{
	  /* Pop the top of the stack, we are going back to annotation < 1. */
	  pop_prompt ();
	}
    }
}

/* Pushes a new prompt on the prompt stack. Each prompt has three
   parts: prefix, prompt, suffix. Usually prefix and suffix are empty
   strings, except when the annotation level is 2. Memory is allocated
   within xstrdup for the new prompt. */
void
push_prompt (char *prefix, char *prompt, char *suffix)
{
  the_prompts.top++;
  PREFIX (0) = xstrdup (prefix);

  /* Note that this function is used by the set annotate 2
     command. This is why we take care of saving the old prompt
     in case a new one is not specified. */
  if (prompt)
    PROMPT (0) = xstrdup (prompt);
  else
    PROMPT (0) = xstrdup (PROMPT (-1));

  SUFFIX (0) = xstrdup (suffix);
}

/* Pops the top of the prompt stack, and frees the memory allocated for it. */
void
pop_prompt (void)
{
  /* If we are not during a 'synchronous' execution command, in which
     case, the top prompt would be empty. */
  if (strcmp (PROMPT (0), ""))
    /* This is for the case in which the prompt is set while the
       annotation level is 2. The top prompt will be changed, but when
       we return to annotation level < 2, we want that new prompt to be
       in effect, until the user does another 'set prompt'. */
    if (strcmp (PROMPT (0), PROMPT (-1)))
      {
	xfree (PROMPT (-1));
	PROMPT (-1) = xstrdup (PROMPT (0));
      }

  xfree (PREFIX (0));
  xfree (PROMPT (0));
  xfree (SUFFIX (0));
  the_prompts.top--;
}

/* When there is an event ready on the stdin file desriptor, instead
   of calling readline directly throught the callback function, or
   instead of calling gdb_readline2, give gdb a chance to detect
   errors and do something. */
void
stdin_event_handler (int error, gdb_client_data client_data)
{
  if (error)
    {
      printf_unfiltered (_("error detected on stdin\n"));
      delete_file_handler (input_fd);
      discard_all_continuations ();
      discard_all_intermediate_continuations ();
      /* If stdin died, we may as well kill gdb. */
      quit_command ((char *) 0, stdin == instream);
    }
  else
    (*call_readline) (client_data);
}

/* Re-enable stdin after the end of an execution command in
   synchronous mode, or after an error from the target, and we aborted
   the exec operation. */

void
async_enable_stdin (void)
{
  if (sync_execution)
    {
      /* See NOTE in async_disable_stdin() */
      /* FIXME: cagney/1999-09-27: Call this before clearing
	 sync_execution.  Current target_terminal_ours() implementations
	 check for sync_execution before switching the terminal. */
      target_terminal_ours ();
      pop_prompt ();
      sync_execution = 0;
    }
}

/* Disable reads from stdin (the console) marking the command as
   synchronous. */

void
async_disable_stdin (void)
{
  if (!sync_execution)
    {
      sync_execution = 1;
      push_prompt ("", "", "");
    }
}


/* Handles a gdb command. This function is called by
   command_line_handler, which has processed one or more input lines
   into COMMAND. */
/* NOTE: 1999-04-30 This is the asynchronous version of the command_loop
   function.  The command_loop function will be obsolete when we
   switch to use the event loop at every execution of gdb. */
static void
command_handler (char *command)
{
  int stdin_is_tty = ISATTY (stdin);
  long time_at_cmd_start;
#ifdef HAVE_SBRK
  long space_at_cmd_start = 0;
#endif
  extern int display_time;
  extern int display_space;

  quit_flag = 0;
  if (instream == stdin && stdin_is_tty)
    reinitialize_more_filter ();

  /* If readline returned a NULL command, it means that the 
     connection with the terminal is gone. This happens at the
     end of a testsuite run, after Expect has hung up 
     but GDB is still alive. In such a case, we just quit gdb
     killing the inferior program too. */
  if (command == 0)
    {
      printf_unfiltered ("quit\n");
      execute_command ("quit", stdin == instream);
    }

  time_at_cmd_start = get_run_time ();

  if (display_space)
    {
#ifdef HAVE_SBRK
      char *lim = (char *) sbrk (0);

      space_at_cmd_start = lim - lim_at_start;
#endif
    }

  execute_command (command, instream == stdin);

  /* Do any commands attached to breakpoint we stopped at.  */
  bpstat_do_actions ();

  if (display_time)
    {
      long cmd_time = get_run_time () - time_at_cmd_start;

      printf_unfiltered (_("Command execution time: %ld.%06ld\n"),
			 cmd_time / 1000000, cmd_time % 1000000);
    }

  if (display_space)
    {
#ifdef HAVE_SBRK
      char *lim = (char *) sbrk (0);
      long space_now = lim - lim_at_start;
      long space_diff = space_now - space_at_cmd_start;

      printf_unfiltered (_("Space used: %ld (%c%ld for this command)\n"),
			 space_now,
			 (space_diff >= 0 ? '+' : '-'),
			 space_diff);
#endif
    }
}

/* Handle a complete line of input. This is called by the callback
   mechanism within the readline library.  Deal with incomplete commands
   as well, by saving the partial input in a global buffer.  */

/* NOTE: 1999-04-30 This is the asynchronous version of the
   command_line_input function. command_line_input will become
   obsolete once we use the event loop as the default mechanism in
   GDB. */
static void
command_line_handler (char *rl)
{
  static char *linebuffer = 0;
  static unsigned linelength = 0;
  char *p;
  char *p1;
  extern char *line;
  extern int linesize;
  char *nline;
  char got_eof = 0;

  int repeat = (instream == stdin);

  if (annotation_level > 1 && instream == stdin)
    {
      printf_unfiltered (("\n\032\032post-"));
      puts_unfiltered (async_annotation_suffix);
      printf_unfiltered (("\n"));
    }

  if (linebuffer == 0)
    {
      linelength = 80;
      linebuffer = (char *) xmalloc (linelength);
    }

  p = linebuffer;

  if (more_to_come)
    {
      strcpy (linebuffer, readline_input_state.linebuffer);
      p = readline_input_state.linebuffer_ptr;
      xfree (readline_input_state.linebuffer);
      more_to_come = 0;
      pop_prompt ();
    }

#ifdef STOP_SIGNAL
  if (job_control)
    signal (STOP_SIGNAL, handle_stop_sig);
#endif

  /* Make sure that all output has been output.  Some machines may let
     you get away with leaving out some of the gdb_flush, but not all.  */
  wrap_here ("");
  gdb_flush (gdb_stdout);
  gdb_flush (gdb_stderr);

  if (source_file_name != NULL)
    ++source_line_number;

  /* If we are in this case, then command_handler will call quit 
     and exit from gdb. */
  if (!rl || rl == (char *) EOF)
    {
      got_eof = 1;
      command_handler (0);
      return;			/* Lint. */
    }
  if (strlen (rl) + 1 + (p - linebuffer) > linelength)
    {
      linelength = strlen (rl) + 1 + (p - linebuffer);
      nline = (char *) xrealloc (linebuffer, linelength);
      p += nline - linebuffer;
      linebuffer = nline;
    }
  p1 = rl;
  /* Copy line.  Don't copy null at end.  (Leaves line alone
     if this was just a newline)  */
  while (*p1)
    *p++ = *p1++;

  xfree (rl);			/* Allocated in readline.  */

  if (p > linebuffer && *(p - 1) == '\\')
    {
      *p = '\0';
      p--;			/* Put on top of '\'.  */

      readline_input_state.linebuffer = xstrdup (linebuffer);
      readline_input_state.linebuffer_ptr = p;

      /* We will not invoke a execute_command if there is more
	 input expected to complete the command. So, we need to
	 print an empty prompt here. */
      more_to_come = 1;
      push_prompt ("", "", "");
      display_gdb_prompt (0);
      return;
    }

#ifdef STOP_SIGNAL
  if (job_control)
    signal (STOP_SIGNAL, SIG_DFL);
#endif

#define SERVER_COMMAND_LENGTH 7
  server_command =
    (p - linebuffer > SERVER_COMMAND_LENGTH)
    && strncmp (linebuffer, "server ", SERVER_COMMAND_LENGTH) == 0;
  if (server_command)
    {
      /* Note that we don't set `line'.  Between this and the check in
         dont_repeat, this insures that repeating will still do the
         right thing.  */
      *p = '\0';
      command_handler (linebuffer + SERVER_COMMAND_LENGTH);
      display_gdb_prompt (0);
      return;
    }

  /* Do history expansion if that is wished.  */
  if (history_expansion_p && instream == stdin
      && ISATTY (instream))
    {
      char *history_value;
      int expanded;

      *p = '\0';		/* Insert null now.  */
      expanded = history_expand (linebuffer, &history_value);
      if (expanded)
	{
	  /* Print the changes.  */
	  printf_unfiltered ("%s\n", history_value);

	  /* If there was an error, call this function again.  */
	  if (expanded < 0)
	    {
	      xfree (history_value);
	      return;
	    }
	  if (strlen (history_value) > linelength)
	    {
	      linelength = strlen (history_value) + 1;
	      linebuffer = (char *) xrealloc (linebuffer, linelength);
	    }
	  strcpy (linebuffer, history_value);
	  p = linebuffer + strlen (linebuffer);
	}
      xfree (history_value);
    }

  /* If we just got an empty line, and that is supposed
     to repeat the previous command, return the value in the
     global buffer.  */
  if (repeat && p == linebuffer && *p != '\\')
    {
      command_handler (line);
      display_gdb_prompt (0);
      return;
    }

  for (p1 = linebuffer; *p1 == ' ' || *p1 == '\t'; p1++);
  if (repeat && !*p1)
    {
      command_handler (line);
      display_gdb_prompt (0);
      return;
    }

  *p = 0;

  /* Add line to history if appropriate.  */
  if (instream == stdin
      && ISATTY (stdin) && *linebuffer)
    add_history (linebuffer);

  /* Note: lines consisting solely of comments are added to the command
     history.  This is useful when you type a command, and then
     realize you don't want to execute it quite yet.  You can comment
     out the command and then later fetch it from the value history
     and remove the '#'.  The kill ring is probably better, but some
     people are in the habit of commenting things out.  */
  if (*p1 == '#')
    *p1 = '\0';			/* Found a comment. */

  /* Save into global buffer if appropriate.  */
  if (repeat)
    {
      if (linelength > linesize)
	{
	  line = xrealloc (line, linelength);
	  linesize = linelength;
	}
      strcpy (line, linebuffer);
      if (!more_to_come)
	{
	  command_handler (line);
	  display_gdb_prompt (0);
	}
      return;
    }

  command_handler (linebuffer);
  display_gdb_prompt (0);
  return;
}

/* Does reading of input from terminal w/o the editing features
   provided by the readline library. */

/* NOTE: 1999-04-30 Asynchronous version of gdb_readline. gdb_readline
   will become obsolete when the event loop is made the default
   execution for gdb. */
void
gdb_readline2 (gdb_client_data client_data)
{
  int c;
  char *result;
  int input_index = 0;
  int result_size = 80;
  static int done_once = 0;

  /* Unbuffer the input stream, so that, later on, the calls to fgetc
     fetch only one char at the time from the stream. The fgetc's will
     get up to the first newline, but there may be more chars in the
     stream after '\n'. If we buffer the input and fgetc drains the
     stream, getting stuff beyond the newline as well, a select, done
     afterwards will not trigger. */
  if (!done_once && !ISATTY (instream))
    {
      setbuf (instream, NULL);
      done_once = 1;
    }

  result = (char *) xmalloc (result_size);

  /* We still need the while loop here, even though it would seem
     obvious to invoke gdb_readline2 at every character entered.  If
     not using the readline library, the terminal is in cooked mode,
     which sends the characters all at once. Poll will notice that the
     input fd has changed state only after enter is pressed. At this
     point we still need to fetch all the chars entered. */

  while (1)
    {
      /* Read from stdin if we are executing a user defined command.
         This is the right thing for prompt_for_continue, at least.  */
      c = fgetc (instream ? instream : stdin);

      if (c == EOF)
	{
	  if (input_index > 0)
	    /* The last line does not end with a newline.  Return it, and
	       if we are called again fgetc will still return EOF and
	       we'll return NULL then.  */
	    break;
	  xfree (result);
	  (*input_handler) (0);
	  return;
	}

      if (c == '\n')
	{
	  if (input_index > 0 && result[input_index - 1] == '\r')
	    input_index--;
	  break;
	}

      result[input_index++] = c;
      while (input_index >= result_size)
	{
	  result_size *= 2;
	  result = (char *) xrealloc (result, result_size);
	}
    }

  result[input_index++] = '\0';
  (*input_handler) (result);
}


/* Initialization of signal handlers and tokens.  There is a function
   handle_sig* for each of the signals GDB cares about. Specifically:
   SIGINT, SIGFPE, SIGQUIT, SIGTSTP, SIGHUP, SIGWINCH.  These
   functions are the actual signal handlers associated to the signals
   via calls to signal().  The only job for these functions is to
   enqueue the appropriate event/procedure with the event loop.  Such
   procedures are the old signal handlers. The event loop will take
   care of invoking the queued procedures to perform the usual tasks
   associated with the reception of the signal. */
/* NOTE: 1999-04-30 This is the asynchronous version of init_signals.
   init_signals will become obsolete as we move to have to event loop
   as the default for gdb. */
void
async_init_signals (void)
{
  signal (SIGINT, handle_sigint);
  sigint_token =
    create_async_signal_handler (async_request_quit, NULL);
  signal (SIGTERM, handle_sigterm);

  /* If SIGTRAP was set to SIG_IGN, then the SIG_IGN will get passed
     to the inferior and breakpoints will be ignored.  */
#ifdef SIGTRAP
  signal (SIGTRAP, SIG_DFL);
#endif

#ifdef SIGQUIT
  /* If we initialize SIGQUIT to SIG_IGN, then the SIG_IGN will get
     passed to the inferior, which we don't want.  It would be
     possible to do a "signal (SIGQUIT, SIG_DFL)" after we fork, but
     on BSD4.3 systems using vfork, that can affect the
     GDB process as well as the inferior (the signal handling tables
     might be in memory, shared between the two).  Since we establish
     a handler for SIGQUIT, when we call exec it will set the signal
     to SIG_DFL for us.  */
  signal (SIGQUIT, handle_sigquit);
  sigquit_token =
    create_async_signal_handler (async_do_nothing, NULL);
#endif
#ifdef SIGHUP
  if (signal (SIGHUP, handle_sighup) != SIG_IGN)
    sighup_token =
      create_async_signal_handler (async_disconnect, NULL);
  else
    sighup_token =
      create_async_signal_handler (async_do_nothing, NULL);
#endif
  signal (SIGFPE, handle_sigfpe);
  sigfpe_token =
    create_async_signal_handler (async_float_handler, NULL);

#if defined(SIGWINCH) && defined(SIGWINCH_HANDLER)
  signal (SIGWINCH, handle_sigwinch);
  sigwinch_token =
    create_async_signal_handler (SIGWINCH_HANDLER, NULL);
#endif
#ifdef STOP_SIGNAL
  sigtstp_token =
    create_async_signal_handler (async_stop_sig, NULL);
#endif

}

void
mark_async_signal_handler_wrapper (void *token)
{
  mark_async_signal_handler ((struct async_signal_handler *) token);
}

/* Tell the event loop what to do if SIGINT is received. 
   See event-signal.c. */
void
handle_sigint (int sig)
{
  signal (sig, handle_sigint);

  /* We could be running in a loop reading in symfiles or something so
     it may be quite a while before we get back to the event loop.  So
     set quit_flag to 1 here. Then if QUIT is called before we get to
     the event loop, we will unwind as expected.  */

  quit_flag = 1;

  /* If immediate_quit is set, we go ahead and process the SIGINT right
     away, even if we usually would defer this to the event loop. The
     assumption here is that it is safe to process ^C immediately if
     immediate_quit is set. If we didn't, SIGINT would be really
     processed only the next time through the event loop.  To get to
     that point, though, the command that we want to interrupt needs to
     finish first, which is unacceptable.  If immediate quit is not set,
     we process SIGINT the next time through the loop, which is fine. */
  gdb_call_async_signal_handler (sigint_token, immediate_quit);
}

/* Quit GDB if SIGTERM is received.
   GDB would quit anyway, but this way it will clean up properly.  */
void
handle_sigterm (int sig)
{
  signal (sig, handle_sigterm);
  quit_force ((char *) 0, stdin == instream);
}

/* Do the quit. All the checks have been done by the caller. */
void
async_request_quit (gdb_client_data arg)
{
  /* If the quit_flag has gotten reset back to 0 by the time we get
     back here, that means that an exception was thrown to unwind the
     current command before we got back to the event loop.  So there
     is no reason to call quit again here, unless immediate_quit is
     set.*/

  if (quit_flag || immediate_quit)
    quit ();
}

#ifdef SIGQUIT
/* Tell the event loop what to do if SIGQUIT is received. 
   See event-signal.c. */
static void
handle_sigquit (int sig)
{
  mark_async_signal_handler_wrapper (sigquit_token);
  signal (sig, handle_sigquit);
}
#endif

#if defined (SIGQUIT) || defined (SIGHUP)
/* Called by the event loop in response to a SIGQUIT or an
   ignored SIGHUP.  */
static void
async_do_nothing (gdb_client_data arg)
{
  /* Empty function body. */
}
#endif

#ifdef SIGHUP
/* Tell the event loop what to do if SIGHUP is received. 
   See event-signal.c. */
static void
handle_sighup (int sig)
{
  mark_async_signal_handler_wrapper (sighup_token);
  signal (sig, handle_sighup);
}

/* Called by the event loop to process a SIGHUP */
static void
async_disconnect (gdb_client_data arg)
{
  catch_errors (quit_cover, NULL,
		"Could not kill the program being debugged",
		RETURN_MASK_ALL);
  signal (SIGHUP, SIG_DFL);	/*FIXME: ??????????? */
  raise (SIGHUP);
}
#endif

#ifdef STOP_SIGNAL
void
handle_stop_sig (int sig)
{
  mark_async_signal_handler_wrapper (sigtstp_token);
  signal (sig, handle_stop_sig);
}

static void
async_stop_sig (gdb_client_data arg)
{
  char *prompt = get_prompt ();

#if STOP_SIGNAL == SIGTSTP
  signal (SIGTSTP, SIG_DFL);
#if HAVE_SIGPROCMASK
  {
    sigset_t zero;

    sigemptyset (&zero);
    sigprocmask (SIG_SETMASK, &zero, 0);
  }
#elif HAVE_SIGSETMASK
  sigsetmask (0);
#endif
  raise (SIGTSTP);
  signal (SIGTSTP, handle_stop_sig);
#else
  signal (STOP_SIGNAL, handle_stop_sig);
#endif
  printf_unfiltered ("%s", prompt);
  gdb_flush (gdb_stdout);

  /* Forget about any previous command -- null line now will do nothing.  */
  dont_repeat ();
}
#endif /* STOP_SIGNAL */

/* Tell the event loop what to do if SIGFPE is received. 
   See event-signal.c. */
static void
handle_sigfpe (int sig)
{
  mark_async_signal_handler_wrapper (sigfpe_token);
  signal (sig, handle_sigfpe);
}

/* Event loop will call this functin to process a SIGFPE. */
static void
async_float_handler (gdb_client_data arg)
{
  /* This message is based on ANSI C, section 4.7. Note that integer
     divide by zero causes this, so "float" is a misnomer. */
  error (_("Erroneous arithmetic operation."));
}

/* Tell the event loop what to do if SIGWINCH is received. 
   See event-signal.c. */
#if defined(SIGWINCH) && defined(SIGWINCH_HANDLER)
static void
handle_sigwinch (int sig)
{
  mark_async_signal_handler_wrapper (sigwinch_token);
  signal (sig, handle_sigwinch);
}
#endif


/* Called by do_setshow_command.  */
void
set_async_editing_command (char *args, int from_tty, struct cmd_list_element *c)
{
  change_line_handler ();
}

/* Called by do_setshow_command.  */
void
set_async_annotation_level (char *args, int from_tty, struct cmd_list_element *c)
{
  change_annotation_level ();
}

/* Called by do_setshow_command.  */
void
set_async_prompt (char *args, int from_tty, struct cmd_list_element *c)
{
  PROMPT (0) = xstrdup (new_async_prompt);
}

/* Set things up for readline to be invoked via the alternate
   interface, i.e. via a callback function (rl_callback_read_char),
   and hook up instream to the event loop. */
void
gdb_setup_readline (void)
{
  /* This function is a noop for the sync case.  The assumption is
     that the sync setup is ALL done in gdb_init, and we would only
     mess it up here.  The sync stuff should really go away over
     time.  */
  if (!batch_silent)
    gdb_stdout = stdio_fileopen (stdout);
  gdb_stderr = stdio_fileopen (stderr);
  gdb_stdlog = gdb_stderr;  /* for moment */
  gdb_stdtarg = gdb_stderr; /* for moment */

  /* If the input stream is connected to a terminal, turn on
     editing.  */
  if (ISATTY (instream))
    {
      /* Tell gdb that we will be using the readline library. This
	 could be overwritten by a command in .gdbinit like 'set
	 editing on' or 'off'.  */
      async_command_editing_p = 1;
	  
      /* When a character is detected on instream by select or poll,
	 readline will be invoked via this callback function.  */
      call_readline = rl_callback_read_char_wrapper;
    }
  else
    {
      async_command_editing_p = 0;
      call_readline = gdb_readline2;
    }
  
  /* When readline has read an end-of-line character, it passes the
     complete line to gdb for processing. command_line_handler is the
     function that does this.  */
  input_handler = command_line_handler;
      
  /* Tell readline to use the same input stream that gdb uses. */
  rl_instream = instream;

  /* Get a file descriptor for the input stream, so that we can
     register it with the event loop.  */
  input_fd = fileno (instream);

  /* Now we need to create the event sources for the input file
     descriptor.  */
  /* At this point in time, this is the only event source that we
     register with the even loop. Another source is going to be the
     target program (inferior), but that must be registered only when
     it actually exists (I.e. after we say 'run' or after we connect
     to a remote target.  */
  add_file_handler (input_fd, stdin_event_handler, 0);
}

/* Disable command input through the standard CLI channels.  Used in
   the suspend proc for interpreters that use the standard gdb readline
   interface, like the cli & the mi.  */
void
gdb_disable_readline (void)
{
  /* FIXME - It is too heavyweight to delete and remake these every
     time you run an interpreter that needs readline.  It is probably
     better to have the interpreters cache these, which in turn means
     that this needs to be moved into interpreter specific code.  */

#if 0
  ui_file_delete (gdb_stdout);
  ui_file_delete (gdb_stderr);
  gdb_stdlog = NULL;
  gdb_stdtarg = NULL;
#endif

  rl_callback_handler_remove ();
  delete_file_handler (input_fd);
}

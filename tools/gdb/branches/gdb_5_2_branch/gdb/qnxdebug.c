/* Machine independent support for SVR4 /proc (process file system) for GDB.
   Copyright 1991, 1992-97, 1998 Free Software Foundation, Inc.
   Written by Fred Fish at Cygnus Support.  Changes for sysv4.2mp procfs
   compatibility by Geoffrey Noer at Cygnus Solutions.

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
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */


/*			N  O  T  E  S

 */


#include "defs.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <setjmp.h>
#include <time.h>
#include <unistd.h>

#include "top.h"
#include "target.h"
#include "inferior.h"
#include "call-cmds.h"
#include "gdbcmd.h"
#include "gdb_string.h"
#include "gdb_stat.h"

#include "inferior.h"
#include "target.h"

/* If this definition isn't overridden by the header files, assume
   that isatty and fileno exist on this system.  */
#ifndef ISATTY
#define ISATTY(FP)	(isatty (fileno (FP)))
#endif

static FILE	*logstream = NULL;
static int	logging = 0;
static char	*logpath = NULL;
static char	*defpath = "gdb.log";

static void qnx_command_loop_marker(int foo)
{
}

void qnx_command_loop( void )
{
struct cleanup	*old_chain;
char		*command;
int		stdin_is_tty = ISATTY (stdin);

    while (instream && !feof( instream )) {
	quit_flag = 0;
	if (instream == stdin && stdin_is_tty)
	    reinitialize_more_filter();
	old_chain = make_cleanup((make_cleanup_ftype*)qnx_command_loop_marker, 0 );
	command = command_line_input( instream == stdin ? get_prompt() : (char *) NULL,
				    instream == stdin, "prompt");
	if (command == 0)
	    return;

	if ( logging ) {
	    fprintf( logstream, "%s%s\n", get_prompt(), command );
	    fflush( logstream );
	}
	execute_command( command, instream == stdin );
	/* Do any commands attached to breakpoint we stopped at.  */
	bpstat_do_actions( &stop_bpstat );
	do_cleanups( old_chain );
    }
}

static void logging_sfunc( char *args, int from_tty, struct cmd_list_element *c)
{
char *path = logpath? logpath:defpath;

    if ( !logging ) {
    	if ( logstream != NULL ) {
	    printf_unfiltered( "Closing logfile.\n" );
	    fclose( logstream );
	    logstream = NULL;
    	}
    }
    else {
    	if ( logstream == NULL ) {
	    logstream = fopen( path, "w" );
	    if ( logstream == NULL ) {
	    	logging = 0;
	    	fprintf_unfiltered( gdb_stderr, "Couldn't open '%s': %s\n",
	    		path, strerror(errno) );
	    }
	    else
	    	printf_unfiltered( "Logging to '%s'\n", path );
    	}
    	else {
	    printf_unfiltered( "Closing logfile.\n" );
	    fclose( logstream );
	    logstream = NULL;
	    logging_sfunc( args, from_tty, c );
    	}
    }
}

static void logpath_sfunc( char *args, int from_tty, struct cmd_list_element *c)
{
    if ( logstream != NULL )
    	logging_sfunc( args, from_tty, c );
}

void qnx_fputs_hook( const char *linebuffer, FILE *stream )
{
    fputs( linebuffer, stream );
    if ( logging && logstream != NULL ) {
    	fputs( linebuffer, logstream );
    	fflush( logstream );
    }
}

void
_initialize_qnxdebug ()
{
struct cmd_list_element *cmd;

    command_loop_hook = qnx_command_loop;

	// GP - Removed Feb 2 2000 - for some reason, this 
	// breaks the printf_(un)filtered stuff - so no text 
	// output was being seen, other than the prompt.

    // fputs_unfiltered_hook = qnx_fputs_hook;

    cmd = add_set_cmd("logname", no_class,
		var_filename, (char *)&logpath,
		"Set log filename.\n", &setlist);
    cmd->function.sfunc = logpath_sfunc;
    add_show_from_set( cmd, &showlist);
    cmd = add_set_cmd("logging", no_class,
		var_boolean, (char *)&logging,
		"Set logging on/off.\n", &setlist);
    cmd->function.sfunc = logging_sfunc;
    add_show_from_set( cmd, &showlist);

#if defined(SIGSELECT)
	/* by default we don't want to stop on these two, but we do want to pass */
	signal_stop_update(SIGSELECT, 0);
	signal_print_update(SIGSELECT, 0);
	signal_pass_update(SIGSELECT, 1);
#endif

#if defined(SIGPHOTON)
	signal_stop_update(SIGPHOTON, 0);
	signal_print_update(SIGPHOTON, 0);
	signal_pass_update(SIGPHOTON, 1);
#endif
}


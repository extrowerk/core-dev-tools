



#include "defs.h"
#include "arch-utils.h"
#include "mi-cmds.h"
#include "ui-out.h"
#include "mi-out.h"
#include "breakpoint.h"
#include "gdb_string.h"
#include "mi-getopt.h"
#include "gdb.h"
#include "exceptions.h"
#include "observer.h"
#include "mi-main.h"
#include "interps.h"


#define MAX_CMD_LEN (32 * 1024)

extern struct ui_out *cli_uiout;
extern char *current_token;

enum
  {
    FROM_TTY = 0
  };

extern int running_result_record_printed;

void
mi_cmd_bb_exec (char *command, char **argv, int argc)
{
  struct ui_out *mi_uiout = interp_ui_out (top_level_interpreter ());
  struct ui_out *saved_uiout = current_uiout;
  struct ui_file *real_uifile;
  char *console_argv[2];
  char cmd[MAX_CMD_LEN] = { '\0' };
  char *p;
  int i;
  struct interp *interp_console;
  char *mi_error_message = NULL;

  p = cmd;

  current_uiout = cli_uiout;

  real_uifile = gdb_stdout;
  gdb_stdout = raw_stdout;

  for (i = 0; i != argc && p < cmd + sizeof (cmd); ++i)
    {
      p += snprintf (p, cmd + sizeof (cmd) - p, "%s ", argv[i]);

      if (i == 0)
	{
	  if (current_token && current_token[0])
	    {
	      p += snprintf (p, cmd + sizeof (cmd) - p, "%s ", current_token);
	    }
	  else
	    {
	      p += snprintf (p, cmd + sizeof (cmd) - p, "%s ", "-1");
	    }
	}
    }

  /* A CLI command was read from the input stream.  */
  /* This "feature" will be removed as soon as we have a
     complete set of mi commands.  */
  /* Echo the command on the console.  */
  fprintf_unfiltered (gdb_stdlog, "%s\n", command);
  /* Call the "console" interpreter.  */
  interp_console = interp_lookup("console");
  if (interp_console == NULL)
    error (_("no console interpreter"));
  else
    {
      struct gdb_exception e = interp_exec (interp_console, cmd);
      if (e.reason < 0)
	{
	  mi_error_message = xstrdup (e.message);
	}
    }
  running_result_record_printed = 1;

  gdb_stdout = real_uifile;

  current_uiout = saved_uiout;

  if (mi_error_message != NULL)
    error ("%s", mi_error_message);
}



/* Core dump and executable file functions above target vector, for GDB.

   Copyright (C) 1986, 1987, 1989, 1991, 1992, 1993, 1994, 1996, 1997, 1998,
   1999, 2000, 2001, 2003, 2006, 2007, 2008, 2009
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
#include "gdb_string.h"
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include "inferior.h"
#include "symtab.h"
#include "command.h"
#include "gdbcmd.h"
#include "bfd.h"
#include "target.h"
#include "gdbcore.h"
#include "dis-asm.h"
#include "gdb_stat.h"
#include "completer.h"
#include "exceptions.h"

/* Local function declarations.  */

extern void _initialize_core (void);
static void call_extra_exec_file_hooks (char *filename);

/* You can have any number of hooks for `exec_file_command' command to
   call.  If there's only one hook, it is set in exec_file_display
   hook.  If there are two or more hooks, they are set in
   exec_file_extra_hooks[], and deprecated_exec_file_display_hook is
   set to a function that calls all of them.  This extra complexity is
   needed to preserve compatibility with old code that assumed that
   only one hook could be set, and which called
   deprecated_exec_file_display_hook directly.  */

typedef void (*hook_type) (char *);

hook_type deprecated_exec_file_display_hook;	/* the original hook */
static hook_type *exec_file_extra_hooks;	/* array of additional hooks */
static int exec_file_hook_count = 0;	/* size of array */

/* Binary file diddling handle for the core file.  */

bfd *core_bfd = NULL;


/* Backward compatability with old way of specifying core files.  */

void
core_file_command (char *filename, int from_tty)
{
  struct target_ops *t;

  dont_repeat ();		/* Either way, seems bogus. */

  t = find_core_target ();
  if (t == NULL)
    error (_("GDB can't read core files on this machine."));

  if (!filename)
    (t->to_detach) (t, filename, from_tty);
  else
    (t->to_open) (filename, from_tty);
}


/* If there are two or more functions that wish to hook into
   exec_file_command, this function will call all of the hook
   functions.  */

static void
call_extra_exec_file_hooks (char *filename)
{
  int i;

  for (i = 0; i < exec_file_hook_count; i++)
    (*exec_file_extra_hooks[i]) (filename);
}

/* Call this to specify the hook for exec_file_command to call back.
   This is called from the x-window display code.  */

void
specify_exec_file_hook (void (*hook) (char *))
{
  hook_type *new_array;

  if (deprecated_exec_file_display_hook != NULL)
    {
      /* There's already a hook installed.  Arrange to have both it
       * and the subsequent hooks called. */
      if (exec_file_hook_count == 0)
	{
	  /* If this is the first extra hook, initialize the hook array.  */
	  exec_file_extra_hooks = (hook_type *) xmalloc (sizeof (hook_type));
	  exec_file_extra_hooks[0] = deprecated_exec_file_display_hook;
	  deprecated_exec_file_display_hook = call_extra_exec_file_hooks;
	  exec_file_hook_count = 1;
	}

      /* Grow the hook array by one and add the new hook to the end.
         Yes, it's inefficient to grow it by one each time but since
         this is hardly ever called it's not a big deal.  */
      exec_file_hook_count++;
      new_array =
	(hook_type *) xrealloc (exec_file_extra_hooks,
				exec_file_hook_count * sizeof (hook_type));
      exec_file_extra_hooks = new_array;
      exec_file_extra_hooks[exec_file_hook_count - 1] = hook;
    }
  else
    deprecated_exec_file_display_hook = hook;
}

/* The exec file must be closed before running an inferior.
   If it is needed again after the inferior dies, it must
   be reopened.  */

void
close_exec_file (void)
{
#if 0				/* FIXME */
  if (exec_bfd)
    bfd_tempclose (exec_bfd);
#endif
}

void
reopen_exec_file (void)
{
#if 0				/* FIXME */
  if (exec_bfd)
    bfd_reopen (exec_bfd);
#else
  char *filename;
  int res;
  struct stat st;
  struct cleanup *cleanups;

  /* Don't do anything if there isn't an exec file. */
  if (exec_bfd == NULL)
    return;

  /* If the timestamp of the exec file has changed, reopen it. */
  filename = xstrdup (bfd_get_filename (exec_bfd));
  cleanups = make_cleanup (xfree, filename);
  res = stat (filename, &st);

  if (exec_bfd_mtime && exec_bfd_mtime != st.st_mtime)
    exec_file_attach (filename, 0);
  else
    /* If we accessed the file since last opening it, close it now;
       this stops GDB from holding the executable open after it
       exits.  */
    bfd_cache_close_all ();

  do_cleanups (cleanups);
#endif
}

/* If we have both a core file and an exec file,
   print a warning if they don't go together.  */

void
validate_files (void)
{
  if (exec_bfd && core_bfd)
    {
      if (!core_file_matches_executable_p (core_bfd, exec_bfd))
	warning (_("core file may not match specified executable file."));
      else if (bfd_get_mtime (exec_bfd) > bfd_get_mtime (core_bfd))
	warning (_("exec file is newer than core file."));
    }
}

/* Return the name of the executable file as a string.
   ERR nonzero means get error if there is none specified;
   otherwise return 0 in that case.  */

char *
get_exec_file (int err)
{
  if (exec_bfd)
    return bfd_get_filename (exec_bfd);
  if (!err)
    return NULL;

  error (_("No executable file specified.\n\
Use the \"file\" or \"exec-file\" command."));
  return NULL;
}


/* Report a memory error by throwing a MEMORY_ERROR error.  */

void
memory_error (int status, CORE_ADDR memaddr)
{
  if (status == EIO)
    /* Actually, address between memaddr and memaddr + len was out of
       bounds.  */
    throw_error (MEMORY_ERROR,
		 _("Cannot access memory at address %s"),
		 paddress (target_gdbarch, memaddr));
  else
    throw_error (MEMORY_ERROR,
		 _("Error accessing memory address %s: %s."),
		 paddress (target_gdbarch, memaddr),
		 safe_strerror (status));
}

/* Same as target_read_memory, but report an error if can't read.  */
void
read_memory (CORE_ADDR memaddr, gdb_byte *myaddr, int len)
{
  int status;
  status = target_read_memory (memaddr, myaddr, len);
  if (status != 0)
    memory_error (status, memaddr);
}

/* Argument / return result struct for use with
   do_captured_read_memory_integer().  MEMADDR and LEN are filled in
   by gdb_read_memory_integer().  RESULT is the contents that were
   successfully read from MEMADDR of length LEN.  */

struct captured_read_memory_integer_arguments
{
  CORE_ADDR memaddr;
  int len;
  enum bfd_endian byte_order;
  LONGEST result;
};

/* Helper function for gdb_read_memory_integer().  DATA must be a
   pointer to a captured_read_memory_integer_arguments struct. 
   Return 1 if successful.  Note that the catch_errors() interface
   will return 0 if an error occurred while reading memory.  This
   choice of return code is so that we can distinguish between
   success and failure.  */

static int
do_captured_read_memory_integer (void *data)
{
  struct captured_read_memory_integer_arguments *args = (struct captured_read_memory_integer_arguments*) data;
  CORE_ADDR memaddr = args->memaddr;
  int len = args->len;
  enum bfd_endian byte_order = args->byte_order;

  args->result = read_memory_integer (memaddr, len, byte_order);

  return 1;
}

/* Read memory at MEMADDR of length LEN and put the contents in
   RETURN_VALUE.  Return 0 if MEMADDR couldn't be read and non-zero
   if successful.  */

int
safe_read_memory_integer (CORE_ADDR memaddr, int len, enum bfd_endian byte_order,
			  LONGEST *return_value)
{
  int status;
  struct captured_read_memory_integer_arguments args;
  args.memaddr = memaddr;
  args.len = len;
  args.byte_order = byte_order;

  status = catch_errors (do_captured_read_memory_integer, &args,
                        "", RETURN_MASK_ALL);
  if (status)
    *return_value = args.result;

  return status;
}

LONGEST
read_memory_integer (CORE_ADDR memaddr, int len, enum bfd_endian byte_order)
{
  gdb_byte buf[sizeof (LONGEST)];

  read_memory (memaddr, buf, len);
  return extract_signed_integer (buf, len, byte_order);
}

ULONGEST
read_memory_unsigned_integer (CORE_ADDR memaddr, int len, enum bfd_endian byte_order)
{
  gdb_byte buf[sizeof (ULONGEST)];

  read_memory (memaddr, buf, len);
  return extract_unsigned_integer (buf, len, byte_order);
}

void
read_memory_string (CORE_ADDR memaddr, char *buffer, int max_len)
{
  char *cp;
  int i;
  int cnt;

  cp = buffer;
  while (1)
    {
      if (cp - buffer >= max_len)
	{
	  buffer[max_len - 1] = '\0';
	  break;
	}
      cnt = max_len - (cp - buffer);
      if (cnt > 8)
	cnt = 8;
      read_memory (memaddr + (int) (cp - buffer), (gdb_byte *)cp, cnt);
      for (i = 0; i < cnt && *cp; i++, cp++)
	;			/* null body */

      if (i < cnt && !*cp)
	break;
    }
}

CORE_ADDR
read_memory_typed_address (CORE_ADDR addr, struct type *type)
{
  gdb_byte *buf = alloca (TYPE_LENGTH (type));
  read_memory (addr, buf, TYPE_LENGTH (type));
  return extract_typed_address (buf, type);
}

/* Same as target_write_memory, but report an error if can't write.  */
void
write_memory (CORE_ADDR memaddr, const bfd_byte *myaddr, int len)
{
  int status;
  status = target_write_memory (memaddr, myaddr, len);
  if (status != 0)
    memory_error (status, memaddr);
}

/* Store VALUE at ADDR in the inferior as a LEN-byte unsigned integer.  */
void
write_memory_unsigned_integer (CORE_ADDR addr, int len, enum bfd_endian byte_order,
			       ULONGEST value)
{
  gdb_byte *buf = alloca (len);
  store_unsigned_integer (buf, len, byte_order, value);
  write_memory (addr, buf, len);
}

/* Store VALUE at ADDR in the inferior as a LEN-byte signed integer.  */
void
write_memory_signed_integer (CORE_ADDR addr, int len, enum bfd_endian byte_order,
			     LONGEST value)
{
  gdb_byte *buf = alloca (len);
  store_signed_integer (buf, len, byte_order, value);
  write_memory (addr, buf, len);
}

/* The current default bfd target.  Points to storage allocated for
   gnutarget_string.  */
char *gnutarget;

/* Same thing, except it is "auto" not NULL for the default case.  */
static char *gnutarget_string;
static void
show_gnutarget_string (struct ui_file *file, int from_tty,
		       struct cmd_list_element *c, const char *value)
{
  fprintf_filtered (file, _("The current BFD target is \"%s\".\n"), value);
}

static void set_gnutarget_command (char *, int, struct cmd_list_element *);

static void
set_gnutarget_command (char *ignore, int from_tty, struct cmd_list_element *c)
{
  if (strcmp (gnutarget_string, "auto") == 0)
    gnutarget = NULL;
  else
    gnutarget = gnutarget_string;
}

/* Set the gnutarget.  */
void
set_gnutarget (char *newtarget)
{
  if (gnutarget_string != NULL)
    xfree (gnutarget_string);
  gnutarget_string = xstrdup (newtarget);
  set_gnutarget_command (NULL, 0, NULL);
}

void
_initialize_core (void)
{
  struct cmd_list_element *c;
  c = add_cmd ("core-file", class_files, core_file_command, _("\
Use FILE as core dump for examining memory and registers.\n\
No arg means have no core file.  This command has been superseded by the\n\
`target core' and `detach' commands."), &cmdlist);
  set_cmd_completer (c, filename_completer);

  
  add_setshow_string_noescape_cmd ("gnutarget", class_files,
				   &gnutarget_string, _("\
Set the current BFD target."), _("\
Show the current BFD target."), _("\
Use `set gnutarget auto' to specify automatic detection."),
				   set_gnutarget_command,
				   show_gnutarget_string,
				   &setlist, &showlist);

  if (getenv ("GNUTARGET"))
    set_gnutarget (getenv ("GNUTARGET"));
  else
    set_gnutarget ("auto");
}

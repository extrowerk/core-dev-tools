/* nto-tdep.c - general QNX Neutrino target functionality.

   Copyright (C) 2003, 2004, 2007 Free Software Foundation, Inc.

   Contributed by QNX Software Systems Ltd.

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

#include "gdb_assert.h"
#include "gdb_stat.h"
#include "gdb_string.h"
#include "nto-tdep.h"
#include "top.h"
#include "cli/cli-decode.h"
#include "cli/cli-cmds.h"
#include "inferior.h"
#include "gdbarch.h"
#include "bfd.h"
#include "elf-bfd.h"
#include "solib-svr4.h"
#include "gdbcore.h"
#include "objfiles.h"

#include "gdbcmd.h"

#ifdef __CYGWIN__
#include <sys/cygwin.h>
#endif

static char default_nto_target[] = "";

struct nto_target_ops current_nto_target;

static char *
nto_target (void)
{
  char *p = getenv ("QNX_TARGET");

#ifdef __CYGWIN__
  static char buf[PATH_MAX];
  if (p)
    cygwin_conv_to_posix_path (p, buf);
  else
    cygwin_conv_to_posix_path (default_nto_target, buf);
  return buf;
#else
  return p ? p : default_nto_target;
#endif
}

void
nto_set_target (struct nto_target_ops *targ)
{
  nto_trace (0) ("%s ()\n", __func__);
  nto_regset_id = targ->regset_id;
  nto_supply_gregset = targ->supply_gregset;
  nto_supply_fpregset = targ->supply_fpregset;
  nto_supply_altregset = targ->supply_altregset;
  nto_supply_regset = targ->supply_regset;
  nto_register_area = targ->register_area;
  nto_regset_fill = targ->regset_fill;
  if (targ->fetch_link_map_offsets)
    nto_fetch_link_map_offsets = targ->fetch_link_map_offsets;
  else
    nto_fetch_link_map_offsets = nto_generic_svr4_fetch_link_map_offsets; 
}

/* Take a string such as i386, rs6000, etc. and map it onto CPUTYPE_X86,
   CPUTYPE_PPC, etc. as defined in nto-share/dsmsgs.h.  */
int
nto_map_arch_to_cputype (const char *arch)
{
  if (!strcmp (arch, "i386") || !strcmp (arch, "x86"))
    return CPUTYPE_X86;
  if (!strcmp (arch, "rs6000") || !strcmp (arch, "powerpc"))
    return CPUTYPE_PPC;
  if (!strcmp (arch, "mips"))
    return CPUTYPE_MIPS;
  if (!strcmp (arch, "arm"))
    return CPUTYPE_ARM;
  if (!strcmp (arch, "sh"))
    return CPUTYPE_SH;
  return CPUTYPE_UNKNOWN;
}

int
nto_find_and_open_solib (char *solib, unsigned o_flags, char **temp_pathname)
{
  char *buf, *arch_path, *nto_root, *endian, *base;
  const char *arch;
  int ret;
#define PATH_FMT "%s/lib:%s/usr/lib:%s/usr/photon/lib:" \
		 "%s/usr/photon/dll:%s/lib/dll"

  nto_root = nto_target ();
  nto_trace (0) ("%s (..) nto_root: %s\n", __func__, nto_root);
  if (strcmp (gdbarch_bfd_arch_info (current_gdbarch)->arch_name, "i386") == 0)
    {
      arch = "x86";
      endian = "";
    }
  else if (strcmp (gdbarch_bfd_arch_info (current_gdbarch)->arch_name,
		   "rs6000") == 0
	   || strcmp (gdbarch_bfd_arch_info (current_gdbarch)->arch_name,
		   "powerpc") == 0)
    {
      arch = "ppc";
      endian = "be";
    }
  else
    {
      arch = gdbarch_bfd_arch_info (current_gdbarch)->arch_name;
      endian = gdbarch_byte_order (current_gdbarch)
	       == BFD_ENDIAN_BIG ? "be" : "le";
    }

  /* In case nto_root is short, add strlen(solib)
     so we can reuse arch_path below.  */
  arch_path =
    alloca (strlen (nto_root) + strlen (arch) + strlen (endian) + 2 +
	    strlen (solib));
  sprintf (arch_path, "%s/%s%s", nto_root, arch, endian);

  buf = alloca (strlen (PATH_FMT) + strlen (arch_path) * 5 + 1);
  sprintf (buf, PATH_FMT, arch_path, arch_path, arch_path, arch_path,
	   arch_path);

  /* Don't assume basename() isn't destructive.  */
  base = strrchr (solib, '/');
  if (!base)
    base = solib;
  else
    base++;			/* Skip over '/'.  */

  ret = openp (buf, 1, base, o_flags, 0, temp_pathname);
  if (ret < 0 && base != solib)
    {
      sprintf (arch_path, "/%s", solib);
      ret = open (arch_path, o_flags, 0);
      if (temp_pathname)
	{
	  if (ret >= 0)
	    *temp_pathname = gdb_realpath (arch_path);
	  else
	    {
	      if (*temp_pathname)
	        **temp_pathname = '\0';
	      else
	        *temp_pathname = "";
	    }
	}
    }
  return ret;
}

void
nto_init_solib_absolute_prefix (void)
{
  char buf[PATH_MAX * 2], arch_path[PATH_MAX];
  char *nto_root, *endian;
  const char *arch;

  nto_root = nto_target ();
  if (strcmp (gdbarch_bfd_arch_info (current_gdbarch)->arch_name, "i386") == 0)
    {
      arch = "x86";
      endian = "";
    }
  else if (strcmp (gdbarch_bfd_arch_info (current_gdbarch)->arch_name,
		   "rs6000") == 0
	   || strcmp (gdbarch_bfd_arch_info (current_gdbarch)->arch_name,
		   "powerpc") == 0)
    {
      arch = "ppc";
      endian = "be";
    }
  else
    {
      arch = gdbarch_bfd_arch_info (current_gdbarch)->arch_name;
      endian = gdbarch_byte_order (current_gdbarch)
	       == BFD_ENDIAN_BIG ? "be" : "le";
    }

  sprintf (arch_path, "%s/%s%s", nto_root, arch, endian);

  sprintf (buf, "set solib-absolute-prefix %s", arch_path);
  execute_command (buf, 0);

#if defined (__MINGW32__)
#define PATH_SEP ";"
#else
#define PATH_SEP ":"
#endif

  sprintf (buf, "set solib-search-path %s/%s" PATH_SEP "%s/%s", arch_path, "lib", arch_path, "usr/lib");
  execute_command (buf, 0);
}

char **
nto_parse_redirection (char *pargv[], const char **pin, const char **pout, const char **perr)
{
  char **argv;
  char *in, *out, *err, *p;
  int argc, i, n;

  for (n = 0; pargv[n]; n++);
  if (n == 0)
    return NULL;
  in = "";
  out = "";
  err = "";

  argv = xcalloc (n + 1, sizeof argv[0]);
  argc = n;
  for (i = 0, n = 0; n < argc; n++)
    {
      p = pargv[n];
      if (*p == '>')
	{
	  p++;
	  if (*p)
	    out = p;
	  else
	    out = pargv[++n];
	}
      else if (*p == '<')
	{
	  p++;
	  if (*p)
	    in = p;
	  else
	    in = pargv[++n];
	}
      else if (*p++ == '2' && *p++ == '>')
	{
	  if (*p == '&' && *(p + 1) == '1')
	    err = out;
	  else if (*p)
	    err = p;
	  else
	    err = pargv[++n];
	}
      else
	argv[i++] = pargv[n];
    }
  *pin = in;
  *pout = out;
  *perr = err;
  return argv;
}

struct link_map_offsets *
nto_generic_svr4_fetch_link_map_offsets (void)
{
  static struct link_map_offsets lmo;
  static struct link_map_offsets *lmp = NULL;

  if (lmp == NULL)
    {
      lmp = &lmo;

      lmo.r_map_offset = 4;

      lmo.link_map_size = 20;	/* The actual size is 552 bytes, but
				   this is all we need.  */
      lmo.l_addr_offset = 0;

      lmo.l_name_offset = 4;

      lmo.l_next_offset = 12;

      lmo.l_prev_offset = 16;
    }

  return lmp;
}

/* The struct lm_info, LM_ADDR, and nto_truncate_ptr are copied from
   solib-svr4.c to support nto_relocate_section_addresses
   which is different from the svr4 version.  */

struct lm_info
{
  /* Pointer to copy of link map from inferior.  The type is char *
     rather than void *, so that we may use byte offsets to find the
     various fields without the need for a cast.  */
  char *lm;
};

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((unsigned long) &((TYPE *)0)->MEMBER)
#endif
#define fieldsize(TYPE, MEMBER) (sizeof (((TYPE *)0)->MEMBER))

static CORE_ADDR
LM_ADDR_FROM_LINK_MAP (struct so_list *so)
{
  struct link_map_offsets *lmo = nto_fetch_link_map_offsets ();

  gdb_byte *buf = so->lm_info->lm + lmo->l_addr_offset;
  if (NULL == buf)
    {
      return 0;
    }
  return extract_typed_address (so->lm_info->lm + lmo->l_addr_offset,
               builtin_type_void_data_ptr);
}

static CORE_ADDR
nto_truncate_ptr (CORE_ADDR addr)
{
  if (gdbarch_ptr_bit (current_gdbarch) == sizeof (CORE_ADDR) * 8)
    /* We don't need to truncate anything, and the bit twiddling below
       will fail due to overflow problems.  */
    return addr;
  else
    return addr & (((CORE_ADDR) 1 << gdbarch_ptr_bit (current_gdbarch)) - 1);
}

Elf_Internal_Phdr *
find_load_phdr (bfd *abfd)
{
  Elf_Internal_Phdr *phdr;
  unsigned int i;

  if (!elf_tdata (abfd))
    return NULL;

  phdr = elf_tdata (abfd)->phdr;
  for (i = 0; i < elf_elfheader (abfd)->e_phnum; i++, phdr++)
    {
      if (phdr->p_type == PT_LOAD && (phdr->p_flags & PF_X))
	return phdr;
    }
  return NULL;
}

void
nto_relocate_section_addresses (struct so_list *so, struct section_table *sec)
{
  /* Neutrino treats the l_addr base address field in link.h as different than
     the base address in the System V ABI and so the offset needs to be
     calculated and applied to relocations.  */
  Elf_Internal_Phdr *phdr = find_load_phdr (sec->bfd);
  unsigned vaddr = phdr ? phdr->p_vaddr : 0;

  sec->addr = nto_truncate_ptr (sec->addr + LM_ADDR_FROM_LINK_MAP (so) - vaddr);
  sec->endaddr = nto_truncate_ptr (sec->endaddr + LM_ADDR_FROM_LINK_MAP (so) - vaddr);
}

/* This is cheating a bit because our linker code is in libc.so.  If we
   ever implement lazy linking, this may need to be re-examined.  */
int
nto_in_dynsym_resolve_code (CORE_ADDR pc)
{
  if (in_plt_section (pc, NULL)) 
    {
      nto_trace (0) ("in plt section\n");
      return 1;
    }
  nto_trace (0) ("Not in plt section\n");
  return 0;
}

void
nto_generic_supply_gpregset (const struct regset *regset,
			     struct regcache *regcache, int regnum,
			     const void *gregs, size_t len)
{
}

void
nto_generic_supply_fpregset (const struct regset *regset,
			     struct regcache *regcache, int regnum,
			     const void *fpregs, size_t len)
{
}

void
nto_generic_supply_altregset (const struct regset *regset,
			      struct regcache *regcache, int regnum,
			      const void *altregs, size_t len)
{
}

void
nto_dummy_supply_regset (struct regcache *regcache, char *regs)
{
  /* Do nothing.  */
}

enum gdb_osabi
nto_elf_osabi_sniffer (bfd *abfd)
{
  if (nto_is_nto_target)
    return nto_is_nto_target (abfd);
  return GDB_OSABI_UNKNOWN;
}

char *
nto_target_extra_thread_info (struct thread_info *ti)
{
  if (ti && ti->private && ti->private->name[0])
    return ti->private->name;
  return "";
}

void
nto_initialize_signals (void)
{
  /* We use SIG45 for pulses, or something, so nostop, noprint
     and pass them.  */
  signal_stop_update (target_signal_from_name ("SIG45"), 0);
  signal_print_update (target_signal_from_name ("SIG45"), 0);
  signal_pass_update (target_signal_from_name ("SIG45"), 1);

  /* By default we don't want to stop on these two, but we do want to pass.  */
#if defined(SIGSELECT)
  signal_stop_update (SIGSELECT, 0);
  signal_print_update (SIGSELECT, 0);
  signal_pass_update (SIGSELECT, 1);
#endif

#if defined(SIGPHOTON)
  signal_stop_update (SIGPHOTON, 0);
  signal_print_update (SIGPHOTON, 0);
  signal_pass_update (SIGPHOTON, 1);
#endif
}

static void
show_nto_debug (struct ui_file *file, int from_tty,
                struct cmd_list_element *c, const char *value)
{
  fprintf_filtered (file, _("QNX NTO debug level is %d.\n"), nto_internal_debugging);
}

static int 
nto_print_tidinfo_callback (struct thread_info *tp, void *data)
{
  printf_filtered("%c%d\t%d\t%d\n", ptid_equal (tp->ptid, inferior_ptid) ? '*' : ' ', tp->private->tid, tp->private->state, tp->private->flags );
  return 0;
}

static void 
nto_info_tidinfo_command (char *args, int from_tty)
{
  nto_trace (0) ("%s (args=%s, from_tty=%d)\n", __func__, args, from_tty);

  target_find_new_threads ();
  printf_filtered("Threads for pid %d (%s)\nTid:\tState:\tFlags:\n", ptid_get_pid (inferior_ptid), get_exec_file (0));
  
  iterate_over_threads (nto_print_tidinfo_callback, NULL);
}

/* Nto signal to gdb's enum target_signal translation. */

/* On hosts other than neutrino, signals may differ. */
#ifndef __QNXNTO__

#define NTO_SIGHUP      1   /* hangup */
#define NTO_SIGINT      2   /* interrupt */
#define NTO_SIGQUIT     3   /* quit */
#define NTO_SIGILL      4   /* illegal instruction (not reset when caught) */
#define NTO_SIGTRAP     5   /* trace trap (not reset when caught) */
#define NTO_SIGIOT      6   /* IOT instruction */
#define NTO_SIGABRT     6   /* used by abort */
#define NTO_SIGEMT      7   /* EMT instruction */
#define NTO_SIGDEADLK   7   /* Mutex deadlock */
#define NTO_SIGFPE      8   /* floating point exception */
#define NTO_SIGKILL     9   /* kill (cannot be caught or ignored) */
#define NTO_SIGBUS      10  /* bus error */
#define NTO_SIGSEGV     11  /* segmentation violation */
#define NTO_SIGSYS      12  /* bad argument to system call */
#define NTO_SIGPIPE     13  /* write on pipe with no reader */
#define NTO_SIGALRM     14  /* real-time alarm clock */
#define NTO_SIGTERM     15  /* software termination signal from kill */
#define NTO_SIGUSR1     16  /* user defined signal 1 */
#define NTO_SIGUSR2     17  /* user defined signal 2 */
#define NTO_SIGCHLD     18  /* death of child */
#define NTO_SIGPWR      19  /* power-fail restart */
#define NTO_SIGWINCH    20  /* window change */
#define NTO_SIGURG      21  /* urgent condition on I/O channel */
#define NTO_SIGPOLL     22  /* System V name for NTO_SIGIO */
#define NTO_SIGIO       NTO_SIGPOLL
#define NTO_SIGSTOP     23  /* sendable stop signal not from tty */
#define NTO_SIGTSTP     24  /* stop signal from tty */
#define NTO_SIGCONT     25  /* continue a stopped process */
#define NTO_SIGTTIN     26  /* attempted background tty read */
#define NTO_SIGTTOU     27  /* attempted background tty write */
#define NTO_SIGVTALRM   28  /* virtual timer expired */
#define NTO_SIGPROF     29  /* profileing timer expired */
#define NTO_SIGXCPU     30  /* exceded cpu limit */
#define NTO_SIGXFSZ     31  /* exceded file size limit */

static struct
  {
    int nto_sig;
    enum target_signal gdb_sig;

  }
sig_map[] =
{
  {NTO_SIGHUP, TARGET_SIGNAL_HUP},
  {NTO_SIGINT, TARGET_SIGNAL_INT},
  {NTO_SIGQUIT, TARGET_SIGNAL_QUIT},
  {NTO_SIGILL, TARGET_SIGNAL_ILL},
  {NTO_SIGTRAP, TARGET_SIGNAL_TRAP},
  {NTO_SIGABRT, TARGET_SIGNAL_ABRT},
  {NTO_SIGEMT, TARGET_SIGNAL_EMT},
  {NTO_SIGFPE, TARGET_SIGNAL_FPE},
  {NTO_SIGKILL, TARGET_SIGNAL_KILL},
  {NTO_SIGBUS, TARGET_SIGNAL_BUS},
  {NTO_SIGSEGV, TARGET_SIGNAL_SEGV},
  {NTO_SIGSYS, TARGET_SIGNAL_SYS},
  {NTO_SIGPIPE, TARGET_SIGNAL_PIPE},
  {NTO_SIGALRM, TARGET_SIGNAL_ALRM},
  {NTO_SIGTERM, TARGET_SIGNAL_TERM},
  {NTO_SIGUSR1, TARGET_SIGNAL_USR1},
  {NTO_SIGUSR2, TARGET_SIGNAL_USR2},
  {NTO_SIGCHLD, TARGET_SIGNAL_CHLD},
  {NTO_SIGPWR, TARGET_SIGNAL_PWR},
  {NTO_SIGWINCH, TARGET_SIGNAL_WINCH},
  {NTO_SIGURG, TARGET_SIGNAL_URG},
  {NTO_SIGPOLL, TARGET_SIGNAL_POLL},
  {NTO_SIGSTOP, TARGET_SIGNAL_STOP},
  {NTO_SIGTSTP, TARGET_SIGNAL_TSTP},
  {NTO_SIGCONT, TARGET_SIGNAL_CONT},
  {NTO_SIGTTIN, TARGET_SIGNAL_TTIN},
  {NTO_SIGTTOU, TARGET_SIGNAL_TTOU},
  {NTO_SIGVTALRM, TARGET_SIGNAL_VTALRM},
  {NTO_SIGPROF, TARGET_SIGNAL_PROF},
  {NTO_SIGXCPU, TARGET_SIGNAL_XCPU},
  {NTO_SIGXFSZ, TARGET_SIGNAL_XFSZ}
};
#endif // ndef __QNXNTO__

/* Convert nto signal to gdb signal.  */
enum target_signal
target_signal_from_nto(int sig)
{
#ifndef __QNXNTO__
  int i;
  if (sig == 0)
    return 0;

  for (i = 0; i != ARRAY_SIZE (sig_map); i++)
    {
      if (sig_map[i].nto_sig == sig)
        return sig_map[i].gdb_sig;
    }
#endif /* __QNXNTO__ */
  return target_signal_from_host(sig);
}

/* Convert gdb signal to nto signal.  */

int
target_signal_to_nto(enum target_signal sig)
{
#ifndef __QNXNTO__
  int i;
  if (sig == 0)
    return 0;
    
  for (i = 0; i != ARRAY_SIZE (sig_map); i++)
    {
      if (sig_map[i].gdb_sig == sig)
        return sig_map[i].nto_sig;
    }
#endif /* __QNXNTO__ */
  return target_signal_to_host(sig);
}

void
_initialize_nto_tdep (void)
{
  nto_trace (0) ("%s ()\n", __func__);
  add_setshow_zinteger_cmd ("nto-debug", class_maintenance,
			    &nto_internal_debugging, _("\
Set QNX NTO debug level."), _("\
Show QNX NTO debug level."), _("\
When non-zero, nto specific debug info is\n\
displayed. Different information is displayed\n\
for different positive values."),
			    NULL,
			    show_nto_debug,
			    &maintenance_set_cmdlist,
			    &maintenance_show_cmdlist);

  add_info ("tidinfo", nto_info_tidinfo_command, "List threads for current process." );
}

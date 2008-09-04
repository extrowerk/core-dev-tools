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

#include "defs.h"
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
#include "filenames.h"

#include "gdbcmd.h"
#include "safe-ctype.h"
#include "gdb_assert.h"

#ifdef __QNX__
#include <sys/debug.h>
#include <sys/elf_notes.h>
#define __ELF_H_INCLUDED /* Needed for our link.h to avoid including elf.h.  */
#include <sys/link.h>
typedef debug_thread_t nto_procfs_status;
#else
#include "nto-share/debug.h"
#endif

#define QNX_NOTE_NAME	"QNX"

#ifdef __CYGWIN__
#include <sys/cygwin.h>
#endif


/* The following define does a cast to const gdb_byte * type.  */

#define EXTRACT_SIGNED_INTEGER(ptr, len) extract_signed_integer ((const gdb_byte *)(ptr), len)
#define EXTRACT_UNSIGNED_INTEGER(ptr, len) extract_unsigned_integer ((const gdb_byte *)(ptr), len)

static char default_nto_target[] = "";

struct nto_target_ops current_nto_target;

unsigned int nto_inferior_stopped_flags;

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
	    *temp_pathname = NULL;
	}
    }
  return ret;
}

/* The following two variables are defined in solib.c.  */
extern char *gdb_sysroot; /* a.k.a solib-absolute-prefix  */
extern char *solib_search_path;

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

  /* Do not change it if already set.  */
  if (!gdb_sysroot
      || strlen (gdb_sysroot) == 0)
    {
      sprintf (buf, "set solib-absolute-prefix %s", arch_path);
      execute_command (buf, 0);
    }

  if (!solib_search_path
      || strlen (solib_search_path) == 0)
    {
      sprintf (buf, "set solib-search-path %s/%s%c%s/%s", 
	      arch_path, "lib", DIRNAME_SEPARATOR, 
	      arch_path, "usr/lib");
      execute_command (buf, 0);
    }
}

char **
nto_parse_redirection (char *pargv[], const char **pin, 
		       const char **pout, const char **perr)
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

      /* r_debug structure.  */
      lmo.r_version_offset = 0;
      lmo.r_version_size = 4;
      lmo.r_map_offset = 4;
      lmo.r_state_offset = 12;
      lmo.r_state_size = 4;
      lmo.r_rdevent_offset = 24;
      lmo.r_rdevent_size = 4;
      lmo.r_ldsomap_offset = -1; /* Our ldd is in libc, we do not want it to
				    show up twice.  */

      /* Link map.  */
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

/* Link map info to include in an allocated so_list entry */

struct lm_info
  {
    /* Pointer to copy of link map from inferior.  The type is char *
       rather than void *, so that we may use byte offsets to find the
       various fields without the need for a cast.  */
    gdb_byte *lm;

    /* Amount by which addresses in the binary should be relocated to
       match the inferior.  This could most often be taken directly
       from lm, but when prelinking is involved and the prelink base
       address changes, we may need a different offset, we want to
       warn about the difference and compute it only once.  */
    CORE_ADDR l_addr;
  };

static CORE_ADDR
LM_ADDR_FROM_LINK_MAP (struct so_list *so)
{
  struct link_map_offsets *lmo = nto_fetch_link_map_offsets ();

  if (so->lm_info->l_addr == (CORE_ADDR)-1)
    so->lm_info->l_addr = extract_typed_address (so->lm_info->lm 
						 + lmo->l_addr_offset,
						 builtin_type_void_data_ptr);
  return so->lm_info->l_addr;
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

  sec->addr = nto_truncate_ptr (sec->addr 
			        + LM_ADDR_FROM_LINK_MAP (so)
				- vaddr);
  sec->endaddr = nto_truncate_ptr (sec->endaddr 
				   + LM_ADDR_FROM_LINK_MAP (so)
				   - vaddr);
}

/* This is cheating a bit because our linker code is in libc.so.  If we
   ever implement lazy linking, this may need to be re-examined.  */
int
nto_in_dynsym_resolve_code (CORE_ADDR pc)
{
  if (in_plt_section (pc, NULL)) 
    return 1;
  return 0;
}

void
nto_dummy_supply_regset (struct regcache *regcache, char *regs)
{
  /* Do nothing.  */
}

static void
nto_sniff_abi_note_section (bfd *abfd, asection *sect, void *obj)
{
  const char *sectname;
  unsigned int sectsize;
  char *note; // buffer holding the section contents
  unsigned int namelen, type;
  const char *name;

  sectname = bfd_get_section_name (abfd, sect);
  sectsize = bfd_section_size (abfd, sect);

  nto_trace (3) ("%s sectname=%s size=%d\n", __func__, sectname, sectsize);

  /* TODO: limit the note size here, for now limit is 128 bytes
     (enough to check the name and type).  */
  if (sectsize > 128)
    sectsize = 128;

  if (sectname == strstr(sectname, "note")) 
    {
      note = alloca (sectsize); 
      bfd_get_section_contents (abfd, sect, note, 0, sectsize);
      namelen = (unsigned int) bfd_h_get_32 (abfd, note);
      name = note + 12;

      if (namelen != strlen (QNX_NOTE_NAME) + 1 
	  || 0 != strcmp (name, QNX_NOTE_NAME)) 
        {
	  nto_trace (0) (
	    "Section name starts with 'note', but our name not found (%s)\n", 
	    name);
	  goto not_ours;
	}

      type = (unsigned int) bfd_h_get_32 (abfd, note + 8);

      switch (type)
        {
	  case QNT_NULL:
	    nto_trace (0) ("Type QNT_NULL not expected\n");
	    gdb_assert (0);
	    break;
	  case QNT_CORE_SYSINFO:
	    nto_trace (0) ("Type QNT_CORE_SYSINFO\n");
	    *(enum gdb_osabi *) obj = GDB_OSABI_QNXNTO;
	    break;
	  case QNT_CORE_INFO:
	    nto_trace (0) ("Type QNT_CORE_INFO\n");
	    break;
	  default:
	    {
	      nto_trace (0) ("Note type not expected (%d).\n", type);
	    }
        }

not_ours:
       { /* We do nothing here.  */ } 
    }
}

enum gdb_osabi
nto_elf_osabi_sniffer (bfd *abfd)
{
  unsigned int elfosabi;
  unsigned int elftype;
  enum gdb_osabi osabi = GDB_OSABI_UNKNOWN;

  /* Note: if we ever get to sign our binaries, we should
     really check if the OSABI matches. But untill then, just
     hope the user knows what they are doing and are really opening
     QNXNTO binary.  */

  elftype = elf_elfheader (abfd)->e_type;

  if (elftype == ET_CORE)
      /* We do properly mark our core files, get the OSABI from
         core note section.  */
      bfd_map_over_sections (abfd,
			     nto_sniff_abi_note_section, 
			     &osabi);
  else
  /* Note: if we ever get to sign our binaries, we should
     really check if the OSABI matches. But untill then, just
     hope the user knows what they are doing and are really opening
     QNXNTO binary.  */
    osabi = GDB_OSABI_QNXNTO;

  if (nto_internal_debugging)
    gdb_assert (osabi == GDB_OSABI_QNXNTO);
  return osabi;
}

char *
nto_target_extra_thread_info (struct thread_info *ti)
{
  if (ti && ti->private && ti->private->name[0])
    return ti->private->name;
  return "";
}

void
nto_initialize_signals (struct gdbarch *gdbarch)
{
  set_gdbarch_target_signal_from_host (gdbarch, target_signal_from_nto);
  set_gdbarch_target_signal_to_host (gdbarch, target_signal_to_nto);

  /* We use SIG45 for pulses, or something, so nostop, noprint
     and pass them.  */
  signal_stop_update (target_signal_from_name ("SIG45"), 0);
  signal_print_update (target_signal_from_name ("SIG45"), 0);
  signal_pass_update (target_signal_from_name ("SIG45"), 1);

  /* By default we don't want to stop on these two, but we do want to pass.  */
#if defined(SIGSELECT)
  signal_stop_update (target_signal_from_nto (gdbarch, SIGSELECT), 0);
  signal_print_update (target_signal_from_nto (gdbarch, SIGSELECT), 0);
  signal_pass_update (target_signal_from_nto (gdbarch, SIGSELECT), 1);
#endif

#if defined(SIGPHOTON)
  signal_stop_update (target_signal_from_nto (gdbarch, SIGPHOTON), 0);
  signal_print_update (target_signal_from_nto (gdbarch, SIGPHOTON), 0);
  signal_pass_update (target_signal_from_nto (gdbarch, SIGPHOTON), 1);
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
  char star = ' ';
  int tid = 0;
  int state = 0;
  int flags = 0;

  if (tp)
    {
      if (ptid_equal (tp->ptid, inferior_ptid))
	star = '*';

      if (tp->private)
	{
	  tid = tp->private->tid;
	  state = tp->private->state;
	  flags = tp->private->flags;
	}
      else
	tid = ptid_get_tid (tp->ptid);

      printf_filtered ("%c%d\t%d\t%d\n", star, tid, state, flags);
    }

  return 0;
}

static void 
nto_info_tidinfo_command (char *args, int from_tty)
{
  char *execfile = get_exec_file (0);
  nto_trace (0) ("%s (args=%s, from_tty=%d)\n", __func__, 
		  args ? args : "(null)", from_tty);

  target_find_new_threads ();
  printf_filtered("Threads for pid %d (%s)\nTid:\tState:\tFlags:\n", 
		  ptid_get_pid (inferior_ptid), execfile ? execfile : "");
  
  iterate_over_threads (nto_print_tidinfo_callback, NULL);
}

int
qnx_filename_cmp (const char *s1, const char *s2)
{
  int c1, c2;
nto_trace (0) ("%s(%s,%s)\n", __func__, s1, s2);
  gdb_assert (s1 != NULL);
  gdb_assert (s2 != NULL);

  if (0 == strcmp (s1, s2))
    return 0;

  for (;;)
    {

#ifdef HAVE_DOS_BASED_FILE_SYSTEM
      int c1 = TOLOWER (*s1);
      int c2 = TOLOWER (*s2);
#else
      int c1 = *s1;
      int c2 = *s2;
#endif

      /* On DOS-based file systems, the '/' and the '\' are equivalent.  */

      if (c1 == '\\')
        c1 = '/';
      if (c2 == '\\')
        c2 = '/';

      if (c1 != c2)
        return (c1 - c2);

      if (c1 == '\0')
        return 0;

      s1++;
      s2++;
    }
}

/* NOTE: this function basically overrides libiberty's implementation.  */
int
filename_cmp (const char *s1, const char *s2)
{
  return qnx_filename_cmp (s1, s2);
}

/* Used in breakpoint.c as SOLIB_HAVE_LOAD_EVENT.  */
int
nto_break_on_this_solib_event (enum bptype type)
{
  struct breakpoint *bpt;
  CORE_ADDR address = svr4_fetch_r_debug ();
  gdb_byte myaddr[128];
  unsigned int len = sizeof (myaddr);
  struct link_map_offsets *lmo = nto_fetch_link_map_offsets ();
  unsigned int rt_state;
  unsigned int rd_event;

  nto_trace (0) ("%s\n", __func__);

  /* see if we should stop here. */
  /* See what is going on.  */
  if (target_read_memory (address, myaddr, len))
    /* Could not read memory. */
    return 0;

  rt_state = extract_unsigned_integer (&myaddr[lmo->r_state_offset], 
				       lmo->r_state_size);
  rd_event = extract_unsigned_integer (&myaddr[lmo->r_rdevent_offset], 
				       lmo->r_rdevent_size);
  if (rt_state == RT_ADD && type == bp_catch_load)
    return 1;
  if (rt_state == RT_DELETE && type == bp_catch_unload)
    return 1;
  return 0;
}

extern struct gdbarch *core_gdbarch;

/* Add thread status for the given gdb_thread_id.  */

static void
nto_core_add_thread_status_info (pid_t core_pid, int gdb_thread_id, const nto_procfs_status *ps)
{
  struct thread_info *ti;
  ptid_t ptid;
  struct private_thread_info *priv;
  struct gdbarch *curr_gdbarch = current_gdbarch;
 
  /* See corelow, function add_to_thread_list for details on pid.  */
  ptid = ptid_build(core_pid, 0, gdb_thread_id);
  ti = find_thread_pid(ptid);
  if(!ti)
    {
      warning ("Thread with gdb id %d not found.\n", gdb_thread_id);
      return;
    }
  priv = malloc (sizeof (*priv));
  if (priv == NULL)
    {
      warning ("Out of memory.\n");
      return;
    }
  memset (priv, 0, sizeof (*priv));
  ti->private = priv;
  if (core_gdbarch != current_gdbarch)
    /* Dirty hack - current_gdbarch is not the same, and we need
       core_gdbarch for endiannes.  */
    current_gdbarch = core_gdbarch;
  priv->tid = EXTRACT_UNSIGNED_INTEGER (&ps->tid, sizeof (ps->tid));
  priv->state = EXTRACT_UNSIGNED_INTEGER (&ps->state, sizeof (ps->state)); 
  priv->flags = EXTRACT_UNSIGNED_INTEGER (&ps->flags, sizeof (ps->flags));
  if (curr_gdbarch != current_gdbarch)
    current_gdbarch = curr_gdbarch;
}

/* Add thread statuses read from qnx notes.  */
static void
nto_core_add_thread_private_data (bfd *abfd, asection *sect, void *notused)
{
  const char *sectname;
  unsigned int sectsize;
  const char qnx_core_status[] = ".qnx_core_status/";
  const unsigned int qnx_sectnamelen = 17;/* strlen (qnx_core_status).  */
  const char warning_msg[] = "Unable to read %s section from core.\n";
  int gdb_thread_id;
  int data_ofs;
  nto_procfs_status status;
  int len;

  sectname = bfd_get_section_name (abfd, sect);
  sectsize = bfd_section_size (abfd, sect);
  if (sectsize > sizeof (status))
    sectsize = sizeof (status);

  if (strncmp (sectname, qnx_core_status, qnx_sectnamelen) != 0) 
    return;

  if (bfd_seek (abfd, sect->filepos, SEEK_SET) != 0)
    {
      warning (warning_msg, sectname);
      return;
    }
  len = bfd_bread ((gdb_byte *)&status, sectsize, abfd);
  if (len != sectsize)
    {
      warning (warning_msg, sectname);
      return;
    }
  gdb_thread_id = atoi (sectname + qnx_sectnamelen);
  nto_core_add_thread_status_info (elf_tdata (abfd)->core_pid, gdb_thread_id, &status);
}

static void (*original_core_open) (char *, int);

static void
nto_core_open (char *filename, int from_tty)
{
  nto_trace (0) ("%s (%s)\n", __func__, filename);

  original_core_open (filename, from_tty);
  nto_init_solib_absolute_prefix ();

  /* Now we need to load additional thread status information stored
     in qnx notes.  */
  if (core_bfd)
    bfd_map_over_sections (core_bfd, nto_core_add_thread_private_data, NULL);
}

static void
init_nto_core_ops ()
{
  gdb_assert (core_ops.to_shortname != NULL 
	      && !!"core_ops must be initialized first!");
  core_ops.to_extra_thread_info = nto_target_extra_thread_info;
  original_core_open = core_ops.to_open;
  if (!original_core_open)
    error ("Orignal core open not set yet\n");
  core_ops.to_open = nto_core_open;
}

int
nto_stopped_by_watchpoint (void)
{
  /* NOTE: nto_stopped_by_watchpoint will be called ONLY while we are 
     stopped due to a SIGTRAP.  This assumes gdb works in 'all-stop' mode;
     future gdb versions will likely run in 'non-stop' mode in which case 
     we will have to store/examine statuses per thread in question.  
     Until then, this will work fine.  */

  return nto_inferior_stopped_flags 
	 & (_DEBUG_FLAG_TRACE_RD
	    | _DEBUG_FLAG_TRACE_WR
	    | _DEBUG_FLAG_TRACE_MODIFY);
}

/* Prevent corelow.c from adding core_ops target. We will do it
   after overriding some of the default functions. See comment in
   corelow.c for details.  */
int coreops_suppress_target = 1;

void
_initialize_nto_tdep (void)
{
  init_nto_core_ops ();
  add_target (&core_ops);

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
			    &setdebuglist,
			    &showdebuglist);

  add_info ("tidinfo", nto_info_tidinfo_command, "List threads for current process." );
 nto_fetch_link_map_offsets = nto_generic_svr4_fetch_link_map_offsets;
 nto_is_nto_target = nto_elf_osabi_sniffer;
}

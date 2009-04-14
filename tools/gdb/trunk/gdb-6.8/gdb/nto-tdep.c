/* nto-tdep.c - general QNX Neutrino target functionality.

   Copyright (C) 2003, 2004, 2007, 2008 Free Software Foundation, Inc.

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
#include "solist.h"
#include "gdbcore.h"
#include "objfiles.h"
#include "filenames.h"

#include "gdbcmd.h"
#include "safe-ctype.h"
#include "gdb_assert.h"

#include "observer.h"

#ifdef __QNX__
#include <sys/debug.h>
#include <sys/elf_notes.h>
#define __ELF_H_INCLUDED /* Needed for our link.h to avoid including elf.h.  */
#include <sys/link.h>
typedef debug_thread_t nto_procfs_status;
typedef debug_process_t nto_procfs_info;
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

/* Helper function, calculates architecture path, e.g.
   /opt/qnx640/target/qnx6/ppcbe
   It allocates string, callers must free the string using free.  */

static char *
nto_build_arch_path ()
{
  const char *nto_root, *arch, *endian;
  char *arch_path;
  const char *variant_suffix = "";

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

  if (strcmp (arch, "ppc") == 0)
    {
      struct bfd_arch_info const *info = 
	gdbarch_bfd_arch_info (current_gdbarch);

      nto_trace (1) ("Selecting -spe variant\n");

      if (info->mach == bfd_mach_ppc_e500)
	variant_suffix = "-spe";
    }

  /* In case nto_root is short, add strlen(solib)
     so we can reuse arch_path below.  */
  arch_path =
    malloc (strlen (nto_root) + strlen (arch) + strlen (endian) 
	    + strlen (variant_suffix) +	2);
  sprintf (arch_path, "%s/%s%s%s", nto_root, arch, endian, variant_suffix);
  return arch_path;
}

int
nto_find_and_open_solib (char *solib, unsigned o_flags, char **temp_pathname)
{
  char *buf, *arch_path, *base;
  const char *arch;
  int ret;
#define PATH_FMT "%s/lib%c%s/usr/lib%c%s/usr/photon/lib%c" \
		 "%s/usr/photon/dll%c%s/lib/dll"

  arch_path = nto_build_arch_path ();
  buf = alloca (strlen (PATH_FMT) + strlen (arch_path) * 5 + 1);
  free (arch_path);
  sprintf (buf, PATH_FMT, arch_path, DIRNAME_SEPARATOR,
	   arch_path, DIRNAME_SEPARATOR, arch_path, DIRNAME_SEPARATOR,
	   arch_path, DIRNAME_SEPARATOR, arch_path);

  /* Don't assume basename() isn't destructive.  */
  base = strrchr (solib, '/');
  if (!base)
    base = solib;
  else
    base++;			/* Skip over '/'.  */

  ret = openp (buf, 1, base, o_flags, 0, temp_pathname);
  if (ret < 0 && base != solib)
    {
      sprintf (buf, "/%s", solib);
      ret = open (buf, o_flags, 0);
      if (temp_pathname)
	{
	  if (ret >= 0)
	    *temp_pathname = gdb_realpath (buf);
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
  /* If it was nto_init_solib_absolute_prefix that set the paths,
   the following variables will be set to 1.  */
  static int nto_set_gdb_sysroot;
  static int nto_set_solib_search_path;

  char *buf, *arch_path;
  char *nto_root; 
  const char *endian;
  const char *arch;

  arch_path = nto_build_arch_path ();

  nto_trace (0) ("nto_init_solib_absolute_prefix\n");

  /* Do not change it if already set.  */
  if ((!gdb_sysroot
      || strlen (gdb_sysroot) == 0)
      || nto_set_gdb_sysroot)
    {
      buf = alloca (26 /* set solib-absolute-prefix */ 
		    + strlen (arch_path) + 1);
      sprintf (buf, "set solib-absolute-prefix %s", arch_path);
      execute_command (buf, 0);
      nto_set_gdb_sysroot = 1;
    }

  if ((!solib_search_path
      || strlen (solib_search_path) == 0)
      || nto_set_solib_search_path)
    {
      const char * const setcmd = "set solib-search-path ";
      const char * const subdirs[] = { "lib", "usr/lib", "lib/dll", NULL };
      unsigned int subdirs_len = 0;
      const unsigned int subdirs_num = sizeof (subdirs) / sizeof (subdirs[0]);
      const char * const *pivot;

      buf = alloca (strlen (setcmd)
		    + strlen (arch_path) * subdirs_num
		    + subdirs_num - 1 /* For DIRNAME_SEPARATOR.  */
		    + subdirs_num /* For  path separator '/' */ 
		    + subdirs_len
		    + 1 /* for final '\0' */ );

      sprintf (buf, "%s", setcmd);
      for (pivot = subdirs; *pivot != NULL; ++pivot)
	{
	  sprintf (buf + strlen (buf), "%s/%s", arch_path, *pivot);
	  if (*(pivot + 1) != NULL)
	    sprintf (buf + strlen (buf), "%c", DIRNAME_SEPARATOR);
	}

      /* Do not set it if already set. Otherwise, this would cause
         re-reading symbols.  */
      if (solib_search_path == NULL
	  || strcmp (solib_search_path, buf + strlen (setcmd)) != 0)
	{
	  nto_trace (0) ("Executing %s\n", buf);
	  execute_command (buf, 0);
	}
      nto_set_solib_search_path = 1;
    }
  free (arch_path);
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
      lmo.r_brk_offset = 8;
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

      lmo.l_ld_offset = 8;

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

static Elf_Internal_Phdr *
find_load_phdr_2 (bfd *abfd, unsigned int p_filesz, 
		  unsigned int p_memsz, unsigned int p_flags,
		  unsigned int p_align)
{
  Elf_Internal_Phdr *phdr;
  unsigned int i;

  if (!elf_tdata (abfd))
    return NULL;

  phdr = elf_tdata (abfd)->phdr;
  for (i = 0; i < elf_elfheader (abfd)->e_phnum; i++, phdr++)
    {
      if (phdr->p_type == PT_LOAD && phdr->p_flags == p_flags
	  && phdr->p_memsz == p_memsz && phdr->p_filesz == p_filesz
	  && phdr->p_align == p_align)
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
  if (so->addr_low == 0)
    so->addr_low = LM_ADDR_FROM_LINK_MAP (so);
  if (so->addr_high < sec->endaddr)
    so->addr_high = sec->endaddr;
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



/* NTO Core handling.  */

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
static void (*original_core_close) (int);

/* When opening a core, we do not want to set inferior hooks.  */
static struct target_so_ops backup_so_ops;

struct target_ops original_core_ops;


static void
nto_core_solib_create_inferior_hook (void)
{
  /* Do nothing.  */
}

struct auxv_buf
{
  LONGEST len;
  LONGEST len_read; /* For passing result. Can be len, 0, or -1  */
  gdb_byte *readbuf;
};

/* Read AUXV from initial_stack.  */
LONGEST
nto_read_auxv_from_initial_stack (CORE_ADDR initial_stack, gdb_byte *readbuf,
				  LONGEST len)
{
  int data_ofs = 0;
  int anint32;
  LONGEST len_read = 0;
  gdb_byte *panint32 = (gdb_byte*)&anint32;
  gdb_byte *buff;

  /* Skip over argc, argv and envp... (see comment in ldd.c)  */
  if (target_read_memory (initial_stack + data_ofs, panint32, 4) != 0)
    return 0;

  anint32 = EXTRACT_UNSIGNED_INTEGER (panint32, sizeof (anint32));

  /* Size of pointer is assumed to be 4 bytes (32 bit arch. ) */
  data_ofs += (anint32 + 2) * 4; /* + 2 comes from argc itself and
				    NULL terminating pointer in argv */

  /* Now loop over env table:  */
  while (target_read_memory (initial_stack + data_ofs, panint32, 4) == 0)
    {
      anint32 = EXTRACT_SIGNED_INTEGER (panint32, sizeof (anint32));
      data_ofs += 4;
      if (anint32 == 0)
	break;
    }
  initial_stack += data_ofs;

  memset (readbuf, 0, len);
  buff = readbuf;
  while (len_read <= len-8)
    {
      /* For 32-bit architecture, size of auxv_t is 8 bytes.  */

      /* Search backwards until we have read AT_PHDR (num. 3),
	 AT_PHENT (num 4), AT_PHNUM (num 5)  */
      if (target_read_memory (initial_stack, buff, 8)
	  == 0)
	{
	  int a_type = EXTRACT_SIGNED_INTEGER (buff, sizeof (a_type));
	  if (a_type != AT_NULL)
	    {
	      buff += 8;
	      len_read += 8;
	      nto_trace (0) ("Read a_type: %d\n", a_type);
	    }
	  if (a_type == AT_PHNUM) /* That's all we need.  */
	    break;
	  initial_stack += 8;
	}
      else
	break;
    }
  return len_read;
}

/* Read AUXV from note.  */
static void
nto_core_read_auxv_from_note (bfd *abfd, asection *sect, void *pauxv_buf)
{
  struct auxv_buf *auxv_buf = (struct auxv_buf *)pauxv_buf;
  const char *sectname;
  unsigned int sectsize;
  const char qnx_core_info[] = ".qnx_core_info/";
  const unsigned int qnx_sectnamelen = 14;/* strlen (qnx_core_status).  */
  const char warning_msg[] = "Unable to read %s section from core.\n";
  int data_ofs;
  nto_procfs_info info;
  int len;
  gdb_byte *buff; /* For skipping over argc, argv and envp-s */
  int anint32;
  CORE_ADDR initial_stack, base_address;

  sectname = bfd_get_section_name (abfd, sect);
  sectsize = bfd_section_size (abfd, sect);
  if (sectsize > sizeof (info))
    sectsize = sizeof (info);

  if (strncmp (sectname, qnx_core_info, qnx_sectnamelen) != 0) 
    return;

  if (bfd_seek (abfd, sect->filepos, SEEK_SET) != 0)
    {
      warning (warning_msg, sectname);
      return;
    }
  len = bfd_bread ((gdb_byte *)&info, sectsize, abfd);
  if (len != sectsize)
    {
      warning (warning_msg, sectname);
      return;
    }
  initial_stack = EXTRACT_UNSIGNED_INTEGER 
    (&info.initial_stack, sizeof (info.initial_stack));
  base_address = EXTRACT_UNSIGNED_INTEGER
    (&info.base_address, sizeof (info.base_address));
  buff = auxv_buf->readbuf;

  auxv_buf->len_read = nto_read_auxv_from_initial_stack 
    (initial_stack, auxv_buf->readbuf, auxv_buf->len);
}

static LONGEST
nto_core_xfer_partial (struct target_ops *ops, enum target_object object,
		       const char *annex, gdb_byte *readbuf,
		       const gdb_byte *writebuf, ULONGEST offset, LONGEST len)
{
  if (object == TARGET_OBJECT_AUXV
      && readbuf)
    {
      struct auxv_buf auxv_buf;

      auxv_buf.len = len;
      auxv_buf.len_read = 0;
      auxv_buf.readbuf = readbuf;
      
      if (offset > 0)
	return 0;

      bfd_map_over_sections (core_bfd, nto_core_read_auxv_from_note, &auxv_buf);
      if (auxv_buf.len_read > 0)
	return auxv_buf.len_read;
    }

  /* In any other case, try default code.  */
  return original_core_ops.to_xfer_partial (ops, object, annex, readbuf,
					    writebuf, offset, len);
} 

static void
nto_core_open (char *filename, int from_tty)
{
  /* Backup target_so_ops.  */
  backup_so_ops = *current_target_so_ops;

  nto_trace (0) ("%s (%s)\n", __func__, filename);
  original_core_ops.to_open (filename, from_tty);
  /* Now we need to load additional thread status information stored
     in qnx notes.  */
  if (core_bfd)
    bfd_map_over_sections (core_bfd, nto_core_add_thread_private_data, NULL);
}

static void
nto_core_close (int i)
{
  original_core_ops.to_close (i);
  /* Revert target_so_ops.  */
  *current_target_so_ops = backup_so_ops;
}

static void
init_nto_core_ops ()
{
  gdb_assert (core_ops.to_shortname != NULL 
	      && !!"core_ops must be initialized first!");
  original_core_ops = core_ops;
  core_ops.to_extra_thread_info = nto_target_extra_thread_info;
  core_ops.to_open = nto_core_open;
  core_ops.to_close = nto_core_close;
  core_ops.to_xfer_partial = nto_core_xfer_partial;
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


/* Check for mismatching solibs.  */

static void
nto_solib_added_listener (struct so_list *solib)
{
  /* Check if the libraries match.
     We compare all PT_LOAD segments.  */
  CORE_ADDR mem_phdr_addr;
  CORE_ADDR phdr_offs_addr = solib->addr_low + 28; 
  /* See Elf32_Ehdr, 28 is offset of e_phoff.  */
  gdb_byte offs_buf[4]; /* Offset is defined Elf32_Off 
			   which is 4 bytes size. */

  if (target_read_memory (phdr_offs_addr, offs_buf, sizeof (offs_buf)))
    {
      nto_trace (0) ("Could not read memory.\n");
      return;
    }

  mem_phdr_addr = solib->addr_low 
		  + extract_typed_address (offs_buf, 
					   builtin_type_void_data_ptr); 

  while (1)
    {
      gdb_byte phdr_buf[32]; /* 32 == sizeof (Elf32_Phdr) */
      /* We compare phdr fields: p_type, p_flags, p_aign, p_filesz, p_memsz */
      unsigned int p_type;
      unsigned int p_filesz;
      unsigned int p_memsz;
      unsigned int p_flags;
      unsigned int p_align;
      Elf_Internal_Phdr *file_phdr;

      if (target_read_memory (mem_phdr_addr, phdr_buf, sizeof (phdr_buf)))
	{
	  nto_trace (0) ("Could not read phdr\n");
	  return;
	}

      p_type = extract_unsigned_integer (&phdr_buf[0], 4);
      if (p_type == PT_LOAD)
	{
	  p_filesz = extract_unsigned_integer (&phdr_buf[16], 4);
	  p_memsz = extract_unsigned_integer (&phdr_buf[20], 4);
	  p_flags = extract_unsigned_integer (&phdr_buf[24], 4);
	  p_align = extract_unsigned_integer (&phdr_buf[28], 4);

	  file_phdr = find_load_phdr_2 (solib->abfd, 
					p_filesz, p_memsz, p_flags, p_align); 
	  if (file_phdr == NULL)
	    {
	      warning ("Host file %s does not match target file.",
		       solib->so_name);
	      break;
	    }
	}

      if (p_type == PT_NULL)
	break;

      mem_phdr_addr += sizeof (phdr_buf);
    }
}

static void
nto_architecture_changed_listener (struct gdbarch *newarch)
{
  nto_trace (0) ("%s\n", __func__);
  nto_init_solib_absolute_prefix ();
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

 observer_attach_solib_loaded (nto_solib_added_listener);
 observer_attach_architecture_changed (nto_architecture_changed_listener);
}

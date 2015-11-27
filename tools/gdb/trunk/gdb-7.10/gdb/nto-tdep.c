/* nto-tdep.c - general QNX Neutrino target functionality.

   Copyright (C) 2003-2015 Free Software Foundation, Inc.

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
#include <sys/stat.h>
#include "nto-tdep.h"
#include "top.h"
#include "inferior.h"
#include "infrun.h"
#include "gdbarch.h"
#include "bfd.h"
#include "elf-bfd.h"
#include "solib-svr4.h"
#include "gdbcore.h"
#include "objfiles.h"
#include "gdbarch.h"

#define QNX_NOTE_NAME	"QNX"
#define QNX_INFO_SECT_NAME "QNX_info"

#ifdef __QNXNTO__
#include <sys/debug.h>
typedef debug_process_t nto_procfs_info;
typedef union nto_procfs_status {
  debug_thread32_t _32;
  debug_thread64_t _64;
} nto_procfs_status;
typedef union nto_siginfo_t {
  __siginfo32_t _32;
  __siginfo64_t _64;
} nto_siginfo_t;
#define nto_si_pid	si_pid
#define nto_si_uid	si_uid
#define nto_si_value	si_value
#define nto_si_utime	si_utime
#define nto_si_status	si_status
#define nto_si_stime	si_stime
#define nto_si_fltno	si_fltno
#define nto_si_fltip	si_fltip
#define nto_si_addr	si_addr
#define nto_si_bdslot	si_bdslot
#else
#include "nto-share/debug.h"
#endif

struct nto_target_ops current_nto_target;

static char default_nto_target[] = "";

static const struct inferior_data *nto_inferior_data_reg;

static struct gdbarch_data *nto_gdbarch_data_handle;

struct nto_gdbarch_data
  {
    struct type *siginfo_type;
  };

static void *
init_nto_gdbarch_data (struct gdbarch *gdbarch)
{
  return GDBARCH_OBSTACK_ZALLOC (gdbarch, struct nto_gdbarch_data);
}

static struct nto_gdbarch_data *
get_nto_gdbarch_data (struct gdbarch *gdbarch)
{
  return gdbarch_data (gdbarch, nto_gdbarch_data_handle);
}

struct type *
nto_get_siginfo_type (struct gdbarch *gdbarch)
{
  struct nto_gdbarch_data *nto_gdbarch_data;
  struct type *int_type, *uint_type, *long_type, *void_ptr_type;
  struct type *uid_type, *pid_type;
  struct type *sigval_type, *clock_type;
  struct type *siginfo_type, *sidata_type;
  struct type *siproc_type, *sipdata_type, *type;

  nto_gdbarch_data = get_nto_gdbarch_data (gdbarch);
  if (nto_gdbarch_data->siginfo_type != NULL)
    return nto_gdbarch_data->siginfo_type;

  int_type = arch_integer_type (gdbarch, gdbarch_int_bit (gdbarch),
			 	0, "int");
  uint_type = arch_integer_type (gdbarch, gdbarch_int_bit (gdbarch),
				 1, "unsigned int");
  long_type = arch_integer_type (gdbarch, gdbarch_long_bit (gdbarch),
				 0, "long");
  void_ptr_type = lookup_pointer_type (builtin_type (gdbarch)->builtin_void);

  /* union sigval */
  sigval_type = arch_composite_type (gdbarch, NULL, TYPE_CODE_UNION);
  TYPE_NAME (sigval_type) = xstrdup ("union sigval");
  append_composite_type_field (sigval_type, "sival_int", int_type);
  append_composite_type_field_aligned (sigval_type, "sival_ptr",
				       void_ptr_type, TYPE_LENGTH (long_type));

  /* pid_t */
  pid_type = arch_type (gdbarch, TYPE_CODE_TYPEDEF,
			TYPE_LENGTH (int_type), "pid_t");
  TYPE_TARGET_TYPE (pid_type) = int_type;
  TYPE_TARGET_STUB (pid_type) = 1;

  /* uid_t */
  uid_type = arch_type (gdbarch, TYPE_CODE_TYPEDEF,
			TYPE_LENGTH (uint_type), "uid_t");
  TYPE_TARGET_TYPE (uid_type) = int_type;
  TYPE_TARGET_STUB (uid_type) = 1;

  /* clock_t */
  clock_type = arch_type (gdbarch, TYPE_CODE_TYPEDEF,
			  TYPE_LENGTH (uint_type), "clock_t");
  TYPE_TARGET_TYPE (clock_type) = uint_type;
  TYPE_TARGET_STUB (clock_type) = 1;

  /* __data */
  sidata_type = arch_composite_type (gdbarch, NULL, TYPE_CODE_UNION);

  /* __pad */
  append_composite_type_field (sidata_type, "__pad",
			       init_vector_type (int_type, 7));

  /* __data.__proc */
  siproc_type = arch_composite_type (gdbarch, NULL, TYPE_CODE_STRUCT);
  append_composite_type_field (siproc_type, "__pid", pid_type);

  /* __data.__pdata */
  sipdata_type = arch_composite_type (gdbarch, NULL, TYPE_CODE_UNION);

  /* __data.__pdata.__kill */
  type = arch_composite_type (gdbarch, NULL, TYPE_CODE_STRUCT);
  append_composite_type_field (type, "__uid", uid_type);
  append_composite_type_field (type, "__value", sigval_type);
  append_composite_type_field (sipdata_type, "__kill", type);

  /* __data.__pdata.__chld */
  type = arch_composite_type (gdbarch, NULL, TYPE_CODE_STRUCT);
  append_composite_type_field (type, "__utime", clock_type);
  append_composite_type_field (type, "__status", int_type);
  append_composite_type_field (type, "__stime", clock_type);
  append_composite_type_field_aligned (sipdata_type, "__chld", type,
				       TYPE_LENGTH (long_type));
  append_composite_type_field_aligned (siproc_type, "__pdata", sipdata_type,
				       TYPE_LENGTH (long_type));
  append_composite_type_field_aligned (sidata_type, "__proc", siproc_type,
				       TYPE_LENGTH (long_type));

  /* __data.__fault */
  type = arch_composite_type (gdbarch, NULL, TYPE_CODE_STRUCT);
  append_composite_type_field (type, "__fltno", int_type);
  append_composite_type_field_aligned (type, "__fltip", void_ptr_type,
				       TYPE_LENGTH (long_type));
  append_composite_type_field_aligned (type, "__addr", void_ptr_type,
				       TYPE_LENGTH (long_type));
  append_composite_type_field_aligned (type, "__bdslot", int_type,
				       TYPE_LENGTH (long_type));
  append_composite_type_field_aligned (sidata_type, "__fault", type,
				       TYPE_LENGTH (long_type));

  /* struct siginfo */
  siginfo_type = arch_composite_type (gdbarch, NULL, TYPE_CODE_STRUCT);
  TYPE_NAME (siginfo_type) = xstrdup ("siginfo");
  append_composite_type_field (siginfo_type, "si_signo", int_type);
  append_composite_type_field (siginfo_type, "si_code", int_type);
  append_composite_type_field (siginfo_type, "si_errno", int_type);
  append_composite_type_field_aligned (siginfo_type,
				       "__data", sidata_type,
				       TYPE_LENGTH (long_type));

  nto_gdbarch_data->siginfo_type = siginfo_type;

  return siginfo_type;
}

static char *
nto_target (void)
{
  char *p = getenv ("QNX_TARGET");

#ifdef __CYGWIN__
  static char buf[PATH_MAX];
  if (p)
    cygwin_conv_path (CCP_WIN_A_TO_POSIX, p, buf, PATH_MAX);
  else
    cygwin_conv_path (CCP_WIN_A_TO_POSIX, default_nto_target, buf, PATH_MAX);
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
  char *buf, *arch_path, *nto_root;
  const char *endian;
  const char *base;
  const char *arch;
  int arch_len, len, ret;
#define PATH_FMT \
  "%s/lib:%s/usr/lib:%s/usr/photon/lib:%s/usr/photon/dll:%s/lib/dll"

  nto_root = nto_target ();
  if (strcmp (gdbarch_bfd_arch_info (target_gdbarch ())->arch_name, "i386") == 0)
    {
      arch = "x86";
      endian = "";
    }
  else if (strcmp (gdbarch_bfd_arch_info (target_gdbarch ())->arch_name,
		   "rs6000") == 0
	   || strcmp (gdbarch_bfd_arch_info (target_gdbarch ())->arch_name,
		   "powerpc") == 0)
    {
      arch = "ppc";
      endian = "be";
    }
  else
    {
      arch = gdbarch_bfd_arch_info (target_gdbarch ())->arch_name;
      endian = gdbarch_byte_order (target_gdbarch ())
	       == BFD_ENDIAN_BIG ? "be" : "le";
    }

  /* In case nto_root is short, add strlen(solib)
     so we can reuse arch_path below.  */

  arch_len = (strlen (nto_root) + strlen (arch) + strlen (endian) + 2
	      + strlen (solib));
  arch_path = alloca (arch_len);
  xsnprintf (arch_path, arch_len, "%s/%s%s", nto_root, arch, endian);

  len = strlen (PATH_FMT) + strlen (arch_path) * 5 + 1;
  buf = alloca (len);
  xsnprintf (buf, len, PATH_FMT, arch_path, arch_path, arch_path, arch_path,
	     arch_path);

  base = lbasename (solib);
  ret = openp (buf, OPF_TRY_CWD_FIRST | OPF_RETURN_REALPATH, base, o_flags,
	       temp_pathname);
  if (ret < 0 && base != solib)
    {
      xsnprintf (arch_path, arch_len, "/%s", solib);
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

void
nto_init_solib_absolute_prefix (void)
{
  char buf[PATH_MAX * 2], arch_path[PATH_MAX];
  char *nto_root;
  const char *endian;
  const char *arch;

  nto_root = nto_target ();
  if (strcmp (gdbarch_bfd_arch_info (target_gdbarch ())->arch_name, "i386") == 0)
    {
      arch = "x86";
      endian = "";
    }
  else if (strcmp (gdbarch_bfd_arch_info (target_gdbarch ())->arch_name,
		   "rs6000") == 0
	   || strcmp (gdbarch_bfd_arch_info (target_gdbarch ())->arch_name,
		   "powerpc") == 0)
    {
      arch = "ppc";
      endian = "be";
    }
  else
    {
      arch = gdbarch_bfd_arch_info (target_gdbarch ())->arch_name;
      endian = gdbarch_byte_order (target_gdbarch ())
	       == BFD_ENDIAN_BIG ? "be" : "le";
    }

  xsnprintf (arch_path, sizeof (arch_path), "%s/%s%s", nto_root, arch, endian);

  xsnprintf (buf, sizeof (buf), "set solib-absolute-prefix %s", arch_path);
  execute_command (buf, 0);
}

char **
nto_parse_redirection (char *pargv[], const char **pin, const char **pout, 
		       const char **perr)
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
  static struct link_map_offsets lmo32;
  static struct link_map_offsets *lmp32 = NULL;
  static struct link_map_offsets lmo64;
  static struct link_map_offsets *lmp64 = NULL;

  if (lmp32 == NULL)
    {
      lmp32 = &lmo32;

      /* r_debug structure.  */
      lmo32.r_version_offset = 0;
      lmo32.r_version_size = 4;
      lmo32.r_map_offset = 4;
      lmo32.r_brk_offset = 8;
#ifdef EXTENDED_LINK_MAP
      lmo32.r_state_offset = 12;
      lmo32.r_state_size = 4;
      lmo32.r_rdevent_offset = 24;
      lmo32.r_rdevent_size = 4;
#endif
      lmo32.r_ldsomap_offset = -1; /* Our ldd is in libc, we do not want it to
				    show up twice.  */

      /* Link map.  */
      lmo32.link_map_size = 32;	/* The actual size is 552 bytes, but
				   this is all we need.  */
      lmo32.l_addr_offset = 0;

      lmo32.l_name_offset = 4;

      lmo32.l_ld_offset = 8;

      lmo32.l_next_offset = 12;

      lmo32.l_prev_offset = 16;
#ifdef EXTENDED_LINK_MAP
      lmo32.l_path_offset = 28;
#endif
    }
  if (lmp64 == NULL)
    {
      lmp64 = &lmo64;

      /* r_debug structure.  */
      lmo64.r_version_offset = 0;
      lmo64.r_version_size = 4;
      lmo64.r_map_offset = 8;
      lmo64.r_brk_offset = 16;
#ifdef EXTENDED_LINK_MAP
      lmo64.r_state_offset = 24;
      lmo64.r_state_size = 4;
      lmo64.r_rdevent_offset = 48;
      lmo64.r_rdevent_size = 4;
#endif
      lmo64.r_ldsomap_offset = -1; /* Our ldd is in libc, we do not want it to
				    show up twice.  */

      /* Link map.  */
      lmo64.link_map_size = 64;

      lmo64.l_addr_offset = 0;

      lmo64.l_name_offset = 8;

      lmo64.l_ld_offset = 16;

      lmo64.l_next_offset = 24;

      lmo64.l_prev_offset = 32;
#ifdef EXTENDED_LINK_MAP
      lmo64.l_path_offset = 56;
#endif
    }

  if (IS_64BIT())
    return lmp64;
  else
    return lmp32;
}


/* The struct lm_info, lm_addr, and nto_truncate_ptr are copied from
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

    /* The target location of lm.  */
    CORE_ADDR lm_addr;
  };


static CORE_ADDR
lm_addr (struct so_list *so)
{
  if (so->lm_info->l_addr == (CORE_ADDR)-1)
    {
      struct link_map_offsets *lmo =
	nto_generic_svr4_fetch_link_map_offsets ();
      struct type *ptr_type =
	builtin_type (target_gdbarch ())->builtin_data_ptr;

      so->lm_info->l_addr =
	extract_typed_address (so->lm_info->lm + lmo->l_addr_offset, ptr_type);
    }
  return so->lm_info->l_addr;
}

static CORE_ADDR
nto_truncate_ptr (CORE_ADDR addr)
{
  if (gdbarch_ptr_bit (target_gdbarch ()) == sizeof (CORE_ADDR) * 8)
    /* We don't need to truncate anything, and the bit twiddling below
       will fail due to overflow problems.  */
    return addr;
  else
    return addr & (((CORE_ADDR) 1 << gdbarch_ptr_bit (target_gdbarch ())) - 1);
}

static Elf_Internal_Phdr *
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
nto_relocate_section_addresses (struct so_list *so, struct target_section *sec)
{
  /* Neutrino treats the l_addr base address field in link.h as different than
     the base address in the System V ABI and so the offset needs to be
     calculated and applied to relocations.  */
  Elf_Internal_Phdr *phdr = find_load_phdr (sec->the_bfd_section->owner);
  unsigned vaddr = phdr ? phdr->p_vaddr : 0;

  sec->addr = nto_truncate_ptr (sec->addr + lm_addr (so) - vaddr);
  sec->endaddr = nto_truncate_ptr (sec->endaddr + lm_addr (so) - vaddr);
}

/* This is cheating a bit because our linker code is in libc.so.  If we
   ever implement lazy linking, this may need to be re-examined.  */
int
nto_in_dynsym_resolve_code (CORE_ADDR pc)
{
  if (in_plt_section (pc))
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
  /* Buffer holding the section contents.  */
  char *note;
  unsigned int namelen;
  const char *name;
  const unsigned sizeof_Elf_Nhdr = 12;

  sectname = bfd_get_section_name (abfd, sect);
  sectsize = bfd_section_size (abfd, sect);

  if (sectsize > 128)
    sectsize = 128;

  if (sectname != NULL && strstr (sectname, QNX_INFO_SECT_NAME) != NULL)
    *(enum gdb_osabi *) obj = GDB_OSABI_QNXNTO;
  else if (sectname != NULL && strstr (sectname, "note") != NULL
	   && sectsize > sizeof_Elf_Nhdr)
    {
      note = XNEWVEC (char, sectsize);
      bfd_get_section_contents (abfd, sect, note, 0, sectsize);
      namelen = (unsigned int) bfd_h_get_32 (abfd, note);
      name = note + sizeof_Elf_Nhdr;
      if (sectsize >= namelen + sizeof_Elf_Nhdr
	  && namelen == sizeof (QNX_NOTE_NAME)
	  && 0 == strcmp (name, QNX_NOTE_NAME))
        *(enum gdb_osabi *) obj = GDB_OSABI_QNXNTO;

      XDELETEVEC (note);
    }
}

enum gdb_osabi
nto_elf_osabi_sniffer (bfd *abfd)
{
  enum gdb_osabi osabi = GDB_OSABI_UNKNOWN;

  bfd_map_over_sections (abfd,
			 nto_sniff_abi_note_section,
			 &osabi);

  return osabi;
}

static const char *nto_thread_state_str[] =
{
  "DEAD",		/* 0  0x00 */
  "RUNNING",	/* 1  0x01 */
  "READY",	/* 2  0x02 */
  "STOPPED",	/* 3  0x03 */
  "SEND",		/* 4  0x04 */
  "RECEIVE",	/* 5  0x05 */
  "REPLY",	/* 6  0x06 */
  "STACK",	/* 7  0x07 */
  "WAITTHREAD",	/* 8  0x08 */
  "WAITPAGE",	/* 9  0x09 */
  "SIGSUSPEND",	/* 10 0x0a */
  "SIGWAITINFO",	/* 11 0x0b */
  "NANOSLEEP",	/* 12 0x0c */
  "MUTEX",	/* 13 0x0d */
  "CONDVAR",	/* 14 0x0e */
  "JOIN",		/* 15 0x0f */
  "INTR",		/* 16 0x10 */
  "SEM",		/* 17 0x11 */
  "WAITCTX",	/* 18 0x12 */
  "NET_SEND",	/* 19 0x13 */
  "NET_REPLY"	/* 20 0x14 */
};

char *
nto_extra_thread_info (struct target_ops *self, struct thread_info *ti)
{
  if (ti && ti->priv
      && ti->priv->state < ARRAY_SIZE (nto_thread_state_str))
    return (char *)nto_thread_state_str [ti->priv->state];
  return "";
}

void
nto_initialize_signals (void)
{
  /* We use SIG45 for pulses, or something, so nostop, noprint
     and pass them.  */
  signal_stop_update (gdb_signal_from_name ("SIG45"), 0);
  signal_print_update (gdb_signal_from_name ("SIG45"), 0);
  signal_pass_update (gdb_signal_from_name ("SIG45"), 1);

  /* By default we don't want to stop on these two, but we do want to pass.  */
  signal_stop_update (GDB_SIGNAL_SELECT, 0);
  signal_print_update (GDB_SIGNAL_SELECT, 0);
  signal_pass_update (GDB_SIGNAL_SELECT, 1);

#if defined(SIGPHOTON)
  signal_stop_update (SIGPHOTON, 0);
  signal_print_update (SIGPHOTON, 0);
  signal_pass_update (SIGPHOTON, 1);
#endif
}

void
nto_get_siginfo_from_procfs_status (const void *const ps, void *siginfo)
{
  enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch ());
  __siginfo32_t *dst32 = siginfo;
  __siginfo64_t *dst64 = siginfo;
  const nto_procfs_status *status = ps;
  const __siginfo32_t *src32 = &status->_32.info;
  const __siginfo64_t *src64 = &status->_64.info64;
  struct type *ptr_t = builtin_type (target_gdbarch ())->builtin_data_ptr;

  memset (dst64, 0, IS_64BIT() ? sizeof (__siginfo64_t)
			       : sizeof (__siginfo32_t));

  if (IS_64BIT ())
    {
      dst64->si_signo = extract_signed_integer ((gdb_byte *)&src64->si_signo,
						sizeof (src64->si_signo),
						byte_order);
      dst64->si_code = extract_signed_integer ((gdb_byte *)&src64->si_code,
					       sizeof (src64->si_code),
					       byte_order);
      if (dst64->si_code == 127) // SI_NOINFO
	return;

      dst64->si_errno = extract_signed_integer ((gdb_byte *)&src64->si_errno,
						sizeof (src64->si_errno),
						byte_order);

      if (dst64->si_code <= 0) // SI_FROMUSER
	{
	  dst64->nto_si_pid
	    = extract_signed_integer ((gdb_byte *)&src64->nto_si_pid,
				      sizeof (src64->nto_si_pid), byte_order);
	  dst64->nto_si_uid
	    = extract_signed_integer ((gdb_byte *)&src64->nto_si_uid,
				      sizeof (src64->nto_si_uid), byte_order);
	  dst64->nto_si_value.sival_ptr
	    = extract_typed_address ((gdb_byte *)&src64->nto_si_value, ptr_t);
	}
      else if (dst64->si_signo
	       == gdbarch_gdb_signal_to_target (target_gdbarch (),
						GDB_SIGNAL_CHLD))
	{
	  dst64->nto_si_pid
	    = extract_signed_integer ((gdb_byte *)&src64->nto_si_pid,
				      sizeof (src64->nto_si_pid), byte_order);
	  dst64->nto_si_utime
	    = extract_unsigned_integer ((gdb_byte *)&src64->nto_si_utime,
					sizeof (src64->nto_si_utime),
					byte_order);
	  dst64->nto_si_status
	    = extract_signed_integer ((gdb_byte *)&src64->nto_si_status,
				      sizeof (src64->nto_si_status), \
				      byte_order);
	  dst64->nto_si_stime
	    = extract_unsigned_integer ((gdb_byte *)&src64->nto_si_stime,
					sizeof (src64->nto_si_stime),
					byte_order);
	}
      else
	{
	  dst64->nto_si_fltno
	    = extract_signed_integer ((gdb_byte *)&src64->nto_si_fltno,
				      sizeof (src64->nto_si_fltno),
				      byte_order);
	  dst64->nto_si_fltip
	    = extract_typed_address ((gdb_byte *)&src64->nto_si_fltip, ptr_t);
	  dst64->nto_si_addr
	    = extract_typed_address ((gdb_byte *)&src64->nto_si_addr, ptr_t);
	  dst64->nto_si_bdslot
	    = extract_signed_integer ((gdb_byte *)&src64->nto_si_bdslot,
				      sizeof (src64->nto_si_bdslot),
				      byte_order);
	}
    }
  else
    {
      dst32->si_signo = extract_signed_integer ((gdb_byte *)&src32->si_signo,
						sizeof (src32->si_signo),
						byte_order);
      dst32->si_code = extract_signed_integer ((gdb_byte *)&src32->si_code,
					       sizeof (src32->si_code),
					       byte_order);
      if (dst32->si_code == 127) // SI_NOINFO
	return;

      dst32->si_errno = extract_signed_integer ((gdb_byte *)&src32->si_errno,
						sizeof (src32->si_errno),
						byte_order);

      if (dst32->si_code <= 0) // SI_FROMUSER
	{
	  dst32->nto_si_pid
	    = extract_signed_integer ((gdb_byte *)&src32->nto_si_pid,
				      sizeof (src32->nto_si_pid), byte_order);
	  dst32->nto_si_uid
	    = extract_signed_integer ((gdb_byte *)&src32->nto_si_uid,
				      sizeof (src32->nto_si_uid), byte_order);
	  dst32->nto_si_value.sival_ptr
	    = extract_typed_address ((gdb_byte *)&src32->nto_si_value, ptr_t);
	}
      else if (dst32->si_signo
	       == gdbarch_gdb_signal_to_target (target_gdbarch (),
						GDB_SIGNAL_CHLD))
	{
	  dst32->nto_si_pid
	    = extract_signed_integer ((gdb_byte *)&src32->nto_si_pid,
				      sizeof (src32->nto_si_pid), byte_order);
	  dst32->nto_si_utime
	    = extract_unsigned_integer ((gdb_byte *)&src32->nto_si_utime,
					sizeof (src32->nto_si_utime),
					byte_order);
	  dst32->nto_si_status
	    = extract_signed_integer ((gdb_byte *)&src32->nto_si_status,
				      sizeof (src32->nto_si_status),
				      byte_order);
	  dst32->nto_si_stime
	    = extract_unsigned_integer ((gdb_byte *)&src32->nto_si_stime,
					sizeof (src32->nto_si_stime),
					byte_order);
	}
      else
	{
	  dst32->nto_si_fltno
	    = extract_signed_integer ((gdb_byte *)&src32->nto_si_fltno,
				      sizeof (src32->nto_si_fltno),
				      byte_order);
	  dst32->nto_si_fltip
	    = extract_typed_address ((gdb_byte *)&src32->nto_si_fltip, ptr_t);
	  dst32->nto_si_addr
	    = extract_typed_address ((gdb_byte *)&src32->nto_si_addr, ptr_t);
	  dst32->nto_si_bdslot
	    = extract_signed_integer ((gdb_byte *)&src32->nto_si_bdslot,
				      sizeof (src32->nto_si_bdslot),
				      byte_order);
	}
    }
}

/* Read AUXV from initial_stack.  */
LONGEST
nto_read_auxv_from_initial_stack (CORE_ADDR initial_stack, gdb_byte *readbuf,
                                  LONGEST len, size_t sizeof_auxv_t)
{
  gdb_byte targ32[4]; /* For 32 bit target values.  */
  gdb_byte targ64[8]; /* For 64 bit target values.  */
  CORE_ADDR data_ofs = 0;
  ULONGEST anint;
  LONGEST len_read = 0;
  gdb_byte *buff;
  enum bfd_endian byte_order;
  int ptr_size;

  if (sizeof_auxv_t == 16)
    ptr_size = 8;
  else
    ptr_size = 4;

  /* Skip over argc, argv and envp... Comment from ldd.c:

     The startup frame is set-up so that we have:
     auxv
     NULL
     ...
     envp2
     envp1 <----- void *frame + (argc + 2) * sizeof(char *)
     NULL
     ...
     argv2
     argv1
     argc  <------ void * frame

     On entry to ldd, frame gives the address of argc on the stack.  */
  /* Read argc. 4 bytes on both 64 and 32 bit arches and luckily little
   * endian. So we just read first 4 bytes.  */
  if (target_read_memory (initial_stack + data_ofs, targ32, 4) != 0)
    return 0;

  byte_order = gdbarch_byte_order (target_gdbarch ());

  anint = extract_unsigned_integer (targ32, sizeof (targ32), byte_order);

  /* Size of pointer is assumed to be 4 bytes (32 bit arch.) */
  data_ofs += (anint + 2) * ptr_size; /* + 2 comes from argc itself and
                                                NULL terminating pointer in
                                                argv.  */

  /* Now loop over env table:  */
  anint = 0;
  while (target_read_memory (initial_stack + data_ofs, targ64, ptr_size)
         == 0)
    {
      if (extract_unsigned_integer (targ64, ptr_size, byte_order) == 0)
	anint = 1; /* Keep looping until non-null entry is found.  */
      else if (anint)
	break;
      data_ofs += ptr_size;
    }
  initial_stack += data_ofs;

  memset (readbuf, 0, len);
  buff = readbuf;
  while (len_read <= len-sizeof_auxv_t)
    {
      if (target_read_memory (initial_stack + len_read, buff, sizeof_auxv_t)
	  == 0)
        {
	  /* Both 32 and 64 bit structures have int as the first field.  */
          const ULONGEST a_type
	    = extract_unsigned_integer (buff, sizeof (targ32), byte_order);

          if (a_type == AT_NULL)
	    break;
	  buff += sizeof_auxv_t;
	  len_read += sizeof_auxv_t;
        }
      else
        break;
    }
  return len_read;
}

/* Allocate new nto_inferior_data object.  */

static struct nto_inferior_data *
nto_new_inferior_data (void)
{
  struct nto_inferior_data *const inf_data
    = XCNEW (struct nto_inferior_data);

  return inf_data;
}

/* Free inferior data.  */

static void
nto_inferior_data_cleanup (struct inferior *const inf, void *const dat)
{
  xfree (dat);
}

/* Return nto_inferior_data for the given INFERIOR.  If not yet created,
   construct it.  */

struct nto_inferior_data *
nto_inferior_data (struct inferior *const inferior)
{
  struct inferior *const inf = inferior ? inferior : current_inferior ();
  struct nto_inferior_data *inf_data;

  gdb_assert (inf != NULL);

  inf_data = inferior_data (inf, nto_inferior_data_reg);
  if (inf_data == NULL)
    {
      set_inferior_data (inf, nto_inferior_data_reg,
			 (inf_data = nto_new_inferior_data ()));
    }

  return inf_data;
}

/* Provide a prototype to silence -Wmissing-prototypes.  */
extern initialize_file_ftype _initialize_nto_tdep;

struct gdbarch_data *nto_gdbarch_ops;

static void *
nto_gdbarch_init (struct obstack *obstack)
{
  struct nto_target_ops *ops;

  ops = OBSTACK_ZALLOC (obstack, struct nto_target_ops);
  return ops;
}

void
_initialize_nto_tdep (void)
{
  nto_gdbarch_ops = gdbarch_data_register_pre_init (nto_gdbarch_init);

  nto_inferior_data_reg
    = register_inferior_data_with_cleanup (NULL, nto_inferior_data_cleanup);

  nto_gdbarch_data_handle =
    gdbarch_data_register_post_init (init_nto_gdbarch_data);
}

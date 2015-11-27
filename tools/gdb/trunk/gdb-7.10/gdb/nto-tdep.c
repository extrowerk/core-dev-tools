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
#include "gdb/signals.h"

#include "gdbcmd.h"
#include "safe-ctype.h"
#include "gdb_assert.h"

#include "observer.h"

#include "gdbtypes.h"

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
int nto_internal_debugging;
int nto_stop_on_thread_events;

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
  if (!strcmp (arch, "x86_64"))
    return CPUTYPE_X86_64;
  if (!strcmp (arch, "???")) // FIXME: AARCH64
    return CPUTYPE_AARCH64;
  return CPUTYPE_UNKNOWN;
}

/* Helper function, calculates architecture path, e.g.
   /opt/qnx640/target/qnx6/ppcbe
   It allocates string, callers must free the string using free.  */

static char *
nto_build_arch_path (void)
{
  const char *nto_root, *arch, *endian;
  char *arch_path;
  const char *variant_suffix = "";

  nto_root = nto_target ();
  if (strcmp (gdbarch_bfd_arch_info (target_gdbarch ())->arch_name, "i386") == 0)
    {
      if (IS_64BIT())
	arch = "x86_64";
      else
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

  if (nto_variant_directory_suffix)
    variant_suffix = nto_variant_directory_suffix ();

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
		 "%s/usr/photon/dll%c%s/lib/dll%c%s"

  arch_path = nto_build_arch_path ();
  buf = alloca (strlen (PATH_FMT) + strlen (arch_path) * 6 + 1);
  sprintf (buf, PATH_FMT, arch_path, DIRNAME_SEPARATOR,
	   arch_path, DIRNAME_SEPARATOR, arch_path, DIRNAME_SEPARATOR,
	   arch_path, DIRNAME_SEPARATOR, arch_path, DIRNAME_SEPARATOR,
	   arch_path);
  free (arch_path);

  ret = openp (buf, OPF_TRY_CWD_FIRST | OPF_SEARCH_IN_PATH, solib, o_flags, temp_pathname);
  if (ret < 0)
    {
      /* Don't assume basename() isn't destructive.  */
      base = strrchr (solib, '/');
      if (!base)
	base = solib;
      else
	base++;			/* Skip over '/'.  */
      if (base != solib)
	ret = openp (buf, OPF_TRY_CWD_FIRST | OPF_SEARCH_IN_PATH, base, o_flags, temp_pathname);
    }
  return ret;
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

static Elf_Internal_Phdr *
find_load_phdr_2 (bfd *abfd, unsigned int p_filesz,
		  unsigned int p_memsz, unsigned int p_flags,
		  unsigned int p_align)
{
  Elf_Internal_Phdr *phdr;
  unsigned int i;

  if (!abfd || !elf_tdata (abfd))
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
nto_relocate_section_addresses (struct so_list *so, struct target_section *sec)
{
  /* Neutrino treats the l_addr base address field in link.h as different than
     the base address in the System V ABI and so the offset needs to be
     calculated and applied to relocations.  */
  Elf_Internal_Phdr *phdr = find_load_phdr (sec->the_bfd_section->owner);
  unsigned vaddr = phdr ? phdr->p_vaddr : 0;

  sec->addr = nto_truncate_ptr (sec->addr
			        + lm_addr_check (so, sec->the_bfd_section->owner)
				- vaddr);
  sec->endaddr = nto_truncate_ptr (sec->endaddr
				   + lm_addr_check (so, sec->the_bfd_section->owner)
				   - vaddr);
  if (so->addr_low == 0)
    so->addr_low = lm_addr_check (so, sec->the_bfd_section->owner);
  if (so->addr_high < sec->endaddr)
    so->addr_high = sec->endaddr;

  /* Still can determine low. */
  if (so->addr_low == 0) {
    so->addr_low = lm_addr_check (so, sec->the_bfd_section->owner); /* Load base */
    so->addr_high = so->addr_low; /* at a minimum */
  }
}

/* This is cheating a bit because our linker code is in libc.so.  If we
   ever implement lazy linking, this may need to be re-examined.  */
int
nto_in_dynsym_resolve_code (CORE_ADDR pc)
{
  struct nto_inferior_data *inf_data;
  struct inferior *inf;
  int in_resolv = 0;

  inf = current_inferior ();
  inf_data = nto_inferior_data (inf);

  if (inf_data->bind_func_p != 0)
    {
      const size_t bind_func_sz = inf_data->bind_func_sz ?
				  inf_data->bind_func_sz : 80;
      if (inf_data->bind_func_addr != 0)
	in_resolv = (pc >= inf_data->bind_func_addr
		     && pc < (inf_data->bind_func_addr + bind_func_sz));
      if (!in_resolv && inf_data->resolve_func_addr != 0)
	in_resolv = (pc == inf_data->resolve_func_addr);
    }

  if (in_resolv || in_plt_section (pc))
    return 1;
  return 0;
}

void
nto_dummy_supply_regset (struct regcache *regcache, const gdb_byte *regs)
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


static CORE_ADDR
nto_ldqnx2_skip_solib_resolver (struct gdbarch *gdbarch, CORE_ADDR pc)
{
  struct nto_inferior_data *const inf_data
    = nto_inferior_data (current_inferior ());

  // TODO: Proper cleanup of inf. data 

  if (inf_data->bind_func_p == 0)
    {
      /* On Neutrino with libc 6.5.0 and later, lazy binding is performed
       * by a function called */
      struct objfile *objfile;
      const struct bound_minimal_symbol resolver
	= lookup_minimal_symbol_and_objfile ("__resolve_func");

      if (resolver.minsym && resolver.objfile)
	{
	  const struct bound_minimal_symbol bind_f
	    = lookup_minimal_symbol ("__bind_func", NULL, resolver.objfile);

	  if (bind_f.minsym)
	    {
	      inf_data->bind_func_p = 1;
	      inf_data->bind_func_addr = BMSYMBOL_VALUE_ADDRESS (bind_f);
	      inf_data->bind_func_sz = MSYMBOL_SIZE (bind_f.minsym);
	      inf_data->resolve_func_addr = BMSYMBOL_VALUE_ADDRESS (resolver);
	    }
	}
    }

  if (inf_data->bind_func_p)
    {
      if (inf_data->resolve_func_addr == pc)
	return frame_unwind_caller_pc (get_current_frame ());
    }

  return 0;
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

      if (tp->priv)
	{
	  tid = tp->priv->tid;
	  state = tp->priv->state;
	  flags = tp->priv->flags;
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

  target_update_thread_list ();
  printf_filtered("Threads for pid %d (%s)\nTid:\tState:\tFlags:\n",
		  ptid_get_pid (inferior_ptid), execfile ? execfile : "");

  iterate_over_threads (nto_print_tidinfo_callback, NULL);
}


char *
nto_pid_to_str (struct target_ops *ops, ptid_t ptid)
{
  static char buf[1024];
  int pid, tid, n;
  char thread_id[50];
  char thread_name[17];
  struct thread_info *ti;

  pid = ptid_get_pid (ptid);
  tid = ptid_get_tid (ptid);

  ti = find_thread_ptid (ptid);
  if (ti && ti->priv && ti->priv->name[0])
    {
      int n;

      n = snprintf (thread_name, ARRAY_SIZE (thread_name), "%s",
		    ti->priv->name);
      if (n >= ARRAY_SIZE (thread_name))
	/* Name did not fit, append ellipses.  */
	snprintf (&thread_name [ARRAY_SIZE (thread_name) - 4], 4, "%s",
		  "...");
      snprintf (thread_id, ARRAY_SIZE (thread_id), " tid %d name \"%s\"",
		tid, thread_name);
    }
  else if (tid > 0)
    snprintf (thread_id, ARRAY_SIZE (thread_id), " tid %d", tid);
  else
    thread_id[0] = '\0';

  n = sprintf (buf, "pid %d%s", pid, thread_id);

  return buf;
}

char *nto_gdbarch_core_pid_to_str (struct gdbarch *gdbarch, ptid_t ptid)
{
    return nto_pid_to_str (NULL, ptid);
}

int
qnx_filename_cmp (const char *s1, const char *s2, size_t n)
{
  gdb_assert (s1 != NULL);
  gdb_assert (s2 != NULL);
  gdb_assert (n >= 0);

  nto_trace (3) ("%s(%s,%s)\n", __func__, s1, s2);

  if (0 == strncmp (s1, s2, n))
    return 0;

  for (; n > 0; --n)
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
  return 0;
}

/* Add thread status for the given gdb_thread_id.  */

static void
nto_private_thread_info_dtor (struct private_thread_info *priv)
{
  if (priv)
    xfree (priv->siginfo);
  xfree (priv);
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

static void
nto_core_add_thread_status_info (pid_t core_pid, int gdb_thread_id,
				 const nto_procfs_status *ps)
{
  struct thread_info *ti;
  ptid_t ptid;
  struct private_thread_info *priv;
  enum bfd_endian byte_order;

  byte_order = gdbarch_byte_order (target_gdbarch ());
 
  /* See corelow, function add_to_thread_list for details on pid.  */
  ptid = ptid_build (core_pid, 0, gdb_thread_id);
  ti = find_thread_ptid (ptid);
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
  ti->priv = priv;
  ti->private_dtor = nto_private_thread_info_dtor;
  if (IS_64BIT ())
    {
      priv->tid
	= extract_unsigned_integer ((gdb_byte *)&ps->_64.tid,
				    sizeof (ps->_64.tid),
				    byte_order);
      priv->state
	= extract_unsigned_integer ((gdb_byte *)&ps->_64.state,
				    sizeof (ps->_64.state),
				    byte_order);
      priv->flags
	= extract_unsigned_integer ((gdb_byte *)&ps->_64.flags,
				    sizeof (ps->_64.flags),
				    byte_order);
      priv->siginfo = malloc (sizeof (ps->_64.info64));
    }
  else
    {
      /* 32 bit */
      priv->tid
	= extract_unsigned_integer ((gdb_byte *)&ps->_32.tid,
				    sizeof (ps->_32.tid),
				    byte_order);
      priv->state
	= extract_unsigned_integer ((gdb_byte *)&ps->_32.state,
				    sizeof (ps->_32.state),
				    byte_order);
      priv->flags
	= extract_unsigned_integer ((gdb_byte *)&ps->_32.flags,
				    sizeof (ps->_32.flags),
				    byte_order);
      priv->siginfo = malloc (sizeof (ps->_32.info));
    }
  if (priv->siginfo == NULL)
    {
      warning ("Out of memory.\n");
      return;
    }
  nto_get_siginfo_from_procfs_status (ps, priv->siginfo);
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
  nto_procfs_status status;
  int len;
  const int is_elf64 = IS_64BIT();

  sectname = bfd_get_section_name (abfd, sect);
  sectsize = bfd_section_size (abfd, sect);
  if (sectsize > (is_elf64? sizeof (status._64): sizeof (status._32)))
    sectsize = (is_elf64? sizeof (status._64) : sizeof (status._32));

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
  nto_core_add_thread_status_info (elf_tdata (abfd)->core->pid, gdb_thread_id,
				   &status);
}
static void (*original_core_open) (char *, int);
static void (*original_core_close) (int);

/* When opening a core, we do not want to set inferior hooks.  */
static struct target_so_ops backup_so_ops;

struct target_ops nto_core_ops;

struct auxv_buf
{
  LONGEST len;
  LONGEST len_read; /* For passing result. Can be len, 0, or -1  */
  gdb_byte *readbuf;
};

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
  nto_procfs_info info;
  int len;
  gdb_byte *buff; /* For skipping over argc, argv and envp-s */
  CORE_ADDR initial_stack, base_address;
  enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch ());

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
  initial_stack = extract_unsigned_integer
    ((gdb_byte *)&info.initial_stack, sizeof (info.initial_stack), byte_order);
  base_address = extract_unsigned_integer
    ((gdb_byte *)&info.base_address, sizeof (info.base_address), byte_order);
  buff = auxv_buf->readbuf;

  auxv_buf->len_read = nto_read_auxv_from_initial_stack
    (initial_stack, auxv_buf->readbuf, auxv_buf->len, IS_64BIT()? 16 : 8);

}

static enum target_xfer_status
nto_core_xfer_partial (struct target_ops *ops, enum target_object object,
		       const char *annex, gdb_byte *readbuf,
		       const gdb_byte *writebuf, ULONGEST offset, ULONGEST len,
		       ULONGEST *xfered_len)
{
  if (object == TARGET_OBJECT_AUXV
      && readbuf)
    {
      struct auxv_buf auxv_buf;
      ssize_t tempread;

      auxv_buf.len = len;
      auxv_buf.len_read = 0;
      auxv_buf.readbuf = readbuf;

      bfd_map_over_sections (core_bfd, nto_core_read_auxv_from_note, &auxv_buf);

      tempread = min (len, auxv_buf.len_read) - offset;

      if (tempread < 0)
	return TARGET_XFER_E_IO;

      if (offset)
	memmove (auxv_buf.readbuf, &auxv_buf.readbuf[offset], tempread);

      *xfered_len = tempread;
      return (tempread ? TARGET_XFER_OK : TARGET_XFER_EOF);
    }
  else if (object == TARGET_OBJECT_SIGNAL_INFO
	   && readbuf)
    {
      struct thread_info *ti;

      ti = find_thread_ptid (inferior_ptid);
      if (!ti)
        {
	  warning ("Thread with gdb id %ld not found.\n", ptid_get_tid (inferior_ptid));
	  return 0;
        }
      if (!ti->priv)
	{
	  warning (_("Thread with gdb id %ld does not have thread private data - siginfo not available\n"),
		   ptid_get_tid (inferior_ptid));
	  return 0;
	}
      if ((offset + len) > sizeof (nto_siginfo_t))
	{
	  if (offset <= sizeof (nto_siginfo_t))
	    len = sizeof (nto_siginfo_t) - offset;
	  else
	    len = 0;
	}

      memcpy (readbuf, (char *)ti->priv->siginfo + offset, len);
      *xfered_len = len;
      return len ? TARGET_XFER_OK : TARGET_XFER_EOF;
    }


  /* In any other case, try default code.  */
  return nto_core_ops.to_xfer_partial (ops, object, annex, readbuf,
					    writebuf, offset, len, xfered_len);
}

static void
nto_core_open (char *filename, int from_tty)
{
  /* Backup target_so_ops.  */
  backup_so_ops = *current_target_so_ops;

  nto_trace (0) ("%s (%s)\n", __func__, filename);
  nto_core_ops.to_open (filename, from_tty);
  /* Now we need to load additional thread status information stored
     in qnx notes.  */
  if (core_bfd)
    bfd_map_over_sections (core_bfd, nto_core_add_thread_private_data, NULL);
}
/* FIXME */
static void
nto_core_close (struct target_ops *ops)
{
  nto_core_ops.to_close (ops);
}

extern struct target_ops *core_target;
static void
init_nto_core_ops (void)
{
  gdb_assert (core_target != NULL && core_target->to_shortname != NULL 
	      && !!"core_ops must be initialized first!");
  nto_core_ops = *core_target;
  core_target->to_extra_thread_info = nto_extra_thread_info;
  core_target->to_open = nto_core_open;
  core_target->to_close = nto_core_close;
  core_target->to_xfer_partial = nto_core_xfer_partial;
  core_target->to_pid_to_str = nto_pid_to_str;
}

int
nto_stopped_by_watchpoint (struct target_ops *const ops)
{
  /* NOTE: nto_stopped_by_watchpoint will be called ONLY while we are 
     stopped due to a SIGTRAP.  This assumes gdb works in 'all-stop' mode;
     future gdb versions will likely run in 'non-stop' mode in which case 
     we will have to store/examine statuses per thread in question.  
     Until then, this will work fine.  */

  struct inferior *inf = current_inferior ();
  struct nto_inferior_data *inf_data;

  gdb_assert (inf != NULL);

  inf_data = nto_inferior_data (inf);

  return inf_data->stopped_flags
	 & (_DEBUG_FLAG_TRACE_RD
	    | _DEBUG_FLAG_TRACE_WR
	    | _DEBUG_FLAG_TRACE_MODIFY);
}

static void
nto_solib_added_listener (struct so_list *solib)
{
  /* Check if the libraries match.
     We compare all PT_LOAD segments.  */
  CORE_ADDR mem_phdr_addr;
  CORE_ADDR phdr_offs_addr;
  gdb_byte offs_buf[8];
  enum bfd_endian byte_order;

  unsigned int offsetof_e_phoff;
  unsigned int sizeof_e_phoff;
  unsigned int sizeof_Elf_Phdr;
  const unsigned int sizeof_Elf_Word = 4;
  const unsigned int offsetof_p_type = 0;
  unsigned int offsetof_p_filesz;
  unsigned int offsetof_p_memsz;
  unsigned int offsetof_p_flags;
  unsigned int offsetof_p_align;
  const unsigned int sizeof_p_type = 4;
  const unsigned sizeof_p_flags = 4;
  unsigned int sizeof_p_filesz;
  unsigned int sizeof_p_memsz;
  unsigned int sizeof_p_align;

  switch (gdbarch_bfd_arch_info (target_gdbarch ())->bits_per_word)
    {
    case 64:
      offsetof_e_phoff = 32;
      sizeof_e_phoff = 8;
      sizeof_Elf_Phdr = 56;
      offsetof_p_filesz = 32;
      offsetof_p_memsz = 40;
      offsetof_p_flags = 4;
      offsetof_p_align = 48;
      sizeof_p_filesz = 8;
      sizeof_p_memsz = 8;
      sizeof_p_align = 8;
      break;
    case 32:
      offsetof_e_phoff = 28;
      sizeof_e_phoff = 4;
      sizeof_Elf_Phdr = 32;
      offsetof_p_filesz = 16;
      offsetof_p_memsz = 20;
      offsetof_p_flags = 24;
      offsetof_p_align = 28;
      sizeof_p_filesz = sizeof_p_memsz = sizeof_p_align = 4;
      break;
    default:
      gdb_assert (!"Unsupported number of bits per word");
    }

  phdr_offs_addr = solib->addr_low + offsetof_e_phoff;
  byte_order = gdbarch_byte_order (target_gdbarch ());

  if (target_read_memory (phdr_offs_addr, offs_buf, sizeof_e_phoff))
    {
      nto_trace (0) ("Could not read memory.\n");
      return;
    }

  mem_phdr_addr =
      solib->addr_low
      + extract_typed_address (offs_buf,
			       builtin_type (target_gdbarch ())->builtin_data_ptr);

  while (1)
    {
      gdb_byte phdr_buf[sizeof_Elf_Phdr];
      /* We compare phdr fields: p_type, p_flags, p_aign, p_filesz, p_memsz */
      unsigned int p_type;
      unsigned int p_filesz;
      unsigned int p_memsz;
      unsigned int p_flags;
      unsigned int p_align;
      Elf_Internal_Phdr *file_phdr;

      if (target_read_memory (mem_phdr_addr, phdr_buf, sizeof_Elf_Phdr))
	{
	  nto_trace (0) ("Could not read phdr\n");
	  return;
	}

      p_type = extract_unsigned_integer (&phdr_buf[offsetof_p_type],
					 sizeof_p_type, byte_order);
      if (p_type == PT_LOAD)
	{
	  p_filesz = extract_unsigned_integer (&phdr_buf[offsetof_p_filesz],
					       sizeof_p_filesz, byte_order);
	  p_memsz = extract_unsigned_integer (&phdr_buf[offsetof_p_memsz],
					      sizeof_p_memsz, byte_order);
	  p_flags = extract_unsigned_integer (&phdr_buf[offsetof_p_flags],
					      sizeof_p_flags, byte_order);
	  p_align = extract_unsigned_integer (&phdr_buf[offsetof_p_align],
					      sizeof_p_align, byte_order);

	  if (solib->symbols_loaded)
	    {
	      file_phdr = find_load_phdr_2 (solib->abfd,
					    p_filesz, p_memsz, p_flags, p_align);
	      if (file_phdr == NULL)
		{
		  /* This warning is being parsed by the IDE, the
		   * format should not change without consultations with
		   * IDE team.  */
		  warning ("Host file %s does not match target file %s.",
			   solib->so_name, solib->so_original_name);
		  break;
		}
	    }
	}

      if (p_type == PT_NULL)
	break;

      mem_phdr_addr += sizeof_Elf_Phdr;
    }
}

const struct target_desc *
nto_read_description (struct target_ops *ops)
{
  if (ntoops_read_description)
    return ntoops_read_description (ops);
  else
    return NULL;
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

int
nto_gdb_signal_to_target (struct gdbarch *gdbarch, enum gdb_signal signal)
{
  switch (signal)
    {
    case GDB_SIGNAL_0:
      return 0;
    case GDB_SIGNAL_HUP:
      return 1; /* SIGHUP */
    case GDB_SIGNAL_INT:
      return 2;   /* interrupt */
    case GDB_SIGNAL_QUIT:
      return 3;   /* quit */
    case GDB_SIGNAL_ILL:
      return 4;   /* illegal instruction (not reset when caught) */
    case GDB_SIGNAL_TRAP:
      return 5;   /* trace trap (not reset when caught) */
    case GDB_SIGNAL_ABRT:
      return 6;   /* used by abort */
    case GDB_SIGNAL_EMT:
      return 7;   /* EMT instruction */
    case GDB_SIGNAL_FPE:
      return 8;   /* floating point exception */
    case GDB_SIGNAL_KILL:
      return 9;   /* kill (cannot be caught or ignored) */
    case GDB_SIGNAL_BUS:
      return 10;  /* bus error */
    case GDB_SIGNAL_SEGV:
      return 11;  /* segmentation violation */
    case GDB_SIGNAL_SYS:
      return 12;  /* bad argument to system call */
    case GDB_SIGNAL_PIPE:
      return 13;  /* write on pipe with no reader */
    case GDB_SIGNAL_ALRM:
      return 14;  /* real-time alarm clock */
    case GDB_SIGNAL_TERM:
      return 15;  /* software termination signal from kill */
    case GDB_SIGNAL_USR1:
      return 16;  /* user defined signal 1 */
    case GDB_SIGNAL_USR2:
      return 17;  /* user defined signal 2 */
    case GDB_SIGNAL_CHLD:
      return 18;  /* death of child */
    case GDB_SIGNAL_PWR:
      return 19;  /* power-fail restart */
    case GDB_SIGNAL_WINCH:
      return 20;  /* window change */
    case GDB_SIGNAL_URG:
      return 21;  /* urgent condition on I/O channel */
    case GDB_SIGNAL_POLL:
    case GDB_SIGNAL_IO:
      return 22;  /* System V name for SIGIO */
    case GDB_SIGNAL_STOP:
      return 23;  /* sendable stop signal not from tty */
    case GDB_SIGNAL_TSTP:
      return 24;  /* stop signal from tty */
    case GDB_SIGNAL_CONT:
      return 25;  /* continue a stopped process */
    case GDB_SIGNAL_TTIN:
      return 26;  /* attempted background tty read */
    case GDB_SIGNAL_TTOU:
      return 27;  /* attempted background tty write */
    case GDB_SIGNAL_VTALRM:
      return 28;  /* virtual timer expired */
    case GDB_SIGNAL_PROF:
      return 29;  /* profileing timer expired */
    case GDB_SIGNAL_XCPU:
      return 30;  /* exceded cpu limit */
    case GDB_SIGNAL_XFSZ:
      return 31;  /* exceded file size limit */
    case GDB_SIGNAL_SELECT:
      return 57;
    default:
      return 0;
    }
}

enum gdb_signal
nto_gdb_signal_from_target (struct gdbarch *gdbarch, int nto_signal)
{
  int i;
  for (i = GDB_SIGNAL_0; i < GDB_SIGNAL_LAST; ++i)
    {
      enum gdb_signal temp = nto_gdb_signal_to_target (gdbarch, i);
      if (temp != 0)
	return i;
    }
  return GDB_SIGNAL_UNKNOWN;
}

void
_initialize_nto_tdep (void)
{
  nto_gdbarch_ops = gdbarch_data_register_pre_init (nto_gdbarch_init);
  foo_initialize_corelow ();
  init_nto_core_ops ();

  nto_trace (0) ("%s ()\n", __func__);

  nto_inferior_data_reg
    = register_inferior_data_with_cleanup (NULL, nto_inferior_data_cleanup);

  add_setshow_zinteger_cmd ("nto-debug", class_maintenance,
			    &nto_internal_debugging, _("\
Set QNX NTO internal debugging."), _("\
Show QNX NTO internal debugging."), _("\
When non-zero, nto specific debug info is\n\
displayed. Different information is displayed\n\
for different positive values."),
			    NULL,
			    &show_nto_debug, /* FIXME: i18n: QNX NTO internal
				     debugging is %s.  */
			    &setdebuglist, &showdebuglist);

  add_setshow_zinteger_cmd ("nto-stop-on-thread-events", class_support,
			    &nto_stop_on_thread_events, _("\
Stop on thread events ."), _("\
Show stop on thread events setting."), _("\
When set to 1, stop on thread created and thread destroyed events.\n"),
			    NULL,
			    NULL,
			    &setlist, &showlist);

  add_info ("tidinfo", nto_info_tidinfo_command, "List threads for current process." );
  //nto_fetch_link_map_offsets = nto_generic_svr4_fetch_link_map_offsets;
  //nto_is_nto_target = nto_elf_osabi_sniffer;

  observer_attach_solib_loaded (nto_solib_added_listener);

  nto_gdbarch_data_handle =
    gdbarch_data_register_post_init (init_nto_gdbarch_data);
}

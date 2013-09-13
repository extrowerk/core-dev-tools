/* nto-tdep.h - QNX Neutrino target header.

   Copyright (C) 2003, 2007-2012 Free Software Foundation, Inc.

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

#ifndef _NTO_TDEP_H
#define _NTO_TDEP_H

#include "solist.h"
#include "osabi.h"
#include "regset.h"
#include "gdbthread.h"

/* Target operations defined for Neutrino targets (<target>-nto-tdep.c).  */

struct nto_target_ops
{
/* For 'maintenance debug nto-debug' command.  */
  int internal_debugging;

  /* For stop-on-thread-events */
  int stop_on_thread_events;

/* The CPUINFO flags from the remote.  Currently used by
   i386 for fxsave but future proofing other hosts.
   This is initialized in procfs_attach or nto_start_remote
   depending on our host/target.  It would only be invalid
   if we were talking to an older pdebug which didn't support
   the cpuinfo message.  */
  unsigned cpuinfo_flags;

/* True if successfully retrieved cpuinfo from remote.  */
  int cpuinfo_valid;

/* Given a register, return an id that represents the Neutrino
   regset it came from.  If reg == -1 update all regsets.  */
  int (*regset_id) (int);

  void (*supply_gregset) (struct regcache *, const gdb_byte *);

  void (*supply_fpregset) (struct regcache *, const gdb_byte *);

  void (*supply_altregset) (struct regcache *, const gdb_byte *);

/* Given a regset, tell gdb about registers stored in data.  */
  void (*supply_regset) (struct regcache *, int, const gdb_byte *);

/* Given a register and regset, calculate the offset into the regset
   and stuff it into the last argument.  If regno is -1, calculate the
   size of the entire regset.  Returns length of data, -1 if unknown
   regset, 0 if unknown register.  */
  int (*register_area) (struct gdbarch *, int, int, unsigned *);

/* Build the Neutrino register set info into the data buffer.
   Return -1 if unknown regset, 0 otherwise.  */
  int (*regset_fill) (const struct regcache *, int, gdb_byte *);

/* Gives the fetch_link_map_offsets function exposure outside of
   solib-svr4.c so that we can override relocate_section_addresses().  */
  struct link_map_offsets *(*fetch_link_map_offsets) (void);

/* Used by nto_elf_osabi_sniffer to determine if we're connected to an
   Neutrino target.  */
  enum gdb_osabi (*is_nto_target) (bfd *abfd);

  /* Variant specific directory extension. e.g. -spe, -v7... */
  const char *(*variant_directory_suffix)(void);

  /* Read description. */
  const struct target_desc *(*read_description) (struct target_ops *ops);
};

extern struct nto_target_ops current_nto_target;

#define nto_internal_debugging (current_nto_target.internal_debugging)

#define nto_stop_on_thread_events (current_nto_target.stop_on_thread_events)

#define nto_cpuinfo_flags (current_nto_target.cpuinfo_flags)

#define nto_cpuinfo_valid (current_nto_target.cpuinfo_valid)

#define nto_regset_id (current_nto_target.regset_id)

#define nto_supply_gregset (current_nto_target.supply_gregset)

#define nto_supply_fpregset (current_nto_target.supply_fpregset)

#define nto_supply_altregset (current_nto_target.supply_altregset)

#define nto_supply_regset (current_nto_target.supply_regset)

#define nto_register_area (current_nto_target.register_area)

#define nto_regset_fill (current_nto_target.regset_fill)

#define nto_fetch_link_map_offsets \
(current_nto_target.fetch_link_map_offsets)

#define nto_is_nto_target (current_nto_target.is_nto_target)

#define nto_variant_directory_suffix (current_nto_target.variant_directory_suffix)

#define ntoops_read_description (current_nto_target.read_description)

#define nto_trace(level) \
  if ((nto_internal_debugging & 0xFF) <= (level)) {} else \
    printf_unfiltered ("nto: "); \
  if ((nto_internal_debugging & 0xFF) <= (level)) {} else \
    printf_unfiltered

/* register supply helper macros*/
#define NTO_ALL_REGS (-1)
#define RAW_SUPPLY_IF_NEEDED(regcache, whichreg, dataptr) \
  {if (!(NTO_ALL_REGS == regno || regno == (whichreg))) {} \
    else regcache_raw_supply (regcache, whichreg, dataptr); }

/* Keep this consistant with neutrino syspage.h.  */
enum
{
  CPUTYPE_X86,
  CPUTYPE_PPC,
  CPUTYPE_MIPS,
  CPUTYPE_SPARE,
  CPUTYPE_ARM,
  CPUTYPE_SH,
  CPUTYPE_UNKNOWN
};

enum
{
  OSTYPE_QNX4,
  OSTYPE_NTO
};

/* These correspond to the DSMSG_* versions in dsmsgs.h.  */
enum
{
  NTO_REG_GENERAL,
  NTO_REG_FLOAT,
  NTO_REG_SYSTEM,
  NTO_REG_ALT,
  NTO_REG_END
};

typedef char qnx_reg64[8];

typedef struct _debug_regs
{
  qnx_reg64 padding[1024];
} nto_regset_t;

struct private_thread_info
{
  short tid;
  unsigned char state;
  unsigned char flags;
  CORE_ADDR starting_ip;
  void *siginfo; // cached from core file read
  char name[1];
};


/* Per-inferior data, common for both procfs and remote.  */
struct nto_inferior_data
{
  /* Is program loaded? */
  int has_memory;

  /* Does target has stack available? */
  int has_stack;

  /* Is it being executed? */
  int has_execution;

  /* Does it have registers? */
  int has_registers;

  /* Last stopped flags result from wait function */
  unsigned int stopped_flags;

  /* Last known stopped PC */
  CORE_ADDR stopped_pc;

  /* In case of a fork, remember child pid. */
  int child_pid;

  /* In case of a fork, is it a vfork? */
  int vfork;

  /* bind_func address needed to determine if we are in
   * dynsym code */
  CORE_ADDR bind_func_addr;

  /* Size of __bind_func symbol */
  size_t bind_func_sz;

  /* Similar to bind_func, we want to look it up only once */
  CORE_ADDR resolve_func_addr;

  /* To avoid repeatedly looking up symbols, mark here
   * that the lookup has been done.  If it is done,
   * then bind_func_ptr will not be re-calculated,
   * even if it is still zero (meaning original attempt
   * failed).
   */
  int bind_func_p;
};


/* Generic functions in nto-tdep.c.  */

void nto_init_solib_absolute_prefix (void);

char **nto_parse_redirection (char *start_argv[], const char **in,
			      const char **out, const char **err);

void nto_relocate_section_addresses (struct so_list *,
				     struct target_section *);

int nto_map_arch_to_cputype (const char *);

int nto_find_and_open_solib (char *, unsigned, char **);

enum gdb_osabi nto_elf_osabi_sniffer (bfd *abfd);

void nto_initialize_signals (struct gdbarch *gdbarch);

/* Dummy function for initializing nto_target_ops on targets which do
   not define a particular regset.  */
void nto_dummy_supply_regset (struct regcache *regcache, const gdb_byte *regs);

int nto_in_dynsym_resolve_code (CORE_ADDR pc);

char *nto_extra_thread_info (struct thread_info *);

struct link_map_offsets* nto_generic_svr4_fetch_link_map_offsets (void);

/* needed for remote protocol and for core files */
enum gdb_signal gdb_signal_from_nto (struct gdbarch *, int sig);
int gdb_signal_to_nto (struct gdbarch *gdbarch, enum gdb_signal sig);

int qnx_filename_cmp (const char *s1, const char *s2, size_t n);

LONGEST nto_read_auxv_from_initial_stack (CORE_ADDR inital_stack,
					  gdb_byte *readbuf,
					  LONGEST len);

char *nto_pid_to_str (struct target_ops *ops, ptid_t);

char *nto_gdbarch_core_pid_to_str (struct gdbarch *, ptid_t);

const struct target_desc *nto_read_description (struct target_ops *ops);


struct nto_inferior_data *nto_inferior_data (struct inferior *inf);

int nto_breakpoint_size (CORE_ADDR addr);

struct type *nto_get_siginfo_type (struct gdbarch *);

void nto_get_siginfo_from_procfs_status (const void *status, void *siginfo);

int nto_stopped_by_watchpoint (void);

#endif

/* Target-dependent code for QNX Neutrino x86.

   Copyright (C) 2003-2016 Free Software Foundation, Inc.

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
#include "frame.h"
#include "osabi.h"
#include "regcache.h"
#include "target.h"

#include "i386-tdep.h"
#include "i387-tdep.h"
#include "nto-tdep.h"
#include "solib.h"
#include "solib-svr4.h"
#include "x86-xstate.h"

/* CPU capability/state flags from x86/syspage.h */
#define X86_CPU_FXSR        (1UL <<  12)  /* CPU supports FXSAVE/FXRSTOR  */
#define X86_CPU_XSAVE       (1UL <<  17)  /* CPU supports XSAVE/XRSTOR */

/* Why 13?  Look in our /usr/include/x86/context.h header at the
   x86_cpu_registers structure and you'll see an 'exx' junk register
   that is just filler.  Don't ask me, ask the kernel guys.  */
#define NUM_GPREGS 13

/* Mapping between the general-purpose registers in `struct xxx'
   format and GDB's register cache layout.  */

/* From <x86/context.h>.  */
static int i386nto_gregset_reg_offset[] =
{
  7 * 4,			/* %eax */
  6 * 4,			/* %ecx */
  5 * 4,			/* %edx */
  4 * 4,			/* %ebx */
  11 * 4,			/* %esp */
  2 * 4,			/* %epb */
  1 * 4,			/* %esi */
  0 * 4,			/* %edi */
  8 * 4,			/* %eip */
  10 * 4,			/* %eflags */
  9 * 4,			/* %cs */
  12 * 4,			/* %ss */
  -1				/* filler */
};

/* Given a GDB register number REGNUM, return the offset into
   Neutrino's register structure or -1 if the register is unknown.  */

static int
nto_reg_offset (int regnum)
{
  if (regnum >= 0 && regnum < ARRAY_SIZE (i386nto_gregset_reg_offset))
    return i386nto_gregset_reg_offset[regnum];

  return -1;
}

static void
i386_nto_supply_fpregset (const struct regset *regset,
                       struct regcache *regcache,
		       int regnum, const void *fpregs, size_t len)
{
  struct gdbarch *gdbarch = get_regcache_arch (regcache);
  const struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  gdb_assert (len >= tdep->sizeof_fpregset);

  if (len > I387_SIZEOF_FXSAVE)
    i387_supply_xsave (regcache, regnum, fpregs);
  else if (len == I387_SIZEOF_FXSAVE)
    i387_supply_fxsave (regcache, regnum, fpregs);
  else
    i387_supply_fsave (regcache, regnum, fpregs);
}

static void
i386_nto_collect_fpregset (const struct regset *regset,
			const struct regcache *regcache,
			int regnum, void *fpregs, size_t len)
{
  struct gdbarch *gdbarch = get_regcache_arch (regcache);
  const struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  gdb_assert (len >= tdep->sizeof_fpregset);

  if (len > I387_SIZEOF_FXSAVE)
    i387_collect_xsave (regcache, regnum, fpregs, 0);
  else if (len == I387_SIZEOF_FXSAVE)
    i387_collect_fxsave (regcache, regnum, fpregs);
  else
    i387_collect_fsave (regcache, regnum, fpregs);
}

static const struct regset i386_nto_fpregset =
  {
    NULL, i386_nto_supply_fpregset, i386_nto_collect_fpregset, REGSET_VARIABLE_SIZE
  };


static void
i386nto_supply_gregset (struct regcache *regcache, const gdb_byte *gpregs,
			size_t len)
{
  struct gdbarch *gdbarch = get_regcache_arch (regcache);
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  gdb_assert (tdep->gregset_reg_offset == i386nto_gregset_reg_offset);
  i386_supply_gregset (&i386_gregset, regcache, -1,
			      gpregs, NUM_GPREGS * 4);
}

static void
i386nto_supply_fpregset (struct regcache *regcache, const gdb_byte *fpregs,
			 size_t len)
{
  i386_nto_fpregset.supply_regset (&i386_nto_fpregset, regcache, -1, fpregs, len);
}

static void
i386nto_supply_regset (struct regcache *regcache, int regset,
		       const gdb_byte *data, size_t len)
{
  switch (regset)
    {
    case NTO_REG_GENERAL:
      i386nto_supply_gregset (regcache, data, len);
      break;
    case NTO_REG_FLOAT:
      i386nto_supply_fpregset (regcache, data, len);
      break;
    }
}

static int
i386nto_regset_id (int regno)
{
  if (regno == -1)
    return NTO_REG_END;
  else if (regno < I386_NUM_GREGS)
    return NTO_REG_GENERAL;
  else if (regno < I386_NUM_GREGS + I387_NUM_REGS)
    return NTO_REG_FLOAT;
  else if (regno < I386_SSE_NUM_REGS)
    return NTO_REG_FLOAT; /* We store xmm registers in fxsave_area.  */
  else if (regno < I386_AVX_NUM_REGS) {
    return NTO_REG_FLOAT;
  }

  return -1;			/* Error.  */
}

static void
i386_nto_iterate_over_regset_sections (struct gdbarch *gdbarch,
				   iterate_over_regset_sections_cb *cb,
				   void *cb_data,
				   const struct regcache *regcache)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  cb (".reg", tdep->sizeof_gregset, &i386_gregset, NULL, cb_data);
  cb (".reg2", tdep->sizeof_fpregset, &i386_nto_fpregset, NULL, cb_data);
}

static int
i386nto_register_area (int regset, unsigned cpuflags)
{
  if (regset == NTO_REG_GENERAL)
	return NUM_GPREGS * 4;
  else if (regset == NTO_REG_FLOAT)
    {
      if (cpuflags & X86_CPU_XSAVE)
	{
	  /* At most DS_DATA_MAX_SIZE: */
	  return 1024;
	}
      else if (cpuflags & X86_CPU_FXSR)
	  return 512;
      else
	  return 108;
    }
  else
    {
      warning(_("Only general and floatpoint registers supported on x86 for now\n"));
    }
  return -1;
}

static int
i386nto_regset_fill (const struct regcache *regcache, int regset,
		     gdb_byte *data, size_t len)
{
  if (regset == NTO_REG_GENERAL)
    {
      int regno;

      for (regno = 0; regno < NUM_GPREGS; regno++)
	{
	  int offset = nto_reg_offset (regno);
	  if (offset != -1)
	    regcache_raw_collect (regcache, regno, data + offset);
	}
    }
  else if (regset == NTO_REG_FLOAT)
    {
      if (len > I387_SIZEOF_FXSAVE)
	i387_collect_xsave (regcache, -1, data, 0);
      else if (len == I387_SIZEOF_FXSAVE)
	i387_collect_fxsave (regcache, -1, data);
      else
	i387_collect_fsave (regcache, -1, data);
    }
  else
    return -1;

  return 0;
}

/* Return whether THIS_FRAME corresponds to a QNX Neutrino sigtramp
   routine.  */

static int
i386nto_sigtramp_p (struct frame_info *this_frame)
{
  CORE_ADDR pc = get_frame_pc (this_frame);
  const char *name;

  find_pc_partial_function (pc, &name, NULL, NULL);
  return name && strcmp ("__signalstub", name) == 0;
}

/* Assuming THIS_FRAME is a QNX Neutrino sigtramp routine, return the
   address of the associated sigcontext structure.  */

static CORE_ADDR
i386nto_sigcontext_addr (struct frame_info *this_frame)
{
  struct gdbarch *gdbarch = get_frame_arch (this_frame);
  enum bfd_endian byte_order = gdbarch_byte_order (gdbarch);
  gdb_byte buf[4];
  CORE_ADDR ptrctx;

  /* We store __ucontext_t addr in EDI register.  */
  get_frame_register (this_frame, I386_EDI_REGNUM, buf);
  ptrctx = extract_unsigned_integer (buf, 4, byte_order);
  ptrctx += 24 /* Context pointer is at this offset.  */;

  return ptrctx;
}

static int
i386nto_breakpoint_size (CORE_ADDR addr)
{
  return 0;
}

static const struct target_desc *
i386nto_read_description(unsigned cpuflags)
{
  /* With a lazy allocation of the fpu context we cannot easily tell
     up-front what the target supports, so set an upper bound on the
     features. */
  return i386_target_description(X86_XSTATE_AVX_MASK);
}

static const struct target_desc *
i386nto_core_read_description (struct gdbarch *gdbarch,
				  struct target_ops *target,
				  bfd *abfd)
{
  /* We could pull xcr0 from the corefile, but just keep things
     consistent with i386nto_read_description() */
  return i386_target_description(X86_XSTATE_AVX_MASK);
}

static struct nto_target_ops i386_nto_ops;

static void
init_i386nto_ops (void)
{
  i386_nto_ops.regset_id = i386nto_regset_id;
  i386_nto_ops.supply_gregset = i386nto_supply_gregset;
  i386_nto_ops.supply_fpregset = i386nto_supply_fpregset;
  i386_nto_ops.supply_altregset = nto_dummy_supply_regset;
  i386_nto_ops.supply_regset = i386nto_supply_regset;
  i386_nto_ops.register_area = i386nto_register_area;
  i386_nto_ops.regset_fill = i386nto_regset_fill;
  i386_nto_ops.fetch_link_map_offsets =
    nto_generic_svr4_fetch_link_map_offsets;
  i386_nto_ops.breakpoint_size = i386nto_breakpoint_size;
  i386_nto_ops.read_description = i386nto_read_description;
}

static void
i386nto_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);
  static struct target_so_ops nto_svr4_so_ops;
  struct nto_target_ops *nto_ops;

  /* Deal with our strange signals.  */
  nto_initialize_signals ();

  /* NTO uses ELF.  */
  i386_elf_init_abi (info, gdbarch);

  /* Neutrino rewinds to look more normal.  Need to override the i386
     default which is [unfortunately] to decrement the PC.  */
  set_gdbarch_decr_pc_after_break (gdbarch, 0);

  tdep->gregset_reg_offset = i386nto_gregset_reg_offset;
  tdep->gregset_num_regs = ARRAY_SIZE (i386nto_gregset_reg_offset);
  tdep->sizeof_gregset = NUM_GPREGS * 4;

  tdep->sigtramp_p = i386nto_sigtramp_p;
  tdep->sigcontext_addr = i386nto_sigcontext_addr;
  tdep->sc_reg_offset = i386nto_gregset_reg_offset;
  tdep->sc_num_regs = ARRAY_SIZE (i386nto_gregset_reg_offset);

  /* Setjmp()'s return PC saved in EDX (5).  */
  tdep->jb_pc_offset = 20;	/* 5x32 bit ints in.  */

  tdep->sizeof_gregset = NUM_GPREGS * 4;

  set_solib_svr4_fetch_link_map_offsets
    (gdbarch, nto_generic_svr4_fetch_link_map_offsets);

  /* Initialize this lazily, to avoid an initialization order
     dependency on solib-svr4.c's _initialize routine.  */
  if (nto_svr4_so_ops.in_dynsym_resolve_code == NULL)
    {
      nto_svr4_so_ops = svr4_so_ops;

      /* Our loader handles solib relocations differently than svr4.  */
      nto_svr4_so_ops.relocate_section_addresses
        = nto_relocate_section_addresses;

      /* Supply a nice function to find our solibs.  */
      nto_svr4_so_ops.find_and_open_solib
        = nto_find_and_open_solib;

      /* Our linker code is in libc.  */
      nto_svr4_so_ops.in_dynsym_resolve_code
        = nto_in_dynsym_resolve_code;
    }
  set_solib_ops (gdbarch, &nto_svr4_so_ops);

  set_gdbarch_get_siginfo_type (gdbarch, nto_get_siginfo_type);

  nto_ops = (struct nto_target_ops *) gdbarch_data (gdbarch, nto_gdbarch_ops);
  *nto_ops = i386_nto_ops;

  set_gdbarch_gdb_signal_to_target (gdbarch, nto_gdb_signal_to_target);
  set_gdbarch_gdb_signal_from_target (gdbarch, nto_gdb_signal_from_target);

  set_gdbarch_iterate_over_regset_sections
    (gdbarch, i386_nto_iterate_over_regset_sections);
  set_gdbarch_core_read_description (gdbarch,
				     i386nto_core_read_description);
}

/* Provide a prototype to silence -Wmissing-prototypes.  */
extern initialize_file_ftype _initialize_i386nto_tdep;

void
_initialize_i386nto_tdep (void)
{
  init_i386nto_ops ();
  gdbarch_register_osabi (bfd_arch_i386, 0, GDB_OSABI_QNXNTO,
			  i386nto_init_abi);
  gdbarch_register_osabi_sniffer (bfd_arch_i386, bfd_target_elf_flavour,
				  nto_elf_osabi_sniffer);
}

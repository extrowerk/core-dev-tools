/* ARM specific functionality for QNX Neutrino.

   Copyright 2003 Free Software Foundation, Inc.

   Contributed by QNX Software Systems Ltd.

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
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#include "defs.h"
#include "frame.h"
#include "target.h"
#include "regcache.h"
#include "solib-svr4.h"
#include "arm-tdep.h"
#include "nto-tdep.h"
#include "osabi.h"
#include "nto-share/debug.h"

#include "trad-frame.h"
#include "tramp-frame.h"

/* 16 GP regs + spsr */
#define GP_REGSET_SIZE (17*4)
#define PS_OFF (16*4)

static void
armnto_supply_reg_gregset (struct regcache *regcache, int regno, char *regs)
{
  int regi;

  for (regi = ARM_A1_REGNUM; regi < ARM_F0_REGNUM; regi++)
    {
      RAW_SUPPLY_IF_NEEDED (regcache, regi, regs);
      regs += 4;
    }
  RAW_SUPPLY_IF_NEEDED (regcache, ARM_PS_REGNUM, regs);
}

static void
armnto_supply_gregset (struct regcache *regcache, char *regs)
{
  armnto_supply_reg_gregset (regcache, NTO_ALL_REGS, regs);
}

static void
armnto_supply_regset (struct regcache *regcache, int regset, char *data)
{
  switch (regset)
    {
    case NTO_REG_GENERAL:
      armnto_supply_gregset (regcache, data);
      break;
    default:
      gdb_assert (0);
    }
}

static int
armnto_regset_id (int regno)
{
  if (regno == -1)
    return NTO_REG_END;
  else if (regno < ARM_F0_REGNUM || regno == ARM_FPS_REGNUM
	   || regno == ARM_PS_REGNUM)
    return NTO_REG_GENERAL;
  else if (regno >= ARM_F0_REGNUM && regno <= ARM_F7_REGNUM)
    return NTO_REG_FLOAT;
  return -1;
}

static int
armnto_register_area (int regno, int regset, unsigned *off)
{
  *off = 0;

  if (regset == NTO_REG_GENERAL)
    {
      if (regno == -1)
	return GP_REGSET_SIZE;

      if (regno < ARM_PS_REGNUM)
	*off = regno * 4;
      else if (regno == ARM_PS_REGNUM)
	*off = PS_OFF;
      else
	return 0;
      return 4;
    }
  return -1;
}

static int
armnto_regset_fill (const struct regcache *regcache, int regset, char *data)
{
  int regi;

  if (regset == NTO_REG_GENERAL)
    {
      for (regi = ARM_A1_REGNUM; regi < ARM_F0_REGNUM; regi++)
	{
	  regcache_raw_collect (regcache, regi, data);
	  data += 4;
	}
      regcache_raw_collect (regcache, ARM_PS_REGNUM, data);
    }
  else
    return -1;
  return 0;
}

/* FIXME: use generic directly */
static struct link_map_offsets *
armnto_svr4_fetch_link_map_offsets (void)
{
  return nto_generic_svr4_fetch_link_map_offsets ();
}


static enum gdb_osabi
ntoarm_elf_osabi_sniffer (bfd *abfd)
{
    return GDB_OSABI_QNXNTO;
}

static void
init_armnto_ops ()
{
  nto_regset_id = armnto_regset_id;
  nto_supply_gregset = armnto_supply_gregset;
  nto_supply_fpregset = nto_dummy_supply_regset;
  nto_supply_altregset = nto_dummy_supply_regset;
  nto_supply_regset = armnto_supply_regset;
  nto_register_area = armnto_register_area;
  nto_regset_fill = armnto_regset_fill;
  nto_fetch_link_map_offsets = armnto_svr4_fetch_link_map_offsets;
}

/* Core file support */
static void
armnto_core_supply_gregset (const struct regset *regset, 
                             struct regcache *regcache,
			     int regnum, const void *preg,
			     size_t len)
{
  nto_trace (0) ("%s () regnum=%d\n", __func__, regnum);

  armnto_supply_reg_gregset (regcache, regnum, (char *)preg);
}

static struct regset armnto_gregset =
{
  NULL,
  armnto_core_supply_gregset,
  NULL,
  NULL
};


/* Return the appropriate register set for the core section identified
   by SECT_NAME and SECT_SIZE.  */

static const struct regset *
armnto_regset_from_core_section (struct gdbarch *gdbarch,
				   const char *sect_name, size_t sect_size)
{
  nto_trace (0) ("%s () sect_name:%s\n", __func__, sect_name);

  if (strcmp (sect_name, ".reg") == 0
      && sect_size >= GP_REGSET_SIZE)
    return &armnto_gregset;

  gdb_assert (0);
  return NULL;
}

/* Signal trampolines. */

static void
armnto_sigtramp_cache_init (const struct tramp_frame *self,
                            struct frame_info *next_frame,
			    struct trad_frame_cache *this_cache,
			    CORE_ADDR func)
{
  struct gdbarch *gdbarch = get_frame_arch (next_frame);
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);
  CORE_ADDR base, addr;
  int regi, off = 0;
  
  nto_trace (0) ("%s () funcaddr=0x%s\n", __func__, paddr (func));

  base = frame_unwind_register_unsigned (next_frame,
                                         gdbarch_sp_regnum (current_gdbarch));

  gdb_assert (gdbarch_sp_regnum (current_gdbarch) == ARM_SP_REGNUM);

  nto_trace (0) ("base address = 0x%s\n", paddr (base));

/* Construct the frame ID using the function start. */
  trad_frame_set_id (this_cache, frame_id_build (base, func));

  base += 124; /* offset to context */
 
  /* retrieve registers */
  for (regi = ARM_A1_REGNUM; regi < ARM_F0_REGNUM; regi++)
    {
      CORE_ADDR addr = base + (regi - ARM_A1_REGNUM) * 4;
      trad_frame_set_reg_addr (this_cache, regi, addr );
    }

  //trad_frame_set_reg_addr (this_cache, ARM_PS_REGNUM, base + 16 * 4);
}

static struct tramp_frame arm_nto_sighandler_tramp_frame = {
  SIGTRAMP_FRAME,
  4,
  { 
    { 0xe1a00004, 0xFFFFFFFF }, /* mov r0, r4 */
    { 0xe8950ffe, 0xFFFFFFFF }, /* ldmia r5, {r1, r2, r3, r4,.... */
    { TRAMP_SENTINEL_INSN, -1 },
  },
  armnto_sigtramp_cache_init
};



static void
armnto_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  /* Deal with our strange signals.  */
  nto_initialize_signals();

  set_solib_svr4_fetch_link_map_offsets (gdbarch,
					 armnto_svr4_fetch_link_map_offsets);

  /* Our loader handles solib relocations slightly differently than svr4.  */
  TARGET_SO_RELOCATE_SECTION_ADDRESSES = nto_relocate_section_addresses;

  /* Supply a nice function to find our solibs.  */
  TARGET_SO_FIND_AND_OPEN_SOLIB = nto_find_and_open_solib;

  /* Our linker code is in libc.  */
  TARGET_SO_IN_DYNSYM_RESOLVE_CODE = nto_in_dynsym_resolve_code;

/* register core handler */
  set_gdbarch_regset_from_core_section (gdbarch, 
                                    armnto_regset_from_core_section);
  
  /* Signal trampoline */
  tramp_frame_prepend_unwinder (gdbarch,
				&arm_nto_sighandler_tramp_frame);

  init_armnto_ops ();
}

void
_initialize_armnto_tdep (void)
{
  gdbarch_register_osabi (bfd_arch_arm, 0, GDB_OSABI_QNXNTO, armnto_init_abi);
  gdbarch_register_osabi_sniffer (bfd_arch_arm, bfd_target_elf_flavour,
		  		  nto_elf_osabi_sniffer);
}

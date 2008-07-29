/* SH4 specific functionality for QNX Neutrino.

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
#include "sh-tdep.h"
#include "nto-tdep.h"
#include "osabi.h"

#include "trad-frame.h"
#include "tramp-frame.h"

#include "frame-unwind.h"

#define GP_REGSET_SIZE ((16 + 6) << 2)	/* 16 gpregs + 6 others.  */
#define SR_OFF (16 << 2)
#define PC_OFF (17 << 2)
#define GBR_OFF (18 << 2)
#define MACH_OFF (19 << 2)
#define MACL_OFF (20 << 2)
#define PR_OFF (21 << 2)

#define FP_REGSET_SIZE ((16 + 16 + 2) << 2)	/* Two fpr banks + fpul & fpscr.  */
#define FPUL_OFF (32 << 2)
#define FPSCR_OFF (33 << 2)

static void
shnto_supply_reg_gregset (struct regcache *regcache, int regno, char *data)
{
  unsigned regi, empty = 0;

  nto_trace (0) ("%s () regno=%d\n", __func__, regno);

  for (regi = 0; regi < 16; regi++)
    {
      RAW_SUPPLY_IF_NEEDED (regcache, regi, data + (regi << 2));
    }
  RAW_SUPPLY_IF_NEEDED (regcache, SR_REGNUM, data + SR_OFF);
  RAW_SUPPLY_IF_NEEDED (regcache, gdbarch_pc_regnum (current_gdbarch), data + PC_OFF);
  RAW_SUPPLY_IF_NEEDED (regcache, GBR_REGNUM, data + GBR_OFF);
  RAW_SUPPLY_IF_NEEDED (regcache, MACH_REGNUM, data + MACH_OFF);
  RAW_SUPPLY_IF_NEEDED (regcache, MACL_REGNUM, data + MACL_OFF);
  RAW_SUPPLY_IF_NEEDED (regcache, PR_REGNUM, data + PR_OFF);
  RAW_SUPPLY_IF_NEEDED (regcache, VBR_REGNUM, (char *) &empty);
}

static void
shnto_supply_gregset (struct regcache *regcache, char *data)
{
  shnto_supply_reg_gregset (regcache, NTO_ALL_REGS, data);
}

static void
shnto_supply_reg_fpregset (struct regcache *regcache, int regno, char *data)
{
  unsigned regi;

  nto_trace (0) ("%s () regno=%d\n", __func__, regno);

  /* First 16 32 bit entities are bank0, second 16 are bank1.  */
  for (regi = 1; regi <= 16; regi++)
    {
      RAW_SUPPLY_IF_NEEDED (regcache, FPSCR_REGNUM + regi, data + (regi << 2));
    }
  RAW_SUPPLY_IF_NEEDED (regcache, FPUL_REGNUM, data + FPUL_OFF);
  RAW_SUPPLY_IF_NEEDED (regcache, FPSCR_REGNUM, data + FPSCR_OFF);
}

static void
shnto_supply_fpregset (struct regcache *regcache, char *data)
{
  shnto_supply_reg_fpregset (regcache, NTO_ALL_REGS, data);
}

static void
shnto_supply_regset (struct regcache *regcache, int regset, char *data)
{
  nto_trace (0) ("%s () regset=%d\n", __func__, regset);
  switch (regset)
    {
    case NTO_REG_GENERAL:
      shnto_supply_gregset (regcache, data);
      break;
    case NTO_REG_FLOAT:
      shnto_supply_fpregset (regcache, data);
      break;
    }
}

static int
shnto_regset_id (int regno)
{
  if (regno == -1)
    return NTO_REG_END;
  else if (regno <= SR_REGNUM)
    return NTO_REG_GENERAL;
  else if (regno >= FPUL_REGNUM && regno <= FP_LAST_REGNUM)
    return NTO_REG_FLOAT;
  return -1;			/* Error.  */
}

static int
shnto_register_area (int regno, int regset, unsigned *off)
{
  *off = 0;
  if (regset == NTO_REG_GENERAL)
    {
      if (regno == -1)
	return GP_REGSET_SIZE;

      if (regno < gdbarch_pc_regnum (current_gdbarch))
	*off = regno << 2;
      else
	{
	  if (regno == gdbarch_pc_regnum (current_gdbarch))
	    *off = PC_OFF;
	  else if (regno == PR_REGNUM)
	    *off = PR_OFF;
	  else if (regno == GBR_REGNUM)
	    *off = GBR_OFF;
	  else if (regno == MACH_REGNUM)
	    *off = MACH_OFF;
	  else if (regno == MACL_REGNUM)
	    *off = MACL_OFF;
	  else if (regno == SR_REGNUM)
	    *off = SR_OFF;
	  else
	    return 0;
	}
      return 4;
    }
  else if (regset == NTO_REG_FLOAT)
    {
      if (regno == -1)
	return FP_REGSET_SIZE;

      if (regno >= gdbarch_fp0_regnum (current_gdbarch) && regno <= FP_LAST_REGNUM)
	*off = (regno - gdbarch_fp0_regnum (current_gdbarch)) << 2;
      else if (regno == FPUL_REGNUM)
	*off = FPUL_OFF;
      else if (regno == FPSCR_REGNUM)
	*off = FPSCR_OFF;
      else
	return 0;
      return 4;
    }
  return -1;
}

static int
shnto_regset_fill (const struct regcache *regcache, int regset, char *data)
{
  int regno;

  if (regset == NTO_REG_GENERAL)
    {
      for (regno = 0; regno < 16; regno++)
	{
	  regcache_raw_collect (regcache, regno, data + (regno << 2));
	}
      regcache_raw_collect (regcache, SR_REGNUM, data + SR_OFF);
      regcache_raw_collect (regcache, gdbarch_pc_regnum (current_gdbarch), data + PC_OFF);
      regcache_raw_collect (regcache, GBR_REGNUM, data + GBR_OFF);
      regcache_raw_collect (regcache, MACH_REGNUM, data + MACH_OFF);
      regcache_raw_collect (regcache, MACL_REGNUM, data + MACL_OFF);
      regcache_raw_collect (regcache, PR_REGNUM, data + PR_OFF);
    }
  else if (regset == NTO_REG_FLOAT)
    {
      for (regno = 0; regno < 16; regno++)
	{
	  regcache_raw_collect (regcache, regno + gdbarch_fp0_regnum (current_gdbarch), data + (regno << 2));
	}
      regcache_raw_collect (regcache, FPUL_REGNUM, data + FPUL_OFF);
      regcache_raw_collect (regcache, FPSCR_REGNUM, data + FPSCR_OFF);
    }
  else
    return -1;
  return 0;
}

#if 0
void
shnto_sh_frame_find_saved_regs (struct frame_info *fi,
				struct frame_saved_regs *fsr)
{
  /* DANGER!  This is ONLY going to work if the char buffer format of
     the saved registers is byte-for-byte identical to the
     CORE_ADDR regs[NUM_REGS] format used by struct frame_saved_regs! */

  /* This is a hack to see if we can return an empty set of regs 
     when we are not connected, otherwise call the regular fun'n */

//      if((inferior_pid == NULL) || (fi->pc == 0))
//      if(target_has_execution)
//              sh_frame_find_saved_regs (fi, fsr);
  return;
}
#endif


static void
init_shnto_ops ()
{
  nto_regset_id = shnto_regset_id;
  nto_supply_gregset = shnto_supply_gregset;
  nto_supply_fpregset = shnto_supply_fpregset;
  nto_supply_altregset = nto_dummy_supply_regset;
  nto_supply_regset = shnto_supply_regset;
  nto_register_area = shnto_register_area;
  nto_regset_fill = shnto_regset_fill;
  nto_fetch_link_map_offsets = nto_generic_svr4_fetch_link_map_offsets;
}

/* Core file support */

static void
shnto_core_supply_gregset (const struct regset *regset, 
                             struct regcache *regcache,
			     int regnum, const void *gpreg,
			     size_t len) 
{
  int regset_id;
 
  nto_trace (0) ("%s () regnum: %d\n", __func__, regnum);

  if (regnum == NTO_ALL_REGS) // all registers
    {
      shnto_supply_regset (regcache, NTO_REG_GENERAL, (char *)gpreg);
    } 
  else
    {
      regset_id = shnto_regset_id (regnum);
      nto_trace (0) ("nto_regset_id=%d\n", regset_id);
      shnto_supply_reg_gregset (regcache, regnum, (char *)gpreg);
    }
}

static void 
shnto_core_supply_fpregset (const struct regset *regset, 
                             struct regcache *regcache,
			     int regnum, const void *fpreg,
			     size_t len) 
{
  int regset_id;
  nto_trace (0) ("%s () regnum: %d\n", __func__, regnum);

  if (regnum == NTO_ALL_REGS)
    {
      shnto_supply_regset (regcache, NTO_REG_FLOAT, (char *)fpreg);
    }
  else
    {
      regset_id = shnto_regset_id (regnum);
      nto_trace (0) ("nto regset_id=%d\n", regset_id);
      shnto_supply_reg_fpregset (regcache, regnum, (char *)fpreg); 
    }
}


struct regset shnto_gregset =
{
  NULL,
  shnto_core_supply_gregset, 
  NULL,
  NULL
};

struct regset shnto_fpregset =
{
  NULL,
  shnto_core_supply_fpregset,
  NULL,
  NULL
};

/* Return the appropriate register set for the core section identified
   by SECT_NAME and SECT_SIZE.  */

static const struct regset *
shnto_regset_from_core_section (struct gdbarch *gdbarch,
				  const char *sect_name, size_t sect_size)
{
  nto_trace (0) ("%s () sect_name:%s\n", __func__, sect_name);
  if (strcmp (sect_name, ".reg") == 0 && sect_size >= GP_REGSET_SIZE)
    return &shnto_gregset;

  if (strcmp (sect_name, ".reg2") == 0 && sect_size >= FP_REGSET_SIZE)
    return &shnto_fpregset;

  gdb_assert (0);
  return NULL;
}

/* Signal trampolines. */

/* Return whether the frame preceding NEXT_FRAME corresponds to a QNX
   Neutrino sigtramp routine.  */

#define SH_WORDSIZE 4

static CORE_ADDR
shnto_sigcontext_addr (struct frame_info *next_frame) 
{
  struct gdbarch *gdbarch = get_frame_arch (next_frame);
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);
  CORE_ADDR sp, ptrctx;
  int regi;
  
  nto_trace (0) ("%s () \n", __func__);

  sp = frame_unwind_register_unsigned (next_frame, 
                                      gdbarch_sp_regnum (current_gdbarch));

  nto_trace (0) ("sp: 0x%s\n", paddr (sp));
 
  /* we store context addr in [r8]. The address is really address
   * of reg 5, we need to back-up for 5 * 4 bytes to point
   * at the begining. */
  ptrctx = frame_unwind_register_unsigned (next_frame, R0_REGNUM + 8);
  nto_trace (0) ("r8: 0x%s\n", paddr (ptrctx));
  ptrctx -= 20;
  nto_trace (0) ("context address: 0x%s\n", paddr (ptrctx));

  return ptrctx;
}

struct sh_nto_sigtramp_cache
{
  CORE_ADDR base;
  struct trad_frame_saved_reg *saved_regs;
};

static struct sh_nto_sigtramp_cache *
shnto_sigtramp_cache (struct frame_info *next_frame, void **this_cache)
{
  CORE_ADDR regs;
  CORE_ADDR ptrctx;
  CORE_ADDR fpregs;
  int i;
  struct sh_nto_sigtramp_cache *cache;
  struct gdbarch *gdbarch = get_frame_arch (next_frame);
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  nto_trace (0) ("%s ()\n", __func__);

  if ((*this_cache) != NULL)
    return (*this_cache);
  cache = FRAME_OBSTACK_ZALLOC (struct sh_nto_sigtramp_cache);
  (*this_cache) = cache;
  cache->saved_regs = trad_frame_alloc_saved_regs (next_frame);
  cache->base = frame_unwind_register_unsigned (next_frame, 
						gdbarch_pc_regnum (gdbarch));
  //gpregs = read_memory_unsigned_integer (cache->base, tdep->wordsize);
  ptrctx = shnto_sigcontext_addr (next_frame);

  /* PC register (from before the signal).  */
  cache->saved_regs[gdbarch_pc_regnum (gdbarch)].addr = ptrctx + PC_OFF;
  cache->saved_regs[SR_REGNUM].addr = ptrctx + SR_OFF;
  cache->saved_regs[GBR_REGNUM].addr = ptrctx + GBR_OFF;
  cache->saved_regs[MACH_REGNUM].addr = ptrctx + MACH_OFF;
  cache->saved_regs[MACL_REGNUM].addr = ptrctx + MACL_OFF;
  cache->saved_regs[PR_REGNUM].addr = ptrctx + PR_OFF;

  /* General purpose.  */
  for (i = 0; i < 16; i++)
    {
      int regnum = i;
      cache->saved_regs[regnum].addr = ptrctx + i * SH_WORDSIZE;
    }

  /* FP registers.  */
  for (i = 1; i != 16; i++)
    {
      cache->saved_regs[FPSCR_REGNUM + i].addr 
	= ptrctx + (i + FPSCR_REGNUM) * 4;
    }

  cache->saved_regs[FPUL_REGNUM].addr = ptrctx + FPUL_OFF;
  cache->saved_regs[FPSCR_REGNUM].addr = ptrctx + FPSCR_OFF;

  return cache;
}

static void
sh_nto_sigtramp_this_id (struct frame_info *next_frame, void **this_cache,
			 struct frame_id *this_id)
{
  struct sh_nto_sigtramp_cache *info = shnto_sigtramp_cache (next_frame, 
							    this_cache);
  nto_trace (0) ("%s ()\n", __func__);
  (*this_id) = frame_id_build (info->base, frame_pc_unwind (next_frame));
}

static void
sh_nto_sigtramp_prev_register (struct frame_info *next_frame,
			       void **this_cache,
			       int regnum, int *optimizedp,
			       enum lval_type *lvalp, CORE_ADDR *addrp,
			       int *relnump, gdb_byte *valuep)
{
  struct sh_nto_sigtramp_cache *info = shnto_sigtramp_cache (next_frame, 
							    this_cache);
  nto_trace (0) ("%s ()\n", __func__);
  trad_frame_get_prev_register (next_frame, info->saved_regs, regnum,
				optimizedp, lvalp, addrp, relnump, valuep);
}

static const struct frame_unwind sh_nto_sigtramp_unwind =
{
  SIGTRAMP_FRAME,
  sh_nto_sigtramp_this_id,
  sh_nto_sigtramp_prev_register
};

static const struct frame_unwind *
sh_nto_sigtramp_sniffer (struct frame_info *next_frame)
{
  CORE_ADDR pc = frame_pc_unwind (next_frame);
  char *name;

  nto_trace (0) ("%s ()\n", __func__);

  /* Note: single underscore is due to bfd stripping off leading underscore.
     if bfd is reconfigured to not do that, the literal must be changed to
     have double underscore.  */
  find_pc_partial_function (pc, &name, NULL, NULL);
  if (name 
      && (strcmp ("_signalstub", name) == 0
	  || strcmp ("SignalReturn", name) == 0))
    return &sh_nto_sigtramp_unwind;

  return NULL;
}

static void
shnto_sigtramp_cache_init (const struct tramp_frame *self,
                            struct frame_info *next_frame,
			    struct trad_frame_cache *this_cache,
			    CORE_ADDR func)
{
  struct gdbarch *gdbarch = get_frame_arch (next_frame);
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);
  CORE_ADDR sp, ptrctx, fp, pc;
  int regi;
  
  nto_trace (0) ("%s () funcaddr=0x%s\n", __func__, paddr (func));

  sp = frame_unwind_register_unsigned (next_frame, 
                                      gdbarch_sp_regnum (current_gdbarch));

  nto_trace (0) ("sp: 0x%s\n", paddr (sp));
 
  /* Construct the frame ID using the function start. */
  trad_frame_set_id (this_cache, frame_id_build (sp, func));
 
  /* we store context addr in [r8]. The address is really address
   * of reg 5, we need to back-up for 5 * 4 bytes to point
   * at the begining. */
  ptrctx = frame_unwind_register_unsigned (next_frame, R0_REGNUM + 8);
  nto_trace (0) ("r8: 0x%s\n", paddr (ptrctx));
  ptrctx -= 20;
  nto_trace (0) ("context address: 0x%s\n", paddr (ptrctx));

  for (regi = 0; regi < 16; regi++)
    {
      unsigned int regval;
      CORE_ADDR addr = ptrctx + regi * SH_WORDSIZE;

      trad_frame_set_reg_addr (this_cache, regi, addr);
    }
  trad_frame_set_reg_addr (this_cache, SR_REGNUM, ptrctx + SR_OFF);
  trad_frame_set_reg_addr (this_cache, gdbarch_pc_regnum (current_gdbarch), ptrctx + PC_OFF);
  trad_frame_set_reg_addr (this_cache, GBR_REGNUM, ptrctx + GBR_OFF);
  trad_frame_set_reg_addr (this_cache, MACH_REGNUM, ptrctx + MACH_OFF);
  trad_frame_set_reg_addr (this_cache, MACL_REGNUM, ptrctx + MACL_OFF);
  trad_frame_set_reg_addr (this_cache, PR_REGNUM, ptrctx + PR_OFF);
}

static struct tramp_frame sh_nto_sighandler_tramp_frame = {
  SIGTRAMP_FRAME,
  4,
  { 
    { 0x64936083, 0xFFFFFFFF }, 
    { 0xfef9fff9, 0xFFFFFFFF }, 
    { 0xfcf9fdf9, 0xFFFFFFFF },
    { TRAMP_SENTINEL_INSN, -1 },
  },
  shnto_sigtramp_cache_init
};


static void
shnto_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  /* Deal with our strange signals.  */
  nto_initialize_signals(gdbarch);

  /* Neutrino rewinds to look more normal.  */
  set_gdbarch_decr_pc_after_break (gdbarch, 0);

  /* NTO has shared libraries.  */
  //set_gdbarch_in_solib_call_trampoline (gdbarch, in_plt_section);
  set_gdbarch_skip_trampoline_code (gdbarch, find_solib_trampoline_target);

  set_solib_svr4_fetch_link_map_offsets (gdbarch,
					 nto_generic_svr4_fetch_link_map_offsets);

  /* Trampoline */
  tramp_frame_prepend_unwinder (gdbarch, &sh_nto_sighandler_tramp_frame);
  frame_unwind_append_sniffer (gdbarch, sh_nto_sigtramp_sniffer);

  /* Our loader handles solib relocations slightly differently than svr4.  */
  TARGET_SO_RELOCATE_SECTION_ADDRESSES = nto_relocate_section_addresses;

  /* Supply a nice function to find our solibs.  */
  TARGET_SO_FIND_AND_OPEN_SOLIB = nto_find_and_open_solib;

  /* Our linker code is in libc.  */
  TARGET_SO_IN_DYNSYM_RESOLVE_CODE = nto_in_dynsym_resolve_code;

  set_gdbarch_regset_from_core_section
    (gdbarch, shnto_regset_from_core_section);

  init_shnto_ops ();
}

void
_initialize_shnto_tdep (void)
{
  const struct bfd_arch_info *arch_info;

  for (arch_info = bfd_lookup_arch (bfd_arch_sh, 0);
       arch_info != NULL;
       arch_info = arch_info->next)
    {
      gdbarch_register_osabi (bfd_arch_sh, arch_info->mach,
			      GDB_OSABI_QNXNTO, shnto_init_abi);
    }
  gdbarch_register_osabi_sniffer (bfd_arch_sh, bfd_target_elf_flavour,
		  		  nto_elf_osabi_sniffer);
}

/* ARM specific functionality for QNX Neutrino.

   Copyright 2003, 2009 Free Software Foundation, Inc.

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

#include "trad-frame.h"
#include "tramp-frame.h"
#include "gdbcore.h"

#include "frame-unwind.h"
#include "solib.h"

#include "elf-bfd.h"

/* 16 GP regs + spsr */
#define GP_REGSET_SIZE (17*4)
#define PS_OFF (16*4)
/* Our FP register size. See arm/context.h  */
#define NTO_FP_REGISTER_SIZE 8

/* FP registers - see context.h, Largest register file size + status regs. */
#define FP_REGSET_SIZE (NTO_FP_REGISTER_SIZE * 32 + INT_REGISTER_SIZE * 4) 


static void
armnto_supply_reg_gregset (struct regcache *regcache, int regno,
			   const gdb_byte *regs)
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
armnto_supply_reg_fpregset (struct regcache *regcache, int regno,
			    const gdb_byte *regs)
{
  int regi;

  for (regi = ARM_F0_REGNUM; regi <= ARM_F7_REGNUM; regi++)
    {
      gdb_byte gdbbuf[FP_REGISTER_SIZE]; /* This is GDB's register size. */

      memset (gdbbuf, 0, 12);
      memcpy (gdbbuf + 4, regs, NTO_FP_REGISTER_SIZE);
      RAW_SUPPLY_IF_NEEDED (regcache, regi, gdbbuf);
      regs += NTO_FP_REGISTER_SIZE;     
    }
  /* Status registers. */
  /* FPSCR a.k.a. ARM_FPS_REGNUM (24) */
  /* FPEXC */
}

static void
armnto_supply_gregset (struct regcache *regcache, const gdb_byte *regs)
{
  armnto_supply_reg_gregset (regcache, NTO_ALL_REGS, regs);
}

static void
armnto_supply_fpregset (struct regcache *regcache, const gdb_byte *regs)
{
  armnto_supply_reg_fpregset (regcache, NTO_ALL_REGS, regs);
}

static void
armnto_supply_regset (struct regcache *regcache, int regset,
		      const gdb_byte *data)
{
  switch (regset)
    {
    case NTO_REG_GENERAL:
      armnto_supply_gregset (regcache, data);
      break;
    case NTO_REG_FLOAT:
      armnto_supply_fpregset (regcache, data);
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
armnto_register_area (struct gdbarch *gdbarch,
		      int regno, int regset, unsigned *off)
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
armnto_regset_fill (const struct regcache *regcache, int regset,
		    gdb_byte *data)
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

static const char *
armnto_variant_directory_suffix (void)
{
  const struct gdbarch_tdep *const tdep = gdbarch_tdep (target_gdbarch);

  if (tdep->arm_abi == ARM_ABI_AAPCS
      && tdep->fp_model == ARM_FLOAT_SOFT_VFP)
    {
      nto_trace(1) ("Selecting -v7 variant\n");
      return "-v7";
    }

  return "";
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
  nto_fetch_link_map_offsets = nto_generic_svr4_fetch_link_map_offsets;
  nto_variant_directory_suffix = armnto_variant_directory_suffix;
}

/* */
static int
arm_nto_in_dynsym_resolve_code (CORE_ADDR pc)
{
  gdb_byte buff[24];
  gdb_byte *p = buff + 8;
  ULONGEST instr[] = { 0xe59fc004, 0xe08fc00c, 0xe59cf000 };
  ULONGEST instrmask[] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };
  enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);

  nto_trace (0) ("%s (pc=%s)\n", __func__, paddress (target_gdbarch, pc));

  read_memory (pc - 8, buff, 24);

  while (p >= buff)
    {
      if ((extract_unsigned_integer (p, 4, byte_order) & instrmask[0])
	  == instr[0])
        break;

      p -= 4;
    }

  if (p >= buff)
    {
      int i;
      int match = 1;
     
      for (i = 1; i != sizeof(instr)/sizeof(instr[0]); ++i)
        {
	  ULONGEST inst;
	  
	  p += 4;

	  inst = extract_unsigned_integer (p, 4, byte_order) & instrmask[i];

	  if (inst != instr[i])
	    {
	      match = 0;
	      break; // did not match
	    }
        }

	if (match) 
	  {
	    nto_trace (0) ("Looks like plt code\n");
	    return 1;
	  }
    }
  
  nto_trace (0) ("%s: could not recognize plt code\n", __func__);
  return nto_in_dynsym_resolve_code (pc);
}

/* Core file support */
static void
armnto_core_supply_gregset (const struct regset *regset, 
                             struct regcache *regcache,
			     int regnum, const void *preg,
			     size_t len)
{
  nto_trace (0) ("%s () regnum=%d\n", __func__, regnum);

  armnto_supply_reg_gregset (regcache, regnum, (const gdb_byte *)preg);
}

static void
armnto_core_supply_fpregset (const struct regset *regset,
			     struct regcache *regcache,
			     int regnum, const void *preg,
			     size_t len)
{
  nto_trace (0) ("%s () regnum=%d\n", __func__, regnum);

  armnto_supply_reg_fpregset (regcache, regnum, (const gdb_byte *)preg);
}

static struct regset armnto_gregset =
{
  NULL,
  armnto_core_supply_gregset,
  NULL,
  NULL
};

static struct regset armnto_fpregset = 
{
  NULL,
  armnto_core_supply_fpregset,
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

  if (strcmp (sect_name, ".reg2") == 0
      && sect_size >= FP_REGSET_SIZE)
    return &armnto_fpregset;

  gdb_assert (0);
  return NULL;
}

/* Signal trampolines. */

/* Signal trampoline sniffer.  */

static CORE_ADDR
armnto_sigcontext_addr (struct frame_info *this_frame)
{
  struct gdbarch *gdbarch = get_frame_arch (this_frame);
  CORE_ADDR ptrctx, sp;

  nto_trace (0) ("%s ()\n", __func__);

  /* stack pointer for _signalstub frame.  */
  sp = get_frame_sp (this_frame);

  /*  read base from r5 of the sigtramp frame:
   we store base + 4 (addr of r1, not r0) in r5. 
   stmia	lr, {r1-r12} 
   mov	r5, lr 
  */
  get_frame_register (this_frame, ARM_A1_REGNUM + 5, (gdb_byte *)&ptrctx);
  ptrctx -= 4;

  nto_trace (0) ("context addr: 0x%s\n", paddress (gdbarch, ptrctx));

  return ptrctx;
}

struct arm_nto_sigtramp_cache
{
  CORE_ADDR base;
  struct trad_frame_saved_reg *saved_regs;
};

static struct arm_nto_sigtramp_cache *
armnto_sigtramp_cache (struct frame_info *this_frame, void **this_cache)
{
  CORE_ADDR ptrctx;
  int i;
  struct arm_nto_sigtramp_cache *cache;
  const int REGSIZE = 4;

  nto_trace (0) ("%s ()\n", __func__);

  if ((*this_cache) != NULL)
    return (*this_cache);
  cache = FRAME_OBSTACK_ZALLOC (struct arm_nto_sigtramp_cache);
  (*this_cache) = cache;
  cache->saved_regs = trad_frame_alloc_saved_regs (this_frame);
  cache->base = get_frame_pc (this_frame);
  ptrctx = armnto_sigcontext_addr (this_frame);

  /* Registers from before signal. */
  for (i = ARM_A1_REGNUM; i != ARM_F0_REGNUM; ++i)
    cache->saved_regs[i].addr = ptrctx + i * REGSIZE; 

  /* PS register. We store it as 16-th register in ucontext_t structure. */
  cache->saved_regs[ARM_PS_REGNUM].addr = ptrctx + 16 * REGSIZE;

  return cache;
}

static void
armnto_sigtramp_this_id (struct frame_info *this_frame,
			 void **this_prologue_cache, struct frame_id *this_id)
{
  struct arm_nto_sigtramp_cache *info =
	armnto_sigtramp_cache (this_frame, this_prologue_cache);
  nto_trace (0) ("%s ()\n", __func__);
  (*this_id) = frame_id_build (info->base, get_frame_pc (this_frame));
}

static struct value *
armnto_sigtramp_prev_register (struct frame_info *this_frame,
			       void **this_prologue_cache,
			       int regnum)
{
  struct arm_nto_sigtramp_cache *info =
	    armnto_sigtramp_cache (this_frame, this_prologue_cache);
  nto_trace (0) ("%s ()\n", __func__);
  trad_frame_get_prev_register (this_frame, info->saved_regs, regnum);
  return NULL;
}

static int
armnto_sigtramp_sniffer (const struct frame_unwind *self,
			 struct frame_info *this_frame,
			 void **this_prolobue_cache)
{
  CORE_ADDR pc = get_frame_pc (this_frame);
  char *name;

  nto_trace (0) ("%s ()\n", __func__);

  find_pc_partial_function (pc, &name, NULL, NULL);
  if (name
      && (strcmp ("__signalstub", name) == 0
	  || strcmp ("SignalReturn", name) == 0))
     return 1;

  return 0;
}

static const struct frame_unwind arm_nto_sigtramp_unwind =
{
  SIGTRAMP_FRAME,
  armnto_sigtramp_this_id,
  armnto_sigtramp_prev_register,
  NULL,
  armnto_sigtramp_sniffer,
  NULL
};



static void
armnto_sigtramp_cache_init (const struct tramp_frame *self,
                            struct frame_info *this_frame,
			    struct trad_frame_cache *this_cache,
			    CORE_ADDR func)
{
  struct gdbarch *gdbarch = get_frame_arch (this_frame);
  CORE_ADDR ptrctx, sp;
  int regi;
  const int REGSIZE = 4;
  //struct frame_info *this_frame = get_prev_frame(next_frame);
  
  nto_trace (0) ("%s () funcaddr=0x%s\n", __func__, paddress (gdbarch, func));

  /* stack pointer for __signal_stub frame */
  sp = get_frame_sp (this_frame);

  nto_trace (0) ("sp: 0x%s\n", paddress (gdbarch, sp));

  gdb_assert (gdbarch_sp_regnum (gdbarch) == ARM_SP_REGNUM);

  /* Construct the frame ID using the function start. */
  trad_frame_set_id (this_cache, frame_id_build (sp, func));
  ptrctx = armnto_sigcontext_addr (this_frame);
  nto_trace (0) ("context addr: 0x%s\n", paddress (gdbarch, ptrctx));

  /* retrieve registers */
  for (regi = ARM_A1_REGNUM; regi < ARM_F0_REGNUM; regi++)
    {
      const CORE_ADDR addr = ptrctx + (regi - ARM_A1_REGNUM) * REGSIZE;
      trad_frame_set_reg_addr (this_cache, regi, addr);
    }

  trad_frame_set_reg_addr (this_cache, ARM_PS_REGNUM, ptrctx + 16 * REGSIZE);
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
#if 0
static struct tramp_frame armbe_nto_sighandler_tramp_frame = {
  SIGTRAMP_FRAME,
  4,
  {
    { 0xebfffff0, 0xFFFFFFFF },
    { 0xe91ba800, 0xFFFFFFFF },
    { TRAMP_SENTINEL_INSN, -1 },
  },
  armnto_sigtramp_cache_init
};
#endif

static void
armnto_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  /* Deal with our strange signals.  */
  nto_initialize_signals(gdbarch);

  set_solib_svr4_fetch_link_map_offsets (gdbarch,
					 nto_generic_svr4_fetch_link_map_offsets);

  /* Our loader handles solib relocations slightly differently than svr4.  */
  svr4_so_ops.relocate_section_addresses = nto_relocate_section_addresses;

  /* Supply a nice function to find our solibs.  */
  svr4_so_ops.find_and_open_solib = nto_find_and_open_solib;

  /* Our linker code is in libc.  */
  svr4_so_ops.in_dynsym_resolve_code = arm_nto_in_dynsym_resolve_code;

  /* register core handler */
  set_gdbarch_regset_from_core_section (gdbarch, 
                                    armnto_regset_from_core_section);
  set_solib_ops (gdbarch, &svr4_so_ops);
  
  /* Signal trampoline */
  tramp_frame_prepend_unwinder (gdbarch,
				&arm_nto_sighandler_tramp_frame);

  init_armnto_ops ();

  /* Our single step is broken. Use software. */
  set_gdbarch_software_single_step (gdbarch, arm_software_single_step);
}

void
_initialize_armnto_tdep (void)
{
  gdbarch_register_osabi (bfd_arch_arm, 0, GDB_OSABI_QNXNTO, armnto_init_abi);
  gdbarch_register_osabi_sniffer (bfd_arch_arm, bfd_target_elf_flavour,
		  		  nto_elf_osabi_sniffer);
}

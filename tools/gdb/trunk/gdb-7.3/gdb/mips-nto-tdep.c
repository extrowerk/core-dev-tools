/* MIPS specific functionality for QNX Neutrino.

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
#include "mips-tdep.h"
#include "nto-tdep.h"
#include "osabi.h"
#include "objfiles.h"
#include "frame.h"

#include "trad-frame.h"
#include "tramp-frame.h"
#include "gdbcore.h"

#include "frame-unwind.h"
#include "solib.h"

#ifndef MIPS_PRID_IMPL
#define MIPS_PRID_IMPL(p)               (((p) >> 8) & 0xff)
#endif

#define GP_REGSET_SIZE (8 * 38)

#define FP_REGSET_SIZE (8 * 32 + 4)
#define FPCR31_OFF (8 * 32)

/* Number of registers in `struct reg' from <machine/reg.h>.  */
#define MIPSNTO_NUM_GREGS	38

/* Number of registers in `struct fpreg' from <machine/reg.h>.  */
#define MIPSNTO_NUM_FPREGS	33

#define MIPS_PC_REGNUM  MIPS_EMBED_PC_REGNUM


extern char *mips_processor_type;
extern char *tmp_mips_processor_type;
extern void mips_set_processor_type_command PARAMS ((char *str, int from_tty));

void 
qnx_set_processor_type (unsigned cpuid)
{
#if 0
    if(MIPS_PRID_IMPL(cpuid) == 0x38)
        tmp_mips_processor_type = xstrdup("tx79");
    else
        tmp_mips_processor_type = xstrdup("r3051");
    mips_set_processor_type_command(tmp_mips_processor_type, 0);
#endif
    return;
}

typedef char nto_reg64[8];

static void
mipsnto_supply_reg_gregset (struct regcache *regcache, int regno,
			    const gdb_byte *data)
{
  int regi, off = 0;
  nto_reg64 *regs = (nto_reg64 *)data;

  nto_trace (0) ("%s ()\n", __func__);

  /* on big endian, register data is in second word of Neutrino's 8 byte regs */
  if (gdbarch_byte_order (target_gdbarch) == BFD_ENDIAN_BIG &&
      register_size (target_gdbarch, MIPS_ZERO_REGNUM) == 4)
    off = 4;

  for (regi = MIPS_ZERO_REGNUM; regi < gdbarch_fp0_regnum (target_gdbarch);
       regi++)
    RAW_SUPPLY_IF_NEEDED (regcache, regi, &regs[regi - MIPS_ZERO_REGNUM][off]);

#if defined(FIRST_ALTREG) && defined(LAST_ALTREG) //tx79
  /* FIXME */
  if (mips_processor_type && !strcasecmp("tx79", mips_processor_type))
    RAW_SUPPLY_IF_NEEDED (regcache, FIRST_ALTREG, LAST_ALTREG,
			  (char *)gregsetp );
#endif
}

static void
mipsnto_supply_gregset (struct regcache *regcache, const gdb_byte *data)
{
  mipsnto_supply_reg_gregset (regcache, NTO_ALL_REGS, data);
}

static void
mipsnto_supply_reg_fpregset (struct regcache *regcache, int regno,
			     const gdb_byte *data)
{
  int regi, off = 0;
  nto_reg64 *regs = (nto_reg64 *)data;

  nto_trace (0) ("%s ()\n", __func__);
  
  if (gdbarch_byte_order (target_gdbarch) == BFD_ENDIAN_BIG
      && register_size (target_gdbarch, MIPS_ZERO_REGNUM) == 4)
    off = 4;

  for (regi = gdbarch_fp0_regnum (target_gdbarch);
       regi < gdbarch_fp0_regnum (target_gdbarch) + 16; regi++)
    regcache_raw_supply (regcache, regi,
			 &regs[regi
			       - gdbarch_fp0_regnum (target_gdbarch)][off]);

  regcache_raw_supply (regcache,
		       mips_regnum (target_gdbarch)->fp_control_status,
		       data + FPCR31_OFF);
}

static void
mipsnto_supply_fpregset (struct regcache *regcache, const gdb_byte *data)
{
  mipsnto_supply_reg_fpregset (regcache, NTO_ALL_REGS, data);
}

static void
mipsnto_supply_regset (struct regcache *regcache, int regset,
		       const gdb_byte *data)
{
  nto_trace (0) ("%s ()\n", __func__);
  switch (regset)
    {
    case NTO_REG_GENERAL:
      mipsnto_supply_gregset (regcache, data);
      break;
    case NTO_REG_FLOAT:
      mipsnto_supply_fpregset (regcache, data);
      break;
    }
}

static int
mipsnto_regset_id( int regno )
{
  if (regno == -1)
    return NTO_REG_END;
  else if (regno < gdbarch_fp0_regnum (target_gdbarch) )
    return NTO_REG_GENERAL;
  else if (regno >= gdbarch_fp0_regnum (target_gdbarch)
	   && regno <= gdbarch_fp0_regnum (target_gdbarch) + 16)
    return NTO_REG_FLOAT;
  else if (regno == mips_regnum(target_gdbarch)->fp_control_status)
    return NTO_REG_FLOAT;
#ifdef FIRST_COP0_REG
  else if (mips_processor_type
	   && !strcasecmp( "tx79", mips_processor_type))
    {
      if (regno < FIRST_COP0_REG)
	return NTO_REG_ALT;
    }
#endif
	return  -1;
}

static int 
mipsnto_register_area(struct gdbarch *gdbarch,
		      int regno, int regset, unsigned *off)
{
  *off = 0;
  if (regset == NTO_REG_GENERAL)
    {
      if (regno == -1)
	return GP_REGSET_SIZE;

      if(regno < 38) 
        {
	  ULONGEST offset = (regno - MIPS_ZERO_REGNUM) * 8;
	  if (gdbarch_byte_order (target_gdbarch) == BFD_ENDIAN_BIG
	      && register_size (target_gdbarch, MIPS_ZERO_REGNUM) == 4)
	    offset += 4;
	  *off = offset;
        }
      else
	      return 0;
      return 4;
    }
  else if (regset == NTO_REG_FLOAT)
    {
      if (regno == -1)
	return FP_REGSET_SIZE;
      
      if (regno >= gdbarch_fp0_regnum (target_gdbarch)
	  && regno <= gdbarch_fp0_regnum (target_gdbarch) + 32)
        {
	  ULONGEST offset = (regno - gdbarch_fp0_regnum (target_gdbarch)) * 8;
	  if (gdbarch_byte_order (target_gdbarch) == BFD_ENDIAN_BIG
	      && register_size (target_gdbarch, MIPS_ZERO_REGNUM) == 4)
	    offset += 4;

	  *off = offset;
        }
      else if (regno == mips_regnum (target_gdbarch)->fp_control_status)
	      *off = FPCR31_OFF;
      else
	      return 0;
      return 4;
    }
  /* NYI: ALT and tx79 stuffies.  */
  return -1;
}

static int
mipsnto_regset_fill (const struct regcache *regcache, int regset,
		     gdb_byte *data)
{
  int regno, off = 0;
  nto_reg64 *regs = (nto_reg64 *)data;

  nto_trace (0) ("%s ()\n", __func__);

  if (gdbarch_byte_order (target_gdbarch) == BFD_ENDIAN_BIG
      && register_size (target_gdbarch, MIPS_ZERO_REGNUM) == 4)
    off = 4;

  if (regset == NTO_REG_GENERAL)
    {
      for (regno = MIPS_ZERO_REGNUM;
	   regno < gdbarch_fp0_regnum (target_gdbarch); regno++)
	regcache_raw_collect (regcache, regno,
			      regs
			      + (regno - MIPS_ZERO_REGNUM) * sizeof (nto_reg64)
			      + off);
    }
  else if (regset == NTO_REG_FLOAT)
    {
      for (regno = 0; regno < 16; regno++)
	regcache_raw_collect (regcache, regno
			      + gdbarch_fp0_regnum (target_gdbarch),
			      regs + regno * sizeof (nto_reg64) + off);
      regcache_raw_collect (regcache,
			    mips_regnum (target_gdbarch)->fp_control_status,
			    data + FPCR31_OFF);
    }
  else
    return -1;
  return 0;
}

static void
init_mipsnto_ops ()
{
  nto_regset_id = mipsnto_regset_id;
  nto_supply_gregset = mipsnto_supply_gregset;
  nto_supply_fpregset = mipsnto_supply_fpregset;
  nto_supply_altregset = nto_dummy_supply_regset;
  nto_supply_regset = mipsnto_supply_regset;
  nto_register_area = mipsnto_register_area;
  nto_regset_fill = mipsnto_regset_fill;
  nto_fetch_link_map_offsets = nto_generic_svr4_fetch_link_map_offsets;
}

/* */
static int
mips_nto_in_dynsym_resolve_code (CORE_ADDR pc)
{
  gdb_byte buff[24];
  gdb_byte *p = buff + 8;
  ULONGEST instr[] = { 0x8f990000, 0x03200008 };
  ULONGEST instrmask[] = { 0xFFFF0000, 0xFFFFFFFF };
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
      p += 4;

      // first instruction found, see if the following one looks correct:
      if ((extract_unsigned_integer (p, 4, byte_order) & instrmask[1])
	  == instr[1])
        {
          if (extract_unsigned_integer (p + 4, 4, byte_order) == 0
	      && extract_unsigned_integer (p + 8, 4, byte_order) == 0)
	    {
	      nto_trace (0) ("looks like plt code\n");
	      return 1;
	    }
        }
    }
  
  nto_trace (0) ("%s: could not recognize plt code\n", __func__);
  return nto_in_dynsym_resolve_code (pc);
}

/* Core file support */
static void
mipsnto_core_supply_gregset (const struct regset *regset, 
                             struct regcache *regcache,
			     int regnum, const void *preg,
			     size_t len)
{
  nto_trace (0) ("%s ()\n", __func__);
  mipsnto_supply_reg_gregset (regcache, regnum, (const gdb_byte *)preg);
}

static void 
mipsnto_core_supply_fpregset (const struct regset *regset, 
                             struct regcache *regcache,
			     int regnum, const void *preg,
			     size_t len)
{
  nto_trace (0) ("%s ()\n", __func__);
  
  mipsnto_supply_reg_fpregset (regcache, regnum, (const gdb_byte *)preg);
}

/* NetBSD/mips register sets.  */

static struct regset mipsnto_gregset =
{
  NULL,
  mipsnto_core_supply_gregset,
  NULL,
  NULL
};

static struct regset mipsnto_fpregset =
{
  NULL,
  mipsnto_core_supply_fpregset,
  NULL,
  NULL
};

/* Return the appropriate register set for the core section identified
   by SECT_NAME and SECT_SIZE.  */

static const struct regset *
mipsnto_regset_from_core_section (struct gdbarch *gdbarch,
				   const char *sect_name, size_t sect_size)
{
  size_t regsize = mips_isa_regsize (gdbarch);
  
  nto_trace (0) ("%s () sect_name:%s\n", __func__, sect_name);

  if (strcmp (sect_name, ".reg") == 0
      && sect_size >= MIPSNTO_NUM_GREGS * regsize)
    return &mipsnto_gregset;

  if (strcmp (sect_name, ".reg2") == 0
      && sect_size >= MIPSNTO_NUM_FPREGS * regsize)
    return &mipsnto_fpregset;

  gdb_assert (0);
  return NULL;
}

/* Signal trampolines. */

/* Signal trampoline sniffer.  */

static CORE_ADDR
mipsnto_sigcontext_addr (struct frame_info *this_frame)
{
  struct gdbarch *gdbarch = get_frame_arch (this_frame);
  CORE_ADDR ptrctx;
  const unsigned int s1_regno = MIPS_AT_REGNUM + 16;

  nto_trace (0) ("%s ()\n", __func__);

/* we store context address in s1 register; */
  ptrctx = get_frame_register_unsigned (this_frame, s1_regno);

  ptrctx += 24;

  nto_trace (0) ("reg s1: 0x%s\n", paddress (gdbarch, ptrctx));

  nto_trace (0) ("context addr: 0x%s \n", paddress (gdbarch, ptrctx));

  return ptrctx;
}

struct mips_nto_sigtramp_cache
{
  CORE_ADDR base;
  struct trad_frame_saved_reg *saved_regs;
};

static struct mips_nto_sigtramp_cache *
mipsnto_sigtramp_cache (struct frame_info *this_frame, void **this_cache)
{
  CORE_ADDR ptrctx;
  int regi;
  struct mips_nto_sigtramp_cache *cache;
  struct gdbarch *gdbarch = get_frame_arch (this_frame);
  const int REGSIZE = 4;
  const int num_regs = gdbarch_num_regs (gdbarch);
  int off = 0;

  nto_trace (0) ("%s ()\n", __func__);

  if ((*this_cache) != NULL)
    return (*this_cache);
  cache = FRAME_OBSTACK_ZALLOC (struct mips_nto_sigtramp_cache);
  (*this_cache) = cache;
  cache->saved_regs = trad_frame_alloc_saved_regs (this_frame);
  cache->base = get_frame_pc (this_frame);
  ptrctx = mipsnto_sigcontext_addr (this_frame);

  /* retrieve registers */
  /* on big endian, register data is in second word of Neutrino's 8 byte regs */
  if(gdbarch_byte_order (gdbarch) == BFD_ENDIAN_BIG &&
     register_size (gdbarch, MIPS_ZERO_REGNUM) == REGSIZE)
          off = REGSIZE;

  for(regi = MIPS_ZERO_REGNUM; regi < gdbarch_fp0_regnum (gdbarch); regi++)
    {
      /* nto stores registers in 8 byte storage */
      const CORE_ADDR addr = ptrctx + 8 * (regi - MIPS_ZERO_REGNUM) + off;
      cache->saved_regs[regi + num_regs].addr = addr;
    }

#if defined(FIRST_ALTREG) && defined(LAST_ALTREG) //tx79
  /* FIXME */
  //if(mips_processor_type && !strcasecmp("tx79", mips_processor_type))
  //  trad_frame_set_reg_addr (this_cache, FIRST_ALTREG, LAST_ALTREG, (char *)gregsetp );
#endif
  return cache;
}

static void
mipsnto_sigtramp_this_id (struct frame_info *this_frame, void **this_cache,
			  struct frame_id *this_id)
{
  struct mips_nto_sigtramp_cache *info = mipsnto_sigtramp_cache (this_frame,
								 this_cache);
  nto_trace (0) ("%s ()\n", __func__);
  (*this_id) = frame_id_build (info->base, gdbarch_unwind_pc (target_gdbarch,
							      this_frame));
}

static struct value *
mipsnto_sigtramp_prev_register (struct frame_info *this_frame,
				void **this_prologue_cache, int regnum)
{
  struct mips_nto_sigtramp_cache *info
	= mipsnto_sigtramp_cache (this_frame,
				  this_prologue_cache);

  nto_trace (0) ("%s ()\n", __func__);
  trad_frame_get_prev_register (this_frame, info->saved_regs, regnum);
  /* FIXME: */
  return NULL;
}

static int
mipsnto_sigtramp_sniffer (const struct frame_unwind *self,
			  struct frame_info *this_frame,
			  void **this_prologue_cache)
{
  CORE_ADDR pc = get_frame_pc (this_frame);
  char *name;

  nto_trace (0) ("%s ()\n", __func__);

  find_pc_partial_function (pc, &name, NULL, NULL);
  if (name && strcmp ("__signalstub", name) == 0)
    return 1;

  return 0;
}

static const struct frame_unwind mips_nto_sigtramp_unwind =
{
  SIGTRAMP_FRAME,
  default_frame_unwind_stop_reason,
  mipsnto_sigtramp_this_id,
  mipsnto_sigtramp_prev_register,
  NULL,
  mipsnto_sigtramp_sniffer,
  NULL
};


static void
mipsnto_sigtramp_cache_init (const struct tramp_frame *self,
                            struct frame_info *this_frame,
			    struct trad_frame_cache *this_cache,
			    CORE_ADDR func)
{
  struct gdbarch *gdbarch = get_frame_arch (this_frame);
  CORE_ADDR ptrctx, sp;
  int regi, off = 0;
  const int num_regs = gdbarch_num_regs (gdbarch);
  
  nto_trace (0) ("%s () funcaddr=0x%s\n", __func__, paddress (gdbarch, func));

  /* stack pointer for __signal_stub frame */
  sp = get_frame_sp (this_frame);

  nto_trace (0) ("sp: 0x%s\n", paddress (gdbarch, sp));

  /* Construct the frame ID using the function start. */
  trad_frame_set_id (this_cache, frame_id_build (sp, func));
  ptrctx = mipsnto_sigcontext_addr (this_frame); 
  nto_trace (0) ("context addr: 0x%s \n", paddress (gdbarch, ptrctx));

  /* retrieve registers */
  /* on big endian, register data is in second word of Neutrino's 8 byte regs */
  if(gdbarch_byte_order (gdbarch) == BFD_ENDIAN_BIG &&
     register_size (gdbarch, MIPS_ZERO_REGNUM) == 4)
          off = 4;

  for(regi = MIPS_ZERO_REGNUM; regi < gdbarch_fp0_regnum (gdbarch); regi++)
    {
      /* nto stores registers in 8 byte storage */
      const CORE_ADDR addr = ptrctx + 8 * (regi - MIPS_ZERO_REGNUM) + off;
      trad_frame_set_reg_addr (this_cache, regi + num_regs, addr);
    }

#if defined(FIRST_ALTREG) && defined(LAST_ALTREG) //tx79
  /* FIXME */
  //if(mips_processor_type && !strcasecmp("tx79", mips_processor_type))
  //  trad_frame_set_reg_addr (this_cache, FIRST_ALTREG, LAST_ALTREG, (char *)gregsetp );
#endif
}

static struct tramp_frame mipsbe32_nto_sighandler_tramp_frame = {
  SIGTRAMP_FRAME,
  4,
  { 
    { 0x02203020, 0xFFFFFFF0 },
    { 0x02201020, 0xFFFFFFF0 },
    { 0x02002020, 0xFFFFFFF0 },
    { TRAMP_SENTINEL_INSN, -1 },
  },
  mipsnto_sigtramp_cache_init
};

static struct tramp_frame mipsle32_nto_sighandler_tramp_frame = {
  SIGTRAMP_FRAME,
  4,
  { 
    { 0x02203020, 0xFFFFFFF0 },
    { 0x02201020, 0xFFFFFFF0 },
    { 0x02002020, 0xFFFFFFF0 },
    { TRAMP_SENTINEL_INSN, -1 },
  },
  mipsnto_sigtramp_cache_init
};

static void
mipsnto_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  /* Deal with our strange signals.  */
  nto_initialize_signals(gdbarch);

  /* Neutrino rewinds to look more normal.  */
  set_gdbarch_decr_pc_after_break (gdbarch, 0);

  /* NTO has shared libraries.  */
  set_gdbarch_skip_trampoline_code (gdbarch, find_solib_trampoline_target);

  set_solib_svr4_fetch_link_map_offsets (gdbarch,
					 nto_generic_svr4_fetch_link_map_offsets);

  /* Trampoline */
  tramp_frame_prepend_unwinder (gdbarch, &mipsbe32_nto_sighandler_tramp_frame);
  tramp_frame_prepend_unwinder (gdbarch, &mipsle32_nto_sighandler_tramp_frame);

  /* Our loader handles solib relocations slightly differently than svr4.  */
  svr4_so_ops.relocate_section_addresses = nto_relocate_section_addresses;

  /* Supply a nice function to find our solibs.  */
  svr4_so_ops.find_and_open_solib = nto_find_and_open_solib;

  /* Our linker code is in libc.  */
  svr4_so_ops.in_dynsym_resolve_code = mips_nto_in_dynsym_resolve_code;

  set_solib_ops (gdbarch, &svr4_so_ops);

  /* register core handler */
  set_gdbarch_regset_from_core_section (gdbarch, 
					mipsnto_regset_from_core_section);

  init_mipsnto_ops ();
}

void
_initialize_mipsnto_tdep (void)
{
  const struct bfd_arch_info *arch_info;

  nto_trace (0) ("%s ()\n", __func__);

  for (arch_info = bfd_lookup_arch (bfd_arch_mips, 0);
       arch_info != NULL;
       arch_info = arch_info->next)
    {
      gdbarch_register_osabi (bfd_arch_mips, arch_info->mach,
			      GDB_OSABI_QNXNTO, mipsnto_init_abi);
    }
  gdbarch_register_osabi_sniffer (bfd_arch_mips, bfd_target_elf_flavour,
		  		  nto_elf_osabi_sniffer);
}

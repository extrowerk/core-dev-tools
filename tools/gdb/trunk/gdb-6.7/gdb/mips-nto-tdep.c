/* MIPS specific functionality for QNX Neutrino.

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
#include "mips-tdep.h"
#include "nto-tdep.h"
#include "osabi.h"
#include "objfiles.h"
#include "frame.h"

#include "trad-frame.h"
#include "tramp-frame.h"
#include "gdbcore.h"

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
mipsnto_supply_reg_gregset (struct regcache *regcache, int regno,  char *data)
{
  int regi, off = 0;
  nto_reg64 *regs = (nto_reg64 *)data;

  nto_trace (0) ("%s ()\n", __func__);

  /* on big endian, register data is in second word of Neutrino's 8 byte regs */
  if(gdbarch_byte_order (current_gdbarch) == BFD_ENDIAN_BIG &&
     register_size (current_gdbarch, MIPS_ZERO_REGNUM) == 4)
          off = 4;

  for(regi = MIPS_ZERO_REGNUM; regi < gdbarch_fp0_regnum (current_gdbarch); regi++)
    RAW_SUPPLY_IF_NEEDED (regcache, regi, &regs[regi - MIPS_ZERO_REGNUM][off]);

#if defined(FIRST_ALTREG) && defined(LAST_ALTREG) //tx79
  /* FIXME */
  if(mips_processor_type && !strcasecmp("tx79", mips_processor_type))
    RAW_SUPPLY_IF_NEEDED (regcache, FIRST_ALTREG, LAST_ALTREG, (char *)gregsetp );
#endif
}

static void
mipsnto_supply_gregset (struct regcache *regcache, char *data)
{
  mipsnto_supply_reg_gregset (regcache, NTO_ALL_REGS, data);
}

static void
mipsnto_supply_reg_fpregset (struct regcache *regcache, int regno, char *data)
{
  int regi, off = 0;
  nto_reg64 *regs = (nto_reg64 *)data;

  nto_trace (0) ("%s ()\n", __func__);
  
  if(gdbarch_byte_order (current_gdbarch) == BFD_ENDIAN_BIG &&
     register_size (current_gdbarch, MIPS_ZERO_REGNUM) == 4)
          off = 4;
  
  for(regi = gdbarch_fp0_regnum (current_gdbarch); regi < gdbarch_fp0_regnum (current_gdbarch) + 16; regi++)
    regcache_raw_supply (regcache, regi, &regs[regi - gdbarch_fp0_regnum (current_gdbarch)][off]);

  regcache_raw_supply (regcache, mips_regnum(current_gdbarch)->fp_control_status, data + FPCR31_OFF);
}

static void
mipsnto_supply_fpregset (struct regcache *regcache, char *data)
{
  mipsnto_supply_reg_fpregset (regcache, NTO_ALL_REGS, data);
}

static void
mipsnto_supply_regset (struct regcache *regcache, int regset, char *data)
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
	if ( regno == -1 )
		return NTO_REG_END;
	else if( regno < gdbarch_fp0_regnum (current_gdbarch) )
		return NTO_REG_GENERAL;
	else if( regno >= gdbarch_fp0_regnum (current_gdbarch) && regno <= gdbarch_fp0_regnum (current_gdbarch) + 16)
		return  NTO_REG_FLOAT;
	else if (regno == mips_regnum(current_gdbarch)->fp_control_status)
		return NTO_REG_FLOAT;
#ifdef FIRST_COP0_REG
	 else if( mips_processor_type && !strcasecmp( "tx79", mips_processor_type ) )
		if( regno < FIRST_COP0_REG) {
		  return NTO_REG_ALT;
		}
#endif
	return  -1;
}

static int 
mipsnto_register_area(int regno, int regset, unsigned *off)
{
  *off = 0;
  if (regset == NTO_REG_GENERAL)
    {
      if (regno == -1)
	return GP_REGSET_SIZE;

      if(regno < 38)
	      *off = (regno - MIPS_ZERO_REGNUM) * 8 +
		      (gdbarch_byte_order (current_gdbarch) == BFD_ENDIAN_BIG &&
		       register_size (current_gdbarch, MIPS_ZERO_REGNUM) == 4) ? 4 : 0;
      else
	      return 0;
      return 4;
    }
  else if (regset == NTO_REG_FLOAT)
    {
      if (regno == -1)
	return FP_REGSET_SIZE;
      
      if (regno >= gdbarch_fp0_regnum (current_gdbarch) && regno <= gdbarch_fp0_regnum (current_gdbarch) + 32)
	      *off = (regno - gdbarch_fp0_regnum (current_gdbarch)) * 8 +
		      (gdbarch_byte_order (current_gdbarch) == BFD_ENDIAN_BIG &&
		       register_size (current_gdbarch, MIPS_ZERO_REGNUM) == 4) ? 4 : 0;
      else if (regno == mips_regnum(current_gdbarch)->fp_control_status)
	      *off = FPCR31_OFF;
      else
	      return 0;
      return 4;
    }
  /* NYI: ALT and tx79 stuffies.  */
  return -1;
}

static int
mipsnto_regset_fill (const struct regcache *regcache, int regset, char *data)
{
  int regno, off = 0;
  nto_reg64 *regs = (nto_reg64 *)data;

  nto_trace (0) ("%s ()\n", __func__);

  if(gdbarch_byte_order (current_gdbarch) == BFD_ENDIAN_BIG &&
     register_size (current_gdbarch, MIPS_ZERO_REGNUM) == 4)
          off = 4;

  if (regset == NTO_REG_GENERAL)
    {
      for (regno = MIPS_ZERO_REGNUM; regno < gdbarch_fp0_regnum (current_gdbarch); regno++)
	  regcache_raw_collect (regcache, regno, &regs[regno - MIPS_ZERO_REGNUM][off]);
    }
  else if (regset == NTO_REG_FLOAT)
    {
      for (regno = 0; regno < 16; regno++)
	{
	  regcache_raw_collect (regcache, regno + gdbarch_fp0_regnum (current_gdbarch), &regs[regno][off]);
	}
      regcache_raw_collect (regcache, mips_regnum(current_gdbarch)->fp_control_status, data + FPCR31_OFF);
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

  nto_trace (0) ("%s (pc=%s)\n", __func__, paddr (pc));

  read_memory (pc - 8, buff, 24);

  while (p >= buff)
    {
      if ((extract_unsigned_integer (p, 4) & instrmask[0]) == instr[0])
        break;

      p -= 4;
    }

  if (p >= buff)
    {
      p += 4;

      // first instruction found, see if the following one looks correct:
      if ((extract_unsigned_integer (p, 4) & instrmask[1]) == instr[1])
        {
          if (extract_unsigned_integer (p + 4, 4) == 0
      	    && extract_unsigned_integer (p + 8, 4) == 0)
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
  mipsnto_supply_reg_gregset (regcache, regnum, (char *)preg);
}

static void 
mipsnto_core_supply_fpregset (const struct regset *regset, 
                             struct regcache *regcache,
			     int regnum, const void *preg,
			     size_t len)
{
  nto_trace (0) ("%s ()\n", __func__);
  
  mipsnto_supply_reg_fpregset (regcache, regnum, (char *)preg);
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

static void
mipsnto_sigtramp_cache_init (const struct tramp_frame *self,
                            struct frame_info *next_frame,
			    struct trad_frame_cache *this_cache,
			    CORE_ADDR func)
{
  struct gdbarch *gdbarch = get_frame_arch (next_frame);
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);
  CORE_ADDR base, addr;
  int regi, off = 0;
  const int num_regs = gdbarch_num_regs (current_gdbarch);
  
  nto_trace (0) ("%s () funcaddr=0x%s\n", __func__, paddr (func));

  base = frame_unwind_register_unsigned (next_frame,
                                         gdbarch_sp_regnum (current_gdbarch));

  /* Construct the frame ID using the function start. */
  trad_frame_set_id (this_cache, frame_id_build (base, func));

  base += 136; /* offset to context */
 
  /* retrieve registers */
  /* on big endian, register data is in second word of Neutrino's 8 byte regs */
  if(gdbarch_byte_order (current_gdbarch) == BFD_ENDIAN_BIG &&
     register_size (current_gdbarch, MIPS_ZERO_REGNUM) == 4)
          off = 4;

  for(regi = MIPS_ZERO_REGNUM; regi < gdbarch_fp0_regnum (current_gdbarch); regi++)
    {
      /* nto stores registers in 8 byte storage */
      const CORE_ADDR addr = base + 8 * (regi - MIPS_ZERO_REGNUM) + off;

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
    { 0x0220102d, 0xFFFFFFFF }, 
    { 0x0200202d, 0xFFFFFFFF }, 
    { 0x04100001, 0xFFFFFFFF },
    { TRAMP_SENTINEL_INSN, -1 },
  },
  mipsnto_sigtramp_cache_init
};



static void
mipsnto_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  /* Deal with our strange signals.  */
  nto_initialize_signals();

  /* Neutrino rewinds to look more normal.  */
  set_gdbarch_decr_pc_after_break (gdbarch, 0);

  /* NTO has shared libraries.  */
  //set_gdbarch_in_solib_call_trampoline (gdbarch, in_plt_section);
  set_gdbarch_skip_trampoline_code (gdbarch, find_solib_trampoline_target);

  set_solib_svr4_fetch_link_map_offsets (gdbarch,
					 nto_generic_svr4_fetch_link_map_offsets);

  /* Trampoline */
  tramp_frame_prepend_unwinder (gdbarch, &mipsbe32_nto_sighandler_tramp_frame);

  /* Our loader handles solib relocations slightly differently than svr4.  */
  TARGET_SO_RELOCATE_SECTION_ADDRESSES = nto_relocate_section_addresses;

  /* Supply a nice function to find our solibs.  */
  TARGET_SO_FIND_AND_OPEN_SOLIB = nto_find_and_open_solib;

  /* Our linker code is in libc.  */
  TARGET_SO_IN_DYNSYM_RESOLVE_CODE = mips_nto_in_dynsym_resolve_code;

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

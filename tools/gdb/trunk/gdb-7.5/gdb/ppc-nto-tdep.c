/* PPC specific functionality for QNX Neutrino.

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
#include "gdbtypes.h"
#include "osabi.h"
#include "frame.h"
#include "target.h"
#include "gdbcore.h"
#include "regcache.h"
#include "regset.h"
#include "trad-frame.h"
#include "tramp-frame.h"

#include "gdb_assert.h"
#include "gdb_string.h"

#include "solib-svr4.h"
#include "ppc-tdep.h"
#include "nto-tdep.h"
#include "osabi.h"
#include "trad-frame.h"
#include "frame.h"
#include "frame-unwind.h"
#include "objfiles.h"
#include "solib.h"

#define NTO_PPC_REGSIZE 4

/* 32 gp regs + ctr, lr, msr and iar.  Also 5 more 32 bit regs cr, xer, ear, mq and vrsave. */
#define GP_REGSET_SIZE ((32 + 4) * NTO_PPC_REGSIZE + 5 * 4)
#define CTR_OFF (32 * NTO_PPC_REGSIZE)
#define LR_OFF (33 * NTO_PPC_REGSIZE)
#define MSR_OFF (34 * NTO_PPC_REGSIZE)
#define IAR_OFF (35 * NTO_PPC_REGSIZE)
#define CR_OFF (IAR_OFF + 1 * 4)
#define XER_OFF (IAR_OFF + 2 * 4)
#define EAR_OFF (IAR_OFF + 3 * 4)
#define MQ_OFF (IAR_OFF + 4 * 4)
#define VRSAVE_OFF (IAR_OFF + 5 * 4)

/* 32 general fp regs + 1 fpscr reg */
#define FP_REG_SIZE (8)
#define FP_REGSET_SIZE (33 * FP_REG_SIZE)
#define FPSCR_OFF (32 * FP_REG_SIZE)
#define FPSCR_VAL_OFF (32 * FP_REG_SIZE + 4)

/* 32 Altivec regs + vscr  */
#define PPCVMX_REGSIZE (16)
#define PPCVMX_NUMREGS (32)
#define ALTIVEC_REGSET_SIZE (PPCVMX_NUMREGS * PPCVMX_REGSIZE + PPCVMX_REGSIZE)
#define VSCR_OFF (PPCVMX_NUMREGS * PPCVMX_REGSIZE)
#define PPC_VSCR_REGNUM (PPCVMX_NUMREGS + 1)

/* SPE registers */
#define ACC_REG_SIZE (8)
#define SPE_GPR_HI_REG_SIZE (4)
#define SPE_GPR_HI_OFF (ACC_REG_SIZE)
#define SPE_REGSET_SIZE (ACC_REG_SIZE + 32 * SPE_GPR_HI_REG_SIZE)

#define GPLAST_REGNUM (tdep->ppc_gp0_regnum + ppc_num_gprs)
#define FPLAST_REGNUM (tdep->ppc_fp0_regnum + ppc_num_fprs)

#if 0
static CORE_ADDR
ppc_nto_skip_trampoline_code (struct frame_info *frame, CORE_ADDR pc)
{
  enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  gdb_byte buf[4];
  struct obj_section *sect;
  struct objfile *objfile;
  unsigned long insn;
  CORE_ADDR plt_start = 0;
  CORE_ADDR symtab = 0;
  CORE_ADDR strtab = 0;
  int num_slots = -1;
  int reloc_index = -1;
  CORE_ADDR plt_table;
  CORE_ADDR reloc;
  CORE_ADDR sym;
  long symidx;
  char symname[1024];
  struct minimal_symbol *msymbol;
  
  nto_trace (0) ("%s () pc=0x%s\n", __func__, paddress (target_gdbarch, pc));

  /* Find the section pc is in; return if not in .plt */
  sect = find_pc_section (pc);
  if (!sect || strcmp (sect->the_bfd_section->name, ".plt") != 0)
    return 0;

  objfile = sect->objfile;

  /* Pick up the instruction at pc.  It had better be of the
     form
     li r11, IDX

     where IDX is an index into the plt_table.  */

  if (target_read_memory (pc, buf, 4) != 0)
    return 0;
  insn = extract_unsigned_integer (buf, 4, byte_order);

  if ((insn & 0xffff0000) != 0x39600000 /* li r11, VAL */ )
    return 0;
  reloc_index = (insn << 16) >> 16;

  /* Find the objfile that pc is in and obtain the information
     necessary for finding the symbol name. */
  for (sect = objfile->sections; sect < objfile->sections_end; ++sect)
    {
      const char *secname = sect->the_bfd_section->name;
      if (strcmp (secname, ".plt") == 0)
	plt_start = sect->addr;
      else if (strcmp (secname, ".rela.plt") == 0)
	num_slots = ((int) sect->the_bfd_section->endaddr - (int) sect->addr) / 12;
      else if (strcmp (secname, ".dynsym") == 0)
	symtab = sect->addr;
      else if (strcmp (secname, ".dynstr") == 0)
	strtab = sect->addr;
    }

  /* Make sure we have all the information we need. */
  if (plt_start == 0 || num_slots == -1 || symtab == 0 || strtab == 0)
    return 0;

  /* Compute the value of the plt table */
  plt_table = plt_start + 72 + 8 * num_slots;

  /* Get address of the relocation entry (Elf32_Rela) */
  if (target_read_memory (plt_table + reloc_index, buf, 4) != 0)
    return 0;
  reloc = extract_unsigned_integer (buf, 4, byte_order);

  sect = find_pc_section (reloc);
  if (!sect)
    return 0;

  if (strcmp (sect->the_bfd_section->name, ".text") == 0)
    return reloc;

  /* Now get the r_info field which is the relocation type and symbol
     index. */
  if (target_read_memory (reloc + 4, buf, 4) != 0)
    return 0;
  symidx = extract_unsigned_integer (buf, 4, byte_order);

  /* Shift out the relocation type leaving just the symbol index */
  /* symidx = ELF32_R_SYM(symidx); */
  symidx = symidx >> 8;

  /* compute the address of the symbol */
  sym = symtab + symidx * 4;

  /* Fetch the string table index */
  if (target_read_memory (sym, buf, 4) != 0)
    return 0;
  symidx = extract_unsigned_integer (buf, 4, byte_order);

  /* Fetch the string; we don't know how long it is.  Is it possible
     that the following will fail because we're trying to fetch too
     much? */
  if (target_read_memory (strtab + symidx, (gdb_byte *)symname,
			  sizeof (symname)) != 0)
    return 0;

  /* This might not work right if we have multiple symbols with the
     same name; the only way to really get it right is to perform
     the same sort of lookup as the dynamic linker. */
  msymbol = lookup_minimal_symbol_text (symname, NULL);
  if (!msymbol)
    return 0;

  return SYMBOL_VALUE_ADDRESS (msymbol);
}
#endif


/* Signal tampoline sniffer.  */

struct ppc_nto_sigtramp_cache
{
  CORE_ADDR base;
  struct trad_frame_saved_reg *saved_regs;
};

/* The context is calculated the same way regardless of the sniffer method. */
static CORE_ADDR
ppcnto_sigcontext_addr (struct frame_info *this_frame)
{
  struct gdbarch *gdbarch = get_frame_arch (this_frame);
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);
  CORE_ADDR ptrctx;

  nto_trace (0) ("%s ()\n", __func__);

  /* Read base from r31 of the sigtramp frame (see ppc/sigstub.S)  */
  ptrctx = get_frame_register_unsigned (this_frame, tdep->ppc_gp0_regnum + 31);
  nto_trace (0) ("context addr: %s\n", paddress (gdbarch, ptrctx));
  if (ptrctx == 0)
    warning ("Unable to retrieve sigstack_context pointer.");
  return ptrctx;
}

static struct ppc_nto_sigtramp_cache *
ppc_nto_sigtramp_cache (struct frame_info *next_frame, void **this_cache)
{
  CORE_ADDR gpregs;
  CORE_ADDR fpregs;
  int i;
  struct ppc_nto_sigtramp_cache *cache;
  struct gdbarch *gdbarch = get_frame_arch (next_frame);
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  nto_trace (0) ("%s ()\n", __func__);

  if ((*this_cache) != NULL)
    return (*this_cache);
  cache = FRAME_OBSTACK_ZALLOC (struct ppc_nto_sigtramp_cache);
  (*this_cache) = cache;
  cache->saved_regs = trad_frame_alloc_saved_regs (next_frame);
  cache->base = frame_unwind_register_unsigned (next_frame,
						gdbarch_pc_regnum (gdbarch));
  gpregs = ppcnto_sigcontext_addr (next_frame);
  fpregs = gpregs + GP_REGSET_SIZE;

  /* General purpose.  */
  for (i = 0; i < ppc_num_gprs; i++)
    {
      int regnum = i + tdep->ppc_gp0_regnum;
      cache->saved_regs[regnum].addr = gpregs + i * NTO_PPC_REGSIZE;
    }
  cache->saved_regs[gdbarch_pc_regnum (gdbarch)].addr = gpregs + IAR_OFF;
  cache->saved_regs[tdep->ppc_ctr_regnum].addr = gpregs + CTR_OFF;
  cache->saved_regs[tdep->ppc_lr_regnum].addr = gpregs + LR_OFF;
  cache->saved_regs[tdep->ppc_xer_regnum].addr = gpregs + XER_OFF;
  cache->saved_regs[tdep->ppc_cr_regnum].addr = gpregs + CR_OFF;

  /* Floating point registers.  */
  if (ppc_floating_point_unit_p (gdbarch))
    {
      for (i = 0; i < ppc_num_fprs; i++)
        {
          int regnum = i + tdep->ppc_fp0_regnum;
          cache->saved_regs[regnum].addr = fpregs + i * FP_REG_SIZE;
        }
      cache->saved_regs[tdep->ppc_fpscr_regnum].addr
        = fpregs + FPSCR_OFF;
    }

  return cache;
}

static void
ppc_nto_sigtramp_this_id (struct frame_info *this_frame,
			  void **this_prologue_cache,
			  struct frame_id *this_id)
{
  struct ppc_nto_sigtramp_cache *info
    = ppc_nto_sigtramp_cache (this_frame, this_prologue_cache);

  nto_trace (0) ("%s ()\n", __func__);
  (*this_id) = frame_id_build (info->base, gdbarch_unwind_pc (target_gdbarch,
							      this_frame));
}

static struct value *
ppc_nto_sigtramp_prev_register (struct frame_info *this_frame,
				void **this_cache,
				int regnum)
{
  struct ppc_nto_sigtramp_cache *info
    = ppc_nto_sigtramp_cache (this_frame, this_cache);

  nto_trace (0) ("%s ()\n", __func__);
  trad_frame_get_prev_register (this_frame, info->saved_regs, regnum);
  /* FIXME */
  return NULL;
}

static int
ppc_nto_sigtramp_sniffer (const struct frame_unwind *self,
			  struct frame_info *this_frame,
			  void **this_prologue_cache)
{
  const CORE_ADDR pc = gdbarch_unwind_pc (target_gdbarch, this_frame);

  nto_trace (0) ("%s ()\n", __func__);
  if (pc > frame_unwind_register_unsigned (this_frame,
					   gdbarch_sp_regnum (target_gdbarch)))
    {
      const CORE_ADDR frame_func = pc;
      char *func_name = "";

      if (frame_func)
        find_pc_partial_function (frame_func, &func_name, NULL, NULL);
      nto_trace (0) ("get_frame_func returned: 0x%s %s\n",
		     paddress (target_gdbarch, frame_func),
		     func_name ? func_name : "(null)");
      if (!func_name || func_name[0] == '\0')
        return 0;
      /* see if this is __signalstub function: */
      if (0 == strcmp (func_name, "__signalstub"))
        {
	  nto_trace (0) ("This is signal trampoline frame\n");
	  return 1;
	}
    }

  return 0;
}

static const struct frame_unwind ppc_nto_sigtramp_unwind =
{
  SIGTRAMP_FRAME,
  default_frame_unwind_stop_reason,
  ppc_nto_sigtramp_this_id,
  ppc_nto_sigtramp_prev_register,
  NULL,
  ppc_nto_sigtramp_sniffer
};

/*****************************************************************************/

static void
ppcnto_supply_reg_gregset (struct regcache *regcache, int regno,
			   const gdb_byte *data)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (target_gdbarch);
  int regi;

  nto_trace (0) ("%s ()\n", __func__);
  for (regi = tdep->ppc_gp0_regnum; regi < GPLAST_REGNUM; regi++)
    {
      RAW_SUPPLY_IF_NEEDED (regcache, regi,
		     data + (regi - tdep->ppc_gp0_regnum) * NTO_PPC_REGSIZE);
    }

  /* fill in between FIRST_UISA_SP_REGNUM and LAST_UISA_SP_REGNUM */
  RAW_SUPPLY_IF_NEEDED (regcache, tdep->ppc_ctr_regnum, data + CTR_OFF);
  RAW_SUPPLY_IF_NEEDED (regcache, tdep->ppc_lr_regnum, data + LR_OFF);
  RAW_SUPPLY_IF_NEEDED (regcache, tdep->ppc_ps_regnum, data + MSR_OFF);
  RAW_SUPPLY_IF_NEEDED (regcache, gdbarch_pc_regnum (target_gdbarch),
			data + IAR_OFF);
  RAW_SUPPLY_IF_NEEDED (regcache, tdep->ppc_cr_regnum, data + CR_OFF);
  RAW_SUPPLY_IF_NEEDED (regcache, tdep->ppc_xer_regnum, data + XER_OFF);
  /* RAW_SUPPLY_IF_NEEDED (current_regcache, tdep->???, (char *) (&regp->ear)); */
  if (tdep->ppc_mq_regnum != -1)
    RAW_SUPPLY_IF_NEEDED (regcache, tdep->ppc_mq_regnum, data + MQ_OFF);
  if (tdep->ppc_vrsave_regnum != -1)
    RAW_SUPPLY_IF_NEEDED (regcache, tdep->ppc_vrsave_regnum, data + VRSAVE_OFF);
  /* Note: vrsave and spefscr share the same space. Only one or the other
     is present on a given cpu. */
  if (tdep->ppc_spefscr_regnum != -1)
    RAW_SUPPLY_IF_NEEDED (regcache, tdep->ppc_spefscr_regnum, data + VRSAVE_OFF);
}

static void
ppcnto_supply_gregset (struct regcache *regcache, const gdb_byte *data)
{
  ppcnto_supply_reg_gregset (regcache, NTO_ALL_REGS, data);
}

static void
ppcnto_supply_reg_fpregset (struct regcache *regcache, int regno,
			    const gdb_byte *data)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (target_gdbarch);
  int regi;

  nto_trace (0) ("%s ()\n", __func__);
  for (regi = 0; regi < 32; regi++)
    RAW_SUPPLY_IF_NEEDED (regcache,
                          gdbarch_fp0_regnum (target_gdbarch) + regi,
			  data + regi * FP_REG_SIZE);
  RAW_SUPPLY_IF_NEEDED (regcache, tdep->ppc_fpscr_regnum, data + FPSCR_OFF);
}

static void
ppcnto_supply_fpregset (struct regcache *regcache, const gdb_byte *data)
{
  ppcnto_supply_reg_fpregset (regcache, NTO_ALL_REGS, data);
}

static void
ppcnto_supply_reg_altregset (struct regcache *regcache, int regno,
			     const gdb_byte *data)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (target_gdbarch);
  int regi;

  nto_trace (0) ("%s ()\n", __func__);

  /* Altivec */
  if (tdep->ppc_vr0_regnum > -1)
    {
      for (regi = 0; regi < PPCVMX_NUMREGS; ++regi)
	RAW_SUPPLY_IF_NEEDED (regcache, regi + tdep->ppc_vr0_regnum,
			      data + regi * PPCVMX_REGSIZE);
      RAW_SUPPLY_IF_NEEDED (regcache, PPC_VSCR_REGNUM,
			    data + VSCR_OFF);
    }
  /* SPE */
  else if (tdep->ppc_ev0_regnum > -1)
    {
      RAW_SUPPLY_IF_NEEDED (regcache, tdep->ppc_acc_regnum, data);

      for (regi = tdep->ppc_ev0_upper_regnum; regi < 32; ++regi)
	RAW_SUPPLY_IF_NEEDED (regcache, regi, data
			      + ACC_REG_SIZE + regi * SPE_GPR_HI_REG_SIZE);
    }
}

static void
ppcnto_supply_altregset (struct regcache *regcache, const gdb_byte *data)
{
  ppcnto_supply_reg_altregset (regcache, NTO_ALL_REGS, data);
}

static void
ppcnto_supply_regset (struct regcache *regcache, int regset,
		      const gdb_byte *data)
{
  nto_trace (0) ("%s () regset=%d\n", __func__, regset);

  switch (regset)
    {
    case NTO_REG_GENERAL:
      ppcnto_supply_gregset (regcache, data);
      break;
    case NTO_REG_FLOAT:
      ppcnto_supply_fpregset (regcache, data);
      break;
    case NTO_REG_ALT:
      ppcnto_supply_altregset (regcache, data);
      break;
    default:
      gdb_assert (0);
    }
}

static int
ppcnto_regset_id (int regno)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (target_gdbarch);

  /* Floating point registers.  */
  if (ppc_floating_point_unit_p (target_gdbarch)) {
    if (regno == tdep->ppc_fpscr_regnum
        || (regno >= gdbarch_fp0_regnum (target_gdbarch)
	    && regno < FPLAST_REGNUM))
      return NTO_REG_FLOAT;
  }

  /* General purpose registers.  */
  if (regno < GPLAST_REGNUM)
    return NTO_REG_GENERAL;

#define NTOGREG(x) if (regno == (x)) return NTO_REG_GENERAL
  NTOGREG(gdbarch_pc_regnum (target_gdbarch));
  NTOGREG(gdbarch_sp_regnum (target_gdbarch));
  NTOGREG(tdep->ppc_ctr_regnum);
  NTOGREG(tdep->ppc_lr_regnum);
  NTOGREG(tdep->ppc_cr_regnum);
  NTOGREG(tdep->ppc_xer_regnum);
  NTOGREG(tdep->ppc_ps_regnum);
  NTOGREG(tdep->ppc_mq_regnum);
  NTOGREG(tdep->ppc_vrsave_regnum);
  NTOGREG(tdep->ppc_spefscr_regnum);
#undef NTOGREG

  /* Altivec and SPE registers.  */

  /* Note that QNX register sets are
     organized slightly different than in gdb:
     - for SPE registers, spefscr is in GP regset on QNX.
     - for Altivec vrsave register is in GP regset on QNX.
     Both cases are being taken care of above, so it is safe
     to use functions from rs6000-tdep.c.  */

  if (spe_register_p (target_gdbarch, regno))
    return NTO_REG_ALT;
  else if (altivec_register_p (target_gdbarch, regno))
    return NTO_REG_ALT;

  return -1;
}

static int
ppcnto_register_area (struct gdbarch *gdbarch,
		      int regno, int regset, unsigned *off)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (target_gdbarch);

  nto_trace (0) ("%s () regno=%d regset=%d\n", __func__, regno, regset);

  *off = 0;
  if (regset == NTO_REG_GENERAL)
    {
      if (regno == -1)
	return GP_REGSET_SIZE;

      if (regno < GPLAST_REGNUM)
	*off = regno * NTO_PPC_REGSIZE;
      else
	{
	  if (regno == tdep->ppc_ctr_regnum)
	    *off = CTR_OFF;
	  else if (regno == tdep->ppc_lr_regnum)
	    *off = LR_OFF;
	  else if (regno == tdep->ppc_ps_regnum)
	    *off = MSR_OFF;
	  else if (regno == gdbarch_pc_regnum (target_gdbarch))
	    *off = IAR_OFF;
	  else if (regno == tdep->ppc_cr_regnum)
	    *off = CR_OFF;
	  else if (regno == tdep->ppc_xer_regnum)
	    *off = XER_OFF;
	  else if (regno == tdep->ppc_mq_regnum)
	    *off = MQ_OFF;
	  else if (regno == tdep->ppc_vrsave_regnum)
	    *off = VRSAVE_OFF;
	  else if (regno == tdep->ppc_spefscr_regnum)
	    *off = VRSAVE_OFF; /* spefscr and vrsave share the same storage. */
	  else
	    return 0;
	}
      return NTO_PPC_REGSIZE;
    }
  else if (regset == NTO_REG_FLOAT)
    {
      if (regno == -1)
	return FP_REGSET_SIZE;

      if (regno < FPLAST_REGNUM)
	{
	  *off = (regno - gdbarch_fp0_regnum (target_gdbarch)) * FP_REG_SIZE;
	  return FP_REG_SIZE;
	}
      else
	{
	  if (regno == tdep->ppc_fpscr_regnum)
	    {
	      *off = FPSCR_OFF;
	      return FP_REG_SIZE;
	    }
	  else
	    return 0;
	}
    }
  else if (regset == NTO_REG_ALT)
    {
      int reg_size = 0;

      if (regno == -1)
	{
	  *off =  0;
	  if (tdep->ppc_vr0_regnum != -1)
	    return ALTIVEC_REGSET_SIZE;
	  else if (tdep->ppc_ev0_regnum != -1)
	    return SPE_REGSET_SIZE;
	}

      /* Altivec.  */
      if (altivec_register_p (target_gdbarch, regno))
	{
	  *off = (regno - tdep->ppc_vr0_regnum) * PPCVMX_REGSIZE;
	  reg_size = PPCVMX_REGSIZE;
	}
      /* SPE */
      else if (spe_register_p (target_gdbarch, regno))
        {
	  if (regno == tdep->ppc_acc_regnum)
	    {
	      *off = 0;
	      reg_size = ACC_REG_SIZE;
	    }
	  else
	    {
	      *off = (regno - tdep->ppc_ev0_upper_regnum) * SPE_GPR_HI_REG_SIZE
		     + SPE_GPR_HI_OFF;
	      reg_size = SPE_GPR_HI_REG_SIZE;
	    }
        }
      return reg_size;
    }

  return -1;
}

static int
ppcnto_regset_fill (const struct regcache *regcache, int regset,
		    gdb_byte *data)
{
  int regno;
  struct gdbarch_tdep *tdep = gdbarch_tdep (target_gdbarch);

  if (regset == NTO_REG_GENERAL)
    {
      for (regno = tdep->ppc_gp0_regnum; regno < GPLAST_REGNUM;
	   regno++)
	regcache_raw_collect (regcache, regno, data
			      + (regno - tdep->ppc_gp0_regnum)
			      * NTO_PPC_REGSIZE);

      regcache_raw_collect (regcache, tdep->ppc_ctr_regnum, data + CTR_OFF);
      regcache_raw_collect (regcache, tdep->ppc_lr_regnum, data + LR_OFF);
      regcache_raw_collect (regcache, tdep->ppc_ps_regnum, data + MSR_OFF);
      regcache_raw_collect (regcache, gdbarch_pc_regnum (target_gdbarch),
			    data + IAR_OFF);
      regcache_raw_collect (regcache, tdep->ppc_cr_regnum, data + CR_OFF);
      regcache_raw_collect (regcache, tdep->ppc_xer_regnum, data + XER_OFF);
      /* regcache_raw_collect (current_regcache, tdep->???,
	 (char *) (&regp->ear)); */
      if(tdep->ppc_mq_regnum != -1)
	regcache_raw_collect (regcache, tdep->ppc_mq_regnum, data + MQ_OFF);
      if (tdep->ppc_vrsave_regnum != -1)
	regcache_raw_collect (regcache, tdep->ppc_vrsave_regnum, data + VRSAVE_OFF);
      if (tdep->ppc_spefscr_regnum != -1)
	regcache_raw_collect (regcache, tdep->ppc_spefscr_regnum,
			      data + VRSAVE_OFF); /* vrsave and spefscr share
						     storage. */ }
  else if (regset == NTO_REG_FLOAT)
    {
      for (regno = 0; regno < 32; regno++)
	regcache_raw_collect (regcache, gdbarch_fp0_regnum (target_gdbarch)
			      + regno, data + regno * FP_REG_SIZE);
      regcache_raw_collect (regcache,
			    tdep->ppc_fpscr_regnum, data + FPSCR_OFF);
    }
  else if (regset == NTO_REG_ALT)
    {
      /* Altivec: */
      if (tdep->ppc_vrsave_regnum != -1)
	{
	  int regi;

	  for (regi = 0; regi < PPCVMX_NUMREGS; ++regi)
	    regcache_raw_collect (regcache, tdep->ppc_vr0_regnum + regi,
				  data + PPCVMX_REGSIZE * regi);
	  regcache_raw_collect (regcache, PPC_VSCR_REGNUM,
				data + VSCR_OFF);
	}
      /* SPE: */
      if (tdep->ppc_spefscr_regnum != -1)
	{
	  int regi;

	  /* Acc: */
	  regcache_raw_collect (regcache, tdep->ppc_acc_regnum, data);
	  /* GPR HI registers */
	  for (regi = 0; regi < 32; ++regi)
	    regcache_raw_collect (regcache, tdep->ppc_ev0_upper_regnum + regi,
				  data + SPE_GPR_HI_OFF
				  + regi * SPE_GPR_HI_REG_SIZE);
	}
    }
  else
    return -1;

  return 0;
}

static const char *
ppcnto_variant_directory_suffix (void)
{
  struct bfd_arch_info const *info = 
    gdbarch_bfd_arch_info (target_gdbarch);

  if (info->mach == bfd_mach_ppc_e500)
    {
      nto_trace (1) ("Selecting -spe variant\n");
      return "-spe";
    }

  return "";
}

static void
init_ppcnto_ops ()
{
  nto_regset_id = ppcnto_regset_id;
  nto_supply_gregset = ppcnto_supply_gregset;
  nto_supply_fpregset = ppcnto_supply_fpregset;
  nto_supply_altregset = ppcnto_supply_altregset;
  nto_supply_regset = ppcnto_supply_regset;
  nto_register_area = ppcnto_register_area;
  nto_regset_fill = ppcnto_regset_fill;
  nto_fetch_link_map_offsets = nto_generic_svr4_fetch_link_map_offsets;
  nto_variant_directory_suffix = ppcnto_variant_directory_suffix;
}

/* Core file support */

static void
ppcnto_core_supply_gregset (const struct regset *regset,
                             struct regcache *regcache,
			     int regnum, const void *gpreg,
			     size_t len)
{
  int regset_id;

  nto_trace (0) ("%s () regnum: %d\n", __func__, regnum);

  if (regnum == NTO_ALL_REGS) // all registers
    {
      ppcnto_supply_regset (regcache, NTO_REG_GENERAL, (const gdb_byte *)gpreg);
    }
  else
    {
      regset_id = ppcnto_regset_id (regnum);
      nto_trace (0) ("nto_regset_id=%d\n", regset_id);
      ppcnto_supply_reg_gregset (regcache, regnum, (const gdb_byte *)gpreg);
    }
}

static void
ppcnto_core_supply_fpregset (const struct regset *regset,
                             struct regcache *regcache,
			     int regnum, const void *fpreg,
			     size_t len)
{
  int regset_id;
  nto_trace (0) ("%s () regnum: %d\n", __func__, regnum);

  if (regnum == NTO_ALL_REGS)
    {
      ppcnto_supply_regset (regcache, NTO_REG_FLOAT, (const gdb_byte *)fpreg);
    }
  else
    {
      regset_id = ppcnto_regset_id (regnum);
      nto_trace (0) ("nto regset_id=%d\n", regset_id);
      ppcnto_supply_reg_fpregset (regcache, regnum, (const gdb_byte *)fpreg);
    }
}


struct regset ppcnto_gregset =
{
  NULL,
  ppcnto_core_supply_gregset,
  NULL,
  NULL
};

struct regset ppcnto_fpregset =
{
  NULL,
  ppcnto_core_supply_fpregset,
  NULL,
  NULL
};

/* Return the appropriate register set for the core section identified
   by SECT_NAME and SECT_SIZE.  */

static const struct regset *
ppcnto_regset_from_core_section (struct gdbarch *gdbarch,
				  const char *sect_name, size_t sect_size)
{
  nto_trace (0) ("%s () sect_name:%s\n", __func__, sect_name);
  if (strcmp (sect_name, ".reg") == 0 && sect_size >= 148)
    return &ppcnto_gregset;

  if (strcmp (sect_name, ".reg2") == 0 && sect_size >= 264)
    return &ppcnto_fpregset;

  gdb_assert (0);
  return NULL;
}

/* Signal trampolines. */

static void
ppcnto_sigtramp_cache_init (const struct tramp_frame *self,
                            struct frame_info *this_frame,
			    struct trad_frame_cache *this_cache,
			    CORE_ADDR func)
{
  struct gdbarch *gdbarch = get_frame_arch (this_frame);
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);
  CORE_ADDR ptrctx, sp;
  int i;

  nto_trace (0) ("%s () funcaddr=0x%s\n", __func__, paddress (gdbarch, func));

  sp = get_frame_register_unsigned (this_frame, gdbarch_sp_regnum (gdbarch));

  nto_trace (0) ("sp: 0x%s\n", paddress (gdbarch, sp));

  /* Construct the frame ID using the function start. */
  trad_frame_set_id (this_cache, frame_id_build (sp, func));

  /* Get ucontext address.  */
  ptrctx = ppcnto_sigcontext_addr (this_frame);

  for (i = 0; i != ppc_num_gprs; i++)
    {
      const int regnum = i + tdep->ppc_gp0_regnum;
      const int addr = ptrctx + i * tdep->wordsize;
      trad_frame_set_reg_addr (this_cache, regnum, addr);
    }

  trad_frame_set_reg_addr (this_cache, tdep->ppc_ctr_regnum,
			   ptrctx + CTR_OFF);
  trad_frame_set_reg_addr (this_cache, tdep->ppc_lr_regnum,
			   ptrctx + LR_OFF);
  trad_frame_set_reg_addr (this_cache, tdep->ppc_ps_regnum,
			   ptrctx + MSR_OFF);
  trad_frame_set_reg_addr (this_cache, gdbarch_pc_regnum (gdbarch),
			   ptrctx + IAR_OFF);
  trad_frame_set_reg_addr (this_cache, tdep->ppc_cr_regnum,
			   ptrctx + CR_OFF);
  trad_frame_set_reg_addr (this_cache, tdep->ppc_xer_regnum,
			   ptrctx + XER_OFF);
//  trad_frame_set_reg_addr (this_cache, ??? tdep->ppc_ear_regnum, base + EAR_OFF);
  /* FIXME: mq is only on the 601 - should we check? */
  if (tdep->ppc_mq_regnum != -1)
    trad_frame_set_reg_addr (this_cache, tdep->ppc_mq_regnum,
			     ptrctx + MQ_OFF);

  /* Altivec */
  if (tdep->ppc_vrsave_regnum != -1)
    trad_frame_set_reg_addr (this_cache, tdep->ppc_vrsave_regnum,
			     ptrctx + VRSAVE_OFF);
  /* SPE */
  if (tdep->ppc_spefscr_regnum != -1)
    trad_frame_set_reg_addr (this_cache, tdep->ppc_spefscr_regnum,
			     ptrctx + VRSAVE_OFF); /* vrsave and spefscr share
						      storage */
}


static struct tramp_frame ppc32_nto_sighandler_tramp_frame_630 = {
  SIGTRAMP_FRAME,
  4,
  {
    { 0x7fc3f378, 0xFFFFFFFF }, /* mr r3,r30 */
    { 0x801f0084, 0xFFFFFFFF }, /* lwz r0,132(r31) */
    { 0x7c0803a6, 0xFFFFFFFF }, /* mtctr r0 */
    { TRAMP_SENTINEL_INSN, -1 },
  },
  ppcnto_sigtramp_cache_init
};

static struct tramp_frame ppc32_nto_sighandler_tramp_frame_632 = {
  SIGTRAMP_FRAME,
  4,
  {
    { 0x7fc3f378, 0xFFFFFFFF }, /* mr r3, r30 */
    { 0x73bd0400, 0xFFFFFFFF }, /* andi. r29,r29,1024*/
    { 0x41a20118, 0xFFFFFFFF }, /* beq+ 1bc88 <__signalstub+0x314> */
    { TRAMP_SENTINEL_INSN, -1 },
  },
  ppcnto_sigtramp_cache_init
};

static void
ppcnto_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  //struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);
  nto_trace (0) ("%s ()\n", __func__);
  /* Deal with our strange signals.  */
  nto_initialize_signals(gdbarch);

  /* Neutrino rewinds to look more normal.  */
  set_gdbarch_decr_pc_after_break (gdbarch, 0);

  /* NTO has shared libraries.  */
  //set_gdbarch_in_solib_call_trampoline (gdbarch, in_plt_section);
  //set_gdbarch_skip_trampoline_code (gdbarch, find_solib_trampoline_target);
  //  set_gdbarch_skip_trampoline_code (gdbarch, ppc_nto_skip_trampoline_code);

  set_solib_svr4_fetch_link_map_offsets (gdbarch,
					 nto_generic_svr4_fetch_link_map_offsets);

  /* Trampoline */
  tramp_frame_prepend_unwinder (gdbarch, &ppc32_nto_sighandler_tramp_frame_630);
  tramp_frame_prepend_unwinder (gdbarch, &ppc32_nto_sighandler_tramp_frame_632);
  /* Our loader handles solib relocations slightly differently than svr4.  */
  svr4_so_ops.relocate_section_addresses = nto_relocate_section_addresses;

  /* Supply a nice function to find our solibs.  */
  svr4_so_ops.find_and_open_solib = nto_find_and_open_solib;

  /* Our linker code is in libc.  */
  svr4_so_ops.in_dynsym_resolve_code = nto_in_dynsym_resolve_code;

  set_solib_ops (gdbarch, &svr4_so_ops);

  set_gdbarch_regset_from_core_section
    (gdbarch, ppcnto_regset_from_core_section);

  set_gdbarch_get_siginfo_type (gdbarch, nto_get_siginfo_type);

  init_ppcnto_ops ();
  set_gdbarch_have_nonsteppable_watchpoint (gdbarch, 0);
}


extern initialize_file_ftype _initialize_ppcnto_tdep;

void
_initialize_ppcnto_tdep (void)
{
  gdbarch_register_osabi (bfd_arch_powerpc, 0, GDB_OSABI_QNXNTO, ppcnto_init_abi);
  gdbarch_register_osabi (bfd_arch_rs6000, 0, GDB_OSABI_QNXNTO, ppcnto_init_abi);
  gdbarch_register_osabi_sniffer (bfd_arch_powerpc, bfd_target_elf_flavour,
		  		  nto_elf_osabi_sniffer);
  gdbarch_register_osabi_sniffer (bfd_arch_rs6000, bfd_target_elf_flavour,
		  		  nto_elf_osabi_sniffer);
}

int nto_breakpoint_size (CORE_ADDR addr)
{
  return 0;
}

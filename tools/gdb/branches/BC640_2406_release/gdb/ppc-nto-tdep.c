/* PPC specific functionality for QNX Neutrino.

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

/* 32 general fp regs + 2 fpscr regs */
#define FP_REGSET_SIZE (32 * 8 + 2 * 4)
#define FPSCR_OFF (32 * 8)
#define FPSCR_VAL_OFF (32 * 8 + 4)

/* 32 Altivec regs + vscr  */
#define ALT_REGSET_SIZE (32 * 16 + 16)
#define VSCR_OFF (32 * 16)

#define GPLAST_REGNUM (tdep->ppc_gp0_regnum + ppc_num_gprs)
#define FPLAST_REGNUM (tdep->ppc_fp0_regnum + ppc_num_fprs)
#define ALTLAST_REGNUM (tdep->ppc_vrsave_regnum)

/********************************************************/

static int ppc_nto_at_sigtramp_return_path (CORE_ADDR pc);

/**********************FIXME***************************/

#define PPC_NTO_SIGNAL_FRAMESIZE 64 
#define PPC_NTO_HANDLER_PTR_OFFSET (PPC_NTO_SIGNAL_FRAMESIZE + 0x14)

#define INSTR_LI_R0_0x6666		0x38006666 
#define INSTR_LI_R0_0x7777		0x38007777
#define INSTR_LI_R0_NR_sigreturn	0x38000077
#define INSTR_LI_R0_NR_rt_sigreturn	0x380000AC

#define INSTR_SC			0x44000002


static unsigned long
nto_read_register (int regnum)
{
  ULONGEST value;

  regcache_raw_read_unsigned (get_current_regcache (), regnum, &value);
  return (unsigned long) value;
}

static int
insn_is_sigreturn (unsigned long pcinsn)
{
  switch(pcinsn)
    {
    case INSTR_LI_R0_0x6666:
    case INSTR_LI_R0_0x7777:
    case INSTR_LI_R0_NR_sigreturn:
    case INSTR_LI_R0_NR_rt_sigreturn:
      return 1;
    default:
      return 0;
    }
}

static CORE_ADDR
ppc_nto_skip_trampoline_code (struct frame_info *frame, CORE_ADDR pc)
{
  char buf[4];
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

  nto_trace (0) ("%s () pc=0x%s\n", __func__, paddr (pc));

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
  insn = extract_unsigned_integer (buf, 4);

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
	num_slots = ((int) sect->endaddr - (int) sect->addr) / 12;
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
  reloc = extract_unsigned_integer (buf, 4);

  sect = find_pc_section (reloc);
  if (!sect)
    return 0;

  if (strcmp (sect->the_bfd_section->name, ".text") == 0)
    return reloc;

  /* Now get the r_info field which is the relocation type and symbol
     index. */
  if (target_read_memory (reloc + 4, buf, 4) != 0)
    return 0;
  symidx = extract_unsigned_integer (buf, 4);

  /* Shift out the relocation type leaving just the symbol index */
  /* symidx = ELF32_R_SYM(symidx); */
  symidx = symidx >> 8;

  /* compute the address of the symbol */
  sym = symtab + symidx * 4;

  /* Fetch the string table index */
  if (target_read_memory (sym, buf, 4) != 0)
    return 0;
  symidx = extract_unsigned_integer (buf, 4);

  /* Fetch the string; we don't know how long it is.  Is it possible
     that the following will fail because we're trying to fetch too
     much? */
  if (target_read_memory (strtab + symidx, symname, sizeof (symname)) != 0)
    return 0;

  /* This might not work right if we have multiple symbols with the
     same name; the only way to really get it right is to perform
     the same sort of lookup as the dynamic linker. */
  msymbol = lookup_minimal_symbol_text (symname, NULL);
  if (!msymbol)
    return 0;

  return SYMBOL_VALUE_ADDRESS (msymbol);
}

static int
ppc_nto_at_sigtramp_return_path (CORE_ADDR pc)
{
  char buf[12];
  unsigned long pcinsn;

  nto_trace (0) ("%s () pc=0x%s\n", __func__, paddr (pc));

  if (target_read_memory (pc - 4, buf, sizeof (buf)) != 0)
    return 0;

  /* extract the instruction at the pc */
  pcinsn = extract_unsigned_integer (buf + 4, 4);

  return (
	   (insn_is_sigreturn (pcinsn)
	    && extract_unsigned_integer (buf + 8, 4) == INSTR_SC)
	   ||
	   (pcinsn == INSTR_SC
	    && insn_is_sigreturn (extract_unsigned_integer (buf, 4))));
}

/**********************FIXME***************************/

int
ppc_nto_in_sigtramp (CORE_ADDR pc, char *func_name)
{
  CORE_ADDR lr;
  CORE_ADDR sp;
  CORE_ADDR tramp_sp;
  char buf[4];
  CORE_ADDR handler;

  nto_trace (0) ("%s () pc=0x%s, func_name=%s\n", __func__, paddr (pc), func_name);

  lr = nto_read_register (gdbarch_tdep (current_gdbarch)->ppc_lr_regnum);
  if (!ppc_nto_at_sigtramp_return_path (lr))
    return 0;

  sp = nto_read_register (gdbarch_sp_regnum (current_gdbarch));

  if (target_read_memory (sp, buf, sizeof (buf)) != 0)
    return 0;

  tramp_sp = extract_unsigned_integer (buf, 4);

  if (target_read_memory (tramp_sp + PPC_NTO_HANDLER_PTR_OFFSET, buf,
			  sizeof (buf)) != 0)
    return 0;

  handler = extract_unsigned_integer (buf, 4);

  return (pc == handler || pc == handler + 4);
}


/* Signal tampoline sniffer.  */

struct ppc_nto_sigtramp_cache
{
  CORE_ADDR base;
  struct trad_frame_saved_reg *saved_regs;
};

/* The context is calculated the same way regardless of the sniffer method. */
static CORE_ADDR
ppcnto_sigcontext_addr (struct frame_info *next_frame)
{
  struct gdbarch *gdbarch = get_frame_arch (next_frame);
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);
  char buf[4];
  CORE_ADDR ptrctx;

  nto_trace (0) ("%s ()\n", __func__);

  /* Read base from r31 of the sigtramp frame (see ppc/sigstub.S)  */
  ptrctx = frame_unwind_register_unsigned (next_frame,
					   tdep->ppc_gp0_regnum + 31);
  if (ptrctx == 0)
    warning ("Unable to retrieve sigstack_context pointer.");
  return ptrctx;
}

static struct ppc_nto_sigtramp_cache *
ppc_nto_sigtramp_cache (struct frame_info *next_frame, void **this_cache)
{
  CORE_ADDR regs;
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
  gpregs = ppcnto_sigcontext_addr(next_frame); 
  fpregs = gpregs + 41 * tdep->wordsize;

  /* General purpose.  */
  for (i = 0; i < ppc_num_gprs; i++)
    {
      int regnum = i + tdep->ppc_gp0_regnum;
      cache->saved_regs[regnum].addr = gpregs + i * tdep->wordsize;
    }
  cache->saved_regs[gdbarch_pc_regnum (gdbarch)].addr = gpregs + 35
							* tdep->wordsize;
  cache->saved_regs[tdep->ppc_ctr_regnum].addr = gpregs + 32 * tdep->wordsize;
  cache->saved_regs[tdep->ppc_lr_regnum].addr = gpregs + 33 * tdep->wordsize;
  cache->saved_regs[tdep->ppc_xer_regnum].addr = gpregs + 37 * tdep->wordsize;
  cache->saved_regs[tdep->ppc_cr_regnum].addr = gpregs + 36 * tdep->wordsize;

  /* Floating point registers.  */
  if (ppc_floating_point_unit_p (gdbarch))
    {
      for (i = 0; i < ppc_num_fprs; i++)
        {
          int regnum = i + tdep->ppc_fp0_regnum;
          cache->saved_regs[regnum].addr = fpregs + i * tdep->wordsize;
        }
      cache->saved_regs[tdep->ppc_fpscr_regnum].addr
        = fpregs + 32 * tdep->wordsize;
    }

  return cache;
}

static void
ppc_nto_sigtramp_this_id (struct frame_info *next_frame, void **this_cache,
			  struct frame_id *this_id)
{
  struct ppc_nto_sigtramp_cache *info
    = ppc_nto_sigtramp_cache (next_frame, this_cache);
  nto_trace (0) ("%s ()\n", __func__);
  (*this_id) = frame_id_build (info->base, frame_pc_unwind (next_frame));
}

static void
ppc_nto_sigtramp_prev_register (struct frame_info *next_frame,
				void **this_cache,
				int regnum, int *optimizedp,
				enum lval_type *lvalp, CORE_ADDR *addrp,
				int *realnump, gdb_byte *valuep)
{
  struct ppc_nto_sigtramp_cache *info
    = ppc_nto_sigtramp_cache (next_frame, this_cache);
  nto_trace (0) ("%s ()\n", __func__);
  trad_frame_get_prev_register (next_frame, info->saved_regs, regnum,
				optimizedp, lvalp, addrp, realnump, valuep);
}

static const struct frame_unwind ppc_nto_sigtramp_unwind =
{
  SIGTRAMP_FRAME,
  ppc_nto_sigtramp_this_id,
  ppc_nto_sigtramp_prev_register
};

static const struct frame_unwind *
ppc_nto_sigtramp_sniffer (struct frame_info *next_frame)
{
  struct frame_id id;
  CORE_ADDR frame_func;
  char *func_name = "";
  nto_trace (0) ("%s ()\n", __func__);
  if (frame_pc_unwind (next_frame)
      > frame_unwind_register_unsigned (next_frame, 
                                        gdbarch_sp_regnum (current_gdbarch)))
    {
      frame_func = frame_pc_unwind (next_frame);
      if (frame_func)
        find_pc_partial_function (frame_func, &func_name, NULL, NULL);
      nto_trace (0) ("get_frame_func returned: 0x%s %s\n", paddr (frame_func), func_name);
      if (!func_name || func_name[0] == '\0')
        return NULL;
      /* see if this is __signalstub function: */
      if (0 == strcmp (func_name, "__signalstub"))
        {
	  nto_trace (0) ("This is signal trampoline frame\n");
	  return &ppc_nto_sigtramp_unwind;
	}
    }
      
  return NULL;
}

/*****************************************************************************/

static void
ppcnto_supply_reg_gregset (struct regcache *regcache, int regno, char *data)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);
  int regi;
  nto_trace (0) ("%s ()\n", __func__);
  for (regi = tdep->ppc_gp0_regnum; regi < GPLAST_REGNUM; regi++)
    {
      RAW_SUPPLY_IF_NEEDED (regcache, regi,
		     data + (regi - tdep->ppc_gp0_regnum) * NTO_PPC_REGSIZE);
    }

  /* fill in between FIRST_UISA_SP_REGNUM and LAST_UISA_SP_REGNUM */
  RAW_SUPPLY_IF_NEEDED (regcache, tdep->ppc_ctr_regnum, data + CTR_OFF);
  RAW_SUPPLY_IF_NEEDED (regcache, tdep->ppc_ctr_regnum, data + CTR_OFF);
  RAW_SUPPLY_IF_NEEDED (regcache, tdep->ppc_lr_regnum, data + LR_OFF);
  RAW_SUPPLY_IF_NEEDED (regcache, tdep->ppc_ps_regnum, data + MSR_OFF);
  RAW_SUPPLY_IF_NEEDED (regcache, gdbarch_pc_regnum (current_gdbarch), data + IAR_OFF);
  RAW_SUPPLY_IF_NEEDED (regcache, tdep->ppc_cr_regnum, data + CR_OFF);
  RAW_SUPPLY_IF_NEEDED (regcache, tdep->ppc_xer_regnum, data + XER_OFF);
  /* RAW_SUPPLY_IF_NEEDED (current_regcache, tdep->???, (char *) (&regp->ear)); */
  /* FIXME: mq is only on the 601 - should we check? */
  if(tdep->ppc_mq_regnum != -1)
    RAW_SUPPLY_IF_NEEDED (regcache, tdep->ppc_mq_regnum, data + MQ_OFF);
  if(tdep->ppc_vrsave_regnum != -1)
    RAW_SUPPLY_IF_NEEDED (regcache, tdep->ppc_vrsave_regnum, data + VRSAVE_OFF);
}

static void
ppcnto_supply_gregset (struct regcache *regcache, char *data)
{
  ppcnto_supply_reg_gregset (regcache, NTO_ALL_REGS, data);
}

static void
ppcnto_supply_reg_fpregset (struct regcache *regcache, int regno, char *data)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);
  int regi;
  nto_trace (0) ("%s ()\n", __func__);
  for (regi = 0; regi < 32; regi++)
    RAW_SUPPLY_IF_NEEDED (regcache, 
                          gdbarch_fp0_regnum (current_gdbarch) + regi, 
			  data + regi * 8);
  RAW_SUPPLY_IF_NEEDED (regcache, tdep->ppc_fpscr_regnum, data + FPSCR_VAL_OFF);
}

static void
ppcnto_supply_fpregset (struct regcache *regcache, char *data)
{
  ppcnto_supply_reg_fpregset (regcache, NTO_ALL_REGS, data);
}
   
static void
ppcnto_supply_reg_altregset (struct regcache *regcache, int regno, char *data)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);
  int regi;
  
  nto_trace (0) ("%s ()\n", __func__);

  for (regi = tdep->ppc_vr0_regnum; regi < ALTLAST_REGNUM; regi++)
    RAW_SUPPLY_IF_NEEDED (regcache, regi,
                         data+((regi-tdep->ppc_vr0_regnum)*16));
}

static void
ppcnto_supply_altregset (struct regcache *regcache, char *data)
{
  ppcnto_supply_reg_altregset (regcache, NTO_ALL_REGS, data);
}

static void
ppcnto_supply_regset (struct regcache *regcache, int regset, char *data)
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
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);

  if (regno == -1)
    return NTO_REG_END;
  else if (regno >= gdbarch_fp0_regnum (current_gdbarch) && regno < FPLAST_REGNUM)
    return NTO_REG_FLOAT;
  else if (regno >= tdep->ppc_vr0_regnum && regno < tdep->ppc_vrsave_regnum)
    return NTO_REG_ALT;
  else if (regno < GPLAST_REGNUM ||
		  (regno >= gdbarch_pc_regnum (current_gdbarch) && 
		   regno < tdep->ppc_fpscr_regnum))
    return NTO_REG_GENERAL;
  return -1;
}

static int
ppcnto_register_area (int regno, int regset, unsigned *off)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);

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
	  else if (regno == gdbarch_pc_regnum (current_gdbarch))
	    *off = IAR_OFF;
	  else if (regno == tdep->ppc_cr_regnum)
	    *off = CR_OFF;
	  else if (regno == tdep->ppc_xer_regnum)
	    *off = XER_OFF;
	  else if (regno == tdep->ppc_mq_regnum)
	    *off = MQ_OFF;
	  else if (regno == tdep->ppc_vrsave_regnum)
	    *off = VRSAVE_OFF;
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
	  *off = regno * 8;
	  return 8;
	}
      else
	{
	  if (regno == tdep->ppc_fpscr_regnum)
	    *off = FPSCR_VAL_OFF;
	  else
	    return 0;
	  return 4;
	}
    }
  else if (regset == NTO_REG_ALT)
    {
      if (regno == -1)
        return ALT_REGSET_SIZE;

      if (regno < ALTLAST_REGNUM)
        {
          *off = regno * 16;
          return 16;
        }
      else
        {
          return 0;
        }
    }

  return -1;
}

static int
ppcnto_regset_fill (const struct regcache *regcache, int regset, char *data)
{
  int regno;
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);

  if (regset == NTO_REG_GENERAL)
    {
      for (regno = tdep->ppc_gp0_regnum; regno < GPLAST_REGNUM;
	   regno++)
	{
	  regcache_raw_collect (regcache, regno, data + (regno - tdep->ppc_gp0_regnum) * NTO_PPC_REGSIZE);
	}

      regcache_raw_collect (regcache, tdep->ppc_ctr_regnum, data + CTR_OFF);
      regcache_raw_collect (regcache, tdep->ppc_lr_regnum, data + LR_OFF);
      regcache_raw_collect (regcache, tdep->ppc_ps_regnum, data + MSR_OFF);
      regcache_raw_collect (regcache, gdbarch_pc_regnum (current_gdbarch), data + IAR_OFF);
      regcache_raw_collect (regcache, tdep->ppc_cr_regnum, data + CR_OFF);
      regcache_raw_collect (regcache, tdep->ppc_xer_regnum, data + XER_OFF);
      /* regcache_raw_collect (current_regcache, tdep->???, (char *) (&regp->ear)); */
      if(tdep->ppc_mq_regnum != -1)
	      regcache_raw_collect (regcache, tdep->ppc_mq_regnum, data + MQ_OFF);
      regcache_raw_collect (regcache, tdep->ppc_vrsave_regnum, data + VRSAVE_OFF);
    }
  else if (regset == NTO_REG_FLOAT){
    for (regno = 0; regno < 32; regno++)
      regcache_raw_collect (regcache, gdbarch_fp0_regnum (current_gdbarch) + regno, data + regno * 8);
    /* FIXME: is this right?  Lower order bits?  */
    regcache_raw_collect (regcache, tdep->ppc_fpscr_regnum, data + FPSCR_VAL_OFF);
  }
  else
	  return -1;
  return 0;
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
      ppcnto_supply_regset (regcache, NTO_REG_GENERAL, (char *)gpreg);
    } 
  else
    {
      regset_id = ppcnto_regset_id (regnum);
      nto_trace (0) ("nto_regset_id=%d\n", regset_id);
      ppcnto_supply_reg_gregset (regcache, regnum, (char *)gpreg);
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
      ppcnto_supply_regset (regcache, NTO_REG_FLOAT, (char *)fpreg);
    }
  else
    {
      regset_id = ppcnto_regset_id (regnum);
      nto_trace (0) ("nto regset_id=%d\n", regset_id);
      ppcnto_supply_reg_fpregset (regcache, regnum, (char *)fpreg); 
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
                            struct frame_info *next_frame,
			    struct trad_frame_cache *this_cache,
			    CORE_ADDR func)
{
  struct gdbarch *gdbarch = get_frame_arch (next_frame);
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);
  CORE_ADDR ptrctx, sp;
  int i;
  
  nto_trace (0) ("%s () funcaddr=0x%s\n", __func__, paddr (func));

  sp = frame_unwind_register_unsigned (next_frame,
                                         gdbarch_sp_regnum (current_gdbarch));

  nto_trace (0) ("sp: 0x%s\n", paddr (sp));

  /* Construct the frame ID using the function start. */
  trad_frame_set_id (this_cache, frame_id_build (sp, func));

  /* Get ucontext address.  */
  ptrctx = ppcnto_sigcontext_addr (next_frame);
  
  for (i = 0; i != ppc_num_gprs; i++)
    {
      const int regnum = i + tdep->ppc_gp0_regnum;
      const int addr = ptrctx + i * tdep->wordsize;
      trad_frame_set_reg_addr (this_cache, regnum, addr);
    }
  
  trad_frame_set_reg_addr (this_cache, tdep->ppc_ctr_regnum, ptrctx + CTR_OFF);
  trad_frame_set_reg_addr (this_cache, tdep->ppc_lr_regnum, ptrctx + LR_OFF);
  trad_frame_set_reg_addr (this_cache, tdep->ppc_ps_regnum, ptrctx + MSR_OFF);
  trad_frame_set_reg_addr (this_cache, gdbarch_pc_regnum (gdbarch), 
			   ptrctx + IAR_OFF);
  trad_frame_set_reg_addr (this_cache, tdep->ppc_cr_regnum, ptrctx + CR_OFF);
  trad_frame_set_reg_addr (this_cache, tdep->ppc_xer_regnum, ptrctx + XER_OFF);
//  trad_frame_set_reg_addr (this_cache, ??? tdep->ppc_ear_regnum, base + EAR_OFF);
  /* FIXME: mq is only on the 601 - should we check? */
  if(tdep->ppc_mq_regnum != -1)
    trad_frame_set_reg_addr (this_cache, tdep->ppc_mq_regnum, ptrctx + MQ_OFF);
  if(tdep->ppc_vrsave_regnum != -1)
    trad_frame_set_reg_addr (this_cache, tdep->ppc_vrsave_regnum, 
			     ptrctx + VRSAVE_OFF);
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
  TARGET_SO_RELOCATE_SECTION_ADDRESSES = nto_relocate_section_addresses;

  /* Supply a nice function to find our solibs.  */
  TARGET_SO_FIND_AND_OPEN_SOLIB = nto_find_and_open_solib;

  /* Our linker code is in libc.  */
  TARGET_SO_IN_DYNSYM_RESOLVE_CODE = nto_in_dynsym_resolve_code;

  set_gdbarch_regset_from_core_section
    (gdbarch, ppcnto_regset_from_core_section);

  frame_unwind_append_sniffer (gdbarch, ppc_nto_sigtramp_sniffer);
  init_ppcnto_ops ();
}

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

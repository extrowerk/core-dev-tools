/* DWARF2 EH unwinding support for x86.
   Copyright (C) 2014 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

Under Section 7 of GPL version 3, you are granted additional
permissions described in the GCC Runtime Library Exception, version
3.1, as published by the Free Software Foundation.

You should have received a copy of the GNU General Public License and
a copy of the GCC Runtime Library Exception along with this program;
see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
<http://www.gnu.org/licenses/>.  */

/* Do code reading to identify a signal frame, and set the frame
   state data appropriately.  See unwind-dw2.c for the structs.  */

#if defined(__QNXNTO__)

#include <sys/neutrino.h>

#if !defined(inhibit_libc) && _NTO_VERSION >= 660

#include <ucontext.h>
#include <sys/link.h>

struct address_range
{
  _Unwind_Ptr begin;
  _Unwind_Ptr end;
};

static int
load_contains_pc (const struct dl_phdr_info *info, size_t size, void *ptr)
{
  long n;
  struct address_range *range = (struct address_range *)ptr;
  const Elf32_Phdr *phdr = info->dlpi_phdr;

  (void)size;

  for (n = info->dlpi_phnum; --n >= 0; phdr++)
    {
      if (phdr->p_type == PT_LOAD)
        {
	      _Unwind_Ptr vaddr = (_Unwind_Ptr)phdr->p_vaddr + info->dlpi_addr;
	      if (range->begin >= vaddr && range->end < vaddr + phdr->p_memsz)
	        return 1;
	    }
	}
  return 0;
}

#define MD_FALLBACK_FRAME_STATE_FOR x86_fallback_frame_state

static _Unwind_Reason_Code
x86_fallback_frame_state (struct _Unwind_Context *context,
			  _Unwind_FrameState *fs)
{
  unsigned char *pc;
  mcontext_t *mctx;
  long new_cfa;
  struct address_range range;

  /*
   < 0>:	push   %eax
   <+1>:	push   %esi
   <+2>:	pushl  (%esi)
   <+4>:	call   *0x28(%esi)
   <+7>:	push   %esi             <--- PC
   <+8>:	mov    %edi,%eax
   <+10>:	mov    0x18(%eax),%edi
   <+13>:	mov    0x1c(%eax),%esi
   <+16>:	mov    0x20(%eax),%ebp
   <+19>:	mov    0x28(%eax),%ebx
   <+22>:	mov    0x2c(%eax),%edx
   <+25>:	mov    0x30(%eax),%ecx
   <+28>:	sub    $0x4,%esp
   <+31>:	mov    $0x1b,%eax
   <+36>:	int    $0x28
   <+38>:	ret
   <+39>:   ret
  */

  pc = context->ra - 7;
  range.begin = (_Unwind_Ptr)pc;
  range.end = range.begin + 40;
  if (dl_iterate_phdr (load_contains_pc, &range)
      && *(unsigned int*)(pc) == 0x36ff5650
      && *(unsigned int*)(pc+4) == 0x562856ff
      && *(unsigned int*)(pc+8) == 0x788bf889
      && *(unsigned int*)(pc+12) == 0x1c708b18
      && *(unsigned int*)(pc+16) == 0x8b20688b
      && *(unsigned int*)(pc+20) == 0x508b2858
      && *(unsigned int*)(pc+24) == 0x30488b2c
      && *(unsigned int*)(pc+28) == 0xb804ec83
      && *(unsigned int*)(pc+32) == 0x0000001b
      && *(unsigned int*)(pc+36) == 0xc3c328cd)
    {
      struct handler_args {
	    int signo;
	    siginfo_t *sip;
	    ucontext_t *ucontext;
      } *handler_args = context->cfa;
      mctx = &handler_args->ucontext->uc_mcontext;
    }
  else
    return _URC_END_OF_STACK;

  new_cfa = mctx->cpu.esp;

  fs->regs.cfa_how = CFA_REG_OFFSET;
  fs->regs.cfa_reg = 4;
  fs->regs.cfa_offset = new_cfa - (long) context->cfa;

  /* The SVR4 register numbering macros aren't usable in libgcc.  */
  fs->regs.reg[0].how = REG_SAVED_OFFSET;
  fs->regs.reg[0].loc.offset = (long)&mctx->cpu.eax - new_cfa;
  fs->regs.reg[3].how = REG_SAVED_OFFSET;
  fs->regs.reg[3].loc.offset = (long)&mctx->cpu.ebx - new_cfa;
  fs->regs.reg[1].how = REG_SAVED_OFFSET;
  fs->regs.reg[1].loc.offset = (long)&mctx->cpu.ecx - new_cfa;
  fs->regs.reg[2].how = REG_SAVED_OFFSET;
  fs->regs.reg[2].loc.offset = (long)&mctx->cpu.edx - new_cfa;
  fs->regs.reg[6].how = REG_SAVED_OFFSET;
  fs->regs.reg[6].loc.offset = (long)&mctx->cpu.esi - new_cfa;
  fs->regs.reg[7].how = REG_SAVED_OFFSET;
  fs->regs.reg[7].loc.offset = (long)&mctx->cpu.edi - new_cfa;
  fs->regs.reg[5].how = REG_SAVED_OFFSET;
  fs->regs.reg[5].loc.offset = (long)&mctx->cpu.ebp - new_cfa;
  fs->regs.reg[8].how = REG_SAVED_OFFSET;
  fs->regs.reg[8].loc.offset = (long)&mctx->cpu.eip - new_cfa;
  fs->retaddr_column = 8;
  fs->signal_frame = 1;

  return _URC_NO_REASON;
}

#endif
#endif


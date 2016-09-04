/* Target-dependent code for QNX Neutrino x86_64.

   Copyright (C) 2014 Free Software Foundation, Inc.

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


#ifndef AMD64_NTO_TDEP_H
#define AMD64_NTO_TDEP_H

#include <stdint.h>

/* From x86_64/context.h.  */
#define  X86_64_RDI    0
#define  X86_64_RSI    1
#define  X86_64_RDX    2
#define  X86_64_R10    3
#define  X86_64_R8     4
#define  X86_64_R9     5
#define  X86_64_RAX    6
#define  X86_64_RBX    7
#define  X86_64_RBP    8
#define  X86_64_RCX    9
#define  X86_64_R11    10
#define  X86_64_R12    11
#define  X86_64_R13    12
#define  X86_64_R14    13
#define  X86_64_R15    14
#define  X86_64_RIP    15
#define  X86_64_CS     16
#define  X86_64_RFLAGS 17
#define  X86_64_RSP    18
#define  X86_64_SS     19

#ifndef __QNXNTO__
typedef uint8_t _Uint8t;
typedef uint32_t _Uint32t;
typedef uint64_t _Uint64t;
#endif

typedef struct x86_64_cpu_registers {
/*-
    This layout permits mimics the kernel call argument order.
    Normally, rcx should be in the place of r10; however the syscall
    Instruction insists on stuffing %rip into %rcx (what were they thinking?),
    So the kernel call does a "mov %rcx, %r10" in the preamble.
*/
    _Uint64t    rdi,
                rsi,
                rdx,
                r10,
                r8,
                r9,
                rax,
                rbx,
                rbp,
                rcx,
                r11,
                r12,
                r13,
                r14,
                r15;
    _Uint64t    rip;
    _Uint32t    cs;
    _Uint32t    rsvd1;
    _Uint64t    rflags;
    _Uint64t    rsp;
    _Uint32t    ss;
} X86_64_CPU_REGISTERS;

typedef struct fsave_area_64 {
    _Uint32t fpu_control_word;
    _Uint32t fpu_status_word;
    _Uint32t fpu_tag_word;
    _Uint32t fpu_ip;
    _Uint32t fpu_cs;
    _Uint32t fpu_op;
    _Uint32t fpu_ds;
    _Uint8t  st_regs[80];
} X86_64_NDP_REGISTERS;

#endif /* amd64-nto-tdep.h */

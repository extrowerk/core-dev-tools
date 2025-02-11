/* Target-dependent code for AMD64.

   Copyright (C) 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009
   Free Software Foundation, Inc.

   Contributed by Jiri Smid, SuSE Labs.

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
#include "opcode/i386.h"
#include "dis-asm.h"
#include "arch-utils.h"
#include "block.h"
#include "dummy-frame.h"
#include "frame.h"
#include "frame-base.h"
#include "frame-unwind.h"
#include "inferior.h"
#include "gdbcmd.h"
#include "gdbcore.h"
#include "objfiles.h"
#include "regcache.h"
#include "regset.h"
#include "symfile.h"

#include "gdb_assert.h"

#include "amd64-tdep.h"
#include "i387-tdep.h"

/* Note that the AMD64 architecture was previously known as x86-64.
   The latter is (forever) engraved into the canonical system name as
   returned by config.guess, and used as the name for the AMD64 port
   of GNU/Linux.  The BSD's have renamed their ports to amd64; they
   don't like to shout.  For GDB we prefer the amd64_-prefix over the
   x86_64_-prefix since it's so much easier to type.  */

/* Register information.  */

static const char *amd64_register_names[] = 
{
  "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp",

  /* %r8 is indeed register number 8.  */
  "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
  "rip", "eflags", "cs", "ss", "ds", "es", "fs", "gs",

  /* %st0 is register number 24.  */
  "st0", "st1", "st2", "st3", "st4", "st5", "st6", "st7",
  "fctrl", "fstat", "ftag", "fiseg", "fioff", "foseg", "fooff", "fop",

  /* %xmm0 is register number 40.  */
  "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
  "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15",
  "mxcsr",
};

/* Total number of registers.  */
#define AMD64_NUM_REGS	ARRAY_SIZE (amd64_register_names)

/* Return the name of register REGNUM.  */

const char *
amd64_register_name (struct gdbarch *gdbarch, int regnum)
{
  if (regnum >= 0 && regnum < AMD64_NUM_REGS)
    return amd64_register_names[regnum];

  return NULL;
}

/* Return the GDB type object for the "standard" data type of data in
   register REGNUM. */

struct type *
amd64_register_type (struct gdbarch *gdbarch, int regnum)
{
  if (regnum >= AMD64_RAX_REGNUM && regnum <= AMD64_RDI_REGNUM)
    return builtin_type (gdbarch)->builtin_int64;
  if (regnum == AMD64_RBP_REGNUM || regnum == AMD64_RSP_REGNUM)
    return builtin_type (gdbarch)->builtin_data_ptr;
  if (regnum >= AMD64_R8_REGNUM && regnum <= AMD64_R15_REGNUM)
    return builtin_type (gdbarch)->builtin_int64;
  if (regnum == AMD64_RIP_REGNUM)
    return builtin_type (gdbarch)->builtin_func_ptr;
  if (regnum == AMD64_EFLAGS_REGNUM)
    return i386_eflags_type (gdbarch);
  if (regnum >= AMD64_CS_REGNUM && regnum <= AMD64_GS_REGNUM)
    return builtin_type (gdbarch)->builtin_int32;
  if (regnum >= AMD64_ST0_REGNUM && regnum <= AMD64_ST0_REGNUM + 7)
    return i387_ext_type (gdbarch);
  if (regnum >= AMD64_FCTRL_REGNUM && regnum <= AMD64_FCTRL_REGNUM + 7)
    return builtin_type (gdbarch)->builtin_int32;
  if (regnum >= AMD64_XMM0_REGNUM && regnum <= AMD64_XMM0_REGNUM + 15)
    return i386_sse_type (gdbarch);
  if (regnum == AMD64_MXCSR_REGNUM)
    return i386_mxcsr_type (gdbarch);

  internal_error (__FILE__, __LINE__, _("invalid regnum"));
}

/* DWARF Register Number Mapping as defined in the System V psABI,
   section 3.6.  */

static int amd64_dwarf_regmap[] =
{
  /* General Purpose Registers RAX, RDX, RCX, RBX, RSI, RDI.  */
  AMD64_RAX_REGNUM, AMD64_RDX_REGNUM,
  AMD64_RCX_REGNUM, AMD64_RBX_REGNUM,
  AMD64_RSI_REGNUM, AMD64_RDI_REGNUM,

  /* Frame Pointer Register RBP.  */
  AMD64_RBP_REGNUM,

  /* Stack Pointer Register RSP.  */
  AMD64_RSP_REGNUM,

  /* Extended Integer Registers 8 - 15.  */
  8, 9, 10, 11, 12, 13, 14, 15,

  /* Return Address RA.  Mapped to RIP.  */
  AMD64_RIP_REGNUM,

  /* SSE Registers 0 - 7.  */
  AMD64_XMM0_REGNUM + 0, AMD64_XMM1_REGNUM,
  AMD64_XMM0_REGNUM + 2, AMD64_XMM0_REGNUM + 3,
  AMD64_XMM0_REGNUM + 4, AMD64_XMM0_REGNUM + 5,
  AMD64_XMM0_REGNUM + 6, AMD64_XMM0_REGNUM + 7,

  /* Extended SSE Registers 8 - 15.  */
  AMD64_XMM0_REGNUM + 8, AMD64_XMM0_REGNUM + 9,
  AMD64_XMM0_REGNUM + 10, AMD64_XMM0_REGNUM + 11,
  AMD64_XMM0_REGNUM + 12, AMD64_XMM0_REGNUM + 13,
  AMD64_XMM0_REGNUM + 14, AMD64_XMM0_REGNUM + 15,

  /* Floating Point Registers 0-7.  */
  AMD64_ST0_REGNUM + 0, AMD64_ST0_REGNUM + 1,
  AMD64_ST0_REGNUM + 2, AMD64_ST0_REGNUM + 3,
  AMD64_ST0_REGNUM + 4, AMD64_ST0_REGNUM + 5,
  AMD64_ST0_REGNUM + 6, AMD64_ST0_REGNUM + 7,
  
  /* Control and Status Flags Register.  */
  AMD64_EFLAGS_REGNUM,

  /* Selector Registers.  */
  AMD64_ES_REGNUM,
  AMD64_CS_REGNUM,
  AMD64_SS_REGNUM,
  AMD64_DS_REGNUM,
  AMD64_FS_REGNUM,
  AMD64_GS_REGNUM,
  -1,
  -1,

  /* Segment Base Address Registers.  */
  -1,
  -1,
  -1,
  -1,

  /* Special Selector Registers.  */
  -1,
  -1,

  /* Floating Point Control Registers.  */
  AMD64_MXCSR_REGNUM,
  AMD64_FCTRL_REGNUM,
  AMD64_FSTAT_REGNUM
};

static const int amd64_dwarf_regmap_len =
  (sizeof (amd64_dwarf_regmap) / sizeof (amd64_dwarf_regmap[0]));

/* Convert DWARF register number REG to the appropriate register
   number used by GDB.  */

static int
amd64_dwarf_reg_to_regnum (struct gdbarch *gdbarch, int reg)
{
  int regnum = -1;

  if (reg >= 0 && reg < amd64_dwarf_regmap_len)
    regnum = amd64_dwarf_regmap[reg];

  if (regnum == -1)
    warning (_("Unmapped DWARF Register #%d encountered."), reg);

  return regnum;
}

/* Map architectural register numbers to gdb register numbers.  */

static const int amd64_arch_regmap[16] =
{
  AMD64_RAX_REGNUM,	/* %rax */
  AMD64_RCX_REGNUM,	/* %rcx */
  AMD64_RDX_REGNUM,	/* %rdx */
  AMD64_RBX_REGNUM,	/* %rbx */
  AMD64_RSP_REGNUM,	/* %rsp */
  AMD64_RBP_REGNUM,	/* %rbp */
  AMD64_RSI_REGNUM,	/* %rsi */
  AMD64_RDI_REGNUM,	/* %rdi */
  AMD64_R8_REGNUM,	/* %r8 */
  AMD64_R9_REGNUM,	/* %r9 */
  AMD64_R10_REGNUM,	/* %r10 */
  AMD64_R11_REGNUM,	/* %r11 */
  AMD64_R12_REGNUM,	/* %r12 */
  AMD64_R13_REGNUM,	/* %r13 */
  AMD64_R14_REGNUM,	/* %r14 */
  AMD64_R15_REGNUM	/* %r15 */
};

static const int amd64_arch_regmap_len =
  (sizeof (amd64_arch_regmap) / sizeof (amd64_arch_regmap[0]));

/* Convert architectural register number REG to the appropriate register
   number used by GDB.  */

static int
amd64_arch_reg_to_regnum (int reg)
{
  gdb_assert (reg >= 0 && reg < amd64_arch_regmap_len);

  return amd64_arch_regmap[reg];
}



/* Register classes as defined in the psABI.  */

enum amd64_reg_class
{
  AMD64_INTEGER,
  AMD64_SSE,
  AMD64_SSEUP,
  AMD64_X87,
  AMD64_X87UP,
  AMD64_COMPLEX_X87,
  AMD64_NO_CLASS,
  AMD64_MEMORY
};

/* Return the union class of CLASS1 and CLASS2.  See the psABI for
   details.  */

static enum amd64_reg_class
amd64_merge_classes (enum amd64_reg_class class1, enum amd64_reg_class class2)
{
  /* Rule (a): If both classes are equal, this is the resulting class.  */
  if (class1 == class2)
    return class1;

  /* Rule (b): If one of the classes is NO_CLASS, the resulting class
     is the other class.  */
  if (class1 == AMD64_NO_CLASS)
    return class2;
  if (class2 == AMD64_NO_CLASS)
    return class1;

  /* Rule (c): If one of the classes is MEMORY, the result is MEMORY.  */
  if (class1 == AMD64_MEMORY || class2 == AMD64_MEMORY)
    return AMD64_MEMORY;

  /* Rule (d): If one of the classes is INTEGER, the result is INTEGER.  */
  if (class1 == AMD64_INTEGER || class2 == AMD64_INTEGER)
    return AMD64_INTEGER;

  /* Rule (e): If one of the classes is X87, X87UP, COMPLEX_X87 class,
     MEMORY is used as class.  */
  if (class1 == AMD64_X87 || class1 == AMD64_X87UP
      || class1 == AMD64_COMPLEX_X87 || class2 == AMD64_X87
      || class2 == AMD64_X87UP || class2 == AMD64_COMPLEX_X87)
    return AMD64_MEMORY;

  /* Rule (f): Otherwise class SSE is used.  */
  return AMD64_SSE;
}

static void amd64_classify (struct type *type, enum amd64_reg_class class[2]);

/* Return non-zero if TYPE is a non-POD structure or union type.  */

static int
amd64_non_pod_p (struct type *type)
{
  /* ??? A class with a base class certainly isn't POD, but does this
     catch all non-POD structure types?  */
  if (TYPE_CODE (type) == TYPE_CODE_STRUCT && TYPE_N_BASECLASSES (type) > 0)
    return 1;

  return 0;
}

/* Classify TYPE according to the rules for aggregate (structures and
   arrays) and union types, and store the result in CLASS.  */

static void
amd64_classify_aggregate (struct type *type, enum amd64_reg_class class[2])
{
  int len = TYPE_LENGTH (type);

  /* 1. If the size of an object is larger than two eightbytes, or in
        C++, is a non-POD structure or union type, or contains
        unaligned fields, it has class memory.  */
  if (len > 16 || amd64_non_pod_p (type))
    {
      class[0] = class[1] = AMD64_MEMORY;
      return;
    }

  /* 2. Both eightbytes get initialized to class NO_CLASS.  */
  class[0] = class[1] = AMD64_NO_CLASS;

  /* 3. Each field of an object is classified recursively so that
        always two fields are considered. The resulting class is
        calculated according to the classes of the fields in the
        eightbyte: */

  if (TYPE_CODE (type) == TYPE_CODE_ARRAY)
    {
      struct type *subtype = check_typedef (TYPE_TARGET_TYPE (type));

      /* All fields in an array have the same type.  */
      amd64_classify (subtype, class);
      if (len > 8 && class[1] == AMD64_NO_CLASS)
	class[1] = class[0];
    }
  else
    {
      int i;

      /* Structure or union.  */
      gdb_assert (TYPE_CODE (type) == TYPE_CODE_STRUCT
		  || TYPE_CODE (type) == TYPE_CODE_UNION);

      for (i = 0; i < TYPE_NFIELDS (type); i++)
	{
	  struct type *subtype = check_typedef (TYPE_FIELD_TYPE (type, i));
	  int pos = TYPE_FIELD_BITPOS (type, i) / 64;
	  enum amd64_reg_class subclass[2];

	  /* Ignore static fields.  */
	  if (field_is_static (&TYPE_FIELD (type, i)))
	    continue;

	  gdb_assert (pos == 0 || pos == 1);

	  amd64_classify (subtype, subclass);
	  class[pos] = amd64_merge_classes (class[pos], subclass[0]);
	  if (pos == 0)
	    class[1] = amd64_merge_classes (class[1], subclass[1]);
	}
    }

  /* 4. Then a post merger cleanup is done:  */

  /* Rule (a): If one of the classes is MEMORY, the whole argument is
     passed in memory.  */
  if (class[0] == AMD64_MEMORY || class[1] == AMD64_MEMORY)
    class[0] = class[1] = AMD64_MEMORY;

  /* Rule (b): If SSEUP is not preceeded by SSE, it is converted to
     SSE.  */
  if (class[0] == AMD64_SSEUP)
    class[0] = AMD64_SSE;
  if (class[1] == AMD64_SSEUP && class[0] != AMD64_SSE)
    class[1] = AMD64_SSE;
}

/* Classify TYPE, and store the result in CLASS.  */

static void
amd64_classify (struct type *type, enum amd64_reg_class class[2])
{
  enum type_code code = TYPE_CODE (type);
  int len = TYPE_LENGTH (type);

  class[0] = class[1] = AMD64_NO_CLASS;

  /* Arguments of types (signed and unsigned) _Bool, char, short, int,
     long, long long, and pointers are in the INTEGER class.  Similarly,
     range types, used by languages such as Ada, are also in the INTEGER
     class.  */
  if ((code == TYPE_CODE_INT || code == TYPE_CODE_ENUM
       || code == TYPE_CODE_BOOL || code == TYPE_CODE_RANGE
       || code == TYPE_CODE_CHAR
       || code == TYPE_CODE_PTR || code == TYPE_CODE_REF)
      && (len == 1 || len == 2 || len == 4 || len == 8))
    class[0] = AMD64_INTEGER;

  /* Arguments of types float, double, _Decimal32, _Decimal64 and __m64
     are in class SSE.  */
  else if ((code == TYPE_CODE_FLT || code == TYPE_CODE_DECFLOAT)
	   && (len == 4 || len == 8))
    /* FIXME: __m64 .  */
    class[0] = AMD64_SSE;

  /* Arguments of types __float128, _Decimal128 and __m128 are split into
     two halves.  The least significant ones belong to class SSE, the most
     significant one to class SSEUP.  */
  else if (code == TYPE_CODE_DECFLOAT && len == 16)
    /* FIXME: __float128, __m128.  */
    class[0] = AMD64_SSE, class[1] = AMD64_SSEUP;

  /* The 64-bit mantissa of arguments of type long double belongs to
     class X87, the 16-bit exponent plus 6 bytes of padding belongs to
     class X87UP.  */
  else if (code == TYPE_CODE_FLT && len == 16)
    /* Class X87 and X87UP.  */
    class[0] = AMD64_X87, class[1] = AMD64_X87UP;

  /* Aggregates.  */
  else if (code == TYPE_CODE_ARRAY || code == TYPE_CODE_STRUCT
	   || code == TYPE_CODE_UNION)
    amd64_classify_aggregate (type, class);
}

static enum return_value_convention
amd64_return_value (struct gdbarch *gdbarch, struct type *func_type,
		    struct type *type, struct regcache *regcache,
		    gdb_byte *readbuf, const gdb_byte *writebuf)
{
  enum amd64_reg_class class[2];
  int len = TYPE_LENGTH (type);
  static int integer_regnum[] = { AMD64_RAX_REGNUM, AMD64_RDX_REGNUM };
  static int sse_regnum[] = { AMD64_XMM0_REGNUM, AMD64_XMM1_REGNUM };
  int integer_reg = 0;
  int sse_reg = 0;
  int i;

  gdb_assert (!(readbuf && writebuf));

  /* 1. Classify the return type with the classification algorithm.  */
  amd64_classify (type, class);

  /* 2. If the type has class MEMORY, then the caller provides space
     for the return value and passes the address of this storage in
     %rdi as if it were the first argument to the function. In effect,
     this address becomes a hidden first argument.

     On return %rax will contain the address that has been passed in
     by the caller in %rdi.  */
  if (class[0] == AMD64_MEMORY)
    {
      /* As indicated by the comment above, the ABI guarantees that we
         can always find the return value just after the function has
         returned.  */

      if (readbuf)
	{
	  ULONGEST addr;

	  regcache_raw_read_unsigned (regcache, AMD64_RAX_REGNUM, &addr);
	  read_memory (addr, readbuf, TYPE_LENGTH (type));
	}

      return RETURN_VALUE_ABI_RETURNS_ADDRESS;
    }

  gdb_assert (class[1] != AMD64_MEMORY);
  gdb_assert (len <= 16);

  for (i = 0; len > 0; i++, len -= 8)
    {
      int regnum = -1;
      int offset = 0;

      switch (class[i])
	{
	case AMD64_INTEGER:
	  /* 3. If the class is INTEGER, the next available register
	     of the sequence %rax, %rdx is used.  */
	  regnum = integer_regnum[integer_reg++];
	  break;

	case AMD64_SSE:
	  /* 4. If the class is SSE, the next available SSE register
             of the sequence %xmm0, %xmm1 is used.  */
	  regnum = sse_regnum[sse_reg++];
	  break;

	case AMD64_SSEUP:
	  /* 5. If the class is SSEUP, the eightbyte is passed in the
	     upper half of the last used SSE register.  */
	  gdb_assert (sse_reg > 0);
	  regnum = sse_regnum[sse_reg - 1];
	  offset = 8;
	  break;

	case AMD64_X87:
	  /* 6. If the class is X87, the value is returned on the X87
             stack in %st0 as 80-bit x87 number.  */
	  regnum = AMD64_ST0_REGNUM;
	  if (writebuf)
	    i387_return_value (gdbarch, regcache);
	  break;

	case AMD64_X87UP:
	  /* 7. If the class is X87UP, the value is returned together
             with the previous X87 value in %st0.  */
	  gdb_assert (i > 0 && class[0] == AMD64_X87);
	  regnum = AMD64_ST0_REGNUM;
	  offset = 8;
	  len = 2;
	  break;

	case AMD64_NO_CLASS:
	  continue;

	default:
	  gdb_assert (!"Unexpected register class.");
	}

      gdb_assert (regnum != -1);

      if (readbuf)
	regcache_raw_read_part (regcache, regnum, offset, min (len, 8),
				readbuf + i * 8);
      if (writebuf)
	regcache_raw_write_part (regcache, regnum, offset, min (len, 8),
				 writebuf + i * 8);
    }

  return RETURN_VALUE_REGISTER_CONVENTION;
}


static CORE_ADDR
amd64_push_arguments (struct regcache *regcache, int nargs,
		      struct value **args, CORE_ADDR sp, int struct_return)
{
  static int integer_regnum[] =
  {
    AMD64_RDI_REGNUM,		/* %rdi */
    AMD64_RSI_REGNUM,		/* %rsi */
    AMD64_RDX_REGNUM,		/* %rdx */
    AMD64_RCX_REGNUM,		/* %rcx */
    8,				/* %r8 */
    9				/* %r9 */
  };
  static int sse_regnum[] =
  {
    /* %xmm0 ... %xmm7 */
    AMD64_XMM0_REGNUM + 0, AMD64_XMM1_REGNUM,
    AMD64_XMM0_REGNUM + 2, AMD64_XMM0_REGNUM + 3,
    AMD64_XMM0_REGNUM + 4, AMD64_XMM0_REGNUM + 5,
    AMD64_XMM0_REGNUM + 6, AMD64_XMM0_REGNUM + 7,
  };
  struct value **stack_args = alloca (nargs * sizeof (struct value *));
  int num_stack_args = 0;
  int num_elements = 0;
  int element = 0;
  int integer_reg = 0;
  int sse_reg = 0;
  int i;

  /* Reserve a register for the "hidden" argument.  */
  if (struct_return)
    integer_reg++;

  for (i = 0; i < nargs; i++)
    {
      struct type *type = value_type (args[i]);
      int len = TYPE_LENGTH (type);
      enum amd64_reg_class class[2];
      int needed_integer_regs = 0;
      int needed_sse_regs = 0;
      int j;

      /* Classify argument.  */
      amd64_classify (type, class);

      /* Calculate the number of integer and SSE registers needed for
         this argument.  */
      for (j = 0; j < 2; j++)
	{
	  if (class[j] == AMD64_INTEGER)
	    needed_integer_regs++;
	  else if (class[j] == AMD64_SSE)
	    needed_sse_regs++;
	}

      /* Check whether enough registers are available, and if the
         argument should be passed in registers at all.  */
      if (integer_reg + needed_integer_regs > ARRAY_SIZE (integer_regnum)
	  || sse_reg + needed_sse_regs > ARRAY_SIZE (sse_regnum)
	  || (needed_integer_regs == 0 && needed_sse_regs == 0))
	{
	  /* The argument will be passed on the stack.  */
	  num_elements += ((len + 7) / 8);
	  stack_args[num_stack_args++] = args[i];
	}
      else
	{
	  /* The argument will be passed in registers.  */
	  const gdb_byte *valbuf = value_contents (args[i]);
	  gdb_byte buf[8];

	  gdb_assert (len <= 16);

	  for (j = 0; len > 0; j++, len -= 8)
	    {
	      int regnum = -1;
	      int offset = 0;

	      switch (class[j])
		{
		case AMD64_INTEGER:
		  regnum = integer_regnum[integer_reg++];
		  break;

		case AMD64_SSE:
		  regnum = sse_regnum[sse_reg++];
		  break;

		case AMD64_SSEUP:
		  gdb_assert (sse_reg > 0);
		  regnum = sse_regnum[sse_reg - 1];
		  offset = 8;
		  break;

		default:
		  gdb_assert (!"Unexpected register class.");
		}

	      gdb_assert (regnum != -1);
	      memset (buf, 0, sizeof buf);
	      memcpy (buf, valbuf + j * 8, min (len, 8));
	      regcache_raw_write_part (regcache, regnum, offset, 8, buf);
	    }
	}
    }

  /* Allocate space for the arguments on the stack.  */
  sp -= num_elements * 8;

  /* The psABI says that "The end of the input argument area shall be
     aligned on a 16 byte boundary."  */
  sp &= ~0xf;

  /* Write out the arguments to the stack.  */
  for (i = 0; i < num_stack_args; i++)
    {
      struct type *type = value_type (stack_args[i]);
      const gdb_byte *valbuf = value_contents (stack_args[i]);
      int len = TYPE_LENGTH (type);

      write_memory (sp + element * 8, valbuf, len);
      element += ((len + 7) / 8);
    }

  /* The psABI says that "For calls that may call functions that use
     varargs or stdargs (prototype-less calls or calls to functions
     containing ellipsis (...) in the declaration) %al is used as
     hidden argument to specify the number of SSE registers used.  */
  regcache_raw_write_unsigned (regcache, AMD64_RAX_REGNUM, sse_reg);
  return sp; 
}

static CORE_ADDR
amd64_push_dummy_call (struct gdbarch *gdbarch, struct value *function,
		       struct regcache *regcache, CORE_ADDR bp_addr,
		       int nargs, struct value **args,	CORE_ADDR sp,
		       int struct_return, CORE_ADDR struct_addr)
{
  enum bfd_endian byte_order = gdbarch_byte_order (gdbarch);
  gdb_byte buf[8];

  /* Pass arguments.  */
  sp = amd64_push_arguments (regcache, nargs, args, sp, struct_return);

  /* Pass "hidden" argument".  */
  if (struct_return)
    {
      store_unsigned_integer (buf, 8, byte_order, struct_addr);
      regcache_cooked_write (regcache, AMD64_RDI_REGNUM, buf);
    }

  /* Store return address.  */
  sp -= 8;
  store_unsigned_integer (buf, 8, byte_order, bp_addr);
  write_memory (sp, buf, 8);

  /* Finally, update the stack pointer...  */
  store_unsigned_integer (buf, 8, byte_order, sp);
  regcache_cooked_write (regcache, AMD64_RSP_REGNUM, buf);

  /* ...and fake a frame pointer.  */
  regcache_cooked_write (regcache, AMD64_RBP_REGNUM, buf);

  return sp + 16;
}

/* Displaced instruction handling.  */

/* A partially decoded instruction.
   This contains enough details for displaced stepping purposes.  */

struct amd64_insn
{
  /* The number of opcode bytes.  */
  int opcode_len;
  /* The offset of the rex prefix or -1 if not present.  */
  int rex_offset;
  /* The offset to the first opcode byte.  */
  int opcode_offset;
  /* The offset to the modrm byte or -1 if not present.  */
  int modrm_offset;

  /* The raw instruction.  */
  gdb_byte *raw_insn;
};

struct displaced_step_closure
{
  /* For rip-relative insns, saved copy of the reg we use instead of %rip.  */
  int tmp_used;
  int tmp_regno;
  ULONGEST tmp_save;

  /* Details of the instruction.  */
  struct amd64_insn insn_details;

  /* Amount of space allocated to insn_buf.  */
  int max_len;

  /* The possibly modified insn.
     This is a variable-length field.  */
  gdb_byte insn_buf[1];
};

/* WARNING: Keep onebyte_has_modrm, twobyte_has_modrm in sync with
   ../opcodes/i386-dis.c (until libopcodes exports them, or an alternative,
   at which point delete these in favor of libopcodes' versions).  */

static const unsigned char onebyte_has_modrm[256] = {
  /*	   0 1 2 3 4 5 6 7 8 9 a b c d e f	  */
  /*	   -------------------------------	  */
  /* 00 */ 1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0, /* 00 */
  /* 10 */ 1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0, /* 10 */
  /* 20 */ 1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0, /* 20 */
  /* 30 */ 1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0, /* 30 */
  /* 40 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 40 */
  /* 50 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 50 */
  /* 60 */ 0,0,1,1,0,0,0,0,0,1,0,1,0,0,0,0, /* 60 */
  /* 70 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 70 */
  /* 80 */ 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, /* 80 */
  /* 90 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 90 */
  /* a0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* a0 */
  /* b0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* b0 */
  /* c0 */ 1,1,0,0,1,1,1,1,0,0,0,0,0,0,0,0, /* c0 */
  /* d0 */ 1,1,1,1,0,0,0,0,1,1,1,1,1,1,1,1, /* d0 */
  /* e0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* e0 */
  /* f0 */ 0,0,0,0,0,0,1,1,0,0,0,0,0,0,1,1  /* f0 */
  /*	   -------------------------------	  */
  /*	   0 1 2 3 4 5 6 7 8 9 a b c d e f	  */
};

static const unsigned char twobyte_has_modrm[256] = {
  /*	   0 1 2 3 4 5 6 7 8 9 a b c d e f	  */
  /*	   -------------------------------	  */
  /* 00 */ 1,1,1,1,0,0,0,0,0,0,0,0,0,1,0,1, /* 0f */
  /* 10 */ 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, /* 1f */
  /* 20 */ 1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1, /* 2f */
  /* 30 */ 0,0,0,0,0,0,0,0,1,0,1,0,0,0,0,0, /* 3f */
  /* 40 */ 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, /* 4f */
  /* 50 */ 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, /* 5f */
  /* 60 */ 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, /* 6f */
  /* 70 */ 1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1, /* 7f */
  /* 80 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 8f */
  /* 90 */ 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, /* 9f */
  /* a0 */ 0,0,0,1,1,1,1,1,0,0,0,1,1,1,1,1, /* af */
  /* b0 */ 1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1, /* bf */
  /* c0 */ 1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0, /* cf */
  /* d0 */ 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, /* df */
  /* e0 */ 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, /* ef */
  /* f0 */ 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0  /* ff */
  /*	   -------------------------------	  */
  /*	   0 1 2 3 4 5 6 7 8 9 a b c d e f	  */
};

static int amd64_syscall_p (const struct amd64_insn *insn, int *lengthp);

static int
rex_prefix_p (gdb_byte pfx)
{
  return REX_PREFIX_P (pfx);
}

/* Skip the legacy instruction prefixes in INSN.
   We assume INSN is properly sentineled so we don't have to worry
   about falling off the end of the buffer.  */

static gdb_byte *
amd64_skip_prefixes (gdb_byte *insn)
{
  while (1)
    {
      switch (*insn)
	{
	case DATA_PREFIX_OPCODE:
	case ADDR_PREFIX_OPCODE:
	case CS_PREFIX_OPCODE:
	case DS_PREFIX_OPCODE:
	case ES_PREFIX_OPCODE:
	case FS_PREFIX_OPCODE:
	case GS_PREFIX_OPCODE:
	case SS_PREFIX_OPCODE:
	case LOCK_PREFIX_OPCODE:
	case REPE_PREFIX_OPCODE:
	case REPNE_PREFIX_OPCODE:
	  ++insn;
	  continue;
	default:
	  break;
	}
      break;
    }

  return insn;
}

/* fprintf-function for amd64_insn_length.
   This function is a nop, we don't want to print anything, we just want to
   compute the length of the insn.  */

static int ATTR_FORMAT (printf, 2, 3)
amd64_insn_length_fprintf (void *stream, const char *format, ...)
{
  return 0;
}

/* Initialize a struct disassemble_info for amd64_insn_length.	*/

static void
amd64_insn_length_init_dis (struct gdbarch *gdbarch,
			    struct disassemble_info *di,
			    const gdb_byte *insn, int max_len,
			    CORE_ADDR addr)
{
  init_disassemble_info (di, NULL, amd64_insn_length_fprintf);

  /* init_disassemble_info installs buffer_read_memory, etc.
     so we don't need to do that here.
     The cast is necessary until disassemble_info is const-ified.  */
  di->buffer = (gdb_byte *) insn;
  di->buffer_length = max_len;
  di->buffer_vma = addr;

  di->arch = gdbarch_bfd_arch_info (gdbarch)->arch;
  di->mach = gdbarch_bfd_arch_info (gdbarch)->mach;
  di->endian = gdbarch_byte_order (gdbarch);
  di->endian_code = gdbarch_byte_order_for_code (gdbarch);

  disassemble_init_for_target (di);
}

/* Return the length in bytes of INSN.
   MAX_LEN is the size of the buffer containing INSN.
   libopcodes currently doesn't export a utility to compute the
   instruction length, so use the disassembler until then.  */

static int
amd64_insn_length (struct gdbarch *gdbarch,
		   const gdb_byte *insn, int max_len, CORE_ADDR addr)
{
  struct disassemble_info di;

  amd64_insn_length_init_dis (gdbarch, &di, insn, max_len, addr);

  return gdbarch_print_insn (gdbarch, addr, &di);
}

/* Return an integer register (other than RSP) that is unused as an input
   operand in INSN.
   In order to not require adding a rex prefix if the insn doesn't already
   have one, the result is restricted to RAX ... RDI, sans RSP.
   The register numbering of the result follows architecture ordering,
   e.g. RDI = 7.  */

static int
amd64_get_unused_input_int_reg (const struct amd64_insn *details)
{
  /* 1 bit for each reg */
  int used_regs_mask = 0;

  /* There can be at most 3 int regs used as inputs in an insn, and we have
     7 to choose from (RAX ... RDI, sans RSP).
     This allows us to take a conservative approach and keep things simple.
     E.g. By avoiding RAX, we don't have to specifically watch for opcodes
     that implicitly specify RAX.  */

  /* Avoid RAX.  */
  used_regs_mask |= 1 << EAX_REG_NUM;
  /* Similarily avoid RDX, implicit operand in divides.  */
  used_regs_mask |= 1 << EDX_REG_NUM;
  /* Avoid RSP.  */
  used_regs_mask |= 1 << ESP_REG_NUM;

  /* If the opcode is one byte long and there's no ModRM byte,
     assume the opcode specifies a register.  */
  if (details->opcode_len == 1 && details->modrm_offset == -1)
    used_regs_mask |= 1 << (details->raw_insn[details->opcode_offset] & 7);

  /* Mark used regs in the modrm/sib bytes.  */
  if (details->modrm_offset != -1)
    {
      int modrm = details->raw_insn[details->modrm_offset];
      int mod = MODRM_MOD_FIELD (modrm);
      int reg = MODRM_REG_FIELD (modrm);
      int rm = MODRM_RM_FIELD (modrm);
      int have_sib = mod != 3 && rm == 4;

      /* Assume the reg field of the modrm byte specifies a register.  */
      used_regs_mask |= 1 << reg;

      if (have_sib)
	{
	  int base = SIB_BASE_FIELD (details->raw_insn[details->modrm_offset + 1]);
	  int index = SIB_INDEX_FIELD (details->raw_insn[details->modrm_offset + 1]);
	  used_regs_mask |= 1 << base;
	  used_regs_mask |= 1 << index;
	}
      else
	{
	  used_regs_mask |= 1 << rm;
	}
    }

  gdb_assert (used_regs_mask < 256);
  gdb_assert (used_regs_mask != 255);

  /* Finally, find a free reg.  */
  {
    int i;

    for (i = 0; i < 8; ++i)
      {
	if (! (used_regs_mask & (1 << i)))
	  return i;
      }

    /* We shouldn't get here.  */
    internal_error (__FILE__, __LINE__, _("unable to find free reg"));
  }
}

/* Extract the details of INSN that we need.  */

static void
amd64_get_insn_details (gdb_byte *insn, struct amd64_insn *details)
{
  gdb_byte *start = insn;
  int need_modrm;

  details->raw_insn = insn;

  details->opcode_len = -1;
  details->rex_offset = -1;
  details->opcode_offset = -1;
  details->modrm_offset = -1;

  /* Skip legacy instruction prefixes.  */
  insn = amd64_skip_prefixes (insn);

  /* Skip REX instruction prefix.  */
  if (rex_prefix_p (*insn))
    {
      details->rex_offset = insn - start;
      ++insn;
    }

  details->opcode_offset = insn - start;

  if (*insn == TWO_BYTE_OPCODE_ESCAPE)
    {
      /* Two or three-byte opcode.  */
      ++insn;
      need_modrm = twobyte_has_modrm[*insn];

      /* Check for three-byte opcode.  */
      switch (*insn)
	{
	case 0x24:
	case 0x25:
	case 0x38:
	case 0x3a:
	case 0x7a:
	case 0x7b:
	  ++insn;
	  details->opcode_len = 3;
	  break;
	default:
	  details->opcode_len = 2;
	  break;
	}
    }
  else
    {
      /* One-byte opcode.  */
      need_modrm = onebyte_has_modrm[*insn];
      details->opcode_len = 1;
    }

  if (need_modrm)
    {
      ++insn;
      details->modrm_offset = insn - start;
    }
}

/* Update %rip-relative addressing in INSN.

   %rip-relative addressing only uses a 32-bit displacement.
   32 bits is not enough to be guaranteed to cover the distance between where
   the real instruction is and where its copy is.
   Convert the insn to use base+disp addressing.
   We set base = pc + insn_length so we can leave disp unchanged.  */

static void
fixup_riprel (struct gdbarch *gdbarch, struct displaced_step_closure *dsc,
	      CORE_ADDR from, CORE_ADDR to, struct regcache *regs)
{
  enum bfd_endian byte_order = gdbarch_byte_order (gdbarch);
  const struct amd64_insn *insn_details = &dsc->insn_details;
  int modrm_offset = insn_details->modrm_offset;
  gdb_byte *insn = insn_details->raw_insn + modrm_offset;
  CORE_ADDR rip_base;
  int32_t disp;
  int insn_length;
  int arch_tmp_regno, tmp_regno;
  ULONGEST orig_value;

  /* %rip+disp32 addressing mode, displacement follows ModRM byte.  */
  ++insn;

  /* Compute the rip-relative address.	*/
  disp = extract_signed_integer (insn, sizeof (int32_t), byte_order);
  insn_length = amd64_insn_length (gdbarch, dsc->insn_buf, dsc->max_len, from);
  rip_base = from + insn_length;

  /* We need a register to hold the address.
     Pick one not used in the insn.
     NOTE: arch_tmp_regno uses architecture ordering, e.g. RDI = 7.  */
  arch_tmp_regno = amd64_get_unused_input_int_reg (insn_details);
  tmp_regno = amd64_arch_reg_to_regnum (arch_tmp_regno);

  /* REX.B should be unset as we were using rip-relative addressing,
     but ensure it's unset anyway, tmp_regno is not r8-r15.  */
  if (insn_details->rex_offset != -1)
    dsc->insn_buf[insn_details->rex_offset] &= ~REX_B;

  regcache_cooked_read_unsigned (regs, tmp_regno, &orig_value);
  dsc->tmp_regno = tmp_regno;
  dsc->tmp_save = orig_value;
  dsc->tmp_used = 1;

  /* Convert the ModRM field to be base+disp.  */
  dsc->insn_buf[modrm_offset] &= ~0xc7;
  dsc->insn_buf[modrm_offset] |= 0x80 + arch_tmp_regno;

  regcache_cooked_write_unsigned (regs, tmp_regno, rip_base);

  if (debug_displaced)
    fprintf_unfiltered (gdb_stdlog, "displaced: %%rip-relative addressing used.\n"
			"displaced: using temp reg %d, old value %s, new value %s\n",
			dsc->tmp_regno, paddress (gdbarch, dsc->tmp_save),
			paddress (gdbarch, rip_base));
}

static void
fixup_displaced_copy (struct gdbarch *gdbarch,
		      struct displaced_step_closure *dsc,
		      CORE_ADDR from, CORE_ADDR to, struct regcache *regs)
{
  const struct amd64_insn *details = &dsc->insn_details;

  if (details->modrm_offset != -1)
    {
      gdb_byte modrm = details->raw_insn[details->modrm_offset];

      if ((modrm & 0xc7) == 0x05)
	{
	  /* The insn uses rip-relative addressing.
	     Deal with it.  */
	  fixup_riprel (gdbarch, dsc, from, to, regs);
	}
    }
}

struct displaced_step_closure *
amd64_displaced_step_copy_insn (struct gdbarch *gdbarch,
				CORE_ADDR from, CORE_ADDR to,
				struct regcache *regs)
{
  int len = gdbarch_max_insn_length (gdbarch);
  /* Extra space for sentinels so fixup_{riprel,displaced_copy don't have to
     continually watch for running off the end of the buffer.  */
  int fixup_sentinel_space = len;
  struct displaced_step_closure *dsc =
    xmalloc (sizeof (*dsc) + len + fixup_sentinel_space);
  gdb_byte *buf = &dsc->insn_buf[0];
  struct amd64_insn *details = &dsc->insn_details;

  dsc->tmp_used = 0;
  dsc->max_len = len + fixup_sentinel_space;

  read_memory (from, buf, len);

  /* Set up the sentinel space so we don't have to worry about running
     off the end of the buffer.  An excessive number of leading prefixes
     could otherwise cause this.  */
  memset (buf + len, 0, fixup_sentinel_space);

  amd64_get_insn_details (buf, details);

  /* GDB may get control back after the insn after the syscall.
     Presumably this is a kernel bug.
     If this is a syscall, make sure there's a nop afterwards.  */
  {
    int syscall_length;

    if (amd64_syscall_p (details, &syscall_length))
      buf[details->opcode_offset + syscall_length] = NOP_OPCODE;
  }

  /* Modify the insn to cope with the address where it will be executed from.
     In particular, handle any rip-relative addressing.	 */
  fixup_displaced_copy (gdbarch, dsc, from, to, regs);

  write_memory (to, buf, len);

  if (debug_displaced)
    {
      fprintf_unfiltered (gdb_stdlog, "displaced: copy %s->%s: ",
			  paddress (gdbarch, from), paddress (gdbarch, to));
      displaced_step_dump_bytes (gdb_stdlog, buf, len);
    }

  return dsc;
}

static int
amd64_absolute_jmp_p (const struct amd64_insn *details)
{
  const gdb_byte *insn = &details->raw_insn[details->opcode_offset];

  if (insn[0] == 0xff)
    {
      /* jump near, absolute indirect (/4) */
      if ((insn[1] & 0x38) == 0x20)
	return 1;

      /* jump far, absolute indirect (/5) */
      if ((insn[1] & 0x38) == 0x28)
	return 1;
    }

  return 0;
}

static int
amd64_absolute_call_p (const struct amd64_insn *details)
{
  const gdb_byte *insn = &details->raw_insn[details->opcode_offset];

  if (insn[0] == 0xff)
    {
      /* Call near, absolute indirect (/2) */
      if ((insn[1] & 0x38) == 0x10)
	return 1;

      /* Call far, absolute indirect (/3) */
      if ((insn[1] & 0x38) == 0x18)
	return 1;
    }

  return 0;
}

static int
amd64_ret_p (const struct amd64_insn *details)
{
  /* NOTE: gcc can emit "repz ; ret".  */
  const gdb_byte *insn = &details->raw_insn[details->opcode_offset];

  switch (insn[0])
    {
    case 0xc2: /* ret near, pop N bytes */
    case 0xc3: /* ret near */
    case 0xca: /* ret far, pop N bytes */
    case 0xcb: /* ret far */
    case 0xcf: /* iret */
      return 1;

    default:
      return 0;
    }
}

static int
amd64_call_p (const struct amd64_insn *details)
{
  const gdb_byte *insn = &details->raw_insn[details->opcode_offset];

  if (amd64_absolute_call_p (details))
    return 1;

  /* call near, relative */
  if (insn[0] == 0xe8)
    return 1;

  return 0;
}

/* Return non-zero if INSN is a system call, and set *LENGTHP to its
   length in bytes.  Otherwise, return zero.  */

static int
amd64_syscall_p (const struct amd64_insn *details, int *lengthp)
{
  const gdb_byte *insn = &details->raw_insn[details->opcode_offset];

  if (insn[0] == 0x0f && insn[1] == 0x05)
    {
      *lengthp = 2;
      return 1;
    }

  return 0;
}

/* Fix up the state of registers and memory after having single-stepped
   a displaced instruction.  */

void
amd64_displaced_step_fixup (struct gdbarch *gdbarch,
			    struct displaced_step_closure *dsc,
			    CORE_ADDR from, CORE_ADDR to,
			    struct regcache *regs)
{
  enum bfd_endian byte_order = gdbarch_byte_order (gdbarch);
  /* The offset we applied to the instruction's address.  */
  ULONGEST insn_offset = to - from;
  gdb_byte *insn = dsc->insn_buf;
  const struct amd64_insn *insn_details = &dsc->insn_details;

  if (debug_displaced)
    fprintf_unfiltered (gdb_stdlog,
			"displaced: fixup (%s, %s), "
			"insn = 0x%02x 0x%02x ...\n",
			paddress (gdbarch, from), paddress (gdbarch, to),
			insn[0], insn[1]);

  /* If we used a tmp reg, restore it.	*/

  if (dsc->tmp_used)
    {
      if (debug_displaced)
	fprintf_unfiltered (gdb_stdlog, "displaced: restoring reg %d to %s\n",
			    dsc->tmp_regno, paddress (gdbarch, dsc->tmp_save));
      regcache_cooked_write_unsigned (regs, dsc->tmp_regno, dsc->tmp_save);
    }

  /* The list of issues to contend with here is taken from
     resume_execution in arch/x86/kernel/kprobes.c, Linux 2.6.28.
     Yay for Free Software!  */

  /* Relocate the %rip back to the program's instruction stream,
     if necessary.  */

  /* Except in the case of absolute or indirect jump or call
     instructions, or a return instruction, the new rip is relative to
     the displaced instruction; make it relative to the original insn.
     Well, signal handler returns don't need relocation either, but we use the
     value of %rip to recognize those; see below.  */
  if (! amd64_absolute_jmp_p (insn_details)
      && ! amd64_absolute_call_p (insn_details)
      && ! amd64_ret_p (insn_details))
    {
      ULONGEST orig_rip;
      int insn_len;

      regcache_cooked_read_unsigned (regs, AMD64_RIP_REGNUM, &orig_rip);

      /* A signal trampoline system call changes the %rip, resuming
	 execution of the main program after the signal handler has
	 returned.  That makes them like 'return' instructions; we
	 shouldn't relocate %rip.

	 But most system calls don't, and we do need to relocate %rip.

	 Our heuristic for distinguishing these cases: if stepping
	 over the system call instruction left control directly after
	 the instruction, the we relocate --- control almost certainly
	 doesn't belong in the displaced copy.	Otherwise, we assume
	 the instruction has put control where it belongs, and leave
	 it unrelocated.  Goodness help us if there are PC-relative
	 system calls.	*/
      if (amd64_syscall_p (insn_details, &insn_len)
	  && orig_rip != to + insn_len
	  /* GDB can get control back after the insn after the syscall.
	     Presumably this is a kernel bug.
	     Fixup ensures its a nop, we add one to the length for it.  */
	  && orig_rip != to + insn_len + 1)
	{
	  if (debug_displaced)
	    fprintf_unfiltered (gdb_stdlog,
				"displaced: syscall changed %%rip; "
				"not relocating\n");
	}
      else
	{
	  ULONGEST rip = orig_rip - insn_offset;

	  /* If we just stepped over a breakpoint insn, we don't backup
	     the pc on purpose; this is to match behaviour without
	     stepping.  */

	  regcache_cooked_write_unsigned (regs, AMD64_RIP_REGNUM, rip);

	  if (debug_displaced)
	    fprintf_unfiltered (gdb_stdlog,
				"displaced: "
				"relocated %%rip from %s to %s\n",
				paddress (gdbarch, orig_rip),
				paddress (gdbarch, rip));
	}
    }

  /* If the instruction was PUSHFL, then the TF bit will be set in the
     pushed value, and should be cleared.  We'll leave this for later,
     since GDB already messes up the TF flag when stepping over a
     pushfl.  */

  /* If the instruction was a call, the return address now atop the
     stack is the address following the copied instruction.  We need
     to make it the address following the original instruction.	 */
  if (amd64_call_p (insn_details))
    {
      ULONGEST rsp;
      ULONGEST retaddr;
      const ULONGEST retaddr_len = 8;

      regcache_cooked_read_unsigned (regs, AMD64_RSP_REGNUM, &rsp);
      retaddr = read_memory_unsigned_integer (rsp, retaddr_len, byte_order);
      retaddr = (retaddr - insn_offset) & 0xffffffffUL;
      write_memory_unsigned_integer (rsp, retaddr_len, byte_order, retaddr);

      if (debug_displaced)
	fprintf_unfiltered (gdb_stdlog,
			    "displaced: relocated return addr at %s "
			    "to %s\n",
			    paddress (gdbarch, rsp),
			    paddress (gdbarch, retaddr));
    }
}

/* The maximum number of saved registers.  This should include %rip.  */
#define AMD64_NUM_SAVED_REGS	AMD64_NUM_GREGS

struct amd64_frame_cache
{
  /* Base address.  */
  CORE_ADDR base;
  CORE_ADDR sp_offset;
  CORE_ADDR pc;

  /* Saved registers.  */
  CORE_ADDR saved_regs[AMD64_NUM_SAVED_REGS];
  CORE_ADDR saved_sp;
  int saved_sp_reg;

  /* Do we have a frame?  */
  int frameless_p;
};

/* Initialize a frame cache.  */

static void
amd64_init_frame_cache (struct amd64_frame_cache *cache)
{
  int i;

  /* Base address.  */
  cache->base = 0;
  cache->sp_offset = -8;
  cache->pc = 0;

  /* Saved registers.  We initialize these to -1 since zero is a valid
     offset (that's where %rbp is supposed to be stored).  */
  for (i = 0; i < AMD64_NUM_SAVED_REGS; i++)
    cache->saved_regs[i] = -1;
  cache->saved_sp = 0;
  cache->saved_sp_reg = -1;

  /* Frameless until proven otherwise.  */
  cache->frameless_p = 1;
}

/* Allocate and initialize a frame cache.  */

static struct amd64_frame_cache *
amd64_alloc_frame_cache (void)
{
  struct amd64_frame_cache *cache;

  cache = FRAME_OBSTACK_ZALLOC (struct amd64_frame_cache);
  amd64_init_frame_cache (cache);
  return cache;
}

/* GCC 4.4 and later, can put code in the prologue to realign the
   stack pointer.  Check whether PC points to such code, and update
   CACHE accordingly.  Return the first instruction after the code
   sequence or CURRENT_PC, whichever is smaller.  If we don't
   recognize the code, return PC.  */

static CORE_ADDR
amd64_analyze_stack_align (CORE_ADDR pc, CORE_ADDR current_pc,
			   struct amd64_frame_cache *cache)
{
  /* There are 2 code sequences to re-align stack before the frame
     gets set up:

	1. Use a caller-saved saved register:

		leaq  8(%rsp), %reg
		andq  $-XXX, %rsp
		pushq -8(%reg)

	2. Use a callee-saved saved register:

		pushq %reg
		leaq  16(%rsp), %reg
		andq  $-XXX, %rsp
		pushq -8(%reg)

     "andq $-XXX, %rsp" can be either 4 bytes or 7 bytes:
     
     	0x48 0x83 0xe4 0xf0			andq $-16, %rsp
     	0x48 0x81 0xe4 0x00 0xff 0xff 0xff	andq $-256, %rsp
   */

  gdb_byte buf[18];
  int reg, r;
  int offset, offset_and;

  if (target_read_memory (pc, buf, sizeof buf))
    return pc;

  /* Check caller-saved saved register.  The first instruction has
     to be "leaq 8(%rsp), %reg".  */
  if ((buf[0] & 0xfb) == 0x48
      && buf[1] == 0x8d
      && buf[3] == 0x24
      && buf[4] == 0x8)
    {
      /* MOD must be binary 10 and R/M must be binary 100.  */
      if ((buf[2] & 0xc7) != 0x44)
	return pc;

      /* REG has register number.  */
      reg = (buf[2] >> 3) & 7;

      /* Check the REX.R bit.  */
      if (buf[0] == 0x4c)
	reg += 8;

      offset = 5;
    }
  else
    {
      /* Check callee-saved saved register.  The first instruction
	 has to be "pushq %reg".  */
      reg = 0;
      if ((buf[0] & 0xf8) == 0x50)
	offset = 0;
      else if ((buf[0] & 0xf6) == 0x40
	       && (buf[1] & 0xf8) == 0x50)
	{
	  /* Check the REX.B bit.  */
	  if ((buf[0] & 1) != 0)
	    reg = 8;

	  offset = 1;
	}
      else
	return pc;

      /* Get register.  */
      reg += buf[offset] & 0x7;

      offset++;

      /* The next instruction has to be "leaq 16(%rsp), %reg".  */
      if ((buf[offset] & 0xfb) != 0x48
	  || buf[offset + 1] != 0x8d
	  || buf[offset + 3] != 0x24
	  || buf[offset + 4] != 0x10)
	return pc;

      /* MOD must be binary 10 and R/M must be binary 100.  */
      if ((buf[offset + 2] & 0xc7) != 0x44)
	return pc;
      
      /* REG has register number.  */
      r = (buf[offset + 2] >> 3) & 7;

      /* Check the REX.R bit.  */
      if (buf[offset] == 0x4c)
	r += 8;

      /* Registers in pushq and leaq have to be the same.  */
      if (reg != r)
	return pc;

      offset += 5;
    }

  /* Rigister can't be %rsp nor %rbp.  */
  if (reg == 4 || reg == 5)
    return pc;

  /* The next instruction has to be "andq $-XXX, %rsp".  */
  if (buf[offset] != 0x48
      || buf[offset + 2] != 0xe4
      || (buf[offset + 1] != 0x81 && buf[offset + 1] != 0x83))
    return pc;

  offset_and = offset;
  offset += buf[offset + 1] == 0x81 ? 7 : 4;

  /* The next instruction has to be "pushq -8(%reg)".  */
  r = 0;
  if (buf[offset] == 0xff)
    offset++;
  else if ((buf[offset] & 0xf6) == 0x40
	   && buf[offset + 1] == 0xff)
    {
      /* Check the REX.B bit.  */
      if ((buf[offset] & 0x1) != 0)
	r = 8;
      offset += 2;
    }
  else
    return pc;

  /* 8bit -8 is 0xf8.  REG must be binary 110 and MOD must be binary
     01.  */
  if (buf[offset + 1] != 0xf8
      || (buf[offset] & 0xf8) != 0x70)
    return pc;

  /* R/M has register.  */
  r += buf[offset] & 7;

  /* Registers in leaq and pushq have to be the same.  */
  if (reg != r)
    return pc;

  if (current_pc > pc + offset_and)
    cache->saved_sp_reg = amd64_arch_reg_to_regnum (reg);

  return min (pc + offset + 2, current_pc);
}

/* Do a limited analysis of the prologue at PC and update CACHE
   accordingly.  Bail out early if CURRENT_PC is reached.  Return the
   address where the analysis stopped.

   We will handle only functions beginning with:

      pushq %rbp        0x55
      movq %rsp, %rbp   0x48 0x89 0xe5

   Any function that doesn't start with this sequence will be assumed
   to have no prologue and thus no valid frame pointer in %rbp.  */

static CORE_ADDR
amd64_analyze_prologue (struct gdbarch *gdbarch,
			CORE_ADDR pc, CORE_ADDR current_pc,
			struct amd64_frame_cache *cache)
{
  enum bfd_endian byte_order = gdbarch_byte_order (gdbarch);
  static gdb_byte proto[3] = { 0x48, 0x89, 0xe5 }; /* movq %rsp, %rbp */
  gdb_byte buf[3];
  gdb_byte op;

  if (current_pc <= pc)
    return current_pc;

  pc = amd64_analyze_stack_align (pc, current_pc, cache);

  op = read_memory_unsigned_integer (pc, 1, byte_order);

  if (op == 0x55)		/* pushq %rbp */
    {
      /* Take into account that we've executed the `pushq %rbp' that
         starts this instruction sequence.  */
      cache->saved_regs[AMD64_RBP_REGNUM] = 0;
      cache->sp_offset += 8;

      /* If that's all, return now.  */
      if (current_pc <= pc + 1)
        return current_pc;

      /* Check for `movq %rsp, %rbp'.  */
      read_memory (pc + 1, buf, 3);
      if (memcmp (buf, proto, 3) != 0)
	return pc + 1;

      /* OK, we actually have a frame.  */
      cache->frameless_p = 0;
      return pc + 4;
    }

  return pc;
}

/* Return PC of first real instruction.  */

static CORE_ADDR
amd64_skip_prologue (struct gdbarch *gdbarch, CORE_ADDR start_pc)
{
  struct amd64_frame_cache cache;
  CORE_ADDR pc;

  amd64_init_frame_cache (&cache);
  pc = amd64_analyze_prologue (gdbarch, start_pc, 0xffffffffffffffffLL,
			       &cache);
  if (cache.frameless_p)
    return start_pc;

  return pc;
}


/* Normal frames.  */

static struct amd64_frame_cache *
amd64_frame_cache (struct frame_info *this_frame, void **this_cache)
{
  struct gdbarch *gdbarch = get_frame_arch (this_frame);
  enum bfd_endian byte_order = gdbarch_byte_order (gdbarch);
  struct amd64_frame_cache *cache;
  gdb_byte buf[8];
  int i;

  if (*this_cache)
    return *this_cache;

  cache = amd64_alloc_frame_cache ();
  *this_cache = cache;

  cache->pc = get_frame_func (this_frame);
  if (cache->pc != 0)
    amd64_analyze_prologue (gdbarch, cache->pc, get_frame_pc (this_frame),
			    cache);

  if (cache->saved_sp_reg != -1)
    {
      /* Stack pointer has been saved.  */
      get_frame_register (this_frame, cache->saved_sp_reg, buf);
      cache->saved_sp = extract_unsigned_integer(buf, 8, byte_order);
    }

  if (cache->frameless_p)
    {
      /* We didn't find a valid frame.  If we're at the start of a
	 function, or somewhere half-way its prologue, the function's
	 frame probably hasn't been fully setup yet.  Try to
	 reconstruct the base address for the stack frame by looking
	 at the stack pointer.  For truly "frameless" functions this
	 might work too.  */

      if (cache->saved_sp_reg != -1)
	{
	  /* We're halfway aligning the stack.  */
	  cache->base = ((cache->saved_sp - 8) & 0xfffffffffffffff0LL) - 8;
	  cache->saved_regs[AMD64_RIP_REGNUM] = cache->saved_sp - 8;

	  /* This will be added back below.  */
	  cache->saved_regs[AMD64_RIP_REGNUM] -= cache->base;
	}
      else
	{
	  get_frame_register (this_frame, AMD64_RSP_REGNUM, buf);
	  cache->base = extract_unsigned_integer (buf, 8, byte_order)
			+ cache->sp_offset;
	}
    }
  else
    {
      get_frame_register (this_frame, AMD64_RBP_REGNUM, buf);
      cache->base = extract_unsigned_integer (buf, 8, byte_order);
    }

  /* Now that we have the base address for the stack frame we can
     calculate the value of %rsp in the calling frame.  */
  cache->saved_sp = cache->base + 16;

  /* For normal frames, %rip is stored at 8(%rbp).  If we don't have a
     frame we find it at the same offset from the reconstructed base
     address.  If we're halfway aligning the stack, %rip is handled
     differently (see above).  */
  if (!cache->frameless_p || cache->saved_sp_reg == -1)
    cache->saved_regs[AMD64_RIP_REGNUM] = 8;

  /* Adjust all the saved registers such that they contain addresses
     instead of offsets.  */
  for (i = 0; i < AMD64_NUM_SAVED_REGS; i++)
    if (cache->saved_regs[i] != -1)
      cache->saved_regs[i] += cache->base;

  return cache;
}

static void
amd64_frame_this_id (struct frame_info *this_frame, void **this_cache,
		     struct frame_id *this_id)
{
  struct amd64_frame_cache *cache =
    amd64_frame_cache (this_frame, this_cache);

  /* This marks the outermost frame.  */
  if (cache->base == 0)
    return;

  (*this_id) = frame_id_build (cache->base + 16, cache->pc);
}

static struct value *
amd64_frame_prev_register (struct frame_info *this_frame, void **this_cache,
			   int regnum)
{
  struct gdbarch *gdbarch = get_frame_arch (this_frame);
  struct amd64_frame_cache *cache =
    amd64_frame_cache (this_frame, this_cache);

  gdb_assert (regnum >= 0);

  if (regnum == gdbarch_sp_regnum (gdbarch) && cache->saved_sp)
    return frame_unwind_got_constant (this_frame, regnum, cache->saved_sp);

  if (regnum < AMD64_NUM_SAVED_REGS && cache->saved_regs[regnum] != -1)
    return frame_unwind_got_memory (this_frame, regnum,
				    cache->saved_regs[regnum]);

  return frame_unwind_got_register (this_frame, regnum, regnum);
}

static const struct frame_unwind amd64_frame_unwind =
{
  NORMAL_FRAME,
  amd64_frame_this_id,
  amd64_frame_prev_register,
  NULL,
  default_frame_sniffer
};


/* Signal trampolines.  */

/* FIXME: kettenis/20030419: Perhaps, we can unify the 32-bit and
   64-bit variants.  This would require using identical frame caches
   on both platforms.  */

static struct amd64_frame_cache *
amd64_sigtramp_frame_cache (struct frame_info *this_frame, void **this_cache)
{
  struct gdbarch *gdbarch = get_frame_arch (this_frame);
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);
  enum bfd_endian byte_order = gdbarch_byte_order (gdbarch);
  struct amd64_frame_cache *cache;
  CORE_ADDR addr;
  gdb_byte buf[8];
  int i;

  if (*this_cache)
    return *this_cache;

  cache = amd64_alloc_frame_cache ();

  get_frame_register (this_frame, AMD64_RSP_REGNUM, buf);
  cache->base = extract_unsigned_integer (buf, 8, byte_order) - 8;

  addr = tdep->sigcontext_addr (this_frame);
  gdb_assert (tdep->sc_reg_offset);
  gdb_assert (tdep->sc_num_regs <= AMD64_NUM_SAVED_REGS);
  for (i = 0; i < tdep->sc_num_regs; i++)
    if (tdep->sc_reg_offset[i] != -1)
      cache->saved_regs[i] = addr + tdep->sc_reg_offset[i];

  *this_cache = cache;
  return cache;
}

static void
amd64_sigtramp_frame_this_id (struct frame_info *this_frame,
			      void **this_cache, struct frame_id *this_id)
{
  struct amd64_frame_cache *cache =
    amd64_sigtramp_frame_cache (this_frame, this_cache);

  (*this_id) = frame_id_build (cache->base + 16, get_frame_pc (this_frame));
}

static struct value *
amd64_sigtramp_frame_prev_register (struct frame_info *this_frame,
				    void **this_cache, int regnum)
{
  /* Make sure we've initialized the cache.  */
  amd64_sigtramp_frame_cache (this_frame, this_cache);

  return amd64_frame_prev_register (this_frame, this_cache, regnum);
}

static int
amd64_sigtramp_frame_sniffer (const struct frame_unwind *self,
			      struct frame_info *this_frame,
			      void **this_cache)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (get_frame_arch (this_frame));

  /* We shouldn't even bother if we don't have a sigcontext_addr
     handler.  */
  if (tdep->sigcontext_addr == NULL)
    return 0;

  if (tdep->sigtramp_p != NULL)
    {
      if (tdep->sigtramp_p (this_frame))
	return 1;
    }

  if (tdep->sigtramp_start != 0)
    {
      CORE_ADDR pc = get_frame_pc (this_frame);

      gdb_assert (tdep->sigtramp_end != 0);
      if (pc >= tdep->sigtramp_start && pc < tdep->sigtramp_end)
	return 1;
    }

  return 0;
}

static const struct frame_unwind amd64_sigtramp_frame_unwind =
{
  SIGTRAMP_FRAME,
  amd64_sigtramp_frame_this_id,
  amd64_sigtramp_frame_prev_register,
  NULL,
  amd64_sigtramp_frame_sniffer
};


static CORE_ADDR
amd64_frame_base_address (struct frame_info *this_frame, void **this_cache)
{
  struct amd64_frame_cache *cache =
    amd64_frame_cache (this_frame, this_cache);

  return cache->base;
}

static const struct frame_base amd64_frame_base =
{
  &amd64_frame_unwind,
  amd64_frame_base_address,
  amd64_frame_base_address,
  amd64_frame_base_address
};

static struct frame_id
amd64_dummy_id (struct gdbarch *gdbarch, struct frame_info *this_frame)
{
  CORE_ADDR fp;

  fp = get_frame_register_unsigned (this_frame, AMD64_RBP_REGNUM);

  return frame_id_build (fp + 16, get_frame_pc (this_frame));
}

/* 16 byte align the SP per frame requirements.  */

static CORE_ADDR
amd64_frame_align (struct gdbarch *gdbarch, CORE_ADDR sp)
{
  return sp & -(CORE_ADDR)16;
}


/* Supply register REGNUM from the buffer specified by FPREGS and LEN
   in the floating-point register set REGSET to register cache
   REGCACHE.  If REGNUM is -1, do this for all registers in REGSET.  */

static void
amd64_supply_fpregset (const struct regset *regset, struct regcache *regcache,
		       int regnum, const void *fpregs, size_t len)
{
  const struct gdbarch_tdep *tdep = gdbarch_tdep (regset->arch);

  gdb_assert (len == tdep->sizeof_fpregset);
  amd64_supply_fxsave (regcache, regnum, fpregs);
}

/* Collect register REGNUM from the register cache REGCACHE and store
   it in the buffer specified by FPREGS and LEN as described by the
   floating-point register set REGSET.  If REGNUM is -1, do this for
   all registers in REGSET.  */

static void
amd64_collect_fpregset (const struct regset *regset,
			const struct regcache *regcache,
			int regnum, void *fpregs, size_t len)
{
  const struct gdbarch_tdep *tdep = gdbarch_tdep (regset->arch);

  gdb_assert (len == tdep->sizeof_fpregset);
  amd64_collect_fxsave (regcache, regnum, fpregs);
}

/* Return the appropriate register set for the core section identified
   by SECT_NAME and SECT_SIZE.  */

static const struct regset *
amd64_regset_from_core_section (struct gdbarch *gdbarch,
				const char *sect_name, size_t sect_size)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  if (strcmp (sect_name, ".reg2") == 0 && sect_size == tdep->sizeof_fpregset)
    {
      if (tdep->fpregset == NULL)
	tdep->fpregset = regset_alloc (gdbarch, amd64_supply_fpregset,
				       amd64_collect_fpregset);

      return tdep->fpregset;
    }

  return i386_regset_from_core_section (gdbarch, sect_name, sect_size);
}


/* Figure out where the longjmp will land.  Slurp the jmp_buf out of
   %rdi.  We expect its value to be a pointer to the jmp_buf structure
   from which we extract the address that we will land at.  This
   address is copied into PC.  This routine returns non-zero on
   success.  */

static int
amd64_get_longjmp_target (struct frame_info *frame, CORE_ADDR *pc)
{
  gdb_byte buf[8];
  CORE_ADDR jb_addr;
  struct gdbarch *gdbarch = get_frame_arch (frame);
  int jb_pc_offset = gdbarch_tdep (gdbarch)->jb_pc_offset;
  int len = TYPE_LENGTH (builtin_type (gdbarch)->builtin_func_ptr);

  /* If JB_PC_OFFSET is -1, we have no way to find out where the
     longjmp will land.	 */
  if (jb_pc_offset == -1)
    return 0;

  get_frame_register (frame, AMD64_RDI_REGNUM, buf);
  jb_addr= extract_typed_address
	    (buf, builtin_type (gdbarch)->builtin_data_ptr);
  if (target_read_memory (jb_addr + jb_pc_offset, buf, len))
    return 0;

  *pc = extract_typed_address (buf, builtin_type (gdbarch)->builtin_func_ptr);

  return 1;
}

void
amd64_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  /* AMD64 generally uses `fxsave' instead of `fsave' for saving its
     floating-point registers.  */
  tdep->sizeof_fpregset = I387_SIZEOF_FXSAVE;

  /* AMD64 has an FPU and 16 SSE registers.  */
  tdep->st0_regnum = AMD64_ST0_REGNUM;
  tdep->num_xmm_regs = 16;

  /* This is what all the fuss is about.  */
  set_gdbarch_long_bit (gdbarch, 64);
  set_gdbarch_long_long_bit (gdbarch, 64);
  set_gdbarch_ptr_bit (gdbarch, 64);

  /* In contrast to the i386, on AMD64 a `long double' actually takes
     up 128 bits, even though it's still based on the i387 extended
     floating-point format which has only 80 significant bits.  */
  set_gdbarch_long_double_bit (gdbarch, 128);

  set_gdbarch_num_regs (gdbarch, AMD64_NUM_REGS);
  set_gdbarch_register_name (gdbarch, amd64_register_name);
  set_gdbarch_register_type (gdbarch, amd64_register_type);

  /* Register numbers of various important registers.  */
  set_gdbarch_sp_regnum (gdbarch, AMD64_RSP_REGNUM); /* %rsp */
  set_gdbarch_pc_regnum (gdbarch, AMD64_RIP_REGNUM); /* %rip */
  set_gdbarch_ps_regnum (gdbarch, AMD64_EFLAGS_REGNUM); /* %eflags */
  set_gdbarch_fp0_regnum (gdbarch, AMD64_ST0_REGNUM); /* %st(0) */

  /* The "default" register numbering scheme for AMD64 is referred to
     as the "DWARF Register Number Mapping" in the System V psABI.
     The preferred debugging format for all known AMD64 targets is
     actually DWARF2, and GCC doesn't seem to support DWARF (that is
     DWARF-1), but we provide the same mapping just in case.  This
     mapping is also used for stabs, which GCC does support.  */
  set_gdbarch_stab_reg_to_regnum (gdbarch, amd64_dwarf_reg_to_regnum);
  set_gdbarch_dwarf2_reg_to_regnum (gdbarch, amd64_dwarf_reg_to_regnum);

  /* We don't override SDB_REG_RO_REGNUM, since COFF doesn't seem to
     be in use on any of the supported AMD64 targets.  */

  /* Call dummy code.  */
  set_gdbarch_push_dummy_call (gdbarch, amd64_push_dummy_call);
  set_gdbarch_frame_align (gdbarch, amd64_frame_align);
  set_gdbarch_frame_red_zone_size (gdbarch, 128);

  set_gdbarch_convert_register_p (gdbarch, i387_convert_register_p);
  set_gdbarch_register_to_value (gdbarch, i387_register_to_value);
  set_gdbarch_value_to_register (gdbarch, i387_value_to_register);

  set_gdbarch_return_value (gdbarch, amd64_return_value);

  set_gdbarch_skip_prologue (gdbarch, amd64_skip_prologue);

  /* Avoid wiring in the MMX registers for now.  */
  set_gdbarch_num_pseudo_regs (gdbarch, 0);
  tdep->mm0_regnum = -1;

  set_gdbarch_dummy_id (gdbarch, amd64_dummy_id);

  frame_unwind_append_unwinder (gdbarch, &amd64_sigtramp_frame_unwind);
  frame_unwind_append_unwinder (gdbarch, &amd64_frame_unwind);
  frame_base_set_default (gdbarch, &amd64_frame_base);

  /* If we have a register mapping, enable the generic core file support.  */
  if (tdep->gregset_reg_offset)
    set_gdbarch_regset_from_core_section (gdbarch,
					  amd64_regset_from_core_section);

  set_gdbarch_get_longjmp_target (gdbarch, amd64_get_longjmp_target);
}


/* The 64-bit FXSAVE format differs from the 32-bit format in the
   sense that the instruction pointer and data pointer are simply
   64-bit offsets into the code segment and the data segment instead
   of a selector offset pair.  The functions below store the upper 32
   bits of these pointers (instead of just the 16-bits of the segment
   selector).  */

/* Fill register REGNUM in REGCACHE with the appropriate
   floating-point or SSE register value from *FXSAVE.  If REGNUM is
   -1, do this for all registers.  This function masks off any of the
   reserved bits in *FXSAVE.  */

void
amd64_supply_fxsave (struct regcache *regcache, int regnum,
		     const void *fxsave)
{
  struct gdbarch *gdbarch = get_regcache_arch (regcache);
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  i387_supply_fxsave (regcache, regnum, fxsave);

  if (fxsave && gdbarch_ptr_bit (gdbarch) == 64)
    {
      const gdb_byte *regs = fxsave;

      if (regnum == -1 || regnum == I387_FISEG_REGNUM (tdep))
	regcache_raw_supply (regcache, I387_FISEG_REGNUM (tdep), regs + 12);
      if (regnum == -1 || regnum == I387_FOSEG_REGNUM (tdep))
	regcache_raw_supply (regcache, I387_FOSEG_REGNUM (tdep), regs + 20);
    }
}

/* Fill register REGNUM (if it is a floating-point or SSE register) in
   *FXSAVE with the value from REGCACHE.  If REGNUM is -1, do this for
   all registers.  This function doesn't touch any of the reserved
   bits in *FXSAVE.  */

void
amd64_collect_fxsave (const struct regcache *regcache, int regnum,
		      void *fxsave)
{
  struct gdbarch *gdbarch = get_regcache_arch (regcache);
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);
  gdb_byte *regs = fxsave;

  i387_collect_fxsave (regcache, regnum, fxsave);

  if (gdbarch_ptr_bit (gdbarch) == 64)
    {
      if (regnum == -1 || regnum == I387_FISEG_REGNUM (tdep))
	regcache_raw_collect (regcache, I387_FISEG_REGNUM (tdep), regs + 12);
      if (regnum == -1 || regnum == I387_FOSEG_REGNUM (tdep))
	regcache_raw_collect (regcache, I387_FOSEG_REGNUM (tdep), regs + 20);
    }
}

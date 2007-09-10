/* Definitions of target machine for GNU compiler.  MIPS R3000 version with
   GOFAST floating point library.
   Copyright (C) 1994 Free Software Foundation, Inc.

This file is part of GNU CC.

GNU CC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU CC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU CC; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

#undef GCC_DRIVER_HOST_INITIALIZATION
#define GCC_DRIVER_HOST_INITIALIZATION \
  do { \
    char *qnx_host = getenv ("QNX_HOST"); \
    char *qnx_target = getenv ("QNX_TARGET"); \
    if (qnx_host == NULL && qnx_target == NULL) \
      fatal ("error: environment variables QNX_HOST and QNX_TARGET not defined"); \
    if (qnx_host == NULL) \
      fatal ("environment variable QNX_HOST not defined"); \
    if (qnx_target == NULL) \
      fatal ("environment variable QNX_TARGET not defined"); \
    standard_exec_prefix = concat (qnx_host, "/usr/lib/gcc-lib/", NULL); \
    standard_startfile_prefix = concat (qnx_host, "/usr/lib/", NULL); \
    tooldir_base_prefix = concat (qnx_host, "/usr/", NULL); \
    add_prefix (&exec_prefixes, concat (qnx_host, "/usr/ntomips/bin/", NULL), \
                "BINUTILS", 0, 0, NULL_PTR); \
    add_prefix (&exec_prefixes, concat (qnx_host, "/usr/bin/", NULL), \
                NULL, 0, 0, NULL_PTR); \
  } while (0)

/* Use ELF.  */
#define OBJECT_FORMAT_ELF
#define HAS_INIT_SECTION

#define HAVE_ATEXIT

/* We want to use DWARF, but we're not exactly sure how to yet */
#define DBX_DEBGUGING_INFO
#define DWARF_DEBUGGING_INFO
#define DWARF2_DEBUGGING_INFO

/* Work around assembler forward label references generated in exception
   handling code. */
#define DWARF2_UNWIND_INFO 0

#ifndef PREFERRED_DEBUGGING_TYPE
#define PREFERRED_DEBUGGING_TYPE DBX_DEBUG
#endif

#ifndef SET_ASM_OP
#define SET_ASM_OP 	"\t.set"
#endif

#ifndef INIT_SECTION_ASM_OP
#define INIT_SECTION_ASM_OP "\t.section \".init\",\"ax\""
#endif

#ifndef FINI_SECTION_ASM_OP
#define FINI_SECTION_ASM_OP "\t.section \".fini\",\"ax\""
#endif

#ifndef TARGET_DEFAULT
#define TARGET_DEFAULT 0
#endif

#include "mips/mips.h"
#include "dbxelf.h"

/* These don't work in the presence of $gp relative calls */
#undef ASM_OUTPUT_REG_PUSH
#undef ASM_OUTPUT_REG_POP

/* Support the ctors/dtors and other sections.  */
 
/* Define the pseudo-ops used to switch to the .ctors and .dtors sections.
 
   Note that we want to give these sections the SHF_WRITE attribute
   because these sections will actually contain data (i.e. tables of
   addresses of functions in the current root executable or shared library
   file) and, in the case of a shared library, the relocatable addresses
   will have to be properly resolved/relocated (and then written into) by
   the dynamic linker when it actually attaches the given shared library
   to the executing process.  (Note that on SVR4, you may wish to use the
   `-z text' option to the ELF linker, when building a shared library, as
   an additional check that you are doing everything right.  But if you do
   use the `-z text' option when building a shared library, you will get
   errors unless the .ctors and .dtors sections are marked as writable
   via the SHF_WRITE attribute.)  */

#undef CTORS_SECTION_ASM_OP 
#undef DTORS_SECTION_ASM_OP  
#define CTORS_SECTION_ASM_OP    "\t.section\t.ctors,\"aw\""
#define DTORS_SECTION_ASM_OP    "\t.section\t.dtors,\"aw\""
 
/* A list of other sections which the compiler might be "in" at any
   given time.  */
#undef EXTRA_SECTIONS
#define EXTRA_SECTIONS in_sbss, in_sdata, in_rdata, in_ctors, in_dtors
 
#undef EXTRA_SECTION_FUNCTIONS
#define EXTRA_SECTION_FUNCTIONS                                         \
  SECTION_FUNCTION_TEMPLATE(sbss_section, in_sbss, SBSS_SECTION_ASM_OP) \
  SECTION_FUNCTION_TEMPLATE(sdata_section, in_sdata, SDATA_SECTION_ASM_OP) \
  SECTION_FUNCTION_TEMPLATE(rdata_section, in_rdata, RDATA_SECTION_ASM_OP) \
  SECTION_FUNCTION_TEMPLATE(ctors_section, in_ctors, CTORS_SECTION_ASM_OP) \
  SECTION_FUNCTION_TEMPLATE(dtors_section, in_dtors, DTORS_SECTION_ASM_OP)

#define SECTION_FUNCTION_TEMPLATE(FN, ENUM, OP)                               \
void FN ()                                                            \
{                                                                     \
  if (in_section != ENUM)                                             \
    {                                                                 \
      fprintf (asm_out_file, "%s\n", OP);                             \
      in_section = ENUM;                                              \
    }                                                                 \
}


/* A C statement (sans semicolon) to output an element in the table of
   global constructors.  */
#define ASM_OUTPUT_CONSTRUCTOR(FILE,NAME)                             \
  do {                                                                \
    ctors_section ();                                                 \
    fprintf (FILE, "\t%s\t", TARGET_LONG64 ? ".dword" : ".word");     \
    assemble_name (FILE, NAME);                                       \
    fprintf (FILE, "\n");                                             \
  } while (0)


/* A C statement (sans semicolon) to output an element in the table of
   global destructors.  */
#define ASM_OUTPUT_DESTRUCTOR(FILE,NAME)                              \
  do {                                                                \
    dtors_section ();                                                 \
    fprintf (FILE, "\t%s\t", TARGET_LONG64 ? ".dword" : ".word");     \
    assemble_name (FILE, NAME);                                       \
    fprintf (FILE, "\n");                                             \
  } while (0)
/* Handle #pragma weak and #pragma pack.  */
#undef  HANDLE_SYSV_PRAGMA
#define HANDLE_SYSV_PRAGMA

#define HANDLE_PRAGMA_PACK_PUSH_POP 1

#undef LIB_SPEC
#define LIB_SPEC \
"-L %$QNX_TARGET/mips%{EL:le}%{!EL:be}/lib/gcc/2.95.3 \
 -L %$QNX_TARGET/mips%{EL:le}%{!EL:be}/lib \
 -L %$QNX_TARGET/mips%{EL:le}%{!EL:be}/usr/lib \
 -L %$QNX_TARGET/mips%{EL:le}%{!EL:be}/opt/lib \
 -rpath-link %$QNX_TARGET/mips%{EL:le}%{!EL:be}/lib/gcc/2.95.3:\
%$QNX_TARGET/mips%{EL:le}%{!EL:be}/lib:\
%$QNX_TARGET/mips%{EL:le}%{!EL:be}/usr/lib:\
%$QNX_TARGET/mips%{EL:le}%{!EL:be}/opt/lib \
 %{!symbolic: -lc \
 -dn -Bstatic %{!shared: -lc} %{shared: -lcS}} "

#undef STARTFILE_SPEC
#define STARTFILE_SPEC \
"%{!shared: %$QNX_TARGET/mips%{!EL:be}%{EL:le}/lib/%{pg:m}%{p:m}crt1.o} \
%$QNX_TARGET/mips%{!EL:be}%{EL:le}/lib/crti.o \
%$QNX_TARGET/mips%{!EL:be}%{EL:le}/lib/crtbegin.o \
 -u_start"

#undef ENDFILE_SPEC
#define ENDFILE_SPEC "\
%$QNX_TARGET/mips%{!EL:be}%{EL:le}/lib/crtend.o \
%$QNX_TARGET/mips%{!EL:be}%{EL:le}/lib/crtn.o"

#undef LINK_SPEC
#define LINK_SPEC "\
%{G*} %{!EL:-EB} %{EL:-EL} %{mips1} %{mips2} %{mips3} %{mips4} \
%{bestGnum} %{shared} %{non_shared} \
%(linker_endian_spec) \
%{EL:-belf32-littlemips} %{!EL:-belf32-bigmips} \
%{MAP: -Map mapfile} \
%{static: -dn -Bstatic} \
%{!shared: --dynamic-linker /usr/lib/ldqnx.so.2} \
-melf32bmipnto"

#undef CC1_SPEC
#define CC1_SPEC "\
%{gline:%{!g:%{!g0:%{!g1:%{!g2: -g1}}}}} \
%{mips1:-mfp32 -mgp32} %{mips2:-mfp32 -mgp32}\
%{mips3:%{!msingle-float:%{!m4650:-mfp64}} -mgp64} \
%{mips4:%{!msingle-float:%{!m4650:-mfp64}} -mgp64} \
%{mfp64:%{msingle-float:%emay not use both -mfp64 and -msingle-float}} \
%{mfp64:%{m4650:%emay not use both -mfp64 and -m4650}} \
%{m4650:-mcpu=r4650} \
%{m3900:-mips1 -mcpu=r3900 -mfp32 -mgp32} \
%{G*} %{EB:-meb} %{EL:-mel} %{EB:%{EL:%emay not use both -EB and -EL}} \
%{pic-none:   -mno-half-pic} \
%{pic-lib:    -mhalf-pic} \
%{pic-extern: -mhalf-pic} \
%{pic-calls:  -mhalf-pic} \
%{fpic:	-mqnxpic} \
%{fPIC: -mqnxpic} \
%{save-temps: } \
%(subtarget_cc1_spec) \
-mgas"

#undef CPP_PREDEFINES
#define CPP_PREDEFINES "-D__QNX__ -D__QNXNTO__ -D__MIPS__ -D__ELF__"

#undef CPP_SPEC
#define CPP_SPEC "\
%{.cc:  -D__LANGUAGE_C_PLUS_PLUS -D_LANGUAGE_C_PLUS_PLUS} \
%{.cxx: -D__LANGUAGE_C_PLUS_PLUS -D_LANGUAGE_C_PLUS_PLUS} \
%{.C:   -D__LANGUAGE_C_PLUS_PLUS -D_LANGUAGE_C_PLUS_PLUS} \
%{.m:   -D__LANGUAGE_OBJECTIVE_C -D_LANGUAGE_OBJECTIVE_C -D__LANGUAGE_C -D_LANGUAGE_C} \
%{.S:   -D__LANGUAGE_ASSEMBLY -D_LANGUAGE_ASSEMBLY %{!ansi:-DLANGUAGE_ASSEMBLY}} \
%{.s:   -D__LANGUAGE_ASSEMBLY -D_LANGUAGE_ASSEMBLY %{!ansi:-DLANGUAGE_ASSEMBLY}} \
%{!.S: %{!.s: %{!.cc: %{!.cxx: %{!.C: %{!.m: -D__LANGUAGE_C -D_LANGUAGE_C %{!ansi:-DLANGUAGE_C}}}}}}} \
%(subtarget_cpp_size_spec) \
%{mips3:-U__mips -D__mips=3 -D__mips64} \
%{mips4:-U__mips -D__mips=4 -D__mips64} \
%{mgp32:-U__mips64} %{mgp64:-D__mips64} \
%{msingle-float:%{!msoft-float:-D__mips_single_float}} \
%{m4650:%{!msoft-float:-D__mips_single_float}} \
%{msoft-float:-D__mips_soft_float} \
%{mabi=eabi:-D__mips_eabi} \
%{!EL:-D__BIGENDIAN__} \
%{EB:-UMIPSEL -U_MIPSEL -U__MIPSEL -U__MIPSEL__ -D_MIPSEB -D__MIPSEB -D__MIPSEB__ %{!ansi:-DMIPSEB}} \
%{EL:-UMIPSEB -U_MIPSEB -U__MIPSEB -U__MIPSEB__ -D_MIPSEL -D__MIPSEL -D__MIPSEL__ %{!ansi:-DMIPSEL} -D__LITTLEENDIAN__} \
%(long_max_spec) \
%(subtarget_cpp_spec) \
%{fpic: -D__PIC__} %{fPIC: -D__PIC__} \
%{mqnxpic: -D__PIC__} \
-idirafter %$QNX_TARGET/usr/include"
/* Define the specs passed to the assembler */

#undef ASM_SPEC
#define ASM_SPEC "\
%{!qnxpic: %{G*}} %{!EL:-EB} %{EL:-EL} %{mips2: -trap} %{mips3: -trap} %{mips4: -trap} %{mips1} %{mips2} %{mips3} %{mips4} \
%(subtarget_asm_optimizing_spec) \
%(subtarget_asm_debugging_spec) \
%{membedded-pic} \
%{fpic: --defsym __PIC__=1} \
%{mqnxpic: --defsym __PIC__=1} \
%{mabi=32:-32}%{mabi=o32:-32}%{mabi=n32:-n32}%{mabi=64:-64}%{mabi=n64:-64} \
%(target_asm_spec) \
%(subtarget_asm_spec)"


/* Use memcpy, et. al., rather than bcopy.  */
#define TARGET_MEM_FUNCTIONS

/* We need to use .esize and .etype instead of .size and .type to
   avoid conflicting with ELF directives.  */
#undef PUT_SDB_SIZE
#define PUT_SDB_SIZE(a)					\
do {							\
  extern FILE *asm_out_text_file;			\
  fprintf (asm_out_text_file, "\t.esize\t%d;", (a));	\
} while (0)

#undef PUT_SDB_TYPE
#define PUT_SDB_TYPE(a)					\
do {							\
  extern FILE *asm_out_text_file;			\
  fprintf (asm_out_text_file, "\t.etype\t0x%x;", (a));	\
} while (0)

/* Biggest alignment supported by the object file format of this
   machine.  Use this macro to limit the alignment which can be
   specified using the `__attribute__ ((aligned (N)))' construct.  If
   not defined, the default value is `BIGGEST_ALIGNMENT'.  */

#define MAX_OFILE_ALIGNMENT (32768*8)

/* We don't want to run mips-tfile */
#undef ASM_FINAL_SPEC


#define ASM_FINISH_DECLARE_OBJECT(FILE, DECL, TOP_LEVEL, AT_END)	 \
do {									 \
     char *name = XSTR (XEXP (DECL_RTL (DECL), 0), 0);			 \
     if (!flag_inhibit_size_directive && DECL_SIZE (DECL)		 \
         && ! AT_END && TOP_LEVEL					 \
	 && DECL_INITIAL (DECL) == error_mark_node			 \
	 && !size_directive_output)					 \
       {								 \
	 size_directive_output = 1;					 \
	 fprintf (FILE, "\t%s\t ", ".size");				 \
	 assemble_name (FILE, name);					 \
	 fprintf (FILE, ",%d\n",  int_size_in_bytes (TREE_TYPE (DECL))); \
       }								 \
   } while (0)

/* Note about .weak vs. .weakext
   The mips native assemblers support .weakext, but not .weak.
   mips-elf gas supports .weak, but not .weakext.
   mips-elf gas has been changed to support both .weak and .weakext,
   but until that support is generally available, the 'if' below
   should serve. */

#define ASM_WEAKEN_LABEL(FILE,NAME) ASM_OUTPUT_WEAK_ALIAS(FILE,NAME,0)
#define ASM_OUTPUT_WEAK_ALIAS(FILE,NAME,VALUE)	\
 do {						\
  if (TARGET_GAS)                               \
      fputs ("\t.weak\t", FILE);		\
  else                                          \
      fputs ("\t.weakext\t", FILE);		\
  assemble_name (FILE, NAME);			\
  if (VALUE)					\
    {						\
      fputc (' ', FILE);			\
      assemble_name (FILE, VALUE);		\
    }						\
  fputc ('\n', FILE);				\
 } while (0)

#undef BOOL_TYPE_SIZE
#define BOOL_TYPE_SIZE POINTER_SIZE

#define NO_IMPLICIT_EXTERN_C 1

/* If defined, a C expression whose value is a string containing the
   assembler operation to identify the following data as
   uninitialized global data.  If not defined, and neither
   `ASM_OUTPUT_BSS' nor `ASM_OUTPUT_ALIGNED_BSS' are defined,
   uninitialized global data will be output in the data section if
   `-fno-common' is passed, otherwise `ASM_OUTPUT_COMMON' will be
   used.  */
#ifndef BSS_SECTION_ASM_OP
#define BSS_SECTION_ASM_OP	"\t.section\t.bss"
#endif

#define SBSS_SECTION_ASM_OP	"\t.section .sbss"

/* Like `ASM_OUTPUT_BSS' except takes the required alignment as a
   separate, explicit argument.  If you define this macro, it is used
   in place of `ASM_OUTPUT_BSS', and gives you more flexibility in
   handling the required alignment of the variable.  The alignment is
   specified as the number of bits.

   Try to use function `asm_output_aligned_bss' defined in file
   `varasm.c' when defining this macro. */
#ifndef ASM_OUTPUT_ALIGNED_BSS
#define ASM_OUTPUT_ALIGNED_BSS(FILE, DECL, NAME, SIZE, ALIGN) \
do {									\
  ASM_GLOBALIZE_LABEL (FILE, NAME);					\
  if (SIZE > 0 && SIZE <= mips_section_threshold)			\
    sbss_section ();							\
  else									\
    bss_section ();							\
  ASM_OUTPUT_ALIGN (FILE, floor_log2 (ALIGN / BITS_PER_UNIT));		\
  last_assemble_variable_decl = DECL;					\
  ASM_DECLARE_OBJECT_NAME (FILE, NAME, DECL);				\
  ASM_OUTPUT_SKIP (FILE, SIZE ? SIZE : 1);				\
} while (0)
#endif

/* Don't set libgcc.a's gthread/pthread symbols to weak, as our
   libc has them as well, and we get problems when linking static,
   as libgcc.a will get a symbol value of 0.  */
#define GTHREAD_USE_WEAK 0

#undef MIPS_DEFAULT_GVALUE
#define MIPS_DEFAULT_GVALUE 0

/* Definitions of target machine for gcc for Hitachi Super-H using ELF.
   Copyright (C) 1996 Free Software Foundation, Inc.
   Contributed by Ian Lance Taylor <ian@cygnus.com>.

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

/* Mostly like the regular SH configuration.  */
#include "sh/sh.h"

/* No SDB debugging info.  */
#undef SDB_DEBUGGING_INFO

/* Undefine some macros defined in both sh.h and svr4.h.  */
#undef IDENT_ASM_OP
#undef ASM_FILE_END
#undef ASM_OUTPUT_SOURCE_LINE
#undef DBX_OUTPUT_MAIN_SOURCE_FILE_END
#undef CTORS_SECTION_ASM_OP
#undef DTORS_SECTION_ASM_OP
#undef ASM_OUTPUT_SECTION_NAME
#undef ASM_OUTPUT_CONSTRUCTOR
#undef ASM_OUTPUT_DESTRUCTOR
#undef ASM_DECLARE_FUNCTION_NAME
#undef MAX_OFILE_ALIGNMENT

/* Be ELF-like.  */
#include "qnx-nto.h"

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
    add_prefix (&exec_prefixes, concat (qnx_host, "/usr/ntosh/bin/", NULL), \
                "BINUTILS", 0, 0, NULL_PTR); \
    add_prefix (&exec_prefixes, concat (qnx_host, "/usr/bin/", NULL), \
                NULL, 0, 0, NULL_PTR); \
  } while (0)

#undef FUNCTION_PROFILER
#define SH_MCOUNT_NAME "_mcount"
#define FUNCTION_PROFILER(STREAM,LABELNO)                              \
do                                                                     \
{                                                                      \
  if (flag_pic)                                                        \
    {                                                                  \
      fprintf (STREAM, "       mov.l   3f,r1\n");                      \
      fprintf (STREAM, "       mova    3f,r0\n");                      \
      fprintf (STREAM, "       add     r1,r0\n");                      \
      fprintf (STREAM, "       mov.l   1f,r1\n");                      \
      fprintf (STREAM, "       mov.l   @(r0,r1),r1\n");                \
    }                                                                  \
  else                                                                 \
    {                                                                  \
      fprintf (STREAM, "       mov.l   1f,r1\n");                      \
    }                                                                  \
  fprintf (STREAM, "   sts.l   pr,@-r15\n");                           \
  fprintf (STREAM, "   mova    2f,r0\n");                              \
  fprintf (STREAM, "   jmp     @r1\n");                                \
  fprintf (STREAM, "   lds     r0,pr\n");                              \
  fprintf (STREAM, "   .align  2\n");                                  \
  if (flag_pic)                                                        \
    {                                                                  \
      fprintf (STREAM, "1:     .long   %s@GOT\n", SH_MCOUNT_NAME);     \
      fprintf (STREAM, "3:     .long   _GLOBAL_OFFSET_TABLE_\n");      \
    }                                                                  \
  else                                                                 \
    {                                                                  \
      fprintf (STREAM, "1:     .long   %s\n", SH_MCOUNT_NAME);         \
    }                                                                  \
  fprintf (STREAM, "2: lds.l   @r15+,pr\n");                           \
} while (0)

/* Allow stabs and dwarf, and make stabs the default for Neutrino */
#undef  PREFERRED_DEBUGGING_TYPE
#define PREFERRED_DEBUGGING_TYPE DBX_DEBUG

#define DBX_DEBUGGING_INFO
#define DWARF_DEBUGGING_INFO
#define DWARF2_DEBUGGING_INFO

/* The prefix to add to user-visible assembler symbols.
   Note that svr4.h redefined it from the original value (that we want)
   in sh.h */

#undef USER_LABEL_PREFIX
#define USER_LABEL_PREFIX ""

#undef LOCAL_LABEL_PREFIX
#define LOCAL_LABEL_PREFIX "."

#undef ASM_FILE_START
#define ASM_FILE_START(FILE) do {				\
  output_file_directive ((FILE), main_input_filename);		\
  if (TARGET_LITTLE_ENDIAN)					\
    fprintf ((FILE), "\t.little\n");				\
} while (0)


/* Let code know that this is ELF.  */

#undef CPP_SPEC
#define CPP_SPEC " \
-D__SH4__ \
-idirafter %$QNX_TARGET/usr/include %(cpp_cpu) \
%{fPIC:-D__PIC__ -D__pic__} %{fpic:-D__PIC__ -D__pic__} \
%{posix:-DPOSIX_SOURCE} \
%{!mb:-D__LITTLEENDIAN__} \
%{!mb:-D__LITTLE_ENDIAN__} \
%{mb:-D__BIGENDIAN__} \
%{mb:-D__BIG_ENDIAN__} "

#undef CPP_PREDEFINES
#define CPP_PREDEFINES "-D__sh__ -D__SH__ -D__ELF__ -Dunix -D__QNXNTO__ -D__QNX__ -Asystem(unix) -Asystem(nto) -Asystem(qnxnto) -Asystem(qnx) -Acpu(sh) -Amachine(sh)"

/* GNU pr-9594 : as complains 'pcrel too far'. Workaround is to use 
   -fno-delayed-branch.  Should be fixed in gcc-3.4+.  */

#undef CC1_SPEC
#define CC1_SPEC "%(cc1_spec) %{!mb:-ml} -m4 -fno-delayed-branch"

#undef CC1PLUS_SPEC
#define CC1PLUS_SPEC "%(cc1plus_spec) %{!mb:-ml} -m4 -fno-delayed-branch"

/* Pass -ml and -mrelax to the assembler and linker.  */
#undef ASM_SPEC
#define ASM_SPEC  "%{mb:-big} %{!mb:-little} %{mrelax:-relax}"

#undef LINK_SPEC
#define LINK_SPEC \
"%{!mb:-EL}%{mb:-EB} --small-stubs -u_start \
%{!mb:-m shlelf_nto}%{mb:-m shelf_nto} %{mrelax:-relax} \
-YP,%$QNX_TARGET/sh%{!mb:le}%{mb:be}/lib \
-YP,%$QNX_TARGET/sh%{!mb:le}%{mb:be}/usr/lib \
-YP,%$QNX_TARGET/sh%{!mb:le}%{mb:be}/opt/lib \
%{MAP:-Map mapfile} %{static:-dn -Bstatic} %{shared:-G -dy -z text} \
%{symbolic: -Bsymbolic -G -dy -z text} %{G:-G} \
%{!shared: --dynamic-linker /usr/lib/ldqnx.so.2}"

#undef  LIB_SPEC
#define LIB_SPEC "\
-L %$QNX_TARGET/sh%{!mb:le}%{mb:be}/lib/gcc/2.95.3 \
-L %$QNX_TARGET/sh%{!mb:le}%{mb:be}/lib \
-L %$QNX_TARGET/sh%{!mb:le}%{mb:be}/usr/lib \
-L %$QNX_TARGET/sh%{!mb:le}%{mb:be}/opt/lib \
-rpath-link %$QNX_TARGET/sh%{!mb:le}%{mb:be}/lib/gcc/2.95.3:\
%$QNX_TARGET/sh%{!mb:le}%{mb:be}/lib:\
%$QNX_TARGET/sh%{!mb:le}%{mb:be}/usr/lib:\
%$QNX_TARGET/sh%{!mb:le}%{mb:be}/opt/lib \
%{!symbolic:-lc -dn -Bstatic %{!shared: -lc} %{shared: -lcS}}"

/* svr4.h undefined DBX_REGISTER_NUMBER, so we need to define it
   again.  */
#define DBX_REGISTER_NUMBER(REGNO)	\
  (((REGNO) >= 22 && (REGNO) <= 39) ? ((REGNO) + 1) : (REGNO))

/* SH ELF, unlike most ELF implementations, uses underscores before
   symbol names.  */
#undef ASM_OUTPUT_LABELREF
#define ASM_OUTPUT_LABELREF(STREAM,NAME) \
  asm_fprintf (STREAM, "%U%s", NAME)

#undef ASM_GENERATE_INTERNAL_LABEL
#define ASM_GENERATE_INTERNAL_LABEL(STRING, PREFIX, NUM) \
  sprintf ((STRING), "*%s%s%d", LOCAL_LABEL_PREFIX, (PREFIX), (NUM))

#undef ASM_OUTPUT_INTERNAL_LABEL
#define ASM_OUTPUT_INTERNAL_LABEL(FILE,PREFIX,NUM) \
  asm_fprintf ((FILE), "%L%s%d:\n", (PREFIX), (NUM))

#undef  ASM_OUTPUT_SOURCE_LINE
#define ASM_OUTPUT_SOURCE_LINE(file, line)				\
do									\
  {									\
    static int sym_lineno = 1;						\
    asm_fprintf ((file), ".stabn 68,0,%d,%LLM%d-",			\
	     (line), sym_lineno);					\
    assemble_name ((file),						\
		   XSTR (XEXP (DECL_RTL (current_function_decl), 0), 0));\
    asm_fprintf ((file), "\n%LLM%d:\n", sym_lineno);			\
    sym_lineno += 1;							\
  }									\
while (0)

#undef DBX_OUTPUT_MAIN_SOURCE_FILE_END
#define DBX_OUTPUT_MAIN_SOURCE_FILE_END(FILE, FILENAME)			\
do {									\
  text_section ();							\
  fprintf ((FILE), "\t.stabs \"\",%d,0,0,Letext\nLetext:\n", N_SO);	\
} while (0)

#undef  STARTFILE_SPEC
#define STARTFILE_SPEC "\
%{!shared: %$QNX_TARGET/sh%{mb:be}%{!mb:le}/lib/crt1.o} \
%$QNX_TARGET/sh%{mb:be}%{!mb:le}/lib/crti.o \
%$QNX_TARGET/sh%{mb:be}%{!mb:le}/lib/crtbegin.o"

#undef	ENDFILE_SPEC
#define ENDFILE_SPEC "\
%$QNX_TARGET/sh%{mb:be}%{!mb:le}/lib/crtend.o \
%$QNX_TARGET/sh%{mb:be}%{!mb:le}/lib/crtn.o"

#undef	ENDFILE_DEFAULT_SPEC
#define	ENDFILE_DEFAULT_SPEC ""

/* HANDLE_SYSV_PRAGMA (defined by svr4.h) takes precedence over HANDLE_PRAGMA.
   We want to use the HANDLE_PRAGMA from sh.h.  */
#undef HANDLE_SYSV_PRAGMA
/* Make sure we still handle #pragma pack */
#define HANDLE_PRAGMA_WEAK 1
#define HANDLE_PRAGMA_PACK 1

#define HANDLE_PRAGMA_PACK_PUSH_POP 1

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

/* Like `ASM_OUTPUT_BSS' except takes the required alignment as a
   separate, explicit argument.  If you define this macro, it is used
   in place of `ASM_OUTPUT_BSS', and gives you more flexibility in
   handling the required alignment of the variable.  The alignment is
   specified as the number of bits.

   Try to use function `asm_output_aligned_bss' defined in file
   `varasm.c' when defining this macro. */
#ifndef ASM_OUTPUT_ALIGNED_BSS
#define ASM_OUTPUT_ALIGNED_BSS(FILE, DECL, NAME, SIZE, ALIGN) \
  asm_output_aligned_bss (FILE, DECL, NAME, SIZE, ALIGN)
#endif

/* Don't set libgcc.a's gthread/pthread symbols to weak, as our
   libc has them as well, and we get problems when linking static,
   as libgcc.a will get a symbol value of 0.  */
#define GTHREAD_USE_WEAK 0


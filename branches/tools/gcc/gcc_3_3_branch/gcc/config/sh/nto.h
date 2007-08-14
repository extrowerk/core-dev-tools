/* Definitions of target machine for gcc for Hitachi Super-H running
   QNX Neutrino.
   Copyright (C) 1996 Free Software Foundation, Inc.

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

/* Run-time Target Specification.  */
#undef TARGET_VERSION
#define TARGET_VERSION  fputs (" (QNX/Neutrino/SH ELF)", stderr);

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
    standard_bindir_prefix = concat (qnx_host, "/usr/bin/", NULL); \
    tooldir_base_prefix = concat (qnx_host, "/usr/", NULL); \
    add_prefix (&exec_prefixes, standard_bindir_prefix, NULL, PREFIX_PRIORITY_LAST, 0, NULL, 0); \
  } while (0)

#define QNX_SYSTEM_INCLUDES \
"-nostdinc -nostdinc++ -idirafter %$QNX_TARGET/usr/include \
 -isystem %$QNX_TARGET/usr/include/c++/%v1.%v2.%v3 \
 -isystem %$QNX_TARGET/usr/include/c++/%v1.%v2.%v3/" TARGET_ALIAS " \
 -isystem %$QNX_TARGET/usr/include/c++/%v1.%v2.%v3/backward \
 -isystem %$QNX_TARGET/usr/include \
 -isystem %$QNX_HOST/usr/lib/gcc-lib/" TARGET_ALIAS "/%v1.%v2.%v3/include"

/* Allow stabs and dwarf, and make stabs the default for Neutrino */
#undef PREFERRED_DEBUGGING_TYPE
#undef DBX_DEBUGGING_INFO
#undef DWARF_DEBUGGING_INFO
#undef DWARF2_DEBUGGING_INFO

#define PREFERRED_DEBUGGING_TYPE DWARF2_DEBUG
#define DBX_DEBUGGING_INFO
#define DWARF_DEBUGGING_INFO
#define DWARF2_DEBUGGING_INFO

#undef TARGET_DEFAULT 
#define TARGET_DEFAULT  (SH1_BIT|SH4_BIT)

/* Return to the original ELF way.  */
#undef USER_LABEL_PREFIX
#define USER_LABEL_PREFIX ""

#undef LOCAL_LABEL_PREFIX
#define LOCAL_LABEL_PREFIX "."

#undef SIZE_TYPE
#define SIZE_TYPE "unsigned int"

#undef PTRDIFF_TYPE
#define PTRDIFF_TYPE "int"

#undef WCHAR_TYPE
#define WCHAR_TYPE "long int"

#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE BITS_PER_WORD

#undef BOOL_TYPE_SIZE
#define BOOL_TYPE_SIZE POINTER_SIZE

/* Handle various #pragma's, including pack, pop and weak */
#undef  HANDLE_SYSV_PRAGMA
#define HANDLE_SYSV_PRAGMA 1
#define HANDLE_PRAGMA_WEAK 1
#define HANDLE_PRAGMA_PACK 1
#define HANDLE_PRAGMA_PACK_PUSH_POP 1
#define SUPPORTS_WEAK 1

#define QNX_SYSTEM_LIBDIRS \
"-L %$QNX_TARGET/sh%{EB:be}%{!EB:le}/lib/gcc/%v1.%v2.%v3 \
 -L %$QNX_TARGET/sh%{EB:be}%{!EB:le}/lib \
 -L %$QNX_TARGET/sh%{EB:be}%{!EB:le}/usr/lib \
 -L %$QNX_TARGET/sh%{EB:be}%{!EB:le}/opt/lib \
 -rpath-link %$QNX_TARGET/sh%{EB:be}%{!EB:le}/lib/gcc/%v1.%v2.%v3:\
%$QNX_TARGET/sh%{EB:be}%{!EB:le}/lib:\
%$QNX_TARGET/sh%{EB:be}%{!EB:le}/usr/lib:\
%$QNX_TARGET/sh%{EB:be}%{!EB:le}/opt/lib "

#undef SUBTARGET_CPP_SPEC
#define SUBTARGET_CPP_SPEC \
QNX_SYSTEM_INCLUDES " \
 -D__SH4__ \
 %{fPIC:-D__PIC__ -D__pic__} \
 %{fpic:-D__PIC__ -D__pic__} \
 %{posix:-D_POSIX_SOURCE} \
 %{!mb:-D__LITTLE_ENDIAN__} \
 %{!mb:-D__LITTLEENDIAN__} \
 %{mb:-D__BIG_ENDIAN__} \
 %{mb:-D__BIGENDIAN__}"

#undef CPP_DEFAULT_CPU_SPEC
#define CPP_DEFAULT_CPU_SPEC "-D__SH4__ -D__sh4__"

#undef CPP_PREDEFINES
#define CPP_PREDEFINES \
"-D__ELF__ -Dunix -D__QNXNTO__ -D__QNX__ \
 -D__sh__ -D__SH__ \
 -Asystem(unix) -Asystem(nto) -Asystem(qnxnto) -Asystem(qnx)"

#undef ASM_SPEC
#define ASM_SPEC  "%{!mb:-little} %{mrelax:-relax}"

/* GNU pr-9594 : as complains 'pcrel too far'.  gcc-3.X and gcc-4.x.
   Workaround is to use -fno-delayed-branch.  Remove once pr fixed. 
   GP - Sept 5, 2003. */

#undef CC1_SPEC
#define CC1_SPEC "%(cc1_spec) %{!mb:-ml} -m4 -fno-delayed-branch"

#undef CC1PLUS_SPEC
#define CC1PLUS_SPEC "%(cc1plus_spec) %{!mb:-ml} -m4 -fno-delayed-branch"


/* Do we need this stuff?  Leave it for now... GP Feb 2003 */
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

/* Legacy "--small-stubs" is now "-z now".  */
#undef LINK_SPEC
#define LINK_SPEC \
"%{!mb:-EL}%{mb:-EB} -z now %{!mb:-m shlelf_nto}%{mb:-m shelf_nto} \
 %{mrelax:-relax} -YP,%$QNX_TARGET/lib -YP,%$QNX_TARGET/usr/lib \
 %{MAP:-Map mapfile} %{static:-dn -Bstatic} %{shared:-G -dy -z text} \
 %{symbolic: -Bsymbolic -G -dy -z text} %{G:-G} \
 %{!shared: --dynamic-linker /usr/lib/ldqnx.so.2}"

#undef  LIB_SPEC
#define LIB_SPEC \
QNX_SYSTEM_LIBDIRS \
"%{!symbolic:-lc -dn -Bstatic %{!shared: -lc} %{shared: -lcS}}"

#undef  STARTFILE_SPEC
#define STARTFILE_SPEC \
"%{!shared: %$QNX_TARGET/sh%{mb:be}%{!mb:le}/lib/%{pg:m}%{p:m}crt1.o} \
%$QNX_TARGET/sh%{mb:be}%{!mb:le}/lib/crti.o \
%{!fno-exceptions: crtbegin.o%s} \
%{fno-exceptions: %$QNX_TARGET/sh%{mb:be}%{!mb:le}/lib/crtbegin.o}"

#undef  ENDFILE_SPEC
#define ENDFILE_SPEC \
"%{!fno-exceptions: crtend.o%s} \
%{fno-exceptions: %$QNX_TARGET/sh%{mb:be}%{!mb:le}/lib/crtend.o} \
%$QNX_TARGET/sh%{mb:be}%{!mb:le}/lib/crtn.o"

#undef	ENDFILE_DEFAULT_SPEC
#define	ENDFILE_DEFAULT_SPEC ""

#define NO_IMPLICIT_EXTERN_C 1

/* Don't set libgcc.a's gthread/pthread symbols to weak, as our
   libc has them as well, and we get problems when linking static,
   as libgcc.a will get a symbol value of 0.  */
#define GTHREAD_USE_WEAK 0


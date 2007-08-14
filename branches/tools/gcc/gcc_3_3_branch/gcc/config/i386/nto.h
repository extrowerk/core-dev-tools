/* Definitions for Intel 386 running QNX/Neutrino.
   Copyright (C) 2002 Free Software Foundation, Inc.

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

#include "i386/unix.h"

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

#undef  DEFAULT_PCC_STRUCT_RETURN
#define DEFAULT_PCC_STRUCT_RETURN 1

#undef TARGET_VERSION
#define TARGET_VERSION	fprintf (stderr, " (QNX/Neutrino/i386 ELF)");

#undef CPP_PREDEFINES
#define CPP_PREDEFINES  \
  "-D__X86__ -Di386 -D__QNXNTO__ -D__QNX__ --D__LITTLEENDIAN__ " \
  "-Asystem=unix -Asystem=nto -Asystem=qnxnto -Asystem=qnx -D__ELF__"

#undef CPP_SPEC
#define CPP_SPEC \
QNX_SYSTEM_INCLUDES "\
 %(cpp_cpu) \
 %{fPIC:-D__PIC__ -D__pic__} %{fpic:-D__PIC__ -D__pic__} \
 %{posix:-D_POSIX_SOURCE}"

#undef STARTFILE_SPEC
#define STARTFILE_SPEC \
"%{!shared: \
  %{!symbolic: \
    %{pg:%$QNX_TARGET/x86/lib/mcrt1.o%s} \
    %{!pg:%{p:%$QNX_TARGET/x86/lib/mcrt1.o%s} \
    %{!p:%$QNX_TARGET/x86/lib/crt1.o%s}}}} \
%$QNX_TARGET/x86/lib/crti.o%s \
%{!fno-exceptions: crtbegin.o%s} \
%{fno-exceptions: %$QNX_TARGET/x86/lib/crtbegin.o}"

#undef ENDFILE_SPEC
#define ENDFILE_SPEC \
"%{!fno-exceptions: crtend.o%s} \
%{fno-exceptions: %$QNX_TARGET/x86/lib/crtend.o} \
%$QNX_TARGET/x86/lib/crtn.o"

#define QNX_SYSTEM_LIBDIRS \
"-L %$QNX_TARGET/x86/lib/gcc/%v1.%v2.%v3 \
 -L %$QNX_TARGET/x86/lib \
 -L %$QNX_TARGET/x86/usr/lib \
 -L %$QNX_TARGET/x86/opt/lib \
 -rpath-link %$QNX_TARGET/x86/lib/gcc/%v1.%v2.%v3:\
%$QNX_TARGET/x86/lib:\
%$QNX_TARGET/x86/usr/lib:\
%$QNX_TARGET/x86/opt/lib"

#undef LIB_SPEC
#define LIB_SPEC \
  QNX_SYSTEM_LIBDIRS \
  "%{!symbolic: -lc -Bstatic %{!shared: -lc} %{shared:-lcS}}"

#undef LINK_SPEC
#define LINK_SPEC \
  "%{h*} %{v:-V} \
   %{b} \
   %{static:-dn -Bstatic} \
   %{shared:-G -dy -z text} \
   %{symbolic:-Bsymbolic -G -dy -z text} \
   %{G:-G} \
   %{YP,*} \
   %{!YP,*:%{p:-Y P,%$QNX_TARGET/x86/lib} \
    %{!p:-Y P,%$QNX_TARGET/x86/lib}} \
   %{Qy:} %{!Qn:-Qy} \
   -m i386nto \
   %{!shared: --dynamic-linker /usr/lib/ldqnx.so.2}"

#undef WCHAR_TYPE
#define WCHAR_TYPE "long unsigned int"

#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 32

#undef EH_FRAME_SECTION_NAME
#define EH_FRAME_SECTION_NAME ".eh_frame"

/* Handle various #pragma's, including pack, pop and weak */
#undef  HANDLE_SYSV_PRAGMA
#define HANDLE_SYSV_PRAGMA 1
#define HANDLE_PRAGMA_WEAK 1
#define HANDLE_PRAGMA_PACK 1
#define HANDLE_PRAGMA_PACK_PUSH_POP 1
#define SUPPORTS_WEAK 1

#define NO_IMPLICIT_EXTERN_C 1

/* Define the register numbers to be used in Dwarf debugging information.
   QNX NTO use the SVR4 register numbers in Dwarf output code, for gdb */
#undef DBX_REGISTER_NUMBER
#define DBX_REGISTER_NUMBER(n) \
  (TARGET_64BIT ? dbx64_register_map[n] : svr4_dbx_register_map[n])

/* Don't set libgcc.a's gthread/pthread symbols to weak, as our
   libc has them as well, and we get problems when linking static,
   as libgcc.a will get a symbol value of 0.  */
#define GTHREAD_USE_WEAK 0


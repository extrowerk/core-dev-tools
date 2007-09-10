/* Target definitions for GNU compiler for PowerPC running QNX Neutrino.
   Copyright (C) 2003 Free Software Foundation, Inc.

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

/* Turn on shared libraries for Neutrino. */
#define SHARED_LIB_SUPPORT
#include "rs6000/sysv4.h"

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

#undef TARGET_VERSION
#define TARGET_VERSION fprintf (stderr, " (QNX/Neutrino/PowerPC ELF)");

/* Allow stabs and dwarf, and make stabs the default for Neutrino */
#undef PREFERRED_DEBUGGING_TYPE
#undef DBX_DEBUGGING_INFO
#undef DWARF_DEBUGGING_INFO
#undef DWARF2_DEBUGGING_INFO

#define PREFERRED_DEBUGGING_TYPE DWARF2_DEBUG
#define DBX_DEBUGGING_INFO
#define DWARF_DEBUGGING_INFO
#define DWARF2_DEBUGGING_INFO

/* For backward compatibility, we must continue to use the AIX
   structure return convention.  */
#undef DRAFT_V4_STRUCT_RET
#define DRAFT_V4_STRUCT_RET 1

#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()		\
do {						\
	builtin_define_std ("PPC");		\
	builtin_define ("__PPC__");		\
	builtin_define ("__QNX__");		\
	builtin_define ("__QNXNTO__");		\
	builtin_define ("__ELF__");		\
	builtin_define ("unix");		\
	builtin_assert ("system=unix");		\
	builtin_assert ("system=posix");	\
	builtin_assert ("system=qnx");		\
	builtin_assert ("system=nto");		\
	builtin_assert ("system=qnxnto");	\
} while (0)

#define EXPAND_BUILTIN_SAVEREGS() rs6000_expand_builtin_saveregs() 

/* Pass various options to the assembler */
#undef ASM_SPEC
#define ASM_SPEC "%(asm_cpu) \
%{.s: %{mregnames} %{mno-regnames}} %{.S: %{mregnames} %{mno-regnames}} \
%{v:-V} %{Qy:} %{!Qn:-Qy} %{n} %{T} %{Ym,*} %{Yd,*} %{Wa,*:%*} \
%{mrelocatable} %{mrelocatable-lib} %{fpic:-K PIC} %{fPIC:-K PIC} \
%{memb} %{!memb: %{msdata: -memb} %{msdata=eabi: -memb}} \
%{mlittle} %{mlittle-endian} %{mbig} %{mbig-endian} \
%{!mlittle: %{!mlittle-endian: %{!mbig: %{!mbig-endian: \
    %{mcall-freebsd: -mbig} \
    %{mcall-i960-old: -mlittle} \
    %{mcall-linux: -mbig} \
    %{mcall-gnu: -mbig} \
    %{mcall-netbsd: -mbig}}}}} \
%{EL:-mlittle} %{!EL:-mbig}"

/* Pass -G xxx to the compiler and set correct endian mode */
#undef CC1_SPEC
#define CC1_SPEC "%{G*} \
%{mlittle: %(cc1_endian_little)} %{!mlittle: %{mlittle-endian: %(cc1_endian_little)}} \
%{mbig: %(cc1_endian_big)} %{!mbig: %{mbig-endian: %(cc1_endian_big)}} \
%{!mlittle: %{!mlittle-endian: %{!mbig: %{!mbig-endian: \
    %{mcall-aixdesc: -mbig %(cc1_endian_big) } \
    %{mcall-freebsd: -mbig %(cc1_endian_big) } \
    %{mcall-i960-old: -mlittle %(cc1_endian_little) } \
    %{mcall-linux: -mbig %(cc1_endian_big) } \
    %{mcall-gnu: -mbig %(cc1_endian_big) } \
    %{mcall-netbsd: -mbig %(cc1_endian_big) } \
    %{!mcall-aixdesc: %{!mcall-freebsd: %{!mcall-i960-old: %{!mcall-linux: %{!mcall-gnu: %{!mcall-netbsd: \
    %(cc1_endian_default) \
    }}}}}} \
}}}} \
%{mno-sdata: -msdata=none } \
%{meabi: %{!mcall-*: -mcall-sysv }} \
%{!meabi: %{!mno-eabi: \
    %{mrelocatable: -meabi } \
    %{mcall-freebsd: -mno-eabi } \
    %{mcall-i960-old: -meabi } \
    %{mcall-linux: -mno-eabi } \
    %{mcall-gnu: -mno-eabi } \
    %{mcall-netbsd: -mno-eabi }}} \
%{!msdata: -msdata=none} \
%{msdata: -msdata=default} \
%{mno-sdata: -msdata=none} \
%{!mfp-moves: -mno-fp-moves} \
%{profile: -p} \
%{EL:} %{EB:}"

#undef LINK_SPEC
#define LINK_SPEC "\
%{h*} %{v:-V} %{!msdata=none:%{G*}} %{msdata=none:-G0} \
%{YP,*} %{R*} \
%{Qy:} %{!Qn:-Qy} \
%(link_shlib) \
%{!Wl,-T*: %{!T*: %(link_start) }} \
%(link_target) \
%(link_os) \
%{EB} %{EL} \
%{EL:-melf32lppcnto} %{!EL:-melf32ppcnto} %{MAP: -Map mapfile} \
%{!shared: --dynamic-linker /usr/lib/ldqnx.so.2} "

#undef CPP_SPEC
#define CPP_SPEC "%{posix: -D_POSIX_SOURCE} %(cpp_sysv) %(cpp_endian) %(cpp_cpu) \
%{mads: %(cpp_os_ads) } \
%{myellowknife: %(cpp_os_yellowknife) } \
%{mmvme: %(cpp_os_mvme) } \
%{msim: %(cpp_os_sim) } \
%{mcall-freebsd: %(cpp_os_freebsd) } \
%{mcall-linux: %(cpp_os_linux) } \
%{mcall-gnu: %(cpp_os_gnu) } \
%{mcall-netbsd: %(cpp_os_netbsd) } \
%{!mads: %{!myellowknife: %{!mmvme: %{!msim: %{!mcall-freebsd: \
%{!mcall-linux: %{!mcall-gnu: %{!mcall-netbsd: %(cpp_os_default) }}}}}}}} \
" QNX_SYSTEM_INCLUDES "\
%{EL:-D__LITTLEENDIAN__} %{!EL:-D__BIGENDIAN__}"

#undef  STARTFILE_SPEC
#define STARTFILE_SPEC \
"%{!shared: %$QNX_TARGET/ppc%{!EL:be}%{EL:le}/lib/%{pg:m}%{p:m}crt1.o}\
%$QNX_TARGET/ppc%{!EL:be}%{EL:le}/lib/crti.o \
%{!fno-exceptions: crtbegin.o%s} \
%{fno-exceptions: %$QNX_TARGET/ppc%{!EL:be}%{EL:le}/lib/crtbegin.o}" 

#undef  ENDFILE_SPEC
#define ENDFILE_SPEC \
"%{!fno-exceptions: crtend.o%s} \
 %{fno-exceptions: %$QNX_TARGET/ppc%{!EL:be}%{EL:le}/lib/crtend.o} \
%$QNX_TARGET/ppc%{!EL:be}%{EL:le}/lib/crtn.o"

#define QNX_SYSTEM_LIBDIRS \
"-L %$QNX_TARGET/ppc%{!EL:be}%{EL:le}/lib/gcc/%v1.%v2.%v3 \
 -L %$QNX_TARGET/ppc%{!EL:be}%{EL:le}/lib \
 -L %$QNX_TARGET/ppc%{!EL:be}%{EL:le}/usr/lib \
 -L %$QNX_TARGET/ppc%{!EL:be}%{EL:le}/opt/lib \
 -rpath-link %$QNX_TARGET/ppc%{!EL:be}%{EL:le}/lib/gcc/%v1.%v2.%v3:\
%$QNX_TARGET/ppc%{!EL:be}%{EL:le}/lib:\
%$QNX_TARGET/ppc%{!EL:be}%{EL:le}/usr/lib:\
%$QNX_TARGET/ppc%{!EL:be}%{EL:le}/opt/lib "

#undef	LIB_SPEC
#define LIB_SPEC \
QNX_SYSTEM_LIBDIRS \
"-lc -dn -Bstatic %{!shared: -lc} %{shared: -lcS}"

#undef	LIBGCC_SPEC
#define	LIBGCC_SPEC "libgcc.a%s"

#undef BOOL_TYPE_SIZE
#define BOOL_TYPE_SIZE POINTER_SIZE

/* Handle various #pragma's, including pack, pop and weak */
#undef  HANDLE_SYSV_PRAGMA
#define HANDLE_SYSV_PRAGMA 1
#define HANDLE_PRAGMA_WEAK 1
#define HANDLE_PRAGMA_PACK 1
#define HANDLE_PRAGMA_PACK_PUSH_POP 1
#define SUPPORTS_WEAK 1

#undef NO_IMPLICIT_EXTERN_C
#define NO_IMPLICIT_EXTERN_C 1

/* Don't set libgcc.a's gthread/pthread symbols to weak, as our
   libc has them as well, and we get problems when linking static,
   as libgcc.a will get a symbol value of 0.  */
#define GTHREAD_USE_WEAK 0

/* Definitions of target machine for GNU compiler.  MIPS QNX Neutrino.
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

#define HAVE_ATEXIT 

/* Work around assembler forward label references generated in exception
   handling code. */
#define DWARF2_UNWIND_INFO 0 

/* Allow stabs and dwarf, and make stabs the default for Neutrino */
#undef PREFERRED_DEBUGGING_TYPE
#undef DBX_DEBUGGING_INFO
#undef DWARF_DEBUGGING_INFO
#undef DWARF2_DEBUGGING_INFO

#define PREFERRED_DEBUGGING_TYPE DWARF2_DEBUG
#define DBX_DEBUGGING_INFO
#define DWARF_DEBUGGING_INFO
#define DWARF2_DEBUGGING_INFO

#ifndef SET_ASM_OP
#define SET_ASM_OP      "\t.set"
#endif

/* These don't work in the presence of $gp relative calls */
#undef ASM_OUTPUT_REG_PUSH
#undef ASM_OUTPUT_REG_POP

#undef MULTILIB_DEFAULTS
#define MULTILIB_DEFAULTS {""}

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

/* Handle various #pragma's, including pack, pop and weak */
#undef  HANDLE_SYSV_PRAGMA
#define HANDLE_SYSV_PRAGMA 1
#define HANDLE_PRAGMA_WEAK 1
#define HANDLE_PRAGMA_PACK 1
#define HANDLE_PRAGMA_PACK_PUSH_POP 1
#define SUPPORTS_WEAK 1

/* We don't want to run mips-tfile */
#undef ASM_FINAL_SPEC

#undef THREAD_MODEL_SPEC
#define THREAD_MODEL_SPEC "posix"

#define QNX_SYSTEM_LIBDIRS \
"-L %$QNX_TARGET/mips%{EB:be}%{!EB:le}/lib/gcc/%v1.%v2.%v3 \
 -L %$QNX_TARGET/mips%{EB:be}%{!EB:le}/lib \
 -L %$QNX_TARGET/mips%{EB:be}%{!EB:le}/usr/lib \
 -L %$QNX_TARGET/mips%{EB:be}%{!EB:le}/opt/lib \
 -rpath-link %$QNX_TARGET/mips%{EB:be}%{!EB:le}/lib/gcc/%v1.%v2.%v3:\
%$QNX_TARGET/mips%{EB:be}%{!EB:le}/lib:\
%$QNX_TARGET/mips%{EB:be}%{!EB:le}/usr/lib:\
%$QNX_TARGET/mips%{EB:be}%{!EB:le}/opt/lib "

#undef LIB_SPEC
#define LIB_SPEC \
  QNX_SYSTEM_LIBDIRS \
  "%{!symbolic: -lc -Bstatic %{!shared: -lc} %{shared:-lcS}}"

#undef LIBGCC_SPEC
#define LIBGCC_SPEC "-lgcc"

#undef TARGET_ENDIAN_DEFAULT 
#define TARGET_ENDIAN_DEFAULT 0 	/* LE */

#undef STARTFILE_SPEC
#define STARTFILE_SPEC \
"%{!shared: %$QNX_TARGET/mips%{EB:be}%{!EB:le}/lib/%{pg:m}%{p:m}crt1.o} \
%$QNX_TARGET/mips%{EB:be}%{!EB:le}/lib/crti.o \
%$QNX_TARGET/mips%{EB:be}%{!EB:le}/lib/crtbegin.o"

#undef ENDFILE_SPEC
#define ENDFILE_SPEC "\
%$QNX_TARGET/mips%{EB:be}%{!EB:le}/lib/crtend.o \
%$QNX_TARGET/mips%{EB:be}%{!EB:le}/lib/crtn.o"

#undef LINK_SPEC
#define LINK_SPEC "-mips2 \
%{!EB:%{!meb:-EL}} %{EB|meb:-EB} \
%{G*} %{mips1} %{mips2} %{mips3} %{mips4} %{mips32} %{mips64} \
%{shared} %{non_shared} \
%{!EB:%{!meb:-belf32-littlemips}} %{EB|meb:-belf32-bigmips} \
%{MAP: -Map mapfile} \
%{static: -dn -Bstatic} \
%{!shared: --dynamic-linker /usr/lib/ldqnx.so.2} \
%{EB|meb:-melf32bmipnto} %{!EB:%{!meb:-melf32lmipnto}}"

#undef SUBTARGET_CC1_SPEC
#define SUBTARGET_CC1_SPEC "\
%{fpic: -mqnxpic} \
%{fPIC: -mqnxpic} \
-mgas -mips2"

#undef CPP_PREDEFINES
#define CPP_PREDEFINES "-D__QNX__ -D__QNXNTO__ -D__MIPS__  -D__ELF__"

#undef SUBTARGET_CPP_SPEC
#define SUBTARGET_CPP_SPEC \
QNX_SYSTEM_INCLUDES " \
%{!EB:-D__LITTLEENDIAN__} \
%{EB:-D__BIGENDIAN__} \
%{fpic: -D__PIC__} %{fPIC: -D__PIC__} \
%{mqnxpic: -D__PIC__} \
%{posix:-D_POSIX_SOURCE}"

/* Define the specs passed to the assembler */
#undef ASM_SPEC
#define ASM_SPEC "-mips2 \
%{!qnxpic: %{G*}} %{EB} %{!EB:-EL} %{EL} \
%{mips1} %{mips2} %{mips3} %{mips4} %{mips32} %{mips64} \
%{mips16:%{!mno-mips16:-mips16}} %{mno-mips16:-no-mips16} \
%(subtarget_asm_optimizing_spec) \
%(subtarget_asm_debugging_spec) \
%{membedded-pic} \
%{fpic: --defsym __PIC__=1} \
%{fPIC: --defsym __PIC__=1} \
%{mqnxpic: --defsym __PIC__=1} \
%{mabi=32:-32}%{mabi=o32:-32}%{mabi=n32:-n32}%{mabi=64:-64}%{mabi=n64:-64} \
%(target_asm_spec) \
%(subtarget_asm_spec)"

/* Use memcpy, et. al., rather than bcopy.  */
#define TARGET_MEM_FUNCTIONS

#undef BOOL_TYPE_SIZE
#define BOOL_TYPE_SIZE POINTER_SIZE

#define NO_IMPLICIT_EXTERN_C 1

/* Do indirect call through function pointer to avoid section switching
   problem with assembler and R_MIPS_PC16 relocation errors. */
#define DO_CRT_STATIC_CALL(f) { void (* volatile fp)() = f; fp(); }

#undef MIPS_DEFAULT_GVALUE
#define MIPS_DEFAULT_GVALUE 0

/* Don't set libgcc.a's gthread/pthread symbols to weak, as our
   libc has them as well, and we get problems when linking static,
   as libgcc.a will get a symbol value of 0.  */
#define GTHREAD_USE_WEAK 0


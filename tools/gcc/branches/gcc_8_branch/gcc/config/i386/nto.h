/* Definitions for Intel 386 running QNX/Neutrino.
   Copyright (C) 2002-2018 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#undef  DEFAULT_PCC_STRUCT_RETURN
#define DEFAULT_PCC_STRUCT_RETURN 1

#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()		\
  do						\
    {						\
        NTO_TARGET_OS_CPP_BUILTINS();           \
        builtin_define_std ("__X86__");		\
        builtin_define ("__LITTLEENDIAN__");    \
    }						\
  while (0)

#undef CPP_SPEC
#define CPP_SPEC \
QNX_SYSTEM_INCLUDES \
" %(cpp_cpu) \
 %{posix:-D_POSIX_SOURCE} %{pthread:}"

#ifdef CROSS_DIRECTORY_STRUCTURE
#define SYSROOT_SUFFIX_SPEC "x86"
#endif

#undef STARTFILE_SPEC
#define STARTFILE_SPEC \
"%{!shared: \
  %{!symbolic: \
    %{pg:%$QNX_TARGET/x86/lib/mcrt1.o%s} \
    %{!pg:%{p:%$QNX_TARGET/x86/lib/mcrt1.o%s} \
    %{!p:%$QNX_TARGET/x86/lib/crt1%{" PIE_SPEC ":S}.o%s}} \
    }} \
%$QNX_TARGET/x86/lib/crti.o%s crtbegin.o%s" 

#undef ENDFILE_SPEC
#define ENDFILE_SPEC "crtend.o%s %$QNX_TARGET/x86/lib/crtn.o"

#define QNX_SYSTEM_LIBDIRS \
"-L %$QNX_TARGET/x86/lib/gcc/%v1.%v2.%v3 \
 -L %$QNX_TARGET/x86/lib \
 -L %$QNX_TARGET/x86/usr/lib \
 -L %$QNX_TARGET/x86/opt/lib \
 -rpath-link %$QNX_TARGET/x86/lib/gcc/%v1.%v2.%v3:\
%$QNX_TARGET/x86/lib:\
%$QNX_TARGET/x86/usr/lib:\
%$QNX_TARGET/x86/opt/lib"

#undef LINK_LIBGCC_SPEC
#define LINK_LIBGCC_SPEC \
  "%D " \
  QNX_SYSTEM_LIBDIRS

#undef LINK_SPEC
#define LINK_SPEC \
  "%{h*} %{v:-V} \
   %{b} \
   %{!r:--build-id=md5} \
   --warn-shared-textrel \
   " QNX_RELRO_SPEC " \
   %{static:-dn -Bstatic} \
   %{shared:-G -dy -z text} \
   %{symbolic:-Bsymbolic -G -dy -z text} \
   %{G:-G} \
   %{YP,*} \
   %{!YP,*:%{p:-Y P,%R/lib} \
    %{!p:-Y P,%R/lib}} \
   %{Qy:} %{!Qn:-Qy} \
   -m i386nto \
   %{!shared: \
     %{!static: \
       %{rdynamic:-export-dynamic}} \
     --dynamic-linker /usr/lib/ldqnx.so.2} "

#undef  ASM_SPEC
#define ASM_SPEC ""

#undef SIZE_TYPE
#define SIZE_TYPE "unsigned int"

#undef PTRDIFF_TYPE
#define PTRDIFF_TYPE "int"

/* Define the register numbers to be used in Dwarf debugging information.
   QNX NTO use the SVR4 register numbers in Dwarf output code, for gdb */
#undef DBX_REGISTER_NUMBER
#define DBX_REGISTER_NUMBER(n) \
  (TARGET_64BIT ? dbx64_register_map[n] : svr4_dbx_register_map[n])

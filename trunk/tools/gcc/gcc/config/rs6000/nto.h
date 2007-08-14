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
#define HAVE_ATEXIT

#undef TARGET_VERSION
#define TARGET_VERSION fprintf (stderr, " (QNX/Neutrino/PowerPC ELF)");

#undef TARGET_64BIT
#define TARGET_64BIT 0

#define OBJECT_FORMAT_ELF
#define HAS_INIT_SECTION

#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()                \
do {                                            \
	NTO_TARGET_OS_CPP_BUILTINS();           \
	builtin_define_std ("PPC");             \
	builtin_define ("__PPC__");             \
} while (0)

/* Pass various options to the assembler */
#undef ASM_SPEC
#define ASM_SPEC "%(asm_cpu) \
%{me500v1: -me500} \
%{me500v2: -me500} \
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
%{me500v1: %{!mfloat-gprs*: -mfloat-gprs=single} \
           %{!mabi=*: -mabi=spe} %{!mspe=no: -mspe=yes}\
           %{!mcpu=*: -mcpu=8540} -D__E500_V1__} \
%{me500v2: %{!me500v1: %{!mfloat-gprs*: -mfloat-gprs=double} \
           %{!mabi=*: -mabi=spe} %{!mspe=no: -mspe=yes}\
           %{!mcpu=*: -mcpu=8548} -D__E500_V2__}} \
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
#define CPP_SPEC "%{posix: -D_POSIX_SOURCE} \
%(cpp_sysv) %(cpp_endian) %(cpp_cpu) \
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
-idirafter %$QNX_TARGET/usr/include \
%{e500v1:-D__E500_V1__} %{e500v2:-D__E500_V2__} \
%{EL:-D__LITTLEENDIAN__} %{!EL:-D__BIGENDIAN__}"

#undef  STARTFILE_SPEC
#define STARTFILE_SPEC \
"%{!shared: %$QNX_TARGET/ppc%{!EL:be}%{EL:le}/lib/%{pg:m}%{p:m}crt1.o} \
%$QNX_TARGET/ppc%{!EL:be}%{EL:le}/lib/crti.o \
%{!fno-exceptions: crtbegin.o%s} \
%{fno-exceptions: %$QNX_TARGET/ppc%{!EL:be}%{EL:le}/lib/crtbegin.o}" 

#undef  ENDFILE_SPEC
#define ENDFILE_SPEC \
"%{!fno-exceptions: crtend.o%s} \
 %{fno-exceptions: %$QNX_TARGET/ppc%{!EL:be}%{EL:le}/lib/crtend.o} \
%$QNX_TARGET/ppc%{!EL:be}%{EL:le}/lib/crtn.o"

#define QNX_SYSTEM_LIBDIRS \
"%{me500*: \
-L %$QNX_TARGET/ppc%{!EL:be}%{EL:le}/lib/gcc/%v1.%v2.%v3/e500 \
-L %$QNX_TARGET/ppc%{!EL:be}%{EL:le}/lib/gcc/%v1.%v2.%v3 \
-L %$QNX_TARGET/ppc%{!EL:be}%{EL:le}/lib/e500 \
-L %$QNX_TARGET/ppc%{!EL:be}%{EL:le}/lib \
-L %$QNX_TARGET/ppc%{!EL:be}%{EL:le}/usr/lib/e500 \
-L %$QNX_TARGET/ppc%{!EL:be}%{EL:le}/usr/lib \
-L %$QNX_TARGET/ppc%{!EL:be}%{EL:le}/opt/lib/e500 \
-L %$QNX_TARGET/ppc%{!EL:be}%{EL:le}/opt/lib \
-rpath-link \
%$QNX_TARGET/ppc%{!EL:be}%{EL:le}/lib/gcc/%v1.%v2.%v3/e500:\
%$QNX_TARGET/ppc%{!EL:be}%{EL:le}/lib/gcc/%v1.%v2.%v3:\
%$QNX_TARGET/ppc%{!EL:be}%{EL:le}/lib/e500:\
%$QNX_TARGET/ppc%{!EL:be}%{EL:le}/lib:\
%$QNX_TARGET/ppc%{!EL:be}%{EL:le}/usr/lib/e500:\
%$QNX_TARGET/ppc%{!EL:be}%{EL:le}/usr/lib:\
%$QNX_TARGET/ppc%{!EL:be}%{EL:le}/opt/lib/e500:\
%$QNX_TARGET/ppc%{!EL:be}%{EL:le}/opt/lib} \
%{!me500*: \
-L %$QNX_TARGET/ppc%{!EL:be}%{EL:le}/lib/gcc/%v1.%v2.%v3 \
-L %$QNX_TARGET/ppc%{!EL:be}%{EL:le}/lib \
-L %$QNX_TARGET/ppc%{!EL:be}%{EL:le}/usr/lib \
-L %$QNX_TARGET/ppc%{!EL:be}%{EL:le}/opt/lib \
-rpath-link \
%$QNX_TARGET/ppc%{!EL:be}%{EL:le}/lib/gcc/%v1.%v2.%v3:\
%$QNX_TARGET/ppc%{!EL:be}%{EL:le}/lib:\
%$QNX_TARGET/ppc%{!EL:be}%{EL:le}/usr/lib:\
%$QNX_TARGET/ppc%{!EL:be}%{EL:le}/opt/lib} "

#undef	LIB_SPEC
#define LIB_SPEC \
QNX_SYSTEM_LIBDIRS \
"%{!symbolic: -lc -Bstatic %{!shared: -lc} %{shared:-lcS}}"

#undef	LIBGCC_SPEC
#define	LIBGCC_SPEC "libgcc.a%s"

#undef BOOL_TYPE_SIZE
#define BOOL_TYPE_SIZE POINTER_SIZE

/* For backward compatibility, we must continue to use the AIX
   structure return convention.  */

#undef  DRAFT_V4_STRUCT_RET
#define DRAFT_V4_STRUCT_RET 1

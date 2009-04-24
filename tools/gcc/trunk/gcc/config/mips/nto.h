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

#define HAVE_ATEXIT 

/* Work around assembler forward label references generated in exception
   handling code. */
#define DWARF2_UNWIND_INFO 0 

#undef ASM_DECLARE_OBJECT_NAME
#define ASM_DECLARE_OBJECT_NAME mips_declare_object_name

#undef TARGET_ASM_NAMED_SECTION
#define TARGET_ASM_NAMED_SECTION  default_elf_asm_named_section

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


#define QNX_SYSTEM_LIBDIRS \
"-L %$QNX_TARGET/mips%{EL:le}%{!EL:be}/lib/gcc/%v1.%v2.%v3 \
 -L %$QNX_TARGET/mips%{EL:le}%{!EL:be}/lib \
 -L %$QNX_TARGET/mips%{EL:le}%{!EL:be}/usr/lib \
 -L %$QNX_TARGET/mips%{EL:le}%{!EL:be}/opt/lib \
 -rpath-link %$QNX_TARGET/mips%{EL:be}%{!EL:be}/lib/gcc/%v1.%v2.%v3:\
%$QNX_TARGET/mips%{EL:le}%{!EL:be}/lib:\
%$QNX_TARGET/mips%{EL:le}%{!EL:be}/usr/lib:\
%$QNX_TARGET/mips%{EL:le}%{!EL:be}/opt/lib "

#undef LIB_SPEC
#define LIB_SPEC \
  QNX_SYSTEM_LIBDIRS \
  "%{!symbolic: -lc -Bstatic %{!shared: -lc} %{shared:-lcS}}"

#undef LIBGCC_SPEC
#define LIBGCC_SPEC "-lgcc"

/*
#undef TARGET_ENDIAN_DEFAULT 
#define TARGET_ENDIAN_DEFAULT 1
*/

/* If we don't set MASK_ABICALLS, we can't default to PIC.  */
#undef TARGET_DEFAULT
#define TARGET_DEFAULT MASK_ABICALLS


#undef STARTFILE_SPEC
#define STARTFILE_SPEC \
"%{!shared: %$QNX_TARGET/mips%{EL:le}%{!EL:be}/lib/%{pg:m}%{p:m}crt1.o} \
%$QNX_TARGET/mips%{EL:le}%{!EL:be}/lib/crti_PIC.o \
%$QNX_TARGET/mips%{EL:le}%{!EL:be}/lib/crtbegin.o"

#undef ENDFILE_SPEC
#define ENDFILE_SPEC "\
%$QNX_TARGET/mips%{EL:le}%{!EL:be}/lib/crtend.o \
%$QNX_TARGET/mips%{EL:le}%{!EL:be}/lib/crtn_PIC.o"

#undef LINK_SPEC
#define LINK_SPEC "-mips2 \
%{!EL:%{!mel:-EB}} %{EL|leb:-EL} \
%{G*} %{mips1} %{mips2} %{mips3} %{mips4} %{mips32} %{mips64} \
%{shared} %{non_shared} \
%{!EL:%{!mel:-belf32-tradbigmips}} %{EL|mel:-belf32-tradlittlemips} \
%{MAP: -Map mapfile} \
%{static: -dn -Bstatic} \
%{!shared: --dynamic-linker /usr/lib/ldqnx.so.3} \
-melf32btsmip"

#undef SUBTARGET_CC1_SPEC
#define SUBTARGET_CC1_SPEC "-mips2 \
%{mshared|mno-shared|fpic|fPIC|fpie|fPIE:;:-mno-shared -mplt}"

#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()                  \
do                                                \
   {                                              \
	NTO_TARGET_OS_CPP_BUILTINS();             \
	builtin_define_std ("__MIPS__");          \
	if (TARGET_ABICALLS)                      \
	  builtin_define ("__MIPS_ABICALLS__");   \
  }						  \
  while (0)

#undef SUBTARGET_CPP_SPEC
#define SUBTARGET_CPP_SPEC QNX_SYSTEM_INCLUDES "\
%{!EL:-D__BIGENDIAN__} \
%{EL:-D__LITTLEENDIAN__} \
%{fpic: -D__PIC__} %{fPIC: -D__PIC__} \
%{mqnx-pic: -D__PIC__} \
%{posix:-D_POSIX_SOURCE}"

/* Define the specs passed to the assembler */
#undef ASM_SPEC
#define ASM_SPEC "-mips2 \
%{!qnx-pic: %{G*}} %{EB} %{!EL:-EB} %{EL} \
%{mips1} %{mips2} %{mips3} %{mips4} %{mips32} %{mips64} \
%{mips16:%{!mno-mips16:-mips16}} %{mno-mips16:-no-mips16} \
%(subtarget_asm_optimizing_spec) \
%(subtarget_asm_debugging_spec) \
%{membedded-pic} \
%{fpic: --defsym __PIC__=1} \
%{fPIC: --defsym __PIC__=1} \
%{mqnx-pic: --defsym __PIC__=1} \
%{mabi=32:-32}%{mabi=o32:-32}%{mabi=n32:-n32}%{mabi=64:-64}%{mabi=n64:-64} \
%(target_asm_spec) \
%(subtarget_asm_spec)"

#undef BOOL_TYPE_SIZE
#define BOOL_TYPE_SIZE POINTER_SIZE

/* Do indirect call through function pointer to avoid section switching
   problem with assembler and R_MIPS_PC16 relocation errors. */
#undef CRT_CALL_STATIC_FUNCTION
# define CRT_CALL_STATIC_FUNCTION(SECTION_OP, FUNC)     \
static void __attribute__((__used__))                   \
call_ ## FUNC (void)                                    \
{                                                       \
  asm (SECTION_OP);                                     \
  void (* volatile fp)() = FUNC; fp();                  \
  FORCE_CODE_SECTION_ALIGN                              \
  asm (TEXT_SECTION_ASM_OP);                            \
}

#undef MIPS_DEFAULT_GVALUE
#define MIPS_DEFAULT_GVALUE 0

/* If defined, a C expression whose value is a string containing the
   assembler operation to identify the following data as
   uninitialized global data.  If not defined, and neither
   `ASM_OUTPUT_BSS' nor `ASM_OUTPUT_ALIGNED_BSS' are defined,
   uninitialized global data will be output in the data section if
   `-fno-common' is passed, otherwise `ASM_OUTPUT_COMMON' will be
   used.  */
#define BSS_SECTION_ASM_OP	"\t.section\t.bss"

/* #define ASM_OUTPUT_ALIGNED_BSS mips_output_aligned_bss */

#undef ASM_DECLARE_OBJECT_NAME
#define ASM_DECLARE_OBJECT_NAME mips_declare_object_name

/* The MIPS assembler has different syntax for .set. We set it to
   .dummy to trap any errors.  */
#undef SET_ASM_OP
#define SET_ASM_OP "\t.dummy\t"

#undef ASM_OUTPUT_DEF
#define ASM_OUTPUT_DEF(FILE,LABEL1,LABEL2)				\
 do {									\
	fputc ( '\t', FILE);						\
	assemble_name (FILE, LABEL1);					\
	fputs ( " = ", FILE);						\
	assemble_name (FILE, LABEL2);					\
	fputc ( '\n', FILE);						\
 } while (0)

#undef ASM_DECLARE_FUNCTION_NAME
#define ASM_DECLARE_FUNCTION_NAME(STREAM, NAME, DECL)			\
  do {									\
    if (!flag_inhibit_size_directive)					\
      {									\
	fputs ("\t.ent\t", STREAM);					\
	assemble_name (STREAM, NAME);					\
	putc ('\n', STREAM);						\
      }									\
    assemble_name (STREAM, NAME);					\
    fputs (":\n", STREAM);						\
  } while (0)

#undef ASM_DECLARE_FUNCTION_SIZE
#define ASM_DECLARE_FUNCTION_SIZE(STREAM, NAME, DECL)			\
  do {									\
    if (!flag_inhibit_size_directive)					\
      {									\
	fputs ("\t.end\t", STREAM);					\
	assemble_name (STREAM, NAME);					\
	putc ('\n', STREAM);						\
      }									\
  } while (0)

/* Tell function_prologue in mips.c that we have already output the .ent/.end
   pseudo-ops.  */
#undef FUNCTION_NAME_ALREADY_DECLARED
#define FUNCTION_NAME_ALREADY_DECLARED 1

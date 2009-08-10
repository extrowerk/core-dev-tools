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
    add_prefix (&exec_prefixes, concat (qnx_host, "/usr/ntoarm/bin/", NULL), \
                "BINUTILS", 0, 0, NULL_PTR); \
    add_prefix (&exec_prefixes, concat (qnx_host, "/usr/bin/", NULL), \
                NULL, 0, 0, NULL_PTR); \
  } while (0)

#define HAVE_ATEXIT

#undef SIZE_TYPE
#define SIZE_TYPE "unsigned int"

#undef PTRDIFF_TYPE
#define PTRDIFF_TYPE "int"

#undef WCHAR_TYPE
#define WCHAR_TYPE "long int"

#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE BITS_PER_WORD

#undef	TARGET_VERSION
#define	TARGET_VERSION fprintf(stderr, " (QNX/Neutrino)");

#define	OBJECT_FORMAT_ELF
#define	HAS_INIT_SECTION

/* Allow stabs and dwarf, and make stabs the default for Neutrino */
#undef  PREFERRED_DEBUGGING_TYPE
#define PREFERRED_DEBUGGING_TYPE DBX_DEBUG

#define DBX_DEBUGGING_INFO
#define DWARF_DEBUGGING_INFO
#define DWARF2_DEBUGGING_INFO

/* Handle #pragma weak and #pragma pack.  */
#undef  HANDLE_SYSV_PRAGMA
#define HANDLE_SYSV_PRAGMA

#define HANDLE_PRAGMA_PACK_PUSH_POP 1

#undef	DEFAULT_VTABLE_THUNKS
#define DEFAULT_VTABLE_THUNKS   1

#undef	TARGET_CPU_DEFAULT
#define TARGET_CPU_DEFAULT TARGET_CPU_strongarm

#define TARGET_DEFAULT (ARM_FLAG_APCS_32 | ARM_FLAG_SHORT_BYTE | ARM_FLAG_SOFT_FLOAT)

#define MULTILIB_DEFAULTS   { "mapcs-32" }

#undef CPP_PREDEFINES
#define	CPP_PREDEFINES \
"-D__ARM__ -D__QNXNTO__ -D__QNX__ -D__ELF__"

#undef ASM_SPEC
#define ASM_SPEC \
"%{!EL:-EB} %{EL:-EL} \
 %{fpic:--defsym __PIC__=1}"

/* we don't need an ASM_FINAL_SPEC */

#undef LIB_SPEC
#define LIB_SPEC \
"-L %$QNX_TARGET/arm%{!EL:be}%{EL:le}/lib/gcc/2.95.3 \
 -L %$QNX_TARGET/arm%{!EL:be}%{EL:le}/lib \
 -L %$QNX_TARGET/arm%{!EL:be}%{EL:le}/usr/lib \
 -L %$QNX_TARGET/arm%{!EL:be}%{EL:le}/opt/lib \
 -rpath-link %$QNX_TARGET/arm%{!EL:be}%{EL:le}/lib/gcc/2.95.3:\
%$QNX_TARGET/arm%{!EL:be}%{EL:le}/lib:\
%$QNX_TARGET/arm%{!EL:be}%{EL:le}/usr/lib:\
%$QNX_TARGET/arm%{!EL:be}%{EL:le}/opt/lib \
 %{!symbolic: -lc -Bstatic %{!shared: -lc} %{shared:-lcS}}"

#undef LIBGCC_SPEC
#define LIBGCC_SPEC "-lgcc"

#undef STARTFILE_SPEC
#define STARTFILE_SPEC \
"%{!shared: -u_start \
 %$QNX_TARGET/arm%{!EL:be}%{EL:le}/lib/%{pg:m}%{p:m}crt1.o} \
 %$QNX_TARGET/arm%{!EL:be}%{EL:le}/lib/crti.o \
 %{!fno-exceptions: crtbeginC++.o%s} \
 %{fno-exceptions: %$QNX_TARGET/arm%{!EL:be}%{EL:le}/lib/crtbegin.o}"

#undef ENDFILE_SPEC
#define ENDFILE_SPEC \
"%{!shared: %$QNX_TARGET/arm%{!EL:be}%{EL:le}/lib/crtend.o \
 %$QNX_TARGET/arm%{!EL:be}%{EL:le}/lib/crtn.o}"

#undef LINK_SPEC
#define LINK_SPEC \
"%{h*} %{v:-V} \
 %{b} %{Wl,*:%*} \
 %{static:-Bstatic} \
 %{shared} \
 %{symbolic:-Bsymbolic} \
 %{G:-G} %{MAP:-Map mapfile} \
 %{!shared:-dynamic-linker /usr/lib/ldqnx.so.2} \
 -m armnto -X \
 %{!EL:-EB} %{EL:-EL}"

#include "dbxelf.h"
#include "arm/elf.h"

#undef CPP_APCS_PC_DEFAULT_SPEC
#define CPP_APCS_PC_DEFAULT_SPEC "-D__APCS_32__"

#undef	SUBTARGET_CPP_SPEC
#define	SUBTARGET_CPP_SPEC \
"-idirafter %$QNX_TARGET/usr/include %(cpp_cpu) \
 %{EL:-D__LITTLEENDIAN__} \
 %{EL:-D__LITTLEENDIAN__} \
 %{!EL:-D__BIGENDIAN__ -D__ARMEB__} \
 %{fPIC:-D__PIC__ -D__pic__} \
 %{fpic:-D__PIC__ -D__pic__} \
 %{posix:-D_POSIX_SOURCE}"

#undef	CC1_SPEC
#define	CC1_SPEC \
"%{!EL:-mbig-endian} %{EL:-mlittle-endian}"

/*
 * Keep floating point word order the same as multi-word integers
 */
#undef FLOAT_WORDS_BIG_ENDIAN

/* Call the function profiler with a given profile label.  */
#undef FUNCTION_PROFILER
#define FUNCTION_PROFILER(STREAM, LABELNO)  				\
{									\
  fprintf (STREAM, "\tbl\tmcount%s\n", NEED_PLT_GOT ? "(PLT)" : "");	\
}

#undef BOOL_TYPE_SIZE
#define BOOL_TYPE_SIZE POINTER_SIZE

#define NO_IMPLICIT_EXTERN_C 1

/* Switch into a generic section.
   This is currently only used to support section attributes.

   We make the section read-only and executable for a function decl,
   read-only for a const data decl, and writable for a non-const data decl.  */
#define ASM_OUTPUT_SECTION_NAME(FILE, DECL, NAME, RELOC) \
  fprintf (FILE, ".section\t%s,\"%s\",%%progbits\n", NAME, \
	   (DECL) && TREE_CODE (DECL) == FUNCTION_DECL ? "ax" : \
	   (DECL) && DECL_READONLY_SECTION (DECL, RELOC) ? "a" : "aw")

/* Don't set libgcc.a's gthread/pthread symbols to weak, as our
   libc has them as well, and we get problems when linking static,
   as libgcc.a will get a symbol value of 0.  */
#define GTHREAD_USE_WEAK 0


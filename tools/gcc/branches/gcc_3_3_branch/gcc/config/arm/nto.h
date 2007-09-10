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

/* Allow stabs and dwarf, and make stabs the default for Neutrino */
#undef PREFERRED_DEBUGGING_TYPE
#undef DBX_DEBUGGING_INFO
#undef DWARF_DEBUGGING_INFO
#undef DWARF2_DEBUGGING_INFO

#define PREFERRED_DEBUGGING_TYPE DWARF2_DEBUG
#define DBX_DEBUGGING_INFO
#define DWARF_DEBUGGING_INFO
#define DWARF2_DEBUGGING_INFO

#undef SIZE_TYPE
#define SIZE_TYPE "unsigned int"

#undef PTRDIFF_TYPE
#define PTRDIFF_TYPE "int"

#undef WCHAR_TYPE
#define WCHAR_TYPE "long int"

#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE BITS_PER_WORD

#undef	TARGET_VERSION
#define	TARGET_VERSION fprintf(stderr, " (QNX/Neutrino/ARM ELF)");

#define	OBJECT_FORMAT_ELF
#define	HAS_INIT_SECTION

/* Handle various #pragma's, including pack, pop and weak */
#undef  HANDLE_SYSV_PRAGMA
#define HANDLE_SYSV_PRAGMA 1
#define HANDLE_PRAGMA_WEAK 1
#define HANDLE_PRAGMA_PACK 1
#define HANDLE_PRAGMA_PACK_PUSH_POP 1
#define SUPPORTS_WEAK 1

#undef	DEFAULT_VTABLE_THUNKS
#define DEFAULT_VTABLE_THUNKS   1

#undef	TARGET_CPU_DEFAULT
#define TARGET_CPU_DEFAULT TARGET_CPU_strongarm

#undef TARGET_DEFAULT
#define TARGET_DEFAULT (ARM_FLAG_SOFT_FLOAT | ARM_FLAG_APCS_32 | ARM_FLAG_APCS_FRAME)

#undef  MULTILIB_DEFAULTS
#define MULTILIB_DEFAULTS \
        { "marm", "mlittle-endian", "mhard-float", "mapcs-32", "mno-thumb-interwork" }

#undef CPP_PREDEFINES
#define	CPP_PREDEFINES \
"-D__ARM__ -D__QNXNTO__ -D__QNX__  -D__ELF__"

#undef ASM_SPEC
#define ASM_SPEC \
"%{EB:-EB} %{!EB:-EL} %{EL:-EL} \
 %{fpic:--defsym __PIC__=1} \
 %{fPIC:--defsym __PIC__=1}"

#define QNX_SYSTEM_LIBDIRS \
"-L %$QNX_TARGET/arm%{EB:be}%{!EB:le}/lib/gcc/%v1.%v2.%v3 \
 -L %$QNX_TARGET/arm%{EB:be}%{!EB:le}/lib \
 -L %$QNX_TARGET/arm%{EB:be}%{!EB:le}/usr/lib \
 -L %$QNX_TARGET/arm%{EB:be}%{!EB:le}/opt/lib \
 -rpath-link %$QNX_TARGET/arm%{EB:be}%{!EB:le}/lib/gcc/%v1.%v2.%v3:\
%$QNX_TARGET/arm%{EB:be}%{!EB:le}/lib:\
%$QNX_TARGET/arm%{EB:be}%{!EB:le}/usr/lib:\
%$QNX_TARGET/arm%{EB:be}%{!EB:le}/opt/lib "

#undef LIB_SPEC
#define LIB_SPEC \
  QNX_SYSTEM_LIBDIRS \
  "%{!symbolic: -lc -Bstatic %{!shared: -lc} %{shared:-lcS}}"

#undef LIBGCC_SPEC
#define LIBGCC_SPEC "-lgcc"

#undef THREAD_MODEL_SPEC
#define THREAD_MODEL_SPEC "posix"

#undef STARTFILE_SPEC
#define STARTFILE_SPEC \
"%{!shared: %$QNX_TARGET/arm%{EB:be}%{!EB:le}/lib/%{pg:m}%{p:m}crt1.o} \
%$QNX_TARGET/arm%{EB:be}%{!EB:le}/lib/crti.o \
%{!fno-exceptions: crtbegin.o%s} \
%{fno-exceptions: %$QNX_TARGET/arm%{EB:be}%{!EB:le}/lib/crtbegin.o}"

#undef ENDFILE_SPEC
#define ENDFILE_SPEC \
"%{!fno-exceptions: crtend.o%s} \
%{fno-exceptions: %$QNX_TARGET/arm%{EB:be}%{!EB:le}/lib/crtend.o} \
%$QNX_TARGET/arm%{EB:be}%{!EB:le}/lib/crtn.o"

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
 %{EB:-EB} %{!EB:-EL} %{EL:-EL}"

#undef CPP_APCS_PC_DEFAULT_SPEC
#define CPP_APCS_PC_DEFAULT_SPEC "-D__APCS_32__"

#undef	SUBTARGET_CPP_SPEC
#define	SUBTARGET_CPP_SPEC \
 QNX_SYSTEM_INCLUDES "\
 %(cpp_cpu) \
 %{!EB:-D__LITTLEENDIAN__ -D__ARMEL__} \
 %{EB:-D__BIGENDIAN__ -D__ARMEB__} \
 %{fPIC:-D__PIC__ -D__pic__} \
 %{fpic:-D__PIC__ -D__pic__} \
 %{posix:-D_POSIX_SOURCE}"

#undef	CC1_SPEC
#define	CC1_SPEC \
"%{EB:-mbig-endian} %{!EB:-mlittle-endian}"

/*
 * Keep floating point word order the same as multi-word integers
 */
#undef FLOAT_WORDS_BIG_ENDIAN

/* Call the function profiler with a given profile label.  */
#undef FUNCTION_PROFILER
#define FUNCTION_PROFILER(STREAM, LABELNO)  				\
{									\
  fprintf (STREAM, "\tbl\tmcount%s\n", NEED_PLT_RELOC ? "(PLT)" : "");	\
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

#undef  DEFAULT_STRUCTURE_SIZE_BOUNDARY
#define DEFAULT_STRUCTURE_SIZE_BOUNDARY 8

/* This macro outputs ".global __XXXX" where __XXXX is a symbol normally
   found in a lib like libgcc.  We have a static __umodsi3 in ldd.c that
   is being ignored in favour of the external one because of the .global.
   This puts a plt call in before the GOT is there to use.  BAD.  So we
   undef the macro.

   QNX GP 2003-08-12. */
#undef ASM_OUTPUT_EXTERNAL_LIBCALL

/* Don't set libgcc.a's gthread/pthread symbols to weak, as our
   libc has them as well, and we get problems when linking static,
   as libgcc.a will get a symbol value of 0.  */
#define GTHREAD_USE_WEAK 0


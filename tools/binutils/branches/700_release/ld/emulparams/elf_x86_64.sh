. ${srcdir}/emulparams/plt_unwind.sh
SCRIPT_NAME=elf
ELFSIZE=64
OUTPUT_FORMAT="elf64-x86-64"
NO_REL_RELOCS=yes
TEXT_START_ADDR=0x400000
MAXPAGESIZE="CONSTANT (MAXPAGESIZE)"
COMMONPAGESIZE="CONSTANT (COMMONPAGESIZE)"
ARCH="i386:x86-64"
MACHINE=
TEMPLATE_NAME=elf32
GENERATE_SHLIB_SCRIPT=yes
GENERATE_PIE_SCRIPT=yes
NO_SMALL_DATA=yes
LARGE_SECTIONS=yes
LARGE_BSS_AFTER_BSS=
SEPARATE_GOTPLT="SIZEOF (.got.plt) >= 24 ? 24 : 0"
IREL_IN_PLT=
# Reuse TINY_READONLY_SECTION which is placed right after .plt section.
TINY_READONLY_SECTION=".plt.bnd      ${RELOCATING-0} : { *(.plt.bnd) }"

TEXT_START_SYMBOLS='_btext = .;'

if [ "x${host}" = "x${target}" ]; then
  case " $EMULATION_LIBPATH " in
    *" ${EMULATION_NAME} "*)
      NATIVE=yes
  esac
fi

# Linux/Solaris modify the default library search path to first include
# a 64-bit specific directory.
case "$target" in
  x86_64*-linux*|i[3-7]86-*-linux-*)
    case "$EMULATION_NAME" in
      *64*)
        LIBPATH_SUFFIX=64
        BNDPLT=yes
        ;;
    esac
    ;;
  *-*-solaris2*)
    LIBPATH_SUFFIX=/amd64
    ELF_INTERPRETER_NAME=\"/lib/amd64/ld.so.1\"
  ;;
esac

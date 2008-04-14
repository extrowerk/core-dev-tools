SCRIPT_NAME=elf
OUTPUT_FORMAT="elf32-qnxbigmips"
BIG_OUTPUT_FORMAT="elf32-qnxbigmips"
LITTLE_OUTPUT_FORMAT="elf32-qnxlittlemips"
TEXT_START_ADDR=0x08020000
DATA_ADDR="ALIGN (0x1000) + (. & (${MAXPAGESIZE} - 1))"
MAXPAGESIZE=0x1000
NONPAGED_TEXT_START_ADDR=0x08020000
TEXT_DYNAMIC=
INITIAL_READONLY_SECTIONS="
  .reginfo      ${RELOCATING-0} : { *(.reginfo) }
"
OTHER_TEXT_SECTIONS='*(.mips16.fn.*) *(.mips16.call.*)'
OTHER_GOT_SYMBOLS='
  _gp = ALIGN(16) + 0x7ff0;
'
OTHER_SDATA_SECTIONS="
  .lit8         ${RELOCATING-0} : { *(.lit8) }
  .lit4         ${RELOCATING-0} : { *(.lit4) }
"
TEXT_START_SYMBOLS='_btext = . ;'
DATA_START_SYMBOLS='_bdata = . ;'
OTHER_BSS_SYMBOLS='_fbss = .;'
OTHER_SECTIONS='
  .gptab.sdata : { *(.gptab.data) *(.gptab.sdata) }
  .gptab.sbss : { *(.gptab.bss) *(.gptab.sbss) }
'
STRICT_DATA_ALIGN=yes
ARCH=mips
MACHINE=
TEMPLATE_NAME=elf32
GENERATE_SHLIB_SCRIPT=yes
EXTRA_EM_FILE=nto

SCRIPT_NAME=elf
OUTPUT_FORMAT="elf32-bigmips"
BIG_OUTPUT_FORMAT="elf32-bigmips"
LITTLE_OUTPUT_FORMAT="elf32-littlemips"
TEXT_START_ADDR=0x08020000
MAXPAGESIZE=0x1000
NONPAGED_TEXT_START_ADDR=0x08020000
#SHLIB_TEXT_START_ADDR=0x78200000
TEXT_DYNAMIC=
INITIAL_READONLY_SECTIONS='.reginfo : { *(.reginfo) }'
OTHER_TEXT_SECTIONS='*(.mips16.fn.*) *(.mips16.call.*)'
OTHER_GOT_SYMBOLS='
  _gp = ALIGN(16) + 0x7ff0;
'
OTHER_GOT_SECTIONS='
  .lit8 : { *(.lit8) }
  .lit4 : { *(.lit4) }
'
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
TEMPLATE_NAME=qnxelf32
GENERATE_SHLIB_SCRIPT=yes


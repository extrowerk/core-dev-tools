SCRIPT_NAME=elf
OUTPUT_FORMAT="elf32-shl"
TEXT_START_ADDR=0x08040000
MAXPAGESIZE=0x1000
ARCH=sh
MACHINE=
TEMPLATE_NAME=qnxelf32
GENERATE_SHLIB_SCRIPT=yes
TEXT_START_SYMBOLS='_btext = .;'
#SHLIB_TEXT_START_ADDR=0x70300000

ENTRY=_start

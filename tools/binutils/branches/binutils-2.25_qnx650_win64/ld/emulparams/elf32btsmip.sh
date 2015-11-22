# If you change this file, please also look at files which source this one:
# elf32ltsmip.sh

. ${srcdir}/emulparams/elf32bmip.sh
OUTPUT_FORMAT="elf32-tradbigmips"
BIG_OUTPUT_FORMAT="elf32-tradbigmips"
LITTLE_OUTPUT_FORMAT="elf32-tradlittlemips"
unset DATA_ADDR
SHLIB_TEXT_START_ADDR=0

ENTRY=_start
TEXT_START_SYMBOLS='_btext = .;'
TEXT_START_ADDR=0x08020000
MAXPAGESIZE=0x1000

# Place .got.plt as close to .plt as possible so that the former can be
# referred to from the latter with the microMIPS ADDIUPC instruction
# that only has a span of +/-16MB.
PLT_NEXT_DATA=
INITIAL_READWRITE_SECTIONS=$OTHER_READWRITE_SECTIONS
unset OTHER_READWRITE_SECTIONS

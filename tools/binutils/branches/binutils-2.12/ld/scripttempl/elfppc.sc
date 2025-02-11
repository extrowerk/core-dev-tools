#
# Unusual variables checked by this code:
#	NOP - two byte opcode for no-op (defaults to 0)
#	DATA_ADDR - if end-of-text-plus-one-page isn't right for data start
#	OTHER_READONLY_SECTIONS - other than .text .init .rodata ...
#		(e.g., .PARISC.milli)
#	OTHER_READWRITE_SECTIONS - other than .data .bss .ctors .sdata ...
#		(e.g., .PARISC.global)
#	OTHER_SECTIONS - at the end
#	EXECUTABLE_SYMBOLS - symbols that must be defined for an
#		executable (e.g., _DYNAMIC_LINK)
#	TEXT_START_SYMBOLS - symbols that appear at the start of the
#		.text section.
#	DATA_START_SYMBOLS - symbols that appear at the start of the
#		.data section.
#	OTHER_BSS_SYMBOLS - symbols that appear at the start of the
#		.bss section besides __bss_start.
#
# When adding sections, do note that the names of some sections are used
# when specifying the start address of the next.
#
test -z "$ENTRY" && ENTRY=_start
test -z "${BIG_OUTPUT_FORMAT}" && BIG_OUTPUT_FORMAT=${OUTPUT_FORMAT}
test -z "${LITTLE_OUTPUT_FORMAT}" && LITTLE_OUTPUT_FORMAT=${OUTPUT_FORMAT}
test "$LD_FLAG" = "N" && DATA_ADDR=.
SBSS2=".sbss2 ${RELOCATING-0} : { *(.sbss2) }"
SDATA2=".sdata2 ${RELOCATING-0} : { *(.sdata2) }"
INTERP=".interp ${RELOCATING-0} : { *(.interp) }"
PLT=".plt ${RELOCATING-0} : { *(.plt) }"
CTOR=".ctors ${CONSTRUCTING-0} : 
  {
    ${CONSTRUCTING+${CTOR_START}}
    /* gcc uses crtbegin.o to find the start of
       the constructors, so we make sure it is
       first.  Because this is a wildcard, it
       doesn't matter if the user does not
       actually link against crtbegin.o; the
       linker won't look for a file to match a
       wildcard.  The wildcard also means that it
       doesn't matter which directory crtbegin.o
       is in.  */

    KEEP (*crtbegin.o(.ctors))

    /* We don't want to include the .ctor section from
       from the crtend.o file until after the sorted ctors.
       The .ctor section from the crtend file contains the
       end of ctors marker and it must be last */

    KEEP (*(EXCLUDE_FILE (*crtend.o) .ctors))
    KEEP (*(SORT(.ctors.*)))
    KEEP (*(.ctors))
    ${CONSTRUCTING+${CTOR_END}}
  }"

DTOR=" .dtors       ${CONSTRUCTING-0} :
  {
    ${CONSTRUCTING+${DTOR_START}}
    KEEP (*crtbegin.o(.dtors))
    KEEP (*(EXCLUDE_FILE (*crtend.o) .dtors))
    KEEP (*(SORT(.dtors.*)))
    KEEP (*(.dtors))
    ${CONSTRUCTING+${DTOR_END}}
  }"

cat <<EOF
OUTPUT_FORMAT("${OUTPUT_FORMAT}", "${BIG_OUTPUT_FORMAT}",
	      "${LITTLE_OUTPUT_FORMAT}")
OUTPUT_ARCH(${ARCH})
ENTRY(${ENTRY})

${RELOCATING+${LIB_SEARCH_DIRS}}
${RELOCATING+/* Do we need any of these for elf?
   __DYNAMIC = 0; ${STACKZERO+${STACKZERO}} ${SHLIB_PATH+${SHLIB_PATH}}  */}
${RELOCATING+${EXECUTABLE_SYMBOLS}}
${RELOCATING- /* For some reason, the Solaris linker makes bad executables
  if gld -r is used and the intermediate file has sections starting
  at non-zero addresses.  Could be a Solaris ld bug, could be a GNU ld
  bug.  But for now assigning the zero vmas works.  */}

${RELOCATING+PROVIDE (__stack = 0);}
${RELOCATING+PROVIDE (___stack = 0);}
SECTIONS
{
  /* Read-only sections, merged into text segment: */
  ${CREATE_SHLIB-${RELOCATING+. = ${TEXT_START_ADDR} + SIZEOF_HEADERS;}}
  ${CREATE_SHLIB+${RELOCATING+. = SIZEOF_HEADERS;}}
  ${CREATE_SHLIB-${INTERP}}
  .hash		${RELOCATING-0} : { *(.hash)		}
  .dynsym	${RELOCATING-0} : { *(.dynsym)		}
  .dynstr	${RELOCATING-0} : { *(.dynstr)		}
  .gnu.version ${RELOCATING-0} : { *(.gnu.version)      }
  .gnu.version_d ${RELOCATING-0} : { *(.gnu.version_d)  }
  .gnu.version_r ${RELOCATING-0} : { *(.gnu.version_r)  }
  .rela.text   ${RELOCATING-0} :
    {
      *(.rela.text)
      ${RELOCATING+*(.rela.text.*)}
      ${RELOCATING+*(.rela.gnu.linkonce.t*)}
    }
  .rela.data   ${RELOCATING-0} :
    {
      *(.rela.data)
      ${RELOCATING+*(.rela.data.*)}
      ${RELOCATING+*(.rela.gnu.linkonce.d*)}
    }
  .rela.rodata ${RELOCATING-0} :
    {
      *(.rela.rodata)
      ${RELOCATING+*(.rela.rodata.*)}
      ${RELOCATING+*(.rela.gnu.linkonce.r*)}
    }
  .rela.got	${RELOCATING-0} : { *(.rela.got)	}
  .rela.got1	${RELOCATING-0} : { *(.rela.got1)	}
  .rela.got2	${RELOCATING-0} : { *(.rela.got2)	}
  .rela.ctors	${RELOCATING-0} : { *(.rela.ctors)	}
  .rela.dtors	${RELOCATING-0} : { *(.rela.dtors)	}
  .rela.init	${RELOCATING-0} : { *(.rela.init)	}
  .rela.fini	${RELOCATING-0} : { *(.rela.fini)	}
  .rela.bss	${RELOCATING-0} : { *(.rela.bss)	}
  .rela.plt	${RELOCATING-0} : { *(.rela.plt)	}
  .rela.sdata	${RELOCATING-0} : { *(.rela.sdata)	}
  .rela.sbss	${RELOCATING-0} : { *(.rela.sbss)	}
  .rela.sdata2	${RELOCATING-0} : { *(.rela.sdata2)	}
  .rela.sbss2	${RELOCATING-0} : { *(.rela.sbss2)	}
  .text    ${RELOCATING-0} :
  {
    ${RELOCATING+${TEXT_START_SYMBOLS}}
    *(.text)
    ${RELOCATING+*(.text.*)}
    /* .gnu.warning sections are handled specially by elf32.em.  */
    *(.gnu.warning)
    ${RELOCATING+*(.gnu.linkonce.t*)}
    . += 16; /* To allow access of one vector beyond the last object.  */
  } =${NOP-0}
  .init		${RELOCATING-0} : { KEEP (*(.init))	} =${NOP-0}
  .fini		${RELOCATING-0} : { KEEP (*(.fini))	} =${NOP-0}
  .rodata  ${RELOCATING-0} :
  {
    *(.rodata)
    ${RELOCATING+*(.rodata.*)}
    ${RELOCATING+*(.gnu.linkonce.r*)}
  }
  .rodata1	${RELOCATING-0} : { *(.rodata1) }
  ${RELOCATING+_etext = .;}
  ${RELOCATING+PROVIDE (etext = .);}
  ${RELOCATING+PROVIDE (__etext = .);}
  ${CREATE_SHLIB-${SDATA2}}
  ${CREATE_SHLIB-${SBSS2}}
  ${RELOCATING+${OTHER_READONLY_SECTIONS}}

  /* Adjust the address for the data segment.  We want to adjust up to
     the same address within the page on the next page up.  It would
     be more correct to do this:
       ${RELOCATING+. = ${DATA_ADDR-ALIGN(${MAXPAGESIZE}) + (ALIGN(8) & (${MAXPAGESIZE} - 1))};}
     The current expression does not correctly handle the case of a
     text segment ending precisely at the end of a page; it causes the
     data segment to skip a page.  The above expression does not have
     this problem, but it will currently (2/95) cause BFD to allocate
     a single segment, combining both text and data, for this case.
     This will prevent the text segment from being shared among
     multiple executions of the program; I think that is more
     important than losing a page of the virtual address space (note
     that no actual memory is lost; the page which is skipped can not
     be referenced).  

     Align to a 16 byte boundary to allow vector access of non-vector 
     objects.  */
  ${RELOCATING+. = ${DATA_ADDR- ALIGN(16) + ${MAXPAGESIZE}};}

  .data  ${RELOCATING-0} :
  {
    ${RELOCATING+${DATA_START_SYMBOLS}}
    *(.data)
    ${RELOCATING+*(.data.*)}
    ${RELOCATING+*(.gnu.linkonce.d*)}
    ${CONSTRUCTING+CONSTRUCTORS}
    . += 16; /* To allow access of one vector beyond the last object.  */
  }
  .data1 ${RELOCATING-0} : { *(.data1) }
  ${RELOCATING+${OTHER_READWRITE_SECTIONS}}

  .got1		${RELOCATING-0} : { *(.got1) }
  .dynamic	${RELOCATING-0} : { *(.dynamic) }

  /* Put .ctors and .dtors next to the .got2 section, so that the pointers
     get relocated with -mrelocatable. Also put in the .fixup pointers.
     The current compiler no longer needs this, but keep it around for 2.7.2  */

		${RELOCATING+PROVIDE (_GOT2_START_ = .);}
		${RELOCATING+PROVIDE (__GOT2_START_ = .);}
  .got2		${RELOCATING-0} :  { *(.got2) }

		${RELOCATING+PROVIDE (__CTOR_LIST__ = .);}
		${RELOCATING+PROVIDE (___CTOR_LIST__ = .);}
                ${RELOCATING+${CTOR}}
		${RELOCATING+PROVIDE (__CTOR_END__ = .);}
		${RELOCATING+PROVIDE (___CTOR_END__ = .);}

		${RELOCATING+PROVIDE (__DTOR_LIST__ = .);}
		${RELOCATING+PROVIDE (___DTOR_LIST__ = .);}
                ${RELOCATING+${DTOR}}
		${RELOCATING+PROVIDE (__DTOR_END__ = .);}
		${RELOCATING+PROVIDE (___DTOR_END__ = .);}

		${RELOCATING+PROVIDE (_FIXUP_START_ = .);}
		${RELOCATING+PROVIDE (__FIXUP_START_ = .);}
  .fixup	${RELOCATING-0} : { *(.fixup) }
		${RELOCATING+PROVIDE (_FIXUP_END_ = .);}
		${RELOCATING+PROVIDE (__FIXUP_END_ = .);}
		${RELOCATING+PROVIDE (_GOT2_END_ = .);}
		${RELOCATING+PROVIDE (__GOT2_END_ = .);}

		${RELOCATING+PROVIDE (_GOT_START_ = .);}
		${RELOCATING+PROVIDE (__GOT_START_ = .);}
  .got		${RELOCATING-0} : { *(.got) }
  .got.plt	${RELOCATING-0} : { *(.got.plt) }
  ${CREATE_SHLIB+${SDATA2}}
  ${CREATE_SHLIB+${SBSS2}}
		${RELOCATING+PROVIDE (_GOT_END_ = .);}
		${RELOCATING+PROVIDE (__GOT_END_ = .);}

  /* We want the small data sections together, so single-instruction offsets
     can access them all, and initialized data all before uninitialized, so
     we can shorten the on-disk segment size.  */
  .sdata	${RELOCATING-0} : { *(.sdata) }
  ${RELOCATING+_edata  =  .;}
  ${RELOCATING+PROVIDE (edata = .);}
  ${RELOCATING+PROVIDE (__edata = .);}
  .sbss    ${RELOCATING-0} :
  {
    ${RELOCATING+PROVIDE (__sbss_start = .);}
    ${RELOCATING+PROVIDE (___sbss_start = .);}
    *(.sbss)
    *(.scommon)
    *(.dynsbss)
    ${RELOCATING+PROVIDE (__sbss_end = .);}
    ${RELOCATING+PROVIDE (___sbss_end = .);}
  }
  ${PLT}
  . = ALIGN(16); /* For vector access of non-vector objects.  */
  .bss     ${RELOCATING-0} :
  {
   ${RELOCATING+${OTHER_BSS_SYMBOLS}}
   ${RELOCATING+PROVIDE (__bss_start = .);}
   ${RELOCATING+PROVIDE (___bss_start = .);}
   *(.dynbss)
   *(.bss)
   *(COMMON)
   . += 16; /* To allow access of one vector beyond the last object.  */
  }
  ${RELOCATING+_end = . ;}
  ${RELOCATING+PROVIDE (end = .);}
  ${RELOCATING+PROVIDE (__end = .);}

  /* These are needed for ELF backends which have not yet been
     converted to the new style linker.  */
  .stab 0 : { *(.stab) }
  .stabstr 0 : { *(.stabstr) }

  /* DWARF debug sections.
     Symbols in the DWARF debugging sections are relative to the beginning
     of the section so we begin them at 0.  */

  /* DWARF 1 */
  .debug          0 : { *(.debug) }
  .line           0 : { *(.line) }

  /* GNU DWARF 1 extensions */
  .debug_srcinfo  0 : { *(.debug_srcinfo) }
  .debug_sfnames  0 : { *(.debug_sfnames) }

  /* DWARF 1.1 and DWARF 2 */
  .debug_aranges  0 : { *(.debug_aranges) }
  .debug_pubnames 0 : { *(.debug_pubnames) }

  /* DWARF 2 */
  .debug_info     0 : { *(.debug_info) }
  .debug_abbrev   0 : { *(.debug_abbrev) }
  .debug_line     0 : { *(.debug_line) }
  .debug_frame    0 : { *(.debug_frame) }
  .debug_str      0 : { *(.debug_str) }
  .debug_loc      0 : { *(.debug_loc) }
  .debug_macinfo  0 : { *(.debug_macinfo) }

  /* SGI/MIPS DWARF 2 extensions */
  .debug_weaknames 0 : { *(.debug_weaknames) }
  .debug_funcnames 0 : { *(.debug_funcnames) }
  .debug_typenames 0 : { *(.debug_typenames) }
  .debug_varnames  0 : { *(.debug_varnames) }

  /* These must appear regardless of ${RELOCATING}.  */
  ${OTHER_SECTIONS}
}
EOF

# Target: PowerPC running qnx6 eabi 
include $(srcdir)/config/nto.mt

ppc-nto-tdep.o: ppc-nto-tdep.c $(defs_h) $(gdbtypes_h) $(osabi_h) $(frame_h) \
	$(target_h) $(gdbcore_h) $(regcache_h) $(regset_h) \
	$(trad_frame_h) $(tramp_frame_h) $(gdb_assert_h) \
	$(gdb_string_h) $(solib_svr4_h) $(ppc_tdep_h) $(nto_tdep_h) \
	$(osabi_h) $(trad_frame_h) $(frame_undwind_h) $(objfiles_h)

TDEPFILES= rs6000-tdep.o ppc-sysv-tdep.o corelow.o solib.o solib-svr4.o \
	ppc-nto-tdep.o $(NTO_TDEPFILES)

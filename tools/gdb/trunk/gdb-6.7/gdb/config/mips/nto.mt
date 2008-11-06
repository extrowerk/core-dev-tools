# Target: Big-endian mips board, typically an IDT.
include $(srcdir)/config/nto.mt

mips-nto-tdep.o: mips-nto-tdep.c $(defs_h) $(frame_h) $(target_h) \
	$(regcache_h) $(solib_svr4_h) $(mips_tdep_h) $(nto_tdep_h) \
	$(osabi_h) $(objfiles_h) $(frame_h) $(trad_frame_h) \
	$(tramp_frame_h) $(gdbcore_h)

TDEPFILES= mips-tdep.o corelow.o solib.o solib-svr4.o \
	mips-nto-tdep.o $(NTO_TDEPFILES)

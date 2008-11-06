# Target: Hitachi Super-H with NTO/pdebug.
include $(srcdir)/config/nto.mt

sh-nto-tdep.o: sh-nto-tdep.c $(defs_h) $(frame_h) $(target_h) $(regcache_h) \
	$(solib_svr4_h) $(sh_tdep_h) $(nto_tdep_h) $(osabi_h) \
	$(trad_frame_h) $(tramp_frame_h)

TDEPFILES= sh-tdep.o corelow.o solib.o solib-svr4.o \
	sh-nto-tdep.o sh64-tdep.o $(NTO_TDEPFILES)

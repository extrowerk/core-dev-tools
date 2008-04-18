# Target: ARM based machine running QNX/Neutrino
include $(srcdir)/config/nto.mt

arm-nto-tdep.o: arm-nto-tdep.c $(defs_h) $(frame_h) $(target_h) $(regcache_h) \
	$(solib_svr4_h) $(arm_tdep_h) $(nto_tdep_h) $(osabi_h) \
	$(trad_frame_h) $(tramp_frame_h) $(gdbcore_h)

TDEPFILES= corelow.o solib.o solib-svr4.o \
	arm-nto-tdep.o nto-tdep.o arm-tdep.o remote-nto.o

# Target: Intel 386 running qnx6.
include $(srcdir)/config/nto.mt

i386-nto-tdep.o: i386-nto-tdep.c $(defs_h) $(frame_h) $(osabi_h) \
	$(regcache_h) $(target_h) $(gdb_assert_h) $(gdb_string_h) \
	$(i386_tdep_h) $(i387_tdep_h) $(nto_tdep_h) $(solib_svr4_h)

TDEPFILES = i386-tdep.o i387-tdep.o corelow.o solib.o solib-svr4.o \
	i386-nto-tdep.o nto-tdep.o remote-nto.o

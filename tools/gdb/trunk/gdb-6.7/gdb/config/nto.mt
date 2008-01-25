nto_share_dsmsgs_h = $(srcdir)/nto-share/dsmsgs.h
nto_share_debug_h = $(srcdir)/nto-share/debug.h
remote-nto.o: remote-nto.c $(defs_h) $(exceptions_h) $(gdb_string_h) \
	$(terminal_h) $(inferior_h) $(target_h) $(gdbcmd_h) $(objfiles_h) \
	$(completer_h) $(cli_decode_h) $(regcache_h) $(gdbcore_h) $(serial_h) \
	$(readline_h) $(elf_bfd_h) $(elf_common_h) $(nto_share_dsmsgs_h) \
	$(nto_share_debug_h) $(nto_tdep_h)

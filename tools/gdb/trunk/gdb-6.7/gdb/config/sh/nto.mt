# Target: Hitachi Super-H with NTO/pdebug.
include $(srcdir)/config/nto.mt
TDEPFILES= sh-tdep.o corelow.o solib.o solib-svr4.o \
	remote-nto.o nto-tdep.o sh-nto-tdep.o sh64-tdep.o

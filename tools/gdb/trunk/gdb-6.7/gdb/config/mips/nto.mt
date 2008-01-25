# Target: Big-endian mips board, typically an IDT.
include $(srcdir)/config/nto.mt
TDEPFILES= mips-tdep.o corelow.o solib.o solib-svr4.o \
	mips-nto-tdep.o nto-tdep.o remote-nto.o

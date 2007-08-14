# Target: Big-endian mips board, typically an IDT.
TDEPFILES= mips-tdep.o remote-mips.o remote-array.o solib.o solib-svr4.o \
	corelow.o remote-qnx.o remote-qnx-mips.o nto-cache.o
TM_FILE= tm-mips-qnx.h

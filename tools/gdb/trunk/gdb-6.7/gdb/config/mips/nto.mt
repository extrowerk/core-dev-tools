# Target: Big-endian mips board, typically an IDT.
TDEPFILES= mips-tdep.o corelow.o solib.o solib-svr4.o \
	mips-nto-tdep.o nto-tdep.o remote-nto.o
#DEPRECATED_TM_FILE= tm-nto.h

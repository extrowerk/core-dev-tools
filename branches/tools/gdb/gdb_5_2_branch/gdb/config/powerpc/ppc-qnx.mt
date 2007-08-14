# Target: PowerPC running eabi
TDEPFILES= rs6000-tdep.o monitor.o dsrec.o ppcbug-rom.o solib.o solib-svr4.o \
	remote-qnx.o remote-qnx-ppc.o corelow.o ppc-linux-tdep.o nto-cache.o
TM_FILE= tm-ppc-qnx.h

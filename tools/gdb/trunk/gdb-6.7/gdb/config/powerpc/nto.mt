# Target: PowerPC running qnx6 eabi 
include $(srcdir)/config/nto.mt
TDEPFILES= rs6000-tdep.o ppc-sysv-tdep.o corelow.o solib.o solib-svr4.o \
	ppc-nto-tdep.o nto-tdep.o remote-nto.o

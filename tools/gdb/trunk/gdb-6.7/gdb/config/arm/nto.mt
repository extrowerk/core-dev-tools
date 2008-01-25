# Target: ARM based machine running QNX/Neutrino
include $(srcdir)/config/nto.mt
TDEPFILES= corelow.o solib.o solib-svr4.o \
	arm-nto-tdep.o nto-tdep.o arm-tdep.o remote-nto.o

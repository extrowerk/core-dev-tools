# Target: ARM based machine running QNX/Neutrino
TM_FILE= tm-armnto.h
TDEPFILES= arm-tdep.o solib.o solib-svr4.o corelow.o \
	remote-qnx.o remote-qnx-arm.o nto-cache.o

#SIM_OBS = remote-sim.o
#SIM = ../sim/arm/libsim.a

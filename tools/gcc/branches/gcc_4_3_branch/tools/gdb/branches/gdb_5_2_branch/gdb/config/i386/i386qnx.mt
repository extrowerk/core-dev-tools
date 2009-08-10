# Target: Intel 386 running qnx6
TDEPFILES= i386-tdep.o i387-tdep.o corelow.o solib.o solib-svr4.o \
	i387-nat.o remote-qnx.o remote-qnx-i386.o qnxdebug.o nto-cache.o
TM_FILE= tm-i386qnx.h

# Elf with shared libm, so we can link it into the shared libstdc++.

ARLIB   = libstdc++.a.2.$(VERSION)
SHLIB   = libstdc++.so.2.$(VERSION)

LIBS    = $(ARLIB) $(ARLINK) $(SHLIB) $(SHLINK)
SHFLAGS = -Wl,-soname,$(SHLIB)
SHDEPS  = -lm
DEPLIBS = ../$(SHLIB)

CXXFLAGS = -fno-honor-std -g -O2

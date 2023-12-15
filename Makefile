VERSION = 1.0
SHELL = /bin/sh

CXX = g++
CXXFLAGS = -std=c++11 -O -Wall -Wextra -pedantic
CC = gcc
CFLAGS = -std=c11 -O -Wall -Wextra -pedantic
SRCDIR = src
BUILDDIR = build
BINS = $(addprefix $(BUILDDIR)/,rlzparse rlzunparse builddict rlztools.rlzexplain rlztools.suffixdump rlztools.endflip rlztools.divsuffix rlztools.5to8 rlztools.5to4 rlztools.count-vbyte-tokens)

all: $(BINS)

$(BINS): | $(BUILDDIR)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/rlzparse: $(addprefix $(SRCDIR)/,rlzparse.cpp rlzcommon.cpp rlzcommon.h)
	$(CXX) $(CXXFLAGS) -o $(BUILDDIR)/rlzparse $(SRCDIR)/rlzparse.cpp $(SRCDIR)/rlzcommon.cpp

$(BUILDDIR)/rlzunparse: $(addprefix $(SRCDIR)/,rlzunparse.cpp rlzcommon.cpp rlzcommon.h)
	$(CXX) $(CXXFLAGS) -o $(BUILDDIR)/rlzunparse $(SRCDIR)/rlzunparse.cpp $(SRCDIR)/rlzcommon.cpp

$(BUILDDIR)/builddict: $(SRCDIR)/builddict.cpp
	$(CXX) $(CXXFLAGS) -o $(BUILDDIR)/builddict $(SRCDIR)/builddict.cpp

$(BUILDDIR)/rlztools.rlzexplain: $(addprefix $(SRCDIR)/,rlzexplain.cpp rlzcommon.cpp rlzcommon.h)
	$(CXX) $(CXXFLAGS) -o $(BUILDDIR)/rlztools.rlzexplain $(SRCDIR)/rlzexplain.cpp $(SRCDIR)/rlzcommon.cpp

$(BUILDDIR)/rlztools.suffixdump: $(addprefix $(SRCDIR)/,suffixdump.cpp rlzcommon.cpp rlzcommon.h)
	$(CXX) $(CXXFLAGS) -o $(BUILDDIR)/rlztools.suffixdump $(SRCDIR)/suffixdump.cpp $(SRCDIR)/rlzcommon.cpp

$(BUILDDIR)/rlztools.endflip: $(SRCDIR)/endflip.c
	$(CC) $(CFLAGS) -o $(BUILDDIR)/rlztools.endflip $(SRCDIR)/endflip.c

$(BUILDDIR)/rlztools.5to8: $(SRCDIR)/5to8.c
	$(CC) $(CFLAGS) -o $(BUILDDIR)/rlztools.5to8 $(SRCDIR)/5to8.c

$(BUILDDIR)/rlztools.5to4: $(SRCDIR)/5to8.c
	$(CC) $(CFLAGS) -o $(BUILDDIR)/rlztools.5to4 -D FIVETOFOUR $(SRCDIR)/5to8.c

$(BUILDDIR)/rlztools.32toVbyte: $(SRCDIR)/32toVbyte.c
	$(CC) $(CFLAGS) -o $(BUILDDIR)/rlztools.32toVbyte $(SRCDIR)/32toVbyte.c

$(BUILDDIR)/rlztools.count-vbyte-tokens: $(SRCDIR)/count-vbyte-tokens.c
	$(CC) $(CFLAGS) -o $(BUILDDIR)/rlztools.count-vbyte-tokens $(SRCDIR)/count-vbyte-tokens.c

$(BUILDDIR)/rlztools.divsuffix: $(SRCDIR)/divsuffix.cpp
	$(CXX) $(CXXFLAGS) -o $(BUILDDIR)/rlztools.divsuffix $(SRCDIR)/divsuffix.cpp

clean:
	rm -rf $(BUILDDIR)


SHELL = /bin/sh

CC = gcc
LD = gcc

EXECS = sg_rw \
	sg_rw_stream \
	sg_readcap \
	sg_stctrl \
	sg_ststat \
	sg_boctrl \
	sg_ielog \
	sg_blksz

LARGE_FILE_FLAGS = -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64

CFLAGS = -g -O2 -W -Wall -iquote ../include -D_REENTRANT $(LARGE_FILE_FLAGS)

LDFLAGS =

LIBFILES = ../lib/sg_lib.o ../lib/sg_lib_data.o ../lib/sg_io_linux.o

all: $(EXECS)

depend dep:
	for i in *.c; do $(CC) $(INCLUDES) $(CFLAGS) -M $$i; \
	done > .depend

install:
	/bin/cp $(EXECS) /usr/bin

clean:
	/bin/rm -f *.o $(EXECS) core .depend

sg_rw: sg_rw.o $(LIBFILES)
	$(LD) -o $@ $(LDFLAGS) $^

sg_rw_stream: sg_rw_stream.o $(LIBFILES)
	$(LD) -o $@ $(LDFLAGS) $^

sg_readcap: sg_readcap.o $(LIBFILES)
	$(LD) -o $@ $(LDFLAGS) $^

sg_stctrl: sg_stctrl.o $(LIBFILES)
	$(LD) -o $@ $(LDFLAGS) $^

sg_ststat: sg_ststat.o $(LIBFILES)
	$(LD) -o $@ $(LDFLAGS) $^

sg_boctrl: sg_boctrl.o $(LIBFILES)
	$(LD) -o $@ $(LDFLAGS) $^

sg_ielog: sg_ielog.o $(LIBFILES)
	$(LD) -static -o $@ $(LDFLAGS) $^

sg_blksz: sg_blksz.o $(LIBFILES)
	$(LD) -static -o $@ $(LDFLAGS) $^

ifeq (.depend,$(wildcard .depend))
include .depend
endif

#  Build system for CAEN 1720 FADC

export OSNAME       := $(shell uname)

VXDIR=/adaqfs/halla/apar/vxworks/5.5/5.5
CCVXFLAGS =  -I$(VXDIR) -DCPU_FAMILY=PPC -DCPU=PPC604 -mlongcall


all : caen1720Lib.o


caen1720Lib.o: caen1720Lib.c caen1720.h
	rm -f $@
	ccppc -c $(CCVXFLAGS) caen1720Lib.c

clean :
	rm -f *.o

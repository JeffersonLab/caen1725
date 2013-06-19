#
# File:
#    Makefile
#
# Description:
#    Makefile for the CAEN 1720 Library using a VME Controller running Linux/vxWorks
#
# SVN: $Rev$
#
# Uncomment DEBUG line, to include some debugging info ( -g and -Wall)
DEBUG=1
#
ifndef ARCH
	ifdef LINUXVME_LIB
		ARCH=Linux
	else
		ARCH=VXWORKSPPC
	endif
endif

# Defs and build for VxWorks
ifeq ($(ARCH),VXWORKSPPC)
VXWORKS_ROOT = /site/vxworks/5.5/ppc/target

CC			= ccppc
LD			= ldppc
DEFS			= -mcpu=604 -DCPU=PPC604 -DVXWORKS -D_GNU_TOOL -mlongcall \
				-fno-for-scope -fno-builtin -fvolatile -DVXWORKSPPC
INCS			= -I. -I$(VXWORKS_ROOT)/h -I$(VXWORKS_ROOT)/h/rpc -I$(VXWORKS_ROOT)/h/net
CFLAGS			= $(INCS) $(DEFS)

endif #ARCH=VXWORKSPPC#

# Defs and build for Linux
ifeq ($(ARCH),Linux)
LINUXVME_LIB		?= ${CODA}/extensions/linuxvme/libs
LINUXVME_INC		?= ${CODA}/extensions/linuxvme/include

CC			= gcc
AR                      = ar
RANLIB                  = ranlib
CFLAGS			= -I. -I${LINUXVME_INC} -I/usr/include \
				-L${LINUXVME_LIB} -L.

LIBS			= libcaen1720.a
endif #ARCH=Linux#

ifdef DEBUG
CFLAGS			+= -Wall -g
else
CFLAGS			+= -O2
endif
SRC			= caen1720Lib.c
HDRS			= $(SRC:.c=.h)
OBJ			= caen1720Lib.o

ifeq ($(ARCH),Linux)
all: echoarch $(LIBS) links
else
all: echoarch $(OBJ) copy
endif

$(OBJ): $(SRC) $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $(SRC)

$(LIBS): $(OBJ)
	$(CC) -fpic -shared $(CFLAGS) -o $(@:%.a=%.so) $(SRC)
	$(AR) ruv $@ $<
	$(RANLIB) $@

ifeq ($(ARCH),Linux)
links: $(LIBS)
	@ln -vsf $(PWD)/$< $(LINUXVME_LIB)/$<
	@ln -vsf $(PWD)/$(<:%.a=%.so) $(LINUXVME_LIB)/$(<:%.a=%.so)
	@ln -vsf ${PWD}/*lib.h $(LINUXVME_INC)

else
copy: $(OBJ)
	cp $< vx/
endif

clean:
	@rm -vf $(OBJ) *.{a,so}

echoarch:
	@echo "Make for $(ARCH)"

.PHONY: clean echoarch


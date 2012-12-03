#
# refind/Makefile
# Build control file for the rEFInd boot menu
#

SRCDIR = .

VPATH = $(SRCDIR)

ARCH            = $(shell uname -m | sed s,i[3456789]86,ia32,)

TARGET	= refind.efi

ifeq ($(ARCH),ia32)
  LIBEG = build32
  TARGET = refind_ia32.efi
endif

ifeq ($(ARCH),x86_64)
  LIBEG = build64
  TARGET = refind_x64.efi
endif

LOCAL_CPPFLAGS  = -I$(SRCDIR) -I$(SRCDIR)/../include -I$(SRCDIR)/../libeg
#LOCAL_LDFLAGS   = -L$(SRCDIR)/../libeg/$(LIBEG)
LOCAL_LDFLAGS   = -L$(SRCDIR)/../libeg/
LOCAL_LIBS      = -leg

OBJS            = main.o config.o menu.o screen.o icns.o lib.o mok.o driver_support.o

all: $(TARGET)

include $(SRCDIR)/../Make.common

$(TARGET): $(SHLIB_TARGET)
	$(OBJCOPY) -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel \
		   -j .rela -j .reloc --target=$(FORMAT) $< $@

# EOF
# DO NOT DELETE

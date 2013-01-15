#
# libeg/Makefile
# Build control file for the libeg library
#

SRCDIR = .

VPATH = $(SRCDIR)

LOCAL_CPPFLAGS  = -I$(SRCDIR) -I$(SRCDIR)/../include

OBJS            = screen.o image.o text.o load_bmp.o load_icns.o lodepng.o
TARGET          = libeg.a

all: $(TARGET)

include $(SRCDIR)/../Make.common

# EOF

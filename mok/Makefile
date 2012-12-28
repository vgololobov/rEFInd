#
# mok/Makefile
# Build control file for the libeg library
#

SRCDIR = .

VPATH = $(SRCDIR)

LOCAL_CPPFLAGS  = -I$(SRCDIR) -I$(SRCDIR)/../include

OBJS            = guid.o mok.o security_policy.o simple_file.o
TARGET          = libmok.a

all: $(TARGET)

include $(SRCDIR)/../Make.common

# EOF

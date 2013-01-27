
DRIVERNAME = ext4

CC		= /usr/bin/gcc
CFLAGS		= -Wall -g -D_REENTRANT -DVERSION=\"$(VERSION)\" -DHOST_POSIX -I ../ -DFSTYPE=$(DRIVERNAME)

FSW_NAMES       = ../fsw_core ../fsw_lib
FSW_OBJS	= $(FSW_NAMES:=.o)
LSLR_OBJS	= $(FSW_OBJS) ../fsw_$(DRIVERNAME).o fsw_posix.o lslr.o
LSLR_BIN	= lslr
LSROOT_OBJS	= $(FSW_OBJS) ../fsw_ext2.o ../fsw_ext4.o ../fsw_hfs.o ../fsw_iso9660.o ../fsw_reiserfs.o fsw_posix.o lsroot.o
LSROOT_BIN	= lsroot
CATFILE_OBJS = $(FSW_OBJS) ../fsw_$(DRIVERNAME).o fsw_posix.o catfile.o
CATFILE_BIN = catfile

all:        $(CATFILE_BIN) $(LSLR_BIN) $(LSROOT_BIN)

$(LSLR_BIN):	$(LSLR_OBJS)
		$(CC) $(CFLAGS) -o $(LSLR_BIN) $(LSLR_OBJS) $(LDFLAGS)

$(LSROOT_BIN):	$(LSROOT_OBJS) 
		$(CC) $(CFLAGS) -o $(LSROOT_BIN) $(LSROOT_OBJS) $(LDFLAGS)

$(CATFILE_BIN): $(CATFILE_OBJS)
		$(CC) $(CFLAGS) -o $(CATFILE_BIN) $(CATFILE_OBJS) $(LDFLAGS)


clean:		
		@rm -f *.o ../*.o lslr lsroot catfile


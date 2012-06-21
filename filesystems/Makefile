# meta-Makefile for rEFInd filesystem drivers
#
# Most of the functionality is in Make.tiano; this Makefile merely
# deletes critical temporary files and calls Make.tiano with the
# name of the driver to be built. This is done because of a dependency
# in the fsw_efi.c file on the filesystem type; this file must be
# recompiled for each new filesystem built.

INSTALL_DIR = /boot/efi/EFI/refind/drivers

FILESYSTEMS = ext2 reiserfs iso9660 hfs
TEXTFILES = $(FILESYSTEMS:=*.txt)

all:	$(FILESYSTEMS)

ext2:
	rm -f fsw_efi.o
	make DRIVERNAME=ext2 -f Make.tiano

reiserfs:
	rm -f fsw_efi.o
	make DRIVERNAME=reiserfs -f Make.tiano

iso9660:
	rm -f fsw_efi.o
	make DRIVERNAME=iso9660 -f Make.tiano

hfs:
	rm -f fsw_efi.o
	make DRIVERNAME=hfs -f Make.tiano

# utility rules

clean:
	rm -f *~ *.bak *.o *.efi *.dll err.txt $(TEXTFILES)


install:
	mkdir -p $(INSTALL_DIR)
	cp *.efi $(INSTALL_DIR)

# DO NOT DELETE

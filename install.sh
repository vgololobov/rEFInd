#!/bin/bash
#
# Linux/MacOS X script to install rEFInd
#
# Usage:
#
# ./install.sh [esp]
#
# The "esp" option is valid only on Mac OS X; it causes
# installation to the EFI System Partition (ESP) rather than
# to the current OS X boot partition. Under Linux, this script
# installs to the ESP by default.
#
# This program is copyright (c) 2012 by Roderick W. Smith
# It is released under the terms of the GNU GPL, version 3,
# a copy of which should be included in the file COPYING.txt.
#
# Revision history:
#
# 0.4.5   -- Fixed check for rEFItBlesser in OS X
# 0.4.2   -- Added notice about BIOS-based OSes & made NVRAM changes in Linux smarter
# 0.4.1   -- Added check for rEFItBlesser in OS X
# 0.3.3.1 -- Fixed OS X 10.7 bug; also works as make target
# 0.3.2.1 -- Check for presence of source files; aborts if not present
# 0.3.2   -- Initial version
#
# Note: install.sh version numbers match those of the rEFInd package
# with which they first appeared.

TargetDir=/EFI/refind

#
# Functions used by both OS X and Linux....
#

# Abort if the rEFInd files can't be found.
# Also sets $ConfFile to point to the configuration file, and
# $IconsDir to point to the icons directory
CheckForFiles() {
   # Note: This check is satisfied if EITHER the 32- or the 64-bit version
   # is found, even on the wrong platform. This is because the platform
   # hasn't yet been determined. This could obviously be improved, but it
   # would mean restructuring lots more code....
   if [[ ! -f $RefindDir/refind_ia32.efi && ! -f $RefindDir/refind_x64.efi ]] ; then
      echo "The rEFInd binary file is missing! Aborting installation!"
      exit 1
   fi

   if [[ -f $RefindDir/refind.conf-sample ]] ; then
      ConfFile=$RefindDir/refind.conf-sample
   elif [[ -f $ThisDir/refind.conf-sample ]] ; then
      ConfFile=$ThisDir/refind.conf-sample
   else
      echo "The sample configuration file is missing! Aborting installation!"
      exit 1
   fi

   if [[ -d $RefindDir/icons ]] ; then
      IconsDir=$RefindDir/icons
   elif [[ -d $ThisDir/icons ]] ; then
      IconsDir=$ThisDir/icons
   else
      echo "The icons directory is missing! Aborting installation!"
   fi
} # CheckForFiles()

# Copy the rEFInd files to the ESP or OS X root partition.
# Sets Problems=1 if any critical commands fail.
CopyRefindFiles() {
   mkdir -p $InstallPart/$TargetDir &> /dev/null
   if [[ $Platform == 'EFI32' ]] ; then
      cp $RefindDir/refind_ia32.efi $InstallPart/$TargetDir
      if [[ $? != 0 ]] ; then
         Problems=1
      fi
      Refind="refind_ia32.efi"
   elif [[ $Platform == 'EFI64' ]] ; then
      cp $RefindDir/refind_x64.efi $InstallPart/$TargetDir
      if [[ $? != 0 ]] ; then
         Problems=1
      fi
      Refind="refind_x64.efi"
   else
      echo "Unknown platform! Aborting!"
      exit 1
   fi
   echo "Copied rEFInd binary file $Refind"
   echo ""
   if [[ -d $InstallPart/$TargetDir/icons ]] ; then
      rm -rf $InstallPart/$TargetDir/icons-backup &> /dev/null
      mv -f $InstallPart/$TargetDir/icons $InstallPart/$TargetDir/icons-backup
      echo "Notice: Backed up existing icons directory as icons-backup."
   fi
   cp -r $IconsDir $InstallPart/$TargetDir
   if [[ $? != 0 ]] ; then
      Problems=1
   fi
   if [[ -f $InstallPart/$TargetDir/refind.conf ]] ; then
      echo "Existing refind.conf file found; copying sample file as refind.conf-sample"
      echo "to avoid collision."
      echo ""
      cp -f $ConfFile $InstallPart/$TargetDir
      if [[ $? != 0 ]] ; then
         Problems=1
      fi
   else
      echo "Copying sample configuration file as refind.conf; edit this file to configure"
      echo "rEFInd."
      echo ""
      cp -f $ConfFile $InstallPart/$TargetDir/refind.conf
      if [[ $? != 0 ]] ; then
         Problems=1
      fi
   fi
} # CopyRefindFiles()


#
# A series of OS X support functions....
#

# Mount the ESP at /Volumes/ESP or determine its current mount
# point.
# Sets InstallPart to the ESP mount point
# Sets UnmountEsp if we mounted it
MountOSXESP() {
   # Identify the ESP. Note: This returns the FIRST ESP found;
   # if the system has multiple disks, this could be wrong!
   Temp=`diskutil list | grep " EFI "`
   Esp=/dev/`echo $Temp | cut -f 5 -d ' '`
   # If the ESP is mounted, use its current mount point....
   Temp=`df | grep $Esp`
   InstallPart=`echo $Temp | cut -f 6 -d ' '`
   if [[ $InstallPart == '' ]] ; then
      mkdir /Volumes/ESP &> /dev/null
      mount -t msdos $Esp /Volumes/ESP
      if [[ $? != 0 ]] ; then
         echo "Unable to mount ESP! Aborting!\n"
         exit 1
      fi
      UnmountEsp=1
      InstallPart="/Volumes/ESP"
   fi
} # MountOSXESP()

# Control the OS X installation.
# Sets Problems=1 if problems found during the installation.
InstallOnOSX() {
   echo "Installing rEFInd on OS X...."
   if [[ $1 == 'esp' || $1 == 'ESP' ]] ; then
      MountOSXESP
   else
      InstallPart="/"
   fi
   echo "Installing rEFInd to the partition mounted at '$InstallPart'"
   Platform=`ioreg -l -p IODeviceTree | grep firmware-abi | cut -d "\"" -f 4`
   CopyRefindFiles
   if [[ $1 == 'esp' || $1 == 'ESP' ]] ; then
      bless --mount $InstallPart --setBoot --file $InstallPart/$TargetDir/$Refind
   else
      bless --setBoot --folder $InstallPart/$TargetDir --file $InstallPart/$TargetDir/$Refind
   fi
   if [[ $? != 0 ]] ; then
      Problems=1
   fi
   if [[ -f /Library/StartupItems/rEFItBlesser || -d /Library/StartupItems/rEFItBlesser ]] ; then
      echo
      echo "/Library/StartupItems/rEFItBlesser found!"
      echo "This program is part of rEFIt, and will cause rEFInd to fail to work after"
      echo -n "its first boot. Do you want to remove rEFItBlesser (Y/N)? "
      read YesNo
      if [[ $YesNo == "Y" || $YesNo == "y" ]] ; then
         echo "Deleting /Library/StartupItems/rEFItBlesser..."
	 rm -r /Library/StartupItems/rEFItBlesser
      else
         echo "Not deleting rEFItBlesser."
      fi
   fi
   echo
   echo "WARNING: If you have an Advanced Format disk, *DO NOT* attempt to check the"
   echo "bless status with 'bless --info', since this is known to cause disk corruption"
   echo "on some systems!!"
   echo
   echo "NOTE: If you want to boot an OS via BIOS emulation (such as Windows or some"
   echo "Linux installations), you *MUST* edit the $InstallPart/$TargetDir/refind.conf"
   echo "file's 'scanfor' line to include the 'hdbios' option, and perhaps"
   echo "'biosexternal' and 'cd', as well."
   echo
} # InstallOnOSX()


#
# Now a series of Linux support functions....
#

# Identifies the ESP's location (/boot or /boot/efi); aborts if
# the ESP isn't mounted at either location.
# Sets InstallPart to the ESP mount point.
FindLinuxESP() {
   EspLine=`df /boot/efi | grep boot`
   InstallPart=`echo $EspLine | cut -d " " -f 6`
   EspFilesystem=`grep $InstallPart /etc/mtab | cut -d " " -f 3`
   if [[ $EspFilesystem != 'vfat' ]] ; then
      echo "/boot/efi doesn't seem to be on a VFAT filesystem. The ESP must be mounted at"
      echo "/boot or /boot/efi and it must be VFAT! Aborting!"
      exit 1
   fi
   echo "ESP was found at $InstallPart using $EspFilesystem"
} # MountLinuxESP

# Uses efibootmgr to add an entry for rEFInd to the EFI's NVRAM.
# If this fails, sets Problems=1
AddBootEntry() {
   InstallIt="0"
   Efibootmgr=`which efibootmgr 2> /dev/null`
   if [[ $Efibootmgr ]] ; then
      modprobe efivars &> /dev/null
      InstallDisk=`grep $InstallPart /etc/mtab | cut -d " " -f 1 | cut -c 1-8`
      PartNum=`grep $InstallPart /etc/mtab | cut -d " " -f 1 | cut -c 9-10`
      EntryFilename=$TargetDir/$Refind
      EfiEntryFilename=`echo ${EntryFilename//\//\\\}`
      EfiEntryFilename2=`echo ${EfiEntryFilename} | sed s/\\\\\\\\/\\\\\\\\\\\\\\\\/g`
      ExistingEntry=`$Efibootmgr -v | grep $EfiEntryFilename2`
      if [[ $ExistingEntry ]] ; then
         ExistingEntryBootNum=`echo $ExistingEntry | cut -c 5-8`
         FirstBoot=`$Efibootmgr | grep BootOrder | cut -c 12-15`
         if [[ $ExistingEntryBootNum != $FirstBoot ]] ; then
            echo "An existing rEFInd boot entry exists, but isn't set as the default boot"
            echo "manager. The boot order is being adjusted to make rEFInd the default boot"
            echo "manager. If this is NOT what you want, you should use efibootmgr to"
            echo "manually adjust your EFI's boot order."
            $Efibootmgr -b $ExistingEntryBootNum -B &> /dev/null
	    InstallIt="1"
         fi
      else
         InstallIt="1"
      fi
      if [[ $InstallIt == "1" ]] ; then
         echo "Installing it!"
         $Efibootmgr -c -l $EfiEntryFilename -L rEFInd -d $InstallDisk -p $PartNum &> /dev/null
         if [[ $? != 0 ]] ; then
            EfibootmgrProblems=1
            Problems=1
         fi
      fi
   else
      EfibootmgrProblems=1
      Problems=1
   fi
   if [[ $EfibootmgrProblems ]] ; then
      echo
      echo "ALERT: There were problems running the efibootmgr program! You may need to"
      echo "rename the $Refind binary to the default name (EFI/boot/bootx64.efi"
      echo "on x86-64 systems or EFI/boot/bootia32.efi on x86 systems) to have it run!"
      echo
   fi
} # AddBootEntry()

# Controls rEFInd installation under Linux.
# Sets Problems=1 if something goes wrong.
InstallOnLinux() {
   echo "Installing rEFInd on Linux...."
   FindLinuxESP
   CpuType=`uname -m`
   if [[ $CpuType == 'x86_64' ]] ; then
      Platform="EFI64"
   elif [[ $CpuType == 'i386' || $CpuType == 'i486' || $CpuType == 'i586' || $CpuType == 'i686' ]] ; then
      Platform="EFI32"
      echo
      echo "CAUTION: This Linux installation uses a 32-bit kernel. 32-bit EFI-based"
      echo "computers are VERY RARE. If you've installed a 32-bit version of Linux"
      echo "on a 64-bit computer, you should manually install the 64-bit version of"
      echo "rEFInd. If you're installing on a Mac, you should do so from OS X. If"
      echo "you're positive you want to continue with this installation, answer 'Y'"
      echo "to the following question..."
      echo
      echo -n "Are you sure you want to continue (Y/N)? "
      read ContYN
      if [[ $ContYN == "Y" || $ContYN == "y" ]] ; then
         echo "OK; continuing with the installation..."
      else
         exit 0
      fi
   else
      echo "Unknown CPU type '$CpuType'; aborting!"
      exit 1
   fi
   CopyRefindFiles
   AddBootEntry
} # InstallOnLinux()

#
# The main part of the script. Sets a few environment variables,
# performs a few startup checks, and then calls functions to
# install under OS X or Linux, depending on the detected platform.
#

OSName=`uname -s`
ThisDir="$( cd -P "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
RefindDir="$ThisDir/refind"
ThisScript="$ThisDir/`basename $0`"
CheckForFiles
if [[ `whoami` != "root" ]] ; then
   echo "Not running as root; attempting to elevate privileges via sudo...."
   sudo $ThisScript $1
   if [[ $? != 0 ]] ; then
      echo "This script must be run as root (or using sudo). Exiting!"
      exit 1
   else
      exit 0
   fi
fi
if [[ $OSName == 'Darwin' ]] ; then
   InstallOnOSX $1
elif [[ $OSName == 'Linux' ]] ; then
   InstallOnLinux
else
   echo "Running on unknown OS; aborting!"
fi

if [[ $Problems ]] ; then
   echo
   echo "ALERT:"
   echo "Installation has completed, but problems were detected. Review the output for"
   echo "error messages and take corrective measures as necessary. You may need to"
   echo "re-run this script or install manually before rEFInd will work."
   echo
else
   echo
   echo "Installation has completed successfully."
   echo
fi

if [[ $UnmountEsp ]] ; then
   umount $InstallPart
fi

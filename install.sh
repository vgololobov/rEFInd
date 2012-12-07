#!/bin/bash
#
# Linux/MacOS X script to install rEFInd
#
# Usage:
#
# ./install.sh [options]
#
# options include:
#    "--esp" to install to the ESP rather than to the system's root
#           filesystem. This is the default on Linux
#    "--usedefault {devicefile}" to install as default
#           (/EFI/BOOT/BOOTX64.EFI and similar) to the specified
#           device (/dev/sdd1 or whatever) without registering with
#           the NVRAM
#    "--drivers" to install drivers along with regular files
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
# 0.5.0   -- Added --usedefault & --drivers options & changed "esp" option to "--esp"
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
EtcKeysDir=/etc/refind.d/keys
SBModeInstall=0

#
# Functions used by both OS X and Linux....
#

GetParams() {
   InstallToEspOnMac=0
   InstallDrivers=0
   while [[ $# -gt 0 ]]; do
      case $1 in
         --esp | --ESP) InstallToEspOnMac=1
              ;;
         --usedefault) TargetDir=/EFI/BOOT
              TargetPart=$2
              shift
              ;;
         --drivers) InstallDrivers=1
              ;;
         * ) echo "Usage: $0 [--esp | --usedefault {device-file}] [--drivers]"
              echo "Aborting!"
              exit 1
      esac
      shift
   done
   if [[ $InstallToEspOnMac == 1 && $TargetDir == '/EFI/BOOT' ]] ; then
      echo "You may use --esp OR --usedefault, but not both! Aborting!"
      exit 1
   fi
#   exit 1
} # GetParams()

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
   mkdir -p $InstallDir/$TargetDir &> /dev/null
   if [[ $TargetDir == '/EFI/BOOT' ]] ; then
      cp $RefindDir/refind_ia32.efi $InstallDir/$TargetDir/bootia32.efi 2> /dev/null
      if [[ $? != 0 ]] ; then
         echo "Note: IA32 (x86) binary not installed!"
      fi
      cp $RefindDir/refind_x64.efi $InstallDir/$TargetDir/bootx64.efi 2> /dev/null
      if [[ $? != 0 ]] ; then
         Problems=1
      fi
      if [[ $InstallDrivers == 1 ]] ; then
         cp -r $RefindDir/drivers_* $InstallDir/$TargetDir/
      fi
      Refind=""
      cp $ThisDir/refind.cer $InstallDir/$TargetDir
      cp $ThisDir/refind.crt $InstallDir/$TargetDir
   elif [[ $Platform == 'EFI32' ]] ; then
      cp $RefindDir/refind_ia32.efi $InstallDir/$TargetDir
      if [[ $? != 0 ]] ; then
         Problems=1
      fi
      if [[ $InstallDrivers == 1 ]] ; then
         mkdir -p $InstallDir/$TargetDir/drivers_ia32
         cp -r $RefindDir/drivers_ia32/*_ia32.efi $InstallDir/$TargetDir/drivers_ia32/
      fi
      Refind="refind_ia32.efi"
   elif [[ $Platform == 'EFI64' ]] ; then
      cp $RefindDir/refind_x64.efi $InstallDir/$TargetDir
      if [[ $? != 0 ]] ; then
         Problems=1
      fi
      if [[ $InstallDrivers == 1 ]] ; then
         mkdir -p $InstallDir/$TargetDir/drivers_x64
         cp -r $RefindDir/drivers_x64/*_x64.efi $InstallDir/$TargetDir/drivers_x64/
      fi
      Refind="refind_x64.efi"
      cp $ThisDir/refind.cer $InstallDir/$TargetDir
      cp $ThisDir/refind.crt $InstallDir/$TargetDir
      if [[ $SBModeInstall == 1 ]] ; then
         echo "Storing copies of rEFInd Secure Boot public keys in $EtcKeysDir"
         mkdir -p $EtcKeysDir
         cp $ThisDir/refind.cer $EtcKeysDir
         cp $ThisDir/refind.crt $EtcKeysDir
      fi
   else
      echo "Unknown platform! Aborting!"
      exit 1
   fi
   echo "Copied rEFInd binary file $Refind"
   echo ""
   if [[ -d $InstallDir/$TargetDir/icons ]] ; then
      rm -rf $InstallDir/$TargetDir/icons-backup &> /dev/null
      mv -f $InstallDir/$TargetDir/icons $InstallDir/$TargetDir/icons-backup
      echo "Notice: Backed up existing icons directory as icons-backup."
   fi
   cp -r $IconsDir $InstallDir/$TargetDir
   if [[ $? != 0 ]] ; then
      Problems=1
   fi
   if [[ -f $InstallDir/$TargetDir/refind.conf ]] ; then
      echo "Existing refind.conf file found; copying sample file as refind.conf-sample"
      echo "to avoid collision."
      echo ""
      cp -f $ConfFile $InstallDir/$TargetDir
      if [[ $? != 0 ]] ; then
         Problems=1
      fi
   else
      echo "Copying sample configuration file as refind.conf; edit this file to configure"
      echo "rEFInd."
      echo ""
      cp -f $ConfFile $InstallDir/$TargetDir/refind.conf
      if [[ $? != 0 ]] ; then
         Problems=1
      fi
   fi
} # CopyRefindFiles()

# Mount the partition the user specified with the --usedefault option
MountDefaultTarget() {
   InstallDir=/tmp/refind_install
   mkdir -p $InstallDir
   if [[ $OSName == 'Darwin' ]] ; then
      mount -t msdos $TargetPart $InstallDir
   elif [[ $OSName == 'Linux' ]] ; then
      mount -t vfat $TargetPart $InstallDir
   fi
   if [[ $? != 0 ]] ; then
      echo "Couldn't mount $TargetPart ! Aborting!"
      rmdir $InstallDir
      exit 1
   fi
   UnmountEsp=1
} # MountDefaultTarget()

#
# A series of OS X support functions....
#

# Mount the ESP at /Volumes/ESP or determine its current mount
# point.
# Sets InstallDir to the ESP mount point
# Sets UnmountEsp if we mounted it
MountOSXESP() {
   # Identify the ESP. Note: This returns the FIRST ESP found;
   # if the system has multiple disks, this could be wrong!
   Temp=`diskutil list | grep " EFI "`
   Esp=/dev/`echo $Temp | cut -f 5 -d ' '`
   # If the ESP is mounted, use its current mount point....
   Temp=`df | grep $Esp`
   InstallDir=`echo $Temp | cut -f 6 -d ' '`
   if [[ $InstallDir == '' ]] ; then
      mkdir /Volumes/ESP &> /dev/null
      mount -t msdos $Esp /Volumes/ESP
      if [[ $? != 0 ]] ; then
         echo "Unable to mount ESP! Aborting!\n"
         exit 1
      fi
      UnmountEsp=1
      InstallDir="/Volumes/ESP"
   fi
} # MountOSXESP()

# Control the OS X installation.
# Sets Problems=1 if problems found during the installation.
InstallOnOSX() {
   echo "Installing rEFInd on OS X...."
   if [[ $TargetDir == "/EFI/BOOT" ]] ; then
      MountDefaultTarget
   elif [[ $InstallToEspOnMac == "1" ]] ; then
      MountOSXESP
   else
      InstallDir="/"
   fi
   echo "Installing rEFInd to the partition mounted at '$InstallDir'"
   Platform=`ioreg -l -p IODeviceTree | grep firmware-abi | cut -d "\"" -f 4`
   CopyRefindFiles
   if [[ $InstallToEspOnMac == "1" ]] ; then
      bless --mount $InstallDir --setBoot --file $InstallDir/$TargetDir/$Refind
   elif [[ $TargetDir != "/EFI/BOOT" ]] ; then
      bless --setBoot --folder $InstallDir/$TargetDir --file $InstallDir/$TargetDir/$Refind
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
} # InstallOnOSX()


#
# Now a series of Linux support functions....
#

# Check for evidence that we're running in Secure Boot mode. If so, warn the
# user and confirm installation.
# TODO: Perform a reasonable Secure Boot installation.
CheckSecureBoot() {
   VarFile=`ls -ld /sys/firmware/efi/vars/SecureBoot* 2> /dev/null`
   if [[ -n $VarFile  && $TargetDir != '/EFI/BOOT' ]] ; then
      echo ""
      echo "CAUTION: The computer seems to have been booted with Secure Boot active."
      echo "Although rEFInd, when installed in conjunction with the shim boot loader, can"
      echo "work on a Secure Boot computer, this installation script doesn't yet support"
      echo "direct installation in a way that will work on such a computer. You may"
      echo "proceed with installation with this script, but if you intend to boot with"
      echo "Secure Boot active, you must then reconfigure your boot programs to add shim"
      echo "to the process. Alternatively, you may terminate this script and do a manual"
      echo "installation, as described at http://www.rodsbooks.com/refind/secureboot.html."
      echo ""
      echo -n "Do you want to proceed with installation (Y/N)? "
      read ContYN
      if [[ $ContYN == "Y" || $ContYN == "y" ]] ; then
         echo "OK; continuing with the installation..."
      else
         exit 0
      SBModeInstall=1
      fi
   fi
}

# Identifies the ESP's location (/boot or /boot/efi); aborts if
# the ESP isn't mounted at either location.
# Sets InstallDir to the ESP mount point.
FindLinuxESP() {
   EspLine=`df /boot/efi | grep boot`
   InstallDir=`echo $EspLine | cut -d " " -f 6`
   EspFilesystem=`grep $InstallDir /etc/mtab | cut -d " " -f 3`
   if [[ $EspFilesystem != 'vfat' ]] ; then
      echo "/boot/efi doesn't seem to be on a VFAT filesystem. The ESP must be mounted at"
      echo "/boot or /boot/efi and it must be VFAT! Aborting!"
      exit 1
   fi
   echo "ESP was found at $InstallDir using $EspFilesystem"
} # MountLinuxESP

# Uses efibootmgr to add an entry for rEFInd to the EFI's NVRAM.
# If this fails, sets Problems=1
AddBootEntry() {
   InstallIt="0"
   Efibootmgr=`which efibootmgr 2> /dev/null`
   if [[ $Efibootmgr ]] ; then
      modprobe efivars &> /dev/null
      InstallDisk=`grep $InstallDir /etc/mtab | cut -d " " -f 1 | cut -c 1-8`
      PartNum=`grep $InstallDir /etc/mtab | cut -d " " -f 1 | cut -c 9-10`
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
   if [[ $TargetDir == "/EFI/BOOT" ]] ; then
      MountDefaultTarget
   else
      FindLinuxESP
   fi
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
   CheckSecureBoot
   CopyRefindFiles
   if [[ $TargetDir != "/EFI/BOOT" ]] ; then
      AddBootEntry
   fi
} # InstallOnLinux()

#
# The main part of the script. Sets a few environment variables,
# performs a few startup checks, and then calls functions to
# install under OS X or Linux, depending on the detected platform.
#

GetParams $@
OSName=`uname -s`
ThisDir="$( cd -P "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
RefindDir="$ThisDir/refind"
ThisScript="$ThisDir/`basename $0`"
CheckForFiles
if [[ `whoami` != "root" ]] ; then
   echo "Not running as root; attempting to elevate privileges via sudo...."
   sudo $ThisScript $1 $2 $3
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
   echo "Unmounting install dir"
   umount $InstallDir
fi

if [[ $InstallDir == /tmp/refind_install ]] ; then
#   sleep 5
   rmdir $InstallDir
fi

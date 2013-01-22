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
#           (/EFI/BOOT/BOOTX64.EFI and similar) to the specified device
#           (/dev/sdd1 or whatever) without registering with the NVRAM.
#    "--alldrivers" to install all drivers along with regular files
#    "--nodrivers" to suppress driver installation (default in Linux is
#           driver used on /boot; --nodrivers is OS X default)
#    "--shim {shimfile}" to install a shim.efi file for Secure Boot
#    "--localkeys" to re-sign x86-64 binaries with a locally-generated key
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
# 0.6.6   -- Bug fix: Upgrade drivers when installed to EFI/BOOT.
# 0.6.4   -- Copies ext2 driver rather than ext4 driver for ext2/3fs
# 0.6.3   -- Support for detecting rEFInd in EFI/BOOT and EFI/Microsoft/Boot
#            directories & for installing to EFI/BOOT in BIOS mode
# 0.6.2-1 -- Added --yes option & tweaked key-copying for use with RPM install script
# 0.6.1   -- Added --root option; minor bug fixes
# 0.6.0   -- Changed --drivers to --alldrivers and added --nodrivers option;
#            changed default driver installation behavior in Linux to install
#            the driver needed to read /boot (if available)
# 0.5.1.2 -- Fixed bug that caused failure to generate refind_linux.conf file
# 0.5.1.1 -- Fixed bug that caused script failure under OS X
# 0.5.1   -- Added --shim & --localkeys options & create sample refind_linux.conf
#            in /boot
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

RootDir="/"
TargetDir=/EFI/refind
LocalKeysBase="refind_local"
ShimSource="none"
TargetShim="default"
TargetX64="refind_x64.efi"
TargetIA32="refind_ia32.efi"
LocalKeys=0
DeleteRefindDir=0
AlwaysYes=0

#
# Functions used by both OS X and Linux....
#

GetParams() {
   InstallToEspOnMac=0
   if [[ $OSName == "Linux" ]] ; then
      # Install the driver required to read /boot, if it's available
      InstallDrivers="boot"
   else
      InstallDrivers="none"
   fi
   while [[ $# -gt 0 ]]; do
      case $1 in
         --esp | --ESP) InstallToEspOnMac=1
              ;;
         --usedefault) TargetDir=/EFI/BOOT
              TargetPart=$2
              TargetX64="bootx64.efi"
              TargetIA32="bootia32.efi"
              shift
              ;;
         --root) RootDir=$2
              shift
              ;;
         --localkeys) LocalKeys=1
              ;;
         --shim) ShimSource=$2
              shift
              ;;
         --drivers | --alldrivers) InstallDrivers="all"
              ;;
         --nodrivers) InstallDrivers="none"
              ;;
         --yes) AlwaysYes=1
              ;;
         * ) echo "Usage: $0 [--esp | --usedefault {device-file} | --root {directory} ]"
             echo "                  [--nodrivers | --alldrivers] [--shim {shim-filename}]"
             echo "                  [--localkeys] [--yes]"
             exit 1
      esac
      shift
   done

   if [[ $InstallToEspOnMac == 1 && $TargetDir == '/EFI/BOOT' ]] ; then
      echo "You may use --esp OR --usedefault, but not both! Aborting!"
      exit 1
   fi
   if [[ $RootDir != '/' && $TargetDir == '/EFI/BOOT' ]] ; then
      echo "You may use --usedefault OR --root, but not both! Aborting!"
      exit 1
   fi
   if [[ $RootDir != '/' && $InstallToEspOnMac == 1 ]] ; then
      echo "You may use --root OR --esp, but not both! Aborting!"
      exit 1
   fi

   RLConfFile="$RootDir/boot/refind_linux.conf"
   EtcKeysDir="$RootDir/etc/refind.d/keys"
} # GetParams()

# Get a yes/no response from the user and place it in the YesNo variable.
# If the AlwaysYes variable is set to 1, skip the user input and set "Y"
# in the YesNo variable.
ReadYesNo() {
   if [[ $AlwaysYes == 1 ]] ; then
      YesNo="Y"
      echo "Y"
   else
      read YesNo
   fi
}

# Abort if the rEFInd files can't be found.
# Also sets $ConfFile to point to the configuration file,
# $IconsDir to point to the icons directory, and
# $ShimSource to the source of the shim.efi file (if necessary).
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
      exit 1
   fi

   if [[ $ShimSource != "none" ]] ; then
      if [[ -f $ShimSource ]] ; then
         TargetX64="grubx64.efi"
         MokManagerSource=`dirname $ShimSource`/MokManager.efi
      else
         echo "The specified shim file, $ShimSource, doesn't exist!"
         echo "Aborting installation!"
         exit 1
      fi
   fi
} # CheckForFiles()

# Helper for CopyRefindFiles; copies shim files (including MokManager, if it's
# available) to target.
CopyShimFiles() {
   cp $ShimSource $InstallDir/$TargetDir/$TargetShim
   if [[ $? != 0 ]] ; then
      Problems=1
   fi
   if [[ -f $MokManagerSource ]] ; then
      cp $MokManagerSource $InstallDir/$TargetDir/
   fi
   if [[ $? != 0 ]] ; then
      Problems=1
   fi
} # CopyShimFiles()

# Copy the public keys to the installation medium
CopyKeys() {
   if [[ $LocalKeys == 1 ]] ; then
      mkdir -p $InstallDir/$TargetDir/keys/
      cp $EtcKeysDir/$LocalKeysBase.cer $InstallDir/$TargetDir/keys/
      cp $EtcKeysDir/$LocalKeysBase.crt $InstallDir/$TargetDir/keys/
#    else
#       cp $ThisDir/refind.cer $InstallDir/$TargetDir/keys/
#       cp $ThisDir/refind.crt $InstallDir/$TargetDir/keys/
   fi
} # CopyKeys()

# Copy drivers from $RefindDir/drivers_$1 to $InstallDir/$TargetDir/drivers_$1,
# honoring the $InstallDrivers condition. Must be passed a suitable
# architecture code (ia32 or x64).
CopyDrivers() {
   if [[ $InstallDrivers == "all" ]] ; then
      mkdir -p $InstallDir/$TargetDir/drivers_$1
      cp $RefindDir/drivers_$1/*_$1.efi $InstallDir/$TargetDir/drivers_$1/ 2> /dev/null
      cp $ThisDir/drivers_$1/*_$1.efi $InstallDir/$TargetDir/drivers_$1/ 2> /dev/null
   elif [[ $InstallDrivers == "boot" && -x `which blkid` ]] ; then
      BootPart=`df /boot | grep dev | cut -f 1 -d " "`
      BootFS=`blkid -o export $BootPart 2> /dev/null | grep TYPE= | cut -f 2 -d =`
      DriverType=""
      case $BootFS in
         ext2 | ext3) DriverType="ext2"
              # Could use ext4, but that can create unwanted entries from symbolic
              # links in / to /boot/vmlinuz if a separate /boot partition is used.
              ;;
         ext4) DriverType="ext4"
              ;;
         reiserfs) DriverType="reiserfs"
              ;;
         hfsplus) DriverType="hfs"
              ;;
         *) BootFS=""
      esac
      if [[ -n $BootFS ]] ; then
         echo "Installing driver for $BootFS (${DriverType}_$1.efi)"
         mkdir -p $InstallDir/$TargetDir/drivers_$1
         cp $RefindDir/drivers_$1/${DriverType}_$1.efi $InstallDir/$TargetDir/drivers_$1/ 2> /dev/null
         cp $ThisDir/drivers_$1/${DriverType}_$1.efi $InstallDir/$TargetDir/drivers_$1/ 2> /dev/null
      fi
   fi
}

# Copy the rEFInd files to the ESP or OS X root partition.
# Sets Problems=1 if any critical commands fail.
CopyRefindFiles() {
   mkdir -p $InstallDir/$TargetDir
   if [[ $TargetDir == '/EFI/BOOT' ]] ; then
      cp $RefindDir/refind_ia32.efi $InstallDir/$TargetDir/$TargetIA32 2> /dev/null
      if [[ $? != 0 ]] ; then
         echo "Note: IA32 (x86) binary not installed!"
      fi
      cp $RefindDir/refind_x64.efi $InstallDir/$TargetDir/$TargetX64 2> /dev/null
      if [[ $? != 0 ]] ; then
         Problems=1
      fi
      if [[ $ShimSource != "none" ]] ; then
         TargetShim="bootx64.efi"
         CopyShimFiles
      fi
      if [[ $InstallDrivers == "all" ]] ; then
         cp -r $RefindDir/drivers_* $InstallDir/$TargetDir/ 2> /dev/null
         cp -r $ThisDir/drivers_* $InstallDir/$TargetDir/ 2> /dev/null
      elif [[ $Upgrade == 1 ]] ; then
         if [[ $Platform == 'EFI64' ]] ; then
            CopyDrivers x64
         else
            CopyDrivers ia32
         fi
      fi
      Refind=""
      CopyKeys
   elif [[ $Platform == 'EFI64' || $TargetDir == "/EFI/Microsoft/Boot" ]] ; then
      cp $RefindDir/refind_x64.efi $InstallDir/$TargetDir/$TargetX64
      if [[ $? != 0 ]] ; then
         Problems=1
      fi
      CopyDrivers x64
      Refind="refind_x64.efi"
      CopyKeys
      if [[ $ShimSource != "none" ]] ; then
         if [[ $TargetShim == "default" ]] ; then
            TargetShim=`basename $ShimSource`
         fi
         CopyShimFiles
         Refind=$TargetShim
         if [[ $LocalKeys == 0 ]] ; then
            echo "Storing copies of rEFInd Secure Boot public keys in $EtcKeysDir"
            mkdir -p $EtcKeysDir
            cp $ThisDir/keys/refind.cer $EtcKeysDir 2> /dev/null
            cp $ThisDir/keys/refind.crt $EtcKeysDir 2> /dev/null
         fi
      fi
   elif [[ $Platform == 'EFI32' ]] ; then
      cp $RefindDir/refind_ia32.efi $InstallDir/$TargetDir/$TargetIA32
      if [[ $? != 0 ]] ; then
         Problems=1
      fi
      CopyDrivers ia32
      Refind="refind_ia32.efi"
   else
      echo "Unknown platform! Aborting!"
      exit 1
   fi
   echo "Copied rEFInd binary files"
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
   mkdir -p $InstallDir/$TargetDir/keys
   cp -rf $ThisDir/keys/*.[cd]er $InstallDir/$TargetDir/keys/ 2> /dev/null
   cp -rf $EtcKeysDir/*.[cd]er $InstallDir/$TargetDir/keys/ 2> /dev/null
   if [[ -f $InstallDir/$TargetDir/refind.conf ]] ; then
      echo "Existing refind.conf file found; copying sample file as refind.conf-sample"
      echo "to avoid overwriting your customizations."
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
   if [[ $DeleteRefindDir == 1 ]] ; then
      echo "Deleting the temporary directory $RefindDir"
      rm -r $RefindDir
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
      InstallDir="$RootDir/"
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
      ReadYesNo
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

# Check for evidence that we're running in Secure Boot mode. If so, and if
# appropriate options haven't been set, warn the user and offer to abort.
# If we're NOT in Secure Boot mode but the user HAS specified the --shim
# or --localkeys option, warn the user and offer to abort.
#
# FIXME: Although I checked the presence (and lack thereof) of the
# /sys/firmware/efi/vars/SecureBoot* files on my Secure Boot test system
# before releasing this script, I've since found that they are at least
# sometimes present when Secure Boot is absent. This means that the first
# test can produce false alarms. A better test is highly desirable.
CheckSecureBoot() {
   VarFile=`ls -d /sys/firmware/efi/vars/SecureBoot* 2> /dev/null`
   if [[ -n $VarFile  && $TargetDir != '/EFI/BOOT' && $ShimSource == "none" ]] ; then
      echo ""
      echo "CAUTION: Your computer appears to support Secure Boot, but you haven't"
      echo "specified a valid shim.efi file source. If you've disabled Secure Boot and"
      echo "intend to leave it disabled, this is fine; but if Secure Boot is active, the"
      echo "resulting installation won't boot. You can read more about this topic at"
      echo "http://www.rodsbooks.com/refind/secureboot.html."
      echo ""
      echo -n "Do you want to proceed with installation (Y/N)? "
      ReadYesNo
      if [[ $YesNo == "Y" || $YesNo == "y" ]] ; then
         echo "OK; continuing with the installation..."
      else
         exit 0
      fi
   fi

   if [[ $ShimSource != "none" && ! -n $VarFile ]] ; then
      echo ""
      echo "You've specified installing using a shim.efi file, but your computer does not"
      echo "appear to be running in Secure Boot mode. Although installing in this way"
      echo "should work, it's unnecessarily complex. You may continue, but unless you"
      echo "plan to enable Secure Boot, you should consider stopping and omitting the"
      echo "--shim option. You can read more about this topic at"
      echo "http://www.rodsbooks.com/refind/secureboot.html."
      echo ""
      echo -n "Do you want to proceed with installation (Y/N)? "
      ReadYesNo
      if [[ $YesNo == "Y" || $YesNo == "y" ]] ; then
         echo "OK; continuing with the installation..."
      else
         exit 0
      fi
   fi

   if [[ $LocalKeys != 0 && ! -n $VarFile ]] ; then
      echo ""
      echo "You've specified re-signing your rEFInd binaries with locally-generated keys,"
      echo "but your computer does not appear to be running in Secure Boot mode. The"
      echo "keys you generate will be useless unless you enable Secure Boot. You may"
      echo "proceed with this installation, but before you do so, you may want to read"
      echo "more about it at http://www.rodsbooks.com/refind/secureboot.html."
      echo ""
      echo -n "Do you want to proceed with installation (Y/N)? "
      ReadYesNo
      if [[ $YesNo == "Y" || $YesNo == "y" ]] ; then
         echo "OK; continuing with the installation..."
      else
         exit 0
      fi
   fi

} # CheckSecureBoot()

# Check for the presence of locally-generated keys from a previous installation in
# $EtcKeysDir (/etc/refind.d/keys). If they're not present, generate them using
# openssl.
GenerateKeys() {
   PrivateKey=$EtcKeysDir/$LocalKeysBase.key
   CertKey=$EtcKeysDir/$LocalKeysBase.crt
   DerKey=$EtcKeysDir/$LocalKeysBase.cer
   OpenSSL=`which openssl 2> /dev/null`

   # Do the work only if one or more of the necessary keys is missing
   # TODO: Technically, we don't need the DerKey; but if it's missing and openssl
   # is also missing, this will fail. This could be improved.
   if [[ ! -f $PrivateKey || ! -f $CertKey || ! -f $DerKey ]] ; then
      echo "Generating a fresh set of local keys...."
      mkdir -p $EtcKeysDir
      chmod 0700 $EtcKeysDir
      if [[ ! -x $OpenSSL ]] ; then
         echo "Can't find openssl, which is required to create your private signing keys!"
         echo "Aborting!"
         exit 1
      fi
      if [[ -f $PrivateKey ]] ; then
         echo "Backing up existing $PrivateKey"
         cp -f $PrivateKey $PrivateKey.backup 2> /dev/null
      fi
      if [[ -f $CertKey ]] ; then
         echo "Backing up existing $CertKey"
         cp -f $CertKey $CertKey.backup 2> /dev/null
      fi
      if [[ -f $DerKey ]] ; then
         echo "Backing up existing $DerKey"
         cp -f $DerKey $DerKey.backup 2> /dev/null
      fi
      $OpenSSL req -new -x509 -newkey rsa:2048 -keyout $PrivateKey -out $CertKey \
                   -nodes -days 3650 -subj "/CN=Locally-generated rEFInd key/"
      $OpenSSL x509 -in $CertKey -out $DerKey -outform DER
      chmod 0600 $PrivateKey
   else
      echo "Using existing local keys...."
   fi
}

# Sign a single binary. Requires parameters:
#   $1 = source file
#   $2 = destination file
# Also assumes that the SBSign, PESign, UseSBSign, UsePESign, and various key variables are set
# appropriately.
# Aborts script on error
SignOneBinary() {
   $SBSign --key $PrivateKey --cert $CertKey --output $2 $1
   if [[ $? != 0 ]] ; then
      echo "Problem signing the binary $1! Aborting!"
      exit 1
   fi
}

# Re-sign the x86-64 binaries with a locally-generated key, First look for appropriate
# key files in $EtcKeysDir. If they're present, use them to re-sign the binaries. If
# not, try to generate new keys and store them in $EtcKeysDir.
ReSignBinaries() {
   SBSign=`which sbsign 2> /dev/null`
   echo "Found sbsign at $SBSign"
   TempDir="/tmp/refind_local"
   if [[ ! -x $SBSign ]] ; then
      echo "Can't find sbsign, which is required to sign rEFInd with your own keys!"
      echo "Aborting!"
      exit 1
   fi
   GenerateKeys
   mkdir -p $TempDir/drivers_x64
   cp $RefindDir/refind.conf-sample $TempDir 2> /dev/null
   cp $ThisDir/refind.conf-sample $TempDir 2> /dev/null
   cp $RefindDir/refind_ia32.efi $TempDir 2> /dev/null
   cp -a $RefindDir/drivers_ia32 $TempDir 2> /dev/null
   cp -a $ThisDir/drivers_ia32 $TempDir 2> /dev/null
   SignOneBinary $RefindDir/refind_x64.efi $TempDir/refind_x64.efi
   for Driver in `ls $RefindDir/drivers_x64/*.efi $ThisDir/drivers_x64/*.efi 2> /dev/null` ; do
      TempName=`basename $Driver`
      SignOneBinary $Driver $TempDir/drivers_x64/$TempName
   done
   RefindDir=$TempDir
   DeleteRefindDir=1
}

# Identifies the ESP's location (/boot or /boot/efi, or these locations under
# the directory specified by --root); aborts if the ESP isn't mounted at
# either location.
# Sets InstallDir to the ESP mount point.
FindLinuxESP() {
   EspLine=`df $RootDir/boot/efi 2> /dev/null | grep boot/efi`
   if [[ ! -n $EspLine ]] ; then
      EspLine=`df $RootDir/boot | grep boot`
   fi
   InstallDir=`echo $EspLine | cut -d " " -f 6`
   if [[ -n $InstallDir ]] ; then
      EspFilesystem=`grep $InstallDir /etc/mtab | cut -d " " -f 3`
   fi
   if [[ $EspFilesystem != 'vfat' ]] ; then
      echo "$RootDir/boot/efi doesn't seem to be on a VFAT filesystem. The ESP must be"
      echo "mounted at $RootDir/boot or $RootDir/boot/efi and it must be VFAT! Aborting!"
      exit 1
   fi
   echo "ESP was found at $InstallDir using $EspFilesystem"
} # FindLinuxESP

# Uses efibootmgr to add an entry for rEFInd to the EFI's NVRAM.
# If this fails, sets Problems=1
AddBootEntry() {
   InstallIt="0"
   Efibootmgr=`which efibootmgr 2> /dev/null`
   if [[ $Efibootmgr ]] ; then
      InstallDisk=`grep $InstallDir /etc/mtab | cut -d " " -f 1 | cut -c 1-8`
      PartNum=`grep $InstallDir /etc/mtab | cut -d " " -f 1 | cut -c 9-10`
      EntryFilename=$TargetDir/$Refind
      EfiEntryFilename=`echo ${EntryFilename//\//\\\}`
      EfiEntryFilename2=`echo ${EfiEntryFilename} | sed s/\\\\\\\\/\\\\\\\\\\\\\\\\/g`
      ExistingEntry=`$Efibootmgr -v | grep -i $EfiEntryFilename2`

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
         $Efibootmgr -c -l $EfiEntryFilename -L "rEFInd Boot Manager" -d $InstallDisk -p $PartNum &> /dev/null
         if [[ $? != 0 ]] ; then
            EfibootmgrProblems=1
            Problems=1
         fi
      fi

   else # efibootmgr not found
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

# Create a minimal/sample refind_linux.conf file in /boot.
GenerateRefindLinuxConf() {
   if [[ -f $RLConfFile ]] ; then
      echo "Existing $RLConfFile found; not overwriting."
   else
      if [[ -f "$RootDir/etc/default/grub" ]] ; then
         # We want the default options used by the distribution, stored here....
         source "$RootDir/etc/default/grub"
      fi
      RootFS=`df $RootDir | grep dev | cut -f 1 -d " "`
      StartOfDevname=`echo $RootFS | cut -b 1-7`
      if [[ $StartOfDevname == "/dev/sd" || $StartOfDevName == "/dev/hd" ]] ; then
         # Identify root filesystem by UUID rather than by device node, if possible
         Uuid=`blkid -o export $RootFS 2> /dev/null | grep UUID=`
         if [[ -n $Uuid ]] ; then
            RootFS=$Uuid
         fi
      fi
      DefaultOptions="$GRUB_CMDLINE_LINUX $GRUB_CMDLINE_LINUX_DEFAULT"
      echo "\"Boot with standard options\" \"ro root=$RootFS $DefaultOptions \"" > $RLConfFile
      echo "\"Boot to single-user mode\"   \"ro root=$RootFS $DefaultOptions single\"" >> $RLConfFile
      echo "\"Boot with minimal options\"  \"ro root=$RootFS\"" >> $RLConfFile
   fi
}

# Set varaibles for installation in EFI/BOOT directory
SetVarsForBoot() {
   TargetDir="/EFI/BOOT"
   if [[ $ShimSource == "none" ]] ; then
      TargetX64="bootx64.efi"
      TargetIA32="bootia32.efi"
   else
      TargetX64="grubx64.efi"
      TargetIA32="bootia32.efi"
      TargetShim="bootx64.efi"
   fi
} # SetFilenamesForBoot()

# Set variables for installation in EFI/Microsoft/Boot directory
SetVarsForMsBoot() {
   TargetDir="/EFI/Microsoft/Boot"
   if [[ $ShimSource == "none" ]] ; then
      TargetX64="bootmgfw.efi"
   else
      TargetX64="grubx64.efi"
      TargetShim="bootmgfw.efi"
   fi
}

# TargetDir defaults to /EFI/refind; however, this function adjusts it as follows:
# - If an existing refind.conf is available in /EFI/BOOT or /EFI/Microsoft/Boot,
#   install to that directory under the suitable name; but DO NOT do this if
#   refind.conf is also in /EFI/refind.
# - If booted in BIOS mode and the ESP lacks any other EFI files, install to
#   /EFI/BOOT
# - If booted in BIOS mode and there's no refind.conf file and there is a
#   /EFI/Microsoft/Boot/bootmgfw.efi file, move it down one level and
#   install under that name, "hijacking" the Windows boot loader filename
DetermineTargetDir() {
   Upgrade=0

   if [[ -f $InstallDir/EFI/BOOT/refind.conf ]] ; then
      SetVarsForBoot
      Upgrade=1
   fi
   if [[ -f $InstallDir/EFI/Microsoft/Boot/refind.conf ]] ; then
      SetVarsForMsBoot
      Upgrade=1
   fi
   if [[ -f $InstallDir/EFI/refind/refind.conf ]] ; then
      TargetDir="/EFI/refind"
      Upgrade=1
   fi
   if [[ $Upgrade == 1 ]] ; then
      echo "Found rEFInd installation in $InstallDir$TargetDir; upgrading it."
   fi

   if [[ ! -d /sys/firmware/efi && $Upgrade == 0 ]] ; then     # BIOS-mode
      FoundEfiFiles=`find $InstallDir/EFI/BOOT -name "*.efi" 2> /dev/null`
      FoundConfFiles=`find $InstallDir -name "refind\.conf" 2> /dev/null`
      if [[ ! -n $FoundConfFiles && -f $InstallDir/EFI/Microsoft/Boot/bootmgfw.efi ]] ; then
         mv -n $InstallDir/EFI/Microsoft/Boot/bootmgfw.efi $InstallDir/EFI/Microsoft &> /dev/null
         SetVarsForMsBoot
         echo "Running in BIOS mode with a suspected Windows installation; moving boot loader"
         echo "files so as to install to $InstallDir$TargetDir."
      elif [[ ! -n $FoundEfiFiles ]] ; then  # In BIOS mode and no default loader; install as default loader
         SetVarsForBoot
         echo "Running in BIOS mode with no existing default boot loader; installing to"
         echo $InstallDir$TargetDir
      else
         echo "Running in BIOS mode with an existing default boot loader; backing it up and"
         echo "installing rEFInd in its place."
         if [[ -d $InstallDir/EFI/BOOT-rEFIndBackup ]] ; then
            echo ""
            echo "Caution: An existing backup of a default boot loader exists! If the current"
            echo "default boot loader and the backup are different boot loaders, the current"
            echo "one will become inaccessible."
            echo ""
            echo -n "Do you want to proceed with installation (Y/N)? "
            ReadYesNo
            if [[ $YesNo == "Y" || $YesNo == "y" ]] ; then
               echo "OK; continuing with the installation..."
            else
               exit 0
            fi
         fi
         mv -n $InstallDir/EFI/BOOT $InstallDir/EFI/BOOT-rEFIndBackup
         SetVarsForBoot
      fi
   fi # BIOS-mode
} # DetermineTargetDir()

# Controls rEFInd installation under Linux.
# Sets Problems=1 if something goes wrong.
InstallOnLinux() {
   echo "Installing rEFInd on Linux...."
   modprobe efivars &> /dev/null
   if [[ $TargetDir == "/EFI/BOOT" ]] ; then
      MountDefaultTarget
   else
      FindLinuxESP
      DetermineTargetDir
   fi
   CpuType=`uname -m`
   if [[ $CpuType == 'x86_64' ]] ; then
      Platform="EFI64"
   elif [[ ($CpuType == 'i386' || $CpuType == 'i486' || $CpuType == 'i586' || $CpuType == 'i686') ]] ; then
      Platform="EFI32"
      # If we're in EFI mode, do some sanity checks, and alert the user or even
      # abort. Not in BIOS mode, though, since that could be used on an emergency
      # disc to try to recover a troubled Linux installation.
      if [[ -d /sys/firmware/efi ]] ; then
         if [[ $ShimSource != "none" && $TargetDir != "/BOOT/EFI" ]] ; then
            echo ""
            echo "CAUTION: Neither rEFInd nor shim currently supports 32-bit systems, so you"
            echo "should not use the --shim option to install on such systems. Aborting!"
            echo ""
            exit 1
         fi
         echo
         echo "CAUTION: This Linux installation uses a 32-bit kernel. 32-bit EFI-based"
         echo "computers are VERY RARE. If you've installed a 32-bit version of Linux"
         echo "on a 64-bit computer, you should manually install the 64-bit version of"
         echo "rEFInd. If you're installing on a Mac, you should do so from OS X. If"
         echo "you're positive you want to continue with this installation, answer 'Y'"
         echo "to the following question..."
         echo
         echo -n "Are you sure you want to continue (Y/N)? "
         ReadYesNo
         if [[ $YesNo == "Y" || $YesNo == "y" ]] ; then
            echo "OK; continuing with the installation..."
         else
            exit 0
         fi
      fi # in EFI mode
   else
      echo "Unknown CPU type '$CpuType'; aborting!"
      exit 1
   fi

   if [[ $LocalKeys == 1 ]] ; then
      ReSignBinaries
   fi

   CheckSecureBoot
   CopyRefindFiles
   if [[ $TargetDir != "/EFI/BOOT" && $TargetDir != "/EFI/Microsoft/Boot" ]] ; then
      AddBootEntry
      GenerateRefindLinuxConf
   fi
} # InstallOnLinux()

#
# The main part of the script. Sets a few environment variables,
# performs a few startup checks, and then calls functions to
# install under OS X or Linux, depending on the detected platform.
#

OSName=`uname -s`
GetParams $@
ThisDir="$( cd -P "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
RefindDir="$ThisDir/refind"
ThisScript="$ThisDir/`basename $0`"
if [[ `whoami` != "root" ]] ; then
   echo "Not running as root; attempting to elevate privileges via sudo...."
   sudo $ThisScript "$@"
   if [[ $? != 0 ]] ; then
      echo "This script must be run as root (or using sudo). Exiting!"
      exit 1
   else
      exit 0
   fi
fi
CheckForFiles
if [[ $OSName == 'Darwin' ]] ; then
   if [[ $ShimSource != "none" ]] ; then
      echo "The --shim option is not supported on OS X! Exiting!"
      exit 1
   fi
   if [[ $LocalKeys != 0 ]] ; then
      echo "The --localkeys option is not supported on OS X! Exiting!"
      exit 1
   fi
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

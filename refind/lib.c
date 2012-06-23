/*
 * refit/lib.c
 * General library functions
 *
 * Copyright (c) 2006-2009 Christoph Pfisterer
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the
 *    distribution.
 *
 *  * Neither the name of Christoph Pfisterer nor the names of the
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Modifications copyright (c) 2012 Roderick W. Smith
 * 
 * Modifications distributed under the terms of the GNU General Public
 * License (GPL) version 3 (GPLv3), a copy of which must be distributed
 * with this source code or binaries made from it.
 * 
 */

#include "global.h"
#include "lib.h"
#include "icns.h"
#include "screen.h"
#include "../include/refit_call_wrapper.h"
#include "../include/RemovableMedia.h"

#ifdef __MAKEWITH_GNUEFI
#define EfiReallocatePool ReallocatePool
#else
#define LibLocateHandle gBS->LocateHandleBuffer
#define DevicePathProtocol gEfiDevicePathProtocolGuid
#define BlockIoProtocol gEfiBlockIoProtocolGuid
#define LibFileSystemInfo EfiLibFileSystemInfo
#define LibOpenRoot EfiLibOpenRoot
EFI_DEVICE_PATH EndDevicePath[] = {
   {END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, {END_DEVICE_PATH_LENGTH, 0}}
};

//#define EndDevicePath DevicePath
#endif

// variables

EFI_HANDLE       SelfImageHandle;
EFI_LOADED_IMAGE *SelfLoadedImage;
EFI_FILE         *SelfRootDir;
EFI_FILE         *SelfDir;
CHAR16           *SelfDirPath;

REFIT_VOLUME     *SelfVolume = NULL;
REFIT_VOLUME     **Volumes = NULL;
UINTN            VolumesCount = 0;

// Maximum size for disk sectors
#define SECTOR_SIZE 4096

// Default names for volume badges (mini-icon to define disk type) and icons
#define VOLUME_BADGE_NAME L".VolumeBadge.icns"
#define VOLUME_ICON_NAME L".VolumeIcon.icns"

// functions

static EFI_STATUS FinishInitRefitLib(VOID);

static VOID UninitVolumes(VOID);

//
// self recognition stuff
//

// Converts forward slashes to backslashes, removes duplicate slashes, and
// removes slashes from both the start and end of the pathname.
// Necessary because some (buggy?) EFI implementations produce "\/" strings
// in pathnames, because some user inputs can produce duplicate directory
// separators, and because we want consistent start and end slashes for
// directory comparisons. A special case: If the PathName refers to root,
// return "/", since some firmware implementations flake out if this
// isn't present.
VOID CleanUpPathNameSlashes(IN OUT CHAR16 *PathName) {
   CHAR16   *NewName;
   UINTN    i, FinalChar = 0;
   BOOLEAN  LastWasSlash = FALSE;

   NewName = AllocateZeroPool(sizeof(CHAR16) * (StrLen(PathName) + 2));
   if (NewName != NULL) {
      for (i = 0; i < StrLen(PathName); i++) {
         if ((PathName[i] == L'/') || (PathName[i] == L'\\')) {
            if ((!LastWasSlash) && (FinalChar != 0))
               NewName[FinalChar++] = L'\\';
            LastWasSlash = TRUE;
         } else {
            NewName[FinalChar++] = PathName[i];
            LastWasSlash = FALSE;
         } // if/else
      } // for
      NewName[FinalChar] = 0;
      if ((FinalChar > 0) && (NewName[FinalChar - 1] == L'\\'))
         NewName[--FinalChar] = 0;
      if (FinalChar == 0) {
         NewName[0] = L'\\';
         NewName[1] = 0;
      }
      // Copy the transformed name back....
      StrCpy(PathName, NewName);
      FreePool(NewName);
   } // if allocation OK
} // CleanUpPathNameSlashes()

EFI_STATUS InitRefitLib(IN EFI_HANDLE ImageHandle)
{
    EFI_STATUS  Status;
    CHAR16      *DevicePathAsString;

    SelfImageHandle = ImageHandle;
    Status = refit_call3_wrapper(BS->HandleProtocol, SelfImageHandle, &LoadedImageProtocol, (VOID **) &SelfLoadedImage);
    if (CheckFatalError(Status, L"while getting a LoadedImageProtocol handle"))
        return EFI_LOAD_ERROR;

    // find the current directory
    DevicePathAsString = DevicePathToStr(SelfLoadedImage->FilePath);
    CleanUpPathNameSlashes(DevicePathAsString);
    if (SelfDirPath != NULL)
       FreePool(SelfDirPath);
    SelfDirPath = FindPath(DevicePathAsString);
    FreePool(DevicePathAsString);

    return FinishInitRefitLib();
}

// called before running external programs to close open file handles
VOID UninitRefitLib(VOID)
{
    UninitVolumes();

    if (SelfDir != NULL) {
        refit_call1_wrapper(SelfDir->Close, SelfDir);
        SelfDir = NULL;
    }

    if (SelfRootDir != NULL) {
       refit_call1_wrapper(SelfRootDir->Close, SelfRootDir);
       SelfRootDir = NULL;
    }
}

// called after running external programs to re-open file handles
EFI_STATUS ReinitRefitLib(VOID)
{
    ReinitVolumes();

    if ((ST->Hdr.Revision >> 16) == 1) {
       // Below two lines were in rEFIt, but seem to cause system crashes or
       // reboots when launching OSes after returning from programs on most
       // systems. OTOH, my Mac Mini produces errors about "(re)opening our
       // installation volume" (see the next function) when returning from
       // programs when these two lines are removed, and it often crashes
       // when returning from a program or when launching a second program
       // with these lines removed. Therefore, the preceding if() statement
       // executes these lines only on EFIs with a major version number of 1
       // (which Macs have) and not with 2 (which UEFI PCs have). My selection
       // of hardware on which to test is limited, though, so this may be the
       // wrong test, or there may be a better way to fix this problem.
       // TODO: Figure out cause of above weirdness and fix it more
       // reliably!
       if (SelfVolume != NULL && SelfVolume->RootDir != NULL)
          SelfRootDir = SelfVolume->RootDir;
    } // if

    return FinishInitRefitLib();
}

static EFI_STATUS FinishInitRefitLib(VOID)
{
    EFI_STATUS  Status;

    if (SelfRootDir == NULL) {
        SelfRootDir = LibOpenRoot(SelfLoadedImage->DeviceHandle);
        if (SelfRootDir == NULL) {
            CheckError(EFI_LOAD_ERROR, L"while (re)opening our installation volume");
            return EFI_LOAD_ERROR;
        }
    }

    Status = refit_call5_wrapper(SelfRootDir->Open, SelfRootDir, &SelfDir, SelfDirPath, EFI_FILE_MODE_READ, 0);
    if (CheckFatalError(Status, L"while opening our installation directory"))
        return EFI_LOAD_ERROR;

    return EFI_SUCCESS;
}

//
// list functions
//

VOID CreateList(OUT VOID ***ListPtr, OUT UINTN *ElementCount, IN UINTN InitialElementCount)
{
    UINTN AllocateCount;

    *ElementCount = InitialElementCount;
    if (*ElementCount > 0) {
        AllocateCount = (*ElementCount + 7) & ~7;   // next multiple of 8
        *ListPtr = AllocatePool(sizeof(VOID *) * AllocateCount);
    } else {
        *ListPtr = NULL;
    }
}

VOID AddListElement(IN OUT VOID ***ListPtr, IN OUT UINTN *ElementCount, IN VOID *NewElement)
{
    UINTN AllocateCount;

    if ((*ElementCount & 7) == 0) {
        AllocateCount = *ElementCount + 8;
        if (*ElementCount == 0)
            *ListPtr = AllocatePool(sizeof(VOID *) * AllocateCount);
        else
            *ListPtr = EfiReallocatePool(*ListPtr, sizeof(VOID *) * (*ElementCount), sizeof(VOID *) * AllocateCount);
    }
    (*ListPtr)[*ElementCount] = NewElement;
    (*ElementCount)++;
} /* VOID AddListElement() */

VOID FreeList(IN OUT VOID ***ListPtr, IN OUT UINTN *ElementCount)
{
    UINTN i;

    if (*ElementCount > 0) {
        for (i = 0; i < *ElementCount; i++) {
            // TODO: call a user-provided routine for each element here
            FreePool((*ListPtr)[i]);
        }
        FreePool(*ListPtr);
    }
}

//
// firmware device path discovery
//

static UINT8 LegacyLoaderMediaPathData[] = {
    0x04, 0x06, 0x14, 0x00, 0xEB, 0x85, 0x05, 0x2B,
    0xB8, 0xD8, 0xA9, 0x49, 0x8B, 0x8C, 0xE2, 0x1B,
    0x01, 0xAE, 0xF2, 0xB7, 0x7F, 0xFF, 0x04, 0x00,
};
static EFI_DEVICE_PATH *LegacyLoaderMediaPath = (EFI_DEVICE_PATH *)LegacyLoaderMediaPathData;

VOID ExtractLegacyLoaderPaths(EFI_DEVICE_PATH **PathList, UINTN MaxPaths, EFI_DEVICE_PATH **HardcodedPathList)
{
    EFI_STATUS          Status;
    UINTN               HandleCount = 0;
    UINTN               HandleIndex, HardcodedIndex;
    EFI_HANDLE          *Handles;
    EFI_HANDLE          Handle;
    UINTN               PathCount = 0;
    UINTN               PathIndex;
    EFI_LOADED_IMAGE    *LoadedImage;
    EFI_DEVICE_PATH     *DevicePath;
    BOOLEAN             Seen;

    MaxPaths--;  // leave space for the terminating NULL pointer

    // get all LoadedImage handles
    Status = LibLocateHandle(ByProtocol, &LoadedImageProtocol, NULL, &HandleCount, &Handles);
    if (CheckError(Status, L"while listing LoadedImage handles")) {
        if (HardcodedPathList) {
            for (HardcodedIndex = 0; HardcodedPathList[HardcodedIndex] && PathCount < MaxPaths; HardcodedIndex++)
                PathList[PathCount++] = HardcodedPathList[HardcodedIndex];
        }
        PathList[PathCount] = NULL;
        return;
    }
    for (HandleIndex = 0; HandleIndex < HandleCount && PathCount < MaxPaths; HandleIndex++) {
        Handle = Handles[HandleIndex];

        Status = refit_call3_wrapper(BS->HandleProtocol, Handle, &LoadedImageProtocol, (VOID **) &LoadedImage);
        if (EFI_ERROR(Status))
            continue;  // This can only happen if the firmware scewed up, ignore it.

        Status = refit_call3_wrapper(BS->HandleProtocol, LoadedImage->DeviceHandle, &DevicePathProtocol, (VOID **) &DevicePath);
        if (EFI_ERROR(Status))
            continue;  // This happens, ignore it.

        // Only grab memory range nodes
        if (DevicePathType(DevicePath) != HARDWARE_DEVICE_PATH || DevicePathSubType(DevicePath) != HW_MEMMAP_DP)
            continue;

        // Check if we have this device path in the list already
        // WARNING: This assumes the first node in the device path is unique!
        Seen = FALSE;
        for (PathIndex = 0; PathIndex < PathCount; PathIndex++) {
            if (DevicePathNodeLength(DevicePath) != DevicePathNodeLength(PathList[PathIndex]))
                continue;
            if (CompareMem(DevicePath, PathList[PathIndex], DevicePathNodeLength(DevicePath)) == 0) {
                Seen = TRUE;
                break;
            }
        }
        if (Seen)
            continue;

        PathList[PathCount++] = AppendDevicePath(DevicePath, LegacyLoaderMediaPath);
    }
    FreePool(Handles);

    if (HardcodedPathList) {
        for (HardcodedIndex = 0; HardcodedPathList[HardcodedIndex] && PathCount < MaxPaths; HardcodedIndex++)
            PathList[PathCount++] = HardcodedPathList[HardcodedIndex];
    }
    PathList[PathCount] = NULL;
}

//
// volume functions
//

static VOID ScanVolumeBootcode(IN OUT REFIT_VOLUME *Volume, OUT BOOLEAN *Bootable)
{
    EFI_STATUS              Status;
    UINT8                   SectorBuffer[SECTOR_SIZE];
    UINTN                   i;
    MBR_PARTITION_INFO      *MbrTable;
    BOOLEAN                 MbrTableFound;

    Volume->HasBootCode = FALSE;
    Volume->OSIconName = NULL;
    Volume->OSName = NULL;
    *Bootable = FALSE;

    if (Volume->BlockIO == NULL)
        return;
    if (Volume->BlockIO->Media->BlockSize > SECTOR_SIZE)
        return;   // our buffer is too small...

    // look at the boot sector (this is used for both hard disks and El Torito images!)
    Status = refit_call5_wrapper(Volume->BlockIO->ReadBlocks,
                                 Volume->BlockIO, Volume->BlockIO->Media->MediaId,
                                 Volume->BlockIOOffset, SECTOR_SIZE, SectorBuffer);
    if (!EFI_ERROR(Status)) {

        if (*((UINT16 *)(SectorBuffer + 510)) == 0xaa55 && SectorBuffer[0] != 0) {
            *Bootable = TRUE;
            Volume->HasBootCode = TRUE;
        }

        // detect specific boot codes
        if (CompareMem(SectorBuffer + 2, "LILO", 4) == 0 ||
            CompareMem(SectorBuffer + 6, "LILO", 4) == 0 ||
            CompareMem(SectorBuffer + 3, "SYSLINUX", 8) == 0 ||
            FindMem(SectorBuffer, SECTOR_SIZE, "ISOLINUX", 8) >= 0) {
            Volume->HasBootCode = TRUE;
            Volume->OSIconName = L"linux";
            Volume->OSName = L"Linux";

        } else if (FindMem(SectorBuffer, 512, "Geom\0Hard Disk\0Read\0 Error", 26) >= 0) {   // GRUB
            Volume->HasBootCode = TRUE;
            Volume->OSIconName = L"grub,linux";
            Volume->OSName = L"Linux";

//         // Below doesn't produce a bootable entry, so commented out for the moment....
//         // GRUB in BIOS boot partition:
//         } else if (FindMem(SectorBuffer, 512, "Geom\0Read\0 Error", 16) >= 0) {
//             Volume->HasBootCode = TRUE;
//             Volume->OSIconName = L"grub,linux";
//             Volume->OSName = L"Linux";
//             Volume->VolName = L"BIOS Boot Partition";
//             *Bootable = TRUE;

        } else if ((*((UINT32 *)(SectorBuffer + 502)) == 0 &&
                    *((UINT32 *)(SectorBuffer + 506)) == 50000 &&
                    *((UINT16 *)(SectorBuffer + 510)) == 0xaa55) ||
                    FindMem(SectorBuffer, SECTOR_SIZE, "Starting the BTX loader", 23) >= 0) {
            Volume->HasBootCode = TRUE;
            Volume->OSIconName = L"freebsd";
            Volume->OSName = L"FreeBSD";

        } else if (FindMem(SectorBuffer, 512, "!Loading", 8) >= 0 ||
                   FindMem(SectorBuffer, SECTOR_SIZE, "/cdboot\0/CDBOOT\0", 16) >= 0) {
            Volume->HasBootCode = TRUE;
            Volume->OSIconName = L"openbsd";
            Volume->OSName = L"OpenBSD";

        } else if (FindMem(SectorBuffer, 512, "Not a bootxx image", 18) >= 0 ||
                   *((UINT32 *)(SectorBuffer + 1028)) == 0x7886b6d1) {
            Volume->HasBootCode = TRUE;
            Volume->OSIconName = L"netbsd";
            Volume->OSName = L"NetBSD";

        } else if (FindMem(SectorBuffer, SECTOR_SIZE, "NTLDR", 5) >= 0) {
            Volume->HasBootCode = TRUE;
            Volume->OSIconName = L"win";
            Volume->OSName = L"Windows";

        } else if (FindMem(SectorBuffer, SECTOR_SIZE, "BOOTMGR", 7) >= 0) {
            Volume->HasBootCode = TRUE;
            Volume->OSIconName = L"winvista,win";
            Volume->OSName = L"Windows";

        } else if (FindMem(SectorBuffer, 512, "CPUBOOT SYS", 11) >= 0 ||
                   FindMem(SectorBuffer, 512, "KERNEL  SYS", 11) >= 0) {
            Volume->HasBootCode = TRUE;
            Volume->OSIconName = L"freedos";
            Volume->OSName = L"FreeDOS";

        } else if (FindMem(SectorBuffer, 512, "OS2LDR", 6) >= 0 ||
                   FindMem(SectorBuffer, 512, "OS2BOOT", 7) >= 0) {
            Volume->HasBootCode = TRUE;
            Volume->OSIconName = L"ecomstation";
            Volume->OSName = L"eComStation";

        } else if (FindMem(SectorBuffer, 512, "Be Boot Loader", 14) >= 0) {
            Volume->HasBootCode = TRUE;
            Volume->OSIconName = L"beos";
            Volume->OSName = L"BeOS";

        } else if (FindMem(SectorBuffer, 512, "yT Boot Loader", 14) >= 0) {
            Volume->HasBootCode = TRUE;
            Volume->OSIconName = L"zeta,beos";
            Volume->OSName = L"ZETA";

        } else if (FindMem(SectorBuffer, 512, "\x04" "beos\x06" "system\x05" "zbeos", 18) >= 0 ||
                   FindMem(SectorBuffer, 512, "\x06" "system\x0c" "haiku_loader", 20) >= 0) {
            Volume->HasBootCode = TRUE;
            Volume->OSIconName = L"haiku,beos";
            Volume->OSName = L"Haiku";

        }

        // NOTE: If you add an operating system with a name that starts with 'W' or 'L', you
        //  need to fix AddLegacyEntry in main.c.

#if REFIT_DEBUG > 0
        Print(L"  Result of bootcode detection: %s %s (%s)\n",
              Volume->HasBootCode ? L"bootable" : L"non-bootable",
              Volume->OSName, Volume->OSIconName);
#endif

        // dummy FAT boot sector (created by OS X's newfs_msdos)
        if (FindMem(SectorBuffer, 512, "Non-system disk", 15) >= 0)
            Volume->HasBootCode = FALSE;

        // dummy FAT boot sector (created by Linux's mkdosfs)
        if (FindMem(SectorBuffer, 512, "This is not a bootable disk", 27) >= 0)
            Volume->HasBootCode = FALSE;

        // dummy FAT boot sector (created by Windows)
        if (FindMem(SectorBuffer, 512, "Press any key to restart", 24) >= 0)
            Volume->HasBootCode = FALSE;

        // check for MBR partition table
        if (*((UINT16 *)(SectorBuffer + 510)) == 0xaa55) {
            MbrTableFound = FALSE;
            MbrTable = (MBR_PARTITION_INFO *)(SectorBuffer + 446);
            for (i = 0; i < 4; i++)
                if (MbrTable[i].StartLBA && MbrTable[i].Size)
                    MbrTableFound = TRUE;
            for (i = 0; i < 4; i++)
                if (MbrTable[i].Flags != 0x00 && MbrTable[i].Flags != 0x80)
                    MbrTableFound = FALSE;
            if (MbrTableFound) {
                Volume->MbrPartitionTable = AllocatePool(4 * 16);
                CopyMem(Volume->MbrPartitionTable, MbrTable, 4 * 16);
            }
        }

    } else {
#if REFIT_DEBUG > 0
        CheckError(Status, L"while reading boot sector");
#endif
    }
}

// default volume badge icon based on disk kind
static VOID ScanVolumeDefaultIcon(IN OUT REFIT_VOLUME *Volume)
{
    switch (Volume->DiskKind) {
       case DISK_KIND_INTERNAL:
          Volume->VolBadgeImage = BuiltinIcon(BUILTIN_ICON_VOL_INTERNAL);
          break;
       case DISK_KIND_EXTERNAL:
          Volume->VolBadgeImage = BuiltinIcon(BUILTIN_ICON_VOL_EXTERNAL);
          break;
       case DISK_KIND_OPTICAL:
          Volume->VolBadgeImage = BuiltinIcon(BUILTIN_ICON_VOL_OPTICAL);
          break;
    } // switch()
}

static VOID ScanVolume(IN OUT REFIT_VOLUME *Volume)
{
    EFI_STATUS              Status;
    EFI_DEVICE_PATH         *DevicePath, *NextDevicePath;
    EFI_DEVICE_PATH         *DiskDevicePath, *RemainingDevicePath;
    EFI_HANDLE              WholeDiskHandle;
    UINTN                   PartialLength;
    EFI_FILE_SYSTEM_INFO    *FileSystemInfoPtr;
    BOOLEAN                 Bootable;

    // get device path
    Volume->DevicePath = DuplicateDevicePath(DevicePathFromHandle(Volume->DeviceHandle));
#if REFIT_DEBUG > 0
    if (Volume->DevicePath != NULL) {
        Print(L"* %s\n", DevicePathToStr(Volume->DevicePath));
#if REFIT_DEBUG >= 2
        DumpHex(1, 0, DevicePathSize(Volume->DevicePath), Volume->DevicePath);
#endif
    }
#endif

    Volume->DiskKind = DISK_KIND_INTERNAL;  // default

    // get block i/o
    Status = refit_call3_wrapper(BS->HandleProtocol, Volume->DeviceHandle, &BlockIoProtocol, (VOID **) &(Volume->BlockIO));
    if (EFI_ERROR(Status)) {
        Volume->BlockIO = NULL;
        Print(L"Warning: Can't get BlockIO protocol.\n");
    } else {
        if (Volume->BlockIO->Media->BlockSize == 2048)
            Volume->DiskKind = DISK_KIND_OPTICAL;
    }

    // scan for bootcode and MBR table
    Bootable = FALSE;
    ScanVolumeBootcode(Volume, &Bootable);

    // detect device type
    DevicePath = Volume->DevicePath;
    while (DevicePath != NULL && !IsDevicePathEndType(DevicePath)) {
        NextDevicePath = NextDevicePathNode(DevicePath);

        if (DevicePathType(DevicePath) == MESSAGING_DEVICE_PATH &&
            (DevicePathSubType(DevicePath) == MSG_USB_DP ||
             DevicePathSubType(DevicePath) == MSG_USB_CLASS_DP ||
             DevicePathSubType(DevicePath) == MSG_1394_DP ||
             DevicePathSubType(DevicePath) == MSG_FIBRECHANNEL_DP))
            Volume->DiskKind = DISK_KIND_EXTERNAL;    // USB/FireWire/FC device -> external
        if (DevicePathType(DevicePath) == MEDIA_DEVICE_PATH &&
            DevicePathSubType(DevicePath) == MEDIA_CDROM_DP) {
            Volume->DiskKind = DISK_KIND_OPTICAL;     // El Torito entry -> optical disk
            Bootable = TRUE;
        }

        if (DevicePathType(DevicePath) == MEDIA_DEVICE_PATH && DevicePathSubType(DevicePath) == MEDIA_VENDOR_DP) {
            Volume->IsAppleLegacy = TRUE;             // legacy BIOS device entry
            // TODO: also check for Boot Camp GUID
            Bootable = FALSE;   // this handle's BlockIO is just an alias for the whole device
        }

        if (DevicePathType(DevicePath) == MESSAGING_DEVICE_PATH) {
            // make a device path for the whole device
            PartialLength = (UINT8 *)NextDevicePath - (UINT8 *)(Volume->DevicePath);
            DiskDevicePath = (EFI_DEVICE_PATH *)AllocatePool(PartialLength + sizeof(EFI_DEVICE_PATH));
            CopyMem(DiskDevicePath, Volume->DevicePath, PartialLength);
            CopyMem((UINT8 *)DiskDevicePath + PartialLength, EndDevicePath, sizeof(EFI_DEVICE_PATH));

            // get the handle for that path
            RemainingDevicePath = DiskDevicePath;
            //Print(L"  * looking at %s\n", DevicePathToStr(RemainingDevicePath));
            Status = refit_call3_wrapper(BS->LocateDevicePath, &BlockIoProtocol, &RemainingDevicePath, &WholeDiskHandle);
            //Print(L"  * remaining: %s\n", DevicePathToStr(RemainingDevicePath));
            FreePool(DiskDevicePath);

            if (!EFI_ERROR(Status)) {
                //Print(L"  - original handle: %08x - disk handle: %08x\n", (UINT32)DeviceHandle, (UINT32)WholeDiskHandle);

                // get the device path for later
                Status = refit_call3_wrapper(BS->HandleProtocol, WholeDiskHandle, &DevicePathProtocol, (VOID **) &DiskDevicePath);
                if (!EFI_ERROR(Status)) {
                    Volume->WholeDiskDevicePath = DuplicateDevicePath(DiskDevicePath);
                }

                // look at the BlockIO protocol
                Status = refit_call3_wrapper(BS->HandleProtocol, WholeDiskHandle, &BlockIoProtocol, (VOID **) &Volume->WholeDiskBlockIO);
                if (!EFI_ERROR(Status)) {

                    // check the media block size
                    if (Volume->WholeDiskBlockIO->Media->BlockSize == 2048)
                        Volume->DiskKind = DISK_KIND_OPTICAL;

                } else {
                    Volume->WholeDiskBlockIO = NULL;
                    //CheckError(Status, L"from HandleProtocol");
                }
            } //else
              //  CheckError(Status, L"from LocateDevicePath");
        }

        DevicePath = NextDevicePath;
    } // while

    if (!Bootable) {
#if REFIT_DEBUG > 0
        if (Volume->HasBootCode)
            Print(L"  Volume considered non-bootable, but boot code is present\n");
#endif
        Volume->HasBootCode = FALSE;
    }

    // default volume icon based on disk kind
    ScanVolumeDefaultIcon(Volume);

    // open the root directory of the volume
    Volume->RootDir = LibOpenRoot(Volume->DeviceHandle);
    if (Volume->RootDir == NULL) {
        Volume->IsReadable = FALSE;
        return;
    } else {
        Volume->IsReadable = TRUE;
    }

    // get volume name
    FileSystemInfoPtr = LibFileSystemInfo(Volume->RootDir);
    if (FileSystemInfoPtr != NULL) {
        Volume->VolName = StrDuplicate(FileSystemInfoPtr->VolumeLabel);
        FreePool(FileSystemInfoPtr);
    }

    if (Volume->VolName == NULL) {
       Volume->VolName = StrDuplicate(L"Unknown");
    }
    // TODO: if no official volume name is found or it is empty, use something else, e.g.:
    //   - name from bytes 3 to 10 of the boot sector
    //   - partition number
    //   - name derived from file system type or partition type

    // get custom volume icon if present
    if (FileExists(Volume->RootDir, VOLUME_BADGE_NAME))
        Volume->VolBadgeImage = LoadIcns(Volume->RootDir, VOLUME_BADGE_NAME, 32);
    if (FileExists(Volume->RootDir, VOLUME_ICON_NAME)) {
       Volume->VolIconImage = LoadIcns(Volume->RootDir, VOLUME_ICON_NAME, 128);
    }
} // ScanVolume()

static VOID ScanExtendedPartition(REFIT_VOLUME *WholeDiskVolume, MBR_PARTITION_INFO *MbrEntry)
{
    EFI_STATUS              Status;
    REFIT_VOLUME            *Volume;
    UINT32                  ExtBase, ExtCurrent, NextExtCurrent;
    UINTN                   i;
    UINTN                   LogicalPartitionIndex = 4;
    UINT8                   SectorBuffer[512];
    BOOLEAN                 Bootable;
    MBR_PARTITION_INFO      *EMbrTable;

    ExtBase = MbrEntry->StartLBA;

    for (ExtCurrent = ExtBase; ExtCurrent; ExtCurrent = NextExtCurrent) {
        // read current EMBR
      Status = refit_call5_wrapper(WholeDiskVolume->BlockIO->ReadBlocks,
                                   WholeDiskVolume->BlockIO,
                                   WholeDiskVolume->BlockIO->Media->MediaId,
                                   ExtCurrent, 512, SectorBuffer);
        if (EFI_ERROR(Status))
            break;
        if (*((UINT16 *)(SectorBuffer + 510)) != 0xaa55)
            break;
        EMbrTable = (MBR_PARTITION_INFO *)(SectorBuffer + 446);

        // scan logical partitions in this EMBR
        NextExtCurrent = 0;
        for (i = 0; i < 4; i++) {
            if ((EMbrTable[i].Flags != 0x00 && EMbrTable[i].Flags != 0x80) ||
                EMbrTable[i].StartLBA == 0 || EMbrTable[i].Size == 0)
                break;
            if (IS_EXTENDED_PART_TYPE(EMbrTable[i].Type)) {
                // set next ExtCurrent
                NextExtCurrent = ExtBase + EMbrTable[i].StartLBA;
                break;
            } else {

                // found a logical partition
                Volume = AllocateZeroPool(sizeof(REFIT_VOLUME));
                Volume->DiskKind = WholeDiskVolume->DiskKind;
                Volume->IsMbrPartition = TRUE;
                Volume->MbrPartitionIndex = LogicalPartitionIndex++;
                Volume->VolName = AllocateZeroPool(256 * sizeof(UINT16));
                SPrint(Volume->VolName, 255, L"Partition %d", Volume->MbrPartitionIndex + 1);
                Volume->BlockIO = WholeDiskVolume->BlockIO;
                Volume->BlockIOOffset = ExtCurrent + EMbrTable[i].StartLBA;
                Volume->WholeDiskBlockIO = WholeDiskVolume->BlockIO;

                Bootable = FALSE;
                ScanVolumeBootcode(Volume, &Bootable);
                if (!Bootable)
                    Volume->HasBootCode = FALSE;

                ScanVolumeDefaultIcon(Volume);

                AddListElement((VOID ***) &Volumes, &VolumesCount, Volume);

            }
        }
    }
}

VOID ScanVolumes(VOID)
{
    EFI_STATUS              Status;
    UINTN                   HandleCount = 0;
    UINTN                   HandleIndex;
    EFI_HANDLE              *Handles;
    REFIT_VOLUME            *Volume, *WholeDiskVolume;
    UINTN                   VolumeIndex, VolumeIndex2;
    MBR_PARTITION_INFO      *MbrTable;
    UINTN                   PartitionIndex;
    UINT8                   *SectorBuffer1, *SectorBuffer2;
    UINTN                   SectorSum, i;

    FreePool(Volumes);
    Volumes = NULL;
    VolumesCount = 0;

    // get all filesystem handles
    Status = LibLocateHandle(ByProtocol, &BlockIoProtocol, NULL, &HandleCount, &Handles);
    // was: &FileSystemProtocol
    if (Status == EFI_NOT_FOUND) {
        return;  // no filesystems. strange, but true...
    }
    if (CheckError(Status, L"while listing all file systems"))
        return;

    // first pass: collect information about all handles
    for (HandleIndex = 0; HandleIndex < HandleCount; HandleIndex++) {
        Volume = AllocateZeroPool(sizeof(REFIT_VOLUME));
        Volume->DeviceHandle = Handles[HandleIndex];
        ScanVolume(Volume);

        AddListElement((VOID ***) &Volumes, &VolumesCount, Volume);

        if (Volume->DeviceHandle == SelfLoadedImage->DeviceHandle)
            SelfVolume = Volume;
    }
    FreePool(Handles);

    if (SelfVolume == NULL)
        Print(L"WARNING: SelfVolume not found");

    // second pass: relate partitions and whole disk devices
    for (VolumeIndex = 0; VolumeIndex < VolumesCount; VolumeIndex++) {
        Volume = Volumes[VolumeIndex];
        // check MBR partition table for extended partitions
        if (Volume->BlockIO != NULL && Volume->WholeDiskBlockIO != NULL &&
            Volume->BlockIO == Volume->WholeDiskBlockIO && Volume->BlockIOOffset == 0 &&
            Volume->MbrPartitionTable != NULL) {
            MbrTable = Volume->MbrPartitionTable;
            for (PartitionIndex = 0; PartitionIndex < 4; PartitionIndex++) {
                if (IS_EXTENDED_PART_TYPE(MbrTable[PartitionIndex].Type)) {
                   ScanExtendedPartition(Volume, MbrTable + PartitionIndex);
                }
            }
        }

        // search for corresponding whole disk volume entry
        WholeDiskVolume = NULL;
        if (Volume->BlockIO != NULL && Volume->WholeDiskBlockIO != NULL &&
            Volume->BlockIO != Volume->WholeDiskBlockIO) {
            for (VolumeIndex2 = 0; VolumeIndex2 < VolumesCount; VolumeIndex2++) {
                if (Volumes[VolumeIndex2]->BlockIO == Volume->WholeDiskBlockIO &&
                    Volumes[VolumeIndex2]->BlockIOOffset == 0)
                    WholeDiskVolume = Volumes[VolumeIndex2];
            }
        }

        if (WholeDiskVolume != NULL && WholeDiskVolume->MbrPartitionTable != NULL) {
            // check if this volume is one of the partitions in the table
            MbrTable = WholeDiskVolume->MbrPartitionTable;
            SectorBuffer1 = AllocatePool(512);
            SectorBuffer2 = AllocatePool(512);
            for (PartitionIndex = 0; PartitionIndex < 4; PartitionIndex++) {
                // check size
                if ((UINT64)(MbrTable[PartitionIndex].Size) != Volume->BlockIO->Media->LastBlock + 1)
                    continue;

                // compare boot sector read through offset vs. directly
                Status = refit_call5_wrapper(Volume->BlockIO->ReadBlocks,
                                             Volume->BlockIO, Volume->BlockIO->Media->MediaId,
                                             Volume->BlockIOOffset, 512, SectorBuffer1);
                if (EFI_ERROR(Status))
                    break;
                Status = refit_call5_wrapper(Volume->WholeDiskBlockIO->ReadBlocks,
                                             Volume->WholeDiskBlockIO, Volume->WholeDiskBlockIO->Media->MediaId,
                                             MbrTable[PartitionIndex].StartLBA, 512, SectorBuffer2);
                if (EFI_ERROR(Status))
                    break;
                if (CompareMem(SectorBuffer1, SectorBuffer2, 512) != 0)
                    continue;
                SectorSum = 0;
                for (i = 0; i < 512; i++)
                    SectorSum += SectorBuffer1[i];
                if (SectorSum < 1000)
                    continue;

                // TODO: mark entry as non-bootable if it is an extended partition

                // now we're reasonably sure the association is correct...
                Volume->IsMbrPartition = TRUE;
                Volume->MbrPartitionIndex = PartitionIndex;
                if (Volume->VolName == NULL) {
                    Volume->VolName = AllocateZeroPool(sizeof(CHAR16) * 256);
                    SPrint(Volume->VolName, 255, L"Partition %d", PartitionIndex + 1);
                }
                break;
            }

            FreePool(SectorBuffer1);
            FreePool(SectorBuffer2);
        }

    }
} /* VOID ScanVolumes() */

static VOID UninitVolumes(VOID)
{
    REFIT_VOLUME            *Volume;
    UINTN                   VolumeIndex;

    for (VolumeIndex = 0; VolumeIndex < VolumesCount; VolumeIndex++) {
        Volume = Volumes[VolumeIndex];

        if (Volume->RootDir != NULL) {
            refit_call1_wrapper(Volume->RootDir->Close, Volume->RootDir);
            Volume->RootDir = NULL;
        }

        Volume->DeviceHandle = NULL;
        Volume->BlockIO = NULL;
        Volume->WholeDiskBlockIO = NULL;
    }
}

VOID ReinitVolumes(VOID)
{
    EFI_STATUS              Status;
    REFIT_VOLUME            *Volume;
    UINTN                   VolumeIndex;
    EFI_DEVICE_PATH         *RemainingDevicePath;
    EFI_HANDLE              DeviceHandle, WholeDiskHandle;

    for (VolumeIndex = 0; VolumeIndex < VolumesCount; VolumeIndex++) {
        Volume = Volumes[VolumeIndex];

        if (Volume->DevicePath != NULL) {
            // get the handle for that path
            RemainingDevicePath = Volume->DevicePath;
            Status = refit_call3_wrapper(BS->LocateDevicePath, &BlockIoProtocol, &RemainingDevicePath, &DeviceHandle);

            if (!EFI_ERROR(Status)) {
                Volume->DeviceHandle = DeviceHandle;

                // get the root directory
                Volume->RootDir = LibOpenRoot(Volume->DeviceHandle);

            } else
                CheckError(Status, L"from LocateDevicePath");
        }

        if (Volume->WholeDiskDevicePath != NULL) {
            // get the handle for that path
            RemainingDevicePath = Volume->WholeDiskDevicePath;
            Status = refit_call3_wrapper(BS->LocateDevicePath, &BlockIoProtocol, &RemainingDevicePath, &WholeDiskHandle);

            if (!EFI_ERROR(Status)) {
                // get the BlockIO protocol
                Status = refit_call3_wrapper(BS->HandleProtocol, WholeDiskHandle, &BlockIoProtocol, (VOID **) &Volume->WholeDiskBlockIO);
                if (EFI_ERROR(Status)) {
                    Volume->WholeDiskBlockIO = NULL;
                    CheckError(Status, L"from HandleProtocol");
                }
            } else
                CheckError(Status, L"from LocateDevicePath");
        }
    }
}

//
// file and dir functions
//

BOOLEAN FileExists(IN EFI_FILE *BaseDir, IN CHAR16 *RelativePath)
{
    EFI_STATUS         Status;
    EFI_FILE_HANDLE    TestFile;

    Status = refit_call5_wrapper(BaseDir->Open, BaseDir, &TestFile, RelativePath, EFI_FILE_MODE_READ, 0);
    if (Status == EFI_SUCCESS) {
        refit_call1_wrapper(TestFile->Close, TestFile);
        return TRUE;
    }
    return FALSE;
}

EFI_STATUS DirNextEntry(IN EFI_FILE *Directory, IN OUT EFI_FILE_INFO **DirEntry, IN UINTN FilterMode)
{
    EFI_STATUS Status;
    VOID *Buffer;
    UINTN LastBufferSize, BufferSize;
    INTN IterCount;

    for (;;) {

        // free pointer from last call
        if (*DirEntry != NULL) {
            FreePool(*DirEntry);
            *DirEntry = NULL;
        }

        // read next directory entry
        LastBufferSize = BufferSize = 256;
        Buffer = AllocatePool(BufferSize);
        for (IterCount = 0; ; IterCount++) {
            Status = refit_call3_wrapper(Directory->Read, Directory, &BufferSize, Buffer);
            if (Status != EFI_BUFFER_TOO_SMALL || IterCount >= 4)
                break;
            if (BufferSize <= LastBufferSize) {
                Print(L"FS Driver requests bad buffer size %d (was %d), using %d instead\n", BufferSize, LastBufferSize, LastBufferSize * 2);
                BufferSize = LastBufferSize * 2;
#if REFIT_DEBUG > 0
            } else {
                Print(L"Reallocating buffer from %d to %d\n", LastBufferSize, BufferSize);
#endif
            }
            Buffer = EfiReallocatePool(Buffer, LastBufferSize, BufferSize);
            LastBufferSize = BufferSize;
        }
        if (EFI_ERROR(Status)) {
            FreePool(Buffer);
            break;
        }

        // check for end of listing
        if (BufferSize == 0) {    // end of directory listing
            FreePool(Buffer);
            break;
        }

        // entry is ready to be returned
        *DirEntry = (EFI_FILE_INFO *)Buffer;

        // filter results
        if (FilterMode == 1) {   // only return directories
            if (((*DirEntry)->Attribute & EFI_FILE_DIRECTORY))
                break;
        } else if (FilterMode == 2) {   // only return files
            if (((*DirEntry)->Attribute & EFI_FILE_DIRECTORY) == 0)
                break;
        } else                   // no filter or unknown filter -> return everything
            break;

    }
    return Status;
}

VOID DirIterOpen(IN EFI_FILE *BaseDir, IN CHAR16 *RelativePath OPTIONAL, OUT REFIT_DIR_ITER *DirIter)
{
    if (RelativePath == NULL) {
        DirIter->LastStatus = EFI_SUCCESS;
        DirIter->DirHandle = BaseDir;
        DirIter->CloseDirHandle = FALSE;
    } else {
        DirIter->LastStatus = refit_call5_wrapper(BaseDir->Open, BaseDir, &(DirIter->DirHandle), RelativePath, EFI_FILE_MODE_READ, 0);
        DirIter->CloseDirHandle = EFI_ERROR(DirIter->LastStatus) ? FALSE : TRUE;
    }
    DirIter->LastFileInfo = NULL;
}

#ifndef __MAKEWITH_GNUEFI
EFI_UNICODE_COLLATION_PROTOCOL *mUnicodeCollation = NULL;

static EFI_STATUS
InitializeUnicodeCollationProtocol (VOID)
{
   EFI_STATUS  Status;

   if (mUnicodeCollation != NULL) {
      return EFI_SUCCESS;
   }

   //
   // BUGBUG: Proper impelmentation is to locate all Unicode Collation Protocol
   // instances first and then select one which support English language.
   // Current implementation just pick the first instance.
   //
   Status = gBS->LocateProtocol (
                          &gEfiUnicodeCollation2ProtocolGuid,
                          NULL,
                          (VOID **) &mUnicodeCollation
                          );
  if (EFI_ERROR(Status)) {
    Status = gBS->LocateProtocol (
                  &gEfiUnicodeCollationProtocolGuid,
                  NULL,
                  (VOID **) &mUnicodeCollation
                  );

  }
   return Status;
}

static BOOLEAN
MetaiMatch (IN CHAR16 *String, IN CHAR16 *Pattern)
{
   if (!mUnicodeCollation) {
      InitializeUnicodeCollationProtocol();
   }
   if (mUnicodeCollation)
      return mUnicodeCollation->MetaiMatch (mUnicodeCollation, String, Pattern);
   return FALSE; // Shouldn't happen
}

static VOID StrLwr (IN OUT CHAR16 *Str) {
   if (!mUnicodeCollation) {
      InitializeUnicodeCollationProtocol();
   }
   if (mUnicodeCollation)
      mUnicodeCollation->StrLwr (mUnicodeCollation, Str);
}

#endif

BOOLEAN DirIterNext(IN OUT REFIT_DIR_ITER *DirIter, IN UINTN FilterMode, IN CHAR16 *FilePattern OPTIONAL,
                    OUT EFI_FILE_INFO **DirEntry)
{
    BOOLEAN KeepGoing = TRUE;
    UINTN   i;
    CHAR16  *OnePattern;

    if (DirIter->LastFileInfo != NULL) {
        FreePool(DirIter->LastFileInfo);
        DirIter->LastFileInfo = NULL;
    }

    if (EFI_ERROR(DirIter->LastStatus))
        return FALSE;   // stop iteration

    do {
        DirIter->LastStatus = DirNextEntry(DirIter->DirHandle, &(DirIter->LastFileInfo), FilterMode);
        if (EFI_ERROR(DirIter->LastStatus))
           return FALSE;
        if (DirIter->LastFileInfo == NULL)  // end of listing
            return FALSE;
        if (FilePattern != NULL) {
            if ((DirIter->LastFileInfo->Attribute & EFI_FILE_DIRECTORY))
                KeepGoing = FALSE;
            i = 0;
            while (KeepGoing && (OnePattern = FindCommaDelimited(FilePattern, i++)) != NULL) {
               if (MetaiMatch(DirIter->LastFileInfo->FileName, OnePattern))
                   KeepGoing = FALSE;
            } // while
            // else continue loop
        } else
            break;
   } while (KeepGoing);

    *DirEntry = DirIter->LastFileInfo;
    return TRUE;
}

EFI_STATUS DirIterClose(IN OUT REFIT_DIR_ITER *DirIter)
{
    if (DirIter->LastFileInfo != NULL) {
        FreePool(DirIter->LastFileInfo);
        DirIter->LastFileInfo = NULL;
    }
    if (DirIter->CloseDirHandle)
        refit_call1_wrapper(DirIter->DirHandle->Close, DirIter->DirHandle);
    return DirIter->LastStatus;
}

//
// file name manipulation
//

// Returns the filename portion (minus path name) of the
// specified file
CHAR16 * Basename(IN CHAR16 *Path)
{
    CHAR16  *FileName;
    UINTN   i;

    FileName = Path;

    if (Path != NULL) {
        for (i = StrLen(Path); i > 0; i--) {
            if (Path[i-1] == '\\' || Path[i-1] == '/') {
                FileName = Path + i;
                break;
            }
        }
    }

    return FileName;
}

// Replaces a filename extension of ".efi" with the specified string
// (Extension). If the input Path doesn't end in ".efi", Extension
// is added to the existing filename.
VOID ReplaceEfiExtension(IN OUT CHAR16 *Path, IN CHAR16 *Extension)
{
    UINTN PathLen;

    PathLen = StrLen(Path);
    // Note: Do StriCmp() twice to work around Gigabyte Hybrid EFI case-sensitivity bug....
    if ((PathLen >= 4) && ((StriCmp(&Path[PathLen - 4], L".efi") == 0) || (StriCmp(&Path[PathLen - 4], L".EFI") == 0))) {
       Path[PathLen - 4] = 0;
    } // if
    StrCat(Path, Extension);
} // VOID ReplaceEfiExtension()

//
// memory string search
//

INTN FindMem(IN VOID *Buffer, IN UINTN BufferLength, IN VOID *SearchString, IN UINTN SearchStringLength)
{
    UINT8 *BufferPtr;
    UINTN Offset;

    BufferPtr = Buffer;
    BufferLength -= SearchStringLength;
    for (Offset = 0; Offset < BufferLength; Offset++, BufferPtr++) {
        if (CompareMem(BufferPtr, SearchString, SearchStringLength) == 0)
            return (INTN)Offset;
    }

    return -1;
}

// Performs a case-insensitive search of BigStr for SmallStr.
// Returns TRUE if found, FALSE if not.
BOOLEAN StriSubCmp(IN CHAR16 *SmallStr, IN CHAR16 *BigStr) {
   CHAR16 *SmallCopy, *BigCopy;
   BOOLEAN Found = FALSE;
   UINTN StartPoint = 0, NumCompares = 0, SmallLen = 0;

   if ((SmallStr != NULL) && (BigStr != NULL) && (StrLen(BigStr) >= StrLen(SmallStr))) {
      SmallCopy = StrDuplicate(SmallStr);
      BigCopy = StrDuplicate(BigStr);
      StrLwr(SmallCopy);
      StrLwr(BigCopy);
      SmallLen = StrLen(SmallCopy);
      NumCompares = StrLen(BigCopy) - SmallLen + 1;
      while ((!Found) && (StartPoint < NumCompares)) {
         Found = (StrnCmp(SmallCopy, &BigCopy[StartPoint++], SmallLen) == 0);
      } // while
      FreePool(SmallCopy);
      FreePool(BigCopy);
   } // if

   return (Found);
} // BOOLEAN StriSubCmp()

// Merges two strings, creating a new one and returning a pointer to it.
// If AddChar != 0, the specified character is placed between the two original
// strings (unless the first string is NULL). The original input string
// *First is de-allocated and replaced by the new merged string.
// This is similar to StrCat, but safer and more flexible because
// MergeStrings allocates memory that's the correct size for the
// new merged string, so it can take a NULL *First and it cleans
// up the old memory. It should *NOT* be used with a constant
// *First, though....
VOID MergeStrings(IN OUT CHAR16 **First, IN CHAR16 *Second, CHAR16 AddChar) {
   UINTN Length1 = 0, Length2 = 0;
   CHAR16* NewString;

   if (*First != NULL)
      Length1 = StrLen(*First);
   if (Second != NULL)
      Length2 = StrLen(Second);
   NewString = AllocatePool(sizeof(CHAR16) * (Length1 + Length2 + 2));
   if (NewString != NULL) {
      NewString[0] = L'\0';
      if (*First != NULL) {
         StrCat(NewString, *First);
         if (AddChar) {
            NewString[Length1] = AddChar;
            NewString[Length1 + 1] = 0;
         } // if (AddChar)
      } // if (*First != NULL)
      if (Second != NULL)
         StrCat(NewString, Second);
      FreePool(*First);
      *First = NewString;
   } else {
      Print(L"Error! Unable to allocate memory in MergeStrings()!\n");
   } // if/else
} // static CHAR16* MergeStrings()

// Takes an input pathname (*Path) and returns the part of the filename from
// the final dot onwards, converted to lowercase. If the filename includes
// no dots, or if the input is NULL, returns an empty (but allocated) string.
// The calling function is responsible for freeing the memory associated with
// the return value.
CHAR16 *FindExtension(IN CHAR16 *Path) {
   CHAR16     *Extension;
   BOOLEAN    Found = FALSE, FoundSlash = FALSE;
   UINTN       i;

   Extension = AllocateZeroPool(sizeof(CHAR16));
   if (Path) {
      i = StrLen(Path);
      while ((!Found) && (!FoundSlash) && (i >= 0)) {
         if (Path[i] == L'.')
            Found = TRUE;
         else if ((Path[i] == L'/') || (Path[i] == L'\\'))
            FoundSlash = TRUE;
         if (!Found)
            i--;
      } // while
      if (Found) {
         MergeStrings(&Extension, &Path[i], 0);
         StrLwr(Extension);
      } // if (Found)
   } // if
   return (Extension);
} // CHAR16 *FindExtension

// Takes an input pathname (*Path) and locates the final directory component
// of that name. For instance, if the input path is 'EFI\foo\bar.efi', this
// function returns the string 'foo'.
// Assumes the pathname is separated with backslashes.
CHAR16 *FindLastDirName(IN CHAR16 *Path) {
   UINTN i, StartOfElement = 0, EndOfElement = 0, PathLength, CopyLength;
   CHAR16 *Found = NULL;

   PathLength = StrLen(Path);
   // Find start & end of target element
   for (i = 0; i < PathLength; i++) {
      if (Path[i] == '\\') {
         StartOfElement = EndOfElement;
         EndOfElement = i;
      } // if
   } // for
   // Extract the target element
   if (EndOfElement > 0) {
      while ((StartOfElement < PathLength) && (Path[StartOfElement] == '\\')) {
         StartOfElement++;
      } // while
      EndOfElement--;
      if (EndOfElement >= StartOfElement) {
         CopyLength = EndOfElement - StartOfElement + 1;
         Found = StrDuplicate(&Path[StartOfElement]);
         if (Found != NULL)
            Found[CopyLength] = 0;
      } // if (EndOfElement >= StartOfElement)
   } // if (EndOfElement > 0)
   return (Found);
} // CHAR16 *FindLastDirName

// Returns the directory portion of a pathname. For instance,
// if FullPath is 'EFI\foo\bar.efi', this function returns the
// string 'EFI\foo'. The calling function is responsible for
// freeing the returned string's memory.
CHAR16 *FindPath(IN CHAR16* FullPath) {
   UINTN i, LastBackslash = 0;
   CHAR16 *PathOnly;

   for (i = 0; i < StrLen(FullPath); i++) {
      if (FullPath[i] == '\\')
         LastBackslash = i;
   } // for
   PathOnly = StrDuplicate(FullPath);
   PathOnly[LastBackslash] = 0;
   return (PathOnly);
}

// Returns all the digits in the input string, including intervening
// non-digit characters. For instance, if InString is "foo-3.3.4-7.img",
// this function returns "3.3.4-7". If InString contains no digits,
// the return value is NULL.
CHAR16 *FindNumbers(IN CHAR16 *InString) {
   UINTN i, StartOfElement, EndOfElement = 0, InLength, CopyLength;
   CHAR16 *Found = NULL;

   InLength = StartOfElement = StrLen(InString);
   // Find start & end of target element
   for (i = 0; i < InLength; i++) {
      if ((InString[i] >= '0') && (InString[i] <= '9')) {
         if (StartOfElement > i)
            StartOfElement = i;
         if (EndOfElement < i)
            EndOfElement = i;
      } // if
   } // for
   // Extract the target element
   if (EndOfElement > 0) {
      if (EndOfElement >= StartOfElement) {
         CopyLength = EndOfElement - StartOfElement + 1;
         Found = StrDuplicate(&InString[StartOfElement]);
         if (Found != NULL)
            Found[CopyLength] = 0;
      } // if (EndOfElement >= StartOfElement)
   } // if (EndOfElement > 0)
   return (Found);
} // CHAR16 *FindNumbers()

// Find the #Index element (numbered from 0) in a comma-delimited string
// of elements.
// Returns the found element, or NULL if Index is out of range or InString
// is NULL. Note that the calling function is responsible for freeing the
// memory associated with the returned string pointer.
CHAR16 *FindCommaDelimited(IN CHAR16 *InString, IN UINTN Index) {
   UINTN    StartPos = 0, CurPos = 0;
   BOOLEAN  Found = FALSE;
   CHAR16   *FoundString = NULL;

   if (InString != NULL) {
      // After while() loop, StartPos marks start of item #Index
      while ((Index > 0) && (CurPos < StrLen(InString))) {
         if (InString[CurPos] == L',') {
            Index--;
            StartPos = CurPos + 1;
         } // if
         CurPos++;
      } // while
      // After while() loop, CurPos is one past the end of the element
      while ((CurPos < StrLen(InString)) && (!Found)) {
         if (InString[CurPos] == L',')
            Found = TRUE;
         else
            CurPos++;
      } // while
      if (Index == 0)
         FoundString = StrDuplicate(&InString[StartPos]);
      if (FoundString != NULL)
         FoundString[CurPos - StartPos] = 0;
   } // if
   return (FoundString);
} // CHAR16 *FindCommaDelimited()

// Returns TRUE if SmallString is an element in the comma-delimited List,
// FALSE otherwise. Performs comparison case-insensitively (except on
// buggy EFIs with case-sensitive StriCmp() functions).
BOOLEAN IsIn(IN CHAR16 *SmallString, IN CHAR16 *List) {
   UINTN     i = 0;
   BOOLEAN   Found = FALSE;
   CHAR16    *OneElement;

   if (SmallString && List) {
      while (!Found && (OneElement = FindCommaDelimited(List, i++))) {
         if (StriCmp(OneElement, SmallString) == 0)
            Found = TRUE;
      } // while
   } // if
   return Found;
} // BOOLEAN IsIn()

static EFI_GUID AppleRemovableMediaGuid = APPLE_REMOVABLE_MEDIA_PROTOCOL_GUID;

// Eject all removable media.
// Returns TRUE if any media were ejected, FALSE otherwise.
BOOLEAN EjectMedia(VOID) {
   EFI_STATUS                      Status;
   UINTN                           HandleIndex, HandleCount = 0, Ejected = 0;
   EFI_HANDLE                      *Handles, Handle;
   APPLE_REMOVABLE_MEDIA_PROTOCOL  *Ejectable;

   Status = LibLocateHandle(ByProtocol, &AppleRemovableMediaGuid, NULL, &HandleCount, &Handles);
   if (EFI_ERROR(Status) || HandleCount == 0)
      return (FALSE); // probably not an Apple system

   for (HandleIndex = 0; HandleIndex < HandleCount; HandleIndex++) {
      Handle = Handles[HandleIndex];
      Status = refit_call3_wrapper(BS->HandleProtocol, Handle, &AppleRemovableMediaGuid, (VOID **) &Ejectable);
      if (EFI_ERROR(Status))
         continue;
      Status = refit_call1_wrapper(Ejectable->Eject, Ejectable);
      if (!EFI_ERROR(Status))
         Ejected++;
   }
   FreePool(Handles);
   return (Ejected > 0);
} // VOID EjectMedia()


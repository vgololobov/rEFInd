/* refind/mok.c
 *
 * Based mostly on shim.c by Matthew J. Garrett/Red Hat (see below
 * copyright notice).
 *
 * Code to perform Secure Boot verification of boot loader programs
 * using the Shim program and its Machine Owner Keys (MOKs), to
 * supplement standard Secure Boot checks performed by the firmware.
 *
 */

/*
 * shim - trivial UEFI first-stage bootloader
 *
 * Copyright 2012 Red Hat, Inc <mjg@redhat.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Significant portions of this code are derived from Tianocore
 * (http://tianocore.sf.net) and are Copyright 2009-2012 Intel
 * Corporation.
 */

#include "global.h"
#include "mok.h"
#include "lib.h"
#include "config.h"
#include "screen.h"
#include "../include/refit_call_wrapper.h"
#include "../include/PeImage.h"

#define SECOND_STAGE L"\\refind.efi"
#define MOK_MANAGER L"\\MokManager.efi"

static EFI_STATUS (EFIAPI *entry_point) (EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table);

// /*
//  * The vendor certificate used for validating the second stage loader
//  */
// extern UINT8 vendor_cert[];
// extern UINT32 vendor_cert_size;
// extern UINT32 vendor_dbx_size;

#define EFI_IMAGE_SECURITY_DATABASE_GUID { 0xd719b2cb, 0x3d3a, 0x4596, { 0xa3, 0xbc, 0xda, 0xd0, 0x0e, 0x67, 0x65, 0x6f }}

//static UINT8 insecure_mode;

typedef enum {
   DATA_FOUND,
   DATA_NOT_FOUND,
   VAR_NOT_FOUND
} CHECK_STATUS;

typedef struct {
   UINT32 MokSize;
   UINT8 *Mok;
} MokListNode;


static EFI_STATUS get_variable (CHAR16 *name, EFI_GUID guid, UINT32 *attributes, UINTN *size, VOID **buffer)
{
   EFI_STATUS efi_status;
   char allocate = !(*size);

   efi_status = uefi_call_wrapper(RT->GetVariable, 5, name, &guid, attributes, size, buffer);

   if (efi_status != EFI_BUFFER_TOO_SMALL || !allocate) {
      return efi_status;
   }

   *buffer = AllocatePool(*size);

   if (!*buffer) {
      Print(L"Unable to allocate variable buffer\n");
      return EFI_OUT_OF_RESOURCES;
   }

   efi_status = uefi_call_wrapper(RT->GetVariable, 5, name, &guid, attributes, size, *buffer);

   return efi_status;
} // get_variable()

/*
 * Check whether we're in Secure Boot and user mode
 */
BOOLEAN secure_mode (VOID)
{
   EFI_STATUS status;
   EFI_GUID global_var = EFI_GLOBAL_VARIABLE;
   UINTN charsize = sizeof(char);
   UINT8 sb, setupmode;
   UINT32 attributes;

   status = get_variable(L"SecureBoot", global_var, &attributes, &charsize, (VOID *)&sb);

   /* FIXME - more paranoia here? */
   if (status != EFI_SUCCESS || sb != 1) {
      return FALSE;
   }

   status = get_variable(L"SetupMode", global_var, &attributes, &charsize, (VOID *)&setupmode);

   if (status == EFI_SUCCESS && setupmode == 1) {
      return FALSE;
   }

   return TRUE;
} // secure_mode()

/*
 * Currently, shim/MOK only works on x86-64 (X64) systems, and some of this code
 * generates warnings on x86 (IA32) builds, so don't bother compiling it at all
 * on such systems.
 *
 */

#if defined(EFIX64)

/*
 * Perform basic bounds checking of the intra-image pointers
 */
static void *ImageAddress (void *image, int size, unsigned int address)
{
   if (address > size)
      return NULL;

   return image + address;
}

/*
 * Perform the actual relocation
 */
static EFI_STATUS relocate_coff (GNUEFI_PE_COFF_LOADER_IMAGE_CONTEXT *context, void *data)
{
   EFI_IMAGE_BASE_RELOCATION *RelocBase, *RelocBaseEnd;
   UINT64 Adjust;
   UINT16 *Reloc, *RelocEnd;
   char *Fixup, *FixupBase, *FixupData = NULL;
   UINT16 *Fixup16;
   UINT32 *Fixup32;
   UINT64 *Fixup64;
   int size = context->ImageSize;
   void *ImageEnd = (char *)data + size;

   context->PEHdr->Pe32Plus.OptionalHeader.ImageBase = (UINT64)data;

   // Linux kernels with EFI stub support don't have relocation information, so
   // we can skip all this stuff....
   if (((context->RelocDir->VirtualAddress == 0) && (context->RelocDir->Size == 0)) ||
       ((context->PEHdr->Pe32.FileHeader.Characteristics & EFI_IMAGE_FILE_RELOCS_STRIPPED) != 0)) {
      return EFI_SUCCESS;
   }

   if (context->NumberOfRvaAndSizes <= EFI_IMAGE_DIRECTORY_ENTRY_BASERELOC) {
      Print(L"Image has no relocation entry\n");
      return EFI_UNSUPPORTED;
   }

   RelocBase = ImageAddress(data, size, context->RelocDir->VirtualAddress);
   RelocBaseEnd = ImageAddress(data, size, context->RelocDir->VirtualAddress + context->RelocDir->Size - 1);

   if (!RelocBase || !RelocBaseEnd) {
      Print(L"Reloc table overflows binary\n");
      return EFI_UNSUPPORTED;
   }

   Adjust = (UINT64)data - context->ImageAddress;

   while (RelocBase < RelocBaseEnd) {
      Reloc = (UINT16 *) ((char *) RelocBase + sizeof (EFI_IMAGE_BASE_RELOCATION));
      RelocEnd = (UINT16 *) ((char *) RelocBase + RelocBase->SizeOfBlock);

      if ((void *)RelocEnd < data || (void *)RelocEnd > ImageEnd) {
         Print(L"Reloc entry overflows binary\n");
         return EFI_UNSUPPORTED;
      }

      FixupBase = ImageAddress(data, size, RelocBase->VirtualAddress);
      if (!FixupBase) {
         Print(L"Invalid fixupbase\n");
         return EFI_UNSUPPORTED;
      }

      while (Reloc < RelocEnd) {
         Fixup = FixupBase + (*Reloc & 0xFFF);
         switch ((*Reloc) >> 12) {
         case EFI_IMAGE_REL_BASED_ABSOLUTE:
            break;

         case EFI_IMAGE_REL_BASED_HIGH:
            Fixup16   = (UINT16 *) Fixup;
            *Fixup16 = (UINT16) (*Fixup16 + ((UINT16) ((UINT32) Adjust >> 16)));
            if (FixupData != NULL) {
               *(UINT16 *) FixupData = *Fixup16;
               FixupData             = FixupData + sizeof (UINT16);
            }
            break;

         case EFI_IMAGE_REL_BASED_LOW:
            Fixup16   = (UINT16 *) Fixup;
            *Fixup16  = (UINT16) (*Fixup16 + (UINT16) Adjust);
            if (FixupData != NULL) {
               *(UINT16 *) FixupData = *Fixup16;
               FixupData             = FixupData + sizeof (UINT16);
            }
            break;

         case EFI_IMAGE_REL_BASED_HIGHLOW:
            Fixup32   = (UINT32 *) Fixup;
            *Fixup32  = *Fixup32 + (UINT32) Adjust;
            if (FixupData != NULL) {
               FixupData             = ALIGN_POINTER (FixupData, sizeof (UINT32));
               *(UINT32 *)FixupData  = *Fixup32;
               FixupData             = FixupData + sizeof (UINT32);
            }
            break;

         case EFI_IMAGE_REL_BASED_DIR64:
            Fixup64 = (UINT64 *) Fixup;
            *Fixup64 = *Fixup64 + (UINT64) Adjust;
            if (FixupData != NULL) {
               FixupData = ALIGN_POINTER (FixupData, sizeof(UINT64));
               *(UINT64 *)(FixupData) = *Fixup64;
               FixupData = FixupData + sizeof(UINT64);
            }
            break;

         default:
            Print(L"Unknown relocation\n");
            return EFI_UNSUPPORTED;
         }
         Reloc += 1;
      }
      RelocBase = (EFI_IMAGE_BASE_RELOCATION *) RelocEnd;
   }

   return EFI_SUCCESS;
} /* relocate_coff() */

/*
 * Read the binary header and grab appropriate information from it
 */
static EFI_STATUS read_header(void *data, unsigned int datasize,
               GNUEFI_PE_COFF_LOADER_IMAGE_CONTEXT *context)
{
   EFI_IMAGE_DOS_HEADER *DosHdr = data;
   EFI_IMAGE_OPTIONAL_HEADER_UNION *PEHdr = data;

   if (datasize < sizeof(EFI_IMAGE_DOS_HEADER)) {
      Print(L"Invalid image\n");
      return EFI_UNSUPPORTED;
   }

   if (DosHdr->e_magic == EFI_IMAGE_DOS_SIGNATURE)
      PEHdr = (EFI_IMAGE_OPTIONAL_HEADER_UNION *)((char *)data + DosHdr->e_lfanew);

   if ((((UINT8 *)PEHdr - (UINT8 *)data) + sizeof(EFI_IMAGE_OPTIONAL_HEADER_UNION)) > datasize) {
      Print(L"Invalid image\n");
      return EFI_UNSUPPORTED;
   }

   if (PEHdr->Te.Signature != EFI_IMAGE_NT_SIGNATURE) {
      Print(L"Unsupported image type\n");
      return EFI_UNSUPPORTED;
   }

   if (PEHdr->Pe32.FileHeader.Characteristics & EFI_IMAGE_FILE_RELOCS_STRIPPED) {
      Print(L"Unsupported image - Relocations have been stripped\n");
      return EFI_UNSUPPORTED;
   }

   if (PEHdr->Pe32.OptionalHeader.Magic != EFI_IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
      Print(L"Only 64-bit images supported\n");
      return EFI_UNSUPPORTED;
   }

   context->PEHdr = PEHdr;
   context->ImageAddress = PEHdr->Pe32Plus.OptionalHeader.ImageBase;
   context->ImageSize = (UINT64)PEHdr->Pe32Plus.OptionalHeader.SizeOfImage;
   context->SizeOfHeaders = PEHdr->Pe32Plus.OptionalHeader.SizeOfHeaders;
   context->EntryPoint = PEHdr->Pe32Plus.OptionalHeader.AddressOfEntryPoint;
   context->RelocDir = &PEHdr->Pe32Plus.OptionalHeader.DataDirectory[EFI_IMAGE_DIRECTORY_ENTRY_BASERELOC];
   context->NumberOfRvaAndSizes = PEHdr->Pe32Plus.OptionalHeader.NumberOfRvaAndSizes;
   context->NumberOfSections = PEHdr->Pe32.FileHeader.NumberOfSections;
   context->FirstSection = (EFI_IMAGE_SECTION_HEADER *)((char *)PEHdr + PEHdr->Pe32.FileHeader.SizeOfOptionalHeader + sizeof(UINT32) + sizeof(EFI_IMAGE_FILE_HEADER));
   context->SecDir = (EFI_IMAGE_DATA_DIRECTORY *) &PEHdr->Pe32Plus.OptionalHeader.DataDirectory[EFI_IMAGE_DIRECTORY_ENTRY_SECURITY];

   if (context->ImageSize < context->SizeOfHeaders) {
      Print(L"Invalid image\n");
      return EFI_UNSUPPORTED;
   }

   if (((UINT8 *)context->SecDir - (UINT8 *)data) > (datasize - sizeof(EFI_IMAGE_DATA_DIRECTORY))) {
      Print(L"Invalid image\n");
      return EFI_UNSUPPORTED;
   }

   if (context->SecDir->VirtualAddress >= datasize) {
      Print(L"Malformed security header\n");
      return EFI_INVALID_PARAMETER;
   }
   return EFI_SUCCESS;
}

// The following is based on the grub_linuxefi_secure_validate() function in Fedora's
// version of GRUB 2.
// Returns TRUE if the specified data is validated by Shim's MOK, FALSE otherwise
static BOOLEAN ShimValidate (VOID *data, UINT32 size)
{
   EFI_GUID    ShimLockGuid = SHIM_LOCK_GUID;
   SHIM_LOCK   *shim_lock;

   if (BS->LocateProtocol(&ShimLockGuid, NULL, (VOID**) &shim_lock) == EFI_SUCCESS) {
      if (!shim_lock)
         return FALSE;

      if (shim_lock->shim_verify(data, size) == EFI_SUCCESS)
         return TRUE;
   } // if

   return FALSE;
} // BOOLEAN ShimValidate()

/*
 * Once the image has been loaded it needs to be validated and relocated
 */
static EFI_STATUS handle_image (void *data, unsigned int datasize, EFI_LOADED_IMAGE *li,
                                CHAR16 *Options, REFIT_VOLUME *DeviceVolume, IN EFI_DEVICE_PATH *DevicePath)
{
   EFI_STATUS efi_status;
   char *buffer;
   int i, size;
   EFI_IMAGE_SECTION_HEADER *Section;
   char *base, *end;
   GNUEFI_PE_COFF_LOADER_IMAGE_CONTEXT context;

   /*
    * The binary header contains relevant context and section pointers
    */
   efi_status = read_header(data, datasize, &context);
   if (efi_status != EFI_SUCCESS) {
      Print(L"Failed to read header\n");
      return efi_status;
   }

   /*
    * Validate the image; if this fails, return EFI_ACCESS_DENIED
    */
   if (!ShimValidate(data, datasize)) {
      return EFI_ACCESS_DENIED;
   }

   buffer = AllocatePool(context.ImageSize);

   if (!buffer) {
      Print(L"Failed to allocate image buffer\n");
      return EFI_OUT_OF_RESOURCES;
   }

   CopyMem(buffer, data, context.SizeOfHeaders);

   /*
    * Copy the executable's sections to their desired offsets
    */
   Section = context.FirstSection;
   for (i = 0; i < context.NumberOfSections; i++) {
      size = Section->Misc.VirtualSize;

      if (size > Section->SizeOfRawData)
         size = Section->SizeOfRawData;

      base = ImageAddress (buffer, context.ImageSize, Section->VirtualAddress);
      end = ImageAddress (buffer, context.ImageSize, Section->VirtualAddress + size - 1);

      if (!base || !end) {
         Print(L"Invalid section size\n");
         return EFI_UNSUPPORTED;
      }

      if (Section->SizeOfRawData > 0)
         CopyMem(base, data + Section->PointerToRawData, size);

      if (size < Section->Misc.VirtualSize)
         ZeroMem (base + size, Section->Misc.VirtualSize - size);

      Section += 1;
   }

   /*
    * Run the relocation fixups
    */
   efi_status = relocate_coff(&context, buffer);

   if (efi_status != EFI_SUCCESS) {
      Print(L"Relocation failed\n");
      FreePool(buffer);
      return efi_status;
   }

   entry_point = ImageAddress(buffer, context.ImageSize, context.EntryPoint);
   /*
    * grub needs to know its location and size in memory, its location on
    * disk, and its load options, so fix up the loaded image protocol values
    */
   li->DeviceHandle = DeviceVolume->DeviceHandle;
   li->FilePath = DevicePath;
   li->LoadOptionsSize = ((UINT32)StrLen(Options) + 1) * sizeof(CHAR16);
   li->LoadOptions = (VOID *)Options;
   li->ImageBase = buffer;
   li->ImageSize = context.ImageSize;

   if (!entry_point) {
      Print(L"Invalid entry point\n");
      FreePool(buffer);
      return EFI_UNSUPPORTED;
   }

   return EFI_SUCCESS;
}

#endif /* defined(EFIX64) */

/*
 * Load and run an EFI executable.
 * Note that most of this function compiles only on x86-64 (X64) systems, since
 * shim/MOK works only on those systems. I'm leaving just enough to get the
 * function to return EFI_ACCESS_DENIED on x86 (IA32) systems, which should
 * let the calling function work in case it somehow ends up calling this
 * function inappropriately.
 */
EFI_STATUS start_image(EFI_HANDLE image_handle, CHAR16 *ImagePath, VOID *data, UINTN datasize,
                       CHAR16 *Options, REFIT_VOLUME *DeviceVolume, IN EFI_DEVICE_PATH *DevicePath)
{
   EFI_STATUS efi_status = EFI_ACCESS_DENIED;
#if defined(EFIX64)
   EFI_GUID loaded_image_protocol = LOADED_IMAGE_PROTOCOL;
   EFI_LOADED_IMAGE *li, li_bak;
   CHAR16 *PathName = NULL;

   if (data == NULL)
      return EFI_LOAD_ERROR;

   /*
    * We need to refer to the loaded image protocol on the running
    * binary in order to find our path
    */
   efi_status = uefi_call_wrapper(BS->HandleProtocol, 3, image_handle, &loaded_image_protocol, (void **)&li);

   if (efi_status != EFI_SUCCESS) {
      Print(L"Unable to init protocol\n");
      return efi_status;
   }

   /*
    * We need to modify the loaded image protocol entry before running
    * the new binary, so back it up
    */
   CopyMem(&li_bak, li, sizeof(li_bak));

   /*
    * Verify and, if appropriate, relocate and execute the executable
    */
   efi_status = handle_image(data, datasize, li, Options, DeviceVolume, DevicePath);

   if (efi_status != EFI_SUCCESS) {
      Print(L"Failed to load image\n");
      CopyMem(li, &li_bak, sizeof(li_bak));
      goto done;
   }

   /*
    * The binary is trusted and relocated. Run it
    */
   efi_status = refit_call2_wrapper(entry_point, image_handle, ST);

//   efi_status = refit_call1_wrapper(BS->UnloadImage, li);

   /*
    * Restore our original loaded image values
    */
   CopyMem(li, &li_bak, sizeof(li_bak));
done:
   if (PathName)
      FreePool(PathName);

   if (data)
      FreePool(data);

#endif
   return efi_status;
} // EFI_STATUS start_image()

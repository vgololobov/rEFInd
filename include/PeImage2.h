/** @file
  EFI image format for PE32, PE32+ and TE. Please note some data structures are
  different for PE32 and PE32+. EFI_IMAGE_NT_HEADERS32 is for PE32 and
  EFI_IMAGE_NT_HEADERS64 is for PE32+.

  This file is coded to the Visual Studio, Microsoft Portable Executable and
  Common Object File Format Specification, Revision 8.0 - May 16, 2006.
  This file also includes some definitions in PI Specification, Revision 1.0.

Copyright (c) 2006 - 2010, Intel Corporation. All rights reserved.<BR>
Portions copyright (c) 2008 - 2009, Apple Inc. All rights reserved.<BR>
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php.

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

/*
 * Stripped down because the original file was flaking out on me and I just
 * needed the one definition....
 *
 */

#ifndef _PEIMAGE2_H_
#define _PEIMAGE2_H_


typedef struct _GNUEFI_PE_COFF_LOADER_IMAGE_CONTEXT {
   UINT64                            ImageAddress;
   UINT64                            ImageSize;
   UINT64                            EntryPoint; 
   UINTN                             SizeOfHeaders;
   UINT16                            ImageType;
   UINT16                            NumberOfSections;
   EFI_IMAGE_SECTION_HEADER          *FirstSection;
   EFI_IMAGE_DATA_DIRECTORY          *RelocDir;
   EFI_IMAGE_DATA_DIRECTORY          *SecDir;
   UINT64                            NumberOfRvaAndSizes;
   EFI_IMAGE_OPTIONAL_HEADER_UNION   *PEHdr;
} GNUEFI_PE_COFF_LOADER_IMAGE_CONTEXT;



//
// Return status codes from the PE/COFF Loader services
//
#define IMAGE_ERROR_SUCCESS                      0
#define IMAGE_ERROR_IMAGE_READ                   1
#define IMAGE_ERROR_INVALID_PE_HEADER_SIGNATURE  2
#define IMAGE_ERROR_INVALID_MACHINE_TYPE         3
#define IMAGE_ERROR_INVALID_SUBSYSTEM            4
#define IMAGE_ERROR_INVALID_IMAGE_ADDRESS        5
#define IMAGE_ERROR_INVALID_IMAGE_SIZE           6
#define IMAGE_ERROR_INVALID_SECTION_ALIGNMENT    7
#define IMAGE_ERROR_SECTION_NOT_LOADED           8
#define IMAGE_ERROR_FAILED_RELOCATION            9
#define IMAGE_ERROR_FAILED_ICACHE_FLUSH          10

#ifdef __MAKEWITH_GNUEFI
#define RETURN_STATUS EFI_STATUS
typedef UINT64 PHYSICAL_ADDRESS;
#endif

#endif

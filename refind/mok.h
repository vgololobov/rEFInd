#include "../include/PeImage.h"

#define SHIM_LOCK_GUID \
   { 0x605dab50, 0xe046, 0x4300, {0xab, 0xb6, 0x3d, 0xd8, 0x10, 0xdd, 0x8b, 0x23} }

//#define INTERFACE_DECL(x)

// INTERFACE_DECL(_SHIM_LOCK);
// 
// typedef
// EFI_STATUS
// (*EFI_SHIM_LOCK_VERIFY) (
//    IN VOID *buffer,
//    IN UINT32 size
//    );
// 
// typedef
// EFI_STATUS
// (*EFI_SHIM_LOCK_HASH) (
//    IN char *data,
//    IN int datasize,
//    GNUEFI_PE_COFF_LOADER_IMAGE_CONTEXT *context,
//    UINT8 *sha256hash,
//    UINT8 *sha1hash
//    );
// 
// typedef
// EFI_STATUS
// (*EFI_SHIM_LOCK_CONTEXT) (
//    IN VOID *data,
//    IN unsigned int datasize,
//    GNUEFI_PE_COFF_LOADER_IMAGE_CONTEXT *context
//    );
// 
// typedef struct _SHIM_LOCK {
//    EFI_SHIM_LOCK_VERIFY Verify;
//    EFI_SHIM_LOCK_HASH Hash;
//    EFI_SHIM_LOCK_CONTEXT Context;
// } SHIM_LOCK;

typedef struct _SHIM_LOCK
{
   EFI_STATUS __attribute__((sysv_abi)) (*shim_verify) (VOID *buffer, UINT32 size);
   EFI_STATUS __attribute__((sysv_abi)) (*generate_hash) (char *data, int datasize,
                                                          GNUEFI_PE_COFF_LOADER_IMAGE_CONTEXT *context, UINT8 *sha256hash,
                                                          UINT8 *sha1hash);
   EFI_STATUS __attribute__((sysv_abi)) (*read_header) (void *data, unsigned int datasize,
                                                        GNUEFI_PE_COFF_LOADER_IMAGE_CONTEXT *context);
} SHIM_LOCK;

EFI_STATUS start_image(EFI_HANDLE image_handle, CHAR16 *ImagePath, VOID *data, UINTN datasize,
                       CHAR16 *Options, REFIT_VOLUME *DeviceVolume);

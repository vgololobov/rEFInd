#include "../include/PeImage.h"
#include "../include/PeImage2.h"

#define SHIM_LOCK_GUID \
   { 0x605dab50, 0xe046, 0x4300, {0xab, 0xb6, 0x3d, 0xd8, 0x10, 0xdd, 0x8b, 0x23} }

#if defined(EFIX64)

typedef struct _SHIM_LOCK
{
   EFI_STATUS __attribute__((sysv_abi)) (*shim_verify) (VOID *buffer, UINT32 size);
   EFI_STATUS __attribute__((sysv_abi)) (*generate_hash) (char *data, int datasize,
                                                          GNUEFI_PE_COFF_LOADER_IMAGE_CONTEXT *context, UINT8 *sha256hash,
                                                          UINT8 *sha1hash);
   EFI_STATUS __attribute__((sysv_abi)) (*read_header) (void *data, unsigned int datasize,
                                                        GNUEFI_PE_COFF_LOADER_IMAGE_CONTEXT *context);
} SHIM_LOCK;

#endif

BOOLEAN secure_mode (VOID);
EFI_STATUS start_image(EFI_HANDLE image_handle, CHAR16 *ImagePath, VOID *data, UINTN datasize,
                       CHAR16 *Options, REFIT_VOLUME *DeviceVolume, IN EFI_DEVICE_PATH *DevicePath);

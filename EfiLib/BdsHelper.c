/*
 * EfiLib/BdsHelper.c
 * Functions to call legacy BIOS API.
 *
 */


#include "BdsHelper.h"

EFI_GUID gEfiLegacyBootProtocolGuid     = { 0xdb9a1e3d, 0x45cb, 0x4abb, { 0x85, 0x3b, 0xe5, 0x38, 0x7f, 0xdb, 0x2e, 0x2d }};

/**
    Internal helper function.

    Update the BBS Table so that devices of DeviceType have their boot priority
    updated to a high/bootable value.

    See "DeviceType values" in 
    http://www.intel.com/content/dam/doc/reference-guide/efi-compatibility-support-module-specification-v097.pdf

    NOTE: This function should probably be refactored! Currently, all devices of
    type are enabled. This should be updated so that only a specific device is
    enabled. The wrong device could boot if there are multiple targets of the same
    type.

    @param DeviceType   The device type that we wish to enable
**/
VOID
UpdateBbsTable (
  IN UINT16     DeviceType
  )
{
    UINT16  Idx;
    EFI_LEGACY_BIOS_PROTOCOL  *LegacyBios;
//    EFI_GUID EfiLegacyBootProtocolGuid     = { 0xdb9a1e3d, 0x45cb, 0x4abb, { 0x85, 0x3b, 0xe5, 0x38, 0x7f, 0xdb, 0x2e, 0x2d }};
    EFI_STATUS                Status;
    UINT16                       HddCount = 0;
    HDD_INFO                     *HddInfo = NULL; 
    UINT16                       BbsCount = 0; 
    BBS_TABLE                 *LocalBbsTable = NULL;

    Status = gBS->LocateProtocol (&gEfiLegacyBootProtocolGuid, NULL, (VOID **) &LegacyBios);
    if (EFI_ERROR (Status)) {
      return;
    }

    Status = LegacyBios->GetBbsInfo (LegacyBios, &HddCount, &HddInfo, &BbsCount, &LocalBbsTable);

    //Print (L"\n");
    //Print (L" NO  Prio bb/dd/ff cl/sc Type Stat segm:offs\n");
    //Print (L"=============================================\n");

    for (Idx = 0; Idx < BbsCount; Idx++) 
    {
     if(LocalBbsTable[Idx].DeviceType == 0){
        continue;
     }

     // Set devices of a particular type to BootPriority of 0. I believe 0 is the highest priority
     if(LocalBbsTable[Idx].DeviceType == DeviceType){
        LocalBbsTable[Idx].BootPriority = 0;
     }

/*
      Print (
        L" %02x: %04x %02x/%02x/%02x %02x/%02x %04x %04x %04x:%04x\n",
        (UINTN) Idx,
        (UINTN) LocalBbsTable[Idx].BootPriority,
        (UINTN) LocalBbsTable[Idx].Bus,
        (UINTN) LocalBbsTable[Idx].Device,
        (UINTN) LocalBbsTable[Idx].Function,
        (UINTN) LocalBbsTable[Idx].Class,
        (UINTN) LocalBbsTable[Idx].SubClass,
        (UINTN) LocalBbsTable[Idx].DeviceType,
        (UINTN) * (UINT16 *) &LocalBbsTable[Idx].StatusFlags,
        (UINTN) LocalBbsTable[Idx].BootHandlerSegment,
        (UINTN) LocalBbsTable[Idx].BootHandlerOffset,
        (UINTN) ((LocalBbsTable[Idx].MfgStringSegment << 4) + LocalBbsTable[Idx].MfgStringOffset),
        (UINTN) ((LocalBbsTable[Idx].DescStringSegment << 4) + LocalBbsTable[Idx].DescStringOffset)
        );
*/
    }

  //Print(L"\n");
}

// Internal helper function
BOOLEAN ArrayContains(UINT16* Arr, UINT8 ArrLen, UINT8 Target)
{
    UINT8 i;
    for(i = 0; i < ArrLen; i++)
    {
       if(Arr[i] == Target)
          return TRUE;
    }

  return FALSE;
}

/**
    Boot the legacy system with the boot option

    @param  Option                 The legacy boot option which have BBS device path

    @retval EFI_UNSUPPORTED        There is no legacybios protocol, do not support
                                 legacy boot.
    @retval EFI_STATUS             Return the status of LegacyBios->LegacyBoot ().

**/
EFI_STATUS
BdsLibDoLegacyBoot (
  IN  BDS_COMMON_OPTION           *Option
  )
{
    EFI_STATUS                Status;
    EFI_LEGACY_BIOS_PROTOCOL  *LegacyBios;
    BBS_BBS_DEVICE_PATH *OptionBBS;

    Status = gBS->LocateProtocol (&gEfiLegacyBootProtocolGuid, NULL, (VOID **) &LegacyBios);
    if (EFI_ERROR (Status)) {
      return EFI_UNSUPPORTED;
    }
    OptionBBS = (BBS_BBS_DEVICE_PATH *) Option->DevicePath;

    //Print(L"\n\n");
    //Print(L"Option->Name='%s'\n", Option->OptionName);
    //Print(L"Option->Number='%d'\n", Option->OptionNumber);
    //Print(L"Option->Description='%s'\n", Option->Description);
    //Print(L"Option->LoadOptionsSize='%d'\n",Option->LoadOptionsSize);
    //Print(L"Option->BootCurrent='%d'\n",Option->BootCurrent);
    //Print(L"Option->DevicePath->Type= '%d'\n", Option->DevicePath->Type);
    //Print(L"Option->DevicePath->SubType= '%d'\n", Option->DevicePath->SubType);
    //Print(L"Option->DevicePath->Length[0]= '%d'\n", Option->DevicePath->Length[0]);
    //Print(L"Option->DevicePath->Length[1]= '%d'\n", Option->DevicePath->Length[1]); 
    //Print(L"OptionBBS->DeviceType='%d'\n",OptionBBS->DeviceType);
    //Print(L"OptionBBS->StatusFlag='%d'\n",OptionBBS->StatusFlag);
    //Print(L"OptionBBS->String[0]='%c'\n",OptionBBS->String[0]);
    //Print(L"About to legacy boot!\n");
    //PauseForKey();

    UpdateBbsTable(OptionBBS->DeviceType); 

    return LegacyBios->LegacyBoot (LegacyBios, (BBS_BBS_DEVICE_PATH *) Option->DevicePath, Option->LoadOptionsSize, Option->LoadOptions);
}


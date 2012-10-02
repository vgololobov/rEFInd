/*
 * EfiLib/BdsHelper.c
 * Functions to call legacy BIOS API.
 *
 */

#include "../include/tiano_includes.h"

#ifndef _BDS_HELPER_H_
#define _BDS_HELPER_H_


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
  );

#endif //_BDS_HELPER_H_

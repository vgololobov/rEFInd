/*
 * libeg/screen.c
 * Screen handling functions
 *
 * Copyright (c) 2006 Christoph Pfisterer
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

#include "libegint.h"
#include "../refind/screen.h"
#include "../refind/lib.h"
#include "../include/refit_call_wrapper.h"

#include <efiUgaDraw.h>
#include <efiConsoleControl.h>

#ifndef __MAKEWITH_GNUEFI
#define LibLocateProtocol EfiLibLocateProtocol
#endif

// Console defines and variables

static EFI_GUID ConsoleControlProtocolGuid = EFI_CONSOLE_CONTROL_PROTOCOL_GUID;
static EFI_CONSOLE_CONTROL_PROTOCOL *ConsoleControl = NULL;

static EFI_GUID UgaDrawProtocolGuid = EFI_UGA_DRAW_PROTOCOL_GUID;
static EFI_UGA_DRAW_PROTOCOL *UgaDraw = NULL;

static EFI_GUID GraphicsOutputProtocolGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static EFI_GRAPHICS_OUTPUT_PROTOCOL *GraphicsOutput = NULL;

static BOOLEAN egHasGraphics = FALSE;
static UINTN egScreenWidth  = 800;
static UINTN egScreenHeight = 600;

//
// Screen handling
//

VOID egInitScreen(VOID)
{
    EFI_STATUS Status = EFI_SUCCESS;
    UINT32 UGAWidth, UGAHeight, UGADepth, UGARefreshRate;

    // get protocols
    Status = LibLocateProtocol(&ConsoleControlProtocolGuid, (VOID **) &ConsoleControl);
    if (EFI_ERROR(Status))
        ConsoleControl = NULL;

    Status = LibLocateProtocol(&UgaDrawProtocolGuid, (VOID **) &UgaDraw);
    if (EFI_ERROR(Status))
        UgaDraw = NULL;

    Status = LibLocateProtocol(&GraphicsOutputProtocolGuid, (VOID **) &GraphicsOutput);
    if (EFI_ERROR(Status))
        GraphicsOutput = NULL;

    // get screen size
    egHasGraphics = FALSE;
    if (GraphicsOutput != NULL) {
        egScreenWidth = GraphicsOutput->Mode->Info->HorizontalResolution;
        egScreenHeight = GraphicsOutput->Mode->Info->VerticalResolution;
        egHasGraphics = TRUE;
    } else if (UgaDraw != NULL) {
        Status = refit_call5_wrapper(UgaDraw->GetMode, UgaDraw, &UGAWidth, &UGAHeight, &UGADepth, &UGARefreshRate);
        if (EFI_ERROR(Status)) {
            UgaDraw = NULL;   // graphics not available
        } else {
            egScreenWidth  = UGAWidth;
            egScreenHeight = UGAHeight;
            egHasGraphics = TRUE;
        }
    }
}

// Sets the screen resolution to the specified value, if possible. If *ScreenHeight
// is 0 and GOP mode is detected, assume that *ScreenWidth contains a GOP mode
// number rather than a horizontal resolution. If the specified resolution is not
// valid, displays a warning with the valid modes on GOP (UEFI) systems, or silently
// fails on UGA (EFI 1.x) systems. Note that this function attempts to set ANY screen
// resolution, even 0x0 or ridiculously large values.
// Upon success, returns actual screen resolution in *ScreenWidth and *ScreenHeight.
// These values are unchanged upon failure.
// Returns TRUE if successful, FALSE if not.
BOOLEAN egSetScreenSize(IN OUT UINTN *ScreenWidth, IN OUT UINTN *ScreenHeight) {
   EFI_STATUS                            Status = EFI_SUCCESS;
   EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *Info;
   UINT32                                ModeNum = 0;
   UINTN                                 Size;
   BOOLEAN                               ModeSet = FALSE;
   UINT32                                UGAWidth, UGAHeight, UGADepth, UGARefreshRate;

   if (GraphicsOutput != NULL) { // GOP mode (UEFI)
      if (*ScreenHeight == 0) { // User specified a mode number (stored in *ScreenWidth); use it directly
         ModeNum = (UINT32) *ScreenWidth;
         Status = refit_call4_wrapper(GraphicsOutput->QueryMode, GraphicsOutput, ModeNum, &Size, &Info);
         if (Status == EFI_SUCCESS) {
            Status = refit_call2_wrapper(GraphicsOutput->SetMode, GraphicsOutput, ModeNum);
            if (Status == EFI_SUCCESS) {
               ModeSet = TRUE;
               *ScreenWidth = Info->HorizontalResolution;
               *ScreenHeight = Info->VerticalResolution;
            }
         }
      } else { // User specified width & height; must find mode
         // Do a loop through the modes to see if the specified one is available;
         // and if so, switch to it....
         while ((Status == EFI_SUCCESS) && (!ModeSet)) {
            Status = refit_call4_wrapper(GraphicsOutput->QueryMode, GraphicsOutput, ModeNum, &Size, &Info);
            if ((Status == EFI_SUCCESS) && (Size >= sizeof(*Info)) &&
                (Info->HorizontalResolution == *ScreenWidth) && (Info->VerticalResolution == *ScreenHeight)) {
               Status = refit_call2_wrapper(GraphicsOutput->SetMode, GraphicsOutput, ModeNum);
               ModeSet = (Status == EFI_SUCCESS);
            } // if
            ModeNum++;
         } // while()
      } // if/else

      if (ModeSet) {
         egScreenWidth = *ScreenWidth;
         egScreenHeight = *ScreenHeight;
      } else {// If unsuccessful, display an error message for the user....
         SwitchToText(FALSE);
         Print(L"Error setting graphics mode %d x %d; using default mode!\nAvailable modes are:\n", *ScreenWidth, *ScreenHeight);
         ModeNum = 0;
         do {
            Status = refit_call4_wrapper(GraphicsOutput->QueryMode, GraphicsOutput, ModeNum, &Size, &Info);
            if ((Status == EFI_SUCCESS) && (Info != NULL)) {
               Print(L"Mode %d: %d x %d\n", ModeNum, Info->HorizontalResolution, Info->VerticalResolution);
            } // else
         } while ((ModeNum++ < 10) || (Status == EFI_SUCCESS));
//          Status = EFI_SUCCESS;
//          while (Status == EFI_SUCCESS) {
//             Status = refit_call4_wrapper(GraphicsOutput->QueryMode, GraphicsOutput, ModeNum, &Size, &Info);
//             if ((Status == EFI_SUCCESS) && (Size >= sizeof(*Info))) {
//                Print(L"Mode %d: %d x %d\n", ModeNum, Info->HorizontalResolution, Info->VerticalResolution);
//             } // else
//             ModeNum++;
//          } // while()
         PauseForKey();
         SwitchToGraphics();
      } // if()
   } else if (UgaDraw != NULL) { // UGA mode (EFI 1.x)
      // Try to use current color depth & refresh rate for new mode. Maybe not the best choice
      // in all cases, but I don't know how to probe for alternatives....
      Status = refit_call5_wrapper(UgaDraw->GetMode, UgaDraw, &UGAWidth, &UGAHeight, &UGADepth, &UGARefreshRate);
      Status = refit_call5_wrapper(UgaDraw->SetMode, UgaDraw, *ScreenWidth, *ScreenHeight, UGADepth, UGARefreshRate);
      if (Status == EFI_SUCCESS) {
         egScreenWidth = *ScreenWidth;
         egScreenHeight = *ScreenHeight;
         ModeSet = TRUE;
      } else {
         // TODO: Find a list of supported modes and display it.
         // NOTE: Below doesn't actually appear unless we explicitly switch to text mode.
         // This is just a placeholder until something better can be done....
         Print(L"Error setting graphics mode %d x %d; unsupported mode!\n");
      } // if/else
   } // if/else if
   return (ModeSet);
} // BOOLEAN egSetScreenSize()

VOID egGetScreenSize(OUT UINTN *ScreenWidth, OUT UINTN *ScreenHeight)
{
    if (ScreenWidth != NULL)
        *ScreenWidth = egScreenWidth;
    if (ScreenHeight != NULL)
        *ScreenHeight = egScreenHeight;
}

// Set a text mode
// Returns the mode that was actually set.
UINT32 egSetTextMode(UINT32 RequestedMode) {
   UINTN         i = 0, Width, Height;
   UINT32        UsedMode = ST->ConOut->Mode->Mode;
   EFI_STATUS    Status;

   if (RequestedMode != ST->ConOut->Mode->Mode) {
      Status = refit_call2_wrapper(ST->ConOut->SetMode, ST->ConOut, RequestedMode);
      if (Status == EFI_SUCCESS) {
         UsedMode = RequestedMode;
      } else {
         SwitchToText(FALSE);
         Print(L"Error setting text mode %d; available modes are:\n", RequestedMode);
         do {
            Status = refit_call4_wrapper(ST->ConOut->QueryMode, ST->ConOut, i, &Width, &Height);
            if (Status == EFI_SUCCESS)
               Print(L"Mode: %d: %d x %d\n", i, Width, Height);
         } while ((i++ < 2) || (Status == EFI_SUCCESS));

         PauseForKey();
         SwitchToGraphics();
      } // if/else successful change
   } // if need to change mode
   return UsedMode;
} // UINT32 egSetTextMode()

CHAR16 * egScreenDescription(VOID)
{
    CHAR16 *GraphicsInfo, *TextInfo = NULL;

    GraphicsInfo = AllocateZeroPool(256 * sizeof(CHAR16));
    if (GraphicsInfo == NULL)
       return L"memory allocation error";

    if (egHasGraphics) {
        if (GraphicsOutput != NULL) {
            SPrint(GraphicsInfo, 255, L"Graphics Output (UEFI), %dx%d", egScreenWidth, egScreenHeight);
        } else if (UgaDraw != NULL) {
            GraphicsInfo = AllocateZeroPool(256 * sizeof(CHAR16));
            SPrint(GraphicsInfo, 255, L"UGA Draw (EFI 1.10), %dx%d", egScreenWidth, egScreenHeight);
        } else {
            MyFreePool(GraphicsInfo);
            MyFreePool(TextInfo);
            return L"Internal Error";
        }
        if (!AllowGraphicsMode) { // graphics-capable HW, but in text mode
           TextInfo = AllocateZeroPool(256 * sizeof(CHAR16));
           SPrint(TextInfo, 255, L"(in %dx%d text mode)", ConWidth, ConHeight);
           MergeStrings(&GraphicsInfo, TextInfo, L' ');
        }
    } else {
        SPrint(GraphicsInfo, 255, L"Text-foo console, %dx%d", ConWidth, ConHeight);
    }
    MyFreePool(TextInfo);
    return GraphicsInfo;
}

BOOLEAN egHasGraphicsMode(VOID)
{
    return egHasGraphics;
}

BOOLEAN egIsGraphicsModeEnabled(VOID)
{
    EFI_CONSOLE_CONTROL_SCREEN_MODE CurrentMode;

    if (ConsoleControl != NULL) {
        refit_call4_wrapper(ConsoleControl->GetMode, ConsoleControl, &CurrentMode, NULL, NULL);
        return (CurrentMode == EfiConsoleControlScreenGraphics) ? TRUE : FALSE;
    }

    return FALSE;
}

VOID egSetGraphicsModeEnabled(IN BOOLEAN Enable)
{
    EFI_CONSOLE_CONTROL_SCREEN_MODE CurrentMode;
    EFI_CONSOLE_CONTROL_SCREEN_MODE NewMode;

    if (ConsoleControl != NULL) {
        refit_call4_wrapper(ConsoleControl->GetMode, ConsoleControl, &CurrentMode, NULL, NULL);

        NewMode = Enable ? EfiConsoleControlScreenGraphics
                         : EfiConsoleControlScreenText;
        if (CurrentMode != NewMode)
           refit_call2_wrapper(ConsoleControl->SetMode, ConsoleControl, NewMode);
    }
}

//
// Drawing to the screen
//

VOID egClearScreen(IN EG_PIXEL *Color)
{
    EFI_UGA_PIXEL FillColor;

    if (!egHasGraphics)
        return;

    FillColor.Red   = Color->r;
    FillColor.Green = Color->g;
    FillColor.Blue  = Color->b;
    FillColor.Reserved = 0;

    if (GraphicsOutput != NULL) {
        // EFI_GRAPHICS_OUTPUT_BLT_PIXEL and EFI_UGA_PIXEL have the same
        // layout, and the header from TianoCore actually defines them
        // to be the same type.
        refit_call10_wrapper(GraphicsOutput->Blt, GraphicsOutput, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)&FillColor, EfiBltVideoFill,
                            0, 0, 0, 0, egScreenWidth, egScreenHeight, 0);
    } else if (UgaDraw != NULL) {
        refit_call10_wrapper(UgaDraw->Blt, UgaDraw, &FillColor, EfiUgaVideoFill,
                     0, 0, 0, 0, egScreenWidth, egScreenHeight, 0);
    }
}

VOID egDrawImage(IN EG_IMAGE *Image, IN UINTN ScreenPosX, IN UINTN ScreenPosY)
{
    if (!egHasGraphics)
        return;

    if (Image->HasAlpha) {
        Image->HasAlpha = FALSE;
        egSetPlane(PLPTR(Image, a), 0, Image->Width * Image->Height);
    }

    if (GraphicsOutput != NULL) {
        refit_call10_wrapper(GraphicsOutput->Blt, GraphicsOutput, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)Image->PixelData, EfiBltBufferToVideo,
                            0, 0, ScreenPosX, ScreenPosY, Image->Width, Image->Height, 0);
    } else if (UgaDraw != NULL) {
        refit_call10_wrapper(UgaDraw->Blt, UgaDraw, (EFI_UGA_PIXEL *)Image->PixelData, EfiUgaBltBufferToVideo,
                     0, 0, ScreenPosX, ScreenPosY, Image->Width, Image->Height, 0);
    }
}

VOID egDrawImageArea(IN EG_IMAGE *Image,
                     IN UINTN AreaPosX, IN UINTN AreaPosY,
                     IN UINTN AreaWidth, IN UINTN AreaHeight,
                     IN UINTN ScreenPosX, IN UINTN ScreenPosY)
{
    if (!egHasGraphics)
        return;

    egRestrictImageArea(Image, AreaPosX, AreaPosY, &AreaWidth, &AreaHeight);
    if (AreaWidth == 0)
        return;

    if (Image->HasAlpha) {
        Image->HasAlpha = FALSE;
        egSetPlane(PLPTR(Image, a), 0, Image->Width * Image->Height);
    }

    if (GraphicsOutput != NULL) {
        refit_call10_wrapper(GraphicsOutput->Blt, GraphicsOutput, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)Image->PixelData, EfiBltBufferToVideo,
                            AreaPosX, AreaPosY, ScreenPosX, ScreenPosY, AreaWidth, AreaHeight, Image->Width * 4);
    } else if (UgaDraw != NULL) {
        refit_call10_wrapper(UgaDraw->Blt, UgaDraw, (EFI_UGA_PIXEL *)Image->PixelData, EfiUgaBltBufferToVideo,
                     AreaPosX, AreaPosY, ScreenPosX, ScreenPosY, AreaWidth, AreaHeight, Image->Width * 4);
    }
}

// Display a message in the center of the screen, surrounded by a box of the
// specified color. For the moment, uses graphics calls only. (It still works
// in text mode on GOP/UEFI systems, but not on UGA/EFI 1.x systems.)
VOID egDisplayMessage(IN CHAR16 *Text, EG_PIXEL *BGColor) {
   UINTN BoxWidth, BoxHeight;
   EG_IMAGE *Box;

   if ((Text != NULL) && (BGColor != NULL)) {
      BoxWidth = (StrLen(Text) + 2) * FONT_CELL_WIDTH;
      if (BoxWidth > egScreenWidth)
         BoxWidth = egScreenWidth;
      BoxHeight = 2 * FONT_CELL_HEIGHT;
      Box = egCreateFilledImage(BoxWidth, BoxHeight, FALSE, BGColor);
      egRenderText(Text, Box, FONT_CELL_WIDTH, FONT_CELL_HEIGHT / 2);
      egDrawImage(Box, (egScreenWidth - BoxWidth) / 2, (egScreenHeight - BoxHeight) / 2);
   } // if non-NULL inputs
} // VOID egDisplayMessage()

//
// Make a screenshot
//

VOID egScreenShot(VOID)
{
    EFI_STATUS      Status;
    EG_IMAGE        *Image;
    UINT8           *FileData;
    UINTN           FileDataLength;
    UINTN           Index;

    if (!egHasGraphics)
        return;

    // allocate a buffer for the whole screen
    Image = egCreateImage(egScreenWidth, egScreenHeight, FALSE);
    if (Image == NULL) {
        Print(L"Error egCreateImage returned NULL\n");
        goto bailout_wait;
    }

    // get full screen image
    if (GraphicsOutput != NULL) {
        refit_call10_wrapper(GraphicsOutput->Blt, GraphicsOutput, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)Image->PixelData, EfiBltVideoToBltBuffer,
                            0, 0, 0, 0, Image->Width, Image->Height, 0);
    } else if (UgaDraw != NULL) {
        refit_call10_wrapper(UgaDraw->Blt, UgaDraw, (EFI_UGA_PIXEL *)Image->PixelData, EfiUgaVideoToBltBuffer,
                     0, 0, 0, 0, Image->Width, Image->Height, 0);
    }

    // encode as BMP
    egEncodeBMP(Image, &FileData, &FileDataLength);
    egFreeImage(Image);
    if (FileData == NULL) {
        Print(L"Error egEncodeBMP returned NULL\n");
        goto bailout_wait;
    }

    // save to file on the ESP
    Status = egSaveFile(NULL, L"screenshot.bmp", FileData, FileDataLength);
    FreePool(FileData);
    if (EFI_ERROR(Status)) {
        Print(L"Error egSaveFile: %x\n", Status);
        goto bailout_wait;
    }

    return;

    // DEBUG: switch to text mode
bailout_wait:
    egSetGraphicsModeEnabled(FALSE);
    refit_call3_wrapper(BS->WaitForEvent, 1, &ST->ConIn->WaitForKey, &Index);
}

/* EOF */

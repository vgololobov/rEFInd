/*
 * refit/icns.c
 * Loader for .icns icon files
 *
 * Copyright (c) 2006-2007 Christoph Pfisterer
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

#include "global.h"
#include "lib.h"
#include "icns.h"
#include "config.h"
#include "../refind/screen.h"

//
// well-known icons
//

typedef struct {
    EG_IMAGE    *Image;
    CHAR16      *FileName;
    UINTN       PixelSize;
} BUILTIN_ICON;

BUILTIN_ICON BuiltinIconTable[BUILTIN_ICON_COUNT] = {
   { NULL, L"func_about.icns", 48 },
   { NULL, L"func_reset.icns", 48 },
   { NULL, L"func_shutdown.icns", 48 },
   { NULL, L"func_exit.icns", 48 },
   { NULL, L"tool_shell.icns", 48 },
   { NULL, L"tool_part.icns", 48 },
   { NULL, L"tool_rescue.icns", 48 },
   { NULL, L"tool_apple_rescue.icns", 48 },
   { NULL, L"tool_mok_tool.icns", 48 },
   { NULL, L"vol_internal.icns", 32 },
   { NULL, L"vol_external.icns", 32 },
   { NULL, L"vol_optical.icns", 32 },
};

EG_IMAGE * BuiltinIcon(IN UINTN Id)
{
    CHAR16 FileName[256];

    if (Id >= BUILTIN_ICON_COUNT)
        return NULL;

    if (BuiltinIconTable[Id].Image == NULL) {
        SPrint(FileName, 255, L"%s\\%s", GlobalConfig.IconsDir ? GlobalConfig.IconsDir : DEFAULT_ICONS_DIR,
               BuiltinIconTable[Id].FileName);
        BuiltinIconTable[Id].Image = LoadIcnsFallback(SelfDir, FileName, BuiltinIconTable[Id].PixelSize);
    } // if

    return BuiltinIconTable[Id].Image;
}

// Construct filename for all valid icon extensions and try to load it.
// Returns image data or NULL if it can't be found.
static EG_IMAGE * LoadIconAnyFormat(IN CHAR16 *IconsDir, IN CHAR16 *BaseName, IN CHAR16 *OsName) {
   CHAR16     FileName[256];
   CHAR16     *Extension;
   EG_IMAGE   *Image = NULL;
   UINTN      Index = 0;

   while (((Extension = FindCommaDelimited(ICON_EXTENSIONS, Index++)) != NULL) && (Image == NULL)) {
      SPrint(FileName, 255, L"%s\\%s_%s.%s", IconsDir, BaseName, OsName, Extension);

      // try to load it
      Image = egLoadIcon(SelfDir, FileName, 128);
      MyFreePool(Extension);
   } // while()
   return Image;
} // EG_IMAGE * LoadIconAnyFormat()

//
// Load an icon for an operating system
//

// Load an OS icon from among the comma-delimited list provided in OSIconName.
// Searches for icons with extensions in the ICON_EXTENSIONS list (via
// LoadIconAnyFormat()).
// Returns image data. On failure, returns an ugly "dummy" icon.
EG_IMAGE * LoadOSIcon(IN CHAR16 *OSIconName OPTIONAL, IN CHAR16 *FallbackIconName, BOOLEAN BootLogo)
{
    EG_IMAGE        *Image = NULL;
    CHAR16          *CutoutName;
    UINTN           Index = 0;

    if (GlobalConfig.TextOnly)      // skip loading if it's not used anyway
        return NULL;

    // try the names from OSIconName
    while (((CutoutName = FindCommaDelimited(OSIconName, Index++)) != NULL) && (Image == NULL)) {
       Image = LoadIconAnyFormat(GlobalConfig.IconsDir ? GlobalConfig.IconsDir : DEFAULT_ICONS_DIR,
                                 BootLogo ? L"boot" : L"os", CutoutName);
       MyFreePool(CutoutName);
    } // while()

    // try the fallback name
    if (Image == NULL)
       Image = LoadIconAnyFormat(GlobalConfig.IconsDir ? GlobalConfig.IconsDir : DEFAULT_ICONS_DIR,
                                 BootLogo ? L"boot" : L"os", FallbackIconName);

    // try the fallback name with os_ instead of boot_
    if (BootLogo && (Image == NULL))
       Image = LoadIconAnyFormat(GlobalConfig.IconsDir ? GlobalConfig.IconsDir : DEFAULT_ICONS_DIR,
                                 L"os", FallbackIconName);

    if (Image != NULL)
        return Image;
    else
       return DummyImage(128);
} /* EG_IMAGE * LoadOSIcon() */


// Load an image from a .icns or .png file. FileNames contains a comma-delimited
// list of potential filanems; the function tries each in turn until it finds
// a loadable icon. If no file has a valid icon, it returns NULL.
EG_IMAGE * LoadIcns(IN EFI_FILE_HANDLE BaseDir, IN CHAR16 *FileNames, IN UINTN PixelSize) {
   CHAR16   *FileName;
   EG_IMAGE *Image = NULL;
   UINTN    Index = 0;

   if (GlobalConfig.TextOnly)      // skip loading if it's not used anyway
      return NULL;

   while (((FileName = FindCommaDelimited(FileNames, Index++)) != NULL) && (Image == NULL)) {
      Image = egLoadIcon(BaseDir, FileName, PixelSize);
   }
   return Image;
} // EG_IMAGE * LoadIcns()

static EG_PIXEL BlackPixel  = { 0x00, 0x00, 0x00, 0 };
//static EG_PIXEL YellowPixel = { 0x00, 0xff, 0xff, 0 };

EG_IMAGE * DummyImage(IN UINTN PixelSize)
{
    EG_IMAGE        *Image;
    UINTN           x, y, LineOffset;
    CHAR8           *Ptr, *YPtr;

    Image = egCreateFilledImage(PixelSize, PixelSize, TRUE, &BlackPixel);

    LineOffset = PixelSize * 4;

    YPtr = (CHAR8 *)Image->PixelData + ((PixelSize - 32) >> 1) * (LineOffset + 4);
    for (y = 0; y < 32; y++) {
        Ptr = YPtr;
        for (x = 0; x < 32; x++) {
            if (((x + y) % 12) < 6) {
                *Ptr++ = 0;
                *Ptr++ = 0;
                *Ptr++ = 0;
            } else {
                *Ptr++ = 0;
                *Ptr++ = 255;
                *Ptr++ = 255;
            }
            *Ptr++ = 144;
        }
        YPtr += LineOffset;
    }

    return Image;
}

EG_IMAGE * LoadIcnsFallback(IN EFI_FILE_HANDLE BaseDir, IN CHAR16 *FileName, IN UINTN PixelSize)
{
    EG_IMAGE *Image;
    if (GlobalConfig.TextOnly)      // skip loading if it's not used anyway
        return NULL;

    Image = LoadIcns(BaseDir, FileName, PixelSize);
    if (Image == NULL) {
        Image = DummyImage(PixelSize);
    }
    return Image;
}

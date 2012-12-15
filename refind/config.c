/*
 * refit/config.c
 * Configuration file functions
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
#include "menu.h"
#include "config.h"
#include "screen.h"
#include "../include/refit_call_wrapper.h"

// constants

#define LINUX_OPTIONS_FILENAMES  L"refind_linux.conf,refind-linux.conf"
#define MAXCONFIGFILESIZE        (128*1024)

#define ENCODING_ISO8859_1  (0)
#define ENCODING_UTF8       (1)
#define ENCODING_UTF16_LE   (2)

static REFIT_MENU_ENTRY MenuEntryReturn   = { L"Return to Main Menu", TAG_RETURN, 0, 0, 0, NULL, NULL, NULL };

//
// read a file into a buffer
//

EFI_STATUS ReadFile(IN EFI_FILE_HANDLE BaseDir, IN CHAR16 *FileName, IN OUT REFIT_FILE *File, OUT UINTN *size)
{
    EFI_STATUS      Status;
    EFI_FILE_HANDLE FileHandle;
    EFI_FILE_INFO   *FileInfo;
    UINT64          ReadSize;
    CHAR16          Message[256];

    File->Buffer = NULL;
    File->BufferSize = 0;

    // read the file, allocating a buffer on the way
    Status = refit_call5_wrapper(BaseDir->Open, BaseDir, &FileHandle, FileName, EFI_FILE_MODE_READ, 0);
    SPrint(Message, 255, L"while loading the file '%s'", FileName);
    if (CheckError(Status, Message))
        return Status;

    FileInfo = LibFileInfo(FileHandle);
    if (FileInfo == NULL) {
        // TODO: print and register the error
        refit_call1_wrapper(FileHandle->Close, FileHandle);
        return EFI_LOAD_ERROR;
    }
    ReadSize = FileInfo->FileSize;
    FreePool(FileInfo);

    File->BufferSize = (UINTN)ReadSize;
    File->Buffer = AllocatePool(File->BufferSize);
    if (File->Buffer == NULL) {
       size = 0;
       return EFI_OUT_OF_RESOURCES;
    } else {
       *size = File->BufferSize;
    } // if/else
    Status = refit_call3_wrapper(FileHandle->Read, FileHandle, &File->BufferSize, File->Buffer);
    if (CheckError(Status, Message)) {
        MyFreePool(File->Buffer);
        File->Buffer = NULL;
        refit_call1_wrapper(FileHandle->Close, FileHandle);
        return Status;
    }
    Status = refit_call1_wrapper(FileHandle->Close, FileHandle);

    // setup for reading
    File->Current8Ptr  = (CHAR8 *)File->Buffer;
    File->End8Ptr      = File->Current8Ptr + File->BufferSize;
    File->Current16Ptr = (CHAR16 *)File->Buffer;
    File->End16Ptr     = File->Current16Ptr + (File->BufferSize >> 1);

    // detect encoding
    File->Encoding = ENCODING_ISO8859_1;   // default: 1:1 translation of CHAR8 to CHAR16
    if (File->BufferSize >= 4) {
        if (File->Buffer[0] == 0xFF && File->Buffer[1] == 0xFE) {
            // BOM in UTF-16 little endian (or UTF-32 little endian)
            File->Encoding = ENCODING_UTF16_LE;   // use CHAR16 as is
            File->Current16Ptr++;
        } else if (File->Buffer[0] == 0xEF && File->Buffer[1] == 0xBB && File->Buffer[2] == 0xBF) {
            // BOM in UTF-8
            File->Encoding = ENCODING_UTF8;       // translate from UTF-8 to UTF-16
            File->Current8Ptr += 3;
        } else if (File->Buffer[1] == 0 && File->Buffer[3] == 0) {
            File->Encoding = ENCODING_UTF16_LE;   // use CHAR16 as is
        }
        // TODO: detect other encodings as they are implemented
    }

    return EFI_SUCCESS;
}

//
// get a single line of text from a file
//

static CHAR16 *ReadLine(REFIT_FILE *File)
{
    CHAR16  *Line, *q;
    UINTN   LineLength;

    if (File->Buffer == NULL)
        return NULL;

    if (File->Encoding == ENCODING_ISO8859_1 || File->Encoding == ENCODING_UTF8) {

        CHAR8 *p, *LineStart, *LineEnd;

        p = File->Current8Ptr;
        if (p >= File->End8Ptr)
            return NULL;

        LineStart = p;
        for (; p < File->End8Ptr; p++)
            if (*p == 13 || *p == 10)
                break;
        LineEnd = p;
        for (; p < File->End8Ptr; p++)
            if (*p != 13 && *p != 10)
                break;
        File->Current8Ptr = p;

        LineLength = (UINTN)(LineEnd - LineStart) + 1;
        Line = AllocatePool(LineLength * sizeof(CHAR16));
        if (Line == NULL)
            return NULL;

        q = Line;
        if (File->Encoding == ENCODING_ISO8859_1) {
            for (p = LineStart; p < LineEnd; )
                *q++ = *p++;
        } else if (File->Encoding == ENCODING_UTF8) {
            // TODO: actually handle UTF-8
            for (p = LineStart; p < LineEnd; )
                *q++ = *p++;
        }
        *q = 0;

    } else if (File->Encoding == ENCODING_UTF16_LE) {

        CHAR16 *p, *LineStart, *LineEnd;

        p = File->Current16Ptr;
        if (p >= File->End16Ptr)
            return NULL;

        LineStart = p;
        for (; p < File->End16Ptr; p++)
            if (*p == 13 || *p == 10)
                break;
        LineEnd = p;
        for (; p < File->End16Ptr; p++)
            if (*p != 13 && *p != 10)
                break;
        File->Current16Ptr = p;

        LineLength = (UINTN)(LineEnd - LineStart) + 1;
        Line = AllocatePool(LineLength * sizeof(CHAR16));
        if (Line == NULL)
            return NULL;

        for (p = LineStart, q = Line; p < LineEnd; )
            *q++ = *p++;
        *q = 0;

    } else
        return NULL;   // unsupported encoding

    return Line;
}

// Returns FALSE if *p points to the end of a token, TRUE otherwise.
// Also modifies *p **IF** the first and second characters are both
// quotes ('"'); it deletes one of them.
static BOOLEAN KeepReading(IN OUT CHAR16 *p, IN OUT BOOLEAN *IsQuoted) {
   BOOLEAN MoreToRead = FALSE;
   CHAR16  *Temp = NULL;
//            while (*p && *p != '"' && ((*p != ' ' && *p != '\t' && *p != '=' && *p != '#' && *p != ',') || IsQuoted)) {

   if ((p == NULL) || (IsQuoted == NULL))
      return FALSE;

   if (*p == L'\0')
      return FALSE;

   if ((*p != ' ' && *p != '\t' && *p != '=' && *p != '#' && *p != ',') || *IsQuoted) {
      MoreToRead = TRUE;
   }
   if (*p == L'"') {
      if (p[1] == L'"') {
         Temp = StrDuplicate(&p[1]);
         if (Temp != NULL) {
            StrCpy(p, Temp);
            FreePool(Temp);
         }
         MoreToRead = TRUE;
      } else {
         *IsQuoted = !(*IsQuoted);
         MoreToRead = FALSE;
      } // if/else second character is a quote
   } // if first character is a quote

   return MoreToRead;
} // BOOLEAN KeepReading()

//
// get a line of tokens from a file
//
UINTN ReadTokenLine(IN REFIT_FILE *File, OUT CHAR16 ***TokenList)
{
    BOOLEAN         LineFinished, IsQuoted = FALSE;
    CHAR16          *Line, *Token, *p;
    UINTN           TokenCount = 0;

    *TokenList = NULL;

    while (TokenCount == 0) {
        Line = ReadLine(File);
        if (Line == NULL)
            return(0);

        p = Line;
        LineFinished = FALSE;
        while (!LineFinished) {
            // skip whitespace & find start of token
            while ((*p == ' ' || *p == '\t' || *p == '=' || *p == ',') && !IsQuoted)
                p++;
            if (*p == 0 || *p == '#')
                break;

            if (*p == '"') {
               IsQuoted = !IsQuoted;
               p++;
            } // if
            Token = p;

            // find end of token
            while (KeepReading(p, &IsQuoted)) {
               if ((*p == L'/') && !IsQuoted) // Switch Unix-style to DOS-style directory separators
                  *p = L'\\';
               p++;
            } // while
            if (*p == L'\0' || *p == L'#')
                LineFinished = TRUE;
            *p++ = 0;

            AddListElement((VOID ***)TokenList, &TokenCount, (VOID *)StrDuplicate(Token));
        }

        FreePool(Line);
    }
    return (TokenCount);
} /* ReadTokenLine() */

VOID FreeTokenLine(IN OUT CHAR16 ***TokenList, IN OUT UINTN *TokenCount)
{
    // TODO: also free the items
    FreeList((VOID ***)TokenList, TokenCount);
}

// handle a parameter with a single integer argument
static VOID HandleInt(IN CHAR16 **TokenList, IN UINTN TokenCount, OUT UINTN *Value)
{
    if (TokenCount == 2)
       *Value = Atoi(TokenList[1]);
}

// handle a parameter with a single string argument
static VOID HandleString(IN CHAR16 **TokenList, IN UINTN TokenCount, OUT CHAR16 **Target) {
   if (TokenCount == 2) {
      MyFreePool(*Target);
      *Target = StrDuplicate(TokenList[1]);
   } // if
} // static VOID HandleString()

// Handle a parameter with a series of string arguments, to be added to a comma-delimited
// list. Passes each token through the CleanUpPathNameSlashes() function to ensure
// consistency in subsequent comparisons of filenames.
static VOID HandleStrings(IN CHAR16 **TokenList, IN UINTN TokenCount, OUT CHAR16 **Target) {
   UINTN i;

   if (*Target != NULL) {
      FreePool(*Target);
      *Target = NULL;
   } // if
   for (i = 1; i < TokenCount; i++) {
      CleanUpPathNameSlashes(TokenList[i]);
      MergeStrings(Target, TokenList[i], L',');
   }
} // static VOID HandleStrings()

// read config file
VOID ReadConfig(CHAR16 *FileName)
{
    EFI_STATUS      Status;
    REFIT_FILE      File;
    CHAR16          **TokenList;
    CHAR16          *FlagName;
    UINTN           TokenCount, i;

    // Set a few defaults only if we're loading the default file.
    if (StriCmp(FileName, CONFIG_FILE_NAME) == 0) {
       MyFreePool(GlobalConfig.DontScanDirs);
       GlobalConfig.DontScanDirs = StrDuplicate(SelfDirPath);
       MyFreePool(GlobalConfig.DontScanFiles);
       GlobalConfig.DontScanFiles = StrDuplicate(DONT_SCAN_FILES);
    }

    if (!FileExists(SelfDir, FileName)) {
        Print(L"Configuration file '%s' missing!\n", FileName);
        return;
    }

    Status = ReadFile(SelfDir, FileName, &File, &i);
    if (EFI_ERROR(Status))
        return;

    for (;;) {
        TokenCount = ReadTokenLine(&File, &TokenList);
        if (TokenCount == 0)
            break;

        if (StriCmp(TokenList[0], L"timeout") == 0) {
            HandleInt(TokenList, TokenCount, &(GlobalConfig.Timeout));

        } else if (StriCmp(TokenList[0], L"hideui") == 0) {
            for (i = 1; i < TokenCount; i++) {
                FlagName = TokenList[i];
                if (StriCmp(FlagName, L"banner") == 0) {
                   GlobalConfig.HideUIFlags |= HIDEUI_FLAG_BANNER;
                } else if (StriCmp(FlagName, L"label") == 0) {
                   GlobalConfig.HideUIFlags |= HIDEUI_FLAG_LABEL;
                } else if (StriCmp(FlagName, L"singleuser") == 0) {
                   GlobalConfig.HideUIFlags |= HIDEUI_FLAG_SINGLEUSER;
                } else if (StriCmp(FlagName, L"hwtest") == 0) {
                   GlobalConfig.HideUIFlags |= HIDEUI_FLAG_HWTEST;
                } else if (StriCmp(FlagName, L"arrows") == 0) {
                   GlobalConfig.HideUIFlags |= HIDEUI_FLAG_ARROWS;
                } else if (StriCmp(FlagName, L"hints") == 0) {
                   GlobalConfig.HideUIFlags |= HIDEUI_FLAG_HINTS;
                } else if (StriCmp(FlagName, L"editor") == 0) {
                   GlobalConfig.HideUIFlags |= HIDEUI_FLAG_EDITOR;
                } else if (StriCmp(FlagName, L"all") == 0) {
                   GlobalConfig.HideUIFlags = HIDEUI_FLAG_ALL;
                } else {
                    Print(L" unknown hideui flag: '%s'\n", FlagName);
                }
            }

        } else if (StriCmp(TokenList[0], L"icons_dir") == 0) {
           HandleString(TokenList, TokenCount, &(GlobalConfig.IconsDir));

        } else if (StriCmp(TokenList[0], L"scanfor") == 0) {
           for (i = 0; i < NUM_SCAN_OPTIONS; i++) {
              if (i < TokenCount)
                 GlobalConfig.ScanFor[i] = TokenList[i][0];
              else
                 GlobalConfig.ScanFor[i] = ' ';
           }

        } else if ((StriCmp(TokenList[0], L"scan_delay") == 0) && (TokenCount == 2)) {
           HandleInt(TokenList, TokenCount, &(GlobalConfig.ScanDelay));

        } else if (StriCmp(TokenList[0], L"also_scan_dirs") == 0) {
            HandleStrings(TokenList, TokenCount, &(GlobalConfig.AlsoScan));

        } else if ((StriCmp(TokenList[0], L"don't_scan_dirs") == 0) || (StriCmp(TokenList[0], L"dont_scan_dirs") == 0)) {
            HandleStrings(TokenList, TokenCount, &(GlobalConfig.DontScanDirs));

        } else if ((StriCmp(TokenList[0], L"don't_scan_files") == 0) || (StriCmp(TokenList[0], L"dont_scan_files") == 0)) {
           HandleStrings(TokenList, TokenCount, &(GlobalConfig.DontScanFiles));

        } else if (StriCmp(TokenList[0], L"scan_driver_dirs") == 0) {
            HandleStrings(TokenList, TokenCount, &(GlobalConfig.DriverDirs));

        } else if (StriCmp(TokenList[0], L"showtools") == 0) {
            SetMem(GlobalConfig.ShowTools, NUM_TOOLS * sizeof(UINTN), 0);
            for (i = 1; (i < TokenCount) && (i < NUM_TOOLS); i++) {
                FlagName = TokenList[i];
                if (StriCmp(FlagName, L"shell") == 0) {
                    GlobalConfig.ShowTools[i - 1] = TAG_SHELL;
                } else if (StriCmp(FlagName, L"gptsync") == 0) {
                    GlobalConfig.ShowTools[i - 1] = TAG_GPTSYNC;
                } else if (StriCmp(FlagName, L"about") == 0) {
                   GlobalConfig.ShowTools[i - 1] = TAG_ABOUT;
                } else if (StriCmp(FlagName, L"exit") == 0) {
                   GlobalConfig.ShowTools[i - 1] = TAG_EXIT;
                } else if (StriCmp(FlagName, L"reboot") == 0) {
                   GlobalConfig.ShowTools[i - 1] = TAG_REBOOT;
                } else if (StriCmp(FlagName, L"shutdown") == 0) {
                   GlobalConfig.ShowTools[i - 1] = TAG_SHUTDOWN;
                } else if (StriCmp(FlagName, L"apple_recovery") == 0) {
                   GlobalConfig.ShowTools[i - 1] = TAG_APPLE_RECOVERY;
                } else if (StriCmp(FlagName, L"mok_tool") == 0) {
                   GlobalConfig.ShowTools[i - 1] = TAG_MOK_TOOL;
                } else {
                   Print(L" unknown showtools flag: '%s'\n", FlagName);
                }
            } // showtools options

        } else if (StriCmp(TokenList[0], L"banner") == 0) {
           HandleString(TokenList, TokenCount, &(GlobalConfig.BannerFileName));

        } else if (StriCmp(TokenList[0], L"selection_small") == 0) {
           HandleString(TokenList, TokenCount, &(GlobalConfig.SelectionSmallFileName));

        } else if (StriCmp(TokenList[0], L"selection_big") == 0) {
           HandleString(TokenList, TokenCount, &(GlobalConfig.SelectionBigFileName));

        } else if (StriCmp(TokenList[0], L"default_selection") == 0) {
           HandleString(TokenList, TokenCount, &(GlobalConfig.DefaultSelection));

        } else if (StriCmp(TokenList[0], L"textonly") == 0) {
           if ((TokenCount >= 2) && (StriCmp(TokenList[1], L"0") == 0)) {
              GlobalConfig.TextOnly = FALSE;
           } else {
              GlobalConfig.TextOnly = TRUE;
           }

        } else if (StriCmp(TokenList[0], L"textmode") == 0) {
           HandleInt(TokenList, TokenCount, &(GlobalConfig.RequestedTextMode));

        } else if ((StriCmp(TokenList[0], L"resolution") == 0) && ((TokenCount == 2) || (TokenCount == 3))) {
           GlobalConfig.RequestedScreenWidth = Atoi(TokenList[1]);
           if (TokenCount == 3)
              GlobalConfig.RequestedScreenHeight = Atoi(TokenList[2]);
           else
              GlobalConfig.RequestedScreenHeight = 0;

        } else if (StriCmp(TokenList[0], L"use_graphics_for") == 0) {
           GlobalConfig.GraphicsFor = 0;
           for (i = 1; i < TokenCount; i++) {
              if (StriCmp(TokenList[i], L"osx") == 0) {
                 GlobalConfig.GraphicsFor |= GRAPHICS_FOR_OSX;
              } else if (StriCmp(TokenList[i], L"linux") == 0) {
                 GlobalConfig.GraphicsFor |= GRAPHICS_FOR_LINUX;
              } else if (StriCmp(TokenList[i], L"elilo") == 0) {
                 GlobalConfig.GraphicsFor |= GRAPHICS_FOR_ELILO;
              } else if (StriCmp(TokenList[i], L"grub") == 0) {
                 GlobalConfig.GraphicsFor |= GRAPHICS_FOR_GRUB;
              } else if (StriCmp(TokenList[i], L"windows") == 0) {
                 GlobalConfig.GraphicsFor |= GRAPHICS_FOR_WINDOWS;
              }
           } // for (graphics_on tokens)

        } else if (StriCmp(TokenList[0], L"scan_all_linux_kernels") == 0) {
           if ((TokenCount >= 2) && (StriCmp(TokenList[1], L"0") == 0)) {
              GlobalConfig.ScanAllLinux = FALSE;
           } else {
              GlobalConfig.ScanAllLinux = TRUE;
           }

        } else if (StriCmp(TokenList[0], L"max_tags") == 0) {
           HandleInt(TokenList, TokenCount, &(GlobalConfig.MaxTags));

        } else if ((StriCmp(TokenList[0], L"include") == 0) && (TokenCount == 2) && (StriCmp(FileName, CONFIG_FILE_NAME) == 0)) {
           if (StriCmp(TokenList[1], FileName) != 0) {
              ReadConfig(TokenList[1]);
           }

        }

        FreeTokenLine(&TokenList, &TokenCount);
    }
    MyFreePool(File.Buffer);
} /* VOID ReadConfig() */

// Finds a volume with the specified Identifier (a volume label or a number
// followed by a colon, for the moment). If found, sets *Volume to point to
// that volume. If not, leaves it unchanged.
// Returns TRUE if a match was found, FALSE if not.
static BOOLEAN FindVolume(REFIT_VOLUME **Volume, CHAR16 *Identifier) {
   UINTN     i = 0, CountedVolumes = 0;
   INTN      Number = -1;
   BOOLEAN   Found = FALSE;

   if ((StrLen(Identifier) >= 2) && (Identifier[StrLen(Identifier) - 1] == L':') &&
       (Identifier[0] >= L'0') && (Identifier[0] <= L'9')) {
      Number = (INTN) Atoi(Identifier);
   }
   while ((i < VolumesCount) && (!Found)) {
      if (Number >= 0) { // User specified a volume by number
         if (Volumes[i]->IsReadable) {
            if (CountedVolumes == Number) {
               *Volume = Volumes[i];
               Found = TRUE;
            }
            CountedVolumes++;
         } // if
      } else { // User specified a volume by label
         if (StriCmp(Identifier, Volumes[i]->VolName) == 0) {
            *Volume = Volumes[i];
            Found = TRUE;
         } // if
      } // if/else
      i++;
   } // while()
   return (Found);
} // static VOID FindVolume()

static VOID AddSubmenu(LOADER_ENTRY *Entry, REFIT_FILE *File, REFIT_VOLUME *Volume, CHAR16 *Title) {
   REFIT_MENU_SCREEN  *SubScreen;
   LOADER_ENTRY       *SubEntry;
   UINTN              TokenCount;
   CHAR16             **TokenList;

   SubScreen = InitializeSubScreen(Entry);

   // Set defaults for the new entry; will be modified based on lines read from the config. file....
   SubEntry = InitializeLoaderEntry(Entry);

   if ((SubEntry == NULL) || (SubScreen == NULL))
      return;
   SubEntry->me.Title        = StrDuplicate(Title);

   while (((TokenCount = ReadTokenLine(File, &TokenList)) > 0) && (StriCmp(TokenList[0], L"}") != 0)) {

      if ((StriCmp(TokenList[0], L"loader") == 0) && (TokenCount > 1)) { // set the boot loader filename
         MyFreePool(SubEntry->LoaderPath);
         SubEntry->LoaderPath = StrDuplicate(TokenList[1]);
         SubEntry->DevicePath = FileDevicePath(Volume->DeviceHandle, SubEntry->LoaderPath);

      } else if ((StriCmp(TokenList[0], L"volume") == 0) && (TokenCount > 1)) {
         if (FindVolume(&Volume, TokenList[1])) {
            MyFreePool(SubEntry->me.Title);
            SubEntry->me.Title        = AllocateZeroPool(256 * sizeof(CHAR16));
            SPrint(SubEntry->me.Title, 255, L"Boot %s from %s", (Title != NULL) ? Title : L"Unknown", Volume->VolName);
            SubEntry->me.BadgeImage   = Volume->VolBadgeImage;
            SubEntry->VolName         = Volume->VolName;
         } // if match found

      } else if (StriCmp(TokenList[0], L"initrd") == 0) {
         MyFreePool(SubEntry->InitrdPath);
         SubEntry->InitrdPath = NULL;
         if (TokenCount > 1) {
            SubEntry->InitrdPath = StrDuplicate(TokenList[1]);
         }

      } else if (StriCmp(TokenList[0], L"options") == 0) {
         MyFreePool(SubEntry->LoadOptions);
         SubEntry->LoadOptions = NULL;
         if (TokenCount > 1) {
            SubEntry->LoadOptions = StrDuplicate(TokenList[1]);
         } // if/else

      } else if ((StriCmp(TokenList[0], L"add_options") == 0) && (TokenCount > 1)) {
         MergeStrings(&SubEntry->LoadOptions, TokenList[1], L' ');

      } else if ((StriCmp(TokenList[0], L"graphics") == 0) && (TokenCount > 1)) {
         SubEntry->UseGraphicsMode = (StriCmp(TokenList[1], L"on") == 0);

      } else if (StriCmp(TokenList[0], L"disabled") == 0) {
         SubEntry->Enabled = FALSE;
      } // ief/elseif

      FreeTokenLine(&TokenList, &TokenCount);
   } // while()

   if (SubEntry->InitrdPath != NULL) {
      MergeStrings(&SubEntry->LoadOptions, L"initrd=", L' ');
      MergeStrings(&SubEntry->LoadOptions, SubEntry->InitrdPath, 0);
      MyFreePool(SubEntry->InitrdPath);
      SubEntry->InitrdPath = NULL;
   } // if
   if (SubEntry->Enabled == TRUE) {
      AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
   }
   Entry->me.SubScreen = SubScreen;
} // VOID AddSubmenu()

// Adds the options from a SINGLE refind.conf stanza to a new loader entry and returns
// that entry. The calling function is then responsible for adding the entry to the
// list of entries.
static LOADER_ENTRY * AddStanzaEntries(REFIT_FILE *File, REFIT_VOLUME *Volume, CHAR16 *Title) {
   CHAR16       **TokenList;
   UINTN        TokenCount;
   LOADER_ENTRY *Entry;
   BOOLEAN      DefaultsSet = FALSE, AddedSubmenu = FALSE;
   REFIT_VOLUME *CurrentVolume = Volume;

   // prepare the menu entry
   Entry = InitializeLoaderEntry(NULL);
   if (Entry == NULL)
      return NULL;

   Entry->Title           = StrDuplicate(Title);
   Entry->me.Title        = AllocateZeroPool(256 * sizeof(CHAR16));
   SPrint(Entry->me.Title, 255, L"Boot %s from %s", (Title != NULL) ? Title : L"Unknown", CurrentVolume->VolName);
   Entry->me.Row          = 0;
   Entry->me.BadgeImage   = CurrentVolume->VolBadgeImage;
   Entry->VolName         = CurrentVolume->VolName;

   // Parse the config file to add options for a single stanza, terminating when the token
   // is "}" or when the end of file is reached.
   while (((TokenCount = ReadTokenLine(File, &TokenList)) > 0) && (StriCmp(TokenList[0], L"}") != 0)) {
      if ((StriCmp(TokenList[0], L"loader") == 0) && (TokenCount > 1)) { // set the boot loader filename
         Entry->LoaderPath = StrDuplicate(TokenList[1]);
         Entry->DevicePath = FileDevicePath(CurrentVolume->DeviceHandle, Entry->LoaderPath);
         SetLoaderDefaults(Entry, TokenList[1], CurrentVolume);
         MyFreePool(Entry->LoadOptions);
         Entry->LoadOptions = NULL; // Discard default options, if any
         DefaultsSet = TRUE;

      } else if ((StriCmp(TokenList[0], L"volume") == 0) && (TokenCount > 1)) {
         if (FindVolume(&CurrentVolume, TokenList[1])) {
            MyFreePool(Entry->me.Title);
            Entry->me.Title        = AllocateZeroPool(256 * sizeof(CHAR16));
            SPrint(Entry->me.Title, 255, L"Boot %s from %s", (Title != NULL) ? Title : L"Unknown", CurrentVolume->VolName);
            Entry->me.BadgeImage   = CurrentVolume->VolBadgeImage;
            Entry->VolName         = CurrentVolume->VolName;
         } // if match found

      } else if ((StriCmp(TokenList[0], L"icon") == 0) && (TokenCount > 1)) {
         MyFreePool(Entry->me.Image);
         Entry->me.Image = LoadIcns(CurrentVolume->RootDir, TokenList[1], 128);
         if (Entry->me.Image == NULL) {
            Entry->me.Image = DummyImage(128);
         }

      } else if ((StriCmp(TokenList[0], L"initrd") == 0) && (TokenCount > 1)) {
         MyFreePool(Entry->InitrdPath);
         Entry->InitrdPath = StrDuplicate(TokenList[1]);

      } else if ((StriCmp(TokenList[0], L"options") == 0) && (TokenCount > 1)) {
         MyFreePool(Entry->LoadOptions);
         Entry->LoadOptions = StrDuplicate(TokenList[1]);

      } else if ((StriCmp(TokenList[0], L"ostype") == 0) && (TokenCount > 1)) {
         if (TokenCount > 1) {
            Entry->OSType = TokenList[1][0];
         }

      } else if ((StriCmp(TokenList[0], L"graphics") == 0) && (TokenCount > 1)) {
         Entry->UseGraphicsMode = (StriCmp(TokenList[1], L"on") == 0);

      } else if (StriCmp(TokenList[0], L"disabled") == 0) {
         Entry->Enabled = FALSE;

      } else if ((StriCmp(TokenList[0], L"submenuentry") == 0) && (TokenCount > 1)) {
         AddSubmenu(Entry, File, CurrentVolume, TokenList[1]);
         AddedSubmenu = TRUE;

      } // set options to pass to the loader program
      FreeTokenLine(&TokenList, &TokenCount);
   } // while()

   if (AddedSubmenu)
       AddMenuEntry(Entry->me.SubScreen, &MenuEntryReturn);

   if (Entry->InitrdPath) {
      MergeStrings(&Entry->LoadOptions, L"initrd=", L' ');
      MergeStrings(&Entry->LoadOptions, Entry->InitrdPath, 0);
      MyFreePool(Entry->InitrdPath);
      Entry->InitrdPath = NULL;
   } // if

   if (!DefaultsSet)
      SetLoaderDefaults(Entry, L"\\EFI\\BOOT\\nemo.efi", CurrentVolume); // user included no entry; use bogus one

   return(Entry);
} // static VOID AddStanzaEntries()

// Read the user-configured menu entries from refind.conf and add or delete
// entries based on the contents of that file....
VOID ScanUserConfigured(VOID)
{
   EFI_STATUS        Status;
   REFIT_FILE        File;
   REFIT_VOLUME      *Volume;
   CHAR16            **TokenList;
   CHAR16            *Title = NULL;
   UINTN             TokenCount, size;
   LOADER_ENTRY      *Entry;
//   REFIT_MENU_SCREEN  *SubScreen;

   if (FileExists(SelfDir, CONFIG_FILE_NAME)) {
      Status = ReadFile(SelfDir, CONFIG_FILE_NAME, &File, &size);
      if (EFI_ERROR(Status))
         return;

      Volume = SelfVolume;

      while ((TokenCount = ReadTokenLine(&File, &TokenList)) > 0) {
         if ((StriCmp(TokenList[0], L"menuentry") == 0) && (TokenCount > 1)) {
            Title = StrDuplicate(TokenList[1]);
            Entry = AddStanzaEntries(&File, Volume, TokenList[1]);
            if (Entry->Enabled) {
               if (Entry->me.SubScreen == NULL)
                  GenerateSubScreen(Entry, Volume);
               AddPreparedLoaderEntry(Entry);
            } else {
               MyFreePool(Entry);
            } // if/else
            MyFreePool(Title);
         } // if
         FreeTokenLine(&TokenList, &TokenCount);
      } // while()
   } // if()
} // VOID ScanUserConfigured()

// Read a Linux kernel options file for a Linux boot loader into memory. The LoaderPath
// and Volume variables identify the location of the options file, but not its name --
// you pass this function the filename of the Linux kernel, initial RAM disk, or other
// file in the target directory, and this function finds the file with a name in the
// comma-delimited list of names specified by LINUX_OPTIONS_FILENAMES within that
// directory and loads it. This function tries multiple files because I originally
// used the filename linux.conf, but close on the heels of that decision, the Linux
// kernel developers decided to use that name for a similar purpose, but with a
// different file format. Thus, I'm migrating rEFInd to use the name refind_linux.conf,
// but I want a migration period in which both names are used.
//
// The return value is a pointer to the REFIT_FILE handle for the file, or NULL if
// it wasn't found.
REFIT_FILE * ReadLinuxOptionsFile(IN CHAR16 *LoaderPath, IN REFIT_VOLUME *Volume) {
   CHAR16       *OptionsFilename, *FullFilename;
   BOOLEAN      GoOn = TRUE;
   UINTN        i = 0, size;
   REFIT_FILE   *File = NULL;
   EFI_STATUS   Status;

   do {
      OptionsFilename = FindCommaDelimited(LINUX_OPTIONS_FILENAMES, i++);
      FullFilename = FindPath(LoaderPath);
      if ((OptionsFilename != NULL) && (FullFilename != NULL)) {
         MergeStrings(&FullFilename, OptionsFilename, '\\');
         if (FileExists(Volume->RootDir, FullFilename)) {
            File = AllocateZeroPool(sizeof(REFIT_FILE));
            Status = ReadFile(Volume->RootDir, FullFilename, File, &size);
            GoOn = FALSE;
            if (CheckError(Status, L"while loading the Linux options file")) {
               if (File != NULL)
                  FreePool(File);
               File = NULL;
               GoOn = TRUE;
            } // if error
         } // if file exists
      } else { // a filename string is NULL
         GoOn = FALSE;
      } // if/else
      MyFreePool(OptionsFilename);
      MyFreePool(FullFilename);
      OptionsFilename = FullFilename = NULL;
   } while (GoOn);
   return (File);
} // static REFIT_FILE * ReadLinuxOptionsFile()

// Retrieve a single line of options from a Linux kernel options file
CHAR16 * GetFirstOptionsFromFile(IN CHAR16 *LoaderPath, IN REFIT_VOLUME *Volume) {
   UINTN        TokenCount;
   CHAR16       *Options = NULL;
   CHAR16       **TokenList;
   REFIT_FILE   *File;

   File = ReadLinuxOptionsFile(LoaderPath, Volume);
   if (File != NULL) {
      TokenCount = ReadTokenLine(File, &TokenList);
      if (TokenCount > 1)
         Options = StrDuplicate(TokenList[1]);
      FreeTokenLine(&TokenList, &TokenCount);
      FreePool(File);
   } // if
   return Options;
} // static CHAR16 * GetOptionsFile()


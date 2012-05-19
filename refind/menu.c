/*
 * refit/menu.c
 * Menu functions
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
#include "screen.h"
#include "lib.h"
#include "menu.h"
#include "config.h"
#include "libeg.h"
#include "refit_call_wrapper.h"

#include "egemb_back_selected_small.h"
#include "egemb_arrow_left.h"
#include "egemb_arrow_right.h"

// other menu definitions

#define MENU_FUNCTION_INIT            (0)
#define MENU_FUNCTION_CLEANUP         (1)
#define MENU_FUNCTION_PAINT_ALL       (2)
#define MENU_FUNCTION_PAINT_SELECTION (3)
#define MENU_FUNCTION_PAINT_TIMEOUT   (4)

typedef VOID (*MENU_STYLE_FUNC)(IN REFIT_MENU_SCREEN *Screen, IN SCROLL_STATE *State, IN UINTN Function, IN CHAR16 *ParamText);

static CHAR16 ArrowUp[2] = { ARROW_UP, 0 };
static CHAR16 ArrowDown[2] = { ARROW_DOWN, 0 };

#define TEXT_YMARGIN (2)
#define TEXT_XMARGIN (8)
#define TEXT_LINE_HEIGHT (FONT_CELL_HEIGHT + TEXT_YMARGIN * 2)
#define TITLEICON_SPACING (16)

#define ROW0_TILESIZE (144)
#define ROW1_TILESIZE (64)
#define TILE_XSPACING (8)
#define TILE_YSPACING (16)

// Alignment values for PaintIcon()
#define ALIGN_RIGHT 1
#define ALIGN_LEFT 0

static EG_IMAGE *SelectionImages[4] = { NULL, NULL, NULL, NULL };
static EG_PIXEL SelectionBackgroundPixel = { 0xff, 0xff, 0xff, 0 };
static EG_IMAGE *TextBuffer = NULL;

//
// Graphics helper functions
//

static VOID InitSelection(VOID)
{
    UINTN       x, y, src_x, src_y;
    EG_PIXEL    *DestPtr, *SrcPtr;

    if (!AllowGraphicsMode)
        return;
    if (SelectionImages[0] != NULL)
        return;

    // load small selection image
    if (GlobalConfig.SelectionSmallFileName != NULL) {
        SelectionImages[2] = egLoadImage(SelfDir, GlobalConfig.SelectionSmallFileName, FALSE);
    }
    if (SelectionImages[2] == NULL)
        SelectionImages[2] = egPrepareEmbeddedImage(&egemb_back_selected_small, FALSE);
    SelectionImages[2] = egEnsureImageSize(SelectionImages[2],
                                           ROW1_TILESIZE, ROW1_TILESIZE, &MenuBackgroundPixel);
    if (SelectionImages[2] == NULL)
        return;

    // load big selection image
    if (GlobalConfig.SelectionBigFileName != NULL) {
        SelectionImages[0] = egLoadImage(SelfDir, GlobalConfig.SelectionBigFileName, FALSE);
        SelectionImages[0] = egEnsureImageSize(SelectionImages[0],
                                               ROW0_TILESIZE, ROW0_TILESIZE, &MenuBackgroundPixel);
    }
    if (SelectionImages[0] == NULL) {
        // calculate big selection image from small one

        SelectionImages[0] = egCreateImage(ROW0_TILESIZE, ROW0_TILESIZE, FALSE);
        if (SelectionImages[0] == NULL) {
            egFreeImage(SelectionImages[2]);
            SelectionImages[2] = NULL;
            return;
        }

        DestPtr = SelectionImages[0]->PixelData;
        SrcPtr  = SelectionImages[2]->PixelData;
        for (y = 0; y < ROW0_TILESIZE; y++) {
            if (y < (ROW1_TILESIZE >> 1))
                src_y = y;
            else if (y < (ROW0_TILESIZE - (ROW1_TILESIZE >> 1)))
                src_y = (ROW1_TILESIZE >> 1);
            else
                src_y = y - (ROW0_TILESIZE - ROW1_TILESIZE);

            for (x = 0; x < ROW0_TILESIZE; x++) {
                if (x < (ROW1_TILESIZE >> 1))
                    src_x = x;
                else if (x < (ROW0_TILESIZE - (ROW1_TILESIZE >> 1)))
                    src_x = (ROW1_TILESIZE >> 1);
                else
                    src_x = x - (ROW0_TILESIZE - ROW1_TILESIZE);

                *DestPtr++ = SrcPtr[src_y * ROW1_TILESIZE + src_x];
            }
        }
    }

    // non-selected background images
    SelectionImages[1] = egCreateFilledImage(ROW0_TILESIZE, ROW0_TILESIZE, FALSE, &MenuBackgroundPixel);
    SelectionImages[3] = egCreateFilledImage(ROW1_TILESIZE, ROW1_TILESIZE, FALSE, &MenuBackgroundPixel);
}

//
// Scrolling functions
//

static VOID InitScroll(OUT SCROLL_STATE *State, IN UINTN ItemCount, IN UINTN VisibleSpace)
{
    State->PreviousSelection = State->CurrentSelection = 0;
    State->MaxIndex = (INTN)ItemCount - 1;
    State->FirstVisible = 0;
    if (AllowGraphicsMode) {
       State->MaxVisible = UGAWidth / (ROW0_TILESIZE + TILE_XSPACING) - 1;
    } else
       State->MaxVisible = ConHeight - 4;
    if ((VisibleSpace > 0) && (VisibleSpace < State->MaxVisible))
        State->MaxVisible = (INTN)VisibleSpace;
    State->PaintAll = TRUE;
    State->PaintSelection = FALSE;

    State->LastVisible = State->FirstVisible + State->MaxVisible - 1;
}

// Adjust variables relating to the scrolling of tags, for when a selected icon isn't
// visible given the current scrolling condition....
static VOID AdjustScrollState(IN SCROLL_STATE *State) {
   if (State->CurrentSelection > State->LastVisible) {
      State->LastVisible = State->CurrentSelection;
      State->FirstVisible = 1 + State->CurrentSelection - State->MaxVisible;
      if (State->FirstVisible < 0) // shouldn't happen, but just in case....
         State->FirstVisible = 0;
      State->PaintAll = TRUE;
   } // Scroll forward
   if (State->CurrentSelection < State->FirstVisible) {
      State->FirstVisible = State->CurrentSelection;
      State->LastVisible = State->CurrentSelection + State->MaxVisible - 1;
      State->PaintAll = TRUE;
   } // Scroll backward
} // static VOID AdjustScrollState

static VOID UpdateScroll(IN OUT SCROLL_STATE *State, IN UINTN Movement)
{
    State->PreviousSelection = State->CurrentSelection;

    switch (Movement) {
        case SCROLL_LINE_LEFT:
            if (State->CurrentSelection > 0) {
                State->CurrentSelection --;
            }
            break;

        case SCROLL_LINE_RIGHT:
            if (State->CurrentSelection < State->MaxIndex) {
                State->CurrentSelection ++;
            }
            break;

        case SCROLL_LINE_UP:
            if (State->ScrollMode == SCROLL_MODE_ICONS) {
               if (State->CurrentSelection >= State->InitialRow1) {
                  if (State->MaxIndex > State->InitialRow1) { // avoid division by 0!
                     State->CurrentSelection = State->FirstVisible + (State->LastVisible - State->FirstVisible) *
                                               (State->CurrentSelection - State->InitialRow1) /
                                               (State->MaxIndex - State->InitialRow1);
                  } else {
                     State->CurrentSelection = State->FirstVisible;
                  } // if/else
               } // if in second row
            } else {
               if (State->CurrentSelection > 0)
                  State->CurrentSelection--;
            } // if/else
            break;

        case SCROLL_LINE_DOWN:
           if (State->ScrollMode == SCROLL_MODE_ICONS) {
               if (State->CurrentSelection <= State->FinalRow0) {
                  if (State->LastVisible > State->FirstVisible) { // avoid division by 0!
                     State->CurrentSelection = State->InitialRow1 + (State->MaxIndex - State->InitialRow1) *
                                               (State->CurrentSelection - State->FirstVisible) /
                                               (State->LastVisible - State->FirstVisible);
                  } else {
                     State->CurrentSelection = State->InitialRow1;
                  } // if/else
               } // if in first row
            } else {
               if (State->CurrentSelection < State->MaxIndex)
                  State->CurrentSelection++;
            } // if/else
            break;

        case SCROLL_PAGE_UP:
           if (State->CurrentSelection <= State->FinalRow0)
              State->CurrentSelection -= State->MaxVisible;
           else if (State->CurrentSelection == State->InitialRow1)
              State->CurrentSelection = State->FinalRow0;
           else
              State->CurrentSelection = State->InitialRow1;
           if (State->CurrentSelection < 0)
              State->CurrentSelection = 0;
           break;

        case SCROLL_FIRST:
           if (State->CurrentSelection > 0) {
              State->PaintAll = TRUE;
              State->CurrentSelection = 0;
           }
           break;

        case SCROLL_PAGE_DOWN:
           if (State->CurrentSelection < State->FinalRow0) {
              State->CurrentSelection += State->MaxVisible;
              if (State->CurrentSelection > State->FinalRow0)
                 State->CurrentSelection = State->FinalRow0;
           } else if (State->CurrentSelection == State->FinalRow0) {
              State->CurrentSelection++;
           } else {
              State->CurrentSelection = State->MaxIndex;
           }
           if (State->CurrentSelection > State->MaxIndex)
              State->CurrentSelection = State->MaxIndex;
           break;

        case SCROLL_LAST:
           if (State->CurrentSelection < State->MaxIndex) {
              State->PaintAll = TRUE;
              State->CurrentSelection = State->MaxIndex;
           }
           break;

        case SCROLL_NONE:
            break;

    }
    if (State->ScrollMode == SCROLL_MODE_TEXT)
       AdjustScrollState(State);

    if (!State->PaintAll && State->CurrentSelection != State->PreviousSelection)
        State->PaintSelection = TRUE;
    State->LastVisible = State->FirstVisible + State->MaxVisible - 1;
} // static VOID UpdateScroll()

//
// menu helper functions
//

VOID AddMenuInfoLine(IN REFIT_MENU_SCREEN *Screen, IN CHAR16 *InfoLine)
{
    AddListElement((VOID ***) &(Screen->InfoLines), &(Screen->InfoLineCount), InfoLine);
}

VOID AddMenuEntry(IN REFIT_MENU_SCREEN *Screen, IN REFIT_MENU_ENTRY *Entry)
{
    AddListElement((VOID ***) &(Screen->Entries), &(Screen->EntryCount), Entry);
}

VOID FreeMenu(IN REFIT_MENU_SCREEN *Screen)
{
    if (Screen->Entries)
        FreePool(Screen->Entries);
}

static INTN FindMenuShortcutEntry(IN REFIT_MENU_SCREEN *Screen, IN CHAR16 *Shortcut)
{
    UINTN i;

    if (Shortcut == NULL)
       return (-1);

    if (StrLen(Shortcut) == 1) {
      if (Shortcut[0] >= 'a' && Shortcut[0] <= 'z')
         Shortcut[0] -= ('a' - 'A');
      if (Shortcut[0]) {
         for (i = 0; i < Screen->EntryCount; i++) {
               if (Screen->Entries[i]->ShortcutDigit == Shortcut[0] || Screen->Entries[i]->ShortcutLetter == Shortcut[0]) {
                  return i;
               } // if
         } // for
      } // if
    } else if (StrLen(Shortcut) > 1) {
       for (i = 0; i < Screen->EntryCount; i++) {
          if (StriSubCmp(Shortcut, Screen->Entries[i]->Title))
             return i;
       } // for
    }
    return -1;
}

// Identify the end of row 0 and the beginning of row 1; store the results in the
// appropriate fields in State. Also reduce MaxVisible if that value is greater
// than the total number of row-0 tags and if we're in an icon-based screen
static VOID IdentifyRows(IN SCROLL_STATE *State, IN REFIT_MENU_SCREEN *Screen) {
   UINTN i;

   State->FinalRow0 = 0;
   State->InitialRow1 = State->MaxIndex;
   for (i = 0; i < State->MaxIndex; i++) {
      if (Screen->Entries[i]->Row == 0) {
         State->FinalRow0 = i;
      } else if ((Screen->Entries[i]->Row == 1) && (State->InitialRow1 > i)) {
         State->InitialRow1 = i;
      } // if/else
   } // for
   if ((State->ScrollMode == SCROLL_MODE_ICONS) && (State->MaxVisible > (State->FinalRow0 + 1)))
      State->MaxVisible = State->FinalRow0 + 1;
} // static VOID IdentifyRows()

//
// generic menu function
//
static UINTN RunGenericMenu(IN REFIT_MENU_SCREEN *Screen, IN MENU_STYLE_FUNC StyleFunc, IN INTN DefaultEntryIndex, OUT REFIT_MENU_ENTRY **ChosenEntry)
{
    SCROLL_STATE State;
    EFI_STATUS Status;
    EFI_INPUT_KEY key;
    UINTN index;
    INTN ShortcutEntry;
    BOOLEAN HaveTimeout = FALSE;
    UINTN TimeoutCountdown = 0;
    CHAR16 *TimeoutMessage;
    CHAR16 KeyAsString[2];
    UINTN MenuExit;

    if (Screen->TimeoutSeconds > 0) {
        HaveTimeout = TRUE;
        TimeoutCountdown = Screen->TimeoutSeconds * 10;
    }
    MenuExit = 0;

    StyleFunc(Screen, &State, MENU_FUNCTION_INIT, NULL);
    IdentifyRows(&State, Screen);
    // override the starting selection with the default index, if any
    if (DefaultEntryIndex >= 0 && DefaultEntryIndex <= State.MaxIndex) {
        State.CurrentSelection = DefaultEntryIndex;
        UpdateScroll(&State, SCROLL_NONE);
    }

    while (!MenuExit) {
        // update the screen
        if (State.PaintAll) {
            StyleFunc(Screen, &State, MENU_FUNCTION_PAINT_ALL, NULL);
            State.PaintAll = FALSE;
        } else if (State.PaintSelection) {
            StyleFunc(Screen, &State, MENU_FUNCTION_PAINT_SELECTION, NULL);
            State.PaintSelection = FALSE;
        }

        if (HaveTimeout) {
            TimeoutMessage = PoolPrint(L"%s in %d seconds", Screen->TimeoutText, (TimeoutCountdown + 5) / 10);
            StyleFunc(Screen, &State, MENU_FUNCTION_PAINT_TIMEOUT, TimeoutMessage);
            FreePool(TimeoutMessage);
        }

        // read key press (and wait for it if applicable)
        Status = refit_call2_wrapper(ST->ConIn->ReadKeyStroke, ST->ConIn, &key);
        if (Status == EFI_NOT_READY) {
            if (HaveTimeout && TimeoutCountdown == 0) {
                // timeout expired
                MenuExit = MENU_EXIT_TIMEOUT;
                break;
            } else if (HaveTimeout) {
                refit_call1_wrapper(BS->Stall, 100000);
                TimeoutCountdown--;
            } else
                refit_call3_wrapper(BS->WaitForEvent, 1, &ST->ConIn->WaitForKey, &index);
            continue;
        }
        if (HaveTimeout) {
            // the user pressed a key, cancel the timeout
            StyleFunc(Screen, &State, MENU_FUNCTION_PAINT_TIMEOUT, L"");
            HaveTimeout = FALSE;
        }

        // react to key press
        switch (key.ScanCode) {
            case SCAN_UP:
                UpdateScroll(&State, SCROLL_LINE_UP);
                break;
            case SCAN_LEFT:
                UpdateScroll(&State, SCROLL_LINE_LEFT);
                break;
            case SCAN_DOWN:
                UpdateScroll(&State, SCROLL_LINE_DOWN);
                break;
            case SCAN_RIGHT:
                UpdateScroll(&State, SCROLL_LINE_RIGHT);
                break;
            case SCAN_HOME:
                UpdateScroll(&State, SCROLL_FIRST);
                break;
            case SCAN_END:
                UpdateScroll(&State, SCROLL_LAST);
                break;
            case SCAN_PAGE_UP:
                UpdateScroll(&State, SCROLL_PAGE_UP);
                break;
            case SCAN_PAGE_DOWN:
                UpdateScroll(&State, SCROLL_PAGE_DOWN);
                break;
            case SCAN_ESC:
                MenuExit = MENU_EXIT_ESCAPE;
                break;
            case SCAN_INSERT:
            case SCAN_F2:
                MenuExit = MENU_EXIT_DETAILS;
                break;
            case SCAN_F10:
                egScreenShot();
                break;
			case 0x0016: // F12
				EjectMedia();
				break;
        }
        switch (key.UnicodeChar) {
            case CHAR_LINEFEED:
            case CHAR_CARRIAGE_RETURN:
            case ' ':
                MenuExit = MENU_EXIT_ENTER;
                break;
            case '+':
                MenuExit = MENU_EXIT_DETAILS;
                break;
            default:
                KeyAsString[0] = key.UnicodeChar;
                KeyAsString[1] = 0;
                ShortcutEntry = FindMenuShortcutEntry(Screen, KeyAsString);
                if (ShortcutEntry >= 0) {
                    State.CurrentSelection = ShortcutEntry;
                    MenuExit = MENU_EXIT_ENTER;
                }
                break;
        }
    }

    StyleFunc(Screen, &State, MENU_FUNCTION_CLEANUP, NULL);

    if (ChosenEntry)
        *ChosenEntry = Screen->Entries[State.CurrentSelection];
    return MenuExit;
} /* static UINTN RunGenericMenu( */

//
// text-mode generic style
//

static VOID TextMenuStyle(IN REFIT_MENU_SCREEN *Screen, IN SCROLL_STATE *State, IN UINTN Function, IN CHAR16 *ParamText)
{
    INTN i;
    UINTN MenuWidth, ItemWidth, MenuHeight;
    static UINTN MenuPosY;
    static CHAR16 **DisplayStrings;
    CHAR16 *TimeoutMessage;

    State->ScrollMode = SCROLL_MODE_TEXT;
    switch (Function) {

        case MENU_FUNCTION_INIT:
            // vertical layout
            MenuPosY = 4;
            if (Screen->InfoLineCount > 0)
                MenuPosY += Screen->InfoLineCount + 1;
            MenuHeight = ConHeight - MenuPosY;
            if (Screen->TimeoutSeconds > 0)
                MenuHeight -= 2;
            InitScroll(State, Screen->EntryCount, MenuHeight);

            // determine width of the menu
            MenuWidth = 20;  // minimum
            for (i = 0; i <= State->MaxIndex; i++) {
                ItemWidth = StrLen(Screen->Entries[i]->Title);
                if (MenuWidth < ItemWidth)
                    MenuWidth = ItemWidth;
            }
            if (MenuWidth > ConWidth - 6)
                MenuWidth = ConWidth - 6;

            // prepare strings for display
            DisplayStrings = AllocatePool(sizeof(CHAR16 *) * Screen->EntryCount);
            for (i = 0; i <= State->MaxIndex; i++)
                DisplayStrings[i] = PoolPrint(L" %-.*s ", MenuWidth, Screen->Entries[i]->Title);
            // TODO: shorten strings that are too long (PoolPrint doesn't do that...)
            // TODO: use more elaborate techniques for shortening too long strings (ellipses in the middle)
            // TODO: account for double-width characters

            // initial painting
            BeginTextScreen(Screen->Title);
            if (Screen->InfoLineCount > 0) {
                refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_BASIC);
                for (i = 0; i < (INTN)Screen->InfoLineCount; i++) {
                    refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, 3, 4 + i);
                    refit_call2_wrapper(ST->ConOut->OutputString, ST->ConOut, Screen->InfoLines[i]);
                }
            }

            break;

        case MENU_FUNCTION_CLEANUP:
            // release temporary memory
            for (i = 0; i <= State->MaxIndex; i++)
                FreePool(DisplayStrings[i]);
            FreePool(DisplayStrings);
            break;

        case MENU_FUNCTION_PAINT_ALL:
            // paint the whole screen (initially and after scrolling)
            for (i = 0; i <= State->MaxIndex; i++) {
                if (i >= State->FirstVisible && i <= State->LastVisible) {
                    refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, 2, MenuPosY + (i - State->FirstVisible));
                    if (i == State->CurrentSelection)
                       refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_CHOICE_CURRENT);
                    else
                       refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_CHOICE_BASIC);
                    refit_call2_wrapper(ST->ConOut->OutputString, ST->ConOut, DisplayStrings[i]);
                }
            }
            // scrolling indicators
            refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_SCROLLARROW);
            refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, 0, MenuPosY);
            if (State->FirstVisible > 0)
                refit_call2_wrapper(ST->ConOut->OutputString, ST->ConOut, ArrowUp);
            else
               refit_call2_wrapper(ST->ConOut->OutputString, ST->ConOut, L" ");
            refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, 0, MenuPosY + State->MaxVisible);
            if (State->LastVisible < State->MaxIndex)
               refit_call2_wrapper(ST->ConOut->OutputString, ST->ConOut, ArrowDown);
            else
               refit_call2_wrapper(ST->ConOut->OutputString, ST->ConOut, L" ");
            break;

        case MENU_FUNCTION_PAINT_SELECTION:
            // redraw selection cursor
            refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, 2, MenuPosY + (State->PreviousSelection - State->FirstVisible));
            refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_CHOICE_BASIC);
            refit_call2_wrapper(ST->ConOut->OutputString, ST->ConOut, DisplayStrings[State->PreviousSelection]);
            refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, 2, MenuPosY + (State->CurrentSelection - State->FirstVisible));
            refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_CHOICE_CURRENT);
            refit_call2_wrapper(ST->ConOut->OutputString, ST->ConOut, DisplayStrings[State->CurrentSelection]);
            break;

        case MENU_FUNCTION_PAINT_TIMEOUT:
            if (ParamText[0] == 0) {
                // clear message
                refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_BASIC);
                refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, 0, ConHeight - 1);
                refit_call2_wrapper(ST->ConOut->OutputString, ST->ConOut, BlankLine + 1);
            } else {
                // paint or update message
                refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_ERROR);
                refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, 3, ConHeight - 1);
                TimeoutMessage = PoolPrint(L"%s  ", ParamText);
                refit_call2_wrapper(ST->ConOut->OutputString, ST->ConOut, TimeoutMessage);
                FreePool(TimeoutMessage);
            }
            break;

    }
}

//
// graphical generic style
//


static VOID DrawMenuText(IN CHAR16 *Text, IN UINTN SelectedWidth, IN UINTN XPos, IN UINTN YPos)
{
    if (TextBuffer == NULL)
        TextBuffer = egCreateImage(LAYOUT_TEXT_WIDTH, TEXT_LINE_HEIGHT, FALSE);

    egFillImage(TextBuffer, &MenuBackgroundPixel);
    if (SelectedWidth > 0) {
        // draw selection bar background
        egFillImageArea(TextBuffer, 0, 0, SelectedWidth, TextBuffer->Height,
                        &SelectionBackgroundPixel);
    }

    // render the text
    egRenderText(Text, TextBuffer, TEXT_XMARGIN, TEXT_YMARGIN);
    BltImage(TextBuffer, XPos, YPos);
}

// Displays sub-menus
static VOID GraphicsMenuStyle(IN REFIT_MENU_SCREEN *Screen, IN SCROLL_STATE *State, IN UINTN Function, IN CHAR16 *ParamText)
{
    INTN i;
    UINTN ItemWidth;
    static UINTN MenuWidth, EntriesPosX, EntriesPosY, TimeoutPosY;

    State->ScrollMode = SCROLL_MODE_TEXT;
    switch (Function) {

        case MENU_FUNCTION_INIT:
            InitScroll(State, Screen->EntryCount, 0);

            // determine width of the menu
            MenuWidth = 20;  // minimum
            for (i = 0; i < (INTN)Screen->InfoLineCount; i++) {
                ItemWidth = StrLen(Screen->InfoLines[i]);
                if (MenuWidth < ItemWidth)
                    MenuWidth = ItemWidth;
            }
            for (i = 0; i <= State->MaxIndex; i++) {
                ItemWidth = StrLen(Screen->Entries[i]->Title);
                if (MenuWidth < ItemWidth)
                    MenuWidth = ItemWidth;
            }
            MenuWidth = TEXT_XMARGIN * 2 + MenuWidth * FONT_CELL_WIDTH;
            if (MenuWidth > LAYOUT_TEXT_WIDTH)
                MenuWidth = LAYOUT_TEXT_WIDTH;

            if (Screen->TitleImage)
                EntriesPosX = (UGAWidth + (Screen->TitleImage->Width + TITLEICON_SPACING) - MenuWidth) >> 1;
            else
                EntriesPosX = (UGAWidth - MenuWidth) >> 1;
            EntriesPosY = ((UGAHeight - LAYOUT_TOTAL_HEIGHT) >> 1) + LAYOUT_BANNER_YOFFSET + TEXT_LINE_HEIGHT * 2;
            TimeoutPosY = EntriesPosY + (Screen->EntryCount + 1) * TEXT_LINE_HEIGHT;

            // initial painting
            SwitchToGraphicsAndClear();
            egMeasureText(Screen->Title, &ItemWidth, NULL);
            DrawMenuText(Screen->Title, 0, ((UGAWidth - ItemWidth) >> 1) - TEXT_XMARGIN, EntriesPosY - TEXT_LINE_HEIGHT * 2);
            if (Screen->TitleImage)
                BltImageAlpha(Screen->TitleImage,
                              EntriesPosX - (Screen->TitleImage->Width + TITLEICON_SPACING), EntriesPosY,
                              &MenuBackgroundPixel);
            if (Screen->InfoLineCount > 0) {
                for (i = 0; i < (INTN)Screen->InfoLineCount; i++) {
                    DrawMenuText(Screen->InfoLines[i], 0, EntriesPosX, EntriesPosY);
                    EntriesPosY += TEXT_LINE_HEIGHT;
                }
                EntriesPosY += TEXT_LINE_HEIGHT;  // also add a blank line
            }

            break;

        case MENU_FUNCTION_CLEANUP:
            // nothing to do
            break;

        case MENU_FUNCTION_PAINT_ALL:
            for (i = 0; i <= State->MaxIndex; i++) {
                DrawMenuText(Screen->Entries[i]->Title, (i == State->CurrentSelection) ? MenuWidth : 0,
                             EntriesPosX, EntriesPosY + i * TEXT_LINE_HEIGHT);
            }
            break;

        case MENU_FUNCTION_PAINT_SELECTION:
            // redraw selection cursor
            DrawMenuText(Screen->Entries[State->PreviousSelection]->Title, 0,
                         EntriesPosX, EntriesPosY + State->PreviousSelection * TEXT_LINE_HEIGHT);
            DrawMenuText(Screen->Entries[State->CurrentSelection]->Title, MenuWidth,
                         EntriesPosX, EntriesPosY + State->CurrentSelection * TEXT_LINE_HEIGHT);
            break;

        case MENU_FUNCTION_PAINT_TIMEOUT:
            DrawMenuText(ParamText, 0, EntriesPosX, TimeoutPosY);
            break;

    }
}

//
// graphical main menu style
//

static VOID DrawMainMenuEntry(REFIT_MENU_ENTRY *Entry, BOOLEAN selected, UINTN XPos, UINTN YPos)
{
    UINTN ImageNum;

    ImageNum = ((Entry->Row == 0) ? 0 : 2) + (selected ? 0 : 1);
    if (SelectionImages != NULL)
        BltImageCompositeBadge(SelectionImages[ImageNum],
                               Entry->Image, Entry->BadgeImage, XPos, YPos);
}

static VOID DrawMainMenuText(IN CHAR16 *Text, IN UINTN XPos, IN UINTN YPos)
{
    UINTN TextWidth, TextPosX;

    if (TextBuffer == NULL)
        TextBuffer = egCreateImage(LAYOUT_TEXT_WIDTH, TEXT_LINE_HEIGHT, FALSE);

    egFillImage(TextBuffer, &MenuBackgroundPixel);

    // render the text
    egMeasureText(Text, &TextWidth, NULL);
    if (TextWidth > TextBuffer->Width)
       TextPosX = 0;
    else
       TextPosX = (TextBuffer->Width - TextWidth) / 2;
    egRenderText(Text, TextBuffer, TextPosX, 0);
//    egRenderText(Text, TextBuffer, (TextBuffer->Width - TextWidth) >> 1, 0);
    BltImage(TextBuffer, XPos, YPos);
}

static VOID PaintAll(IN REFIT_MENU_SCREEN *Screen, IN SCROLL_STATE *State, UINTN *itemPosX,
                     UINTN row0PosY, UINTN row1PosY, UINTN textPosY) {
   INTN i;

   if (Screen->Entries[State->CurrentSelection]->Row == 0)
      AdjustScrollState(State);
   for (i = State->FirstVisible; i <= State->MaxIndex; i++) {
      if (Screen->Entries[i]->Row == 0) {
         if (i <= State->LastVisible) {
            DrawMainMenuEntry(Screen->Entries[i], (i == State->CurrentSelection) ? TRUE : FALSE,
                              itemPosX[i - State->FirstVisible], row0PosY);
         } // if
      } else {
         DrawMainMenuEntry(Screen->Entries[i], (i == State->CurrentSelection) ? TRUE : FALSE, itemPosX[i], row1PosY);
      }
   }
   if (!(GlobalConfig.HideUIFlags & HIDEUI_FLAG_LABEL))
      DrawMainMenuText(Screen->Entries[State->CurrentSelection]->Title,
                       (UGAWidth - LAYOUT_TEXT_WIDTH) >> 1, textPosY);
} // static VOID PaintAll()

// Move the selection to State->CurrentSelection, adjusting icon row if necessary...
static VOID PaintSelection(IN REFIT_MENU_SCREEN *Screen, IN SCROLL_STATE *State, UINTN *itemPosX,
                           UINTN row0PosY, UINTN row1PosY, UINTN textPosY) {
   UINTN XSelectPrev, XSelectCur, YPosPrev, YPosCur;

   if (((State->CurrentSelection <= State->LastVisible) && (State->CurrentSelection >= State->FirstVisible)) ||
       (State->CurrentSelection >= State->InitialRow1) ) {
      if (Screen->Entries[State->PreviousSelection]->Row == 0) {
         XSelectPrev = State->PreviousSelection - State->FirstVisible;
         YPosPrev = row0PosY;
      } else {
         XSelectPrev = State->PreviousSelection;
         YPosPrev = row1PosY;
      } // if/else
      if (Screen->Entries[State->CurrentSelection]->Row == 0) {
         XSelectCur = State->CurrentSelection - State->FirstVisible;
         YPosCur = row0PosY;
      } else {
         XSelectCur = State->CurrentSelection;
         YPosCur = row1PosY;
      } // if/else
      DrawMainMenuEntry(Screen->Entries[State->PreviousSelection], FALSE, itemPosX[XSelectPrev], YPosPrev);
      DrawMainMenuEntry(Screen->Entries[State->CurrentSelection], TRUE, itemPosX[XSelectCur], YPosCur);
      if (!(GlobalConfig.HideUIFlags & HIDEUI_FLAG_LABEL))
         DrawMainMenuText(Screen->Entries[State->CurrentSelection]->Title,
                          (UGAWidth - LAYOUT_TEXT_WIDTH) >> 1, textPosY);
   } else { // Current selection not visible; must redraw the menu....
      MainMenuStyle(Screen, State, MENU_FUNCTION_PAINT_ALL, NULL);
   }
} // static VOID MoveSelection(VOID)

// Display an icon at the specified location. Uses the image specified by
// ExternalFilename if it's available, or BuiltInImage if it's not. The 
// Y position is specified as the center value, and so is adjusted by half
// the icon's height. The X position is set along the icon's left
// edge if Alignment == ALIGN_LEFT, and along the right edge if
// Alignment == ALIGN_RIGHT
static VOID PaintIcon(IN EG_EMBEDDED_IMAGE *BuiltInIcon, IN CHAR16 *ExternalFilename, UINTN PosX, UINTN PosY, UINTN Alignment) {
   EG_IMAGE *Icon = NULL;

   if (FileExists(SelfDir, ExternalFilename))
      Icon = egLoadIcon(SelfDir, ExternalFilename, 48);
   if (Icon == NULL)
      Icon = egPrepareEmbeddedImage(BuiltInIcon, TRUE);
   if (Icon != NULL) {
      if (Alignment == ALIGN_RIGHT)
         PosX -= Icon->Width;
      BltImageAlpha(Icon, PosX, PosY - (Icon->Height / 2), &MenuBackgroundPixel);
   }
} // static VOID PaintIcon()

// Display main menu in graphics mode
VOID MainMenuStyle(IN REFIT_MENU_SCREEN *Screen, IN SCROLL_STATE *State, IN UINTN Function, IN CHAR16 *ParamText)
{
    INTN i;
    static UINTN row0PosX, row0PosXRunning, row1PosY, row0Loaders;
    UINTN row0Count, row1Count, row1PosX, row1PosXRunning;
    static UINTN *itemPosX;
    static UINTN row0PosY, textPosY;
    CHAR16 FileName[256];

    State->ScrollMode = SCROLL_MODE_ICONS;
    switch (Function) {

        case MENU_FUNCTION_INIT:
            InitScroll(State, Screen->EntryCount, GlobalConfig.MaxTags);

            // layout
            row0Count = 0;
            row1Count = 0;
            row0Loaders = 0;
            for (i = 0; i <= State->MaxIndex; i++) {
               if (Screen->Entries[i]->Row == 1) {
                  row1Count++;
               } else {
                  row0Loaders++;
                  if (row0Count < State->MaxVisible)
                     row0Count++;
               }
            }
            row0PosX = (UGAWidth + TILE_XSPACING - (ROW0_TILESIZE + TILE_XSPACING) * row0Count) >> 1;
            row0PosY = ((UGAHeight - LAYOUT_TOTAL_HEIGHT) >> 1) + LAYOUT_BANNER_YOFFSET;
            row1PosX = (UGAWidth + TILE_XSPACING - (ROW1_TILESIZE + TILE_XSPACING) * row1Count) >> 1;
            row1PosY = row0PosY + ROW0_TILESIZE + TILE_YSPACING;
            if (row1Count > 0)
                textPosY = row1PosY + ROW1_TILESIZE + TILE_YSPACING;
            else
                textPosY = row1PosY;

            itemPosX = AllocatePool(sizeof(UINTN) * Screen->EntryCount);
            row0PosXRunning = row0PosX;
            row1PosXRunning = row1PosX;
            for (i = 0; i <= State->MaxIndex; i++) {
                if (Screen->Entries[i]->Row == 0) {
                    itemPosX[i] = row0PosXRunning;
                    row0PosXRunning += ROW0_TILESIZE + TILE_XSPACING;
                } else {
                    itemPosX[i] = row1PosXRunning;
                    row1PosXRunning += ROW1_TILESIZE + TILE_XSPACING;
                }
            }
            // initial painting
            InitSelection();
            SwitchToGraphicsAndClear();
            break;

        case MENU_FUNCTION_CLEANUP:
            FreePool(itemPosX);
            break;

        case MENU_FUNCTION_PAINT_ALL:
            BltClearScreen(TRUE);
            PaintAll(Screen, State, itemPosX, row0PosY, row1PosY, textPosY);
            // For PaintIcon() calls, the starting Y position is moved to the midpoint
            // of the surrounding row; PaintIcon() adjusts this back up by half the
            // icon's height to properly center it.
            if ((State->FirstVisible > 0) && (!(GlobalConfig.HideUIFlags & HIDEUI_FLAG_ARROWS))) {
               SPrint(FileName, 255, L"%s\\arrow_left.icns", GlobalConfig.IconsDir ? GlobalConfig.IconsDir : DEFAULT_ICONS_DIR);
               PaintIcon(&egemb_arrow_left, FileName, row0PosX - TILE_XSPACING,
                         row0PosY + (ROW0_TILESIZE / 2), ALIGN_RIGHT);
            } // if
            if ((State->LastVisible < (row0Loaders - 1)) && (!(GlobalConfig.HideUIFlags & HIDEUI_FLAG_ARROWS))) {
               SPrint(FileName, 255, L"%s\\arrow_right.icns", GlobalConfig.IconsDir ? GlobalConfig.IconsDir : DEFAULT_ICONS_DIR);
               PaintIcon(&egemb_arrow_right, FileName,
                         (UGAWidth + (ROW0_TILESIZE + TILE_XSPACING) * State->MaxVisible) / 2 + TILE_XSPACING,
                         row0PosY + (ROW0_TILESIZE / 2), ALIGN_LEFT);
            } // if
            break;

        case MENU_FUNCTION_PAINT_SELECTION:
            PaintSelection(Screen, State, itemPosX, row0PosY, row1PosY, textPosY);
            break;

        case MENU_FUNCTION_PAINT_TIMEOUT:
            if (!(GlobalConfig.HideUIFlags & HIDEUI_FLAG_LABEL))
                DrawMainMenuText(ParamText, (UGAWidth - LAYOUT_TEXT_WIDTH) >> 1, textPosY + TEXT_LINE_HEIGHT);
            break;

    }
}

//
// user-callable dispatcher functions
//

UINTN RunMenu(IN REFIT_MENU_SCREEN *Screen, OUT REFIT_MENU_ENTRY **ChosenEntry)
{
    MENU_STYLE_FUNC Style = TextMenuStyle;

    if (AllowGraphicsMode)
        Style = GraphicsMenuStyle;

    return RunGenericMenu(Screen, Style, -1, ChosenEntry);
}

UINTN RunMainMenu(IN REFIT_MENU_SCREEN *Screen, IN CHAR16* DefaultSelection, OUT REFIT_MENU_ENTRY **ChosenEntry)
{
    MENU_STYLE_FUNC Style = TextMenuStyle;
    MENU_STYLE_FUNC MainStyle = TextMenuStyle;
    REFIT_MENU_ENTRY *TempChosenEntry;
    UINTN MenuExit = 0;
    UINTN DefaultEntryIndex = -1;

    if (DefaultSelection != NULL) {
        // Find a menu entry that includes *DefaultSelection as a substring
        DefaultEntryIndex = FindMenuShortcutEntry(Screen, DefaultSelection);
        // If that didn't work, should we scan more characters?  For now, no.
    }

    if (AllowGraphicsMode) {
        Style = GraphicsMenuStyle;
        MainStyle = MainMenuStyle;
    }

    while (!MenuExit) {
        MenuExit = RunGenericMenu(Screen, MainStyle, DefaultEntryIndex, &TempChosenEntry);
        Screen->TimeoutSeconds = 0;

        if (MenuExit == MENU_EXIT_DETAILS && TempChosenEntry->SubScreen != NULL) {
            MenuExit = RunGenericMenu(TempChosenEntry->SubScreen, Style, -1, &TempChosenEntry);
            if (MenuExit == MENU_EXIT_ESCAPE || TempChosenEntry->Tag == TAG_RETURN)
                MenuExit = 0;
        }
    }

    if (ChosenEntry)
        *ChosenEntry = TempChosenEntry;
    return MenuExit;
} /* UINTN RunMainMenu() */

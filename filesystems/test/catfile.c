/**
 * \file catfile.c
 * Test program for the POSIX user space environment.
 */

/*-
 * Copyright (c) 2012 Stefan Agner
 * Copyright (c) 2006 Christoph Pfisterer
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

#define FSW_DEBUG_LEVEL 3

#include "fsw_posix.h"


extern struct fsw_fstype_table FSW_FSTYPE_TABLE_NAME(FSTYPE);

static struct fsw_fstype_table *fstypes[] = {
    &FSW_FSTYPE_TABLE_NAME(FSTYPE),
    NULL
};

int main(int argc, char **argv)
{
    struct fsw_posix_volume *vol;
    int i;

    if (argc != 3) {
        fprintf(stderr, "Usage: catfile <file/device> <file>\n");
        return 1;
    }

    for (i = 0; fstypes[i]; i++) {
        vol = fsw_posix_mount(argv[1], fstypes[i]);
        if (vol != NULL) {
            fprintf(stderr, "Mounted as '%s'.\n", (char *)fstypes[i]->name.data);
            break;
        }
    }
    if (vol == NULL) {
        fprintf(stderr, "Mounting failed.\n");
        return 1;
    }

    catfile(vol, argv[2]);

    fsw_posix_unmount(vol);

    return 0;
}

// EOF

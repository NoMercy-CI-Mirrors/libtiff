/*
 * Copyright (c) 2026, 0b53rv3r <tripathyomm09@gmail.com>
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that (i) the above copyright notices and this permission notice appear in
 * all copies of the software and related documentation, and (ii) the names of
 * Sam Leffler and Silicon Graphics may not be used in any advertising or
 * publicity relating to the software without the specific, prior written
 * permission of Sam Leffler and Silicon Graphics.
 *
 * THE SOFTWARE IS PROVIDED "AS-IS" AND WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS, IMPLIED OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * IN NO EVENT SHALL SAM LEFFLER OR SILICON GRAPHICS BE LIABLE FOR
 * ANY SPECIAL, INCIDENTAL, INDIRECT OR CONSEQUENTIAL DAMAGES OF ANY KIND,
 * OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER OR NOT ADVISED OF THE POSSIBILITY OF DAMAGE, AND ON ANY THEORY OF
 * LIABILITY, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

/*
 * TIFFReadFromUserBuffer() must reject a nonpositive input size. It used to
 * accept it and then decode the strip with the byte count declared in the
 * file, reading beyond the caller's input buffer.
 */

#include "tif_config.h"

#include <stdio.h>

#include "tiffio.h"

int main(void)
{
    unsigned char input[64] = {0};
    unsigned char output[64];
    TIFF *tif;
    int ret = 0;

    tif = TIFFOpen(SOURCE_DIR "/images/minisblack-1c-8b.tiff", "r");
    if (tif == NULL)
    {
        fprintf(stderr, "Cannot open the test TIFF\n");
        return 1;
    }

    if (TIFFReadFromUserBuffer(tif, 0, input, 0, output, sizeof(output)) != 0)
    {
        fprintf(stderr, "TIFFReadFromUserBuffer() accepted insize 0\n");
        ret = 1;
    }
    if (TIFFReadFromUserBuffer(tif, 0, input, -1, output, sizeof(output)) != 0)
    {
        fprintf(stderr, "TIFFReadFromUserBuffer() accepted insize -1\n");
        ret = 1;
    }

    TIFFClose(tif);
    return ret;
}

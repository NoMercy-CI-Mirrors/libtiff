/*
 * Copyright (c) 2026, Andrew Stellman <astellman at stellman-greene.com>
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
 * TIFF Library
 *
 * Test that TIFFReadBufferSetup() between two TIFFReadScanline() calls on
 * the same strip makes the next read load the strip into the new buffer:
 * a library-owned buffer, a rejected (zero size) replacement and a
 * caller-supplied buffer.
 */

#include "tif_config.h"

#include <stdio.h>
#include <string.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "tiffio.h"

#define WIDTH 8
#define HEIGHT 4

static int check_row(TIFF *tif, uint32_t row)
{
    unsigned char buf[WIDTH];
    int i;

    if (TIFFReadScanline(tif, buf, row, 0) != 1)
    {
        fprintf(stderr, "Reading row %u failed\n", row);
        return 1;
    }
    for (i = 0; i < WIDTH; i++)
    {
        if (buf[i] != (unsigned char)(row * WIDTH + (uint32_t)i))
        {
            fprintf(stderr, "Wrong data in row %u\n", row);
            return 1;
        }
    }
    return 0;
}

int main(void)
{
    static const char filename[] = "test_read_buffer_setup.tif";
    static unsigned char user_buffer[16384];
    unsigned char row[WIDTH];
    uint32_t y;
    int i;
    int ret = 0;

    TIFF *tif = TIFFOpen(filename, "w");
    if (!tif)
    {
        fprintf(stderr, "Cannot create %s\n", filename);
        return 1;
    }
    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, WIDTH);
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, HEIGHT);
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1);
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
    TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, HEIGHT);
    for (y = 0; y < HEIGHT; y++)
    {
        for (i = 0; i < WIDTH; i++)
            row[i] = (unsigned char)(y * WIDTH + (uint32_t)i);
        if (TIFFWriteScanline(tif, row, y, 0) != 1)
        {
            fprintf(stderr, "Writing row %u failed\n", y);
            TIFFClose(tif);
            return 1;
        }
    }
    TIFFClose(tif);

    /* "m": no memory mapping, so the strip is read into tif_rawdata. */
    tif = TIFFOpen(filename, "rm");
    if (!tif)
    {
        fprintf(stderr, "Cannot open %s\n", filename);
        return 1;
    }
    ret |= check_row(tif, 0);

    /* Library-owned buffer. */
    if (!TIFFReadBufferSetup(tif, NULL, WIDTH * HEIGHT))
    {
        fprintf(stderr, "TIFFReadBufferSetup with an owned buffer failed\n");
        ret = 1;
    }
    ret |= check_row(tif, 1);

    /* Rejected replacement: it frees the owned buffer and returns 0. */
    if (TIFFReadBufferSetup(tif, NULL, 0))
    {
        fprintf(stderr, "TIFFReadBufferSetup with size 0 succeeded\n");
        ret = 1;
    }
    ret |= check_row(tif, 2);

    /* Caller-supplied buffer: the strip must be loaded into it. */
    if (!TIFFReadBufferSetup(tif, user_buffer, sizeof(user_buffer)))
    {
        fprintf(stderr, "TIFFReadBufferSetup with a user buffer failed\n");
        ret = 1;
    }
    ret |= check_row(tif, 3);
    for (i = 0; i < WIDTH * HEIGHT; i++)
    {
        if (user_buffer[i] != (unsigned char)i)
        {
            fprintf(stderr, "The strip was not loaded into the user buffer\n");
            ret = 1;
            break;
        }
    }

    TIFFClose(tif);
    unlink(filename);
    return ret;
}

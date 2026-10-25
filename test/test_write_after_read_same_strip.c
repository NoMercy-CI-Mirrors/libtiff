/*
 * Copyright (c) 2026, observer04 <tripathysarojinee@gmail.com>
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
 * Write scanlines of a strip, read that strip back, then write to it again
 * on the same handle. Reading tears down the encoder state of the WebP and
 * ZSTD codecs; the second write must fail cleanly instead of using it.
 */

#include "tif_config.h"

#include <stdio.h>
#include <string.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "tiffio.h"

#define WIDTH 16
#define HEIGHT 16
#define ROWSPERSTRIP 8
#define SPP 4

static int test_codec(uint16_t compression, const char *name)
{
    static const char filename[] = "test_write_after_read_same_strip.tif";
    unsigned char line[WIDTH * SPP];
    unsigned char strip[WIDTH * ROWSPERSTRIP * SPP];
    TIFF *tif;
    int row;
    int ret = 0;

    if (!TIFFIsCODECConfigured(compression))
        return 0;

    tif = TIFFOpen(filename, "w");
    if (!tif)
    {
        fprintf(stderr, "Cannot create %s\n", filename);
        return 1;
    }
    if (!TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, WIDTH) ||
        !TIFFSetField(tif, TIFFTAG_IMAGELENGTH, HEIGHT) ||
        !TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8) ||
        !TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, SPP) ||
        !TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB) ||
        !TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG) ||
        !TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, ROWSPERSTRIP) ||
        !TIFFSetField(tif, TIFFTAG_COMPRESSION, compression))
    {
        fprintf(stderr, "%s: cannot set tags\n", name);
        TIFFClose(tif);
        unlink(filename);
        return 1;
    }

    memset(line, 0xAB, sizeof(line));
    for (row = 0; row < ROWSPERSTRIP; row++)
    {
        if (TIFFWriteScanline(tif, line, (uint32_t)row, 0) != 1)
        {
            fprintf(stderr, "%s: TIFFWriteScanline(%d) failed\n", name, row);
            ret = 1;
        }
    }
    /* Starting strip 1 flushes strip 0. */
    if (ret == 0 && TIFFWriteScanline(tif, line, ROWSPERSTRIP, 0) != 1)
    {
        fprintf(stderr, "%s: TIFFWriteScanline(%d) failed\n", name,
                ROWSPERSTRIP);
        ret = 1;
    }
    if (ret == 0 &&
        TIFFReadEncodedStrip(tif, 0, strip, sizeof(strip)) != sizeof(strip))
    {
        fprintf(stderr, "%s: TIFFReadEncodedStrip(0) failed\n", name);
        ret = 1;
    }
    /* Used to crash (NULL buffer) or hit an assertion. */
    if (ret == 0 && TIFFWriteScanline(tif, line, 0, 0) == 1)
    {
        fprintf(stderr,
                "%s: TIFFWriteScanline() after TIFFReadEncodedStrip() "
                "should have failed\n",
                name);
        ret = 1;
    }
    TIFFClose(tif);
    unlink(filename);
    return ret;
}

int main(void)
{
    int ret = 0;
    ret |= test_codec(COMPRESSION_WEBP, "WebP");
    ret |= test_codec(COMPRESSION_ZSTD, "ZSTD");
    return ret;
}

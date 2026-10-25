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
 * Check that a scanline write left pending on one directory is dropped when
 * another directory is read, instead of being finalized later with the codec
 * state of the new directory.
 */

#include "tif_config.h"

#include <stdio.h>
#include <string.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "tiffiop.h"

#define WIDTH 16
#define HEIGHT 16

static const char filename[] = "test_postencode_dir_switch.tif";

static int write_file(void)
{
    unsigned char buf[WIDTH * HEIGHT];
    TIFF *tif;
    int dir;
    int ok = 1;

    tif = TIFFOpen(filename, "w");
    if (!tif)
    {
        fprintf(stderr, "Cannot create %s\n", filename);
        return 0;
    }
    for (dir = 0; ok && dir < 2; dir++)
    {
        memset(buf, 0x10 * (dir + 1), sizeof(buf));
        ok = TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, WIDTH) &&
             TIFFSetField(tif, TIFFTAG_IMAGELENGTH, HEIGHT) &&
             TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8) &&
             TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1) &&
             TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK) &&
             TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG) &&
             TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, HEIGHT) &&
             TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_LZW) &&
             TIFFWriteEncodedStrip(tif, 0, buf, sizeof(buf)) ==
                 (tmsize_t)sizeof(buf) &&
             TIFFWriteDirectory(tif);
    }
    TIFFClose(tif);
    return ok;
}

static int check_dir1(TIFF *tif)
{
    unsigned char buf[WIDTH * HEIGHT];
    size_t i;

    if (TIFFReadEncodedStrip(tif, 0, buf, sizeof(buf)) != (tmsize_t)sizeof(buf))
    {
        fprintf(stderr, "TIFFReadEncodedStrip() of directory 1 failed\n");
        return 0;
    }
    for (i = 0; i < sizeof(buf); i++)
    {
        if (buf[i] != 0x20)
        {
            fprintf(stderr, "Directory 1 byte %d is 0x%02x, expected 0x20\n",
                    (int)i, buf[i]);
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    unsigned char line[WIDTH];
    TIFF *tif;
    int ret = 0;

    if (!TIFFIsCODECConfigured(COMPRESSION_LZW))
        return 0;

    if (!write_file())
        return 1;

    tif = TIFFOpen(filename, "r+");
    if (!tif)
    {
        fprintf(stderr, "Cannot open %s\n", filename);
        return 1;
    }
    /* Write the first scanline of directory 0 and leave the strip pending. */
    memset(line, 0x55, sizeof(line));
    if (TIFFWriteScanline(tif, line, 0, 0) != 1)
    {
        fprintf(stderr, "TIFFWriteScanline() failed\n");
        ret = 1;
    }
    else if (!TIFFSetDirectory(tif, 1))
    {
        fprintf(stderr, "TIFFSetDirectory(1) failed\n");
        ret = 1;
    }
    else if (tif->tif_flags & TIFF_POSTENCODE)
    {
        fprintf(stderr, "TIFF_POSTENCODE still set after TIFFSetDirectory()\n");
        ret = 1;
    }
    else if (!check_dir1(tif) || !TIFFRewriteDirectory(tif))
    {
        ret = 1;
    }
    TIFFClose(tif);

    if (ret == 0)
    {
        tif = TIFFOpen(filename, "r");
        if (!tif || !TIFFSetDirectory(tif, 1) || !check_dir1(tif))
            ret = 1;
        if (tif)
            TIFFClose(tif);
    }

    unlink(filename);
    return ret;
}

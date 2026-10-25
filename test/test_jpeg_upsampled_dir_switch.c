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
 * Check that selecting JPEGCOLORMODE_RGB on a YCbCr JPEG directory does not
 * change the strip size computed for another YCbCr directory of the file.
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

static const char filename[] = "test_jpeg_upsampled_dir_switch.tif";

static int write_dir(TIFF *tif, uint16_t compression)
{
    /* 2x2 subsampled YCbCr: 4 Y + Cb + Cr per 2x2 block */
    unsigned char buf[(WIDTH / 2) * (HEIGHT / 2) * 6];

    memset(buf, 0x80, sizeof(buf));
    if (!TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, WIDTH) ||
        !TIFFSetField(tif, TIFFTAG_IMAGELENGTH, HEIGHT) ||
        !TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8) ||
        !TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 3) ||
        !TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG) ||
        !TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_YCBCR) ||
        !TIFFSetField(tif, TIFFTAG_YCBCRSUBSAMPLING, 2, 2) ||
        !TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, HEIGHT) ||
        !TIFFSetField(tif, TIFFTAG_COMPRESSION, compression))
        return 0;
    if (compression == COMPRESSION_JPEG)
    {
        unsigned char rgb[WIDTH * 3];
        int row;

        if (!TIFFSetField(tif, TIFFTAG_JPEGCOLORMODE, JPEGCOLORMODE_RGB))
            return 0;
        memset(rgb, 0x80, sizeof(rgb));
        for (row = 0; row < HEIGHT; row++)
        {
            if (TIFFWriteScanline(tif, rgb, (uint32_t)row, 0) != 1)
                return 0;
        }
    }
    else if (TIFFWriteEncodedStrip(tif, 0, buf, sizeof(buf)) !=
             (tmsize_t)sizeof(buf))
    {
        return 0;
    }
    return TIFFWriteDirectory(tif);
}

int main(void)
{
    TIFF *tif;
    tmsize_t size_before;
    tmsize_t size_after;
    int ret = 0;

    if (!TIFFIsCODECConfigured(COMPRESSION_JPEG) ||
        !TIFFIsCODECConfigured(COMPRESSION_LZW))
        return 0;

    tif = TIFFOpen(filename, "w");
    if (!tif)
    {
        fprintf(stderr, "Cannot create %s\n", filename);
        return 1;
    }
    if (!write_dir(tif, COMPRESSION_LZW) || !write_dir(tif, COMPRESSION_JPEG))
    {
        fprintf(stderr, "Cannot write %s\n", filename);
        TIFFClose(tif);
        unlink(filename);
        return 1;
    }
    TIFFClose(tif);

    tif = TIFFOpen(filename, "r");
    if (!tif)
    {
        fprintf(stderr, "Cannot open %s\n", filename);
        unlink(filename);
        return 1;
    }
    size_before = TIFFStripSize(tif);
    if (!TIFFSetDirectory(tif, 1) ||
        !TIFFSetField(tif, TIFFTAG_JPEGCOLORMODE, JPEGCOLORMODE_RGB) ||
        !TIFFSetDirectory(tif, 0))
    {
        fprintf(stderr, "Cannot switch directories\n");
        ret = 1;
    }
    else
    {
        size_after = TIFFStripSize(tif);
        if (size_after != size_before)
        {
            fprintf(stderr,
                    "TIFFStripSize() of directory 0 is %d after visiting "
                    "directory 1, expected %d\n",
                    (int)size_after, (int)size_before);
            ret = 1;
        }
    }
    TIFFClose(tif);
    unlink(filename);
    return ret;
}

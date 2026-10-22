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

#include "tif_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiffio.h"

/* A stored zlib block that expands to 64 'A' bytes. */
static const unsigned char oversized_stream[] = {
    0x78, 0x01, 0x01, 0x40, 0x00, 0xbf, 0xff, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x10, 0x7e, 0x10, 0x41,
};

static int make_tiff(const char *filename)
{
    TIFF *tif = TIFFOpen(filename, "w");
    int ok = tif != NULL;

    if (!ok)
        return 0;
    ok = TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, 16) &&
         TIFFSetField(tif, TIFFTAG_IMAGELENGTH, 2) &&
         TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8) &&
         TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1) &&
         TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK) &&
         TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG) &&
         TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, 2) &&
         TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_ADOBE_DEFLATE) &&
         TIFFWriteRawStrip(tif, 0, (void *)oversized_stream,
                           (tmsize_t)sizeof(oversized_stream)) ==
             (tmsize_t)sizeof(oversized_stream);
    TIFFClose(tif);
    return ok;
}

int main(void)
{
    const char *filename = "test_deflate_extra_data.tif";
    unsigned char strip[32];
    TIFF *tif;
    tmsize_t size;
    size_t i;
    int status = 0;

    if (!TIFFIsCODECConfigured(COMPRESSION_ADOBE_DEFLATE))
        return 0;

    if (!make_tiff(filename))
    {
        fprintf(stderr, "Cannot create %s\n", filename);
        return 1;
    }

    tif = TIFFOpen(filename, "r");
    if (tif == NULL)
    {
        remove(filename);
        return 1;
    }

    /* Skip when this build does not contain the affected fast path. */
    if (!TIFFSetField(tif, TIFFTAG_DEFLATE_SUBCODEC,
                      DEFLATE_SUBCODEC_LIBDEFLATE))
    {
        TIFFClose(tif);
        remove(filename);
        return 0;
    }

    memset(strip, 0xaa, sizeof(strip));
    size = TIFFReadEncodedStrip(tif, 0, strip, sizeof(strip));
    TIFFClose(tif);
    remove(filename);

    if (size != (tmsize_t)sizeof(strip))
    {
        fprintf(stderr, "TIFFReadEncodedStrip() returned %ld, expected %zu\n",
                (long)size, sizeof(strip));
        return 1;
    }

    for (i = 0; i < sizeof(strip); ++i)
    {
        if (strip[i] != 'A')
        {
            fprintf(stderr,
                    "Decoded strip differs at offset %zu: got 0x%02x, expected "
                    "0x41%s\n",
                    i, strip[i], strip[i] == 0xaa ? " (uninitialized)" : "");
            status = 1;
            break;
        }
    }
    return status;
}

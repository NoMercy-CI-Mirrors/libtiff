/*
 * Copyright (c) 2026, Vivek Kumar
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
 * Check that the PackBits encoder never writes past the end of a small raw
 * data buffer installed with TIFFWriteBufferSetup(), on either the scanline
 * path (TIFFWriteScanline) or the strip path (TIFFWriteEncodedStrip).
 *
 * For every buffer size from 1 to MAXBUF the image is written into a buffer
 * followed by a guard region. The write must either report an error, or
 * succeed and read back identical pixels. The guard must stay untouched in
 * both cases.
 */

#include "tif_config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "tiffio.h"

#define FILENAME "test_packbits_small_write_buffer.tif"
#define WIDTH 64
#define HEIGHT 2
#define MAXBUF 200
#define GUARD 256
#define GUARD_BYTE 0xA5

/* Literal-heavy rows: a single long literal is what overflowed the buffer. */
static void fill_image(uint8_t *img)
{
    uint32_t x, y;
    for (y = 0; y < HEIGHT; y++)
        for (x = 0; x < WIDTH; x++)
            img[y * WIDTH + x] = (uint8_t)(x * 37u + y * 11u + (x * x) / 5u);
}

static TIFF *create_image(uint8_t *buf, size_t size)
{
    TIFF *tif = TIFFOpen(FILENAME, "w");
    if (tif == NULL)
        return NULL;
    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, WIDTH);
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, HEIGHT);
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1);
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
    TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, HEIGHT);
    TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_PACKBITS);
    if (!TIFFWriteBufferSetup(tif, buf, (tmsize_t)size))
    {
        TIFFClose(tif);
        return NULL;
    }
    return tif;
}

/* Returns 0 if the case passes, 1 if it fails. */
static int test_size(size_t size, int strip_mode)
{
    uint8_t img[WIDTH * HEIGHT];
    uint8_t readback[WIDTH * HEIGHT];
    uint8_t *buf;
    TIFF *tif;
    int wrote_ok = 1;
    size_t i;
    uint32_t y;

    fill_image(img);
    buf = (uint8_t *)malloc(size + GUARD);
    if (buf == NULL)
        return 1;
    memset(buf + size, GUARD_BYTE, GUARD);

    tif = create_image(buf, size);
    if (tif == NULL)
    {
        free(buf);
        return 1;
    }
    if (strip_mode)
    {
        wrote_ok =
            TIFFWriteEncodedStrip(tif, 0, img, (tmsize_t)sizeof(img)) != -1;
    }
    else
    {
        for (y = 0; y < HEIGHT && wrote_ok; y++)
            wrote_ok = TIFFWriteScanline(tif, img + y * WIDTH, y, 0) == 1;
    }
    TIFFClose(tif);

    for (i = 0; i < GUARD; i++)
    {
        if (buf[size + i] != GUARD_BYTE)
        {
            fprintf(stderr,
                    "size %zu (%s): write past the end of the raw buffer\n",
                    size, strip_mode ? "strip" : "scanline");
            free(buf);
            return 1;
        }
    }
    free(buf);

    if (!wrote_ok)
        return 0; /* refused with an error: acceptable */

    tif = TIFFOpen(FILENAME, "r");
    if (tif == NULL)
        return 1;
    if (strip_mode)
    {
        if (TIFFReadEncodedStrip(tif, 0, readback,
                                 (tmsize_t)sizeof(readback)) == -1)
        {
            TIFFClose(tif);
            fprintf(stderr,
                    "size %zu (strip): reported success but cannot be read\n",
                    size);
            return 1;
        }
    }
    else
    {
        for (y = 0; y < HEIGHT; y++)
        {
            if (TIFFReadScanline(tif, readback + y * WIDTH, y, 0) != 1)
            {
                TIFFClose(tif);
                fprintf(stderr,
                        "size %zu (scanline): reported success but row %u "
                        "cannot be read\n",
                        size, (unsigned)y);
                return 1;
            }
        }
    }
    TIFFClose(tif);
    if (memcmp(readback, img, sizeof(img)) != 0)
    {
        fprintf(stderr, "size %zu (%s): reported success but the data differ\n",
                size, strip_mode ? "strip" : "scanline");
        return 1;
    }
    return 0;
}

int main(void)
{
    size_t size;
    int failures = 0;

    for (size = 1; size <= MAXBUF; size++)
    {
        failures += test_size(size, 0);
        failures += test_size(size, 1);
    }
#ifdef HAVE_UNISTD_H
    unlink(FILENAME);
#endif
    if (failures)
    {
        fprintf(stderr, "%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

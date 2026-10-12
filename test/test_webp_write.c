/*
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
 * Test that a WebP strip of which only some rows were written does not
 * contain uninitialized memory: the rows that were not written must read
 * back as zero.
 */

#include "tif_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "tiffio.h"

#define WIDTH 64
#define ROWSPERSTRIP 16
#define LENGTH (2 * ROWSPERSTRIP)

static const char filename[] = "test_webp_write.tif";

static int write_file(void)
{
    unsigned char row[WIDTH * 3];
    uint32_t i;
    TIFF *tif = TIFFOpen(filename, "w");

    if (tif == NULL)
    {
        fprintf(stderr, "Cannot create %s\n", filename);
        return 1;
    }
    if (!TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, WIDTH) ||
        !TIFFSetField(tif, TIFFTAG_IMAGELENGTH, LENGTH) ||
        !TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8) ||
        !TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 3) ||
        !TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG) ||
        !TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB) ||
        !TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, ROWSPERSTRIP) ||
        !TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_WEBP) ||
        !TIFFSetField(tif, TIFFTAG_WEBP_LOSSLESS, 1))
    {
        fprintf(stderr, "Cannot set tags\n");
        TIFFClose(tif);
        return 1;
    }

    /* Fill the whole first strip with a non-zero pattern. */
    memset(row, 0xAB, sizeof(row));
    for (i = 0; i < ROWSPERSTRIP; i++)
    {
        if (TIFFWriteScanline(tif, row, i, 0) < 0)
        {
            fprintf(stderr, "Cannot write row %u\n", (unsigned)i);
            TIFFClose(tif);
            return 1;
        }
    }

    /* Write only the first row of the second strip. */
    memset(row, 0x11, sizeof(row));
    if (TIFFWriteScanline(tif, row, ROWSPERSTRIP, 0) < 0)
    {
        fprintf(stderr, "Cannot write row %d\n", ROWSPERSTRIP);
        TIFFClose(tif);
        return 1;
    }
    TIFFClose(tif);
    return 0;
}

static int check_file(void)
{
    const tmsize_t rowsize = WIDTH * 3;
    unsigned char *buf;
    tmsize_t i, size;
    int ret = 0;
    TIFF *tif = TIFFOpen(filename, "r");

    if (tif == NULL)
    {
        fprintf(stderr, "Cannot open %s\n", filename);
        return 1;
    }
    size = TIFFStripSize(tif);
    buf = (unsigned char *)malloc((size_t)size);
    if (buf == NULL || TIFFReadEncodedStrip(tif, 1, buf, size) != size)
    {
        fprintf(stderr, "Cannot read strip 1\n");
        free(buf);
        TIFFClose(tif);
        return 1;
    }
    for (i = 0; i < rowsize; i++)
    {
        if (buf[i] != 0x11)
        {
            fprintf(stderr, "Written row does not round trip\n");
            ret = 1;
            break;
        }
    }
    for (i = rowsize; i < size; i++)
    {
        if (buf[i] != 0)
        {
            fprintf(stderr,
                    "Unwritten part of the strip is not zero at byte %ld "
                    "(0x%02x)\n",
                    (long)i, (unsigned)buf[i]);
            ret = 1;
            break;
        }
    }
    free(buf);
    TIFFClose(tif);
    return ret;
}

int main(void)
{
    int ret = write_file();
    if (ret == 0)
        ret = check_file();
    unlink(filename);
    return ret;
}

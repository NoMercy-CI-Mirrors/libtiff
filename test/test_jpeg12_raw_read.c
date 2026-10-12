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
 * Test that 12-bit YCbCr JPEG data read in raw (downsampled) mode keeps all
 * 12 bits of each sample.
 */

#include "tif_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "tiffio.h"

#define WIDTH 16
#define LENGTH 16
/* Mid-scale: encodes as a DC coefficient of 0, so it reads back exactly. */
#define VALUE 0x800U

static const char filename[] = "test_jpeg12_raw_read.tif";

/* Store a 12-bit sample in a packed 12-bit buffer. */
static void put_sample(unsigned char *buf, tmsize_t i, unsigned value)
{
    unsigned char *p = buf + (i / 2) * 3;

    if (i % 2 == 0)
    {
        p[0] = (unsigned char)(value >> 4);
        p[1] = (unsigned char)((p[1] & 0x0f) | ((value & 0x0f) << 4));
    }
    else
    {
        p[1] = (unsigned char)((p[1] & 0xf0) | (value >> 8));
        p[2] = (unsigned char)(value & 0xff);
    }
}

static unsigned get_sample(const unsigned char *buf, tmsize_t i)
{
    const unsigned char *p = buf + (i / 2) * 3;

    if (i % 2 == 0)
        return ((unsigned)p[0] << 4) | (p[1] >> 4);
    return ((unsigned)(p[1] & 0x0f) << 8) | p[2];
}

static int write_file(void)
{
    unsigned char *buf;
    tmsize_t size, i;
    TIFF *tif = TIFFOpen(filename, "w");

    if (tif == NULL)
    {
        fprintf(stderr, "Cannot create %s\n", filename);
        return 1;
    }
    if (!TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, WIDTH) ||
        !TIFFSetField(tif, TIFFTAG_IMAGELENGTH, LENGTH) ||
        !TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 12) ||
        !TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 3) ||
        !TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG) ||
        !TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_YCBCR) ||
        !TIFFSetField(tif, TIFFTAG_YCBCRSUBSAMPLING, 2, 2) ||
        !TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, LENGTH) ||
        !TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_JPEG))
    {
        fprintf(stderr, "Cannot set tags\n");
        TIFFClose(tif);
        return 1;
    }

    size = TIFFStripSize(tif);
    buf = (unsigned char *)calloc(1, (size_t)size);
    if (buf == NULL)
    {
        TIFFClose(tif);
        return 1;
    }
    for (i = 0; i < size * 2 / 3; i++)
        put_sample(buf, i, VALUE);
    if (TIFFWriteEncodedStrip(tif, 0, buf, size) != size)
    {
        fprintf(stderr, "Cannot write strip 0\n");
        free(buf);
        TIFFClose(tif);
        return 1;
    }

    free(buf);
    TIFFClose(tif);
    return 0;
}

static int check_file(void)
{
    unsigned char *buf;
    tmsize_t size, i;
    int ret = 0;
    TIFF *tif = TIFFOpen(filename, "r");

    if (tif == NULL)
    {
        fprintf(stderr, "Cannot open %s\n", filename);
        return 1;
    }
    size = TIFFStripSize(tif);
    buf = (unsigned char *)malloc((size_t)size);
    if (buf == NULL || TIFFReadEncodedStrip(tif, 0, buf, size) != size)
    {
        fprintf(stderr, "Cannot read strip 0\n");
        free(buf);
        TIFFClose(tif);
        return 1;
    }
    for (i = 0; i < size * 2 / 3; i++)
    {
        unsigned value = get_sample(buf, i);
        if (value != VALUE)
        {
            fprintf(stderr, "Sample %ld is 0x%03x, expected 0x%03x\n", (long)i,
                    value, VALUE);
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

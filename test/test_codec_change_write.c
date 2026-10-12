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
 * Test writing a directory after its compression scheme was changed: the
 * private tags of the previous codec must not be written.
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

static const char filename[] = "test_codec_change_write.tif";

/* Set the compression to "first", then to "second", each followed by a
 * horizontal predictor if requested, and write one row. */
static int write_file(uint16_t first, int first_predictor, uint16_t second,
                      int second_predictor)
{
    unsigned char row[WIDTH];
    TIFF *tif = TIFFOpen(filename, "w");

    if (tif == NULL)
    {
        fprintf(stderr, "Cannot create %s\n", filename);
        return 1;
    }
    if (!TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, WIDTH) ||
        !TIFFSetField(tif, TIFFTAG_IMAGELENGTH, 1) ||
        !TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8) ||
        !TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1) ||
        !TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK) ||
        !TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, 1) ||
        !TIFFSetField(tif, TIFFTAG_COMPRESSION, first) ||
        (first_predictor &&
         !TIFFSetField(tif, TIFFTAG_PREDICTOR, PREDICTOR_HORIZONTAL)) ||
        !TIFFSetField(tif, TIFFTAG_COMPRESSION, second) ||
        (second_predictor &&
         !TIFFSetField(tif, TIFFTAG_PREDICTOR, PREDICTOR_HORIZONTAL)))
    {
        fprintf(stderr, "Cannot set tags\n");
        TIFFClose(tif);
        return 1;
    }
    memset(row, 0x42, sizeof(row));
    if (TIFFWriteScanline(tif, row, 0, 0) < 0)
    {
        fprintf(stderr, "Cannot write row\n");
        TIFFClose(tif);
        return 1;
    }
    if (!TIFFWriteDirectory(tif))
    {
        fprintf(stderr, "Cannot write directory\n");
        TIFFClose(tif);
        return 1;
    }
    TIFFClose(tif);
    return 0;
}

static uint32_t get16(const unsigned char *p, int big)
{
    return big ? ((uint32_t)p[0] << 8) | p[1] : ((uint32_t)p[1] << 8) | p[0];
}

static uint32_t get32(const unsigned char *p, int big)
{
    return big ? (get16(p, 1) << 16) | get16(p + 2, 1)
               : (get16(p + 2, 0) << 16) | get16(p, 0);
}

/* Return whether the first IFD of the (classic TIFF) file has an entry for
 * "tag", or -1 on error. The file is parsed directly because libtiff does not
 * report every entry of the file through its API. */
static int file_has_tag(uint32_t tag)
{
    unsigned char buf[12];
    uint32_t i, n;
    int big, found = 0;
    FILE *f = fopen(filename, "rb");

    if (f == NULL)
        return -1;
    if (fread(buf, 1, 8, f) != 8)
        found = -1;
    big = buf[0] == 'M';
    if (found == 0 && fseek(f, (long)get32(buf + 4, big), SEEK_SET) == 0 &&
        fread(buf, 1, 2, f) == 2)
    {
        n = get16(buf, big);
        for (i = 0; i < n; i++)
        {
            if (fread(buf, 1, 12, f) != 12)
            {
                found = -1;
                break;
            }
            if (get16(buf, big) == tag)
                found = 1;
        }
    }
    else
        found = -1;
    fclose(f);
    return found;
}

/* Check the compression and the predictor, and that the file has no entry
 * for "stray_tag", a private tag of the previous codec. */
static int check_file(uint16_t compression, uint16_t predictor,
                      uint32_t stray_tag)
{
    uint16_t v = 0;
    int ret = 0;
    TIFF *tif = TIFFOpen(filename, "r");

    if (tif == NULL)
    {
        fprintf(stderr, "Cannot open %s\n", filename);
        return 1;
    }
    if (!TIFFGetField(tif, TIFFTAG_COMPRESSION, &v) || v != compression)
    {
        fprintf(stderr, "Unexpected compression %u\n", (unsigned)v);
        ret = 1;
    }
    v = 0;
    if (predictor &&
        (!TIFFGetField(tif, TIFFTAG_PREDICTOR, &v) || v != predictor))
    {
        fprintf(stderr, "Unexpected predictor %u\n", (unsigned)v);
        ret = 1;
    }
    TIFFClose(tif);
    if (file_has_tag(stray_tag) != 0)
    {
        fprintf(stderr, "Unexpected tag %u in the file\n", (unsigned)stray_tag);
        ret = 1;
    }
    return ret;
}

int main(void)
{
    int ret;

    /* JPEGTables and Predictor share a codec field bit: the stale
     * JPEGTables tag looks set once a predictor is set. */
    ret = write_file(COMPRESSION_JPEG, 0, COMPRESSION_LZW, 1);
    if (ret == 0)
        ret = check_file(COMPRESSION_LZW, PREDICTOR_HORIZONTAL,
                         TIFFTAG_JPEGTABLES);
    /* The other way round: a stale Predictor tag with JPEG. */
    if (ret == 0)
        ret = write_file(COMPRESSION_LZW, 1, COMPRESSION_JPEG, 0);
    if (ret == 0)
        ret = check_file(COMPRESSION_JPEG, 0, TIFFTAG_PREDICTOR);
    unlink(filename);
    return ret;
}

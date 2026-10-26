/*
 * Copyright (c) 2026, Dshuishui <dyucong@outlook.com>
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
 * Test that TIFFWriteTile() and TIFFReadTile() fail, rather than use tile 0,
 * when the tile index computation overflows.
 */

#include "tif_config.h"

#include <stdio.h>
#include <string.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "tiffio.h"

#define TILE_SIZE 16

/*
 * With 17 tiles per row, an ImageLength of 0xFFFFFF00 gives 17 * 0x0FFFFFF0
 * tiles, which overflows uint32_t, while FAR_Y still passes TIFFCheckTile().
 */
#define TILES_PER_ROW 17
#define LARGE_LENGTH 0xFFFFFF00U
#define FAR_Y 0xFFFFFE00U

int main(void)
{
    const char *filename = "test_tile_index_overflow.tif";
    unsigned char tile0[TILE_SIZE * TILE_SIZE];
    unsigned char other[TILE_SIZE * TILE_SIZE];
    unsigned char check[TILE_SIZE * TILE_SIZE];
    tmsize_t ret;
    TIFF *tif;

    memset(tile0, 0xAA, sizeof(tile0));
    memset(other, 0xBB, sizeof(other));

    tif = TIFFOpen(filename, "w");
    if (!tif)
    {
        fprintf(stderr, "Cannot create %s\n", filename);
        return 1;
    }
    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, TILES_PER_ROW * TILE_SIZE);
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, TILE_SIZE);
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1);
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tif, TIFFTAG_TILEWIDTH, TILE_SIZE);
    TIFFSetField(tif, TIFFTAG_TILELENGTH, TILE_SIZE);
    if (TIFFWriteTile(tif, tile0, 0, 0, 0, 0) != (tmsize_t)sizeof(tile0))
    {
        fprintf(stderr, "Cannot write tile 0\n");
        goto failure;
    }

    /* ImageLength can still be changed while writing. */
    if (!TIFFSetField(tif, TIFFTAG_IMAGELENGTH, LARGE_LENGTH))
    {
        fprintf(stderr, "Cannot enlarge ImageLength while writing\n");
        goto failure;
    }
    ret = TIFFWriteTile(tif, other, 0, FAR_Y, 0, 0);
    if (!TIFFSetField(tif, TIFFTAG_IMAGELENGTH, TILE_SIZE))
    {
        fprintf(stderr, "Cannot restore ImageLength\n");
        goto failure;
    }
    if (ret != -1)
    {
        fprintf(stderr, "TIFFWriteTile() returned %ld instead of -1\n",
                (long)ret);
        goto failure;
    }
    TIFFClose(tif);

    tif = TIFFOpen(filename, "r");
    if (!tif)
    {
        fprintf(stderr, "Cannot reopen %s\n", filename);
        unlink(filename);
        return 1;
    }
    if (TIFFReadTile(tif, check, 0, 0, 0, 0) != (tmsize_t)sizeof(check) ||
        memcmp(check, tile0, sizeof(check)) != 0)
    {
        fprintf(stderr, "Tile 0 was overwritten\n");
        goto failure;
    }

    /* Same overflow on the reading side. */
    if (!TIFFSetField(tif, TIFFTAG_IMAGELENGTH, LARGE_LENGTH))
    {
        fprintf(stderr, "Cannot change ImageLength of a read handle\n");
        goto failure;
    }
    ret = TIFFReadTile(tif, check, 0, FAR_Y, 0, 0);
    if (ret != -1)
    {
        fprintf(stderr, "TIFFReadTile() returned %ld instead of -1\n",
                (long)ret);
        goto failure;
    }
    TIFFClose(tif);
    unlink(filename);
    return 0;

failure:
    TIFFClose(tif);
    unlink(filename);
    return 1;
}

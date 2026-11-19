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
 * Size arguments other than -1 must not be negative in the strip and tile
 * read and write APIs, and in TIFFReadFromUserBuffer() for its output size.
 * A negative size is refused, and the caller buffer is not touched.
 * The value -1 keeps its meaning (the whole strip or tile, or the default).
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiffio.h"

#define WIDTH 64
#define LENGTH 64
#define DATA_SIZE (WIDTH * LENGTH)
#define SMALL_SIZE 64
#define GUARD 64
#define GUARD_BYTE 0xA5

#define STRIP_FILE "test_negative_size_args_strip.tif"
#define TILE_FILE "test_negative_size_args_tile.tif"
#define BIG_FILE "test_negative_size_args_big.tif"
#define RAW_FILE "test_negative_size_args_raw.tif"

static unsigned char image[DATA_SIZE];

static void fill_image(void)
{
    uint32_t x = 12345u;
    size_t i;

    for (i = 0; i < sizeof(image); i++)
    {
        x = x * 1103515245u + 12345u;
        image[i] = (unsigned char)(x >> 16);
    }
}

static void set_common(TIFF *tif)
{
    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, (uint32_t)WIDTH);
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, (uint32_t)LENGTH);
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, (uint16_t)1);
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, (uint16_t)8);
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
}

/* Write the image as one strip or one tile. Returns 1 on success. */
static int make_file(const char *path, int tiled, const char *mode)
{
    TIFF *tif = TIFFOpen(path, mode);
    int ok;

    if (!tif)
        return 0;
    set_common(tif);
    if (tiled)
    {
        TIFFSetField(tif, TIFFTAG_TILEWIDTH, (uint32_t)WIDTH);
        TIFFSetField(tif, TIFFTAG_TILELENGTH, (uint32_t)LENGTH);
        ok = TIFFWriteEncodedTile(tif, 0, image, (tmsize_t)DATA_SIZE) ==
             (tmsize_t)DATA_SIZE;
    }
    else
    {
        TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, (uint32_t)LENGTH);
        ok = TIFFWriteEncodedStrip(tif, 0, image, (tmsize_t)DATA_SIZE) ==
             (tmsize_t)DATA_SIZE;
    }
    TIFFClose(tif);
    return ok;
}

/* Bytes after the first size bytes must still hold the guard fill value. */
static int guard_intact(const unsigned char *buf, size_t size)
{
    size_t i;

    for (i = 0; i < GUARD; i++)
    {
        if (buf[size + i] != GUARD_BYTE)
            return 0;
    }
    return 1;
}

static unsigned char *guarded_buffer(size_t size)
{
    unsigned char *buf = (unsigned char *)malloc(size + GUARD);

    if (buf)
        memset(buf + size, GUARD_BYTE, GUARD);
    return buf;
}

static int check_encoded(int tiled)
{
    const char *path = tiled ? TILE_FILE : STRIP_FILE;
    TIFF *tif = TIFFOpen(path, "r");
    unsigned char *small = guarded_buffer(SMALL_SIZE);
    unsigned char *full = guarded_buffer(DATA_SIZE);
    tmsize_t r;
    int fails = 0;

    if (!tif || !small || !full)
    {
        fprintf(stderr, "%s: setup failed\n", path);
        fails++;
    }
    else
    {
        r = tiled ? TIFFReadEncodedTile(tif, 0, small, (tmsize_t)-2)
                  : TIFFReadEncodedStrip(tif, 0, small, (tmsize_t)-2);
        if (r != (tmsize_t)-1 || !guard_intact(small, SMALL_SIZE))
        {
            fprintf(stderr, "%s encoded read: size -2 was accepted\n",
                    tiled ? "TIFFReadEncodedTile" : "TIFFReadEncodedStrip");
            fails++;
        }
        r = tiled ? TIFFReadEncodedTile(tif, 0, full, (tmsize_t)-1)
                  : TIFFReadEncodedStrip(tif, 0, full, (tmsize_t)-1);
        if (r != (tmsize_t)DATA_SIZE)
        {
            fprintf(stderr, "%s encoded read: size -1 control failed\n",
                    tiled ? "TIFFReadEncodedTile" : "TIFFReadEncodedStrip");
            fails++;
        }
    }
    if (tif)
        TIFFClose(tif);
    free(small);
    free(full);
    return fails;
}

static int check_raw(int tiled)
{
    const char *path = tiled ? TILE_FILE : STRIP_FILE;
    TIFF *tif = TIFFOpen(path, "r");
    unsigned char *small = guarded_buffer(SMALL_SIZE);
    unsigned char *full = guarded_buffer(DATA_SIZE);
    tmsize_t r;
    int fails = 0;

    if (!tif || !small || !full)
    {
        fprintf(stderr, "%s: setup failed\n", path);
        fails++;
    }
    else
    {
        r = tiled ? TIFFReadRawTile(tif, 0, small, (tmsize_t)-2)
                  : TIFFReadRawStrip(tif, 0, small, (tmsize_t)-2);
        if (r != (tmsize_t)-1 || !guard_intact(small, SMALL_SIZE))
        {
            fprintf(stderr, "%s: size -2 was accepted\n",
                    tiled ? "TIFFReadRawTile" : "TIFFReadRawStrip");
            fails++;
        }
        r = tiled ? TIFFReadRawTile(tif, 0, full, (tmsize_t)-1)
                  : TIFFReadRawStrip(tif, 0, full, (tmsize_t)-1);
        if (r != (tmsize_t)DATA_SIZE)
        {
            fprintf(stderr, "%s: size -1 control failed\n",
                    tiled ? "TIFFReadRawTile" : "TIFFReadRawStrip");
            fails++;
        }
    }
    if (tif)
        TIFFClose(tif);
    free(small);
    free(full);
    return fails;
}

static int check_user_buffer(void)
{
    TIFF *tif = TIFFOpen(STRIP_FILE, "r");
    unsigned char *in = (unsigned char *)malloc(DATA_SIZE);
    unsigned char *small = guarded_buffer(SMALL_SIZE);
    unsigned char *full = guarded_buffer(DATA_SIZE);
    int fails = 0;

    if (!tif || !in || !small || !full)
    {
        fprintf(stderr, "%s: setup failed\n", STRIP_FILE);
        fails++;
    }
    else if (TIFFReadRawStrip(tif, 0, in, (tmsize_t)DATA_SIZE) !=
             (tmsize_t)DATA_SIZE)
    {
        fprintf(stderr, "TIFFReadFromUserBuffer: raw input read failed\n");
        fails++;
    }
    else
    {
        if (TIFFReadFromUserBuffer(tif, 0, in, (tmsize_t)DATA_SIZE, small,
                                   (tmsize_t)-2) != 0 ||
            !guard_intact(small, SMALL_SIZE))
        {
            fprintf(stderr,
                    "TIFFReadFromUserBuffer: output size -2 was accepted\n");
            fails++;
        }
        if (TIFFReadFromUserBuffer(tif, 0, in, (tmsize_t)DATA_SIZE, full,
                                   (tmsize_t)DATA_SIZE) != 1)
        {
            fprintf(stderr,
                    "TIFFReadFromUserBuffer: positive control failed\n");
            fails++;
        }
    }
    if (tif)
        TIFFClose(tif);
    free(in);
    free(small);
    free(full);
    return fails;
}

static int check_write_big(void)
{
    TIFF *tif = TIFFOpen(BIG_FILE, "w8");
    tmsize_t r;
    int fails = 0;

    if (!tif)
    {
        fprintf(stderr, "%s: setup failed\n", BIG_FILE);
        return 1;
    }
    set_common(tif);
    TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, (uint32_t)LENGTH);
    r = TIFFWriteRawStrip(tif, 0, image, (tmsize_t)-100);
    if (r != (tmsize_t)-1)
    {
        fprintf(stderr,
                "TIFFWriteRawStrip (BigTIFF): negative count was accepted\n");
        fails++;
    }
    TIFFClose(tif);
    return fails;
}

static int check_write_control(void)
{
    TIFF *tif = TIFFOpen(RAW_FILE, "w");
    tmsize_t r;
    int fails = 0;

    if (!tif)
        return 1;
    set_common(tif);
    TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, (uint32_t)LENGTH);
    r = TIFFWriteRawStrip(tif, 0, image, (tmsize_t)DATA_SIZE);
    if (r != (tmsize_t)DATA_SIZE)
    {
        fprintf(stderr, "TIFFWriteRawStrip: positive control failed\n");
        fails++;
    }
    TIFFClose(tif);
    return fails;
}

int main(void)
{
    int fails = 0;

    fill_image();
    if (!make_file(STRIP_FILE, 0, "w") || !make_file(TILE_FILE, 1, "w"))
    {
        fprintf(stderr, "cannot create the test files\n");
        return 1;
    }
    fails += check_encoded(0);
    fails += check_encoded(1);
    fails += check_raw(0);
    fails += check_raw(1);
    fails += check_user_buffer();
    fails += check_write_big();
    fails += check_write_control();

    remove(STRIP_FILE);
    remove(TILE_FILE);
    remove(BIG_FILE);
    remove(RAW_FILE);
    return fails ? 1 : 0;
}

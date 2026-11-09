/*
 * Test col_offset handling for packed subsampled YCbCr images in
 * TIFFRGBAImageGet().  A column offset that does not start a YCbCr sampling
 * block cannot be represented, and passing it to the packed put routines used
 * to make them read past the end of the decoded strip or tile buffer.
 *
 * The test writes a small 2x2-subsampled YCbCr image in both tiled and
 * stripped form, then checks that an aligned column offset is accepted while
 * an offset inside a sampling block is rejected.
 */

#include "tif_config.h"

#include <stdio.h>
#include <stdlib.h>

#include "tiffio.h"

#define WIDTH 240
#define HEIGHT 224

static void quiet_error_handler(const char *module, const char *fmt, va_list ap)
{
    (void)module;
    (void)fmt;
    (void)ap;
}

static int write_image(const char *name, int tiled)
{
    TIFF *tif;
    unsigned char *buf;
    uint32_t blocks_x = (WIDTH + 1) / 2;
    uint32_t blocks_y = (HEIGHT + 1) / 2;
    size_t size = (size_t)blocks_x * blocks_y * 6;
    size_t i;
    int ok = 0;

    tif = TIFFOpen(name, "w");
    if (tif == NULL)
        return 0;

    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, WIDTH);
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, HEIGHT);
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 3);
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_YCBCR);
    TIFFSetField(tif, TIFFTAG_YCBCRSUBSAMPLING, 2, 2);
    TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
    if (tiled)
    {
        TIFFSetField(tif, TIFFTAG_TILEWIDTH, WIDTH);
        TIFFSetField(tif, TIFFTAG_TILELENGTH, HEIGHT);
    }
    else
    {
        TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, HEIGHT);
    }

    buf = (unsigned char *)malloc(size);
    if (buf == NULL)
    {
        TIFFClose(tif);
        return 0;
    }
    for (i = 0; i < size; i++)
        buf[i] = (unsigned char)(i * 7 + 3);

    if (tiled)
        ok = TIFFWriteEncodedTile(tif, 0, buf, (tmsize_t)size) ==
             (tmsize_t)size;
    else
        ok = TIFFWriteEncodedStrip(tif, 0, buf, (tmsize_t)size) ==
             (tmsize_t)size;

    free(buf);
    TIFFClose(tif);
    return ok;
}

static int check_image(const char *name)
{
    TIFF *tif;
    TIFFRGBAImage img;
    uint32_t *raster;
    char emsg[1024] = "";
    int ok = 1;

    tif = TIFFOpen(name, "r");
    if (tif == NULL)
        return 0;

    if (!TIFFRGBAImageBegin(&img, tif, 1, emsg))
    {
        fprintf(stderr, "%s: TIFFRGBAImageBegin failed: %s\n", name, emsg);
        TIFFClose(tif);
        return 0;
    }

    img.req_orientation = ORIENTATION_TOPLEFT;
    img.row_offset = 0;
    raster = (uint32_t *)_TIFFmalloc((tmsize_t)WIDTH * HEIGHT *
                                     (tmsize_t)sizeof(uint32_t));
    if (raster == NULL)
    {
        TIFFRGBAImageEnd(&img);
        TIFFClose(tif);
        return 0;
    }

    /* A column offset in the middle of a 2x2 sampling block is not
     * representable and must be rejected instead of overreading the buffer. */
    img.col_offset = 1;
    if (TIFFRGBAImageGet(&img, raster, WIDTH, HEIGHT) != 0)
    {
        fprintf(stderr,
                "%s: unaligned col_offset was not rejected\n", name);
        ok = 0;
        goto cleanup;
    }

    /* Starting at the next sampling block is fine and must still work. */
    img.col_offset = 2;
    if (TIFFRGBAImageGet(&img, raster, WIDTH, HEIGHT) != 1)
    {
        fprintf(stderr, "%s: aligned col_offset was rejected\n", name);
        ok = 0;
        goto cleanup;
    }

cleanup:
    _TIFFfree(raster);
    TIFFRGBAImageEnd(&img);
    TIFFClose(tif);
    return ok;
}

int main(void)
{
    const char *tile_name = "test_RGBAImageYCbCrOffsets_tile.tif";
    const char *strip_name = "test_RGBAImageYCbCrOffsets_strip.tif";
    int ok = 1;

    TIFFSetErrorHandler(quiet_error_handler);
    TIFFSetWarningHandler(quiet_error_handler);

    if (!write_image(tile_name, 1) || !check_image(tile_name))
        ok = 0;
    if (!write_image(strip_name, 0) || !check_image(strip_name))
        ok = 0;

    remove(tile_name);
    remove(strip_name);
    return ok ? 0 : 1;
}

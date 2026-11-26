/*
 * test_ycbcr_strip_crop.c -- regression test for heap OOB read in
 * gtStripContig for YCbCr subsampled strips.
 *
 * Creates uncompressed YCbCr stripped images for each supported subsampling
 * pair, writes them, then reads block-aligned crops via TIFFRGBAImage and
 * compares the result against a full-image reference.
 */

#include "tiffio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *fname = "test_ycbcr_strip_crop.tif";

typedef struct
{
    uint16_t h;
    uint16_t v;
} SubsampPair;

static const SubsampPair pairs[] = {
    {1, 1}, {1, 2}, {2, 1}, {2, 2}, {4, 1}, {4, 2},
};
static const int npairs = sizeof(pairs) / sizeof(pairs[0]);

static int create_ycbcr_stripped(uint32_t w, uint32_t h, uint16_t ss_h,
                                  uint16_t ss_v)
{
    TIFF *tif = TIFFOpen(fname, "w");
    if (!tif)
        return -1;

    uint32_t rps = (uint32_t)ss_v * 2;

    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, w);
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, h);
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 3);
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_YCBCR);
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
    TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, rps);
    TIFFSetField(tif, TIFFTAG_YCBCRSUBSAMPLING, ss_h, ss_v);

    tmsize_t ssz = TIFFStripSize(tif);
    if (ssz <= 0)
    {
        TIFFClose(tif);
        return -1;
    }
    unsigned char *buf = (unsigned char *)_TIFFmalloc(ssz);
    if (!buf)
    {
        TIFFClose(tif);
        return -1;
    }

    uint32_t nstrips = (h + rps - 1) / rps;
    uint32_t blocks_per_row = w / ss_h;
    uint32_t block_rows = rps / ss_v;
    uint32_t bsz = (uint32_t)ss_h * ss_v + 2;

    for (uint32_t s = 0; s < nstrips; s++)
    {
        memset(buf, 0, (size_t)ssz);
        unsigned char *p = buf;
        for (uint32_t by = 0; by < block_rows; by++)
        {
            for (uint32_t bx = 0; bx < blocks_per_row; bx++)
            {
                uint8_t y_val =
                    (uint8_t)((s * 37 + (by * blocks_per_row + bx) * 7) &
                              0xFF);
                for (uint32_t i = 0; i < (uint32_t)ss_h * ss_v; i++)
                    p[i] = y_val;
                p[ss_h * ss_v] = 128;
                p[ss_h * ss_v + 1] = 128;
                p += bsz;
            }
        }
        if (TIFFWriteEncodedStrip(tif, s, buf, ssz) < 0)
        {
            _TIFFfree(buf);
            TIFFClose(tif);
            return -1;
        }
    }
    _TIFFfree(buf);
    TIFFClose(tif);
    return 0;
}

static int read_rgba_crop(uint32_t rw, uint32_t rh, uint32_t x, uint32_t y,
                          uint32_t **out)
{
    TIFF *tif = TIFFOpen(fname, "r");
    if (!tif)
        return -1;

    char emsg[1024] = {0};
    TIFFRGBAImage img;
    if (!TIFFRGBAImageBegin(&img, tif, 0, emsg))
    {
        fprintf(stderr, "TIFFRGBAImageBegin failed: %s\n", emsg);
        TIFFClose(tif);
        return -1;
    }
    img.col_offset = (int)x;
    img.row_offset = (int)y;

    *out = (uint32_t *)_TIFFmalloc((tmsize_t)rw * (tmsize_t)rh *
                                   (tmsize_t)sizeof(uint32_t));
    if (!*out)
    {
        TIFFRGBAImageEnd(&img);
        TIFFClose(tif);
        return -1;
    }
    int ok = TIFFRGBAImageGet(&img, *out, rw, rh);
    TIFFRGBAImageEnd(&img);
    TIFFClose(tif);
    return ok ? 0 : -1;
}

static int test_subsamp(uint16_t ss_h, uint16_t ss_v)
{
    uint32_t w = 32, h = 32;
    int failures = 0;

    if (create_ycbcr_stripped(w, h, ss_h, ss_v) != 0)
    {
        fprintf(stderr, "  SKIP: could not create %ux%u stripped image\n",
                ss_h, ss_v);
        return 0;
    }

    uint32_t *full = NULL;
    if (read_rgba_crop(w, h, 0, 0, &full) != 0)
    {
        fprintf(stderr, "  FAIL: could not read full image\n");
        return 1;
    }

    /* Test block-aligned strip crops using multiples of subsampling factors */
    uint32_t offsets_x[] = {0, (uint32_t)ss_h * 2};
    uint32_t offsets_y[] = {0, (uint32_t)ss_v * 2};
    for (int iy = 0; iy < 2; iy++)
    {
        for (int ix = 0; ix < 2; ix++)
        {
            uint32_t ox = offsets_x[ix];
            uint32_t oy = offsets_y[iy];
            uint32_t cw = w - ox;
            uint32_t ch = h - oy;

            uint32_t *crop = NULL;
            if (read_rgba_crop(cw, ch, ox, oy, &crop) != 0)
            {
                fprintf(stderr, "  FAIL: crop (%u,%u) %ux%u read failed\n", ox,
                        oy, cw, ch);
                failures++;
                continue;
            }

            int mismatch = 0;
            for (uint32_t r = 0; r < ch && !mismatch; r++)
            {
                for (uint32_t c = 0; c < cw && !mismatch; c++)
                {
                    uint32_t crop_px = crop[r * cw + c];
                    uint32_t full_px = full[r * w + c + ox];
                    int dr = abs((int)(crop_px & 0xFF) -
                                 (int)(full_px & 0xFF));
                    int dg = abs((int)((crop_px >> 8) & 0xFF) -
                                 (int)((full_px >> 8) & 0xFF));
                    int db = abs((int)((crop_px >> 16) & 0xFF) -
                                 (int)((full_px >> 16) & 0xFF));
                    if (dr > 1 || dg > 1 || db > 1)
                        mismatch = 1;
                }
            }
            if (mismatch)
            {
                fprintf(stderr,
                        "  FAIL: crop (%u,%u) %ux%u pixel mismatch\n", ox, oy,
                        cw, ch);
                failures++;
            }
            _TIFFfree(crop);
        }
    }
    _TIFFfree(full);
    return failures;
}

int main(void)
{
    int failures = 0;
    for (int i = 0; i < npairs; i++)
    {
        fprintf(stderr, "Testing YCbCr subsampling %ux%u strips...\n",
                pairs[i].h, pairs[i].v);
        int f = test_subsamp(pairs[i].h, pairs[i].v);
        if (f > 0)
        {
            fprintf(stderr, "FAIL: %ux%u (%d failures)\n", pairs[i].h,
                    pairs[i].v, f);
            failures += f;
        }
        else
        {
            fprintf(stderr, "PASS: %ux%u\n", pairs[i].h, pairs[i].v);
        }
    }
    unlink(fname);
    return failures > 0 ? 1 : 0;
}

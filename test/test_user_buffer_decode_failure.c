/*
 * Copyright (c) 2026, Nexory
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
 * Test that TIFFReadFromUserBuffer() reports a failed decode in the same
 * cases as TIFFReadEncodedStrip().
 *
 * The compressed data of a CCITT fax strip or tile is cut at every length.
 * TIFFReadEncodedStrip() on a file that holds the cut data as its only strip
 * is the reference. The same bytes are then decoded from memory with
 * TIFFReadFromUserBuffer(), once through its strip branch and once through
 * its tile branch. Both functions run the same decoder on the same bytes, so
 * a cut that the fax decoder tolerates has to succeed in both, and a cut
 * that it rejects has to fail in both. The fax decoders report a hard
 * failure with -1 and not with 0.
 *
 * Every read uses a newly opened file: for cut data the result of the 2D
 * decoder can depend on an earlier decode with the same handle.
 */

#include "tif_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "tiffio.h"

#define FULL_NAME "test_user_buffer_decode_failure_full.tif"
#define TRUNC_NAME "test_user_buffer_decode_failure_trunc.tif"

#define WIDTH 64
#define HEIGHT 32
#define DECODED_SIZE ((WIDTH / 8) * HEIGHT)

typedef struct
{
    const char *name;
    uint16_t compression;
    uint32_t group3options;
} TestCase;

static const TestCase cases[] = {
    {"G4", COMPRESSION_CCITTFAX4, 0},
    {"G3 1D", COMPRESSION_CCITTFAX3, 0},
    {"G3 2D", COMPRESSION_CCITTFAX3, GROUP3OPT_2DENCODING},
    {"RLE", COMPRESSION_CCITTRLE, 0},
};

static void silent_handler(const char *module, const char *fmt, va_list ap)
{
    (void)module;
    (void)fmt;
    (void)ap;
}

/* Runs of black and white of varying length, and some mixed bytes. */
static void fill_image(unsigned char *data)
{
    uint32_t state = 12345;
    int i;
    for (i = 0; i < DECODED_SIZE; i++)
    {
        state = state * 1664525u + 1013904223u;
        if ((state & 0x300) != 0)
            data[i] = (state & 0x400) ? 0xFF : 0x00;
        else
            data[i] = (unsigned char)(state >> 24);
    }
}

static TIFF *create_file(const char *filename, const TestCase *tc, int tiled)
{
    TIFF *tif = TIFFOpen(filename, "w");
    if (!tif)
        return NULL;
    if (!TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, WIDTH) ||
        !TIFFSetField(tif, TIFFTAG_IMAGELENGTH, HEIGHT) ||
        !TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 1) ||
        !TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1) ||
        !TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG) ||
        !TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISWHITE) ||
        !TIFFSetField(tif, TIFFTAG_COMPRESSION, tc->compression))
        goto failure;
    if (tc->group3options &&
        !TIFFSetField(tif, TIFFTAG_GROUP3OPTIONS, tc->group3options))
        goto failure;
    if (tiled)
    {
        if (!TIFFSetField(tif, TIFFTAG_TILEWIDTH, WIDTH) ||
            !TIFFSetField(tif, TIFFTAG_TILELENGTH, HEIGHT))
            goto failure;
    }
    else if (!TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, HEIGHT))
        goto failure;
    return tif;

failure:
    TIFFClose(tif);
    return NULL;
}

/* Writes the first keep bytes of raw as the only strip of a file and reads
 * it back with TIFFReadEncodedStrip(). Returns 1 if the read succeeded, 0 if
 * it failed and -1 if the file could not be set up. */
static int read_cut_as_strip(const TestCase *tc, unsigned char *raw,
                             tmsize_t keep)
{
    unsigned char decoded[DECODED_SIZE];
    tmsize_t written;
    tmsize_t ret;
    TIFF *tif = create_file(TRUNC_NAME, tc, 0);
    if (!tif)
        return -1;
    written = TIFFWriteRawStrip(tif, 0, raw, keep);
    TIFFClose(tif);
    if (written != keep)
        return -1;
    tif = TIFFOpen(TRUNC_NAME, "r");
    if (!tif)
        return -1;
    ret = TIFFReadEncodedStrip(tif, 0, decoded, DECODED_SIZE);
    TIFFClose(tif);
    return ret < 0 ? 0 : 1;
}

/* Returns the number of cuts that the reference rejected, or -1 if the test
 * failed. */
static int test_layout(const TestCase *tc, int tiled)
{
    const char *layout = tiled ? "tile" : "strip";
    unsigned char image[DECODED_SIZE];
    unsigned char decoded[DECODED_SIZE];
    unsigned char *raw = NULL;
    tmsize_t rawsize;
    tmsize_t keep;
    TIFF *tif;
    int rejected = 0;
    int mismatches = 0;

    fill_image(image);

    tif = create_file(FULL_NAME, tc, tiled);
    if (!tif)
    {
        fprintf(stderr, "%s, %s: cannot create %s\n", tc->name, layout,
                FULL_NAME);
        return -1;
    }
    if ((tiled ? TIFFWriteEncodedTile(tif, 0, image, DECODED_SIZE)
               : TIFFWriteEncodedStrip(tif, 0, image, DECODED_SIZE)) !=
        DECODED_SIZE)
    {
        fprintf(stderr, "%s, %s: cannot write the image\n", tc->name, layout);
        TIFFClose(tif);
        return -1;
    }
    TIFFClose(tif);

    tif = TIFFOpen(FULL_NAME, "r");
    if (!tif)
    {
        fprintf(stderr, "%s, %s: cannot reopen %s\n", tc->name, layout,
                FULL_NAME);
        return -1;
    }
    rawsize = (tmsize_t)TIFFGetStrileByteCount(tif, 0);
    raw = (unsigned char *)_TIFFmalloc(rawsize);
    if (!raw || (tiled ? TIFFReadRawTile(tif, 0, raw, rawsize)
                       : TIFFReadRawStrip(tif, 0, raw, rawsize)) != rawsize)
    {
        fprintf(stderr, "%s, %s: cannot read the compressed data\n", tc->name,
                layout);
        goto failure;
    }

    /* The intact data has to decode, and to the image that was written. */
    memset(decoded, 0xAA, sizeof(decoded));
    if (TIFFReadFromUserBuffer(tif, 0, raw, rawsize, decoded, DECODED_SIZE) !=
            1 ||
        memcmp(decoded, image, DECODED_SIZE) != 0)
    {
        fprintf(stderr,
                "%s, %s: TIFFReadFromUserBuffer() does not decode the intact "
                "data\n",
                tc->name, layout);
        goto failure;
    }
    TIFFClose(tif);
    tif = NULL;

    for (keep = 1; keep < rawsize; keep++)
    {
        int reference = read_cut_as_strip(tc, raw, keep);
        int from_buffer;

        if (reference < 0)
        {
            fprintf(stderr, "%s, %s: cannot set up %s\n", tc->name, layout,
                    TRUNC_NAME);
            goto failure;
        }
        /* The intact file only supplies the tags here. */
        tif = TIFFOpen(FULL_NAME, "r");
        if (!tif)
        {
            fprintf(stderr, "%s, %s: cannot reopen %s\n", tc->name, layout,
                    FULL_NAME);
            goto failure;
        }
        from_buffer =
            TIFFReadFromUserBuffer(tif, 0, raw, keep, decoded, DECODED_SIZE);
        TIFFClose(tif);
        tif = NULL;

        if (!reference)
            rejected++;
        if ((from_buffer != 0) != (reference != 0))
        {
            if (mismatches < 3)
                fprintf(stderr,
                        "%s, %s data cut to %d of %d bytes: "
                        "TIFFReadEncodedStrip() %s, TIFFReadFromUserBuffer() "
                        "returned %d\n",
                        tc->name, layout, (int)keep, (int)rawsize,
                        reference ? "succeeded" : "failed", from_buffer);
            mismatches++;
        }
    }
    _TIFFfree(raw);
    if (mismatches)
    {
        fprintf(stderr,
                "%s, %s: %d results differ from TIFFReadEncodedStrip()\n",
                tc->name, layout, mismatches);
        return -1;
    }
    return rejected;

failure:
    if (tif)
        TIFFClose(tif);
    if (raw)
        _TIFFfree(raw);
    return -1;
}

int main(void)
{
    size_t i;
    int tiled;
    int total_rejected = 0;
    int tested = 0;
    int ret = 0;

    TIFFSetErrorHandler(silent_handler);
    TIFFSetWarningHandler(silent_handler);

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        if (!TIFFIsCODECConfigured(cases[i].compression))
            continue;
        tested = 1;
        for (tiled = 0; tiled <= 1; tiled++)
        {
            int rejected = test_layout(&cases[i], tiled);
            if (rejected < 0)
                ret = 1;
            else
                total_rejected += rejected;
        }
    }
    /* Without a failing decode the comparisons above say nothing. */
    if (ret == 0 && tested && total_rejected == 0)
    {
        fprintf(stderr, "No cut made a decoder fail\n");
        ret = 1;
    }

    unlink(FULL_NAME);
    unlink(TRUNC_NAME);
    return ret;
}

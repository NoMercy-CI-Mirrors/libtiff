/*
 * Copyright (c) 2026, Joe Grey
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
 * The objective of this test suite is to check that the ThunderScan decoder
 * never reports success over output bytes it did not write, and never leaves
 * the caller's buffer untouched when a decode fails.
 *
 * ThunderScan has no encoder, so the input files are assembled byte by byte.
 */

#include "tif_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "tiffio.h"

#define CANARY 0xA5

static void put16(unsigned char *p, uint16_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}

static void put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static void entry(unsigned char *p, uint16_t tag, uint16_t type, uint32_t count,
                  uint32_t value)
{
    put16(p, tag);
    put16(p + 2, type);
    put32(p + 4, count);
    put32(p + 8, value);
}

#define TIFF_SHORT 3
#define TIFF_LONG 4
#define ENTRY_COUNT 10
/* 8 byte file header + 2 byte entry count + 10 * 12 byte entries + the
 * 4 byte next-IFD offset is 134, so the strip cannot start before that. */
#define STRIP_ARRAY_OFFSET 136
#define STRIP_COUNT_ARRAY_OFFSET 148
#define STRIP_OFFSET 160

/*
 * Little-endian ThunderScan file, one strip, with caller-chosen geometry and
 * a caller-supplied raw strip payload.
 */
static int write_thunder_tiff(const char *path, uint32_t width, uint32_t length,
                              uint16_t samplesperpixel, uint32_t rowsperstrip,
                              const unsigned char *payload, uint32_t paylen,
                              uint16_t planarconfig, uint32_t nstrips)
{
    unsigned char header[STRIP_OFFSET];
    unsigned char *e = header + 10;
    FILE *fp;

    memset(header, 0, sizeof(header));
    header[0] = 'I';
    header[1] = 'I';
    put16(header + 2, 42);
    put32(header + 4, 8);
    put16(header + 8, ENTRY_COUNT);

    entry(e + 0 * 12, TIFFTAG_IMAGEWIDTH, TIFF_LONG, 1, width);
    entry(e + 1 * 12, TIFFTAG_IMAGELENGTH, TIFF_LONG, 1, length);
    entry(e + 2 * 12, TIFFTAG_BITSPERSAMPLE, TIFF_SHORT, 1, 4);
    entry(e + 3 * 12, TIFFTAG_COMPRESSION, TIFF_SHORT, 1,
          COMPRESSION_THUNDERSCAN);
    entry(e + 4 * 12, TIFFTAG_PHOTOMETRIC, TIFF_SHORT, 1,
          PHOTOMETRIC_MINISBLACK);
    if (nstrips == 1)
    {
        entry(e + 5 * 12, TIFFTAG_STRIPOFFSETS, TIFF_LONG, 1, STRIP_OFFSET);
    }
    else
    {
        /*
         * Two or more strips need an array, and a LONG array of more than one
         * element does not fit the 4 byte value field, so it is parked in the
         * unused tail of the header.
         */
        uint32_t i;
        for (i = 0; i < nstrips; i++)
            put32(header + STRIP_ARRAY_OFFSET + i * 4, STRIP_OFFSET);
        entry(e + 5 * 12, TIFFTAG_STRIPOFFSETS, TIFF_LONG, nstrips,
              STRIP_ARRAY_OFFSET);
    }
    entry(e + 6 * 12, TIFFTAG_SAMPLESPERPIXEL, TIFF_SHORT, 1, samplesperpixel);
    entry(e + 7 * 12, TIFFTAG_ROWSPERSTRIP, TIFF_LONG, 1, rowsperstrip);
    if (nstrips == 1)
    {
        entry(e + 8 * 12, TIFFTAG_STRIPBYTECOUNTS, TIFF_LONG, 1, paylen);
    }
    else
    {
        uint32_t i;
        for (i = 0; i < nstrips; i++)
            put32(header + STRIP_COUNT_ARRAY_OFFSET + i * 4, paylen);
        entry(e + 8 * 12, TIFFTAG_STRIPBYTECOUNTS, TIFF_LONG, nstrips,
              STRIP_COUNT_ARRAY_OFFSET);
    }
    entry(e + 9 * 12, TIFFTAG_PLANARCONFIG, TIFF_SHORT, 1, planarconfig);

    fp = fopen(path, "wb");
    if (fp == NULL)
    {
        fprintf(stderr, "Can't create %s\n", path);
        return 0;
    }
    if (fwrite(header, 1, sizeof(header), fp) != sizeof(header) ||
        fwrite(payload, 1, paylen, fp) != paylen)
    {
        fprintf(stderr, "Can't write %s\n", path);
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return 1;
}

/*
 * A contiguous multi-sample ThunderScan file is rejected.
 *
 * TIFFScanlineSize() multiplies by SamplesPerPixel while the decoder only ever
 * writes ImageWidth pixels, so accepting such a file would report a full
 * scanline decoded over bytes that were never written.
 */
static int test_contig_multisample_is_rejected(void)
{
    const char *path = "thunder_contig_multisample.tif";
    static const unsigned char payload[2] = {0xC1, 0xC2};
    TIFF *tif;
    tmsize_t scanline;
    tmsize_t result;
    unsigned char buf[2];
    int failures = 0;

    if (!write_thunder_tiff(path, 2, 1, 2, 1, payload, sizeof(payload),
                            PLANARCONFIG_CONTIG, 1))
        return 1;

    tif = TIFFOpen(path, "r");
    if (tif == NULL)
    {
        fprintf(stderr, "Can't open %s\n", path);
        unlink(path);
        return 1;
    }

    scanline = TIFFScanlineSize(tif);
    if (scanline != 2)
    {
        fprintf(stderr, "Expected a scanline size of 2, got %" PRId64 "\n",
                (int64_t)scanline);
        failures++;
    }

    memset(buf, CANARY, sizeof(buf));
    result = TIFFReadEncodedStrip(tif, 0, buf, scanline);
    if (result != -1)
    {
        fprintf(stderr,
                "TIFFReadEncodedStrip should have failed on a contiguous "
                "multi-sample ThunderScan file, returned %" PRId64 "\n",
                (int64_t)result);
        failures++;
    }

    TIFFClose(tif);
    unlink(path);
    return failures;
}

/*
 * A truncated strip zeroes every row, not just the one being decoded.
 */
static int test_truncated_strip_clears_whole_buffer(void)
{
    const char *path = "thunder_truncated_strip.tif";
    static const unsigned char payload[2] = {0xC1, 0xC2};
    TIFF *tif;
    tmsize_t stripsize;
    tmsize_t result;
    unsigned char *buf;
    tmsize_t i;
    int failures = 0;

    /* 4 rows of 4 pixels, but only enough payload for part of one row. */
    if (!write_thunder_tiff(path, 4, 4, 1, 4, payload, sizeof(payload),
                            PLANARCONFIG_CONTIG, 1))
        return 1;

    tif = TIFFOpen(path, "r");
    if (tif == NULL)
    {
        fprintf(stderr, "Can't open %s\n", path);
        unlink(path);
        return 1;
    }

    stripsize = TIFFStripSize(tif);
    if (stripsize != 8)
    {
        fprintf(stderr, "Expected a strip size of 8, got %" PRId64 "\n",
                (int64_t)stripsize);
        TIFFClose(tif);
        unlink(path);
        return 1;
    }

    buf = (unsigned char *)_TIFFmalloc(stripsize);
    if (buf == NULL)
    {
        fprintf(stderr, "Can't allocate %" PRId64 " bytes\n",
                (int64_t)stripsize);
        TIFFClose(tif);
        unlink(path);
        return 1;
    }
    memset(buf, CANARY, (size_t)stripsize);

    result = TIFFReadEncodedStrip(tif, 0, buf, stripsize);
    if (result != -1)
    {
        fprintf(stderr,
                "TIFFReadEncodedStrip should have failed on a truncated "
                "strip, returned %" PRId64 "\n",
                (int64_t)result);
        failures++;
    }

    for (i = 0; i < stripsize; i++)
    {
        if (buf[i] == CANARY)
        {
            fprintf(stderr,
                    "Byte %" PRId64 " of %" PRId64
                    " still holds the caller's data after a failed decode\n",
                    (int64_t)i, (int64_t)stripsize);
            failures++;
            break;
        }
    }

    _TIFFfree(buf);
    TIFFClose(tif);
    unlink(path);
    return failures;
}

/*
 * PLANARCONFIG_SEPARATE stays allowed, and this pins why.
 *
 * The separate branch of TIFFScanlineSize64() carries no SamplesPerPixel
 * factor, so the scanline is exactly the ceil(width/2) bytes the decoder
 * writes. If that ever changes, the guards above would be letting through
 * the same mismatch they exist to reject.
 */
static int test_separate_planarconfig_scanline_matches(void)
{
    const char *path = "thunder_separate.tif";
    static const unsigned char payload[2] = {0xC1, 0xC2};
    TIFF *tif;
    tmsize_t scanline;
    int failures = 0;

    if (!write_thunder_tiff(path, 4, 1, 2, 1, payload, sizeof(payload),
                            PLANARCONFIG_SEPARATE, 2))
        return 1;

    tif = TIFFOpen(path, "r");
    if (tif == NULL)
    {
        fprintf(stderr, "Can't open %s\n", path);
        unlink(path);
        return 1;
    }

    /* 4 pixels at 4 bits is 2 bytes, whatever SamplesPerPixel says. */
    scanline = TIFFScanlineSize(tif);
    if (scanline != 2)
    {
        fprintf(stderr,
                "Separate-plane scanline should be 2 bytes and ignore "
                "SamplesPerPixel, got %" PRId64 "\n",
                (int64_t)scanline);
        failures++;
    }

    TIFFClose(tif);
    unlink(path);
    return failures;
}

int main(void)
{
    int failures = 0;

    failures += test_contig_multisample_is_rejected();
    failures += test_truncated_strip_clears_whole_buffer();
    failures += test_separate_planarconfig_scanline_matches();

    return failures ? 1 : 0;
}

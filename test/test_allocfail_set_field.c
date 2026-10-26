/*
 * Copyright (c) 2026, 0b53rv3r
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
 * Read a directory, and set a tag, under every cumulated memory limit up to
 * the one at which it succeeds. When the value of a tag cannot be allocated,
 * the tag must not be reported as set with a NULL value.
 */

#include "tif_config.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "tiffio.h"

static const char filename[] = "test_allocfail_set_field.tif";

static char description[1000];
static FILE *sink;

static int write_file(void)
{
    uint8_t row[4] = {0, 1, 2, 3};
    TIFF *tif = TIFFOpen(filename, "w");
    int ok;

    if (!tif)
        return 0;
    memset(description, 'x', sizeof(description) - 1);
    ok = TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, 4) &&
         TIFFSetField(tif, TIFFTAG_IMAGELENGTH, 1) &&
         TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8) &&
         TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1) &&
         TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK) &&
         TIFFSetField(tif, TIFFTAG_IMAGEDESCRIPTION, description) &&
         TIFFSetField(tif, TIFFTAG_INKSET, INKSET_CMYK) &&
         TIFFWriteScanline(tif, row, 0, 0) == 1;
    TIFFClose(tif);
    return ok;
}

static int quiet_handler(TIFF *tif, void *user_data, const char *module,
                         const char *fmt, va_list ap)
{
    (void)tif;
    (void)user_data;
    (void)module;
    (void)fmt;
    (void)ap;
    return 1;
}

/* Returns -1 on error, 1 if both tags were loaded, 0 otherwise. */
static int check(tmsize_t limit)
{
    TIFFOpenOptions *opts = TIFFOpenOptionsAlloc();
    TIFF *tif;
    uint16_t inkset;
    const char *desc;
    int loaded = 0, ret = 0;

    if (!opts)
        return -1;
    TIFFOpenOptionsSetMaxCumulatedMemAlloc(opts, limit);
    TIFFOpenOptionsSetErrorHandlerExtR(opts, quiet_handler, NULL);
    TIFFOpenOptionsSetWarningHandlerExtR(opts, quiet_handler, NULL);
    tif = TIFFOpenExt(filename, "r", opts);
    TIFFOpenOptionsFree(opts);
    if (!tif)
        return 0;

    if (TIFFGetField(tif, TIFFTAG_IMAGEDESCRIPTION, &desc))
    {
        if (!desc)
        {
            fprintf(stderr, "limit %ld: ImageDescription set to NULL\n",
                    (long)limit);
            ret = -1;
        }
        loaded++;
    }
    if (TIFFGetField(tif, TIFFTAG_INKSET, &inkset))
        loaded++;
    TIFFClose(tif);
    return ret < 0 ? ret : loaded == 2;
}

/* Set a description of length len, then a scalar custom tag, with little
 * memory left. Returns 1 once the description no longer fits. */
static int set_inkset(size_t len)
{
    TIFFOpenOptions *opts = TIFFOpenOptionsAlloc();
    TIFF *tif;
    uint16_t inkset;
    int ret = 0;

    if (!opts)
        return -1;
    TIFFOpenOptionsSetMaxCumulatedMemAlloc(opts, 2 * sizeof(description));
    TIFFOpenOptionsSetErrorHandlerExtR(opts, quiet_handler, NULL);
    TIFFOpenOptionsSetWarningHandlerExtR(opts, quiet_handler, NULL);
    tif = TIFFOpenExt(filename, "w", opts);
    TIFFOpenOptionsFree(opts);
    if (!tif)
        return -1;
    description[len] = '\0';
    if (!TIFFSetField(tif, TIFFTAG_IMAGEDESCRIPTION, description))
        ret = 1;
    /* TIFFGetField() dereferences the stored value. */
    else if (!TIFFSetField(tif, TIFFTAG_INKSET, INKSET_CMYK) &&
             TIFFGetField(tif, TIFFTAG_INKSET, &inkset))
    {
        fprintf(stderr, "InkSet set after a failed TIFFSetField()\n");
        ret = -1;
    }
    else if (sink)
    {
        /* Needs memory for the InkSet value. */
        TIFFPrintDirectory(tif, sink, 0);
    }
    description[len] = 'x';
    TIFFClose(tif);
    return ret;
}

int main(void)
{
    tmsize_t limit;
    size_t len;

    if (!write_file())
    {
        fprintf(stderr, "Cannot write %s\n", filename);
        return 1;
    }
    for (limit = 1;; limit++)
    {
        int ret = check(limit);
        if (ret < 0)
            return 1;
        if (ret == 1)
            break;
    }
    sink = tmpfile();
    for (len = 0; len < sizeof(description) - 1; len++)
    {
        int ret = set_inkset(len);
        if (ret < 0)
            return 1;
        if (ret == 1)
            break;
    }
    if (sink)
        fclose(sink);
    remove(filename);
    return 0;
}

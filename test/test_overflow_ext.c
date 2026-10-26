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
 * Test the overflow flag of the *Ext() checked arithmetic functions and of
 * _TIFFComputeTileExt(). Uses private functions from the static library.
 */

#include "tif_config.h"

#include <stdio.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "tiffio.h"
#include "tiffiop.h"

static int nErrors = 0;

static void countingErrorHandler(const char *module, const char *fmt,
                                 va_list ap)
{
    (void)module;
    (void)fmt;
    (void)ap;
    nErrors++;
}

#define EXPECT(cond)                                                           \
    do                                                                         \
    {                                                                          \
        if (!(cond))                                                           \
        {                                                                      \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,   \
                    #cond);                                                    \
            return 1;                                                          \
        }                                                                      \
    } while (0)

/* An overflow returns 0, sets the flag and reports one error. */
#define EXPECT_OVERFLOW(expr, flag)                                            \
    do                                                                         \
    {                                                                          \
        int nErrorsBefore = nErrors;                                           \
        (flag) = false;                                                        \
        EXPECT((expr) == 0);                                                   \
        EXPECT((flag));                                                        \
        EXPECT(nErrors == nErrorsBefore + 1);                                  \
    } while (0)

/* Any other result is exact, leaves the flag alone and reports nothing. */
#define EXPECT_RESULT(expr, value, flag)                                       \
    do                                                                         \
    {                                                                          \
        int nErrorsBefore = nErrors;                                           \
        (flag) = false;                                                        \
        EXPECT((expr) == (value));                                             \
        EXPECT(!(flag));                                                       \
        EXPECT(nErrors == nErrorsBefore);                                      \
    } while (0)

static int test_arithmetic(TIFF *tif)
{
    bool ovf;

    EXPECT_RESULT(_TIFFMultiply32Ext(tif, 3, 5, "test", &ovf), 15, ovf);
    /* A zero product is not an overflow. */
    EXPECT_RESULT(_TIFFMultiply32Ext(tif, 0, 5, "test", &ovf), 0, ovf);
    EXPECT_RESULT(_TIFFMultiply32Ext(tif, 5, 0, "test", &ovf), 0, ovf);
    EXPECT_RESULT(_TIFFMultiply32Ext(tif, 65535, 65537, "test", &ovf),
                  UINT32_MAX, ovf);
    EXPECT_OVERFLOW(_TIFFMultiply32Ext(tif, 65536, 65536, "test", &ovf), ovf);

    EXPECT_RESULT(_TIFFAdd64Ext(tif, 0, 0, "test", &ovf), 0, ovf);
    EXPECT_RESULT(_TIFFAdd64Ext(tif, UINT64_MAX - 1, 1, "test", &ovf),
                  UINT64_MAX, ovf);
    EXPECT_OVERFLOW(_TIFFAdd64Ext(tif, UINT64_MAX, 1, "test", &ovf), ovf);

    EXPECT_RESULT(_TIFFCastUInt64ToUInt32Ext(tif, UINT32_MAX, "test", &ovf),
                  UINT32_MAX, ovf);
    EXPECT_OVERFLOW(
        _TIFFCastUInt64ToUInt32Ext(tif, (uint64_t)UINT32_MAX + 1, "test", &ovf),
        ovf);

    /* The flag is never reset, so a chain of operations can share it. */
    ovf = false;
    EXPECT(_TIFFMultiply32Ext(tif, 65536, 65536, "test", &ovf) == 0);
    EXPECT(_TIFFAdd64Ext(tif, 2, 3, "test", &ovf) == 5);
    EXPECT(ovf);

    /* A NULL flag is accepted, and the functions without it are unchanged. */
    EXPECT(_TIFFMultiply32Ext(tif, 65536, 65536, "test", NULL) == 0);
    EXPECT(_TIFFMultiply32(tif, 65536, 65536, "test") == 0);
    EXPECT(_TIFFAdd64(tif, UINT64_MAX, 1, "test") == 0);
    EXPECT(_TIFFCastUInt64ToUInt32(tif, (uint64_t)UINT32_MAX + 1, "test") == 0);
    return 0;
}

static int test_compute_tile(TIFF *tif)
{
    bool ovf;

    /* 17 tiles of 16x16 per row */
    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, 17 * 16);
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, 16);
    TIFFSetField(tif, TIFFTAG_TILEWIDTH, 16);
    TIFFSetField(tif, TIFFTAG_TILELENGTH, 16);

    /* Tile 0 is a valid result, not an overflow. */
    EXPECT_RESULT(_TIFFComputeTileExt(tif, 0, 0, 0, 0, &ovf), 0, ovf);
    EXPECT_RESULT(_TIFFComputeTileExt(tif, 16 * 16, 0, 0, 0, &ovf), 16, ovf);

    /* 17 * 0x0FFFFFF0 tiles do not fit in uint32_t. */
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, 0xFFFFFF00U);
    EXPECT_OVERFLOW(_TIFFComputeTileExt(tif, 0, 0xFFFFFE00U, 0, 0, &ovf), ovf);
    EXPECT(TIFFComputeTile(tif, 0, 0xFFFFFE00U, 0, 0) == 0);

    /* Leave the handle in a state that can be closed. */
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, 16);
    return 0;
}

int main(void)
{
    const char *filename = "test_overflow_ext.tif";
    TIFF *tif;
    int ret = 0;

    TIFFSetErrorHandler(countingErrorHandler);
    /* _TIFFAdd64Ext() and _TIFFCastUInt64ToUInt32Ext() only report errors
     * when given a TIFF handle. */
    tif = TIFFOpen(filename, "w");
    if (!tif)
    {
        fprintf(stderr, "Cannot create %s\n", filename);
        return 1;
    }
    ret |= test_arithmetic(tif);
    ret |= test_compute_tile(tif);
    TIFFClose(tif);
    unlink(filename);
    return ret;
}

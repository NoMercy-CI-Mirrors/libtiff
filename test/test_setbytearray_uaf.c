/*
 * test_setbytearray_uaf.c -- regression test for use-after-free in
 * setByteArray() and custom tag value replacement.
 *
 * The old code freed the existing allocation before copying the
 * replacement value, so a get-then-set cycle (where the source pointer
 * aliases the freed buffer) read freed memory.  The fix defers the free
 * until after the copy.
 *
 * Cases:
 *   1. ExtraSamples (setByteArray path)
 *   2. ImageDescription (ASCII / _TIFFsetNString path)
 *   3. Custom SHORT array (custom value replacement path)
 *   4. Slice of a custom array (overlapping source and destination)
 */

#include "tiffio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *fname = "test_setbytearray_uaf.tif";

static int test_extra_samples(void)
{
    TIFF *tif = TIFFOpen(fname, "w");
    if (!tif)
        return 1;
    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, 1);
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, 1);
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 2);
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
    uint16_t ea = EXTRASAMPLE_UNASSALPHA;
    if (!TIFFSetField(tif, TIFFTAG_EXTRASAMPLES, 1, &ea))
    {
        TIFFClose(tif);
        return 1;
    }
    for (int i = 0; i < 20; i++)
    {
        uint16_t count;
        const uint16_t *p;
        if (!TIFFGetField(tif, TIFFTAG_EXTRASAMPLES, &count, &p))
        {
            fprintf(stderr, "ExtraSamples: get failed at iteration %d\n", i);
            TIFFClose(tif);
            return 1;
        }
        if (count != 1 || p[0] != EXTRASAMPLE_UNASSALPHA)
        {
            fprintf(stderr,
                    "ExtraSamples: wrong value at iteration %d "
                    "(count=%u val=%u)\n",
                    i, count, p[0]);
            TIFFClose(tif);
            return 1;
        }
        if (!TIFFSetField(tif, TIFFTAG_EXTRASAMPLES, count, p))
        {
            fprintf(stderr, "ExtraSamples: set failed at iteration %d\n", i);
            TIFFClose(tif);
            return 1;
        }
    }
    TIFFClose(tif);
    return 0;
}

static int test_ascii_alias(void)
{
    TIFF *tif = TIFFOpen(fname, "w");
    if (!tif)
        return 1;
    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, 1);
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, 1);
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1);
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
    if (!TIFFSetField(tif, TIFFTAG_IMAGEDESCRIPTION, "alias-test-string"))
    {
        TIFFClose(tif);
        return 1;
    }
    for (int i = 0; i < 20; i++)
    {
        char *p;
        if (!TIFFGetField(tif, TIFFTAG_IMAGEDESCRIPTION, &p))
        {
            fprintf(stderr, "ASCII: get failed at iteration %d\n", i);
            TIFFClose(tif);
            return 1;
        }
        if (strcmp(p, "alias-test-string") != 0)
        {
            fprintf(stderr, "ASCII: wrong value at iteration %d: '%s'\n", i,
                    p);
            TIFFClose(tif);
            return 1;
        }
        if (!TIFFSetField(tif, TIFFTAG_IMAGEDESCRIPTION, p))
        {
            fprintf(stderr, "ASCII: set failed at iteration %d\n", i);
            TIFFClose(tif);
            return 1;
        }
    }
    TIFFClose(tif);
    return 0;
}

static const TIFFFieldInfo custom_fields[] = {
    {65000, TIFF_VARIABLE2, TIFF_VARIABLE2, TIFF_SHORT, FIELD_CUSTOM, 1, 1,
     (char *)"TestShortArray"},
};

static int test_custom_array(void)
{
    TIFF *tif = TIFFOpen(fname, "w");
    if (!tif)
        return 1;
    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, 1);
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, 1);
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1);
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
    if (TIFFMergeFieldInfo(tif, custom_fields, 1) != 0)
    {
        TIFFClose(tif);
        return 1;
    }
    uint16_t src[] = {7, 11, 13, 17};
    if (!TIFFSetField(tif, 65000, (uint32_t)4, src))
    {
        TIFFClose(tif);
        return 1;
    }
    for (int i = 0; i < 20; i++)
    {
        uint32_t n;
        const uint16_t *p;
        if (!TIFFGetField(tif, 65000, &n, &p))
        {
            fprintf(stderr, "Custom array: get failed at iteration %d\n", i);
            TIFFClose(tif);
            return 1;
        }
        if (n != 4 || memcmp(p, src, sizeof(src)) != 0)
        {
            fprintf(stderr, "Custom array: wrong value at iteration %d\n", i);
            TIFFClose(tif);
            return 1;
        }
        if (!TIFFSetField(tif, 65000, n, p))
        {
            fprintf(stderr, "Custom array: set failed at iteration %d\n", i);
            TIFFClose(tif);
            return 1;
        }
    }
    TIFFClose(tif);
    return 0;
}

static int test_custom_array_slice(void)
{
    TIFF *tif = TIFFOpen(fname, "w");
    if (!tif)
        return 1;
    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, 1);
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, 1);
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1);
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
    if (TIFFMergeFieldInfo(tif, custom_fields, 1) != 0)
    {
        TIFFClose(tif);
        return 1;
    }
    uint16_t src[] = {7, 11, 13, 17};
    if (!TIFFSetField(tif, 65000, (uint32_t)4, src))
    {
        TIFFClose(tif);
        return 1;
    }
    uint32_t n;
    const uint16_t *p;
    if (!TIFFGetField(tif, 65000, &n, &p))
    {
        TIFFClose(tif);
        return 1;
    }
    /* Set from a slice of the existing buffer (p+1, count 2) */
    if (!TIFFSetField(tif, 65000, (uint32_t)2, p + 1))
    {
        fprintf(stderr, "Custom slice: set from slice failed\n");
        TIFFClose(tif);
        return 1;
    }
    if (!TIFFGetField(tif, 65000, &n, &p))
    {
        fprintf(stderr, "Custom slice: get after slice failed\n");
        TIFFClose(tif);
        return 1;
    }
    if (n != 2 || p[0] != 11 || p[1] != 13)
    {
        fprintf(stderr,
                "Custom slice: wrong value (n=%u, p[0]=%u, p[1]=%u)\n", n,
                p[0], p[1]);
        TIFFClose(tif);
        return 1;
    }
    TIFFClose(tif);
    return 0;
}

int main(void)
{
    int failures = 0;

    if (test_extra_samples() != 0)
    {
        fprintf(stderr, "FAIL: test_extra_samples\n");
        failures++;
    }
    else
    {
        fprintf(stderr, "PASS: test_extra_samples\n");
    }

    if (test_ascii_alias() != 0)
    {
        fprintf(stderr, "FAIL: test_ascii_alias\n");
        failures++;
    }
    else
    {
        fprintf(stderr, "PASS: test_ascii_alias\n");
    }

    if (test_custom_array() != 0)
    {
        fprintf(stderr, "FAIL: test_custom_array\n");
        failures++;
    }
    else
    {
        fprintf(stderr, "PASS: test_custom_array\n");
    }

    if (test_custom_array_slice() != 0)
    {
        fprintf(stderr, "FAIL: test_custom_array_slice\n");
        failures++;
    }
    else
    {
        fprintf(stderr, "PASS: test_custom_array_slice\n");
    }

    unlink(fname);
    return failures > 0 ? 1 : 0;
}

/*
 * Copyright (c) 1988-1997 Sam Leffler
 * Copyright (c) 1991-1997 Silicon Graphics, Inc.
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

/**
 * @file tif_thunder.c
 * @brief Implement the ThunderScan 4-bit run/delta decoder.
 */

#include "tiffiop.h"
#include <assert.h>
#ifdef THUNDER_SUPPORT
/*
 * TIFF Library.
 *
 * ThunderScan 4-bit Compression Algorithm Support
 */

/*
 * ThunderScan uses an encoding scheme designed for
 * 4-bit pixel values.  Data is encoded in bytes, with
 * each byte split into a 2-bit code word and a 6-bit
 * data value.  The encoding gives raw data, runs of
 * pixels, or pixel values encoded as a delta from the
 * previous pixel value.  For the latter, either 2-bit
 * or 3-bit delta values are used, with the deltas packed
 * into a single byte.
 */
// #define THUNDER_DATA 0x3f /* mask for 6-bit data */
/**
 * @brief Mask selecting the 2-bit code word in the top bits of a ThunderScan
 * byte.
 * @ingroup libtiff_private_basic_codecs
 *
 * The remaining low 6 bits carry the run count, packed deltas or raw pixel.
 */
#define THUNDER_CODE 0xc0 /* mask for 2-bit code word */
/* code values */
/**
 * @brief ThunderScan code word for a run of the previous pixel.
 * @ingroup libtiff_private_basic_codecs
 *
 * The low 6 bits give the repeat count; a count of zero emits nothing.
 */
#define THUNDER_RUN 0x00 /* run of pixels w/ encoded count */
/**
 * @brief ThunderScan code word for three packed 2-bit pixel deltas.
 * @ingroup libtiff_private_basic_codecs
 *
 * Deltas sit in bits 5-4, 3-2 and 1-0 and index twobitdeltas.
 */
#define THUNDER_2BITDELTAS 0x40 /* 3 pixels w/ encoded 2-bit deltas */
/**
 * @brief 2-bit delta value meaning "no pixel" in a THUNDER_2BITDELTAS byte.
 * @ingroup libtiff_private_basic_codecs
 */
#define DELTA2_SKIP 2 /* skip code for 2-bit deltas */
/**
 * @brief ThunderScan code word for two packed 3-bit pixel deltas.
 * @ingroup libtiff_private_basic_codecs
 *
 * Deltas sit in bits 5-3 and 2-0 and index threebitdeltas.
 */
#define THUNDER_3BITDELTAS 0x80 /* 2 pixels w/ encoded 3-bit deltas */
/**
 * @brief 3-bit delta value meaning "no pixel" in a THUNDER_3BITDELTAS byte.
 * @ingroup libtiff_private_basic_codecs
 */
#define DELTA3_SKIP 4 /* skip code for 3-bit deltas */
/**
 * @brief ThunderScan code word for a literal 4-bit pixel.
 * @ingroup libtiff_private_basic_codecs
 *
 * Same value as THUNDER_CODE; the pixel is in the low 4 bits.
 */
#define THUNDER_RAW 0xc0 /* raw data encoded */

/**
 * @brief Map two-bit delta codes to signed pixel increments.
 * @ingroup libtiff_private_basic_codecs
 *
 * Indexed by a 2-bit delta field; index DELTA2_SKIP is never looked up.
 */
static const int twobitdeltas[4] = {0, 1, 0, -1};
/**
 * @brief Map three-bit delta codes to signed pixel increments.
 * @ingroup libtiff_private_basic_codecs
 *
 * Indexed by a 3-bit delta field; index DELTA3_SKIP is never looked up.
 */
static const int threebitdeltas[8] = {0, 1, 2, 3, 0, -3, -2, -1};

/**
 * @brief Store a 4-bit pixel into the packed output if maxpixels has not
 * been reached.
 * @ingroup libtiff_private_basic_codecs
 *
 * Statement macro used by ThunderDecode(). Always sets the caller local
 * lastpixel to (v) & 0xf, so later deltas are relative to it. If
 * npixels < maxpixels it increments npixels and writes the nibble: an even
 * pixel index stores it in the high nibble of op[0] (clearing the low
 * nibble), an odd index ORs it into the low nibble and advances op. Pixels
 * beyond maxpixels are dropped silently; ThunderDecode() reports the count
 * mismatch afterwards. v is evaluated once; op is read and may be
 * incremented once. No hidden control flow.
 *
 * @param[in,out] op Output byte-pointer lvalue, advanced after each odd
 * pixel.
 * @param[in] v Pixel value; only the low 4 bits are used.
 */
#define SETPIXEL(op, v)                                                        \
    {                                                                          \
        lastpixel = (v) & 0xf;                                                 \
        if (npixels < maxpixels)                                               \
        {                                                                      \
            if (npixels++ & 1)                                                 \
                *op++ |= (uint8_t)lastpixel;                                   \
            else                                                               \
                op[0] = (uint8_t)(lastpixel << 4);                             \
        }                                                                      \
    }

/**
 * @brief Validate four-bit single-plane ThunderScan decoding.
 * @ingroup libtiff_private_basic_codecs
 *
 * Requires BitsPerSample four and either one sample or separate planes.
 * Diagnoses unsupported settings; no allocation, I/O, buffer changes or
 * ownership transfer.
 *
 * @param[in] tif Non-NULL live handle whose BitsPerSample, SamplesPerPixel
 * and PlanarConfiguration are checked; serialise access.
 * @return One for accepted layout, zero otherwise.
 */
static int ThunderSetupDecode(TIFF *tif)
{
    static const char module[] = "ThunderSetupDecode";

    if (tif->tif_dir.td_bitspersample != 4)
    {
        TIFFErrorExtR(tif, module,
                      "Wrong bitspersample value (%d), Thunder decoder only "
                      "supports 4bits per sample.",
                      (int)tif->tif_dir.td_bitspersample);
        return 0;
    }
    if (tif->tif_dir.td_samplesperpixel != 1 &&
        tif->tif_dir.td_planarconfig != PLANARCONFIG_SEPARATE)
    {
        TIFFErrorExtR(tif, module,
                      "Samples/pixel shall be 1 for ThunderScan decoding, "
                      "or PlanarConfiguration must be set to Separate.");
        return 0;
    }

    return (1);
}

/**
 * @brief Expand one ThunderScan row into packed four-bit samples.
 * @ingroup libtiff_private_basic_codecs
 *
 * Consumes raw/delta/run codes, writes high nibble first, and bounds
 * emitted pixels by maxpixels. Updates raw cursor/counts; diagnoses too
 * few/many pixels and clears the remaining packed tail on rejection.
 * Requires nonnegative representable pixel count and ceil(maxpixels/2)
 * destination bytes. No allocation/client I/O or ownership transfer.
 *
 * @param[in,out] tif Non-NULL live handle whose raw cursor and count are
 * consumed and updated; serialise access.
 * @param[out] op0 Borrowed writable packed four-bit output for maxpixels
 * pixels.
 * @param[in] maxpixels Nonnegative target pixel count with representable
 * rounded packed size.
 * @return One for exact pixel count, zero on mismatch; decoded pixels
 * before the mismatch remain and the rest of the row is zeroed.
 */
static int ThunderDecode(TIFF *tif, uint8_t *op0, tmsize_t maxpixels)
{
    static const char module[] = "ThunderDecode";
    unsigned char *bp;
    tmsize_t cc;
    unsigned int lastpixel;
    tmsize_t npixels;
    uint8_t *op = op0;

    bp = (unsigned char *)tif->tif_rawcp;
    cc = tif->tif_rawcc;
    lastpixel = 0;
    npixels = 0;
    while (cc > 0 && npixels < maxpixels)
    {
        int n, delta;

        n = *bp++;
        cc--;
        switch (n & THUNDER_CODE)
        {
            case THUNDER_RUN: /* pixel run */
                /*
                 * Replicate the last pixel n times,
                 * where n is the lower-order 6 bits.
                 */
                if (n == 0)
                    break;
                if (npixels & 1)
                {
                    op[0] |= (uint8_t)lastpixel;
                    lastpixel = *op++;
                    npixels++;
                    n--;
                }
                else
                    lastpixel |= lastpixel << 4;
                npixels += n;
                if (npixels > maxpixels)
                    break;
                for (; n > 0; n -= 2)
                    *op++ = (uint8_t)lastpixel;
                if (n == -1)
                    *--op &= 0xf0;
                lastpixel &= 0xf;
                break;
            case THUNDER_2BITDELTAS: /* 2-bit deltas */
                if ((delta = ((n >> 4) & 3)) != DELTA2_SKIP)
                    SETPIXEL(op,
                             (unsigned)((int)lastpixel + twobitdeltas[delta]));
                if ((delta = ((n >> 2) & 3)) != DELTA2_SKIP)
                    SETPIXEL(op,
                             (unsigned)((int)lastpixel + twobitdeltas[delta]));
                if ((delta = (n & 3)) != DELTA2_SKIP)
                    SETPIXEL(op,
                             (unsigned)((int)lastpixel + twobitdeltas[delta]));
                break;
            case THUNDER_3BITDELTAS: /* 3-bit deltas */
                if ((delta = ((n >> 3) & 7)) != DELTA3_SKIP)
                    SETPIXEL(
                        op, (unsigned)((int)lastpixel + threebitdeltas[delta]));
                if ((delta = (n & 7)) != DELTA3_SKIP)
                    SETPIXEL(
                        op, (unsigned)((int)lastpixel + threebitdeltas[delta]));
                break;
            case THUNDER_RAW: /* raw data */
                SETPIXEL(op, n);
                break;
            default:
                break;
        }
    }
    tif->tif_rawcp = (uint8_t *)bp;
    tif->tif_rawcc = cc;
    if (npixels != maxpixels)
    {
        uint8_t *op_end = op0 + (maxpixels + 1) / 2;
        memset(op, 0, (size_t)(op_end - op));
        TIFFErrorExtR(tif, module,
                      "%s data at scanline %lu (%" PRIu64 " != %" PRIu64 ")",
                      npixels < maxpixels ? "Not enough" : "Too much",
                      (unsigned long)tif->tif_dir.td_row, (uint64_t)npixels,
                      (uint64_t)maxpixels);
        return (0);
    }

    return (1);
}

/**
 * @brief Decode complete ThunderScan scanlines into caller storage.
 * @ingroup libtiff_private_basic_codecs
 *
 * Requires positive cached scanline size, nonnegative extent divisible by
 * it, and validated four-bit geometry. Calls ThunderDecode per row. On a
 * failed row clears that entire row and the remaining requested output,
 * leaving earlier rows and raw progress. No allocation/client I/O or
 * ownership transfer.
 *
 * @param[in,out] tif Non-NULL live handle with validated four-bit geometry
 * whose raw cursor and count are consumed; serialise access.
 * @param[out] buf Borrowed writable decoded output covering the requested
 * byte extent.
 * @param[in] occ Nonnegative decoded output byte extent.
 * @param[in] s Sample plane (ignored).
 * @return One after complete rows, zero on fractional extent or failed row.
 */
static int ThunderDecodeRow(TIFF *tif, uint8_t *buf, tmsize_t occ, uint16_t s)
{
    static const char module[] = "ThunderDecodeRow";
    uint8_t *row = buf;

    (void)s;
    if (occ % tif->tif_dir.td_scanlinesize)
    {
        TIFFErrorExtR(tif, module, "Fractional scanlines cannot be read");
        return (0);
    }
    while (occ > 0)
    {
        if (!ThunderDecode(tif, row, tif->tif_dir.td_imagewidth))
        {
            /*
             * ThunderDecode only clears the tail of the scanline it was
             * working on. Clear this scanline and every row after it, so a
             * failed decode cannot hand the caller whatever its buffer
             * happened to contain.
             */
            memset(row, 0, (size_t)occ);
            return (0);
        }
        occ -= tif->tif_dir.td_scanlinesize;
        row += tif->tif_dir.td_scanlinesize;
    }
    return (1);
}

int TIFFInitThunderScan(TIFF *tif, int scheme)
{
    (void)scheme;

    tif->tif_setupdecode = ThunderSetupDecode;
    tif->tif_decoderow = ThunderDecodeRow;
    tif->tif_decodestrip = ThunderDecodeRow;
    return (1);
}
#endif /* THUNDER_SUPPORT */

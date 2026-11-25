/*
 * Copyright (c) 1996-1997 Sam Leffler
 * Copyright (c) 1996 Pixar
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that (i) the above copyright notices and this permission notice appear in
 * all copies of the software and related documentation, and (ii) the names of
 * Pixar, Sam Leffler and Silicon Graphics may not be used in any advertising or
 * publicity relating to the software without the specific, prior written
 * permission of Pixar, Sam Leffler and Silicon Graphics.
 *
 * THE SOFTWARE IS PROVIDED "AS-IS" AND WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS, IMPLIED OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * IN NO EVENT SHALL PIXAR, SAM LEFFLER OR SILICON GRAPHICS BE LIABLE FOR
 * ANY SPECIAL, INCIDENTAL, INDIRECT OR CONSEQUENTIAL DAMAGES OF ANY KIND,
 * OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER OR NOT ADVISED OF THE POSSIBILITY OF DAMAGE, AND ON ANY THEORY OF
 * LIABILITY, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

/**
 * @file tif_pixarlog.c
 * @brief Compand PixarLog samples and transfer zlib-compressed code streams.
 */

#include "tiffiop.h"
#ifdef PIXARLOG_SUPPORT

/*
 * TIFF Library.
 * PixarLog Compression Support
 *
 * Contributed by Dan McCoy.
 *
 * PixarLog film support uses the TIFF library to store companded
 * 11 bit values into a tiff file, which are compressed using the
 * zip compressor.
 *
 * The codec can take as input and produce as output 32-bit IEEE float values
 * as well as 16-bit or 8-bit unsigned integer values.
 *
 * On writing any of the above are converted into the internal
 * 11-bit log format.   In the case of  8 and 16 bit values, the
 * input is assumed to be unsigned linear color values that represent
 * the range 0-1.  In the case of IEEE values, the 0-1 range is assumed to
 * be the normal linear color range, in addition over 1 values are
 * accepted up to a value of about 25.0 to encode "hot" highlights and such.
 * The encoding is lossless for 8-bit values, slightly lossy for the
 * other bit depths.  The actual color precision should be better
 * than the human eye can perceive with extra room to allow for
 * error introduced by further image computation.  As with any quantized
 * color format, it is possible to perform image calculations which
 * expose the quantization error. This format should certainly be less
 * susceptible to such errors than standard 8-bit encodings, but more
 * susceptible than straight 16-bit or 32-bit encodings.
 *
 * On reading the internal format is converted to the desired output format.
 * The program can request which format it desires by setting the internal
 * pseudo tag TIFFTAG_PIXARLOGDATAFMT to one of these possible values:
 *  PIXARLOGDATAFMT_FLOAT     = provide IEEE float values.
 *  PIXARLOGDATAFMT_16BIT     = provide unsigned 16-bit integer values
 *  PIXARLOGDATAFMT_8BIT      = provide unsigned 8-bit integer values
 *
 * alternately PIXARLOGDATAFMT_8BITABGR provides unsigned 8-bit integer
 * values with the difference that if there are exactly three or four channels
 * (rgb or rgba) it swaps the channel order (bgr or abgr).
 *
 * PIXARLOGDATAFMT_11BITLOG provides the internal encoding directly
 * packed in 16-bit values.   However no tools are supplied for interpreting
 * these values.
 *
 * "hot" (over 1.0) areas written in floating point get clamped to
 * 1.0 in the integer data types.
 *
 * When the file is closed after writing, the bit depth and sample format
 * are set always to appear as if 8-bit data has been written into it.
 * That way a naive program unaware of the particulars of the encoding
 * gets the format it is most likely able to handle.
 *
 * The codec does it's own horizontal differencing step on the coded
 * values so the libraries predictor stuff should be turned off.
 * The codec also handle byte swapping the encoded values as necessary
 * since the library does not have the information necessary
 * to know the bit depth of the raw unencoded buffer.
 *
 * NOTE: This decoder does not appear to update tif_rawcp, and tif_rawcc.
 * This can cause problems with the implementation of CHUNKY_STRIP_READ_SUPPORT
 * as noted in http://trac.osgeo.org/gdal/ticket/3894.   FrankW - Jan'11
 */

#include "tif_predict.h"
#include "zlib.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* Tables for converting to/from 11 bit coded values */

#define TSIZE 2048   /* decode table size (11-bit tokens) */
#define TSIZEP1 2049 /* Plus one for slop */
#define ONE 1250     /* token value of 1.0 exactly */
#define RATIO 1.004  /* nominal ratio for log part */

#define CODE_MASK 0x7ff /* 11 bits. */

/**
 * @brief Scale low-range float lookup indices.
 * @ingroup libtiff_private_pixarlog
 *
 * Process-global coefficient written by every PixarLogMakeTables call
 * (always with the same value) and read by horizontalDifferenceF. The writes
 * are unsynchronised, so serialise table construction against float encoding
 * on other handles.
 */
static float Fltsize;
/**
 * @brief Scale logarithmic float companding.
 * @ingroup libtiff_private_pixarlog
 *
 * Process-global coefficient written by every PixarLogMakeTables call
 * (always with the same value) and read by horizontalDifferenceF. The writes
 * are unsynchronised, so serialise table construction against float encoding
 * on other handles.
 */
static float LogK1;
/**
 * @brief Normalise logarithmic float companding.
 * @ingroup libtiff_private_pixarlog
 *
 * Process-global coefficient with the same lifetime and synchronisation
 * requirements as LogK1.
 */
static float LogK2;

#define REPEAT(n, op)                                                          \
    {                                                                          \
        int i;                                                                 \
        i = n;                                                                 \
        do                                                                     \
        {                                                                      \
            i--;                                                               \
            op;                                                                \
        } while (i > 0);                                                       \
    }

/*
 * PIXARLOGDATAFMT_* buffers are application-facing user data buffers in
 * native byte order.  Use fixed-size memcpy() calls directly so optimizing
 * compilers can expand them in these per-sample paths while callers remain
 * free to provide unaligned public buffers.  _TIFFmemcpy() is an out-of-line
 * wrapper in non-LTO builds.
 */
/**
 * @brief Read one unaligned native-order float.
 * @ingroup libtiff_private_pixarlog
 *
 * Uses fixed-size memcpy and requires enough live byte storage. Does not
 * convert byte order, allocate, do I/O, diagnose, retain pointers or
 * transfer ownership.
 *
 * @param[in] cp Readable bytes for one native value; unaligned allowed.
 * @return Loaded value.
 */
static float PixarLogLoadFloatNativeUnaligned(const uint8_t *cp)
{
    float v;
    memcpy(&v, cp, sizeof(v));
    return v;
}

/**
 * @brief Write one unaligned native-order float.
 * @ingroup libtiff_private_pixarlog
 *
 * Uses fixed-size memcpy and requires enough live byte storage. Does not
 * convert byte order, allocate, do I/O, diagnose, retain pointers or
 * transfer ownership.
 *
 * @param[out] cp Writable bytes for one native value; unaligned allowed.
 * @param[in] v Native value to store.
 */
static void PixarLogStoreFloatNativeUnaligned(uint8_t *cp, float v)
{
    memcpy(cp, &v, sizeof(v));
}

/**
 * @brief Read one unaligned native-order sixteen-bit sample.
 * @ingroup libtiff_private_pixarlog
 *
 * Uses fixed-size memcpy and requires enough live byte storage. Does not
 * convert byte order, allocate, do I/O, diagnose, retain pointers or
 * transfer ownership.
 *
 * @param[in] cp Readable bytes for one native value; unaligned allowed.
 * @return Loaded value.
 */
static uint16_t PixarLogLoad16NativeUnaligned(const uint8_t *cp)
{
    uint16_t v;
    memcpy(&v, cp, sizeof(v));
    return v;
}

/**
 * @brief Write one unaligned native-order sixteen-bit sample.
 * @ingroup libtiff_private_pixarlog
 *
 * Uses fixed-size memcpy and requires enough live byte storage. Does not
 * convert byte order, allocate, do I/O, diagnose, retain pointers or
 * transfer ownership.
 *
 * @param[out] cp Writable bytes for one native value; unaligned allowed.
 * @param[in] v Native value to store.
 */
static void PixarLogStore16NativeUnaligned(uint8_t *cp, uint16_t v)
{
    memcpy(cp, &v, sizeof(v));
}

/**
 * @brief Accumulate PixarLog prediction into float32 linear samples.
 * @ingroup libtiff_private_pixarlog
 *
 * Requires positive stride and n complete interleaved samples, with disjoint
 * code/output storage. Recovers horizontal prediction modulo 2048; generic
 * stride paths update the input code array in place. Does nothing when n is
 * less than stride. Maps codes through ToLinearF and stores native floats
 * through the unaligned store helper. No allocation, I/O, diagnostics,
 * retained pointers, ownership transfer or independent extent validation;
 * void result.
 *
 * @param[in,out] wp Aligned n-entry uint16_t code input; generic-stride
 * paths update it in place.
 * @param[in] n Nonnegative sample count divisible by positive stride;
 * buffers must cover complete rows.
 * @param[in] stride Positive samples per interleaved pixel, or one for
 * separate data.
 * @param[out] op Writable format-specific output for n samples.
 * @param[in] ToLinearF Borrowed initialised float decode table of TSIZEP1
 * entries.
 */
static void horizontalAccumulateF(uint16_t *wp, tmsize_t n, int stride,
                                  uint8_t *op, float *ToLinearF)
{
    unsigned int cr, cg, cb, ca, mask;
    float t0, t1, t2, t3;

    if (n >= stride)
    {
        mask = CODE_MASK;
        if (stride == 3)
        {
            t0 = ToLinearF[cr = (wp[0] & mask)];
            t1 = ToLinearF[cg = (wp[1] & mask)];
            t2 = ToLinearF[cb = (wp[2] & mask)];
            PixarLogStoreFloatNativeUnaligned(op, t0);
            PixarLogStoreFloatNativeUnaligned(op + sizeof(float), t1);
            PixarLogStoreFloatNativeUnaligned(op + 2 * sizeof(float), t2);
            n -= 3;
            while (n > 0)
            {
                wp += 3;
                op += 3 * sizeof(float);
                n -= 3;
                t0 = ToLinearF[(cr += wp[0]) & mask];
                t1 = ToLinearF[(cg += wp[1]) & mask];
                t2 = ToLinearF[(cb += wp[2]) & mask];
                PixarLogStoreFloatNativeUnaligned(op, t0);
                PixarLogStoreFloatNativeUnaligned(op + sizeof(float), t1);
                PixarLogStoreFloatNativeUnaligned(op + 2 * sizeof(float), t2);
            }
        }
        else if (stride == 4)
        {
            t0 = ToLinearF[cr = (wp[0] & mask)];
            t1 = ToLinearF[cg = (wp[1] & mask)];
            t2 = ToLinearF[cb = (wp[2] & mask)];
            t3 = ToLinearF[ca = (wp[3] & mask)];
            PixarLogStoreFloatNativeUnaligned(op, t0);
            PixarLogStoreFloatNativeUnaligned(op + sizeof(float), t1);
            PixarLogStoreFloatNativeUnaligned(op + 2 * sizeof(float), t2);
            PixarLogStoreFloatNativeUnaligned(op + 3 * sizeof(float), t3);
            n -= 4;
            while (n > 0)
            {
                wp += 4;
                op += 4 * sizeof(float);
                n -= 4;
                t0 = ToLinearF[(cr += wp[0]) & mask];
                t1 = ToLinearF[(cg += wp[1]) & mask];
                t2 = ToLinearF[(cb += wp[2]) & mask];
                t3 = ToLinearF[(ca += wp[3]) & mask];
                PixarLogStoreFloatNativeUnaligned(op, t0);
                PixarLogStoreFloatNativeUnaligned(op + sizeof(float), t1);
                PixarLogStoreFloatNativeUnaligned(op + 2 * sizeof(float), t2);
                PixarLogStoreFloatNativeUnaligned(op + 3 * sizeof(float), t3);
            }
        }
        else
        {
            REPEAT(stride,
                   PixarLogStoreFloatNativeUnaligned(op, ToLinearF[*wp & mask]);
                   wp++; op += sizeof(float))
            n -= stride;
            while (n > 0)
            {
                REPEAT(stride, *wp = (uint16_t)(*wp + wp[-stride]);
                       PixarLogStoreFloatNativeUnaligned(op,
                                                         ToLinearF[*wp & mask]);
                       wp++; op += sizeof(float))
                n -= stride;
            }
        }
    }
}

/**
 * @brief Accumulate PixarLog prediction into twelve-bit PICIO values scaled by
 * 2048 and capped at 3071.
 * @ingroup libtiff_private_pixarlog
 *
 * Requires positive stride and n complete interleaved samples, with disjoint
 * code/output storage. Recovers horizontal prediction modulo 2048; generic
 * stride paths update the input code array in place. Does nothing when n is
 * less than stride. Maps codes through ToLinearF, scales by SCALE12 and caps
 * with CLAMP12, storing native 16-bit values through the unaligned store
 * helper. No allocation, I/O, diagnostics, retained pointers, ownership
 * transfer or independent extent validation; void result.
 *
 * @param[in,out] wp Aligned n-entry uint16_t code input; generic-stride
 * paths update it in place.
 * @param[in] n Nonnegative sample count divisible by positive stride;
 * buffers must cover complete rows.
 * @param[in] stride Positive samples per interleaved pixel, or one for
 * separate data.
 * @param[out] op Writable format-specific output for n samples.
 * @param[in] ToLinearF Borrowed initialised float decode table of TSIZEP1
 * entries.
 */
static void horizontalAccumulate12(uint16_t *wp, tmsize_t n, int stride,
                                   uint8_t *op, float *ToLinearF)
{
    unsigned int cr, cg, cb, ca, mask;
    float t0, t1, t2, t3;

#define SCALE12 2048.0f
#define CLAMP12(t) (((t) < 3071) ? (int16_t)(uint16_t)(t) : (int16_t)3071)

    if (n >= stride)
    {
        mask = CODE_MASK;
        if (stride == 3)
        {
            t0 = ToLinearF[cr = (wp[0] & mask)] * SCALE12;
            t1 = ToLinearF[cg = (wp[1] & mask)] * SCALE12;
            t2 = ToLinearF[cb = (wp[2] & mask)] * SCALE12;
            PixarLogStore16NativeUnaligned(op, (uint16_t)CLAMP12(t0));
            PixarLogStore16NativeUnaligned(op + sizeof(uint16_t),
                                           (uint16_t)CLAMP12(t1));
            PixarLogStore16NativeUnaligned(op + 2 * sizeof(uint16_t),
                                           (uint16_t)CLAMP12(t2));
            n -= 3;
            while (n > 0)
            {
                wp += 3;
                op += 3 * sizeof(uint16_t);
                n -= 3;
                t0 = ToLinearF[(cr += wp[0]) & mask] * SCALE12;
                t1 = ToLinearF[(cg += wp[1]) & mask] * SCALE12;
                t2 = ToLinearF[(cb += wp[2]) & mask] * SCALE12;
                PixarLogStore16NativeUnaligned(op, (uint16_t)CLAMP12(t0));
                PixarLogStore16NativeUnaligned(op + sizeof(uint16_t),
                                               (uint16_t)CLAMP12(t1));
                PixarLogStore16NativeUnaligned(op + 2 * sizeof(uint16_t),
                                               (uint16_t)CLAMP12(t2));
            }
        }
        else if (stride == 4)
        {
            t0 = ToLinearF[cr = (wp[0] & mask)] * SCALE12;
            t1 = ToLinearF[cg = (wp[1] & mask)] * SCALE12;
            t2 = ToLinearF[cb = (wp[2] & mask)] * SCALE12;
            t3 = ToLinearF[ca = (wp[3] & mask)] * SCALE12;
            PixarLogStore16NativeUnaligned(op, (uint16_t)CLAMP12(t0));
            PixarLogStore16NativeUnaligned(op + sizeof(uint16_t),
                                           (uint16_t)CLAMP12(t1));
            PixarLogStore16NativeUnaligned(op + 2 * sizeof(uint16_t),
                                           (uint16_t)CLAMP12(t2));
            PixarLogStore16NativeUnaligned(op + 3 * sizeof(uint16_t),
                                           (uint16_t)CLAMP12(t3));
            n -= 4;
            while (n > 0)
            {
                wp += 4;
                op += 4 * sizeof(uint16_t);
                n -= 4;
                t0 = ToLinearF[(cr += wp[0]) & mask] * SCALE12;
                t1 = ToLinearF[(cg += wp[1]) & mask] * SCALE12;
                t2 = ToLinearF[(cb += wp[2]) & mask] * SCALE12;
                t3 = ToLinearF[(ca += wp[3]) & mask] * SCALE12;
                PixarLogStore16NativeUnaligned(op, (uint16_t)CLAMP12(t0));
                PixarLogStore16NativeUnaligned(op + sizeof(uint16_t),
                                               (uint16_t)CLAMP12(t1));
                PixarLogStore16NativeUnaligned(op + 2 * sizeof(uint16_t),
                                               (uint16_t)CLAMP12(t2));
                PixarLogStore16NativeUnaligned(op + 3 * sizeof(uint16_t),
                                               (uint16_t)CLAMP12(t3));
            }
        }
        else
        {
            REPEAT(stride, t0 = ToLinearF[*wp & mask] * SCALE12;
                   PixarLogStore16NativeUnaligned(op, (uint16_t)CLAMP12(t0));
                   wp++; op += sizeof(uint16_t))
            n -= stride;
            while (n > 0)
            {
                REPEAT(
                    stride, *wp = (uint16_t)(*wp + wp[-stride]);
                    t0 = ToLinearF[*wp & mask] * SCALE12;
                    PixarLogStore16NativeUnaligned(op, (uint16_t)CLAMP12(t0));
                    wp++; op += sizeof(uint16_t))
                n -= stride;
            }
        }
    }
}

/**
 * @brief Accumulate PixarLog prediction into clipped unsigned sixteen-bit
 * linear samples.
 * @ingroup libtiff_private_pixarlog
 *
 * Requires positive stride and n complete interleaved samples, with disjoint
 * code/output storage. Recovers horizontal prediction modulo 2048; generic
 * stride paths update the input code array in place. Does nothing when n is
 * less than stride. Maps codes through ToLinear16 and stores native uint16_t
 * values through the unaligned store helper. No allocation, I/O,
 * diagnostics, retained pointers, ownership transfer or independent extent
 * validation; void result.
 *
 * @param[in,out] wp Aligned n-entry uint16_t code input; generic-stride
 * paths update it in place.
 * @param[in] n Nonnegative sample count divisible by positive stride;
 * buffers must cover complete rows.
 * @param[in] stride Positive samples per interleaved pixel, or one for
 * separate data.
 * @param[out] op Writable format-specific output for n samples.
 * @param[in] ToLinear16 Borrowed initialised uint16_t decode table of
 * TSIZEP1 entries.
 */
static void horizontalAccumulate16(uint16_t *wp, tmsize_t n, int stride,
                                   uint8_t *op, uint16_t *ToLinear16)
{
    unsigned int cr, cg, cb, ca, mask;

    if (n >= stride)
    {
        mask = CODE_MASK;
        if (stride == 3)
        {
            PixarLogStore16NativeUnaligned(op, ToLinear16[cr = (wp[0] & mask)]);
            PixarLogStore16NativeUnaligned(op + sizeof(uint16_t),
                                           ToLinear16[cg = (wp[1] & mask)]);
            PixarLogStore16NativeUnaligned(op + 2 * sizeof(uint16_t),
                                           ToLinear16[cb = (wp[2] & mask)]);
            n -= 3;
            while (n > 0)
            {
                wp += 3;
                op += 3 * sizeof(uint16_t);
                n -= 3;
                PixarLogStore16NativeUnaligned(
                    op, ToLinear16[(cr += wp[0]) & mask]);
                PixarLogStore16NativeUnaligned(
                    op + sizeof(uint16_t), ToLinear16[(cg += wp[1]) & mask]);
                PixarLogStore16NativeUnaligned(
                    op + 2 * sizeof(uint16_t),
                    ToLinear16[(cb += wp[2]) & mask]);
            }
        }
        else if (stride == 4)
        {
            PixarLogStore16NativeUnaligned(op, ToLinear16[cr = (wp[0] & mask)]);
            PixarLogStore16NativeUnaligned(op + sizeof(uint16_t),
                                           ToLinear16[cg = (wp[1] & mask)]);
            PixarLogStore16NativeUnaligned(op + 2 * sizeof(uint16_t),
                                           ToLinear16[cb = (wp[2] & mask)]);
            PixarLogStore16NativeUnaligned(op + 3 * sizeof(uint16_t),
                                           ToLinear16[ca = (wp[3] & mask)]);
            n -= 4;
            while (n > 0)
            {
                wp += 4;
                op += 4 * sizeof(uint16_t);
                n -= 4;
                PixarLogStore16NativeUnaligned(
                    op, ToLinear16[(cr += wp[0]) & mask]);
                PixarLogStore16NativeUnaligned(
                    op + sizeof(uint16_t), ToLinear16[(cg += wp[1]) & mask]);
                PixarLogStore16NativeUnaligned(
                    op + 2 * sizeof(uint16_t),
                    ToLinear16[(cb += wp[2]) & mask]);
                PixarLogStore16NativeUnaligned(
                    op + 3 * sizeof(uint16_t),
                    ToLinear16[(ca += wp[3]) & mask]);
            }
        }
        else
        {
            REPEAT(stride,
                   PixarLogStore16NativeUnaligned(op, ToLinear16[*wp & mask]);
                   wp++; op += sizeof(uint16_t))
            n -= stride;
            while (n > 0)
            {
                REPEAT(
                    stride, *wp = (uint16_t)(*wp + wp[-stride]);
                    PixarLogStore16NativeUnaligned(op, ToLinear16[*wp & mask]);
                    wp++; op += sizeof(uint16_t))
                n -= stride;
            }
        }
    }
}

/*
 * Returns the log encoded 11-bit values with the horizontal
 * differencing undone.
 */
/**
 * @brief Accumulate PixarLog prediction into raw eleven-bit code words in
 * native uint16_t slots.
 * @ingroup libtiff_private_pixarlog
 *
 * Requires positive stride and n complete interleaved samples, with disjoint
 * code/output storage. Recovers horizontal prediction modulo 2048; generic
 * stride paths update the input code array in place. Does nothing when n is
 * less than stride. Stores the accumulated codes themselves (no lookup
 * table) as native uint16_t values through the unaligned store helper. For
 * stride 3 and 4 the first pixel's codes are copied unmasked, so output from
 * malformed streams can exceed 11 bits; later pixels and the generic-stride
 * path are masked. No allocation, I/O, diagnostics, retained pointers,
 * ownership transfer or independent extent validation; void result.
 *
 * @param[in,out] wp Aligned n-entry uint16_t code input; generic-stride
 * paths update it in place.
 * @param[in] n Nonnegative sample count divisible by positive stride;
 * buffers must cover complete rows.
 * @param[in] stride Positive samples per interleaved pixel, or one for
 * separate data.
 * @param[out] op Writable format-specific output for n samples.
 */
static void horizontalAccumulate11(uint16_t *wp, tmsize_t n, int stride,
                                   uint8_t *op)
{
    unsigned int cr, cg, cb, ca, mask;

    if (n >= stride)
    {
        mask = CODE_MASK;
        if (stride == 3)
        {
            PixarLogStore16NativeUnaligned(op, wp[0]);
            PixarLogStore16NativeUnaligned(op + sizeof(uint16_t), wp[1]);
            PixarLogStore16NativeUnaligned(op + 2 * sizeof(uint16_t), wp[2]);
            cr = wp[0];
            cg = wp[1];
            cb = wp[2];
            n -= 3;
            while (n > 0)
            {
                wp += 3;
                op += 3 * sizeof(uint16_t);
                n -= 3;
                PixarLogStore16NativeUnaligned(
                    op, (uint16_t)((cr += wp[0]) & mask));
                PixarLogStore16NativeUnaligned(
                    op + sizeof(uint16_t), (uint16_t)((cg += wp[1]) & mask));
                PixarLogStore16NativeUnaligned(
                    op + 2 * sizeof(uint16_t),
                    (uint16_t)((cb += wp[2]) & mask));
            }
        }
        else if (stride == 4)
        {
            PixarLogStore16NativeUnaligned(op, wp[0]);
            PixarLogStore16NativeUnaligned(op + sizeof(uint16_t), wp[1]);
            PixarLogStore16NativeUnaligned(op + 2 * sizeof(uint16_t), wp[2]);
            PixarLogStore16NativeUnaligned(op + 3 * sizeof(uint16_t), wp[3]);
            cr = wp[0];
            cg = wp[1];
            cb = wp[2];
            ca = wp[3];
            n -= 4;
            while (n > 0)
            {
                wp += 4;
                op += 4 * sizeof(uint16_t);
                n -= 4;
                PixarLogStore16NativeUnaligned(
                    op, (uint16_t)((cr += wp[0]) & mask));
                PixarLogStore16NativeUnaligned(
                    op + sizeof(uint16_t), (uint16_t)((cg += wp[1]) & mask));
                PixarLogStore16NativeUnaligned(
                    op + 2 * sizeof(uint16_t),
                    (uint16_t)((cb += wp[2]) & mask));
                PixarLogStore16NativeUnaligned(
                    op + 3 * sizeof(uint16_t),
                    (uint16_t)((ca += wp[3]) & mask));
            }
        }
        else
        {
            REPEAT(stride,
                   PixarLogStore16NativeUnaligned(op, (uint16_t)(*wp & mask));
                   wp++; op += sizeof(uint16_t))
            n -= stride;
            while (n > 0)
            {
                REPEAT(
                    stride, *wp = (uint16_t)(*wp + wp[-stride]);
                    PixarLogStore16NativeUnaligned(op, (uint16_t)(*wp & mask));
                    wp++; op += sizeof(uint16_t))
                n -= stride;
            }
        }
    }
}

/**
 * @brief Accumulate PixarLog prediction into clipped unsigned eight-bit linear
 * samples.
 * @ingroup libtiff_private_pixarlog
 *
 * Requires positive stride and n complete interleaved samples, with disjoint
 * code/output storage. Recovers horizontal prediction modulo 2048; generic
 * stride paths update the input code array in place. Does nothing when n is
 * less than stride. Maps codes through ToLinear8 and writes one byte per
 * sample. No allocation, I/O, diagnostics, retained pointers, ownership
 * transfer or independent extent validation; void result.
 *
 * @param[in,out] wp Aligned n-entry uint16_t code input; generic-stride
 * paths update it in place.
 * @param[in] n Nonnegative sample count divisible by positive stride;
 * buffers must cover complete rows.
 * @param[in] stride Positive samples per interleaved pixel, or one for
 * separate data.
 * @param[out] op Writable format-specific output for n samples.
 * @param[in] ToLinear8 Borrowed initialised byte decode table of TSIZEP1
 * entries.
 */
static void horizontalAccumulate8(uint16_t *wp, tmsize_t n, int stride,
                                  unsigned char *op, unsigned char *ToLinear8)
{
    unsigned int cr, cg, cb, ca, mask;

    if (n >= stride)
    {
        mask = CODE_MASK;
        if (stride == 3)
        {
            op[0] = ToLinear8[cr = (wp[0] & mask)];
            op[1] = ToLinear8[cg = (wp[1] & mask)];
            op[2] = ToLinear8[cb = (wp[2] & mask)];
            n -= 3;
            while (n > 0)
            {
                n -= 3;
                wp += 3;
                op += 3;
                op[0] = ToLinear8[(cr += wp[0]) & mask];
                op[1] = ToLinear8[(cg += wp[1]) & mask];
                op[2] = ToLinear8[(cb += wp[2]) & mask];
            }
        }
        else if (stride == 4)
        {
            op[0] = ToLinear8[cr = (wp[0] & mask)];
            op[1] = ToLinear8[cg = (wp[1] & mask)];
            op[2] = ToLinear8[cb = (wp[2] & mask)];
            op[3] = ToLinear8[ca = (wp[3] & mask)];
            n -= 4;
            while (n > 0)
            {
                n -= 4;
                wp += 4;
                op += 4;
                op[0] = ToLinear8[(cr += wp[0]) & mask];
                op[1] = ToLinear8[(cg += wp[1]) & mask];
                op[2] = ToLinear8[(cb += wp[2]) & mask];
                op[3] = ToLinear8[(ca += wp[3]) & mask];
            }
        }
        else
        {
            REPEAT(stride, *op = ToLinear8[*wp & mask]; wp++; op++)
            n -= stride;
            while (n > 0)
            {
                REPEAT(stride, *wp = (uint16_t)(*wp + wp[-stride]);
                       *op = ToLinear8[*wp & mask]; wp++; op++)
                n -= stride;
            }
        }
    }
}

/**
 * @brief Accumulate PixarLog prediction into eight-bit ABGR bytes.
 * @ingroup libtiff_private_pixarlog
 *
 * Requires positive stride and n complete interleaved samples, with disjoint
 * code/output storage. Recovers horizontal prediction modulo 2048; generic
 * stride paths update the input code array in place. Does nothing when n is
 * less than stride. Maps codes through ToLinear8 and writes bytes. Stride
 * three expands each RGB triplet to four bytes with a leading zero; stride
 * four reverses RGBA to ABGR; other strides preserve order. No allocation,
 * I/O, diagnostics, retained pointers, ownership transfer or independent
 * extent validation; void result.
 *
 * @param[in,out] wp Aligned n-entry uint16_t code input; generic-stride
 * paths update it in place.
 * @param[in] n Nonnegative sample count divisible by positive stride;
 * buffers must cover complete rows.
 * @param[in] stride Positive samples per interleaved pixel, or one for
 * separate data.
 * @param[out] op Borrowed writable format-specific output for the requested
 * complete samples/rows; ABGR stride three requires four bytes per pixel.
 * @param[in] ToLinear8 Borrowed initialised byte decode table of TSIZEP1
 * entries.
 */
static void horizontalAccumulate8abgr(uint16_t *wp, tmsize_t n, int stride,
                                      unsigned char *op,
                                      unsigned char *ToLinear8)
{
    unsigned int cr, cg, cb, ca, mask;
    unsigned char t0, t1, t2, t3;

    if (n >= stride)
    {
        mask = CODE_MASK;
        if (stride == 3)
        {
            op[0] = 0;
            t1 = ToLinear8[cb = (wp[2] & mask)];
            t2 = ToLinear8[cg = (wp[1] & mask)];
            t3 = ToLinear8[cr = (wp[0] & mask)];
            op[1] = t1;
            op[2] = t2;
            op[3] = t3;
            n -= 3;
            while (n > 0)
            {
                n -= 3;
                wp += 3;
                op += 4;
                op[0] = 0;
                t1 = ToLinear8[(cb += wp[2]) & mask];
                t2 = ToLinear8[(cg += wp[1]) & mask];
                t3 = ToLinear8[(cr += wp[0]) & mask];
                op[1] = t1;
                op[2] = t2;
                op[3] = t3;
            }
        }
        else if (stride == 4)
        {
            t0 = ToLinear8[ca = (wp[3] & mask)];
            t1 = ToLinear8[cb = (wp[2] & mask)];
            t2 = ToLinear8[cg = (wp[1] & mask)];
            t3 = ToLinear8[cr = (wp[0] & mask)];
            op[0] = t0;
            op[1] = t1;
            op[2] = t2;
            op[3] = t3;
            n -= 4;
            while (n > 0)
            {
                n -= 4;
                wp += 4;
                op += 4;
                t0 = ToLinear8[(ca += wp[3]) & mask];
                t1 = ToLinear8[(cb += wp[2]) & mask];
                t2 = ToLinear8[(cg += wp[1]) & mask];
                t3 = ToLinear8[(cr += wp[0]) & mask];
                op[0] = t0;
                op[1] = t1;
                op[2] = t2;
                op[3] = t3;
            }
        }
        else
        {
            REPEAT(stride, *op = ToLinear8[*wp & mask]; wp++; op++)
            n -= stride;
            while (n > 0)
            {
                REPEAT(stride, *wp = (uint16_t)(*wp + wp[-stride]);
                       *op = ToLinear8[*wp & mask]; wp++; op++)
                n -= stride;
            }
        }
    }
}

/*
 * State block for each open TIFF
 * file using PixarLog compression/decompression.
 */
/**
 * @brief Own PixarLog companding tables predictor prefix and zlib progress.
 * @ingroup libtiff_private_pixarlog
 *
 * Codec-owned state must remain live until cleanup. Internal buffers and
 * dependency objects have the individual ownership described below;
 * borrowed parent methods must remain callable.
 */
typedef struct
{
    /** @brief Predictor prefix, first in codec allocation. */
    TIFFPredictorState predict;
    /** @brief Zlib stream whose internal allocation ends during cleanup. */
    z_stream stream;
    /**
     * @brief Decode code-staging byte capacity; encoder does not populate this
     * member.
     */
    tmsize_t tbuf_size;
    /** @brief Owned Ext aligned uint16_t code staging, released by cleanup. */
    uint16_t *tbuf;
    /** @brief Samples per interleaved pixel or one for separate data. */
    uint16_t stride;
    /** @brief PLSTATE_INIT after dependency setup. */
    int state;
    /** @brief Cached application-facing format, initially UNKNOWN. */
    int user_datafmt;
    /**
     * @brief Cached zlib quality, applied at setup or active parameter update.
     */
    int quality;
#define PLSTATE_INIT 1

    /**
     * @brief Borrowed parent getter stored under the declared compatible method
     * typedef.
     */
    TIFFVSetMethod vgetparent;
    /** @brief Borrowed parent setter restored during cleanup. */
    TIFFVSetMethod vsetparent;

    /** @brief Owned Ext float decode table, TSIZEP1 entries. */
    float *ToLinearF;
    /** @brief Owned Ext clipped sixteen-bit decode table. */
    uint16_t *ToLinear16;
    /** @brief Owned Ext clipped eight-bit decode table. */
    unsigned char *ToLinear8;
    /** @brief Owned Ext low-range float-to-code mapping. */
    uint16_t *FromLT2;
    /** @brief Owned Ext 16384-entry lookup for input shifted right two bits. */
    uint16_t *From14;
    /** @brief Owned Ext 256-entry byte-to-code lookup. */
    uint16_t *From8;

} PixarLogState;

/**
 * @brief Build linear/log companding tables for PixarLog sample formats.
 * @ingroup libtiff_private_pixarlog
 *
 * Requires fresh NULL table pointers. Allocates six Ext tables, derives
 * linear low-end and logarithmic high-end mapping, clamps integer decode
 * outputs and reduces sixteen-bit encode lookup to fourteen significant
 * bits. Failure frees new tables and clears state pointers. Writes
 * process-global Fltsize/LogK coefficients; serialise concurrent table
 * initialisation and float encoding externally. Codec owns tables until
 * cleanup; no pixel I/O.
 *
 * @param[in,out] tif Non-NULL live PixarLog handle whose Ext allocator
 * receives the table allocations; serialise handle operations.
 * @param[in,out] sp Borrowed live codec state with fresh table pointers.
 * @return One after table construction, zero on allocation failure.
 */
static int PixarLogMakeTables(TIFF *tif, PixarLogState *sp)
{

    /*
     *    We make several tables here to convert between various external
     *    representations (float, 16-bit, and 8-bit) and the internal
     *    11-bit companded representation.  The 11-bit representation has two
     *    distinct regions.  A linear bottom end up through .018316 in steps
     *    of about .000073, and a region of constant ratio up to about 25.
     *    These floating point numbers are stored in the main table ToLinearF.
     *    All other tables are derived from this one.  The tables (and the
     *    ratios) are continuous at the internal seam.
     */

    int nlin, lt2size;
    int i, j;
    double b, c, linstep, v;
    float *ToLinearF;
    uint16_t *ToLinear16;
    unsigned char *ToLinear8;
    uint16_t *FromLT2;
    uint16_t *From14; /* Really for 16-bit data, but we shift down 2 */
    uint16_t *From8;

    c = log(RATIO);
    nlin = (int)(1. / c); /* nlin must be an integer */
    c = 1. / nlin;
    b = exp(-c * ONE); /* multiplicative scale factor [b*exp(c*ONE) = 1] */
    linstep = b * c * exp(1.);

    LogK1 = (float)(1. / c); /* if (v >= 2)  token = k1*log(v*k2) */
    LogK2 = (float)(1. / b);
    lt2size = (int)(2. / linstep) + 1;
    FromLT2 = (uint16_t *)_TIFFmallocExt(
        tif, (tmsize_t)((size_t)lt2size * sizeof(uint16_t)));
    From14 = (uint16_t *)_TIFFmallocExt(tif, 16384 * sizeof(uint16_t));
    From8 = (uint16_t *)_TIFFmallocExt(tif, 256 * sizeof(uint16_t));
    ToLinearF = (float *)_TIFFmallocExt(tif, TSIZEP1 * sizeof(float));
    ToLinear16 = (uint16_t *)_TIFFmallocExt(tif, TSIZEP1 * sizeof(uint16_t));
    ToLinear8 =
        (unsigned char *)_TIFFmallocExt(tif, TSIZEP1 * sizeof(unsigned char));
    if (FromLT2 == NULL || From14 == NULL || From8 == NULL ||
        ToLinearF == NULL || ToLinear16 == NULL || ToLinear8 == NULL)
    {
        if (FromLT2)
            _TIFFfreeExt(tif, FromLT2);
        if (From14)
            _TIFFfreeExt(tif, From14);
        if (From8)
            _TIFFfreeExt(tif, From8);
        if (ToLinearF)
            _TIFFfreeExt(tif, ToLinearF);
        if (ToLinear16)
            _TIFFfreeExt(tif, ToLinear16);
        if (ToLinear8)
            _TIFFfreeExt(tif, ToLinear8);
        sp->FromLT2 = NULL;
        sp->From14 = NULL;
        sp->From8 = NULL;
        sp->ToLinearF = NULL;
        sp->ToLinear16 = NULL;
        sp->ToLinear8 = NULL;
        return 0;
    }

    j = 0;

    for (i = 0; i < nlin; i++)
    {
        v = i * linstep;
        ToLinearF[j++] = (float)v;
    }

    for (i = nlin; i < TSIZE; i++)
        ToLinearF[j++] = (float)(b * exp(c * i));

    ToLinearF[2048] = ToLinearF[2047];

    for (i = 0; i < TSIZEP1; i++)
    {
        v = (double)ToLinearF[i] * 65535.0 + 0.5;
        ToLinear16[i] = (v > 65535.0) ? 65535 : (uint16_t)v;
        v = (double)ToLinearF[i] * 255.0 + 0.5;
        ToLinear8[i] = (v > 255.0) ? 255 : (unsigned char)v;
    }

    j = 0;
    for (i = 0; i < lt2size; i++)
    {
        if ((i * linstep) * (i * linstep) >
            (double)ToLinearF[j] * (double)ToLinearF[j + 1])
            j++;
        FromLT2[i] = (uint16_t)j;
    }

    /*
     * Since we lose info anyway on 16-bit data, we set up a 14-bit
     * table and shift 16-bit values down two bits on input.
     * saves a little table space.
     */
    j = 0;
    for (i = 0; i < 16384; i++)
    {
        while ((i / 16383.) * (i / 16383.) >
               (double)ToLinearF[j] * (double)ToLinearF[j + 1])
            j++;
        From14[i] = (uint16_t)j;
    }

    j = 0;
    for (i = 0; i < 256; i++)
    {
        while ((i / 255.) * (i / 255.) >
               (double)ToLinearF[j] * (double)ToLinearF[j + 1])
            j++;
        From8[i] = (uint16_t)j;
    }

    Fltsize = (float)(lt2size / 2);

    sp->ToLinearF = ToLinearF;
    sp->ToLinear16 = ToLinear16;
    sp->ToLinear8 = ToLinear8;
    sp->FromLT2 = FromLT2;
    sp->From14 = From14;
    sp->From8 = From8;

    return 1;
}

#define PixarLogDecoderState(tif) ((PixarLogState *)(tif)->tif_data)
#define PixarLogEncoderState(tif) ((PixarLogState *)(tif)->tif_data)

static int PixarLogEncode(TIFF *tif, uint8_t *bp, tmsize_t cc, uint16_t s);
static int PixarLogDecode(TIFF *tif, uint8_t *op, tmsize_t occ, uint16_t s);

#define PIXARLOGDATAFMT_UNKNOWN -1

/**
 * @brief Infer the PixarLog user format from depth and sample format.
 * @ingroup libtiff_private_pixarlog
 *
 * Recognises float32, unsigned/void 8/11/16, and signed/void twelve-bit
 * combinations. Reads borrowed directory only, without allocation,
 * diagnostics, I/O or ownership effects.
 *
 * @param[in] td Borrowed current directory metadata.
 * @return Matching PIXARLOGDATAFMT value, or PIXARLOGDATAFMT_UNKNOWN.
 */
static int PixarLogGuessDataFmt(TIFFDirectory *td)
{
    int guess = PIXARLOGDATAFMT_UNKNOWN;
    int format = td->td_sampleformat;

    /* If the user didn't tell us his datafmt,
     * take our best guess from the bitspersample.
     */
    switch (td->td_bitspersample)
    {
        case 32:
            if (format == SAMPLEFORMAT_IEEEFP)
                guess = PIXARLOGDATAFMT_FLOAT;
            break;
        case 16:
            if (format == SAMPLEFORMAT_VOID || format == SAMPLEFORMAT_UINT)
                guess = PIXARLOGDATAFMT_16BIT;
            break;
        case 12:
            if (format == SAMPLEFORMAT_VOID || format == SAMPLEFORMAT_INT)
                guess = PIXARLOGDATAFMT_12BITPICIO;
            break;
        case 11:
            if (format == SAMPLEFORMAT_VOID || format == SAMPLEFORMAT_UINT)
                guess = PIXARLOGDATAFMT_11BITLOG;
            break;
        case 8:
            if (format == SAMPLEFORMAT_VOID || format == SAMPLEFORMAT_UINT)
                guess = PIXARLOGDATAFMT_8BIT;
            break;
        default:
            break;
    }

    return guess;
}

/**
 * @brief Multiply positive buffer extents with signed-size checking.
 * @ingroup libtiff_private_pixarlog
 *
 * Delegates to _TIFFMultiplySSize without a handle or diagnostic label; no
 * allocation, I/O, state or ownership effects.
 *
 * @param[in] m1 Nonnegative first extent.
 * @param[in] m2 Nonnegative second extent.
 * @return Positive checked product, or zero for zero/rejected/overflowed
 * operands.
 */
static tmsize_t multiply_ms(tmsize_t m1, tmsize_t m2)
{
    return _TIFFMultiplySSize(NULL, m1, m2, NULL);
}

/**
 * @brief Add positive buffer extents with signed overflow rejection.
 * @ingroup libtiff_private_pixarlog
 *
 * Requires nonnegative operands, asserted in debug builds. Treats either
 * zero as a prior failure sentinel rather than an arithmetic identity. No
 * allocation, I/O, diagnostics, state or ownership effects.
 *
 * @param[in] m1 Nonnegative first extent.
 * @param[in] m2 Nonnegative second extent.
 * @return Positive sum, or zero for a zero operand or overflow.
 */
static tmsize_t add_ms(tmsize_t m1, tmsize_t m2)
{
    assert(m1 >= 0 && m2 >= 0);
    /* if either input is zero, assume overflow already occurred */
    if (m1 == 0 || m2 == 0)
        return 0;
    else if (m1 > TIFF_TMSIZE_T_MAX - m2)
        return 0;

    return m1 + m2;
}

/**
 * @brief Accept PixarLog tag fixup without changes.
 * @ingroup libtiff_private_pixarlog
 *
 * No allocation, I/O, diagnostics, state or ownership effects.
 *
 * @param[in] tif Unused PixarLog handle.
 * @return One unconditionally.
 */
static int PixarLogFixupTags(TIFF *tif)
{
    (void)tif;
    return (1);
}

/**
 * @brief Allocate companded row storage and initialise PixarLog inflation.
 * @ingroup libtiff_private_pixarlog
 *
 * Returns immediately for initialised state. Disables ordinary postdecode
 * swapping, derives positive sample stride, sizes tile/strip staging with
 * an extra stride through checked size helpers, allocates Ext storage,
 * guesses user format if needed and calls inflateInit. Detected
 * format/dependency errors diagnose; some extent/allocation rejections have
 * no explicit diagnostic. Failure can change postdecode/stride and releases
 * staging on specified format/dependency paths; no rollback or caller
 * ownership transfer.
 *
 * @param[in,out] tif Non-NULL live PixarLog handle with valid
 * state/geometry; serialise handle operations.
 * @return One after setup/already initialised, zero on
 * extent/allocation/format/dependency failure.
 */
static int PixarLogSetupDecode(TIFF *tif)
{
    static const char module[] = "PixarLogSetupDecode";
    TIFFDirectory *td = &tif->tif_dir;
    PixarLogState *sp = PixarLogDecoderState(tif);
    tmsize_t tbuf_size;
    uint32_t segment_width, segment_height;

    assert(sp != NULL);

    /* This function can possibly be called several times by */
    /* PredictorSetupDecode() if this function succeeds but */
    /* PredictorSetup() fails */
    if ((sp->state & PLSTATE_INIT) != 0)
        return 1;

    if (isTiled(tif))
    {
        segment_width = td->td_tilewidth;
        segment_height = td->td_tilelength;
    }
    else
    {
        segment_width = td->td_imagewidth;
        segment_height = td->td_rowsperstrip;
        if (segment_height > td->td_imagelength)
            segment_height = td->td_imagelength;
    }

    /* Make sure no byte swapping happens on the data
     * after decompression. */
    tif->tif_postdecode = _TIFFNoPostDecode;

    /* for some reason, we can't do this in TIFFInitPixarLog */

    sp->stride =
        (td->td_planarconfig == PLANARCONFIG_CONTIG ? td->td_samplesperpixel
                                                    : 1);
    tbuf_size = multiply_ms(
        multiply_ms(multiply_ms(sp->stride, segment_width), segment_height),
        sizeof(uint16_t));
    /* add one more stride in case input ends mid-stride */
    tbuf_size =
        add_ms(tbuf_size, (tmsize_t)(sizeof(uint16_t) * (size_t)sp->stride));
    if (tbuf_size == 0)
        return (0); /* TODO: this is an error return without error report
                       through TIFFErrorExt */
    sp->tbuf = (uint16_t *)_TIFFmallocExt(tif, tbuf_size);
    if (sp->tbuf == NULL)
        return (0);
    sp->tbuf_size = tbuf_size;
    if (sp->user_datafmt == PIXARLOGDATAFMT_UNKNOWN)
        sp->user_datafmt = PixarLogGuessDataFmt(td);
    if (sp->user_datafmt == PIXARLOGDATAFMT_UNKNOWN)
    {
        _TIFFfreeExt(tif, sp->tbuf);
        sp->tbuf = NULL;
        sp->tbuf_size = 0;
        TIFFErrorExtR(tif, module,
                      "PixarLog compression can't handle bits depth/data "
                      "format combination (depth: %" PRIu16 ")",
                      td->td_bitspersample);
        return (0);
    }

    if (inflateInit(&sp->stream) != Z_OK)
    {
        _TIFFfreeExt(tif, sp->tbuf);
        sp->tbuf = NULL;
        sp->tbuf_size = 0;
        TIFFErrorExtR(tif, module, "%s",
                      sp->stream.msg ? sp->stream.msg : "(null)");
        return (0);
    }
    else
    {
        sp->state |= PLSTATE_INIT;
        return (1);
    }
}

/*
 * Setup state for decoding a strip.
 */
/**
 * @brief Reset PixarLog inflation over the current raw strile.
 * @ingroup libtiff_private_pixarlog
 *
 * Requires successful decode setup. Attaches raw input, rejects a raw
 * extent that does not round-trip through zlib uInt and calls inflateReset.
 * Dependency state can change; no direct client I/O or ownership transfer.
 *
 * @param[in,out] tif Non-NULL live PixarLog handle with valid
 * state/geometry; serialise handle operations.
 * @param[in] s Unused sample-plane argument.
 * @return One on reset success, zero on size/reset rejection.
 */
static int PixarLogPreDecode(TIFF *tif, uint16_t s)
{
    static const char module[] = "PixarLogPreDecode";
    PixarLogState *sp = PixarLogDecoderState(tif);

    (void)s;
    assert(sp != NULL);
    sp->stream.next_in = tif->tif_rawdata;
    assert(sizeof(sp->stream.avail_in) == 4); /* if this assert gets raised,
         we need to simplify this code to reflect a ZLib that is likely updated
         to deal with 8byte memory sizes, though this code will respond
         appropriately even before we simplify it */
    sp->stream.avail_in = (uInt)tif->tif_rawcc;
    if ((tmsize_t)sp->stream.avail_in != tif->tif_rawcc)
    {
        TIFFErrorExtR(tif, module, "ZLib cannot deal with buffers this size");
        return (0);
    }
    return (inflateReset(&sp->stream) == Z_OK);
}

/**
 * @brief Inflate companded samples and convert complete PixarLog output rows.
 * @ingroup libtiff_private_pixarlog
 *
 * Requires successful predecode, positive stride/row width, representable
 * whole-row extents and adequate format-specific caller storage. Inflates
 * into bounded tbuf, commits raw cursors on success, swaps code words when
 * needed, accumulates prediction and converts to the selected output
 * format. A partial final sample row is warned/truncated and its unproduced
 * output tail zeroed. Error paths generally clear requested output and
 * diagnose after dependency state can change. The current three-channel
 * ABGR size comparison rejects ordinary positive expanded requests; do not
 * infer usable RGB-to-ABGR support from the format flag alone. No ownership
 * transfer; dependency allocations can occur, no direct client I/O.
 *
 * @param[in,out] tif Non-NULL live PixarLog handle with valid
 * state/geometry; serialise handle operations.
 * @param[out] op Borrowed writable format-specific output for the requested
 * complete samples/rows; ABGR stride three requires four bytes per pixel.
 * @param[in] occ Nonnegative requested output bytes in the selected user
 * format.
 * @param[in] s Unused sample-plane argument.
 * @return One after accepted conversion including warned truncation, zero
 * on detected format/extent/inflate failure.
 */
static int PixarLogDecode(TIFF *tif, uint8_t *op, tmsize_t occ, uint16_t s)
{
    static const char module[] = "PixarLogDecode";
    TIFFDirectory *td = &tif->tif_dir;
    PixarLogState *sp = PixarLogDecoderState(tif);
    tmsize_t i;
    tmsize_t nsamples;
    tmsize_t llen;
    uint32_t row_width;
    uint16_t *up;
    uint8_t *op_start = op;

    switch (sp->user_datafmt)
    {
        case PIXARLOGDATAFMT_FLOAT:
            nsamples = (tmsize_t)((uint64_t)occ /
                                  sizeof(float)); /* XXX float == 32 bits */
            break;
        case PIXARLOGDATAFMT_16BIT:
        case PIXARLOGDATAFMT_12BITPICIO:
        case PIXARLOGDATAFMT_11BITLOG:
            nsamples =
                (tmsize_t)((uint64_t)occ /
                           sizeof(uint16_t)); /* XXX uint16_t == 16 bits */
            break;
        case PIXARLOGDATAFMT_8BIT:
        case PIXARLOGDATAFMT_8BITABGR:
            nsamples = occ;
            break;
        default:
            TIFFErrorExtR(tif, module,
                          "%" PRIu16 " bit input not supported in PixarLog",
                          td->td_bitspersample);
            memset(op, 0, (size_t)occ);
            return 0;
    }

    row_width = isTiled(tif) ? td->td_tilewidth : td->td_imagewidth;
    /* stride (≤ td_samplesperpixel, max 65535) × row_width: fits tmsize_t */
    llen = (tmsize_t)sp->stride * row_width;

    /* Fix: ABGR with stride=3 expands 3 samples to 4 output bytes per pixel */
    if (sp->user_datafmt == PIXARLOGDATAFMT_8BITABGR && sp->stride == 3)
    {
        /* row_width × 4: fits tmsize_t (row_width is uint32) */
        tmsize_t required = (tmsize_t)row_width * 4;
        tmsize_t max_rows;
        tmsize_t max_nsamples;

        /*
         * Ensure at least one expanded output row fits.
         */
        if (occ < required)
        {
            TIFFErrorExtR(tif, module,
                          "Output buffer too small for PixarLog ABGR data");
            memset(op, 0, (size_t)occ);
            return (0);
        }

        /*
         * The caller-provided output buffer size must represent a whole
         * number of expanded ABGR scanlines.
         */
        if (occ % required)
        {
            TIFFErrorExtR(
                tif, module,
                "Fractional scanline not supported for PixarLog ABGR data");
            memset(op, 0, (size_t)occ);
            return (0);
        }

        /*
         * PixarLogDecode() may process multiple rows per call
         * (e.g. strip decoding). Limit nsamples so the total
         * output written by the loop below never exceeds occ.
         */
        max_rows = occ / required;
        max_nsamples = max_rows * llen;

        if (nsamples > max_nsamples)
        {
            TIFFErrorExtR(tif, module,
                          "Output buffer too small for PixarLog ABGR data");
            memset(op, 0, (size_t)occ);
            return (0);
        }
    }

    (void)s;
    assert(sp != NULL);

    sp->stream.next_in = tif->tif_rawcp;
    sp->stream.avail_in = (uInt)tif->tif_rawcc;

    sp->stream.next_out = (unsigned char *)sp->tbuf;
    assert(sizeof(sp->stream.avail_out) == 4); /* if this assert gets raised,
         we need to simplify this code to reflect a ZLib that is likely updated
         to deal with 8byte memory sizes, though this code will respond
         appropriately even before we simplify it */
    sp->stream.avail_out = (uInt)((unsigned long)nsamples * sizeof(uint16_t));
    if (sp->stream.avail_out != (unsigned long)nsamples * sizeof(uint16_t))
    {
        TIFFErrorExtR(tif, module, "ZLib cannot deal with buffers this size");
        memset(op, 0, (size_t)occ);
        return (0);
    }
    /* Check that we will not fill more than what was allocated */
    if ((tmsize_t)sp->stream.avail_out > sp->tbuf_size)
    {
        TIFFErrorExtR(tif, module, "sp->stream.avail_out > sp->tbuf_size");
        memset(op, 0, (size_t)occ);
        return (0);
    }
    do
    {
        int state = inflate(&sp->stream, Z_PARTIAL_FLUSH);
        if (state == Z_STREAM_END)
        {
            break; /* XXX */
        }
        if (state == Z_DATA_ERROR)
        {
            TIFFErrorExtR(tif, module,
                          "Decoding error at scanline %" PRIu32 ", %s",
                          tif->tif_dir.td_row,
                          sp->stream.msg ? sp->stream.msg : "(null)");
            memset(op, 0, (size_t)occ);
            return (0);
        }
        if (state != Z_OK)
        {
            TIFFErrorExtR(tif, module, "ZLib error: %s",
                          sp->stream.msg ? sp->stream.msg : "(null)");
            memset(op, 0, (size_t)occ);
            return (0);
        }
    } while (sp->stream.avail_out > 0);

    /* hopefully, we got all the bytes we needed */
    if (sp->stream.avail_out != 0)
    {
        TIFFErrorExtR(tif, module,
                      "Not enough data at scanline %" PRIu32
                      " (short %u bytes)",
                      tif->tif_dir.td_row, sp->stream.avail_out);
        memset(op, 0, (size_t)occ);
        return (0);
    }

    tif->tif_rawcp = sp->stream.next_in;
    tif->tif_rawcc = sp->stream.avail_in;

    up = sp->tbuf;
    /* Swap bytes in the data if from a different endian machine. */
    if (tif->tif_flags & TIFF_SWAB)
        TIFFSwabArrayOfShort(up, nsamples);

    /*
     * if llen is not an exact multiple of nsamples, the decode operation
     * may overflow the output buffer, so truncate it enough to prevent
     * that but still salvage as much data as possible.
     */
    if (nsamples % llen)
    {
        TIFFWarningExtR(tif, module,
                        "stride %" TIFF_SSIZE_FORMAT
                        " is not a multiple of sample count, "
                        "%" TIFF_SSIZE_FORMAT ", data truncated.",
                        llen, nsamples);
        nsamples -= nsamples % llen;
    }

    for (i = 0; i < nsamples; i += llen, up += llen)
    {
        switch (sp->user_datafmt)
        {
            case PIXARLOGDATAFMT_FLOAT:
                horizontalAccumulateF(up, llen, sp->stride, op, sp->ToLinearF);
                op += (unsigned long)llen * sizeof(float);
                break;
            case PIXARLOGDATAFMT_16BIT:
                horizontalAccumulate16(up, llen, sp->stride, op,
                                       sp->ToLinear16);
                op += (unsigned long)llen * sizeof(uint16_t);
                break;
            case PIXARLOGDATAFMT_12BITPICIO:
                horizontalAccumulate12(up, llen, sp->stride, op, sp->ToLinearF);
                op += (unsigned long)llen * sizeof(int16_t);
                break;
            case PIXARLOGDATAFMT_11BITLOG:
                horizontalAccumulate11(up, llen, sp->stride, op);
                op += (unsigned long)llen * sizeof(uint16_t);
                break;
            case PIXARLOGDATAFMT_8BIT:
                horizontalAccumulate8(up, llen, sp->stride, (unsigned char *)op,
                                      sp->ToLinear8);
                op += (unsigned long)llen * sizeof(unsigned char);
                break;
            case PIXARLOGDATAFMT_8BITABGR:
                horizontalAccumulate8abgr(up, llen, sp->stride,
                                          (unsigned char *)op, sp->ToLinear8);

                /* For stride == 3 (RGB), horizontalAccumulate8abgr expands to 4
                 * bytes/pixel (ABGR) */
                if (sp->stride == 3)
                    op += (unsigned long)row_width * 4;
                else
                    op += (unsigned long)llen * sizeof(unsigned char);
                break;
            default:
                TIFFErrorExtR(tif, module, "Unsupported bits/sample: %" PRIu16,
                              td->td_bitspersample);
                memset(op, 0, (size_t)occ);
                return (0);
        }
    }

    /* A partial final row was not reconstructed. Do not expose its old data. */
    if (op - op_start < occ)
        memset(op, 0, (size_t)(occ - (op - op_start)));

    return (1);
}

/**
 * @brief Allocate PixarLog code staging and initialise zlib deflation.
 * @ingroup libtiff_private_pixarlog
 *
 * Requires fresh encoder state, valid positive stride, ImageWidth and
 * RowsPerStrip; sizing uses strip geometry even for a tiled handle.
 * Allocates a full-strip uint16_t buffer, guesses user format and calls
 * deflateInit at cached quality. Failure can retain staging and modified
 * state for cleanup; repeated setup can replace an unreleased buffer. Some
 * size/allocation failures are silent. No pixel I/O or caller ownership
 * transfer.
 *
 * @param[in,out] tif Non-NULL live PixarLog handle with valid
 * state/geometry; serialise handle operations.
 * @return One after setup, zero on extent/allocation/format/dependency
 * failure.
 */
static int PixarLogSetupEncode(TIFF *tif)
{
    static const char module[] = "PixarLogSetupEncode";
    TIFFDirectory *td = &tif->tif_dir;
    PixarLogState *sp = PixarLogEncoderState(tif);
    tmsize_t tbuf_size;

    assert(sp != NULL);

    /* for some reason, we can't do this in TIFFInitPixarLog */

    sp->stride =
        (td->td_planarconfig == PLANARCONFIG_CONTIG ? td->td_samplesperpixel
                                                    : 1);
    tbuf_size =
        multiply_ms(multiply_ms(multiply_ms(sp->stride, td->td_imagewidth),
                                td->td_rowsperstrip),
                    sizeof(uint16_t));
    if (tbuf_size == 0)
        return (0); /* TODO: this is an error return without error report
                       through TIFFErrorExt */
    sp->tbuf = (uint16_t *)_TIFFmallocExt(tif, tbuf_size);
    if (sp->tbuf == NULL)
        return (0);
    if (sp->user_datafmt == PIXARLOGDATAFMT_UNKNOWN)
        sp->user_datafmt = PixarLogGuessDataFmt(td);
    if (sp->user_datafmt == PIXARLOGDATAFMT_UNKNOWN)
    {
        TIFFErrorExtR(tif, module,
                      "PixarLog compression can't handle %" PRIu16
                      " bit linear encodings",
                      td->td_bitspersample);
        return (0);
    }

    if (deflateInit(&sp->stream, sp->quality) != Z_OK)
    {
        TIFFErrorExtR(tif, module, "%s",
                      sp->stream.msg ? sp->stream.msg : "(null)");
        return (0);
    }
    else
    {
        sp->state |= PLSTATE_INIT;
        return (1);
    }
}

/*
 * Reset encoding state at the start of a strip.
 */
/**
 * @brief Reset PixarLog deflation with the current raw output buffer.
 * @ingroup libtiff_private_pixarlog
 *
 * Requires successful encoder setup and positive usable raw capacity.
 * Attaches raw output, checks uInt round-trip conversion and calls
 * deflateReset. No payload flush or ownership transfer; dependency state
 * can change on reset failure.
 *
 * @param[in,out] tif Non-NULL live PixarLog handle with valid
 * state/geometry; serialise handle operations.
 * @param[in] s Unused sample-plane argument.
 * @return One on reset success, zero on size/reset rejection.
 */
static int PixarLogPreEncode(TIFF *tif, uint16_t s)
{
    static const char module[] = "PixarLogPreEncode";
    PixarLogState *sp = PixarLogEncoderState(tif);

    (void)s;
    assert(sp != NULL);
    sp->stream.next_out = tif->tif_rawdata;
    assert(sizeof(sp->stream.avail_out) == 4); /* if this assert gets raised,
         we need to simplify this code to reflect a ZLib that is likely updated
         to deal with 8byte memory sizes, though this code will respond
         appropriately even before we simplify it */
    sp->stream.avail_out = (uInt)tif->tif_rawdatasize;
    if ((tmsize_t)sp->stream.avail_out != tif->tif_rawdatasize)
    {
        TIFFErrorExtR(tif, module, "ZLib cannot deal with buffers this size");
        return (0);
    }
    return (deflateReset(&sp->stream) == Z_OK);
}

/**
 * @brief Compand finite native float32 samples and difference eleven-bit codes.
 * @ingroup libtiff_private_pixarlog
 *
 * Requires positive stride and n complete interleaved samples, disjoint
 * input/code storage and initialised mapping coefficients/tables. Keeps
 * first-pixel codes then stores horizontal code differences modulo 2048.
 * Float negatives clamp to zero, values above 24.2 cap at 2047 and
 * intermediate values use linear/log mapping; NaNs are not safe for casts.
 * Reads global coefficients that table setup writes. Native memcpy load
 * helpers allow unaligned public wide input; code output remains uint16_t
 * aligned. No allocation, I/O, diagnostics, retained pointers, ownership
 * transfer or status result.
 *
 * @param[in] ip Borrowed complete n-sample input, finite when floating
 * point, native byte order.
 * @param[in] n Nonnegative sample count divisible by positive stride;
 * buffers must cover complete rows.
 * @param[in] stride Positive samples per interleaved pixel, or one for
 * separate data.
 * @param[out] wp Aligned n-entry uint16_t code output.
 * @param[in] FromLT2 Borrowed initialised low-range float companding table
 * matching global coefficients.
 */
static void horizontalDifferenceF(const uint8_t *ip, tmsize_t n, int stride,
                                  uint16_t *wp, uint16_t *FromLT2)
{
    int32_t r1, g1, b1, a1, r2, g2, b2, a2, mask;
    float fltsize = Fltsize;

#define CLAMP(v)                                                               \
    ((v < (float)0.)   ? 0                                                     \
     : (v < (float)2.) ? FromLT2[(int)(v * fltsize)]                           \
     : (v > (float)24.2)                                                       \
         ? 2047                                                                \
         : (double)LogK1 * log((double)v * (double)LogK2) + 0.5)

    mask = CODE_MASK;
    if (n >= stride)
    {
        if (stride == 3)
        {
            r2 = wp[0] = (uint16_t)CLAMP(PixarLogLoadFloatNativeUnaligned(ip));
            g2 = wp[1] = (uint16_t)CLAMP(
                PixarLogLoadFloatNativeUnaligned(ip + sizeof(float)));
            b2 = wp[2] = (uint16_t)CLAMP(
                PixarLogLoadFloatNativeUnaligned(ip + 2 * sizeof(float)));
            n -= 3;
            while (n > 0)
            {
                n -= 3;
                wp += 3;
                ip += 3 * sizeof(float);
                r1 = (int32_t)CLAMP(PixarLogLoadFloatNativeUnaligned(ip));
                wp[0] = (uint16_t)((r1 - r2) & mask);
                r2 = r1;
                g1 = (int32_t)CLAMP(
                    PixarLogLoadFloatNativeUnaligned(ip + sizeof(float)));
                wp[1] = (uint16_t)((g1 - g2) & mask);
                g2 = g1;
                b1 = (int32_t)CLAMP(
                    PixarLogLoadFloatNativeUnaligned(ip + 2 * sizeof(float)));
                wp[2] = (uint16_t)((b1 - b2) & mask);
                b2 = b1;
            }
        }
        else if (stride == 4)
        {
            r2 = wp[0] = (uint16_t)CLAMP(PixarLogLoadFloatNativeUnaligned(ip));
            g2 = wp[1] = (uint16_t)CLAMP(
                PixarLogLoadFloatNativeUnaligned(ip + sizeof(float)));
            b2 = wp[2] = (uint16_t)CLAMP(
                PixarLogLoadFloatNativeUnaligned(ip + 2 * sizeof(float)));
            a2 = wp[3] = (uint16_t)CLAMP(
                PixarLogLoadFloatNativeUnaligned(ip + 3 * sizeof(float)));
            n -= 4;
            while (n > 0)
            {
                n -= 4;
                wp += 4;
                ip += 4 * sizeof(float);
                r1 = (int32_t)CLAMP(PixarLogLoadFloatNativeUnaligned(ip));
                wp[0] = (uint16_t)((r1 - r2) & mask);
                r2 = r1;
                g1 = (int32_t)CLAMP(
                    PixarLogLoadFloatNativeUnaligned(ip + sizeof(float)));
                wp[1] = (uint16_t)((g1 - g2) & mask);
                g2 = g1;
                b1 = (int32_t)CLAMP(
                    PixarLogLoadFloatNativeUnaligned(ip + 2 * sizeof(float)));
                wp[2] = (uint16_t)((b1 - b2) & mask);
                b2 = b1;
                a1 = (int32_t)CLAMP(
                    PixarLogLoadFloatNativeUnaligned(ip + 3 * sizeof(float)));
                wp[3] = (uint16_t)((a1 - a2) & mask);
                a2 = a1;
            }
        }
        else
        {
            REPEAT(stride, wp[0] = (uint16_t)CLAMP(
                               PixarLogLoadFloatNativeUnaligned(ip));
                   wp++; ip += sizeof(float))
            n -= stride;
            while (n > 0)
            {
                REPEAT(
                    stride,
                    wp[0] =
                        (uint16_t)(((int32_t)CLAMP(
                                        PixarLogLoadFloatNativeUnaligned(ip)) -
                                    (int32_t)CLAMP(
                                        PixarLogLoadFloatNativeUnaligned(
                                            ip -
                                            (tmsize_t)stride *
                                                (tmsize_t)sizeof(float)))) &
                                   mask);
                    wp++; ip += sizeof(float))
                n -= stride;
            }
        }
    }
}

/**
 * @brief Compand native unsigned sixteen-bit samples and difference eleven-bit
 * codes.
 * @ingroup libtiff_private_pixarlog
 *
 * Requires positive stride and n complete interleaved samples, disjoint
 * input/code storage and an initialised From14 table. Keeps first-pixel
 * codes then stores horizontal code differences modulo 2048. Sixteen-bit
 * input is reduced to fourteen significant lookup bits. Native memcpy load
 * helpers allow unaligned input; code output remains uint16_t aligned. Does
 * nothing when n is less than stride. No allocation, I/O, diagnostics,
 * retained pointers, ownership transfer or status result.
 *
 * @param[in] ip Borrowed n native-order uint16_t input samples; unaligned
 * allowed.
 * @param[in] n Nonnegative sample count divisible by positive stride;
 * buffers must cover complete rows.
 * @param[in] stride Positive samples per interleaved pixel, or one for
 * separate data.
 * @param[out] wp Aligned n-entry uint16_t code output.
 * @param[in] From14 Borrowed initialised 16384-entry high-fourteen-bit
 * input table.
 */
static void horizontalDifference16(const uint8_t *ip, tmsize_t n, int stride,
                                   unsigned short *wp, uint16_t *From14)
{
    int r1, g1, b1, a1, r2, g2, b2, a2, mask;

/* assumption is unsigned pixel values */
#undef CLAMP
#define CLAMP(v) From14[(v) >> 2]

    mask = CODE_MASK;
    if (n >= stride)
    {
        if (stride == 3)
        {
            r2 = wp[0] = CLAMP(PixarLogLoad16NativeUnaligned(ip));
            g2 = wp[1] =
                CLAMP(PixarLogLoad16NativeUnaligned(ip + sizeof(uint16_t)));
            b2 = wp[2] =
                CLAMP(PixarLogLoad16NativeUnaligned(ip + 2 * sizeof(uint16_t)));
            n -= 3;
            while (n > 0)
            {
                n -= 3;
                wp += 3;
                ip += 3 * sizeof(uint16_t);
                r1 = CLAMP(PixarLogLoad16NativeUnaligned(ip));
                wp[0] = (uint16_t)((r1 - r2) & mask);
                r2 = r1;
                g1 =
                    CLAMP(PixarLogLoad16NativeUnaligned(ip + sizeof(uint16_t)));
                wp[1] = (uint16_t)((g1 - g2) & mask);
                g2 = g1;
                b1 = CLAMP(
                    PixarLogLoad16NativeUnaligned(ip + 2 * sizeof(uint16_t)));
                wp[2] = (uint16_t)((b1 - b2) & mask);
                b2 = b1;
            }
        }
        else if (stride == 4)
        {
            r2 = wp[0] = CLAMP(PixarLogLoad16NativeUnaligned(ip));
            g2 = wp[1] =
                CLAMP(PixarLogLoad16NativeUnaligned(ip + sizeof(uint16_t)));
            b2 = wp[2] =
                CLAMP(PixarLogLoad16NativeUnaligned(ip + 2 * sizeof(uint16_t)));
            a2 = wp[3] =
                CLAMP(PixarLogLoad16NativeUnaligned(ip + 3 * sizeof(uint16_t)));
            n -= 4;
            while (n > 0)
            {
                n -= 4;
                wp += 4;
                ip += 4 * sizeof(uint16_t);
                r1 = CLAMP(PixarLogLoad16NativeUnaligned(ip));
                wp[0] = (uint16_t)((r1 - r2) & mask);
                r2 = r1;
                g1 =
                    CLAMP(PixarLogLoad16NativeUnaligned(ip + sizeof(uint16_t)));
                wp[1] = (uint16_t)((g1 - g2) & mask);
                g2 = g1;
                b1 = CLAMP(
                    PixarLogLoad16NativeUnaligned(ip + 2 * sizeof(uint16_t)));
                wp[2] = (uint16_t)((b1 - b2) & mask);
                b2 = b1;
                a1 = CLAMP(
                    PixarLogLoad16NativeUnaligned(ip + 3 * sizeof(uint16_t)));
                wp[3] = (uint16_t)((a1 - a2) & mask);
                a2 = a1;
            }
        }
        else
        {
            REPEAT(stride, wp[0] = CLAMP(PixarLogLoad16NativeUnaligned(ip));
                   wp++; ip += sizeof(uint16_t))
            n -= stride;
            while (n > 0)
            {
                REPEAT(
                    stride,
                    wp[0] =
                        (uint16_t)((CLAMP(PixarLogLoad16NativeUnaligned(ip)) -
                                    CLAMP(PixarLogLoad16NativeUnaligned(
                                        ip - (tmsize_t)stride *
                                                 (tmsize_t)sizeof(uint16_t)))) &
                                   mask);
                    wp++; ip += sizeof(uint16_t))
                n -= stride;
            }
        }
    }
}

/**
 * @brief Compand unsigned eight-bit samples and difference eleven-bit codes.
 * @ingroup libtiff_private_pixarlog
 *
 * Requires positive stride and n complete interleaved samples, disjoint
 * input/code storage and an initialised From8 table. Keeps first-pixel codes
 * then stores horizontal code differences modulo 2048. Uses all eight input
 * bits for lookup. Does nothing when n is less than stride. No allocation,
 * I/O, diagnostics, retained pointers, ownership transfer or status result.
 *
 * @param[in] ip Borrowed n input bytes.
 * @param[in] n Nonnegative sample count divisible by positive stride;
 * buffers must cover complete rows.
 * @param[in] stride Positive samples per interleaved pixel, or one for
 * separate data.
 * @param[out] wp Aligned n-entry uint16_t code output.
 * @param[in] From8 Borrowed initialised 256-entry input table.
 */
static void horizontalDifference8(unsigned char *ip, tmsize_t n, int stride,
                                  unsigned short *wp, uint16_t *From8)
{
    int r1, g1, b1, a1, r2, g2, b2, a2, mask;

#undef CLAMP
#define CLAMP(v) (From8[(v)])

    mask = CODE_MASK;
    if (n >= stride)
    {
        if (stride == 3)
        {
            r2 = wp[0] = CLAMP(ip[0]);
            g2 = wp[1] = CLAMP(ip[1]);
            b2 = wp[2] = CLAMP(ip[2]);
            n -= 3;
            while (n > 0)
            {
                n -= 3;
                r1 = CLAMP(ip[3]);
                wp[3] = (uint16_t)((r1 - r2) & mask);
                r2 = r1;
                g1 = CLAMP(ip[4]);
                wp[4] = (uint16_t)((g1 - g2) & mask);
                g2 = g1;
                b1 = CLAMP(ip[5]);
                wp[5] = (uint16_t)((b1 - b2) & mask);
                b2 = b1;
                wp += 3;
                ip += 3;
            }
        }
        else if (stride == 4)
        {
            r2 = wp[0] = CLAMP(ip[0]);
            g2 = wp[1] = CLAMP(ip[1]);
            b2 = wp[2] = CLAMP(ip[2]);
            a2 = wp[3] = CLAMP(ip[3]);
            n -= 4;
            while (n > 0)
            {
                n -= 4;
                r1 = CLAMP(ip[4]);
                wp[4] = (uint16_t)((r1 - r2) & mask);
                r2 = r1;
                g1 = CLAMP(ip[5]);
                wp[5] = (uint16_t)((g1 - g2) & mask);
                g2 = g1;
                b1 = CLAMP(ip[6]);
                wp[6] = (uint16_t)((b1 - b2) & mask);
                b2 = b1;
                a1 = CLAMP(ip[7]);
                wp[7] = (uint16_t)((a1 - a2) & mask);
                a2 = a1;
                wp += 4;
                ip += 4;
            }
        }
        else
        {
            REPEAT(stride, wp[0] = CLAMP(ip[0]); wp++; ip++)
            n -= stride;
            while (n > 0)
            {
                REPEAT(stride,
                       wp[0] = (uint16_t)((CLAMP(ip[0]) - CLAMP(ip[-stride])) &
                                          mask);
                       wp++; ip++)
                n -= stride;
            }
        }
    }
}

/*
 * Encode a chunk of pixels.
 */
/**
 * @brief Compand and difference complete input rows then deflate their codes.
 * @ingroup libtiff_private_pixarlog
 *
 * Requires successful setup/preencode, nonnegative complete ImageWidth-based
 * row input, and representable sample counts. Explicitly bounds total samples
 * by RowsPerStrip*row length but does not independently reject a partial final
 * row before full-row conversion. Only float, sixteen-bit unsigned and
 * eight-bit conversion cases are implemented. An unknown format is rejected
 * immediately; PIXARLOGDATAFMT_8BITABGR, 11BITLOG and 12BITPICIO are diagnosed
 * and rejected when the input holds at least one sample, while shorter input
 * with them skips conversion and calls deflate with no new input. Writes owned
 * tbuf, feeds bounded zlib input and flushes full raw buffers. Caller input
 * remains unchanged; float samples must be finite for companding casts.
 * Dependency/I/O failure can leave partial cache/file state; no rollback or
 * ownership transfer.
 *
 * @param[in,out] tif Non-NULL live PixarLog handle with valid
 * state/geometry; serialise handle operations. The float path reads the
 * process-global Fltsize/LogK coefficients written by table initialisation.
 * @param[in] bp Borrowed readable cc-byte user input, disjoint from codec
 * storage.
 * @param[in] cc Nonnegative complete-row input extent in the selected user
 * format.
 * @param[in] s Unused sample-plane argument.
 * @return One after input accepted, zero on detected
 * format/count/dependency/flush failure.
 */
static int PixarLogEncode(TIFF *tif, uint8_t *bp, tmsize_t cc, uint16_t s)
{
    static const char module[] = "PixarLogEncode";
    TIFFDirectory *td = &tif->tif_dir;
    PixarLogState *sp = PixarLogEncoderState(tif);
    tmsize_t i;
    tmsize_t n;
    tmsize_t llen;
    unsigned short *up;

    (void)s;

    switch (sp->user_datafmt)
    {
        case PIXARLOGDATAFMT_FLOAT:
            n = (tmsize_t)((unsigned long)cc /
                           sizeof(float)); /* XXX float == 32 bits */
            break;
        case PIXARLOGDATAFMT_16BIT:
        case PIXARLOGDATAFMT_12BITPICIO:
        case PIXARLOGDATAFMT_11BITLOG:
            n = (tmsize_t)((unsigned long)cc /
                           sizeof(uint16_t)); /* XXX uint16_t == 16 bits */
            break;
        case PIXARLOGDATAFMT_8BIT:
        case PIXARLOGDATAFMT_8BITABGR:
            n = cc;
            break;
        default:
            TIFFErrorExtR(tif, module,
                          "%" PRIu16 " bit input not supported in PixarLog",
                          td->td_bitspersample);
            return 0;
    }

    /* stride (≤ td_samplesperpixel, max 65535) × imagewidth: fits tmsize_t */
    llen = (tmsize_t)sp->stride * td->td_imagewidth;
    /* Check against the number of elements (of size uint16_t) of sp->tbuf */
    tmsize_t max_n =
        _TIFFMultiplySSize(tif, (tmsize_t)td->td_rowsperstrip, llen, module);
    if (max_n == 0 || n > max_n)
    {
        TIFFErrorExtR(tif, module, "Too many input bytes provided");
        return 0;
    }

    for (i = 0, up = sp->tbuf; i < n; i += llen, up += llen)
    {
        switch (sp->user_datafmt)
        {
            case PIXARLOGDATAFMT_FLOAT:
                horizontalDifferenceF(bp, llen, sp->stride, up, sp->FromLT2);
                bp += (unsigned long)llen * sizeof(float);
                break;
            case PIXARLOGDATAFMT_16BIT:
                horizontalDifference16(bp, llen, sp->stride, up, sp->From14);
                bp += (unsigned long)llen * sizeof(uint16_t);
                break;
            case PIXARLOGDATAFMT_8BIT:
                horizontalDifference8((unsigned char *)bp, llen, sp->stride, up,
                                      sp->From8);
                bp += (unsigned long)llen * sizeof(unsigned char);
                break;
            default:
                TIFFErrorExtR(tif, module,
                              "%" PRIu16 " bit input not supported in PixarLog",
                              td->td_bitspersample);
                return 0;
        }
    }

    sp->stream.next_in = (unsigned char *)sp->tbuf;
    assert(sizeof(sp->stream.avail_in) == 4); /* if this assert gets raised,
         we need to simplify this code to reflect a ZLib that is likely updated
         to deal with 8byte memory sizes, though this code will respond
         appropriately even before we simplify it */
    sp->stream.avail_in = (uInt)((unsigned long)n * sizeof(uint16_t));
    if ((sp->stream.avail_in / sizeof(uint16_t)) != (unsigned long)n)
    {
        TIFFErrorExtR(tif, module, "ZLib cannot deal with buffers this size");
        return (0);
    }

    do
    {
        if (deflate(&sp->stream, Z_NO_FLUSH) != Z_OK)
        {
            TIFFErrorExtR(tif, module, "Encoder error: %s",
                          sp->stream.msg ? sp->stream.msg : "(null)");
            return (0);
        }
        if (sp->stream.avail_out == 0)
        {
            tif->tif_rawcc = tif->tif_rawdatasize;
            if (!TIFFFlushData1(tif))
                return 0;
            sp->stream.next_out = tif->tif_rawdata;
            sp->stream.avail_out =
                (uInt)tif
                    ->tif_rawdatasize; /* this is a safe typecast, as check is
                                          made already in PixarLogPreEncode */
        }
    } while (sp->stream.avail_in > 0);
    return (1);
}

/*
 * Finish off an encoded strip by flushing the last
 * string and tacking on an End Of Information code.
 */

/**
 * @brief Finish PixarLog deflation and flush encoded output.
 * @ingroup libtiff_private_pixarlog
 *
 * Requires successful preencode, valid raw output and code state. Runs
 * Z_FINISH until STREAM_END, flushing nonempty extents. Diagnostics report
 * dependency errors; output/file progress can remain on failure. Does not
 * persist IFD or promise durability; no ownership transfer.
 *
 * @param[in,out] tif Non-NULL live PixarLog handle with valid
 * state/geometry; serialise handle operations.
 * @return One on stream completion, zero on dependency/flush failure.
 */
static int PixarLogPostEncode(TIFF *tif)
{
    static const char module[] = "PixarLogPostEncode";
    PixarLogState *sp = PixarLogEncoderState(tif);
    int state;

    sp->stream.avail_in = 0;

    do
    {
        state = deflate(&sp->stream, Z_FINISH);
        switch (state)
        {
            case Z_STREAM_END:
            case Z_OK:
                if ((tmsize_t)sp->stream.avail_out != tif->tif_rawdatasize)
                {
                    tif->tif_rawcc =
                        tif->tif_rawdatasize - sp->stream.avail_out;
                    if (!TIFFFlushData1(tif))
                        return 0;
                    sp->stream.next_out = tif->tif_rawdata;
                    sp->stream.avail_out =
                        (uInt)tif->tif_rawdatasize; /* this is a safe typecast,
                                                       as check is made already
                                                       in PixarLogPreEncode */
                }
                break;
            default:
                TIFFErrorExtR(tif, module, "ZLib error: %s",
                              sp->stream.msg ? sp->stream.msg : "(null)");
                return (0);
        }
    } while (state != Z_STREAM_END);
    return (1);
}

/**
 * @brief Present initialised PixarLog directory metadata as unsigned eight-bit
 * samples.
 * @ingroup libtiff_private_pixarlog
 *
 * When PLSTATE_INIT is set, directly changes BitsPerSample to eight and
 * SampleFormat to unsigned before lifecycle directory writing. This changes
 * the advertised format regardless of application input representation.
 * Does not free state, emit/flush payload, close the handle or return
 * status; no ownership transfer.
 *
 * @param[in,out] tif Non-NULL live PixarLog handle with valid
 * state/geometry; serialise handle operations.
 */
static void PixarLogClose(TIFF *tif)
{
    PixarLogState *sp = (PixarLogState *)tif->tif_data;
    TIFFDirectory *td = &tif->tif_dir;

    assert(sp != 0);
    /* In a really sneaky (and really incorrect, and untruthful, and
     * troublesome, and error-prone) maneuver that completely goes against
     * the spirit of TIFF, and breaks TIFF, on close, we covertly
     * modify both bitspersample and sampleformat in the directory to
     * indicate 8-bit linear.  This way, the decode "just works" even for
     * readers that don't know about PixarLog, or how to set
     * the PIXARLOGDATFMT pseudo-tag.
     */

    if (sp->state & PLSTATE_INIT)
    {
        /* We test the state to avoid an issue such as in
         * http://bugzilla.maptools.org/show_bug.cgi?id=2604
         * What appends in that case is that the bitspersample is 1 and
         * a TransferFunction is set. The size of the TransferFunction
         * depends on 1<<bitspersample. So if we increase it, an access
         * out of the buffer will happen at directory flushing.
         * Another option would be to clear those targs.
         */
        td->td_bitspersample = 8;
        td->td_sampleformat = SAMPLEFORMAT_UINT;
    }
}

/**
 * @brief Restore PixarLog method chains and release companding storage.
 * @ingroup libtiff_private_pixarlog
 *
 * Restores predictor and saved tag hooks, frees all six tables, ends zlib
 * in the mode inferred from the handle open mode, frees tbuf and outer Ext
 * state, clears tif_data and installs default methods. Invalidates all
 * codec-owned views. Does not finish/flush pending payload or close the
 * handle; no status result.
 *
 * @param[in,out] tif Non-NULL live PixarLog handle with valid
 * state/geometry; serialise handle operations.
 */
static void PixarLogCleanup(TIFF *tif)
{
    PixarLogState *sp = (PixarLogState *)tif->tif_data;

    assert(sp != 0);

    (void)TIFFPredictorCleanup(tif);

    tif->tif_tagmethods.vgetfield = sp->vgetparent;
    tif->tif_tagmethods.vsetfield = sp->vsetparent;

    if (sp->FromLT2)
        _TIFFfreeExt(tif, sp->FromLT2);
    if (sp->From14)
        _TIFFfreeExt(tif, sp->From14);
    if (sp->From8)
        _TIFFfreeExt(tif, sp->From8);
    if (sp->ToLinearF)
        _TIFFfreeExt(tif, sp->ToLinearF);
    if (sp->ToLinear16)
        _TIFFfreeExt(tif, sp->ToLinear16);
    if (sp->ToLinear8)
        _TIFFfreeExt(tif, sp->ToLinear8);
    if (sp->state & PLSTATE_INIT)
    {
        if (tif->tif_mode == O_RDONLY)
            inflateEnd(&sp->stream);
        else
            deflateEnd(&sp->stream);
    }
    if (sp->tbuf)
        _TIFFfreeExt(tif, sp->tbuf);
    _TIFFfreeExt(tif, sp);
    tif->tif_data = NULL;

    _TIFFSetDefaultCompressionState(tif);
}

/**
 * @brief Set PixarLog quality or user data format and update directory sizes.
 * @ingroup libtiff_private_pixarlog
 *
 * Both pseudo-tags consume int and cache it without comprehensive
 * validation. QUALITY can call deflateParams on an initialised writer;
 * failure diagnoses after storing the value. DATAFMT changes depth/sample
 * format through nested TIFFSetField calls whose statuses are ignored, then
 * recomputes tile/scanline sizes and returns success even for unrecognised
 * values. Changes apply beyond private state and can dirty tags/invoke
 * callbacks; no rollback or ownership transfer. Other tags follow parent
 * variadic contracts.
 *
 * @param[in,out] tif Non-NULL live PixarLog handle with valid
 * state/geometry; serialise handle operations.
 * @param[in] tag Pseudo-tag or delegated identifier.
 * @param[in,out] ap Live tag-specific va_list, consumed synchronously
 * without retention.
 * @return One for accepted pseudo-tag handling, zero on deflateParams
 * failure, or delegated setter status.
 */
static int PixarLogVSetField(TIFF *tif, uint32_t tag, va_list ap)
{
    static const char module[] = "PixarLogVSetField";
    PixarLogState *sp = (PixarLogState *)tif->tif_data;
    int result;

    switch (tag)
    {
        case TIFFTAG_PIXARLOGQUALITY:
            sp->quality = (int)va_arg(ap, int);
            if (tif->tif_mode != O_RDONLY && (sp->state & PLSTATE_INIT))
            {
                if (deflateParams(&sp->stream, sp->quality,
                                  Z_DEFAULT_STRATEGY) != Z_OK)
                {
                    TIFFErrorExtR(tif, module, "ZLib error: %s",
                                  sp->stream.msg ? sp->stream.msg : "(null)");
                    return (0);
                }
            }
            return (1);
        case TIFFTAG_PIXARLOGDATAFMT:
            sp->user_datafmt = (int)va_arg(ap, int);
            /* Tweak the TIFF header so that the rest of libtiff knows what
             * size of data will be passed between app and library, and
             * assume that the app knows what it is doing and is not
             * confused by these header manipulations...
             */
            switch (sp->user_datafmt)
            {
                case PIXARLOGDATAFMT_8BIT:
                case PIXARLOGDATAFMT_8BITABGR:
                    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
                    TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);
                    break;
                case PIXARLOGDATAFMT_11BITLOG:
                    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 16);
                    TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);
                    break;
                case PIXARLOGDATAFMT_12BITPICIO:
                    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 16);
                    TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_INT);
                    break;
                case PIXARLOGDATAFMT_16BIT:
                    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 16);
                    TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);
                    break;
                case PIXARLOGDATAFMT_FLOAT:
                    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 32);
                    TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT,
                                 SAMPLEFORMAT_IEEEFP);
                    break;
                default:
                    break;
            }
            /*
             * Must recalculate sizes should bits/sample change.
             */
            tif->tif_dir.td_tilesize =
                isTiled(tif) ? TIFFTileSize(tif) : (tmsize_t)(-1);
            tif->tif_dir.td_scanlinesize = TIFFScanlineSize(tif);
            result = 1; /* NB: pseudo tag */
            break;
        default:
            result = (*sp->vsetparent)(tif, tag, ap);
    }
    return (result);
}

/**
 * @brief Retrieve PixarLog quality or format or delegate another query.
 * @ingroup libtiff_private_pixarlog
 *
 * Pseudo-tags consume non-NULL int* outputs for cached values. Other tags
 * follow saved parent contracts. No output allocation or ownership transfer
 * for the scalar settings.
 *
 * @param[in,out] tif Non-NULL live PixarLog handle with valid
 * state/geometry; serialise handle operations.
 * @param[in] tag Pseudo-tag or delegated identifier.
 * @param[in,out] ap Live tag-specific va_list, consumed synchronously
 * without retention.
 * @return One for codec retrieval, otherwise parent getter status.
 */
static int PixarLogVGetField(TIFF *tif, uint32_t tag, va_list ap)
{
    PixarLogState *sp = (PixarLogState *)tif->tif_data;

    switch (tag)
    {
        case TIFFTAG_PIXARLOGQUALITY:
            *va_arg(ap, int *) = sp->quality;
            break;
        case TIFFTAG_PIXARLOGDATAFMT:
            *va_arg(ap, int *) = sp->user_datafmt;
            break;
        default:
            return (*sp->vgetparent)(tif, tag, ap);
    }
    return (1);
}

/**
 * @brief Define PixarLog format and quality pseudo-tags.
 * @ingroup libtiff_private_pixarlog
 *
 * Immutable library-lifetime metadata merged into a handle; no caller
 * ownership transfer.
 */
static const TIFFField pixarlogFields[] = {
    {TIFFTAG_PIXARLOGDATAFMT, 0, 0, TIFF_ANY, 0, TIFF_SETGET_INT, FIELD_PSEUDO,
     FALSE, FALSE, "", NULL},
    {TIFFTAG_PIXARLOGQUALITY, 0, 0, TIFF_ANY, 0, TIFF_SETGET_INT, FIELD_PSEUDO,
     FALSE, FALSE, "", NULL}};

/**
 * @brief Report a conservative PixarLog expansion allocation heuristic.
 * @ingroup libtiff_private_pixarlog
 *
 * Ignores the handle; no allocation, I/O, diagnostics, state or ownership
 * effects. Not a universal bound or successful-decode guarantee.
 *
 * @param[in] tif Unused PixarLog handle.
 * @return 4128, four times the Deflate ratio estimate.
 */
static uint64_t PixarLogGetMaxCompressionRatio(TIFF *tif)
{
    (void)tif;
    /* cf https://zlib.net/zlib_tech.html */
    const uint64_t MAX_DEFLATE_RATIO = 1032;

    /* security margin as I don't understand what this codec does */
    return MAX_DEFLATE_RATIO * (uint64_t)4;
}

int TIFFInitPixarLog(TIFF *tif, int scheme)
{
    static const char module[] = "TIFFInitPixarLog";

    PixarLogState *sp;

    (void)scheme;
    assert(scheme == COMPRESSION_PIXARLOG);

    /*
     * Merge codec-specific tag information.
     */
    if (!_TIFFMergeFields(tif, pixarlogFields, TIFFArrayCount(pixarlogFields)))
    {
        TIFFErrorExtR(tif, module,
                      "Merging PixarLog codec-specific tags failed");
        return 0;
    }

    /*
     * Allocate state block so tag methods have storage to record values.
     */
    tif->tif_data = (uint8_t *)_TIFFmallocExt(tif, sizeof(PixarLogState));
    if (tif->tif_data == NULL)
        goto bad;
    sp = (PixarLogState *)tif->tif_data;
    _TIFFmemset(sp, 0, sizeof(*sp));
    sp->stream.data_type = Z_BINARY;
    sp->user_datafmt = PIXARLOGDATAFMT_UNKNOWN;

    /*
     * Install codec methods.
     */
    tif->tif_fixuptags = PixarLogFixupTags;
    tif->tif_setupdecode = PixarLogSetupDecode;
    tif->tif_predecode = PixarLogPreDecode;
    tif->tif_decoderow = PixarLogDecode;
    tif->tif_decodestrip = PixarLogDecode;
    tif->tif_decodetile = PixarLogDecode;
    tif->tif_setupencode = PixarLogSetupEncode;
    tif->tif_preencode = PixarLogPreEncode;
    tif->tif_postencode = PixarLogPostEncode;
    tif->tif_encoderow = PixarLogEncode;
    tif->tif_encodestrip = PixarLogEncode;
    tif->tif_encodetile = PixarLogEncode;
    tif->tif_close = PixarLogClose;
    tif->tif_cleanup = PixarLogCleanup;
    tif->tif_getmaxcompressionratio = PixarLogGetMaxCompressionRatio;

    /* Override SetField so we can handle our private pseudo-tag */
    sp->vgetparent = tif->tif_tagmethods.vgetfield;
    tif->tif_tagmethods.vgetfield = PixarLogVGetField; /* hook for codec tags */
    sp->vsetparent = tif->tif_tagmethods.vsetfield;
    tif->tif_tagmethods.vsetfield = PixarLogVSetField; /* hook for codec tags */

    /* Default values for codec-specific fields */
    sp->quality = Z_DEFAULT_COMPRESSION; /* default comp. level */
    sp->state = 0;

    /* we don't wish to use the predictor,
     * the default is none, which predictor value 1
     */
    (void)TIFFPredictorInit(tif);

    /*
     * build the companding tables
     */
    if (!PixarLogMakeTables(tif, sp))
    {
        TIFFErrorExtR(tif, module,
                      "Failed to allocate memory for PixarLog tables");
        return (0);
    }

    return (1);
bad:
    TIFFErrorExtR(tif, module, "No space for PixarLog state block");
    return (0);
}
#endif /* PIXARLOG_SUPPORT */

/**
 * @def TSIZE
 * @brief Count the 2048 eleven-bit code values.
 * @ingroup libtiff_private_pixarlog
 *
 * Value 2048: the number of 11-bit code values. PixarLogMakeTables fills
 * ToLinearF for codes 0 to TSIZE - 1.
 */

/**
 * @def TSIZEP1
 * @brief Include one repeated endpoint in decode tables.
 * @ingroup libtiff_private_pixarlog
 *
 * Value 2049 (TSIZE plus one): the size of the ToLinearF, ToLinear16 and
 * ToLinear8 decode tables, which repeat the last entry so that index TSIZE
 * is valid.
 */

/**
 * @def ONE
 * @brief Select code 1250 for linear value one.
 * @ingroup libtiff_private_pixarlog
 *
 * Value 1250: the code that maps to linear 1.0. PixarLogMakeTables scales
 * the logarithmic region so that ToLinearF[ONE] is 1.0.
 */

/**
 * @def RATIO
 * @brief Choose the nominal log-region step ratio.
 * @ingroup libtiff_private_pixarlog
 *
 * Value 1.004. PixarLogMakeTables derives nlin = (int)(1 / log(RATIO))
 * linear codes, and the logarithmic region then steps by exp(1 / nlin),
 * approximately RATIO, per code.
 */

/**
 * @def CODE_MASK
 * @brief Mask accumulated codes to eleven bits.
 * @ingroup libtiff_private_pixarlog
 *
 * Value 0x7ff. The accumulators and differencers apply it so that horizontal
 * prediction wraps modulo 2048.
 */

/**
 * @def REPEAT(n, op)
 * @brief Repeat a statement n times, and at least once.
 * @ingroup libtiff_private_pixarlog
 *
 * Expands to a block that declares a local int i, evaluates n once into it,
 * and runs op in a do/while loop. op therefore executes max(n, 1) times,
 * even when n <= 0, and must not refer to an outer variable named i.
 *
 * @param[in] n Repeat count, evaluated once.
 * @param[in] op Statement executed max(n, 1) times.
 */

/**
 * @def SCALE12
 * @brief Scale PICIO output by 2048.
 * @ingroup libtiff_private_pixarlog
 *
 * Value 2048.0f: multiplies ToLinearF values into the
 * PIXARLOGDATAFMT_12BITPICIO range, where linear 1.0 maps to 2048.
 */

/**
 * @def CLAMP12(t)
 * @brief Cap positive PICIO output at 3071.
 * @ingroup libtiff_private_pixarlog
 *
 * Evaluates t twice. Values below 3071 are converted to int16_t through
 * uint16_t; larger values become 3071. Used only by horizontalAccumulate12.
 *
 * @param[in] t Scaled float sample value.
 */

/**
 * @def PLSTATE_INIT
 * @brief Mark dependency stream initialised.
 * @ingroup libtiff_private_pixarlog
 *
 * Bit in PixarLogState::state, set once setup has initialised the zlib
 * stream. Checked to avoid repeating setup, to decide whether cleanup ends
 * the stream, and before applying quality changes or the close-time tag
 * rewrite.
 */

/**
 * @def PixarLogDecoderState(tif)
 * @brief Borrow the handle-owned PixarLog decoder state.
 * @ingroup libtiff_private_pixarlog
 *
 * Expands to a cast of tif->tif_data to PixarLogState *, evaluating tif
 * once. The state is not checked; it is valid only after TIFFInitPixarLog.
 *
 * @param[in] tif TIFF handle whose codec state is borrowed.
 */

/**
 * @def PixarLogEncoderState(tif)
 * @brief Borrow the handle-owned PixarLog encoder state.
 * @ingroup libtiff_private_pixarlog
 *
 * Expands to a cast of tif->tif_data to PixarLogState *, evaluating tif
 * once. The state is not checked; it is valid only after TIFFInitPixarLog.
 *
 * @param[in] tif TIFF handle whose codec state is borrowed.
 */

/**
 * @def PIXARLOGDATAFMT_UNKNOWN
 * @brief Mark an application format not yet inferred or selected.
 * @ingroup libtiff_private_pixarlog
 *
 * Value -1 in PixarLogState::user_datafmt. Set by TIFFInitPixarLog and
 * returned by PixarLogGuessDataFmt; setup then guesses the format from the
 * directory and fails with a diagnostic if none matches.
 */

/**
 * @def CLAMP(v)
 * @brief Map one float sixteen-bit or byte input to a companded code in its
 * enclosing converter.
 * @ingroup libtiff_private_pixarlog
 *
 * Defined three times as a local helper. In horizontalDifferenceF it maps a
 * float through FromLT2 below 2.0, returns 2047 above 24.2, and otherwise
 * applies the LogK1/LogK2 logarithm; it evaluates v (unparenthesised) up to
 * four times. In horizontalDifference16 it indexes From14 with v >> 2, and
 * in horizontalDifference8 it indexes From8 with v; both evaluate v once.
 *
 * @param[in] v Input sample value.
 */

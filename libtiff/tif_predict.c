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

/*
 * TIFF Library.
 *
 * Predictor Tag Support (used by multiple codecs).
 */
/**
 * @file tif_predict.c
 * @brief Implement integer and floating-point Predictor tag transforms.
 */

#include "tif_predict.h"
#include "tiffiop.h"

#if defined(__x86_64__) || (defined(_M_X64) && !defined(_M_ARM64EC))
#include <emmintrin.h>
#endif

#define PredictorState(tif) ((TIFFPredictorState *)(tif)->tif_data)

static int horAcc8(TIFF *tif, uint8_t *cp0, tmsize_t cc);
static int horAcc16(TIFF *tif, uint8_t *cp0, tmsize_t cc);
static int horAcc32(TIFF *tif, uint8_t *cp0, tmsize_t cc);
static int horAcc64(TIFF *tif, uint8_t *cp0, tmsize_t cc);
static int swabHorAcc16(TIFF *tif, uint8_t *cp0, tmsize_t cc);
static int swabHorAcc32(TIFF *tif, uint8_t *cp0, tmsize_t cc);
static int swabHorAcc64(TIFF *tif, uint8_t *cp0, tmsize_t cc);
static int horDiff8(TIFF *tif, uint8_t *cp0, tmsize_t cc);
static int horDiff16(TIFF *tif, uint8_t *cp0, tmsize_t cc);
static int horDiff32(TIFF *tif, uint8_t *cp0, tmsize_t cc);
static int horDiff64(TIFF *tif, uint8_t *cp0, tmsize_t cc);
static int swabHorDiff16(TIFF *tif, uint8_t *cp0, tmsize_t cc);
static int swabHorDiff32(TIFF *tif, uint8_t *cp0, tmsize_t cc);
static int swabHorDiff64(TIFF *tif, uint8_t *cp0, tmsize_t cc);
static int fpAcc(TIFF *tif, uint8_t *cp0, tmsize_t cc);
static int fpDiff(TIFF *tif, uint8_t *cp0, tmsize_t cc);
static int PredictorDecodeRow(TIFF *tif, uint8_t *op0, tmsize_t occ0,
                              uint16_t s);
static int PredictorDecodeTile(TIFF *tif, uint8_t *op0, tmsize_t occ0,
                               uint16_t s);
static int PredictorEncodeRow(TIFF *tif, uint8_t *bp, tmsize_t cc, uint16_t s);
static int PredictorEncodeTile(TIFF *tif, uint8_t *bp0, tmsize_t cc0,
                               uint16_t s);

/**
 * @brief Validate predictor format and cache stride and row extent.
 * @ingroup libtiff_private_predictor
 *
 * Accepts no predictor, integer horizontal 8/16/32/64-bit samples, or
 * floating-point 16/24/32/64-bit samples. Derives contiguous sample stride
 * or separate stride one and a positive decoded row size. Diagnoses
 * unsupported settings; no pixel I/O or owned result allocation. Cached
 * state may change before failure.
 *
 * @param[in,out] tif Non-NULL live handle whose tif_data begins with a
 * valid TIFFPredictorState; serialise codec operations.
 * @return One for accepted setup, zero on unsupported predictor/format or
 * zero row size.
 */
static int PredictorSetup(TIFF *tif)
{
    static const char module[] = "PredictorSetup";

    TIFFPredictorState *sp = PredictorState(tif);
    TIFFDirectory *td = &tif->tif_dir;

    switch (sp->predictor) /* no differencing */
    {
        case PREDICTOR_NONE:
            return 1;
        case PREDICTOR_HORIZONTAL:
            if (td->td_bitspersample != 8 && td->td_bitspersample != 16 &&
                td->td_bitspersample != 32 && td->td_bitspersample != 64)
            {
                TIFFErrorExtR(tif, module,
                              "Horizontal differencing \"Predictor\" not "
                              "supported with %" PRIu16 "-bit samples",
                              td->td_bitspersample);
                return 0;
            }
            break;
        case PREDICTOR_FLOATINGPOINT:
            if (td->td_sampleformat != SAMPLEFORMAT_IEEEFP)
            {
                TIFFErrorExtR(
                    tif, module,
                    "Floating point \"Predictor\" not supported with %" PRIu16
                    " data format",
                    td->td_sampleformat);
                return 0;
            }
            if (td->td_bitspersample != 16 && td->td_bitspersample != 24 &&
                td->td_bitspersample != 32 && td->td_bitspersample != 64)
            { /* Should 64 be allowed? */
                TIFFErrorExtR(
                    tif, module,
                    "Floating point \"Predictor\" not supported with %" PRIu16
                    "-bit samples",
                    td->td_bitspersample);
                return 0;
            }
            break;
        default:
            TIFFErrorExtR(tif, module, "\"Predictor\" value %d not supported",
                          sp->predictor);
            return 0;
    }
    sp->stride =
        (td->td_planarconfig == PLANARCONFIG_CONTIG ? td->td_samplesperpixel
                                                    : 1);
    /*
     * Calculate the scanline/tile-width size in bytes.
     */
    if (isTiled(tif))
        sp->rowsize = TIFFTileRowSize(tif);
    else
        sp->rowsize = TIFFScanlineSize(tif);
    if (sp->rowsize == 0)
        return 0;

    return 1;
}

/**
 * @brief Chain parent decoder setup and install predictor decoding.
 * @ingroup libtiff_private_predictor
 *
 * Invokes parent setup first without undoing its effects on later failure.
 * Selects accumulation, wraps row/strile callbacks, and disables ordinary
 * postdecode swapping where the predictor handles byte order. Requires
 * fresh/non-recursive parent chains; may allocate through parent setup and
 * diagnostics, but does not read pixels directly.
 *
 * @param[in,out] tif Non-NULL live handle whose tif_data begins with a
 * valid TIFFPredictorState; serialise codec operations.
 * @return One after setup, zero on parent or predictor validation failure.
 */
static int PredictorSetupDecode(TIFF *tif)
{
    TIFFPredictorState *sp = PredictorState(tif);
    TIFFDirectory *td = &tif->tif_dir;

    /* Note: when PredictorSetup() fails, the effets of setupdecode() */
    /* will not be "canceled" so setupdecode() might be robust to */
    /* be called several times. */
    if (!(*sp->setupdecode)(tif) || !PredictorSetup(tif))
        return 0;

    if (sp->predictor == 2)
    {
        switch (td->td_bitspersample)
        {
            case 8:
                sp->decodepfunc = horAcc8;
                break;
            case 16:
                sp->decodepfunc = horAcc16;
                break;
            case 32:
                sp->decodepfunc = horAcc32;
                break;
            case 64:
                sp->decodepfunc = horAcc64;
                break;
            default:
                break;
        }
        /*
         * Override default decoding method with one that does the
         * predictor stuff.
         */
        if (tif->tif_decoderow != PredictorDecodeRow)
        {
            sp->decoderow = tif->tif_decoderow;
            tif->tif_decoderow = PredictorDecodeRow;
            sp->decodestrip = tif->tif_decodestrip;
            tif->tif_decodestrip = PredictorDecodeTile;
            sp->decodetile = tif->tif_decodetile;
            tif->tif_decodetile = PredictorDecodeTile;
        }

        /*
         * If the data is horizontally differenced 16-bit data that
         * requires byte-swapping, then it must be byte swapped before
         * the accumulation step.  We do this with a special-purpose
         * routine and override the normal post decoding logic that
         * the library setup when the directory was read.
         */
        if (tif->tif_flags & TIFF_SWAB)
        {
            if (sp->decodepfunc == horAcc16)
            {
                sp->decodepfunc = swabHorAcc16;
                tif->tif_postdecode = _TIFFNoPostDecode;
            }
            else if (sp->decodepfunc == horAcc32)
            {
                sp->decodepfunc = swabHorAcc32;
                tif->tif_postdecode = _TIFFNoPostDecode;
            }
            else if (sp->decodepfunc == horAcc64)
            {
                sp->decodepfunc = swabHorAcc64;
                tif->tif_postdecode = _TIFFNoPostDecode;
            }
        }
    }

    else if (sp->predictor == 3)
    {
        sp->decodepfunc = fpAcc;
        /*
         * Override default decoding method with one that does the
         * predictor stuff.
         */
        if (tif->tif_decoderow != PredictorDecodeRow)
        {
            sp->decoderow = tif->tif_decoderow;
            tif->tif_decoderow = PredictorDecodeRow;
            sp->decodestrip = tif->tif_decodestrip;
            tif->tif_decodestrip = PredictorDecodeTile;
            sp->decodetile = tif->tif_decodetile;
            tif->tif_decodetile = PredictorDecodeTile;
        }
        /*
         * The data should not be swapped outside of the floating
         * point predictor, the accumulation routine should return
         * bytes in the native order.
         */
        if (tif->tif_flags & TIFF_SWAB)
        {
            tif->tif_postdecode = _TIFFNoPostDecode;
        }
    }

    return 1;
}

/**
 * @brief Chain parent encoder setup and install predictor encoding.
 * @ingroup libtiff_private_predictor
 *
 * Invokes parent setup then validates predictor geometry and selects
 * differencing/wrappers. Can disable ordinary postdecode swapping. Parent
 * and wrapper changes survive failure; parent allocation/diagnostic effects
 * apply. Does not write pixels directly.
 *
 * @param[in,out] tif Non-NULL live handle whose tif_data begins with a
 * valid TIFFPredictorState; serialise codec operations.
 * @return One after setup, zero on parent or predictor validation failure.
 */
static int PredictorSetupEncode(TIFF *tif)
{
    TIFFPredictorState *sp = PredictorState(tif);
    TIFFDirectory *td = &tif->tif_dir;

    if (!(*sp->setupencode)(tif) || !PredictorSetup(tif))
        return 0;

    if (sp->predictor == 2)
    {
        switch (td->td_bitspersample)
        {
            case 8:
                sp->encodepfunc = horDiff8;
                break;
            case 16:
                sp->encodepfunc = horDiff16;
                break;
            case 32:
                sp->encodepfunc = horDiff32;
                break;
            case 64:
                sp->encodepfunc = horDiff64;
                break;
            default:
                break;
        }
        /*
         * Override default encoding method with one that does the
         * predictor stuff.
         */
        if (tif->tif_encoderow != PredictorEncodeRow)
        {
            sp->encoderow = tif->tif_encoderow;
            tif->tif_encoderow = PredictorEncodeRow;
            sp->encodestrip = tif->tif_encodestrip;
            tif->tif_encodestrip = PredictorEncodeTile;
            sp->encodetile = tif->tif_encodetile;
            tif->tif_encodetile = PredictorEncodeTile;
        }

        /*
         * If the data is horizontally differenced 16-bit data that
         * requires byte-swapping, then it must be byte swapped after
         * the differentiation step.  We do this with a special-purpose
         * routine and override the normal post decoding logic that
         * the library setup when the directory was read.
         */
        if (tif->tif_flags & TIFF_SWAB)
        {
            if (sp->encodepfunc == horDiff16)
            {
                sp->encodepfunc = swabHorDiff16;
                tif->tif_postdecode = _TIFFNoPostDecode;
            }
            else if (sp->encodepfunc == horDiff32)
            {
                sp->encodepfunc = swabHorDiff32;
                tif->tif_postdecode = _TIFFNoPostDecode;
            }
            else if (sp->encodepfunc == horDiff64)
            {
                sp->encodepfunc = swabHorDiff64;
                tif->tif_postdecode = _TIFFNoPostDecode;
            }
        }
    }

    else if (sp->predictor == 3)
    {
        sp->encodepfunc = fpDiff;
        /*
         * Override default encoding method with one that does the
         * predictor stuff.
         */
        if (tif->tif_encoderow != PredictorEncodeRow)
        {
            sp->encoderow = tif->tif_encoderow;
            tif->tif_encoderow = PredictorEncodeRow;
            sp->encodestrip = tif->tif_encodestrip;
            tif->tif_encodestrip = PredictorEncodeTile;
            sp->encodetile = tif->tif_encodetile;
            tif->tif_encodetile = PredictorEncodeTile;
        }
        /*
         * The data should not be swapped outside of the floating
         * point predictor, the differentiation routine should return
         * bytes in the native order.
         */
        if (tif->tif_flags & TIFF_SWAB)
        {
            tif->tif_postdecode = _TIFFNoPostDecode;
        }
    }

    return 1;
}

#define REPEAT4(n, op)                                                         \
    switch (n)                                                                 \
    {                                                                          \
        default:                                                               \
        {                                                                      \
            tmsize_t i;                                                        \
            for (i = n - 4; i > 0; i--)                                        \
            {                                                                  \
                op;                                                            \
            }                                                                  \
        } /*-fallthrough*/                                                     \
        case 4:                                                                \
            op; /*-fallthrough*/                                               \
        case 3:                                                                \
            op; /*-fallthrough*/                                               \
        case 2:                                                                \
            op; /*-fallthrough*/                                               \
        case 1:                                                                \
            op; /*-fallthrough*/                                               \
        case 0:;                                                               \
    }

/* Remarks related to C standard compliance in all below functions : */
/* - to avoid any undefined behavior, we only operate on unsigned types */
/*   since the behavior of "overflows" is defined (wrap over) */
/* - when storing into the byte stream, we explicitly mask with 0xff so */
/*   as to make icc -check=conversions happy (not necessary by the standard) */

/*
 * Predictor accumulation works on native-order samples.  If the TIFF byte
 * order differs from host byte order, the swab wrapper first swaps the byte
 * stream explicitly.  These helpers only avoid unaligned typed dereferences.
 * Use fixed-size memcpy() calls directly so optimizing compilers can expand
 * them in this per-sample hot path.  _TIFFmemcpy() is an out-of-line wrapper
 * in non-LTO builds.
 */
/**
 * @brief Add two unaligned native-order 16-bit samples modulo their width.
 * @ingroup libtiff_private_predictor
 *
 * Uses fixed-size memcpy to load/store without typed alignment
 * requirements. Reads previous before overwriting current, permitting
 * identical slots. No bounds checks, allocation, I/O, diagnostics, handle
 * changes or ownership effects.
 *
 * @param[in,out] current Writable byte storage for one native-order sample,
 * unaligned allowed.
 * @param[in] previous Readable byte storage for one same-width native-order
 * sample, unaligned allowed.
 */
TIFF_NOSANITIZE_UNSIGNED_INT_OVERFLOW
static void PredictorAccumulate16NativeUnaligned(uint8_t *current,
                                                 const uint8_t *previous)
{
    uint16_t thisval;
    uint16_t prevval;
    memcpy(&thisval, current, sizeof(thisval));
    memcpy(&prevval, previous, sizeof(prevval));
    thisval =
        (uint16_t)(((unsigned int)thisval + (unsigned int)prevval) & 0xffff);
    memcpy(current, &thisval, sizeof(thisval));
}

/**
 * @brief Add two unaligned native-order 32-bit samples modulo their width.
 * @ingroup libtiff_private_predictor
 *
 * Uses fixed-size memcpy to load/store without typed alignment
 * requirements. Reads previous before overwriting current, permitting
 * identical slots. No bounds checks, allocation, I/O, diagnostics, handle
 * changes or ownership effects.
 *
 * @param[in,out] current Writable byte storage for one native-order sample,
 * unaligned allowed.
 * @param[in] previous Readable byte storage for one same-width native-order
 * sample, unaligned allowed.
 */
TIFF_NOSANITIZE_UNSIGNED_INT_OVERFLOW
static void PredictorAccumulate32NativeUnaligned(uint8_t *current,
                                                 const uint8_t *previous)
{
    uint32_t thisval;
    uint32_t prevval;
    memcpy(&thisval, current, sizeof(thisval));
    memcpy(&prevval, previous, sizeof(prevval));
    thisval += prevval;
    memcpy(current, &thisval, sizeof(thisval));
}

/**
 * @brief Add two unaligned native-order 64-bit samples modulo their width.
 * @ingroup libtiff_private_predictor
 *
 * Uses fixed-size memcpy to load/store without typed alignment
 * requirements. Reads previous before overwriting current, permitting
 * identical slots. No bounds checks, allocation, I/O, diagnostics, handle
 * changes or ownership effects.
 *
 * @param[in,out] current Writable byte storage for one native-order sample,
 * unaligned allowed.
 * @param[in] previous Readable byte storage for one same-width native-order
 * sample, unaligned allowed.
 */
TIFF_NOSANITIZE_UNSIGNED_INT_OVERFLOW
static void PredictorAccumulate64NativeUnaligned(uint8_t *current,
                                                 const uint8_t *previous)
{
    uint64_t thisval;
    uint64_t prevval;
    memcpy(&thisval, current, sizeof(thisval));
    memcpy(&prevval, previous, sizeof(prevval));
    thisval += prevval;
    memcpy(current, &thisval, sizeof(thisval));
}

/**
 * @brief Reverse each 16-bit sample in an unaligned byte stream.
 * @ingroup libtiff_private_predictor
 *
 * Swaps bytes directly; zero count performs no accesses. No bounds checks,
 * allocation, I/O, diagnostics, or ownership effects.
 *
 * @param[in,out] cp Writable byte stream covering n complete samples.
 * @param[in] n Nonnegative sample count.
 */
static void PredictorSwabByteStream16(uint8_t *cp, tmsize_t n)
{
    while (n-- > 0)
    {
        uint8_t tmp = cp[0];
        cp[0] = cp[1];
        cp[1] = tmp;
        cp += 2;
    }
}

/**
 * @brief Reverse each 32-bit sample in an unaligned byte stream.
 * @ingroup libtiff_private_predictor
 *
 * Swaps bytes directly; zero count performs no accesses. No bounds checks,
 * allocation, I/O, diagnostics, or ownership effects.
 *
 * @param[in,out] cp Writable byte stream covering n complete samples.
 * @param[in] n Nonnegative sample count.
 */
static void PredictorSwabByteStream32(uint8_t *cp, tmsize_t n)
{
    while (n-- > 0)
    {
        uint8_t tmp = cp[0];
        cp[0] = cp[3];
        cp[3] = tmp;
        tmp = cp[1];
        cp[1] = cp[2];
        cp[2] = tmp;
        cp += 4;
    }
}

/**
 * @brief Reverse each 64-bit sample in an unaligned byte stream.
 * @ingroup libtiff_private_predictor
 *
 * Swaps bytes directly; zero count performs no accesses. No bounds checks,
 * allocation, I/O, diagnostics, or ownership effects.
 *
 * @param[in,out] cp Writable byte stream covering n complete samples.
 * @param[in] n Nonnegative sample count.
 */
static void PredictorSwabByteStream64(uint8_t *cp, tmsize_t n)
{
    while (n-- > 0)
    {
        uint8_t tmp = cp[0];
        cp[0] = cp[7];
        cp[7] = tmp;
        tmp = cp[1];
        cp[1] = cp[6];
        cp[6] = tmp;
        tmp = cp[2];
        cp[2] = cp[5];
        cp[5] = tmp;
        tmp = cp[3];
        cp[3] = cp[4];
        cp[4] = tmp;
        cp += 8;
    }
}

/**
 * @brief Accumulate 8-bit horizontal predictor samples in place.
 * @ingroup libtiff_private_predictor
 *
 * Requires positive nonzero cached stride and nonnegative complete row
 * bytes divisible by 1*stride, with representable products and live
 * writable storage. Uses unsigned modular arithmetic; first-pixel channel
 * samples remain unchanged. No allocation, pixel I/O, or ownership
 * transfer; invalid extent is diagnosed through handle callbacks.
 *
 * @param[in] tif Non-NULL handle whose TIFFPredictorState supplies the
 * cached stride; used otherwise only for diagnostics.
 * @param[in,out] cp0 Borrowed writable row storage of cc bytes.
 * @param[in] cc Row byte extent under the selected transform preconditions.
 * @return One on transformation, zero on rejected row extent.
 */
TIFF_NOSANITIZE_UNSIGNED_INT_OVERFLOW
static int horAcc8(TIFF *tif, uint8_t *cp0, tmsize_t cc)
{
    tmsize_t stride = PredictorState(tif)->stride;

    uint8_t *cp = cp0;
    if ((cc % stride) != 0)
    {
        TIFFErrorExtR(tif, "horAcc8", "%s", "(cc%stride)!=0");
        return 0;
    }

    if (cc > stride)
    {
        /*
         * Pipeline the most common cases.
         */
        if (stride == 1)
        {
            uint32_t acc = cp[0];
            tmsize_t i = stride;
            for (; i < cc - 3; i += 4)
            {
                cp[i + 0] = (uint8_t)((acc += cp[i + 0]) & 0xff);
                cp[i + 1] = (uint8_t)((acc += cp[i + 1]) & 0xff);
                cp[i + 2] = (uint8_t)((acc += cp[i + 2]) & 0xff);
                cp[i + 3] = (uint8_t)((acc += cp[i + 3]) & 0xff);
            }
            for (; i < cc; i++)
            {
                cp[i + 0] = (uint8_t)((acc += cp[i + 0]) & 0xff);
            }
        }
        else if (stride == 3)
        {
            uint32_t cr = cp[0];
            uint32_t cg = cp[1];
            uint32_t cb = cp[2];
            tmsize_t i = stride;
            for (; i < cc; i += stride)
            {
                cp[i + 0] = (uint8_t)((cr += cp[i + 0]) & 0xff);
                cp[i + 1] = (uint8_t)((cg += cp[i + 1]) & 0xff);
                cp[i + 2] = (uint8_t)((cb += cp[i + 2]) & 0xff);
            }
        }
        else if (stride == 4)
        {
            uint32_t cr = cp[0];
            uint32_t cg = cp[1];
            uint32_t cb = cp[2];
            uint32_t ca = cp[3];
            tmsize_t i = stride;
            for (; i < cc; i += stride)
            {
                cp[i + 0] = (uint8_t)((cr += cp[i + 0]) & 0xff);
                cp[i + 1] = (uint8_t)((cg += cp[i + 1]) & 0xff);
                cp[i + 2] = (uint8_t)((cb += cp[i + 2]) & 0xff);
                cp[i + 3] = (uint8_t)((ca += cp[i + 3]) & 0xff);
            }
        }
        else
        {
            cc -= stride;
            do
            {
                REPEAT4(stride,
                        cp[stride] = (uint8_t)((cp[stride] + *cp) & 0xff);
                        cp++)
                cc -= stride;
            } while (cc > 0);
        }
    }
    return 1;
}

/**
 * @brief Swap and accumulate 16-bit horizontal predictor samples in place.
 * @ingroup libtiff_private_predictor
 *
 * Requires positive nonzero cached stride and nonnegative complete row
 * bytes divisible by 2*stride, with representable products and live
 * writable storage. Swaps complete samples before accumulation, even before
 * the inner divisibility rejection; input can change on rejection.
 * Accumulation uses byte-stream/memcpy helpers and accepts unaligned
 * storage. No allocation, pixel I/O, or ownership transfer; invalid extent
 * is diagnosed through handle callbacks.
 *
 * @param[in] tif Non-NULL handle whose TIFFPredictorState supplies the
 * cached stride; used otherwise only for diagnostics.
 * @param[in,out] cp0 Borrowed writable row storage of cc bytes.
 * @param[in] cc Row byte extent under the selected transform preconditions.
 * @return One on transformation, zero on rejected row extent.
 */
static int swabHorAcc16(TIFF *tif, uint8_t *cp0, tmsize_t cc)
{
    tmsize_t wc = cc / 2;

    PredictorSwabByteStream16(cp0, wc);
    return horAcc16(tif, cp0, cc);
}

/**
 * @brief Accumulate 16-bit horizontal predictor samples in place.
 * @ingroup libtiff_private_predictor
 *
 * Requires positive nonzero cached stride and nonnegative complete row
 * bytes divisible by 2*stride, with representable products and live
 * writable storage. Uses unsigned modular arithmetic; first-pixel channel
 * samples remain unchanged. Accumulation uses byte-stream/memcpy helpers
 * and accepts unaligned storage. No allocation, pixel I/O, or ownership
 * transfer; invalid extent is diagnosed through handle callbacks.
 *
 * @param[in] tif Non-NULL handle whose TIFFPredictorState supplies the
 * cached stride; used otherwise only for diagnostics.
 * @param[in,out] cp0 Borrowed writable row storage of cc bytes.
 * @param[in] cc Row byte extent under the selected transform preconditions.
 * @return One on transformation, zero on rejected row extent.
 */
TIFF_NOSANITIZE_UNSIGNED_INT_OVERFLOW
static int horAcc16(TIFF *tif, uint8_t *cp0, tmsize_t cc)
{
    tmsize_t stride = PredictorState(tif)->stride;
    uint8_t *cp = cp0;
    tmsize_t wc = cc / 2;

    if ((cc % (2 * stride)) != 0)
    {
        TIFFErrorExtR(tif, "horAcc16", "%s", "cc%(2*stride))!=0");
        return 0;
    }

    if (wc > stride)
    {
        wc -= stride;
        do
        {
            REPEAT4(stride,
                    PredictorAccumulate16NativeUnaligned(cp + 2 * stride, cp);
                    cp += 2)
            wc -= stride;
        } while (wc > 0);
    }
    return 1;
}

/**
 * @brief Swap and accumulate 32-bit horizontal predictor samples in place.
 * @ingroup libtiff_private_predictor
 *
 * Requires positive nonzero cached stride and nonnegative complete row
 * bytes divisible by 4*stride, with representable products and live
 * writable storage. Swaps complete samples before accumulation, even before
 * the inner divisibility rejection; input can change on rejection.
 * Accumulation uses byte-stream/memcpy helpers and accepts unaligned
 * storage. No allocation, pixel I/O, or ownership transfer; invalid extent
 * is diagnosed through handle callbacks.
 *
 * @param[in] tif Non-NULL handle whose TIFFPredictorState supplies the
 * cached stride; used otherwise only for diagnostics.
 * @param[in,out] cp0 Borrowed writable row storage of cc bytes.
 * @param[in] cc Row byte extent under the selected transform preconditions.
 * @return One on transformation, zero on rejected row extent.
 */
static int swabHorAcc32(TIFF *tif, uint8_t *cp0, tmsize_t cc)
{
    tmsize_t wc = cc / 4;

    PredictorSwabByteStream32(cp0, wc);
    return horAcc32(tif, cp0, cc);
}

/**
 * @brief Accumulate 32-bit horizontal predictor samples in place.
 * @ingroup libtiff_private_predictor
 *
 * Requires positive nonzero cached stride and nonnegative complete row
 * bytes divisible by 4*stride, with representable products and live
 * writable storage. Uses unsigned modular arithmetic; first-pixel channel
 * samples remain unchanged. Accumulation uses byte-stream/memcpy helpers
 * and accepts unaligned storage. No allocation, pixel I/O, or ownership
 * transfer; invalid extent is diagnosed through handle callbacks.
 *
 * @param[in] tif Non-NULL handle whose TIFFPredictorState supplies the
 * cached stride; used otherwise only for diagnostics.
 * @param[in,out] cp0 Borrowed writable row storage of cc bytes.
 * @param[in] cc Row byte extent under the selected transform preconditions.
 * @return One on transformation, zero on rejected row extent.
 */
TIFF_NOSANITIZE_UNSIGNED_INT_OVERFLOW
static int horAcc32(TIFF *tif, uint8_t *cp0, tmsize_t cc)
{
    tmsize_t stride = PredictorState(tif)->stride;
    uint8_t *cp = cp0;
    tmsize_t wc = cc / 4;

    if ((cc % (4 * stride)) != 0)
    {
        TIFFErrorExtR(tif, "horAcc32", "%s", "cc%(4*stride))!=0");
        return 0;
    }

    if (wc > stride)
    {
        wc -= stride;
        do
        {
            REPEAT4(stride,
                    PredictorAccumulate32NativeUnaligned(cp + 4 * stride, cp);
                    cp += 4)
            wc -= stride;
        } while (wc > 0);
    }
    return 1;
}

/**
 * @brief Swap and accumulate 64-bit horizontal predictor samples in place.
 * @ingroup libtiff_private_predictor
 *
 * Requires positive nonzero cached stride and nonnegative complete row
 * bytes divisible by 8*stride, with representable products and live
 * writable storage. Swaps complete samples before accumulation, even before
 * the inner divisibility rejection; input can change on rejection.
 * Accumulation uses byte-stream/memcpy helpers and accepts unaligned
 * storage. No allocation, pixel I/O, or ownership transfer; invalid extent
 * is diagnosed through handle callbacks.
 *
 * @param[in] tif Non-NULL handle whose TIFFPredictorState supplies the
 * cached stride; used otherwise only for diagnostics.
 * @param[in,out] cp0 Borrowed writable row storage of cc bytes.
 * @param[in] cc Row byte extent under the selected transform preconditions.
 * @return One on transformation, zero on rejected row extent.
 */
static int swabHorAcc64(TIFF *tif, uint8_t *cp0, tmsize_t cc)
{
    tmsize_t wc = cc / 8;

    PredictorSwabByteStream64(cp0, wc);
    return horAcc64(tif, cp0, cc);
}

/**
 * @brief Accumulate 64-bit horizontal predictor samples in place.
 * @ingroup libtiff_private_predictor
 *
 * Requires positive nonzero cached stride and nonnegative complete row
 * bytes divisible by 8*stride, with representable products and live
 * writable storage. Uses unsigned modular arithmetic; first-pixel channel
 * samples remain unchanged. Accumulation uses byte-stream/memcpy helpers
 * and accepts unaligned storage. No allocation, pixel I/O, or ownership
 * transfer; invalid extent is diagnosed through handle callbacks.
 *
 * @param[in] tif Non-NULL handle whose TIFFPredictorState supplies the
 * cached stride; used otherwise only for diagnostics.
 * @param[in,out] cp0 Borrowed writable row storage of cc bytes.
 * @param[in] cc Row byte extent under the selected transform preconditions.
 * @return One on transformation, zero on rejected row extent.
 */
TIFF_NOSANITIZE_UNSIGNED_INT_OVERFLOW
static int horAcc64(TIFF *tif, uint8_t *cp0, tmsize_t cc)
{
    tmsize_t stride = PredictorState(tif)->stride;
    uint8_t *cp = cp0;
    tmsize_t wc = cc / 8;

    if ((cc % (8 * stride)) != 0)
    {
        TIFFErrorExtR(tif, "horAcc64", "%s", "cc%(8*stride))!=0");
        return 0;
    }

    if (wc > stride)
    {
        wc -= stride;
        do
        {
            REPEAT4(stride,
                    PredictorAccumulate64NativeUnaligned(cp + 8 * stride, cp);
                    cp += 8)
            wc -= stride;
        } while (wc > 0);
    }
    return 1;
}

/*
 * Floating point predictor accumulation routine.
 */
/**
 * @brief Undo floating-point byte-plane prediction on one row.
 * @ingroup libtiff_private_predictor
 *
 * Requires setup-selected nonzero bytes per sample and stride, a positive
 * complete row extent divisible by their product, and sufficient writable
 * storage. Accumulates byte planes then interlaces them into native sample
 * order. Allocates a row-sized temporary buffer, released on success;
 * rejected divisibility or allocation returns zero. Does not perform
 * floating-point arithmetic on sample values. Buffer mutations survive
 * later failure; no pixel I/O or ownership transfer.
 *
 * @param[in,out] tif Non-NULL live handle whose tif_data begins with a
 * valid TIFFPredictorState; serialise codec operations.
 * @param[in,out] cp0 Borrowed writable row storage of cc bytes; cc must be
 * positive, since a zero extent fails the temporary allocation.
 * @param[in] cc Row byte extent under the selected transform preconditions.
 * @return One on transformation, zero on extent/allocation failure.
 */
static int fpAcc(TIFF *tif, uint8_t *cp0, tmsize_t cc)
{
    tmsize_t stride = PredictorState(tif)->stride;
    uint32_t bps = tif->tif_dir.td_bitspersample / 8;
    tmsize_t wc = cc / bps;
    tmsize_t count = cc;
    uint8_t *cp = cp0;
    uint8_t *tmp;

    if (cc % (bps * stride) != 0)
    {
        TIFFErrorExtR(tif, "fpAcc", "%s", "cc%(bps*stride))!=0");
        return 0;
    }

    tmp = (uint8_t *)_TIFFmallocExt(tif, cc);
    if (!tmp)
        return 0;

    if (stride == 1)
    {
        /* Optimization of general case */
#define OP                                                                     \
    do                                                                         \
    {                                                                          \
        cp[1] = (uint8_t)((cp[1] + cp[0]) & 0xff);                             \
        ++cp;                                                                  \
    } while (0)
        for (; count > 8; count -= 8)
        {
            OP;
            OP;
            OP;
            OP;
            OP;
            OP;
            OP;
            OP;
        }
        for (; count > 1; count -= 1)
        {
            OP;
        }
#undef OP
    }
    else
    {
        while (count > stride)
        {
            REPEAT4(stride, cp[stride] = (uint8_t)((cp[stride] + cp[0]) & 0xff);
                    cp++)
            count -= stride;
        }
    }

    _TIFFmemcpy(tmp, cp0, cc);
    cp = (uint8_t *)cp0;
    count = 0;

#if defined(__x86_64__) || (defined(_M_X64) && !defined(_M_ARM64EC))
    if (bps == 4)
    {
        /* Optimization of general case */
        for (; count + 15 < wc; count += 16)
        {
            /* Interlace 4*16 byte values */

            __m128i xmm0 =
                _mm_loadu_si128((const __m128i *)(tmp + count + 3 * wc));
            __m128i xmm1 =
                _mm_loadu_si128((const __m128i *)(tmp + count + 2 * wc));
            __m128i xmm2 =
                _mm_loadu_si128((const __m128i *)(tmp + count + 1 * wc));
            __m128i xmm3 =
                _mm_loadu_si128((const __m128i *)(tmp + count + 0 * wc));
            /* (xmm0_0, xmm1_0, xmm0_1, xmm1_1, xmm0_2, xmm1_2, ...) */
            __m128i tmp0 = _mm_unpacklo_epi8(xmm0, xmm1);
            /* (xmm0_8, xmm1_8, xmm0_9, xmm1_9, xmm0_10, xmm1_10, ...) */
            __m128i tmp1 = _mm_unpackhi_epi8(xmm0, xmm1);
            /* (xmm2_0, xmm3_0, xmm2_1, xmm3_1, xmm2_2, xmm3_2, ...) */
            __m128i tmp2 = _mm_unpacklo_epi8(xmm2, xmm3);
            /* (xmm2_8, xmm3_8, xmm2_9, xmm3_9, xmm2_10, xmm3_10, ...) */
            __m128i tmp3 = _mm_unpackhi_epi8(xmm2, xmm3);
            /* (xmm0_0, xmm1_0, xmm2_0, xmm3_0, xmm0_1, xmm1_1, xmm2_1, xmm3_1,
             * ...) */
            __m128i tmp2_0 = _mm_unpacklo_epi16(tmp0, tmp2);
            __m128i tmp2_1 = _mm_unpackhi_epi16(tmp0, tmp2);
            __m128i tmp2_2 = _mm_unpacklo_epi16(tmp1, tmp3);
            __m128i tmp2_3 = _mm_unpackhi_epi16(tmp1, tmp3);
            _mm_storeu_si128((__m128i *)(cp + 4 * count + 0 * 16), tmp2_0);
            _mm_storeu_si128((__m128i *)(cp + 4 * count + 1 * 16), tmp2_1);
            _mm_storeu_si128((__m128i *)(cp + 4 * count + 2 * 16), tmp2_2);
            _mm_storeu_si128((__m128i *)(cp + 4 * count + 3 * 16), tmp2_3);
        }
    }
#endif

    for (; count < wc; count++)
    {
        uint32_t byte;
        for (byte = 0; byte < bps; byte++)
        {
#if WORDS_BIGENDIAN
            cp[bps * count + byte] = tmp[byte * wc + count];
#else
            cp[bps * count + byte] = tmp[(bps - byte - 1) * wc + count];
#endif
        }
    }
    _TIFFfreeExt(tif, tmp);
    return 1;
}

/*
 * Decode a scanline and apply the predictor routine.
 */
/**
 * @brief Decode one row then undo prediction in place.
 * @ingroup libtiff_private_predictor
 *
 * Requires installed parent decoder and transform. Parent callback consumes
 * raw input and can allocate/diagnose; transform runs only on nonzero
 * parent status. Caller retains output, which can be partial or changed on
 * failure. No rollback or ownership transfer.
 *
 * @param[in,out] tif Non-NULL live handle whose tif_data begins with a
 * valid TIFFPredictorState; serialise codec operations.
 * @param[out] op0 Caller-owned writable decoded output of occ0 bytes.
 * @param[in] occ0 Nonnegative decoded byte extent, complete rows for strile
 * wrappers.
 * @param[in] s Sample plane passed unchanged to the parent codec.
 * @return Transform status after parent success, zero on parent failure.
 */
static int PredictorDecodeRow(TIFF *tif, uint8_t *op0, tmsize_t occ0,
                              uint16_t s)
{
    TIFFPredictorState *sp = PredictorState(tif);

    assert(sp != NULL);
    assert(sp->decoderow != NULL);
    assert(sp->decodepfunc != NULL);

    if ((*sp->decoderow)(tif, op0, occ0, s))
    {
        return (*sp->decodepfunc)(tif, op0, occ0);
    }
    else
        return 0;
}

/*
 * Decode a tile/strip and apply the predictor routine.
 * Note that horizontal differencing must be done on a
 * row-by-row basis.  The width of a "row" has already
 * been calculated at pre-decode time according to the
 * strip/tile dimensions.
 */
/**
 * @brief Decode a strile then undo prediction row by row.
 * @ingroup libtiff_private_predictor
 *
 * Calls saved decodetile for both strip and tile wrapping, then requires an
 * extent divisible by positive rowsize. Invokes decodepfunc per row and
 * stops on failure. Parent input progress and earlier output rows remain
 * changed. No rollback or output ownership transfer; callbacks may
 * allocate, diagnose and read client data.
 *
 * @param[in,out] tif Non-NULL live handle whose tif_data begins with a
 * valid TIFFPredictorState; serialise codec operations.
 * @param[out] op0 Caller-owned writable decoded output of occ0 bytes.
 * @param[in] occ0 Nonnegative decoded byte extent, complete rows for strile
 * wrappers.
 * @param[in] s Sample plane passed unchanged to the parent codec.
 * @return One after all rows, zero on parent, extent, or transform failure.
 */
static int PredictorDecodeTile(TIFF *tif, uint8_t *op0, tmsize_t occ0,
                               uint16_t s)
{
    TIFFPredictorState *sp = PredictorState(tif);

    assert(sp != NULL);
    assert(sp->decodetile != NULL);

    if ((*sp->decodetile)(tif, op0, occ0, s))
    {
        tmsize_t rowsize = sp->rowsize;
        assert(rowsize > 0);
        if ((occ0 % rowsize) != 0)
        {
            TIFFErrorExtR(tif, "PredictorDecodeTile", "%s",
                          "occ0%rowsize != 0");
            return 0;
        }
        assert(sp->decodepfunc != NULL);
        while (occ0 > 0)
        {
            if (!(*sp->decodepfunc)(tif, op0, rowsize))
                return 0;
            occ0 -= rowsize;
            op0 += rowsize;
        }
        return 1;
    }
    else
        return 0;
}

/**
 * @brief Difference 8-bit horizontal predictor samples in place.
 * @ingroup libtiff_private_predictor
 *
 * Requires positive nonzero cached stride and nonnegative complete row
 * bytes divisible by 1*stride, with representable products and live
 * writable storage. Uses unsigned modular arithmetic; first-pixel channel
 * samples remain unchanged. No allocation, pixel I/O, or ownership
 * transfer; invalid extent is diagnosed through handle callbacks.
 *
 * @param[in] tif Non-NULL handle whose TIFFPredictorState supplies the
 * cached stride; used otherwise only for diagnostics.
 * @param[in,out] cp0 Borrowed writable row storage of cc bytes.
 * @param[in] cc Row byte extent under the selected transform preconditions.
 * @return One on transformation, zero on rejected row extent.
 */
TIFF_NOSANITIZE_UNSIGNED_INT_OVERFLOW
static int horDiff8(TIFF *tif, uint8_t *cp0, tmsize_t cc)
{
    TIFFPredictorState *sp = PredictorState(tif);
    tmsize_t stride = sp->stride;
    unsigned char *cp = (unsigned char *)cp0;

    if ((cc % stride) != 0)
    {
        TIFFErrorExtR(tif, "horDiff8", "%s", "(cc%stride)!=0");
        return 0;
    }

    if (cc > stride)
    {
        cc -= stride;
        /*
         * Pipeline the most common cases.
         */
        if (stride == 3)
        {
            unsigned int r1, g1, b1;
            unsigned int r2 = cp[0];
            unsigned int g2 = cp[1];
            unsigned int b2 = cp[2];
            do
            {
                r1 = cp[3];
                cp[3] = (unsigned char)((r1 - r2) & 0xff);
                r2 = r1;
                g1 = cp[4];
                cp[4] = (unsigned char)((g1 - g2) & 0xff);
                g2 = g1;
                b1 = cp[5];
                cp[5] = (unsigned char)((b1 - b2) & 0xff);
                b2 = b1;
                cp += 3;
            } while ((cc -= 3) > 0);
        }
        else if (stride == 4)
        {
            unsigned int r1, g1, b1, a1;
            unsigned int r2 = cp[0];
            unsigned int g2 = cp[1];
            unsigned int b2 = cp[2];
            unsigned int a2 = cp[3];
            do
            {
                r1 = cp[4];
                cp[4] = (unsigned char)((r1 - r2) & 0xff);
                r2 = r1;
                g1 = cp[5];
                cp[5] = (unsigned char)((g1 - g2) & 0xff);
                g2 = g1;
                b1 = cp[6];
                cp[6] = (unsigned char)((b1 - b2) & 0xff);
                b2 = b1;
                a1 = cp[7];
                cp[7] = (unsigned char)((a1 - a2) & 0xff);
                a2 = a1;
                cp += 4;
            } while ((cc -= 4) > 0);
        }
        else
        {
            cp += cc - 1;
            do
            {
                REPEAT4(stride,
                        cp[stride] =
                            (unsigned char)((cp[stride] - cp[0]) & 0xff);
                        cp--)
            } while ((cc -= stride) > 0);
        }
    }
    return 1;
}

/**
 * @brief Difference 16-bit horizontal predictor samples in place.
 * @ingroup libtiff_private_predictor
 *
 * Requires positive nonzero cached stride and nonnegative complete row
 * bytes divisible by 2*stride, with representable products and live
 * writable storage. Uses unsigned modular arithmetic; first-pixel channel
 * samples remain unchanged. Requires uint16_t alignment for typed
 * differencing. No allocation, pixel I/O, or ownership transfer; invalid
 * extent is diagnosed through handle callbacks.
 *
 * @param[in] tif Non-NULL handle whose TIFFPredictorState supplies the
 * cached stride; used otherwise only for diagnostics.
 * @param[in,out] cp0 Borrowed writable row storage of cc bytes.
 * @param[in] cc Row byte extent under the selected transform preconditions.
 * @return One on transformation, zero on rejected row extent.
 */
TIFF_NOSANITIZE_UNSIGNED_INT_OVERFLOW
static int horDiff16(TIFF *tif, uint8_t *cp0, tmsize_t cc)
{
    TIFFPredictorState *sp = PredictorState(tif);
    tmsize_t stride = sp->stride;
    uint16_t *wp = (uint16_t *)cp0;
    tmsize_t wc = cc / 2;

    if ((cc % (2 * stride)) != 0)
    {
        TIFFErrorExtR(tif, "horDiff8", "%s", "(cc%(2*stride))!=0");
        return 0;
    }

    if (wc > stride)
    {
        wc -= stride;
        wp += wc - 1;
        do
        {
            REPEAT4(stride, wp[stride] = (uint16_t)(((unsigned int)wp[stride] -
                                                     (unsigned int)wp[0]) &
                                                    0xffff);
                    wp--)
            wc -= stride;
        } while (wc > 0);
    }
    return 1;
}

/**
 * @brief Swap and difference 16-bit horizontal predictor samples in place.
 * @ingroup libtiff_private_predictor
 *
 * Requires positive nonzero cached stride and nonnegative complete row
 * bytes divisible by 2*stride, with representable products and live
 * writable storage. Differences native-order samples first, then swaps
 * after success. Requires uint16_t alignment for typed differencing. No
 * allocation, pixel I/O, or ownership transfer; invalid extent is diagnosed
 * through handle callbacks.
 *
 * @param[in] tif Non-NULL handle whose TIFFPredictorState supplies the
 * cached stride; used otherwise only for diagnostics.
 * @param[in,out] cp0 Borrowed writable row storage of cc bytes.
 * @param[in] cc Row byte extent under the selected transform preconditions.
 * @return One on transformation, zero on rejected row extent.
 */
static int swabHorDiff16(TIFF *tif, uint8_t *cp0, tmsize_t cc)
{
    uint16_t *wp = (uint16_t *)cp0;
    tmsize_t wc = cc / 2;

    if (!horDiff16(tif, cp0, cc))
        return 0;

    TIFFSwabArrayOfShort(wp, wc);
    return 1;
}

/**
 * @brief Difference 32-bit horizontal predictor samples in place.
 * @ingroup libtiff_private_predictor
 *
 * Requires positive nonzero cached stride and nonnegative complete row
 * bytes divisible by 4*stride, with representable products and live
 * writable storage. Uses unsigned modular arithmetic; first-pixel channel
 * samples remain unchanged. Requires uint32_t alignment for typed
 * differencing. No allocation, pixel I/O, or ownership transfer; invalid
 * extent is diagnosed through handle callbacks.
 *
 * @param[in] tif Non-NULL handle whose TIFFPredictorState supplies the
 * cached stride; used otherwise only for diagnostics.
 * @param[in,out] cp0 Borrowed writable row storage of cc bytes.
 * @param[in] cc Row byte extent under the selected transform preconditions.
 * @return One on transformation, zero on rejected row extent.
 */
TIFF_NOSANITIZE_UNSIGNED_INT_OVERFLOW
static int horDiff32(TIFF *tif, uint8_t *cp0, tmsize_t cc)
{
    TIFFPredictorState *sp = PredictorState(tif);
    tmsize_t stride = sp->stride;
    uint32_t *wp = (uint32_t *)cp0;
    tmsize_t wc = cc / 4;

    if ((cc % (4 * stride)) != 0)
    {
        TIFFErrorExtR(tif, "horDiff32", "%s", "(cc%(4*stride))!=0");
        return 0;
    }

    if (wc > stride)
    {
        wc -= stride;
        wp += wc - 1;
        do
        {
            REPEAT4(stride, wp[stride] -= wp[0]; wp--)
            wc -= stride;
        } while (wc > 0);
    }
    return 1;
}

/**
 * @brief Swap and difference 32-bit horizontal predictor samples in place.
 * @ingroup libtiff_private_predictor
 *
 * Requires positive nonzero cached stride and nonnegative complete row
 * bytes divisible by 4*stride, with representable products and live
 * writable storage. Differences native-order samples first, then swaps
 * after success. Requires uint32_t alignment for typed differencing. No
 * allocation, pixel I/O, or ownership transfer; invalid extent is diagnosed
 * through handle callbacks.
 *
 * @param[in] tif Non-NULL handle whose TIFFPredictorState supplies the
 * cached stride; used otherwise only for diagnostics.
 * @param[in,out] cp0 Borrowed writable row storage of cc bytes.
 * @param[in] cc Row byte extent under the selected transform preconditions.
 * @return One on transformation, zero on rejected row extent.
 */
static int swabHorDiff32(TIFF *tif, uint8_t *cp0, tmsize_t cc)
{
    uint32_t *wp = (uint32_t *)cp0;
    tmsize_t wc = cc / 4;

    if (!horDiff32(tif, cp0, cc))
        return 0;

    TIFFSwabArrayOfLong(wp, wc);
    return 1;
}

/**
 * @brief Difference 64-bit horizontal predictor samples in place.
 * @ingroup libtiff_private_predictor
 *
 * Requires positive nonzero cached stride and nonnegative complete row
 * bytes divisible by 8*stride, with representable products and live
 * writable storage. Uses unsigned modular arithmetic; first-pixel channel
 * samples remain unchanged. Requires uint64_t alignment for typed
 * differencing. No allocation, pixel I/O, or ownership transfer; invalid
 * extent is diagnosed through handle callbacks.
 *
 * @param[in] tif Non-NULL handle whose TIFFPredictorState supplies the
 * cached stride; used otherwise only for diagnostics.
 * @param[in,out] cp0 Borrowed writable row storage of cc bytes.
 * @param[in] cc Row byte extent under the selected transform preconditions.
 * @return One on transformation, zero on rejected row extent.
 */
TIFF_NOSANITIZE_UNSIGNED_INT_OVERFLOW
static int horDiff64(TIFF *tif, uint8_t *cp0, tmsize_t cc)
{
    TIFFPredictorState *sp = PredictorState(tif);
    tmsize_t stride = sp->stride;
    uint64_t *wp = (uint64_t *)cp0;
    tmsize_t wc = cc / 8;

    if ((cc % (8 * stride)) != 0)
    {
        TIFFErrorExtR(tif, "horDiff64", "%s", "(cc%(8*stride))!=0");
        return 0;
    }

    if (wc > stride)
    {
        wc -= stride;
        wp += wc - 1;
        do
        {
            REPEAT4(stride, wp[stride] -= wp[0]; wp--)
            wc -= stride;
        } while (wc > 0);
    }
    return 1;
}

/**
 * @brief Swap and difference 64-bit horizontal predictor samples in place.
 * @ingroup libtiff_private_predictor
 *
 * Requires positive nonzero cached stride and nonnegative complete row
 * bytes divisible by 8*stride, with representable products and live
 * writable storage. Differences native-order samples first, then swaps
 * after success. Requires uint64_t alignment for typed differencing. No
 * allocation, pixel I/O, or ownership transfer; invalid extent is diagnosed
 * through handle callbacks.
 *
 * @param[in] tif Non-NULL handle whose TIFFPredictorState supplies the
 * cached stride; used otherwise only for diagnostics.
 * @param[in,out] cp0 Borrowed writable row storage of cc bytes.
 * @param[in] cc Row byte extent under the selected transform preconditions.
 * @return One on transformation, zero on rejected row extent.
 */
static int swabHorDiff64(TIFF *tif, uint8_t *cp0, tmsize_t cc)
{
    uint64_t *wp = (uint64_t *)cp0;
    tmsize_t wc = cc / 8;

    if (!horDiff64(tif, cp0, cc))
        return 0;

    TIFFSwabArrayOfLong8(wp, wc);
    return 1;
}

/*
 * Floating point predictor differencing routine.
 */
/**
 * @brief Apply floating-point byte-plane prediction on one row.
 * @ingroup libtiff_private_predictor
 *
 * Requires setup-selected nonzero bytes per sample and stride, a positive
 * complete row extent divisible by their product, and sufficient writable
 * storage. Reorders native sample bytes into byte planes then differences
 * those planes. Allocates a row-sized temporary buffer, released on
 * success; rejected divisibility or allocation returns zero. Does not
 * perform floating-point arithmetic on sample values. Buffer mutations
 * survive later failure; no pixel I/O or ownership transfer.
 *
 * @param[in,out] tif Non-NULL live handle whose tif_data begins with a
 * valid TIFFPredictorState; serialise codec operations.
 * @param[in,out] cp0 Borrowed writable row storage of cc bytes; cc must be
 * positive, since a zero extent fails the temporary allocation.
 * @param[in] cc Row byte extent under the selected transform preconditions.
 * @return One on transformation, zero on extent/allocation failure.
 */
TIFF_NOSANITIZE_UNSIGNED_INT_OVERFLOW
static int fpDiff(TIFF *tif, uint8_t *cp0, tmsize_t cc)
{
    tmsize_t stride = PredictorState(tif)->stride;
    uint32_t bps = tif->tif_dir.td_bitspersample / 8;
    tmsize_t wc = cc / bps;
    tmsize_t count;
    uint8_t *cp = (uint8_t *)cp0;
    uint8_t *tmp;

    if ((cc % (bps * stride)) != 0)
    {
        TIFFErrorExtR(tif, "fpDiff", "%s", "(cc%(bps*stride))!=0");
        return 0;
    }

    tmp = (uint8_t *)_TIFFmallocExt(tif, cc);
    if (!tmp)
        return 0;

    _TIFFmemcpy(tmp, cp0, cc);
    for (count = 0; count < wc; count++)
    {
        uint32_t byte;
        for (byte = 0; byte < bps; byte++)
        {
#if WORDS_BIGENDIAN
            cp[byte * wc + count] = tmp[bps * count + byte];
#else
            cp[(bps - byte - 1) * wc + count] = tmp[bps * count + byte];
#endif
        }
    }
    _TIFFfreeExt(tif, tmp);

    cp = (uint8_t *)cp0;
    cp += cc - stride - 1;
    for (count = cc; count > stride; count -= stride)
        REPEAT4(stride,
                cp[stride] = (unsigned char)((cp[stride] - cp[0]) & 0xff);
                cp--)
    return 1;
}

/**
 * @brief Transform a temporary row copy then call the parent encoder.
 * @ingroup libtiff_private_predictor
 *
 * Allocates a working copy, preserves caller bytes from this predictor
 * layer, checks transform status, invokes the parent encoder and frees
 * temporary storage on each exit. Parent state/output effects remain on
 * failure; public write postdecode conversion can separately mutate the
 * original input. Reports allocation failure; no ownership transfer.
 *
 * @param[in,out] tif Non-NULL live handle whose tif_data begins with a
 * valid TIFFPredictorState; serialise codec operations.
 * @param[in] bp Borrowed readable row input of cc bytes, copied before
 * prediction.
 * @param[in] cc Row byte extent under the selected transform preconditions.
 * @param[in] s Sample plane passed unchanged to the parent codec.
 * @return Parent encoder status, or zero on allocation/transform failure.
 */
static int PredictorEncodeRow(TIFF *tif, uint8_t *bp, tmsize_t cc, uint16_t s)
{
    static const char module[] = "PredictorEncodeRow";
    TIFFPredictorState *sp = PredictorState(tif);
    uint8_t *working_copy;
    int result_code;

    assert(sp != NULL);
    assert(sp->encodepfunc != NULL);
    assert(sp->encoderow != NULL);

    /*
     * Do predictor manipulation in a working buffer to avoid altering
     * the callers buffer, like for PredictorEncodeTile().
     * https://gitlab.com/libtiff/libtiff/-/issues/5
     */
    working_copy = (uint8_t *)_TIFFmallocExt(tif, cc);
    if (working_copy == NULL)
    {
        TIFFErrorExtR(tif, module,
                      "Out of memory allocating %" PRId64 " byte temp buffer.",
                      (int64_t)cc);
        return 0;
    }
    memcpy(working_copy, bp, (size_t)cc);

    if (!(*sp->encodepfunc)(tif, working_copy, cc))
    {
        _TIFFfreeExt(tif, working_copy);
        return 0;
    }
    result_code = (*sp->encoderow)(tif, working_copy, cc, s);
    _TIFFfreeExt(tif, working_copy);
    return result_code;
}

/**
 * @brief Transform a temporary strile copy then call the parent encoder.
 * @ingroup libtiff_private_predictor
 *
 * Allocates/copies input and validates divisibility by positive rowsize.
 * Transforms each row but currently ignores encodepfunc return values, then
 * calls saved encodetile for both strip and tile wrappers. Frees temporary
 * storage on every ordinary exit. Caller bytes are preserved by this layer;
 * parent state/output can change on failure. No ownership transfer.
 *
 * @param[in,out] tif Non-NULL live handle whose tif_data begins with a
 * valid TIFFPredictorState; serialise codec operations.
 * @param[in] bp0 Borrowed readable strile input of cc0 bytes, copied before
 * prediction.
 * @param[in] cc0 Positive complete-row input byte extent.
 * @param[in] s Sample plane passed unchanged to the parent codec.
 * @return Parent encoder status, or zero on allocation/extent rejection;
 * not an independent guarantee that each row transform succeeded.
 */
static int PredictorEncodeTile(TIFF *tif, uint8_t *bp0, tmsize_t cc0,
                               uint16_t s)
{
    static const char module[] = "PredictorEncodeTile";
    TIFFPredictorState *sp = PredictorState(tif);
    uint8_t *working_copy;
    tmsize_t cc = cc0, rowsize;
    unsigned char *bp;
    int result_code;

    assert(sp != NULL);
    assert(sp->encodepfunc != NULL);
    assert(sp->encodetile != NULL);

    /*
     * Do predictor manipulation in a working buffer to avoid altering
     * the callers buffer. http://trac.osgeo.org/gdal/ticket/1965
     */
    working_copy = (uint8_t *)_TIFFmallocExt(tif, cc0);
    if (working_copy == NULL)
    {
        TIFFErrorExtR(tif, module,
                      "Out of memory allocating %" PRId64 " byte temp buffer.",
                      (int64_t)cc0);
        return 0;
    }
    memcpy(working_copy, bp0, (size_t)cc0);
    bp = working_copy;

    rowsize = sp->rowsize;
    assert(rowsize > 0);
    if ((cc0 % rowsize) != 0)
    {
        TIFFErrorExtR(tif, "PredictorEncodeTile", "%s", "(cc0%rowsize)!=0");
        _TIFFfreeExt(tif, working_copy);
        return 0;
    }
    while (cc > 0)
    {
        (*sp->encodepfunc)(tif, bp, rowsize);
        cc -= rowsize;
        bp += rowsize;
    }
    result_code = (*sp->encodetile)(tif, working_copy, cc0, s);

    _TIFFfreeExt(tif, working_copy);

    return result_code;
}

#define FIELD_PREDICTOR (FIELD_CODEC + 0) /* XXX */

/**
 * @brief Define the codec-owned Predictor field metadata.
 * @ingroup libtiff_private_predictor
 *
 * Immutable library-lifetime table merged during initialisation; do not
 * free it.
 */
static const TIFFField predictFields[] = {
    {TIFFTAG_PREDICTOR, 1, 1, TIFF_SHORT, 0, TIFF_SETGET_UINT16,
     FIELD_PREDICTOR, FALSE, FALSE, "Predictor", NULL},
};

/**
 * @brief Store Predictor or delegate another variadic tag assignment.
 * @ingroup libtiff_private_predictor
 *
 * For TIFFTAG_PREDICTOR consumes uint16_vap (normally promoted int),
 * narrows to uint16_t, sets the field bit and directory dirty flag. Does
 * not validate predictor support until setup. Other tags consume their
 * usual variadic contract through saved vsetparent. Callbacks can
 * mutate/allocate/diagnose; no ownership transfer for Predictor.
 *
 * @param[in,out] tif Non-NULL live handle whose tif_data begins with a
 * valid TIFFPredictorState; serialise codec operations.
 * @param[in] tag Tag identifier; delegated tags follow their usual argument
 * contract.
 * @param[in,out] ap Live va_list cursor carrying the tag-specific promoted
 * value/output pointer; consumed synchronously, not retained.
 * @return One for Predictor assignment, otherwise parent setter status.
 */
static int PredictorVSetField(TIFF *tif, uint32_t tag, va_list ap)
{
    TIFFPredictorState *sp = PredictorState(tif);

    assert(sp != NULL);
    assert(sp->vsetparent != NULL);

    switch (tag)
    {
        case TIFFTAG_PREDICTOR:
            sp->predictor = (uint16_t)va_arg(ap, uint16_vap);
            TIFFSetFieldBit(tif, FIELD_PREDICTOR);
            break;
        default:
            return (*sp->vsetparent)(tif, tag, ap);
    }
    tif->tif_flags |= TIFF_DIRTYDIRECT;
    return 1;
}

/**
 * @brief Fetch Predictor or delegate another variadic tag query.
 * @ingroup libtiff_private_predictor
 *
 * For TIFFTAG_PREDICTOR consumes a non-NULL uint16_t* and writes the cached
 * value. Other tags follow the saved parent getter contract. No output
 * allocation or ownership transfer for Predictor; parent effects depend on
 * tag.
 *
 * @param[in,out] tif Non-NULL live handle whose tif_data begins with a
 * valid TIFFPredictorState; serialise codec operations.
 * @param[in] tag Tag identifier; delegated tags follow their usual argument
 * contract.
 * @param[in,out] ap Live va_list cursor carrying the tag-specific promoted
 * value/output pointer; consumed synchronously, not retained.
 * @return One for Predictor retrieval, otherwise parent getter status.
 */
static int PredictorVGetField(TIFF *tif, uint32_t tag, va_list ap)
{
    TIFFPredictorState *sp = PredictorState(tif);

    assert(sp != NULL);
    assert(sp->vgetparent != NULL);

    switch (tag)
    {
        case TIFFTAG_PREDICTOR:
            *va_arg(ap, uint16_t *) = (uint16_t)sp->predictor;
            break;
        default:
            return (*sp->vgetparent)(tif, tag, ap);
    }
    return 1;
}

/**
 * @brief Print predictor information then chain the parent printer.
 * @ingroup libtiff_private_predictor
 *
 * Writes to borrowed FILE without checking stdio errors, prints only when
 * its field bit is set, then invokes saved printer with flags. Does not
 * close the stream or transfer ownership; parent printer effects apply. No
 * separate status channel.
 *
 * @param[in] tif Non-NULL handle whose TIFFPredictorState holds the
 * predictor value and saved parent printer; not modified.
 * @param[in,out] fd Borrowed writable stdio stream; remains caller-owned.
 * @param[in] flags Print flags forwarded to the saved printer.
 */
static void PredictorPrintDir(TIFF *tif, FILE *fd, long flags)
{
    TIFFPredictorState *sp = PredictorState(tif);

    (void)flags;
    if (TIFFFieldSet(tif, FIELD_PREDICTOR))
    {
        fprintf(fd, "  Predictor: ");
        switch (sp->predictor)
        {
            case 1:
                fprintf(fd, "none ");
                break;
            case 2:
                fprintf(fd, "horizontal differencing ");
                break;
            case 3:
                fprintf(fd, "floating point predictor ");
                break;
            default:
                break;
        }
        fprintf(fd, "%d (0x%x)\n", sp->predictor, (unsigned)sp->predictor);
    }
    if (sp->printdir)
        (*sp->printdir)(tif, fd, flags);
}

int TIFFPredictorInit(TIFF *tif)
{
    TIFFPredictorState *sp = PredictorState(tif);

    assert(sp != 0);

    /*
     * Merge codec-specific tag information.
     */
    if (!_TIFFMergeFields(tif, predictFields, TIFFArrayCount(predictFields)))
    {
        TIFFErrorExtR(tif, "TIFFPredictorInit",
                      "Merging Predictor codec-specific tags failed");
        return 0;
    }

    /*
     * Override parent get/set field methods.
     */
    sp->vgetparent = tif->tif_tagmethods.vgetfield;
    tif->tif_tagmethods.vgetfield =
        PredictorVGetField; /* hook for predictor tag */
    sp->vsetparent = tif->tif_tagmethods.vsetfield;
    tif->tif_tagmethods.vsetfield =
        PredictorVSetField; /* hook for predictor tag */
    sp->printdir = tif->tif_tagmethods.printdir;
    tif->tif_tagmethods.printdir =
        PredictorPrintDir; /* hook for predictor tag */

    sp->setupdecode = tif->tif_setupdecode;
    tif->tif_setupdecode = PredictorSetupDecode;
    sp->setupencode = tif->tif_setupencode;
    tif->tif_setupencode = PredictorSetupEncode;

    sp->predictor = 1;      /* default value */
    sp->encodepfunc = NULL; /* no predictor routine */
    sp->decodepfunc = NULL; /* no predictor routine */
    return 1;
}

int TIFFPredictorCleanup(TIFF *tif)
{
    TIFFPredictorState *sp = PredictorState(tif);

    assert(sp != 0);

    tif->tif_tagmethods.vgetfield = sp->vgetparent;
    tif->tif_tagmethods.vsetfield = sp->vsetparent;
    tif->tif_tagmethods.printdir = sp->printdir;
    tif->tif_setupdecode = sp->setupdecode;
    tif->tif_setupencode = sp->setupencode;

    return 1;
}

/**
 * @def PredictorState(tif)
 * @brief Borrow the predictor prefix of codec-owned tif_data.
 * @ingroup libtiff_private_predictor
 *
 * Expands to a cast of tif->tif_data to TIFFPredictorState *, evaluating tif
 * once. Valid only for codecs whose state struct begins with a
 * TIFFPredictorState member and that have called TIFFPredictorInit; nothing
 * is checked.
 *
 * @param[in] tif TIFF handle whose codec state is borrowed.
 */

/**
 * @def REPEAT4(n, op)
 * @brief Repeat a statement using an unrolled small-count switch.
 * @ingroup libtiff_private_predictor
 *
 * Expands to a switch on n whose cases fall through, so op runs n times for
 * n from 0 to 4 and, through a loop in the default case, n times for larger
 * n. A negative n reaches the default case and runs op four times. n is
 * evaluated twice. The default case declares a block-local tmsize_t i, so op
 * must not refer to an outer i.
 *
 * @param[in] n Repeat count, normally the stride; evaluated twice.
 * @param[in] op Statement to repeat.
 */

/**
 * @def OP
 * @brief Accumulate the next byte in the floating-point fast path.
 * @ingroup libtiff_private_predictor
 *
 * Local to the stride-1 path of fpAcc: adds the byte at cp[0] to cp[1]
 * modulo 256 and advances the enclosing cp by one. Undefined again after
 * that loop.
 */

/**
 * @def FIELD_PREDICTOR
 * @brief Reserve the codec field bit for Predictor.
 * @ingroup libtiff_private_predictor
 *
 * Value FIELD_CODEC + 0, the first codec-private field bit. Used in
 * predictFields, set by PredictorVSetField when Predictor is assigned, and
 * tested by PredictorPrintDir.
 */

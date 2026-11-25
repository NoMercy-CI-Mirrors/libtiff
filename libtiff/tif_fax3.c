/*
 * Copyright (c) 1990-1997 Sam Leffler
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
 * @file tif_fax3.c
 * @brief Implement Group3 Group4 and aligned modified-Huffman fax coding.
 */

#include "tiffiop.h"
#ifdef CCITT_SUPPORT
/*
 * TIFF Library.
 *
 * CCITT Group 3 (T.4) and Group 4 (T.6) Compression Support.
 *
 * This file contains support for decoding and encoding TIFF
 * compression algorithms 2, 3, 4, and 32771.
 *
 * Decoder support is derived, with permission, from the code
 * in Frank Cringle's viewfax program;
 *      Copyright (C) 1990, 1995  Frank D. Cringle.
 */
#include "tif_fax3.h"
#define G3CODES
#include "t4.h"
#include <stdio.h>

#ifndef EOF_REACHED_COUNT_THRESHOLD
/* Arbitrary threshold to avoid corrupted single-strip files with extremely
 * large imageheight to cause apparently endless looping, such as in
 * https://gitlab.com/libtiff/libtiff/-/issues/583
 */
#define EOF_REACHED_COUNT_THRESHOLD 8192
#endif

/*
 * Compression+decompression state blocks are
 * derived from this ``base state'' block.
 */
/**
 * @brief Prefix fax state with geometry options and saved method chains.
 * @ingroup libtiff_private_fax
 *
 * First member of Fax3CodecState; Fax3State() casts tif_data to this type.
 * It owns no buffers. The parent tag and print methods are borrowed and
 * restored during cleanup.
 */
typedef struct
{
    /** @brief Open mode saved during codec initialisation. */
    int rw_mode;
    /** @brief FAXMODE control bits. */
    int mode;
    /** @brief Positive decoded scanline/tile-row byte extent after setup. */
    tmsize_t rowbytes;
    /** @brief Pixels per scanline/tile row, within signed decoder limits. */
    uint32_t rowpixels;

    /** @brief Cached CleanFaxData tag value. */
    uint16_t cleanfaxdata;
    /** @brief Cached consecutive bad fax line count. */
    uint32_t badfaxrun;
    /** @brief Cached total bad fax line count. */
    uint32_t badfaxlines;
    /** @brief Cached Group3/4 options for the selected scheme. */
    uint32_t groupoptions;

    /** @brief Borrowed parent tag getter restored during cleanup. */
    TIFFVGetMethod vgetparent;
    /** @brief Borrowed parent tag setter restored during cleanup. */
    TIFFVSetMethod vsetparent;
    /** @brief Borrowed optional parent printer restored during cleanup. */
    TIFFPrintMethod printdir;
} Fax3BaseState;
#define Fax3State(tif) ((Fax3BaseState *)(tif)->tif_data)

/**
 * @brief Select the next Group3 encoded row dimensionality.
 * @ingroup libtiff_private_fax
 */
typedef enum
{
    /** @brief Encode the next row independently. */
    G3_1D,
    /** @brief Encode the next row relative to its predecessor. */
    G3_2D
} Ttag;
/**
 * @brief Own fax run/reference buffers and bit-stream progress.
 * @ingroup libtiff_private_fax
 *
 * Allocated as tif_data by InitCCITTFax3() and freed by Fax3Cleanup(). It
 * owns the runs and refline allocations; bitmap and fill are borrowed.
 */
typedef struct
{
    /** @brief Shared base prefix, first in codec state. */
    Fax3BaseState b;

    /**
     * @brief Borrowed immutable byte reversal table selected for current
     * FillOrder.
     */
    const unsigned char *bitmap;
    /** @brief Cached decoder bit accumulator or encoder partial byte. */
    uint32_t data;
    /** @brief Valid input bits or remaining output bit positions. */
    int bit;
    /** @brief Recognised end-of-line count/state. */
    int EOLcnt;
    /** @brief Repeated EOF recovery count in the current strile. */
    int eofReachedCount;
    /** @brief Repeated bad-length recovery count in the current strile. */
    int eolReachedCount;
    /** @brief Repeated invalid-code recovery count in the current strile. */
    int unexpectedReachedCount;
    /**
     * @brief Borrowed synchronous run renderer; application code/context must
     * remain live.
     */
    TIFFFaxFillFunc fill;
    /**
     * @brief Owned Ext allocation holding current and optional reference run
     * arrays.
     */
    uint32_t *runs;
    /** @brief Capacity of each run-array partition in uint32_t entries. */
    uint32_t nruns;
    /**
     * @brief Borrowed reference partition within runs, or NULL for
     * one-dimensional state.
     */
    uint32_t *refruns;
    /**
     * @brief Borrowed current partition within runs; swapped with reference in
     * 2D decoding.
     */
    uint32_t *curruns;

    /** @brief Next Group3 row dimensionality. */
    Ttag tag;
    /** @brief Owned Ext packed reference row used by 2D encoding. */
    unsigned char *refline;
    /**
     * @brief Remaining two-dimensional rows before another one-dimensional row.
     */
    int k;
    /** @brief Resolution-derived maximum dimensional schedule interval. */
    int maxk;

    /** @brief Current line count within the strile. */
    int line;
} Fax3CodecState;
#define DecoderState(tif) ((Fax3CodecState *)Fax3State(tif))
#define EncoderState(tif) ((Fax3CodecState *)Fax3State(tif))

#define is2DEncoding(sp) (sp->b.groupoptions & GROUP3OPT_2DENCODING)
#define isAligned(p, t) ((((size_t)(p)) & (sizeof(t) - 1)) == 0)

/*
 * Group 3 and Group 4 Decoding.
 */

/*
 * These macros glue the TIFF library state to
 * the state expected by Frank's decoder.
 */
#define DECLARE_STATE(tif, sp, mod)                                            \
    static const char module[] = mod;                                          \
    Fax3CodecState *sp = DecoderState(tif);                                    \
    int a0;                                   /* reference element */          \
    int lastx = (int)sp->b.rowpixels;         /* last element in row */        \
    uint32_t BitAcc;                          /* bit accumulator */            \
    int BitsAvail;                            /* # valid bits in BitAcc */     \
    int RunLength;                            /* length of current run */      \
    unsigned char *cp;                        /* next byte of input data */    \
    unsigned char *ep;                        /* end of input data */          \
    uint32_t *pa;                             /* place to stuff next run */    \
    uint32_t *thisrun;                        /* current row's run array */    \
    int EOLcnt;                               /* # EOL codes recognized */     \
    const unsigned char *bitmap = sp->bitmap; /* input data bit reverser */    \
    const TIFFFaxTabEnt *TabEnt

#define DECLARE_STATE_2D(tif, sp, mod)                                         \
    DECLARE_STATE(tif, sp, mod);                                               \
    int b1; /* next change on prev line */                                     \
    uint32_t                                                                   \
        *pb /* next run in reference line */ /*                                \
                                              * Load any state that may be     \
                                              * changed during decoding.       \
                                              */
#define CACHE_STATE(tif, sp)                                                   \
    do                                                                         \
    {                                                                          \
        BitAcc = sp->data;                                                     \
        BitsAvail = sp->bit;                                                   \
        EOLcnt = sp->EOLcnt;                                                   \
        cp = (unsigned char *)tif->tif_rawcp;                                  \
        ep = cp + tif->tif_rawcc;                                              \
    } while (0)
/*
 * Save state possibly changed during decoding.
 */
#define UNCACHE_STATE(tif, sp)                                                 \
    do                                                                         \
    {                                                                          \
        sp->bit = BitsAvail;                                                   \
        sp->data = BitAcc;                                                     \
        sp->EOLcnt = EOLcnt;                                                   \
        tif->tif_rawcc -= (tmsize_t)((uint8_t *)cp - tif->tif_rawcp);          \
        tif->tif_rawcp = (uint8_t *)cp;                                        \
    } while (0)

/*
 * Setup state for decoding a strip.
 */
/**
 * @brief Reset fax bit state run pointers and recovery counters.
 * @ingroup libtiff_private_fax
 *
 * Requires allocated runs and valid positive row geometry. Clears the bit
 * accumulator, EOL count and recovery counters, selects a borrowed
 * bit-reversal table from the current FillOrder and sets the line index to
 * zero. Repoints curruns to the start of runs without clearing it; when a
 * reference partition exists, re-anchors refruns at runs + nruns (undoing
 * any earlier swap) and initialises it to one all-white run. No
 * allocation, client I/O, diagnostics or caller ownership transfer.
 *
 * @param[in,out] tif Non-NULL live fax handle with matching geometry/state;
 * serialise operations.
 * @param[in] s Unused sample-plane compatibility argument.
 * @return One unconditionally.
 */
static int Fax3PreDecode(TIFF *tif, uint16_t s)
{
    Fax3CodecState *sp = DecoderState(tif);

    (void)s;
    assert(sp != NULL);
    sp->bit = 0; /* force initial read */
    sp->data = 0;
    sp->EOLcnt = 0; /* force initial scan for EOL */
    sp->eofReachedCount = 0;
    sp->eolReachedCount = 0;
    sp->unexpectedReachedCount = 0;
    /*
     * Decoder assumes lsb-to-msb bit order.  Note that we select
     * this here rather than in Fax3SetupState so that viewers can
     * hold the image open, fiddle with the FillOrder tag value,
     * and then re-decode the image.  Otherwise they'd need to close
     * and open the image to get the state reset.
     */
    sp->bitmap =
        TIFFGetBitRevTable(tif->tif_dir.td_fillorder != FILLORDER_LSB2MSB);
    sp->curruns = sp->runs;
    if (sp->refruns)
    { /* init reference line to white */
        sp->refruns = sp->runs + sp->nruns;
        sp->refruns[0] = (uint32_t)sp->b.rowpixels;
        sp->refruns[1] = 0;
    }
    sp->line = 0;
    return (1);
}

/*
 * Routine for handling various errors/conditions.
 * Note how they are "glued into the decoder" by
 * overriding the definitions used by the decoder.
 */

/**
 * @brief Diagnose an invalid fax code word.
 * @ingroup libtiff_private_fax
 *
 * Reports an error through the handle with the row and pixel position. Does
 * not itself increment recovery counters (the wrapping macro does),
 * allocate, access payload or return status; diagnostic handlers can have
 * application effects.
 *
 * @param[in] module Borrowed diagnostic label.
 * @param[in] tif Handle that receives the diagnostic and supplies the
 * current strip or tile number.
 * @param[in] line Row number within the strile for diagnostics.
 * @param[in] a0 Pixel position of the bad code word.
 */
static void Fax3Unexpected(const char *module, TIFF *tif, uint32_t line,
                           uint32_t a0)
{
    TIFFErrorExtR(
        tif, module,
        "Bad code word at line %" PRIu32 " of %s %" PRIu32 " (x %" PRIu32 ")",
        line, isTiled(tif) ? "tile" : "strip",
        (isTiled(tif) ? tif->tif_dir.td_curtile : tif->tif_dir.td_curstrip),
        a0);
}
#define unexpected(table, a0)                                                  \
    do                                                                         \
    {                                                                          \
        Fax3Unexpected(module, tif, (uint32_t)sp->line, (uint32_t)(a0));       \
        ++sp->unexpectedReachedCount;                                          \
    } while (0)

/**
 * @brief Diagnose unsupported fax uncompressed extension data.
 * @ingroup libtiff_private_fax
 *
 * Reports an error through the handle with the row and pixel position. Does
 * not itself increment recovery counters (the wrapping macro does),
 * allocate, access payload or return status; diagnostic handlers can have
 * application effects.
 *
 * @param[in] module Borrowed diagnostic label.
 * @param[in] tif Handle that receives the diagnostic and supplies the
 * current strip or tile number.
 * @param[in] line Row number within the strile for diagnostics.
 * @param[in] a0 Pixel position of the extension code.
 */
static void Fax3Extension(const char *module, TIFF *tif, uint32_t line,
                          uint32_t a0)
{
    TIFFErrorExtR(
        tif, module,
        "Uncompressed data (not supported) at line %" PRIu32 " of %s %" PRIu32
        " (x %" PRIu32 ")",
        line, isTiled(tif) ? "tile" : "strip",
        (isTiled(tif) ? tif->tif_dir.td_curtile : tif->tif_dir.td_curstrip),
        a0);
}
#define extension(a0)                                                          \
    Fax3Extension(module, tif, (uint32_t)sp->line, (uint32_t)(a0))

/**
 * @brief Warn about a fax row-length mismatch.
 * @ingroup libtiff_private_fax
 *
 * Reports a warning through the handle with the row and pixel position. Does
 * not itself increment recovery counters (the wrapping macro does),
 * allocate, access payload or return status; diagnostic handlers can have
 * application effects.
 *
 * @param[in] module Borrowed diagnostic label.
 * @param[in] tif Handle that receives the diagnostic and supplies the
 * current strip or tile number.
 * @param[in] line Row number within the strile for diagnostics.
 * @param[in] a0 Decoded row length in pixels.
 * @param[in] lastx Expected row width in pixels.
 */
static void Fax3BadLength(const char *module, TIFF *tif, uint32_t line,
                          uint32_t a0, uint32_t lastx)
{
    TIFFWarningExtR(
        tif, module,
        "%s at line %" PRIu32 " of %s %" PRIu32 " (got %" PRIu32
        ", expected %" PRIu32 ")",
        a0 < lastx ? "Premature EOL" : "Line length mismatch", line,
        isTiled(tif) ? "tile" : "strip",
        (isTiled(tif) ? tif->tif_dir.td_curtile : tif->tif_dir.td_curstrip), a0,
        lastx);
}
#define badlength(a0, lastx)                                                   \
    do                                                                         \
    {                                                                          \
        Fax3BadLength(module, tif, (uint32_t)sp->line, (uint32_t)(a0),         \
                      (uint32_t)(lastx));                                      \
        ++sp->eolReachedCount;                                                 \
    } while (0)

/**
 * @brief Warn about premature fax input exhaustion.
 * @ingroup libtiff_private_fax
 *
 * Reports a warning through the handle with the row and pixel position. Does
 * not itself increment recovery counters (the wrapping macro does),
 * allocate, access payload or return status; diagnostic handlers can have
 * application effects.
 *
 * @param[in] module Borrowed diagnostic label.
 * @param[in] tif Handle that receives the diagnostic and supplies the
 * current strip or tile number.
 * @param[in] line Row number within the strile for diagnostics.
 * @param[in] a0 Pixel position reached in the row.
 */
static void Fax3PrematureEOF(const char *module, TIFF *tif, uint32_t line,
                             uint32_t a0)
{
    TIFFWarningExtR(
        tif, module,
        "Premature EOF at line %" PRIu32 " of %s %" PRIu32 " (x %" PRIu32 ")",
        line, isTiled(tif) ? "tile" : "strip",
        (isTiled(tif) ? tif->tif_dir.td_curtile : tif->tif_dir.td_curstrip),
        a0);
}
#define prematureEOF(a0)                                                       \
    do                                                                         \
    {                                                                          \
        Fax3PrematureEOF(module, tif, (uint32_t)sp->line, (uint32_t)(a0));     \
        ++sp->eofReachedCount;                                                 \
    } while (0)

/**
 * @brief Warn that Group3 recovery will retry without EOL.
 * @ingroup libtiff_private_fax
 *
 * Reports a warning through the handle with the row and pixel position. Does
 * not itself increment recovery counters (the wrapping macro does),
 * allocate, access payload or return status; diagnostic handlers can have
 * application effects.
 *
 * @param[in] module Borrowed diagnostic label.
 * @param[in] tif Handle that receives the diagnostic and supplies the
 * current strip or tile number.
 * @param[in] line Row number within the strile for diagnostics.
 * @param[in] a0 Pixel position reached in the row.
 */
static void Fax3TryG3WithoutEOL(const char *module, TIFF *tif, uint32_t line,
                                uint32_t a0)
{
    TIFFWarningExtR(
        tif, module,
        "Try to decode (read) fax Group 3 data without EOL at line %" PRIu32
        " of %s %" PRIu32 " (x %" PRIu32 "). Please check result",
        line, isTiled(tif) ? "tile" : "strip",
        (isTiled(tif) ? tif->tif_dir.td_curtile : tif->tif_dir.td_curstrip),
        a0);
}
#define tryG3WithoutEOL(a0)                                                    \
    do                                                                         \
    {                                                                          \
        Fax3TryG3WithoutEOL(module, tif, (uint32_t)sp->line, (uint32_t)(a0));  \
    } while (0)

/**
 * @brief Stop fax recovery after repeated EOF EOL or bad-code conditions.
 * @ingroup libtiff_private_fax
 *
 * Compares three recovery counters with EOF_REACHED_COUNT_THRESHOLD and
 * diagnoses the first exhausted threshold. Does not increment counters or
 * allocate/read/write payload; no ownership transfer.
 *
 * @param[in,out] tif Non-NULL live fax handle with matching geometry/state;
 * serialise operations.
 * @param[in] module Borrowed diagnostic label.
 * @param[in] sp Borrowed live fax decoder state with recovery counters.
 * @return Minus one when a threshold is reached, zero otherwise.
 */
static int CheckReachedCounters(TIFF *tif, const char *module,
                                Fax3CodecState *sp)
{
    if (sp->eofReachedCount >= EOF_REACHED_COUNT_THRESHOLD)
    {
        TIFFErrorExtR(tif, module,
                      "End of file (EOF) has already been reached %d times "
                      "within that %s.",
                      sp->eofReachedCount, isTiled(tif) ? "tile" : "strip");
        return (-1);
    }
    if (sp->eolReachedCount >= EOF_REACHED_COUNT_THRESHOLD)
    {
        TIFFErrorExtR(tif, module,
                      "Bad line length (EOL) has already been reached %d times "
                      "within that %s",
                      sp->eolReachedCount, isTiled(tif) ? "tile" : "strip");
        return (-1);
    }
    if (sp->unexpectedReachedCount >= EOF_REACHED_COUNT_THRESHOLD)
    {
        TIFFErrorExtR(tif, module,
                      "Bad code word (unexpected) has already been reached %d "
                      "times within that %s",
                      sp->unexpectedReachedCount,
                      isTiled(tif) ? "tile" : "strip");
        return (-1);
    }
    return (0);
}

/**
 * @brief Decode complete Group3 one-dimensional fax rows.
 * @ingroup libtiff_private_fax
 *
 * Requires setup/predecode, positive rowbytes, signed-representable pixel
 * coordinates, nonnegative complete-row occ and valid run capacity. Expands
 * cached codes through state macros and invokes the installed fill callback
 * synchronously; NULL buf is valid only for a custom callback that
 * explicitly supports it. Can recover bad row lengths or missing EOL with
 * diagnostics and counter limits. Updates the current run array, line/bit
 * state and raw cursors; failure can leave a partially filled current row
 * and earlier output. No direct allocation/client I/O or ownership
 * transfer; a custom filler has its own effects. Negative error status is
 * not a Boolean success result.
 *
 * @param[in,out] tif Non-NULL live fax handle with matching geometry/state;
 * serialise operations.
 * @param[out] buf Borrowed decoded row storage of occ bytes, or NULL only
 * with a compatible custom fill callback.
 * @param[in] occ Nonnegative complete-row decoded extent.
 * @param[in] s Unused sample-plane compatibility argument.
 * @return One on normal completion, minus one on detected
 * extent/counter/decoding failure.
 */
static int Fax3Decode1D(TIFF *tif, uint8_t *buf, tmsize_t occ, uint16_t s)
{
    DECLARE_STATE(tif, sp, "Fax3Decode1D");
    (void)s;
    if (occ % sp->b.rowbytes)
    {
        TIFFErrorExtR(tif, module, "Fractional scanlines cannot be read");
        return (-1);
    }
    if (CheckReachedCounters(tif, module, sp))
        return (-1);
RETRY_WITHOUT_EOL_1D:
    CACHE_STATE(tif, sp);
    thisrun = sp->curruns;
    while (occ > 0)
    {
        a0 = 0;
        RunLength = 0;
        pa = thisrun;
#ifdef FAX3_DEBUG
        printf("\nBitAcc=%08" PRIX32 ", BitsAvail = %d\n", BitAcc, BitsAvail);
        printf("-------------------- %" PRIu32 "\n", tif->tif_dir.td_row);
        fflush(stdout);
#endif
        SYNC_EOL(EOF1D, RETRY_WITHOUT_EOL_1D);
        EXPAND1D(EOF1Da);
        (*sp->fill)(buf, thisrun, pa, (uint32_t)lastx);
        buf += sp->b.rowbytes;
        occ -= sp->b.rowbytes;
        sp->line++;
        continue;
    EOF1D: /* premature EOF */
        CLEANUP_RUNS();
    EOF1Da: /* premature EOF */
        (*sp->fill)(buf, thisrun, pa, (uint32_t)lastx);
        UNCACHE_STATE(tif, sp);
        return (-1);
    }
    UNCACHE_STATE(tif, sp);
    return (1);
}

#define SWAP(t, a, b)                                                          \
    {                                                                          \
        t x;                                                                   \
        x = (a);                                                               \
        (a) = (b);                                                             \
        (b) = x;                                                               \
    }
/*
 * Decode the requested amount of G3 2D-encoded data.
 */
/**
 * @brief Decode complete Group3 mixed-dimensional fax rows.
 * @ingroup libtiff_private_fax
 *
 * Requires setup/predecode, positive rowbytes, signed-representable pixel
 * coordinates, nonnegative complete-row occ and valid run/reference
 * capacities. Expands cached codes through state macros and invokes the
 * installed fill callback synchronously; NULL buf is valid only for a
 * custom callback that explicitly supports it. Can recover bad row lengths
 * or missing EOL with diagnostics and counter limits. Updates run/reference
 * arrays, line/bit state and raw cursors; failure can leave a partially
 * filled current row and earlier output. No direct allocation/client I/O or
 * ownership transfer; a custom filler has its own effects. Negative error
 * status is not a Boolean success result.
 *
 * @param[in,out] tif Non-NULL live fax handle with matching geometry/state;
 * serialise operations.
 * @param[out] buf Borrowed decoded row storage of occ bytes, or NULL only
 * with a compatible custom fill callback.
 * @param[in] occ Nonnegative complete-row decoded extent.
 * @param[in] s Unused sample-plane compatibility argument.
 * @return One on normal completion, minus one on detected
 * extent/counter/decoding failure.
 */
static int Fax3Decode2D(TIFF *tif, uint8_t *buf, tmsize_t occ, uint16_t s)
{
    DECLARE_STATE_2D(tif, sp, "Fax3Decode2D");
    int is1D; /* current line is 1d/2d-encoded */
    (void)s;
    if (occ % sp->b.rowbytes)
    {
        TIFFErrorExtR(tif, module, "Fractional scanlines cannot be read");
        return (-1);
    }
    if (CheckReachedCounters(tif, module, sp))
        return (-1);
RETRY_WITHOUT_EOL_2D:
    CACHE_STATE(tif, sp);
    while (occ > 0)
    {
        a0 = 0;
        RunLength = 0;
        pa = thisrun = sp->curruns;
#ifdef FAX3_DEBUG
        printf("\nBitAcc=%08" PRIX32 ", BitsAvail = %d EOLcnt = %d", BitAcc,
               BitsAvail, EOLcnt);
#endif
        SYNC_EOL(EOF2D, RETRY_WITHOUT_EOL_2D);
        NeedBits8(1, EOF2D);
        is1D = GetBits(1); /* 1D/2D-encoding tag bit */
        ClrBits(1);
#ifdef FAX3_DEBUG
        printf(" %s\n-------------------- %" PRIu32 "\n", is1D ? "1D" : "2D",
               tif->tif_dir.td_row);
        fflush(stdout);
#endif
        pb = sp->refruns;
        b1 = (int)*pb++;
        if (is1D)
            EXPAND1D(EOF2Da);
        else
            EXPAND2D(EOF2Da);
        (*sp->fill)(buf, thisrun, pa, (uint32_t)lastx);
        if (pa < thisrun + sp->nruns)
        {
            SETVALUE(0); /* imaginary change for reference */
        }
        SWAP(uint32_t *, sp->curruns, sp->refruns);
        buf += sp->b.rowbytes;
        occ -= sp->b.rowbytes;
        sp->line++;
        continue;
    EOF2D: /* premature EOF */
        CLEANUP_RUNS();
    EOF2Da: /* premature EOF */
        (*sp->fill)(buf, thisrun, pa, (uint32_t)lastx);
        UNCACHE_STATE(tif, sp);
        return (-1);
    }
    UNCACHE_STATE(tif, sp);
    return (1);
}
#undef SWAP

#define FILL(n, cp)                                                            \
    for (int32_t ifill = 0; ifill < (n); ++ifill)                              \
    {                                                                          \
        (cp)[ifill] = 0xff;                                                    \
    }                                                                          \
    (cp) += (n);

#define ZERO(n, cp)                                                            \
    for (int32_t izero = 0; izero < (n); ++izero)                              \
    {                                                                          \
        (cp)[izero] = 0;                                                       \
    }                                                                          \
    (cp) += (n);

/*
 * Bit-fill a row according to the white/black
 * runs generated during G3/G4 decoding.
 */
void _TIFFFax3fillruns(unsigned char *buf, uint32_t *runs, uint32_t *erun,
                       uint32_t lastx)
{
    static const unsigned char _fillmasks[] = {0x00, 0x80, 0xc0, 0xe0, 0xf0,
                                               0xf8, 0xfc, 0xfe, 0xff};
    unsigned char *cp;
    uint32_t x, bx, run;
    int32_t n, nw;
    int64_t *lp;

    if ((erun - runs) & 1)
        *erun++ = 0;
    x = 0;
    for (; runs < erun; runs += 2)
    {
        run = runs[0];
        if (x + run > lastx || run > lastx)
            run = runs[0] = (uint32_t)(lastx - x);
        if (run)
        {
            cp = buf + (x >> 3);
            bx = x & 7;
            if (run > 8 - bx)
            {
                if (bx)
                { /* align to byte boundary */
                    *cp++ &= (unsigned char)(0xff << (8 - bx));
                    run -= 8 - bx;
                }
                if ((n = (int32_t)(run >> 3)) != 0)
                { /* multiple bytes to fill */
                    if (((size_t)n / sizeof(int64_t)) > 1)
                    {
                        /*
                         * Align to int64_tword boundary and fill.
                         */
                        for (; n && !isAligned(cp, int64_t); n--)
                            *cp++ = 0x00;
                        lp = (int64_t *)cp;
                        nw = (int32_t)((size_t)n / sizeof(int64_t));
                        n -= (int32_t)((size_t)nw * sizeof(int64_t));
                        do
                        {
                            *lp++ = 0L;
                        } while (--nw);
                        cp = (unsigned char *)lp;
                    }
                    ZERO(n, cp);
                    run &= 7;
                }
                if (run)
                    cp[0] &= (unsigned char)(0xff >> run);
            }
            else
                cp[0] &= (unsigned char)~(_fillmasks[run] >> bx);
            x += runs[0];
        }
        run = runs[1];
        if (x + run > lastx || run > lastx)
            run = runs[1] = lastx - x;
        if (run)
        {
            cp = buf + (x >> 3);
            bx = x & 7;
            if (run > 8 - bx)
            {
                if (bx)
                { /* align to byte boundary */
                    *cp++ |= (unsigned char)(0xff >> bx);
                    run -= 8 - bx;
                }
                if ((n = (int32_t)(run >> 3)) != 0)
                { /* multiple bytes to fill */
                    if (((size_t)n / sizeof(int64_t)) > 1)
                    {
                        /*
                         * Align to int64_t boundary and fill.
                         */
                        for (; n && !isAligned(cp, int64_t); n--)
                            *cp++ = 0xff;
                        lp = (int64_t *)cp;
                        nw = (int32_t)((size_t)n / sizeof(int64_t));
                        n -= (int32_t)((size_t)nw * sizeof(int64_t));
                        do
                        {
                            *lp++ = -1L;
                        } while (--nw);
                        cp = (unsigned char *)lp;
                    }
                    FILL(n, cp);
                    run &= 7;
                }
                /* Explicit 0xff masking to make icc -check=conversions happy */
                if (run)
                    cp[0] = (unsigned char)((cp[0] | (0xff00 >> run)) & 0xff);
            }
            else
                cp[0] |= (unsigned char)(_fillmasks[run] >> bx);
            x += runs[1];
        }
    }
    assert(x == lastx);
}
#undef ZERO
#undef FILL

/**
 * @brief Accept fax tag fixup without changes.
 * @ingroup libtiff_private_fax
 *
 * Ignores the handle; no allocation, I/O, diagnostics, state or ownership
 * effects.
 *
 * @param[in] tif Unused handle.
 * @return One unconditionally.
 */
static int Fax3FixupTags(TIFF *tif)
{
    (void)tif;
    return (1);
}

/*
 * Setup G3/G4-related compression/decompression state
 * before data is processed.  This routine is called once
 * per image -- it sets up different state based on whether
 * or not decoding or encoding is being done and whether
 * 1D- or 2D-encoded data is involved.
 */
/**
 * @brief Validate bilevel geometry and allocate fax run/reference storage.
 * @ingroup libtiff_private_fax
 *
 * Requires one-bit single-sample or separate-plane data and consistent row
 * bytes/pixels. Caches row extent, replaces owned run arrays with checked
 * rounded counts and optionally replaces the reference line. Changes
 * decoder dispatch for Group3 2D options. Detected extent/allocation
 * failures diagnose and can leave earlier buffers freed, cached geometry
 * changed or partial new state retained for cleanup. Requires representable
 * rowpixels+1 and signed decoder coordinates in addition to the explicit
 * checks. No payload I/O or caller ownership transfer.
 *
 * @param[in,out] tif Non-NULL live fax handle with matching geometry/state;
 * serialise operations.
 * @return One after setup, zero on detected geometry/extent/allocation
 * failure.
 */
static int Fax3SetupState(TIFF *tif)
{
    static const char module[] = "Fax3SetupState";
    TIFFDirectory *td = &tif->tif_dir;
    Fax3BaseState *sp = Fax3State(tif);
    int needsRefLine;
    Fax3CodecState *dsp = (Fax3CodecState *)Fax3State(tif);
    tmsize_t rowbytes;
    uint32_t rowpixels;

    if (td->td_bitspersample != 1)
    {
        TIFFErrorExtR(tif, module,
                      "Bits/sample must be 1 for Group 3/4 encoding/decoding");
        return (0);
    }
    if (td->td_samplesperpixel != 1 &&
        td->td_planarconfig != PLANARCONFIG_SEPARATE)
    {
        TIFFErrorExtR(
            tif, module,
            "Samples/pixel shall be 1 for Group 3/4 encoding/decoding, "
            "or PlanarConfiguration must be set to Separate.");
        return 0;
    }
    /*
     * Calculate the scanline/tile widths.
     */
    if (isTiled(tif))
    {
        rowbytes = TIFFTileRowSize(tif);
        rowpixels = td->td_tilewidth;
    }
    else
    {
        rowbytes = TIFFScanlineSize(tif);
        rowpixels = td->td_imagewidth;
    }
    if ((int64_t)rowbytes < ((int64_t)rowpixels + 7) / 8)
    {
        TIFFErrorExtR(tif, module,
                      "Inconsistent number of bytes per row : rowbytes=%" PRId64
                      " rowpixels=%" PRIu32,
                      (int64_t)rowbytes, rowpixels);
        return (0);
    }
    sp->rowbytes = rowbytes;
    sp->rowpixels = rowpixels;
    /*
     * Allocate any additional space required for decoding/encoding.
     */
    needsRefLine = ((sp->groupoptions & GROUP3OPT_2DENCODING) ||
                    td->td_compression == COMPRESSION_CCITTFAX4);

    /*
      Assure that allocation computations do not overflow.

      TIFFroundup and TIFFSafeMultiply return zero on integer overflow
    */
    if (dsp->runs != NULL)
    {
        _TIFFfreeExt(tif, dsp->runs);
        dsp->runs = (uint32_t *)NULL;
    }
    dsp->nruns = TIFFroundup_32(rowpixels + 1, 32);
    if (needsRefLine)
    {
        dsp->nruns = TIFFSafeMultiply(uint32_t, dsp->nruns, 2);
    }
    if ((dsp->nruns == 0) || (TIFFSafeMultiply(uint32_t, dsp->nruns, 2) == 0))
    {
        TIFFErrorExtR(tif, tif->tif_name,
                      "Row pixels integer overflow (rowpixels %" PRIu32 ")",
                      rowpixels);
        return (0);
    }
    dsp->runs = (uint32_t *)_TIFFCheckMalloc(
        tif, TIFFSafeMultiply(uint32_t, dsp->nruns, 2), sizeof(uint32_t),
        "for Group 3/4 run arrays");
    if (dsp->runs == NULL)
        return (0);
    memset(dsp->runs, 0,
           TIFFSafeMultiply(uint32_t, dsp->nruns, 2) * sizeof(uint32_t));
    dsp->curruns = dsp->runs;
    if (needsRefLine)
        dsp->refruns = dsp->runs + dsp->nruns;
    else
        dsp->refruns = NULL;
    if (td->td_compression == COMPRESSION_CCITTFAX3 && is2DEncoding(dsp))
    { /* NB: default is 1D routine */
        tif->tif_decoderow = Fax3Decode2D;
        tif->tif_decodestrip = Fax3Decode2D;
        tif->tif_decodetile = Fax3Decode2D;
    }

    if (needsRefLine)
    { /* 2d encoding */
        Fax3CodecState *esp = EncoderState(tif);
        /*
         * 2d encoding requires a scanline
         * buffer for the ``reference line''; the
         * scanline against which delta encoding
         * is referenced.  The reference line must
         * be initialized to be ``white'' (done elsewhere).
         */
        if (esp->refline != NULL)
        {
            _TIFFfreeExt(tif, esp->refline);
        }
        esp->refline = (unsigned char *)_TIFFmallocExt(tif, rowbytes);
        if (esp->refline == NULL)
        {
            TIFFErrorExtR(tif, module, "No space for Group 3/4 reference line");
            return (0);
        }
    }
    else /* 1d encoding */
        EncoderState(tif)->refline = NULL;

    return (1);
}

/*
 * CCITT Group 3 FAX Encoding.
 */

#define Fax3FlushBits(tif, sp)                                                 \
    {                                                                          \
        if ((tif)->tif_rawcc >= (tif)->tif_rawdatasize)                        \
        {                                                                      \
            if (!TIFFFlushData1(tif))                                          \
                return 0;                                                      \
        }                                                                      \
        *(tif)->tif_rawcp++ = (uint8_t)(sp)->data;                             \
        (tif)->tif_rawcc++;                                                    \
        (sp)->data = 0, (sp)->bit = 8;                                         \
    }
#define _FlushBits(tif)                                                        \
    {                                                                          \
        if ((tif)->tif_rawcc >= (tif)->tif_rawdatasize)                        \
        {                                                                      \
            if (!TIFFFlushData1(tif))                                          \
                return 0;                                                      \
        }                                                                      \
        *(tif)->tif_rawcp++ = (uint8_t)data;                                   \
        (tif)->tif_rawcc++;                                                    \
        data = 0, bit = 8;                                                     \
    }
/**
 * @brief Mask the requested low bit count for encoder code packing.
 * @ingroup libtiff_private_fax
 *
 * Immutable library-lifetime storage; no ownership transfer.
 */
static const int _msbmask[9] = {0x00, 0x01, 0x03, 0x07, 0x0f,
                                0x1f, 0x3f, 0x7f, 0xff};
#define _PutBits(tif, bits, length)                                            \
    {                                                                          \
        while (length > bit)                                                   \
        {                                                                      \
            data |= (int)((unsigned int)bits >> (length - bit));               \
            length -= bit;                                                     \
            _FlushBits(tif);                                                   \
        }                                                                      \
        assert(length < 9);                                                    \
        data |= (int)(((unsigned int)bits & (unsigned int)_msbmask[length])    \
                      << (unsigned int)(bit - length));                        \
        bit -= length;                                                         \
        if (bit == 0)                                                          \
            _FlushBits(tif);                                                   \
    }

/*
 * Write a variable-length bit-value to
 * the output stream.  Values are
 * assumed to be at most 16 bits.
 */
/**
 * @brief Append up to sixteen significant fax code bits.
 * @ingroup libtiff_private_fax
 *
 * Requires valid encoder bit accumulator and positive raw capacity. Flushes
 * full bytes/buffers through helper macros, then commits local bit state.
 * Early flush failure can leave local/cached progress inconsistent and file
 * data changed; no ownership transfer.
 *
 * @param[in,out] tif Non-NULL live fax handle with matching geometry/state;
 * serialise operations.
 * @param[in] bits Code value whose low @p length bits are written, most
 * significant first; at most 16 significant bits.
 * @param[in] length Code width at most sixteen bits.
 * @return One after append, zero on flush failure.
 */
static int Fax3PutBits(TIFF *tif, unsigned int bits, unsigned int length)
{
    Fax3CodecState *sp = EncoderState(tif);
    unsigned int bit = (unsigned int)sp->bit;
    int data = (int)sp->data;

    _PutBits(tif, bits, length);

    sp->data = (uint32_t)data;
    sp->bit = (int)bit;
    return 1;
}

/*
 * Write a code to the output stream.
 */
#define putcode(tif, te) Fax3PutBits(tif, (te)->code, (te)->length)

#ifdef FAX3_DEBUG
/**
 * @brief Select the trace label for a Huffman table.
 * @ingroup libtiff_private_fax
 *
 * Defined only with FAX3_DEBUG. Concatenates the string literal @p w with
 * "W" or "B", depending on whether the enclosing local @c tab is
 * TIFFFaxWhiteCodes.
 *
 * @param[in] w String literal prefix.
 */

#define DEBUG_COLOR(w) (tab == TIFFFaxWhiteCodes ? w "W" : w "B")
/**
 * @brief Print code bits and run metadata to stdout.
 * @ingroup libtiff_private_fax
 *
 * Defined only with FAX3_DEBUG and used by putspan(). Prints the enclosing
 * locals data and bit, the label, @p len and the @c length bits of @c code.
 *
 * @param[in] what String literal label passed to DEBUG_COLOR().
 * @param[in] len Run length to print.
 */

#define DEBUG_PRINT(what, len)                                                 \
    {                                                                          \
        int t;                                                                 \
        printf("%08" PRIX32 "/%-2d: %s%5d\t", data, bit, DEBUG_COLOR(what),    \
               len);                                                           \
        for (t = length - 1; t >= 0; t--)                                      \
            putchar(code & (1 << t) ? '1' : '0');                              \
        putchar('\n');                                                         \
    }
#endif

/*
 * Write the sequence of codes that describes
 * the specified span of zero's or one's.  The
 * appropriate table that holds the make-up and
 * terminating codes is supplied.
 */
/**
 * @brief Emit makeup and terminating Huffman codes for one fax run.
 * @ingroup libtiff_private_fax
 *
 * Requires nonnegative representable span and the corresponding complete
 * white/black table. Repeats long makeup codes, emits terminating code and
 * updates bit state. Can flush client output and diagnose; early failure
 * leaves partial state/file effects. No input modification or ownership
 * transfer.
 *
 * @param[in,out] tif Non-NULL live fax handle with matching geometry/state;
 * serialise operations.
 * @param[in] span Nonnegative run length in pixels within signed arithmetic
 * limits.
 * @param[in] tab Borrowed immutable complete white/black Huffman code
 * table.
 * @return One after codes buffered, zero on flush failure.
 */
static int putspan(TIFF *tif, int32_t span, const tableentry *tab)
{
    Fax3CodecState *sp = EncoderState(tif);
    unsigned int bit = (unsigned int)sp->bit;
    int data = (int)sp->data;
    unsigned int code, length;

    while (span >= 2624)
    {
        const tableentry *te = &tab[63 + (2560 >> 6)];
        code = te->code;
        length = te->length;
#ifdef FAX3_DEBUG
        DEBUG_PRINT("MakeUp", te->runlen);
#endif
        _PutBits(tif, code, length);
        span -= te->runlen;
    }
    if (span >= 64)
    {
        const tableentry *te = &tab[63 + (span >> 6)];
        assert(te->runlen == 64 * (span >> 6));
        code = te->code;
        length = te->length;
#ifdef FAX3_DEBUG
        DEBUG_PRINT("MakeUp", te->runlen);
#endif
        _PutBits(tif, code, length);
        span -= te->runlen;
    }
    code = tab[span].code;
    length = tab[span].length;
#ifdef FAX3_DEBUG
    DEBUG_PRINT("  Term", tab[span].runlen);
#endif
    _PutBits(tif, code, length);

    sp->data = (uint32_t)data;
    sp->bit = (int)bit;

    return 1;
}

/*
 * Write an EOL code to the output stream.  The zero-fill
 * logic for byte-aligning encoded scanlines is handled
 * here.  We also handle writing the tag bit for the next
 * scanline when doing 2d encoding.
 */
/**
 * @brief Append Group3 EOL padding and optional dimensional tag.
 * @ingroup libtiff_private_fax
 *
 * Uses fill-bit options to align EOL and emits the next 1D/2D tag where
 * applicable. Requires valid encoder/raw state; output helpers can flush
 * and fail after partial changes. No allocation or ownership transfer.
 *
 * @param[in,out] tif Non-NULL live fax handle with matching geometry/state;
 * serialise operations.
 * @return One after append, zero on flush failure.
 */
static int Fax3PutEOL(TIFF *tif)
{
    Fax3CodecState *sp = EncoderState(tif);
    unsigned int bit = (unsigned int)sp->bit;
    int data = (int)sp->data;
    unsigned int code, length, tparm;

    if (sp->b.groupoptions & GROUP3OPT_FILLBITS)
    {
        /*
         * Force bit alignment so EOL will terminate on
         * a byte boundary.  That is, force the bit alignment
         * to 16-12 = 4 before putting out the EOL code.
         */
        int align = 8 - 4;
        if (align != sp->bit)
        {
            if (align > sp->bit)
                align = sp->bit + (8 - align);
            else
                align = sp->bit - align;
            tparm = (unsigned int)align;
            _PutBits(tif, 0, tparm);
        }
    }
    code = EOL;
    length = 12;
    if (is2DEncoding(sp))
    {
        code = (code << 1) | (sp->tag == G3_1D);
        length++;
    }
    _PutBits(tif, code, length);

    sp->data = (uint32_t)data;
    sp->bit = (int)bit;

    return 1;
}

/*
 * Reset encoding state at the start of a strip.
 */
/**
 * @brief Reset fax encoding bits reference line and dimensional schedule.
 * @ingroup libtiff_private_fax
 *
 * Requires setup-owned buffers. Clears the reference row, selects initial
 * 1D mode and derives the 2D K schedule from vertical resolution
 * (centimetres converted to inches, threshold 150). No allocation, payload
 * I/O, diagnostics or ownership transfer.
 *
 * @param[in,out] tif Non-NULL live fax handle with matching geometry/state;
 * serialise operations.
 * @param[in] s Unused sample-plane compatibility argument.
 * @return One unconditionally.
 */
static int Fax3PreEncode(TIFF *tif, uint16_t s)
{
    Fax3CodecState *sp = EncoderState(tif);

    (void)s;
    assert(sp != NULL);
    sp->bit = 8;
    sp->data = 0;
    sp->tag = G3_1D;
    /*
     * This is necessary for Group 4; otherwise it isn't
     * needed because the first scanline of each strip ends
     * up being copied into the refline.
     */
    if (sp->refline)
        _TIFFmemset(sp->refline, 0x00, sp->b.rowbytes);
    if (is2DEncoding(sp))
    {
        float res = tif->tif_dir.td_yresolution;
        /*
         * The CCITT spec says that when doing 2d encoding, you
         * should only do it on K consecutive scanlines, where K
         * depends on the resolution of the image being encoded
         * (2 for <= 200 lpi, 4 for > 200 lpi).  Since the directory
         * code initializes td_yresolution to 0, this code will
         * select a K of 2 unless the YResolution tag is set
         * appropriately.  (Note also that we fudge a little here
         * and use 150 lpi to avoid problems with units conversion.)
         */
        if (tif->tif_dir.td_resolutionunit == RESUNIT_CENTIMETER)
            res *= 2.54f; /* convert to inches */
        sp->maxk = (res > 150 ? 4 : 2);
        sp->k = sp->maxk - 1;
    }
    else
        sp->k = sp->maxk = 0;
    sp->line = 0;
    return (1);
}

/**
 * @brief Count leading zero bits in each byte.
 * @ingroup libtiff_private_fax
 *
 * Immutable library-lifetime storage; no ownership transfer.
 */
static const unsigned char zeroruns[256] = {
    8, 7, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4, /* 0x00 - 0x0f */
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, /* 0x10 - 0x1f */
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, /* 0x20 - 0x2f */
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, /* 0x30 - 0x3f */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 0x40 - 0x4f */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 0x50 - 0x5f */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 0x60 - 0x6f */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 0x70 - 0x7f */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x80 - 0x8f */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x90 - 0x9f */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0xa0 - 0xaf */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0xb0 - 0xbf */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0xc0 - 0xcf */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0xd0 - 0xdf */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0xe0 - 0xef */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0xf0 - 0xff */
};
/**
 * @brief Count leading one bits in each byte.
 * @ingroup libtiff_private_fax
 *
 * Immutable library-lifetime storage; no ownership transfer.
 */
static const unsigned char oneruns[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x00 - 0x0f */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x10 - 0x1f */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x20 - 0x2f */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x30 - 0x3f */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x40 - 0x4f */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x50 - 0x5f */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x60 - 0x6f */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x70 - 0x7f */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 0x80 - 0x8f */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 0x90 - 0x9f */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 0xa0 - 0xaf */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 0xb0 - 0xbf */
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, /* 0xc0 - 0xcf */
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, /* 0xd0 - 0xdf */
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, /* 0xe0 - 0xef */
    4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 7, 8, /* 0xf0 - 0xff */
};

/*
 * Find a span of ones or zeros using the supplied
 * table.  The ``base'' of the bit string is supplied
 * along with the start+end bit indices.
 */
/**
 * @brief Measure the leading zero bit run in a bounded packed row.
 * @ingroup libtiff_private_fax
 *
 * Requires 0<=bs<=be<=INT32_MAX and live bytes through the last inspected
 * bit. Uses immutable run lookup tables, with no mutation, allocation, I/O,
 * diagnostics, bounds validation or ownership transfer.
 *
 * @param[in] bp Borrowed readable packed MSB-first row/input bytes,
 * unchanged.
 * @param[in] bs Nonnegative starting bit offset.
 * @param[in] be Exclusive ending bit offset, not less than bs.
 * @return Run length in bits, limited by the exclusive end position be.
 */
static inline int32_t find0span(unsigned char *bp, int32_t bs, int32_t be)
{
    int32_t bits = be - bs;
    int32_t n, span;

    bp += bs >> 3;
    /*
     * Check partial byte on lhs.
     */
    if (bits > 0 && (n = (bs & 7)) != 0)
    {
        span = zeroruns[(*bp << n) & 0xff];
        if (span > 8 - n) /* table value too generous */
            span = 8 - n;
        if (span > bits) /* constrain span to bit range */
            span = bits;
        if (n + span < 8) /* doesn't extend to edge of byte */
            return (span);
        bits -= span;
        bp++;
    }
    else
        span = 0;
    if (bits >= (int32_t)(2 * 8 * sizeof(int64_t)))
    {
        int64_t *lp;
        /*
         * Align to int64_t boundary and check int64_t words.
         */
        while (!isAligned(bp, int64_t))
        {
            if (*bp != 0x00)
                return (span + zeroruns[*bp]);
            span += 8;
            bits -= 8;
            bp++;
        }
        lp = (int64_t *)bp;
        while ((bits >= (int32_t)(size_t)(8 * sizeof(int64_t))) && (0 == *lp))
        {
            span += (int32_t)(size_t)(8 * sizeof(int64_t));
            bits -= (int32_t)(size_t)(8 * sizeof(int64_t));
            lp++;
        }
        bp = (unsigned char *)lp;
    }
    /*
     * Scan full bytes for all 0's.
     */
    while (bits >= 8)
    {
        if (*bp != 0x00) /* end of run */
            return (span + zeroruns[*bp]);
        span += 8;
        bits -= 8;
        bp++;
    }
    /*
     * Check partial byte on rhs.
     */
    if (bits > 0)
    {
        n = zeroruns[*bp];
        span += (n > bits ? bits : n);
    }
    return (span);
}

/**
 * @brief Measure the leading one bit run in a bounded packed row.
 * @ingroup libtiff_private_fax
 *
 * Requires 0<=bs<=be<=INT32_MAX and live bytes through the last inspected
 * bit. Uses immutable run lookup tables, with no mutation, allocation, I/O,
 * diagnostics, bounds validation or ownership transfer.
 *
 * @param[in] bp Borrowed readable packed MSB-first row/input bytes,
 * unchanged.
 * @param[in] bs Nonnegative starting bit offset.
 * @param[in] be Exclusive ending bit offset, not less than bs.
 * @return Run length in bits, limited by the exclusive end position be.
 */
static inline int32_t find1span(unsigned char *bp, int32_t bs, int32_t be)
{
    int32_t bits = be - bs;
    int32_t n, span;

    bp += bs >> 3;
    /*
     * Check partial byte on lhs.
     */
    if (bits > 0 && (n = (bs & 7)) != 0)
    {
        span = oneruns[(*bp << n) & 0xff];
        if (span > 8 - n) /* table value too generous */
            span = 8 - n;
        if (span > bits) /* constrain span to bit range */
            span = bits;
        if (n + span < 8) /* doesn't extend to edge of byte */
            return (span);
        bits -= span;
        bp++;
    }
    else
        span = 0;
    if (bits >= (int32_t)(2 * 8 * sizeof(int64_t)))
    {
        int64_t *lp;
        /*
         * Align to int64_t boundary and check int64_t words.
         */
        while (!isAligned(bp, int64_t))
        {
            if (*bp != 0xff)
                return (span + oneruns[*bp]);
            span += 8;
            bits -= 8;
            bp++;
        }
        lp = (int64_t *)bp;
        while ((bits >= (int32_t)(size_t)(8 * sizeof(int64_t))) &&
               (~((uint64_t)0) == (uint64_t)*lp))
        {
            span += (int32_t)(size_t)(8 * sizeof(int64_t));
            bits -= (int32_t)(size_t)(8 * sizeof(int64_t));
            lp++;
        }
        bp = (unsigned char *)lp;
    }
    /*
     * Scan full bytes for all 1's.
     */
    while (bits >= 8)
    {
        if (*bp != 0xff) /* end of run */
            return (span + oneruns[*bp]);
        span += 8;
        bits -= 8;
        bp++;
    }
    /*
     * Check partial byte on rhs.
     */
    if (bits > 0)
    {
        n = oneruns[*bp];
        span += (n > bits ? bits : n);
    }
    return (span);
}

/*
 * Return the offset of the next bit in the range
 * [bs..be] that is different from the specified
 * color.  The end, be, is returned if no such bit
 * exists.
 */
#define finddiff(_cp, _bs, _be, _color)                                        \
    (_bs + (_color ? find1span(_cp, _bs, _be) : find0span(_cp, _bs, _be)))
/*
 * Like finddiff, but also check the starting bit
 * against the end in case start > end.
 */
#define finddiff2(_cp, _bs, _be, _color)                                       \
    (_bs < _be ? finddiff(_cp, _bs, _be, _color) : _be)

/*
 * 1d-encode a row of pixels.  The encoding is
 * a sequence of all-white or all-black spans
 * of pixels encoded with Huffman codes.
 */
/**
 * @brief Encode alternating white and black runs for one bilevel row.
 * @ingroup libtiff_private_fax
 *
 * Requires positive bit count within INT32_MAX and a readable packed
 * MSB-first row. Emits Huffman spans and applies requested byte/word
 * alignment. Can flush client output; errors retain partial code/file
 * progress. Does not modify input or transfer ownership.
 *
 * @param[in,out] tif Non-NULL live fax handle with matching geometry/state;
 * serialise operations.
 * @param[in] bp Borrowed readable packed MSB-first row/input bytes,
 * unchanged.
 * @param[in] bits Row width in pixels (number of bits in @p bp to encode);
 * positive and at most INT32_MAX.
 * @return One after row coding, zero on output failure.
 */
static int Fax3Encode1DRow(TIFF *tif, unsigned char *bp, uint32_t bits)
{
    Fax3CodecState *sp = EncoderState(tif);
    int32_t span;
    uint32_t bs = 0;

    for (;;)
    {
        span = find0span(bp, (int32_t)bs, (int32_t)bits); /* white span */
        if (!putspan(tif, span, TIFFFaxWhiteCodes))
            return 0;
        bs += (uint32_t)span;
        if (bs >= bits)
            break;
        span = find1span(bp, (int32_t)bs, (int32_t)bits); /* black span */
        if (!putspan(tif, span, TIFFFaxBlackCodes))
            return 0;
        bs += (uint32_t)span;
        if (bs >= bits)
            break;
    }
    if (sp->b.mode & (FAXMODE_BYTEALIGN | FAXMODE_WORDALIGN))
    {
        if (sp->bit != 8) /* byte-align */
            Fax3FlushBits(tif, sp);
        if ((sp->b.mode & FAXMODE_WORDALIGN) &&
            !isAligned(tif->tif_rawcp, uint16_t))
            Fax3FlushBits(tif, sp);
    }
    return (1);
}

/**
 * @brief Encode the Group4 horizontal mode prefix.
 * @ingroup libtiff_private_fax
 *
 * Immutable library-lifetime storage; no ownership transfer.
 */
static const tableentry horizcode = {3, 0x1, 0}; /* 001 */
/**
 * @brief Encode the Group4 pass mode prefix.
 * @ingroup libtiff_private_fax
 *
 * Immutable library-lifetime storage; no ownership transfer.
 */
static const tableentry passcode = {4, 0x1, 0}; /* 0001 */
/**
 * @brief Encode Group4 vertical offsets minus three through three.
 * @ingroup libtiff_private_fax
 *
 * Immutable library-lifetime storage; no ownership transfer.
 */
static const tableentry vcodes[7] = {
    {7, 0x03, 0}, /* 0000 011 */
    {6, 0x03, 0}, /* 0000 11 */
    {3, 0x03, 0}, /* 011 */
    {1, 0x1, 0},  /* 1 */
    {3, 0x2, 0},  /* 010 */
    {6, 0x02, 0}, /* 0000 10 */
    {7, 0x02, 0}  /* 0000 010 */
};

/*
 * 2d-encode a row of pixels.  Consult the CCITT
 * documentation for the algorithm.
 */
/**
 * @brief Encode one bilevel row relative to its reference row.
 * @ingroup libtiff_private_fax
 *
 * Requires positive bit count within INT32_MAX and readable
 * current/reference rows including safe boundary-byte accesses used by
 * change detection. Emits horizontal, vertical or pass codes without
 * updating the reference row itself. Can flush client output and leave
 * partial state/file changes on failure. No allocation, input mutation or
 * ownership transfer.
 *
 * @param[in,out] tif Non-NULL live fax handle with matching geometry/state;
 * serialise operations.
 * @param[in] bp Borrowed readable packed MSB-first row/input bytes,
 * unchanged.
 * @param[in] rp Borrowed readable packed reference row, unchanged.
 * @param[in] bits Row width in pixels (number of bits in @p bp to encode);
 * positive and at most INT32_MAX.
 * @return One after row coding, zero on output failure.
 */
static int Fax3Encode2DRow(TIFF *tif, unsigned char *bp, unsigned char *rp,
                           uint32_t bits)
{
#define PIXEL(buf, ix) ((((buf)[(ix) >> 3]) >> (7 - ((ix) & 7))) & 1)
    uint32_t a0 = 0;
    uint32_t a1 = (PIXEL(bp, 0) != 0
                       ? 0
                       : (uint32_t)finddiff(bp, (int32_t)0, (int32_t)bits, 0));
    uint32_t b1 = (PIXEL(rp, 0) != 0
                       ? 0
                       : (uint32_t)finddiff(rp, (int32_t)0, (int32_t)bits, 0));
    uint32_t a2, b2;

    for (;;)
    {
        b2 = (uint32_t)finddiff2(rp, (int32_t)b1, (int32_t)bits,
                                 (int32_t)PIXEL(rp, b1));
        if (b2 >= a1)
        {
            /* Naive computation triggers
             * -fsanitize=undefined,unsigned-integer-overflow */
            /* although it is correct unless the difference between both is < 31
             * bit */
            /* int32_t d = b1 - a1; */
            int32_t d = (b1 >= a1 && b1 - a1 <= 3U)  ? (int32_t)(b1 - a1)
                        : (b1 < a1 && a1 - b1 <= 3U) ? -(int32_t)(a1 - b1)
                                                     : 0x7FFFFFFF;
            if (!(-3 <= d && d <= 3))
            { /* horizontal mode */
                a2 = (uint32_t)finddiff2(bp, (int32_t)a1, (int32_t)bits,
                                         (int32_t)PIXEL(bp, a1));
                if (!putcode(tif, &horizcode))
                    return 0;
                if (a0 + a1 == 0 || PIXEL(bp, a0) == 0)
                {
                    if (!putspan(tif, (int32_t)(a1 - a0), TIFFFaxWhiteCodes))
                        return 0;
                    if (!putspan(tif, (int32_t)(a2 - a1), TIFFFaxBlackCodes))
                        return 0;
                }
                else
                {
                    if (!putspan(tif, (int32_t)(a1 - a0), TIFFFaxBlackCodes))
                        return 0;
                    if (!putspan(tif, (int32_t)(a2 - a1), TIFFFaxWhiteCodes))
                        return 0;
                }
                a0 = a2;
            }
            else
            { /* vertical mode */
                if (!putcode(tif, &vcodes[d + 3]))
                    return 0;
                a0 = a1;
            }
        }
        else
        { /* pass mode */
            if (!putcode(tif, &passcode))
                return 0;
            a0 = b2;
        }
        if (a0 >= bits)
            break;
        a1 = (uint32_t)finddiff(bp, (int32_t)a0, (int32_t)bits,
                                (int32_t)PIXEL(bp, a0));
        b1 = (uint32_t)finddiff(rp, (int32_t)a0, (int32_t)bits,
                                (int32_t)!PIXEL(bp, a0));
        b1 = (uint32_t)finddiff(rp, (int32_t)b1, (int32_t)bits,
                                (int32_t)PIXEL(bp, a0));
    }
    return (1);
#undef PIXEL
}

/*
 * Encode a buffer of pixels.
 */
/**
 * @brief Encode complete Group3 or aligned RLE rows.
 * @ingroup libtiff_private_fax
 *
 * Requires positive cached row size, nonnegative complete-row cc and valid
 * raw/reference state. Emits EOL when enabled, chooses 1D/2D schedule,
 * updates reference data and advances input. Does not change caller
 * samples. Fractional requests diagnose; output failure can leave partial
 * state/file data. No allocation or ownership transfer.
 *
 * @param[in,out] tif Non-NULL live fax handle with matching geometry/state;
 * serialise operations.
 * @param[in] bp Borrowed readable packed MSB-first row/input bytes,
 * unchanged.
 * @param[in] cc Nonnegative complete-row input extent.
 * @param[in] s Unused sample-plane compatibility argument.
 * @return One after requested rows, zero on extent or output failure.
 */
static int Fax3Encode(TIFF *tif, uint8_t *bp, tmsize_t cc, uint16_t s)
{
    static const char module[] = "Fax3Encode";
    Fax3CodecState *sp = EncoderState(tif);
    (void)s;
    if (cc % sp->b.rowbytes)
    {
        TIFFErrorExtR(tif, module, "Fractional scanlines cannot be written");
        return (0);
    }
    while (cc > 0)
    {
        if ((sp->b.mode & FAXMODE_NOEOL) == 0)
        {
            if (!Fax3PutEOL(tif))
                return 0;
        }
        if (is2DEncoding(sp))
        {
            if (sp->tag == G3_1D)
            {
                if (!Fax3Encode1DRow(tif, bp, sp->b.rowpixels))
                    return (0);
                sp->tag = G3_2D;
            }
            else
            {
                if (!Fax3Encode2DRow(tif, bp, sp->refline, sp->b.rowpixels))
                    return (0);
                sp->k--;
            }
            if (sp->k == 0)
            {
                sp->tag = G3_1D;
                sp->k = sp->maxk - 1;
            }
            else
                _TIFFmemcpy(sp->refline, bp, sp->b.rowbytes);
        }
        else
        {
            if (!Fax3Encode1DRow(tif, bp, sp->b.rowpixels))
                return (0);
        }
        bp += sp->b.rowbytes;
        cc -= sp->b.rowbytes;
    }
    return (1);
}

/**
 * @brief Pad and buffer the final partial fax code byte.
 * @ingroup libtiff_private_fax
 *
 * Requires valid encoder/raw state. Flush macro can write a full raw buffer
 * before appending its byte and return early on failure. Does not otherwise
 * flush the final payload or persist metadata; no ownership transfer.
 *
 * @param[in,out] tif Non-NULL live fax handle with matching geometry/state;
 * serialise operations.
 * @return One after byte alignment, zero on required flush failure.
 */
static int Fax3PostEncode(TIFF *tif)
{
    Fax3CodecState *sp = EncoderState(tif);

    if (sp->bit != 8)
        Fax3FlushBits(tif, sp);
    return (1);
}

/**
 * @brief Append an optional six-EOL fax return-to-control sequence.
 * @ingroup libtiff_private_fax
 *
 * Runs only when NORTC is clear and raw cursor exists. Ignores Fax3PutBits
 * statuses while emitting EOLs; final flush macro can fail. Can modify
 * buffered/file state without rollback, allocation or ownership transfer.
 *
 * @param[in,out] tif Non-NULL live fax handle with matching geometry/state;
 * serialise operations.
 * @return One on normal exit, zero from final flush failure; earlier
 * ignored code-write failures need not be reflected.
 */
static int _Fax3Close(TIFF *tif)
{
    if ((Fax3State(tif)->mode & FAXMODE_NORTC) == 0 && tif->tif_rawcp)
    {
        Fax3CodecState *sp = EncoderState(tif);
        unsigned int code = EOL;
        unsigned int length = 12;
        int i;

        if (is2DEncoding(sp))
        {
            code = (code << 1) | (sp->tag == G3_1D);
            length++;
        }
        for (i = 0; i < 6; i++)
            Fax3PutBits(tif, code, length);
        Fax3FlushBits(tif, sp);
    }
    return 1;
}

/**
 * @brief Invoke fax return-to-control completion without a status channel.
 * @ingroup libtiff_private_fax
 *
 * Calls _Fax3Close and discards its result. Output diagnostics and partial
 * file/cache effects can occur. Does not free codec state or close the
 * client handle; no ownership transfer.
 *
 * @param[in,out] tif Non-NULL live fax handle with matching geometry/state;
 * serialise operations.
 */
static void Fax3Close(TIFF *tif) { _Fax3Close(tif); }

/**
 * @brief Restore fax tag/print methods and release run/reference state.
 * @ingroup libtiff_private_fax
 *
 * Requires live initialised state. Frees owned runs/reference row and outer
 * Ext allocation, clears tif_data and restores default compression
 * callbacks. Invalidates all codec-owned views. Does not emit RTC, finish
 * or flush payload, close the handle or return status.
 *
 * @param[in,out] tif Non-NULL live fax handle with matching geometry/state;
 * serialise operations.
 */
static void Fax3Cleanup(TIFF *tif)
{
    Fax3CodecState *sp = DecoderState(tif);

    assert(sp != 0);

    tif->tif_tagmethods.vgetfield = sp->b.vgetparent;
    tif->tif_tagmethods.vsetfield = sp->b.vsetparent;
    tif->tif_tagmethods.printdir = sp->b.printdir;

    if (sp->runs)
        _TIFFfreeExt(tif, sp->runs);
    if (sp->refline)
        _TIFFfreeExt(tif, sp->refline);

    _TIFFfreeExt(tif, tif->tif_data);
    tif->tif_data = NULL;

    _TIFFSetDefaultCompressionState(tif);
}

#define FIELD_BADFAXLINES (FIELD_CODEC + 0)
#define FIELD_CLEANFAXDATA (FIELD_CODEC + 1)
#define FIELD_BADFAXRUN (FIELD_CODEC + 2)

#define FIELD_OPTIONS (FIELD_CODEC + 7)

/**
 * @brief Define common fax metadata and pseudo-tags.
 * @ingroup libtiff_private_fax
 *
 * Immutable library-lifetime metadata merged into a handle; no caller
 * ownership transfer.
 */
static const TIFFField faxFields[] = {
    {TIFFTAG_FAXMODE, 0, 0, TIFF_ANY, 0, TIFF_SETGET_INT, FIELD_PSEUDO, FALSE,
     FALSE, "FaxMode", NULL},
    {TIFFTAG_FAXFILLFUNC, 0, 0, TIFF_ANY, 0, TIFF_SETGET_OTHER, FIELD_PSEUDO,
     FALSE, FALSE, "FaxFillFunc", NULL},
    {TIFFTAG_BADFAXLINES, 1, 1, TIFF_LONG, 0, TIFF_SETGET_UINT32,
     FIELD_BADFAXLINES, TRUE, FALSE, "BadFaxLines", NULL},
    {TIFFTAG_CLEANFAXDATA, 1, 1, TIFF_SHORT, 0, TIFF_SETGET_UINT16,
     FIELD_CLEANFAXDATA, TRUE, FALSE, "CleanFaxData", NULL},
    {TIFFTAG_CONSECUTIVEBADFAXLINES, 1, 1, TIFF_LONG, 0, TIFF_SETGET_UINT32,
     FIELD_BADFAXRUN, TRUE, FALSE, "ConsecutiveBadFaxLines", NULL}};
/**
 * @brief Define Group3 options metadata.
 * @ingroup libtiff_private_fax
 *
 * Immutable library-lifetime metadata merged into a handle; no caller
 * ownership transfer.
 */
static const TIFFField fax3Fields[] = {
    {TIFFTAG_GROUP3OPTIONS, 1, 1, TIFF_LONG, 0, TIFF_SETGET_UINT32,
     FIELD_OPTIONS, FALSE, FALSE, "Group3Options", NULL},
};
/**
 * @brief Define Group4 options metadata.
 * @ingroup libtiff_private_fax
 *
 * Immutable library-lifetime metadata merged into a handle; no caller
 * ownership transfer.
 */
static const TIFFField fax4Fields[] = {
    {TIFFTAG_GROUP4OPTIONS, 1, 1, TIFF_LONG, 0, TIFF_SETGET_UINT32,
     FIELD_OPTIONS, FALSE, FALSE, "Group4Options", NULL},
};

/**
 * @brief Assign fax pseudo-tags or persistent fax metadata.
 * @ingroup libtiff_private_fax
 *
 * FAXMODE consumes int; FAXFILLFUNC consumes a non-NULL callable
 * TIFFFaxFillFunc kept live while installed/in flight. GROUP3/4OPTIONS,
 * BADFAXLINES and CONSECUTIVEBADFAXLINES consume uint32_t; options are
 * consumed only for matching compression. CLEANFAXDATA consumes uint16_vap.
 * Persistent tags mark field bits and directory dirty after assignment;
 * lookup failure can leave changed cached values. If the compression does
 * not match GROUP3OPTIONS/GROUP4OPTIONS, the value is not read and
 * groupoptions is unchanged, but FIELD_OPTIONS and TIFF_DIRTYDIRECT are
 * still set and 1 is returned. Other tags delegate to parent contracts. No
 * ownership transfer; callbacks/diagnostics can run.
 *
 * @param[in,out] tif Non-NULL live fax handle with matching geometry/state;
 * serialise operations.
 * @param[in] tag Fax or delegated tag identifier.
 * @param[in,out] ap Live tag-specific va_list, consumed synchronously
 * without retention.
 * @return One after accepted assignment, zero on field lookup failure,
 * otherwise parent status.
 */
static int Fax3VSetField(TIFF *tif, uint32_t tag, va_list ap)
{
    Fax3BaseState *sp = Fax3State(tif);
    const TIFFField *fip;

    assert(sp != 0);
    assert(sp->vsetparent != 0);

    switch (tag)
    {
        case TIFFTAG_FAXMODE:
            sp->mode = (int)va_arg(ap, int);
            return 1; /* NB: pseudo tag */
        case TIFFTAG_FAXFILLFUNC:
            DecoderState(tif)->fill = va_arg(ap, TIFFFaxFillFunc);
            return 1; /* NB: pseudo tag */
        case TIFFTAG_GROUP3OPTIONS:
            /* XXX: avoid reading options if compression mismatches. */
            if (tif->tif_dir.td_compression == COMPRESSION_CCITTFAX3)
                sp->groupoptions = (uint32_t)va_arg(ap, uint32_t);
            break;
        case TIFFTAG_GROUP4OPTIONS:
            /* XXX: avoid reading options if compression mismatches. */
            if (tif->tif_dir.td_compression == COMPRESSION_CCITTFAX4)
                sp->groupoptions = (uint32_t)va_arg(ap, uint32_t);
            break;
        case TIFFTAG_BADFAXLINES:
            sp->badfaxlines = (uint32_t)va_arg(ap, uint32_t);
            break;
        case TIFFTAG_CLEANFAXDATA:
            sp->cleanfaxdata = (uint16_t)va_arg(ap, uint16_vap);
            break;
        case TIFFTAG_CONSECUTIVEBADFAXLINES:
            sp->badfaxrun = (uint32_t)va_arg(ap, uint32_t);
            break;
        default:
            return (*sp->vsetparent)(tif, tag, ap);
    }

    if ((fip = TIFFFieldWithTag(tif, tag)) != NULL)
        TIFFSetFieldBit(tif, fip->field_bit);
    else
        return 0;

    tif->tif_flags |= TIFF_DIRTYDIRECT;
    return 1;
}

/**
 * @brief Retrieve fax mode callback or persistent fax values.
 * @ingroup libtiff_private_fax
 *
 * FAXMODE consumes int*; FAXFILLFUNC consumes TIFFFaxFillFunc* and returns
 * a borrowed installed callback. Options/bad-line counters consume
 * uint32_t*; CLEANFAXDATA consumes uint16_t*. All outputs must be non-NULL.
 * Other tags follow parent contracts. No allocation or ownership transfer.
 *
 * @param[in,out] tif Non-NULL live fax handle with matching geometry/state;
 * serialise operations.
 * @param[in] tag Fax or delegated tag identifier.
 * @param[in,out] ap Live tag-specific va_list, consumed synchronously
 * without retention.
 * @return One for fax retrieval, otherwise parent status.
 */
static int Fax3VGetField(TIFF *tif, uint32_t tag, va_list ap)
{
    Fax3BaseState *sp = Fax3State(tif);

    assert(sp != 0);

    switch (tag)
    {
        case TIFFTAG_FAXMODE:
            *va_arg(ap, int *) = sp->mode;
            break;
        case TIFFTAG_FAXFILLFUNC:
            *va_arg(ap, TIFFFaxFillFunc *) = DecoderState(tif)->fill;
            break;
        case TIFFTAG_GROUP3OPTIONS:
        case TIFFTAG_GROUP4OPTIONS:
            *va_arg(ap, uint32_t *) = sp->groupoptions;
            break;
        case TIFFTAG_BADFAXLINES:
            *va_arg(ap, uint32_t *) = sp->badfaxlines;
            break;
        case TIFFTAG_CLEANFAXDATA:
            *va_arg(ap, uint16_t *) = sp->cleanfaxdata;
            break;
        case TIFFTAG_CONSECUTIVEBADFAXLINES:
            *va_arg(ap, uint32_t *) = sp->badfaxrun;
            break;
        default:
            return (*sp->vgetparent)(tif, tag, ap);
    }
    return (1);
}

/**
 * @brief Print present fax values then chain the saved directory printer.
 * @ingroup libtiff_private_fax
 *
 * Writes to borrowed FILE without checking stdio errors, then invokes
 * optional saved printer. Does not close the stream or transfer ownership;
 * parent callback effects apply. No separate error status.
 *
 * @param[in] tif Non-NULL fax handle whose field bits and cached fax values
 * are printed; not modified.
 * @param[in,out] fd Borrowed writable FILE stream; remains caller-owned.
 * @param[in] flags Print flags forwarded to the saved parent printer.
 */
static void Fax3PrintDir(TIFF *tif, FILE *fd, long flags)
{
    Fax3BaseState *sp = Fax3State(tif);

    assert(sp != 0);

    (void)flags;
    if (TIFFFieldSet(tif, FIELD_OPTIONS))
    {
        const char *sep = " ";
        if (tif->tif_dir.td_compression == COMPRESSION_CCITTFAX4)
        {
            fprintf(fd, "  Group 4 Options:");
            if (sp->groupoptions & GROUP4OPT_UNCOMPRESSED)
                fprintf(fd, "%suncompressed data", sep);
        }
        else
        {

            fprintf(fd, "  Group 3 Options:");
            if (sp->groupoptions & GROUP3OPT_2DENCODING)
            {
                fprintf(fd, "%s2-d encoding", sep);
                sep = "+";
            }
            if (sp->groupoptions & GROUP3OPT_FILLBITS)
            {
                fprintf(fd, "%sEOL padding", sep);
                sep = "+";
            }
            if (sp->groupoptions & GROUP3OPT_UNCOMPRESSED)
                fprintf(fd, "%suncompressed data", sep);
        }
        fprintf(fd, " (%" PRIu32 " = 0x%" PRIx32 ")\n", sp->groupoptions,
                sp->groupoptions);
    }
    if (TIFFFieldSet(tif, FIELD_CLEANFAXDATA))
    {
        fprintf(fd, "  Fax Data:");
        switch (sp->cleanfaxdata)
        {
            case CLEANFAXDATA_CLEAN:
                fprintf(fd, " clean");
                break;
            case CLEANFAXDATA_REGENERATED:
                fprintf(fd, " receiver regenerated");
                break;
            case CLEANFAXDATA_UNCLEAN:
                fprintf(fd, " uncorrected errors");
                break;
            default:
                break;
        }
        fprintf(fd, " (%" PRIu16 " = 0x%" PRIx16 ")\n", sp->cleanfaxdata,
                sp->cleanfaxdata);
    }
    if (TIFFFieldSet(tif, FIELD_BADFAXLINES))
        fprintf(fd, "  Bad Fax Lines: %" PRIu32 "\n", sp->badfaxlines);
    if (TIFFFieldSet(tif, FIELD_BADFAXRUN))
        fprintf(fd, "  Consecutive Bad Fax Lines: %" PRIu32 "\n",
                sp->badfaxrun);
    if (sp->printdir)
        (*sp->printdir)(tif, fd, flags);
}

/**
 * @brief Report the fax expansion allocation heuristic.
 * @ingroup libtiff_private_fax
 *
 * No allocation, diagnostics, I/O, mutation or ownership transfer. The
 * heuristic is not proof of payload validity or universal success.
 *
 * @param[in] tif Unused handle.
 * @return 250.
 */
static uint64_t Fax3GetMaxCompressionRatio(TIFF *tif)
{
    (void)tif;

    /* See README_for_libtiff_developpers.md for raw data used to estimate
     * the maximum compression rate. */

    /* 1024x1024: 36 */
    /* 4096x4096: 100 */
    /* 16383x16383: 163 */
    /* 65536x65536: 200 */
    /* 200000x200000: 208 */

    return 250;
}

/**
 * @brief Allocate shared fax state and install tag and transfer methods.
 * @ingroup libtiff_private_fax
 *
 * Requires prior codec cleanup/default methods. Merges common fields,
 * allocates zeroed Ext state, saves tag/print chains, installs default fill
 * callback and row/strile/setup/close methods. Read-only mode sets NOBITREV
 * because decoder handles FillOrder. Default fill setter status is ignored.
 * Merge/allocation errors diagnose without general rollback. Buffers are
 * allocated at setup; no payload I/O.
 *
 * @param[in,out] tif Non-NULL live fax handle with matching geometry/state;
 * serialise operations.
 * @return One after shared installation, zero on field merge or outer
 * allocation failure.
 */
static int InitCCITTFax3(TIFF *tif)
{
    static const char module[] = "InitCCITTFax3";
    Fax3BaseState *sp;

    /*
     * Merge codec-specific tag information.
     */
    if (!_TIFFMergeFields(tif, faxFields, TIFFArrayCount(faxFields)))
    {
        TIFFErrorExtR(tif, "InitCCITTFax3",
                      "Merging common CCITT Fax codec-specific tags failed");
        return 0;
    }

    /*
     * Allocate state block so tag methods have storage to record values.
     */
    tif->tif_data = (uint8_t *)_TIFFmallocExt(tif, sizeof(Fax3CodecState));

    if (tif->tif_data == NULL)
    {
        TIFFErrorExtR(tif, module, "No space for state block");
        return (0);
    }
    _TIFFmemset(tif->tif_data, 0, sizeof(Fax3CodecState));

    sp = Fax3State(tif);
    sp->rw_mode = tif->tif_mode;

    /*
     * Override parent get/set field methods.
     */
    sp->vgetparent = tif->tif_tagmethods.vgetfield;
    tif->tif_tagmethods.vgetfield = Fax3VGetField; /* hook for codec tags */
    sp->vsetparent = tif->tif_tagmethods.vsetfield;
    tif->tif_tagmethods.vsetfield = Fax3VSetField; /* hook for codec tags */
    sp->printdir = tif->tif_tagmethods.printdir;
    tif->tif_tagmethods.printdir = Fax3PrintDir; /* hook for codec tags */
    sp->groupoptions = 0;

    if (sp->rw_mode == O_RDONLY) /* FIXME: improve for in place update */
        tif->tif_flags |= TIFF_NOBITREV; /* decoder does bit reversal */
    DecoderState(tif)->runs = NULL;
    TIFFSetField(tif, TIFFTAG_FAXFILLFUNC, _TIFFFax3fillruns);
    EncoderState(tif)->refline = NULL;

    /*
     * Install codec methods.
     */
    tif->tif_fixuptags = Fax3FixupTags;
    tif->tif_setupdecode = Fax3SetupState;
    tif->tif_predecode = Fax3PreDecode;
    tif->tif_decoderow = Fax3Decode1D;
    tif->tif_decodestrip = Fax3Decode1D;
    tif->tif_decodetile = Fax3Decode1D;
    tif->tif_setupencode = Fax3SetupState;
    tif->tif_preencode = Fax3PreEncode;
    tif->tif_postencode = Fax3PostEncode;
    tif->tif_encoderow = Fax3Encode;
    tif->tif_encodestrip = Fax3Encode;
    tif->tif_encodetile = Fax3Encode;
    tif->tif_close = Fax3Close;
    tif->tif_cleanup = Fax3Cleanup;
    tif->tif_getmaxcompressionratio = Fax3GetMaxCompressionRatio;

    return (1);
}

int TIFFInitCCITTFax3(TIFF *tif, int scheme)
{
    (void)scheme;
    if (InitCCITTFax3(tif))
    {
        /*
         * Merge codec-specific tag information.
         */
        if (!_TIFFMergeFields(tif, fax3Fields, TIFFArrayCount(fax3Fields)))
        {
            TIFFErrorExtR(tif, "TIFFInitCCITTFax3",
                          "Merging CCITT Fax 3 codec-specific tags failed");
            return 0;
        }

        /*
         * The default format is Class/F-style w/o RTC.
         */
        return TIFFSetField(tif, TIFFTAG_FAXMODE, FAXMODE_CLASSF);
    }
    else
        return 0;
}

/*
 * CCITT Group 4 (T.6) Facsimile-compatible
 * Compression Scheme Support.
 */

#define SWAP(t, a, b)                                                          \
    {                                                                          \
        t x;                                                                   \
        x = (a);                                                               \
        (a) = (b);                                                             \
        (b) = x;                                                               \
    }
/*
 * Decode the requested amount of G4-encoded data.
 */
/**
 * @brief Decode complete Group4 two-dimensional fax rows.
 * @ingroup libtiff_private_fax
 *
 * Requires setup/predecode, positive rowbytes, signed-representable pixel
 * coordinates, nonnegative complete-row occ and valid run/reference
 * capacities. Expands cached codes through state macros and invokes the
 * installed fill callback synchronously; NULL buf is valid only for a
 * custom callback that explicitly supports it. Can recover bad row lengths
 * with diagnostics and counter limits; an EOL/EOFB code ends decoding of the
 * strile. Updates run/reference arrays, line/bit state and raw cursors;
 * failure can leave a partially filled current row and earlier output. The
 * "Buffer overrun detected" failure returns without committing the cached
 * bit and raw cursor state. Group4 can return success at early
 * termination after at least one completed row, tolerating badly terminated
 * striles; not all requested rows are thereby proved filled. No direct
 * allocation/client I/O or ownership transfer; a custom filler has its own
 * effects. Negative error status is not a Boolean success result.
 *
 * @param[in,out] tif Non-NULL live fax handle with matching geometry/state;
 * serialise operations.
 * @param[out] buf Borrowed decoded row storage of occ bytes, or NULL only
 * with a compatible custom fill callback.
 * @param[in] occ Nonnegative complete-row decoded extent.
 * @param[in] s Unused sample-plane compatibility argument.
 * @return One on normal completion or accepted early Group4 termination,
 * minus one on detected extent/counter/decoding failure.
 */
static int Fax4Decode(TIFF *tif, uint8_t *buf, tmsize_t occ, uint16_t s)
{
    DECLARE_STATE_2D(tif, sp, "Fax4Decode");
    (void)s;
    if (occ % sp->b.rowbytes)
    {
        TIFFErrorExtR(tif, module, "Fractional scanlines cannot be read");
        return (-1);
    }
    if (CheckReachedCounters(tif, module, sp))
        return (-1);
    CACHE_STATE(tif, sp);
    int start = sp->line;
    while (occ > 0)
    {
        a0 = 0;
        RunLength = 0;
        pa = thisrun = sp->curruns;
        pb = sp->refruns;
        b1 = (int)*pb++;
#ifdef FAX3_DEBUG
        printf("\nBitAcc=%08" PRIX32 ", BitsAvail = %d\n", BitAcc, BitsAvail);
        printf("-------------------- %d\n", tif->tif_dir.td_row);
        fflush(stdout);
#endif
        EXPAND2D(EOFG4);
        if (EOLcnt)
            goto EOFG4;
        if (((lastx + 7) >> 3) > (int)occ) /* check for buffer overrun */
        {
            TIFFErrorExtR(tif, module,
                          "Buffer overrun detected : %" TIFF_SSIZE_FORMAT
                          " bytes available, %d bits needed",
                          occ, lastx);
            return -1;
        }
        (*sp->fill)(buf, thisrun, pa, (uint32_t)lastx);
        SETVALUE(0); /* imaginary change for reference */
        SWAP(uint32_t *, sp->curruns, sp->refruns);
        buf += sp->b.rowbytes;
        occ -= sp->b.rowbytes;
        sp->line++;
        continue;
    EOFG4:
        NeedBits16(13, BADG4);
    BADG4:
#ifdef FAX3_DEBUG
        if (GetBits(13) != 0x1001)
            fputs("Bad EOFB\n", stderr);
#endif
        ClrBits(13);
        if (((lastx + 7) >> 3) > (int)occ) /* check for buffer overrun */
        {
            TIFFErrorExtR(tif, module,
                          "Buffer overrun detected : %" TIFF_SSIZE_FORMAT
                          " bytes available, %d bits needed",
                          occ, lastx);
            return -1;
        }
        (*sp->fill)(buf, thisrun, pa, (uint32_t)lastx);
        UNCACHE_STATE(tif, sp);
        return (sp->line != start
                    ? 1
                    : -1); /* don't error on badly-terminated strips */
    }
    UNCACHE_STATE(tif, sp);
    return (1);
}
#undef SWAP

/*
 * Encode the requested amount of data.
 */
/**
 * @brief Encode complete Group4 rows relative to the preceding row.
 * @ingroup libtiff_private_fax
 *
 * Requires positive row size, complete-row nonnegative cc and valid
 * reference/raw storage. Calls the 2D encoder and copies each successful
 * row into reference storage. Fractional requests diagnose; I/O failure
 * leaves earlier reference/bit/file changes. Caller input remains
 * unchanged; no allocation or ownership transfer.
 *
 * @param[in,out] tif Non-NULL live fax handle with matching geometry/state;
 * serialise operations.
 * @param[in] bp Borrowed readable packed MSB-first row/input bytes,
 * unchanged.
 * @param[in] cc Nonnegative complete-row input extent.
 * @param[in] s Unused sample-plane compatibility argument.
 * @return One after requested rows, zero on extent/output failure.
 */
static int Fax4Encode(TIFF *tif, uint8_t *bp, tmsize_t cc, uint16_t s)
{
    static const char module[] = "Fax4Encode";
    Fax3CodecState *sp = EncoderState(tif);
    (void)s;
    if (cc % sp->b.rowbytes)
    {
        TIFFErrorExtR(tif, module, "Fractional scanlines cannot be written");
        return (0);
    }
    while (cc > 0)
    {
        if (!Fax3Encode2DRow(tif, bp, sp->refline, sp->b.rowpixels))
            return (0);
        _TIFFmemcpy(sp->refline, bp, sp->b.rowbytes);
        bp += sp->b.rowbytes;
        cc -= sp->b.rowbytes;
    }
    return (1);
}

/**
 * @brief Append Group4 EOFB and align the final byte.
 * @ingroup libtiff_private_fax
 *
 * Emits two EOL codes but ignores their individual statuses, then aligns
 * with the flush macro. Requires valid encoder/raw state. Can leave partial
 * output/file state and does not persist IFD or promise durability; no
 * ownership transfer.
 *
 * @param[in,out] tif Non-NULL live fax handle with matching geometry/state;
 * serialise operations.
 * @return One on normal exit, zero from final required flush; ignored EOL
 * write failures need not be reflected.
 */
static int Fax4PostEncode(TIFF *tif)
{
    Fax3CodecState *sp = EncoderState(tif);

    /* terminate strip w/ EOFB */
    Fax3PutBits(tif, EOL, 12);
    Fax3PutBits(tif, EOL, 12);
    if (sp->bit != 8)
        Fax3FlushBits(tif, sp);
    return (1);
}

/**
 * @brief Report the fax expansion allocation heuristic.
 * @ingroup libtiff_private_fax
 *
 * No allocation, diagnostics, I/O, mutation or ownership transfer. The
 * heuristic is not proof of payload validity or universal success.
 *
 * @param[in] tif Handle whose directory supplies the tile or image width;
 * not modified.
 * @return TileWidth for tiled data, otherwise ImageWidth.
 */
static uint64_t Fax4GetMaxCompressionRatio(TIFF *tif)
{
    /* FAX4 can compress up to almost one byte per line, so the compression
     * ratio can be up to the tile/strip width.
     * See README_for_libtiff_developpers.md for raw data
     */
    return isTiled(tif) ? tif->tif_dir.td_tilewidth
                        : tif->tif_dir.td_imagewidth;
}

int TIFFInitCCITTFax4(TIFF *tif, int scheme)
{
    (void)scheme;
    if (InitCCITTFax3(tif))
    { /* reuse G3 support */
        /*
         * Merge codec-specific tag information.
         */
        if (!_TIFFMergeFields(tif, fax4Fields, TIFFArrayCount(fax4Fields)))
        {
            TIFFErrorExtR(tif, "TIFFInitCCITTFax4",
                          "Merging CCITT Fax 4 codec-specific tags failed");
            return 0;
        }

        tif->tif_decoderow = Fax4Decode;
        tif->tif_decodestrip = Fax4Decode;
        tif->tif_decodetile = Fax4Decode;
        tif->tif_encoderow = Fax4Encode;
        tif->tif_encodestrip = Fax4Encode;
        tif->tif_encodetile = Fax4Encode;
        tif->tif_postencode = Fax4PostEncode;
        tif->tif_getmaxcompressionratio = Fax4GetMaxCompressionRatio;
        /*
         * Suppress RTC at the end of each strip.
         */
        return TIFFSetField(tif, TIFFTAG_FAXMODE, FAXMODE_NORTC);
    }
    else
        return (0);
}

/*
 * CCITT Group 3 1-D Modified Huffman RLE Compression Support.
 * (Compression algorithms 2 and 32771)
 */

/*
 * Decode the requested amount of RLE-encoded data.
 */
/**
 * @brief Decode complete aligned modified-Huffman fax rows.
 * @ingroup libtiff_private_fax
 *
 * Requires setup/predecode, positive rowbytes, signed-representable pixel
 * coordinates, nonnegative complete-row occ and valid run capacity. Expands
 * cached codes through state macros and invokes the installed fill callback
 * synchronously; NULL buf is valid only for a custom callback that
 * explicitly supports it. Can recover bad row lengths with diagnostics and
 * counter limits. After each row, discards the remaining bits up to the next
 * byte (FAXMODE_BYTEALIGN) or 16-bit word (FAXMODE_WORDALIGN) boundary.
 * Updates the current run array, line/bit state and raw cursors; failure
 * can leave a partially filled current row and earlier output. No direct
 * allocation/client I/O or ownership transfer; a custom filler has its own
 * effects. Negative error status is not a Boolean success result.
 *
 * @param[in,out] tif Non-NULL live fax handle with matching geometry/state;
 * serialise operations.
 * @param[out] buf Borrowed decoded row storage of occ bytes, or NULL only
 * with a compatible custom fill callback.
 * @param[in] occ Nonnegative complete-row decoded extent.
 * @param[in] s Unused sample-plane compatibility argument.
 * @return One on normal completion, minus one on detected
 * extent/counter/decoding failure.
 */
static int Fax3DecodeRLE(TIFF *tif, uint8_t *buf, tmsize_t occ, uint16_t s)
{
    DECLARE_STATE(tif, sp, "Fax3DecodeRLE");
    int mode = sp->b.mode;
    (void)s;
    if (occ % sp->b.rowbytes)
    {
        TIFFErrorExtR(tif, module, "Fractional scanlines cannot be read");
        return (-1);
    }
    if (CheckReachedCounters(tif, module, sp))
        return (-1);
    CACHE_STATE(tif, sp);
    thisrun = sp->curruns;
    while (occ > 0)
    {
        a0 = 0;
        RunLength = 0;
        pa = thisrun;
#ifdef FAX3_DEBUG
        printf("\nBitAcc=%08" PRIX32 ", BitsAvail = %d\n", BitAcc, BitsAvail);
        printf("-------------------- %" PRIu32 "\n", tif->tif_dir.td_row);
        fflush(stdout);
#endif
        EXPAND1D(EOFRLE);
        (*sp->fill)(buf, thisrun, pa, (uint32_t)lastx);
        /*
         * Cleanup at the end of the row.
         */
        if (mode & FAXMODE_BYTEALIGN)
        {
            int n = BitsAvail - (BitsAvail & ~7);
            ClrBits(n);
        }
        else if (mode & FAXMODE_WORDALIGN)
        {
            int n = BitsAvail - (BitsAvail & ~15);
            ClrBits(n);
            if (BitsAvail == 0 && !isAligned(cp, uint16_t))
                cp++;
        }
        buf += sp->b.rowbytes;
        occ -= sp->b.rowbytes;
        sp->line++;
        continue;
    EOFRLE: /* premature EOF */
        (*sp->fill)(buf, thisrun, pa, (uint32_t)lastx);
        UNCACHE_STATE(tif, sp);
        return (-1);
    }
    UNCACHE_STATE(tif, sp);
    return (1);
}

/**
 * @brief Report the fax expansion allocation heuristic.
 * @ingroup libtiff_private_fax
 *
 * No allocation, diagnostics, I/O, mutation or ownership transfer. The
 * heuristic is not proof of payload validity or universal success.
 *
 * @param[in] tif Unused handle.
 * @return 250.
 */
static uint64_t Fax3RLEGetMaxCompressionRatio(TIFF *tif)
{
    (void)tif;
    /* See README_for_libtiff_developpers.md for raw data used to estimate
     * the maximum compression rate. */

    /* 1024x1024: 43 */
    /* 4096x4096: 128 */
    /* 16383x16383: 171 */
    /* 65536x65536: 205 */
    /* 200000x200000: 211 */

    return 250;
}

int TIFFInitCCITTRLE(TIFF *tif, int scheme)
{
    (void)scheme;
    if (InitCCITTFax3(tif))
    { /* reuse G3 support */
        tif->tif_decoderow = Fax3DecodeRLE;
        tif->tif_decodestrip = Fax3DecodeRLE;
        tif->tif_decodetile = Fax3DecodeRLE;
        tif->tif_getmaxcompressionratio = Fax3RLEGetMaxCompressionRatio;
        /*
         * Suppress RTC+EOLs when encoding and byte-align data.
         */
        return TIFFSetField(tif, TIFFTAG_FAXMODE,
                            FAXMODE_NORTC | FAXMODE_NOEOL | FAXMODE_BYTEALIGN);
    }
    else
        return (0);
}

int TIFFInitCCITTRLEW(TIFF *tif, int scheme)
{
    (void)scheme;
    if (InitCCITTFax3(tif))
    { /* reuse G3 support */
        tif->tif_decoderow = Fax3DecodeRLE;
        tif->tif_decodestrip = Fax3DecodeRLE;
        tif->tif_decodetile = Fax3DecodeRLE;
        tif->tif_getmaxcompressionratio = Fax3RLEGetMaxCompressionRatio;
        /*
         * Suppress RTC+EOLs when encoding and word-align data.
         */
        return TIFFSetField(tif, TIFFTAG_FAXMODE,
                            FAXMODE_NORTC | FAXMODE_NOEOL | FAXMODE_WORDALIGN);
    }
    else
        return (0);
}
#endif /* CCITT_SUPPORT */

/**
 * @def G3CODES
 * @brief Instantiate the Huffman code tables from t4.h.
 * @ingroup libtiff_private_fax
 *
 * Defined (empty) before including t4.h so that t4.h defines
 * TIFFFaxWhiteCodes and TIFFFaxBlackCodes, rather than only declaring them.
 * tif_fax3.c is the only file that defines it.
 */

/**
 * @def EOF_REACHED_COUNT_THRESHOLD
 * @brief Limit repeated fax recovery conditions per strile.
 * @ingroup libtiff_private_fax
 *
 * Default 8192; it can be overridden at build time because the definition is
 * guarded by @c \#ifndef. CheckReachedCounters() fails decoding once
 * eofReachedCount, eolReachedCount or unexpectedReachedCount reaches it. The
 * counters are reset by Fax3PreDecode() for each strile. The limit stops
 * apparently endless looping on corrupt single-strip files with a very large
 * ImageLength (issue 583).
 */

/**
 * @def Fax3State(tif)
 * @brief Borrow the shared fax state prefix.
 * @ingroup libtiff_private_fax
 *
 * Casts tif->tif_data to Fax3BaseState *; it does not check that the handle
 * uses a fax codec.
 *
 * @param[in] tif Handle whose tif_data holds fax codec state.
 */

/**
 * @def DecoderState(tif)
 * @brief Borrow the complete fax decoder state.
 * @ingroup libtiff_private_fax
 *
 * Casts Fax3State(tif) to Fax3CodecState *. Decoding and encoding share this
 * structure.
 *
 * @param[in] tif Handle whose tif_data holds fax codec state.
 */

/**
 * @def EncoderState(tif)
 * @brief Borrow the complete fax encoder state.
 * @ingroup libtiff_private_fax
 *
 * Casts Fax3State(tif) to Fax3CodecState *. Decoding and encoding share this
 * structure.
 *
 * @param[in] tif Handle whose tif_data holds fax codec state.
 */

/**
 * @def is2DEncoding(sp)
 * @brief Test Group3 two-dimensional mode.
 * @ingroup libtiff_private_fax
 *
 * Nonzero when GROUP3OPT_2DENCODING is set in sp->b.groupoptions. @p sp is
 * not parenthesised in the expansion, so pass a plain identifier.
 *
 * @param[in] sp Fax3CodecState pointer.
 */

/**
 * @def isAligned(p, t)
 * @brief Test a pointer against a type-sized alignment.
 * @ingroup libtiff_private_fax
 *
 * Nonzero when the address @p p is a multiple of sizeof(@p t), which must be a
 * power of two.
 *
 * @param[in] p Pointer to test.
 * @param[in] t Type whose size gives the alignment.
 */

/**
 * @def DECLARE_STATE(tif, sp, mod)
 * @brief Declare cached local one-dimensional decode state.
 * @ingroup libtiff_private_fax
 *
 * Declares, at the start of a decoder function, @c module (set to @p mod),
 * @p sp (set to DecoderState(@p tif)), @c lastx (the row width), @c bitmap
 * (the bit-reversal table) and the uninitialised locals used by the decoding
 * macros: a0, BitAcc, BitsAvail, RunLength, cp, ep, pa, thisrun, EOLcnt and
 * TabEnt. CACHE_STATE() then loads the bit and raw-data state.
 *
 * @param[in] tif Decoding handle.
 * @param[in] sp Name of the Fax3CodecState pointer to declare.
 * @param[in] mod String literal used as the diagnostic module name.
 */

/**
 * @def DECLARE_STATE_2D(tif, sp, mod)
 * @brief Declare local two-dimensional decode and reference state.
 * @ingroup libtiff_private_fax
 *
 * Expands to DECLARE_STATE() plus the locals @c b1 (next change on the
 * reference row) and @c pb (cursor into the reference runs) used by
 * EXPAND2D().
 *
 * @param[in] tif Decoding handle.
 * @param[in] sp Name of the Fax3CodecState pointer to declare.
 * @param[in] mod String literal used as the diagnostic module name.
 */

/**
 * @def CACHE_STATE(tif, sp)
 * @brief Load codec and raw cursor state into decode locals.
 * @ingroup libtiff_private_fax
 *
 * Copies sp->data, sp->bit and sp->EOLcnt into BitAcc, BitsAvail and EOLcnt,
 * and sets cp and ep to the unread raw data tif_rawcp to
 * tif_rawcp + tif_rawcc.
 *
 * @param[in] tif Decoding handle.
 * @param[in] sp Decoder state.
 */

/**
 * @def UNCACHE_STATE(tif, sp)
 * @brief Commit local bit state and consumed raw bytes.
 * @ingroup libtiff_private_fax
 *
 * Stores BitsAvail, BitAcc and EOLcnt back into @p sp, and advances
 * tif_rawcp to cp, reducing tif_rawcc by the bytes consumed. Paths that
 * return without it leave the state as of the last CACHE_STATE().
 *
 * @param[in,out] tif Decoding handle.
 * @param[in,out] sp Decoder state.
 */

/**
 * @def unexpected(table, a0)
 * @brief Diagnose a bad code and increment its recovery counter.
 * @ingroup libtiff_private_fax
 *
 * Calls Fax3Unexpected() with the enclosing @c module, @c tif and
 * @c sp->line, then increments sp->unexpectedReachedCount. @p table is not
 * used by the expansion.
 *
 * @param[in] table Name of the table that failed (unused).
 * @param[in] a0 Pixel position of the bad code.
 */

/**
 * @def extension(a0)
 * @brief Diagnose an unsupported uncompressed extension.
 * @ingroup libtiff_private_fax
 *
 * Calls Fax3Extension() with the enclosing @c module, @c tif and
 * @c sp->line. No recovery counter is incremented.
 *
 * @param[in] a0 Pixel position of the extension code.
 */

/**
 * @def badlength(a0, lastx)
 * @brief Warn about row width and increment its recovery counter.
 * @ingroup libtiff_private_fax
 *
 * Calls Fax3BadLength() with the enclosing @c module, @c tif and
 * @c sp->line, then increments sp->eolReachedCount.
 *
 * @param[in] a0 Decoded row length.
 * @param[in] lastx Expected row width in pixels.
 */

/**
 * @def prematureEOF(a0)
 * @brief Warn about input exhaustion and increment its recovery counter.
 * @ingroup libtiff_private_fax
 *
 * Calls Fax3PrematureEOF() with the enclosing @c module, @c tif and
 * @c sp->line, then increments sp->eofReachedCount.
 *
 * @param[in] a0 Pixel position reached in the row.
 */

/**
 * @def tryG3WithoutEOL(a0)
 * @brief Warn about retrying Group3 data without EOL.
 * @ingroup libtiff_private_fax
 *
 * Calls Fax3TryG3WithoutEOL() with the enclosing @c module, @c tif and
 * @c sp->line. No recovery counter is incremented.
 *
 * @param[in] a0 Pixel position reached in the row.
 */

/**
 * @def SWAP(t, a, b)
 * @brief Exchange current and reference run-array pointers.
 * @ingroup libtiff_private_fax
 *
 * Exchanges the lvalues @p a and @p b through a temporary of type @p t.
 * Expands to a braced block, not a do/while statement. Defined separately
 * for Fax3Decode2D() and Fax4Decode() and undefined after each.
 *
 * @param[in] t Type of @p a and @p b.
 * @param[in,out] a First lvalue.
 * @param[in,out] b Second lvalue.
 */

/**
 * @def FILL(n, cp)
 * @brief Set complete black output bytes and advance the pointer.
 * @ingroup libtiff_private_fax
 *
 * Writes @p n bytes of 0xff at @p cp, then advances @p cp by @p n. Expands
 * to two statements without a do/while wrapper, so it must be braced when
 * used as the body of a conditional.
 *
 * @param[in] n Number of bytes to set.
 * @param[in,out] cp Output byte pointer.
 */

/**
 * @def ZERO(n, cp)
 * @brief Clear complete white output bytes and advance the pointer.
 * @ingroup libtiff_private_fax
 *
 * Writes @p n zero bytes at @p cp, then advances @p cp by @p n. Expands to
 * two statements without a do/while wrapper, so it must be braced when used
 * as the body of a conditional.
 *
 * @param[in] n Number of bytes to clear.
 * @param[in,out] cp Output byte pointer.
 */

/**
 * @def Fax3FlushBits(tif, sp)
 * @brief Append a partial code byte and flush raw output when full.
 * @ingroup libtiff_private_fax
 *
 * If the raw buffer is full (tif_rawcc >= tif_rawdatasize), calls
 * TIFFFlushData1() and executes return 0 from the enclosing function if it
 * fails. Then appends the low byte of sp->data at tif_rawcp, advances
 * tif_rawcp and tif_rawcc, and sets sp->data to 0 and sp->bit to 8.
 * Expands to a braced block, not a do/while statement. Both operands are
 * evaluated several times.
 *
 * @param[in,out] tif Encoding handle whose raw buffer is written.
 * @param[in,out] sp Encoder state whose data and bit are flushed and reset.
 */

/**
 * @def _FlushBits(tif)
 * @brief Append local code bits and reset local byte state.
 * @ingroup libtiff_private_fax
 *
 * Same as Fax3FlushBits() but uses the enclosing function's locals data and
 * bit instead of sp->data and sp->bit: appends the low byte of data to the
 * raw buffer and sets data to 0 and bit to 8. Executes return 0 from the
 * enclosing function if TIFFFlushData1() fails. Expands to a braced block.
 *
 * @param[in,out] tif Encoding handle whose raw buffer is written.
 */

/**
 * @def _PutBits(tif, bits, length)
 * @brief Append local code bits across partial-byte boundaries.
 * @ingroup libtiff_private_fax
 *
 * Appends the low @p length bits of @p bits, most significant first, to the
 * enclosing function's locals data and bit, writing each completed byte
 * with _FlushBits(). @p bits must have no set bits above @p length. Updates
 * data and bit, and decrements @p length while splitting the code, so
 * @p length must be a modifiable lvalue; the final partial byte asserts
 * @p length < 9. If a flush fails, the enclosing function returns 0 before
 * it copies data and bit back to sp->data and sp->bit, while bytes already
 * flushed stay in the raw buffer. Expands to a braced block; @p bits and
 * @p length are evaluated several times.
 *
 * @param[in,out] tif Encoding handle whose raw buffer is written.
 * @param[in] bits Code value to write.
 * @param[in,out] length Number of bits to write; modified by the expansion.
 */

/**
 * @def putcode(tif, te)
 * @brief Emit one Huffman table entry.
 * @ingroup libtiff_private_fax
 *
 * Expands to Fax3PutBits(tif, te->code, te->length), so its value is 1 on
 * success and 0 when flushing raw data fails.
 *
 * @param[in,out] tif Encoding handle.
 * @param[in] te Code table entry to write.
 */

/**
 * @def finddiff(_cp, _bs, _be, _color)
 * @brief Find the next pixel differing from the selected colour.
 * @ingroup libtiff_private_fax
 *
 * Returns the offset of the first bit in [@p _bs, @p _be) of @p _cp that
 * differs from @p _color, or @p _be if there is none. @p _bs is evaluated
 * twice.
 *
 * @param[in] _cp Packed MSB-first row.
 * @param[in] _bs Starting bit offset.
 * @param[in] _be End bit offset.
 * @param[in] _color Colour of the run to skip (0 or 1).
 */

/**
 * @def finddiff2(_cp, _bs, _be, _color)
 * @brief Find a change while bounding an already exhausted start.
 * @ingroup libtiff_private_fax
 *
 * Like finddiff(), but returns @p _be when @p _bs is not less than @p _be.
 * @p _bs and @p _be are evaluated more than once.
 *
 * @param[in] _cp Packed MSB-first row.
 * @param[in] _bs Starting bit offset.
 * @param[in] _be End bit offset.
 * @param[in] _color Colour of the run to skip (0 or 1).
 */

/**
 * @def PIXEL(buf, ix)
 * @brief Read one MSB-first packed pixel bit.
 * @ingroup libtiff_private_fax
 *
 * Returns bit @p ix (0 or 1) of @p buf, counting from the most significant bit
 * of byte 0. @p ix is evaluated twice.
 *
 * @param[in] buf Packed row bytes.
 * @param[in] ix Pixel index.
 */

/**
 * @def FIELD_BADFAXLINES
 * @brief Reserve the BadFaxLines codec field bit.
 * @ingroup libtiff_private_fax
 *
 * Codec-private field bit FIELD_CODEC + 0, set with TIFFTAG_BADFAXLINES and
 * tested by Fax3PrintDir().
 */

/**
 * @def FIELD_CLEANFAXDATA
 * @brief Reserve the CleanFaxData codec field bit.
 * @ingroup libtiff_private_fax
 *
 * Codec-private field bit FIELD_CODEC + 1, set with TIFFTAG_CLEANFAXDATA and
 * tested by Fax3PrintDir().
 */

/**
 * @def FIELD_BADFAXRUN
 * @brief Reserve the consecutive-bad-lines codec field bit.
 * @ingroup libtiff_private_fax
 *
 * Codec-private field bit FIELD_CODEC + 2, set with
 * TIFFTAG_CONSECUTIVEBADFAXLINES and tested by Fax3PrintDir().
 */

/**
 * @def FIELD_OPTIONS
 * @brief Reserve the Group3/4 options codec field bit.
 * @ingroup libtiff_private_fax
 *
 * Codec-private field bit FIELD_CODEC + 7, shared by TIFFTAG_GROUP3OPTIONS
 * and TIFFTAG_GROUP4OPTIONS. Fax3VSetField() sets it, and Fax3PrintDir()
 * tests it.
 */

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
 * @file tif_packbits.c
 * @brief Implement the PackBits byte-run codec.
 */

#include "tiffiop.h"
#ifdef PACKBITS_SUPPORT
/*
 * TIFF Library.
 *
 * PackBits Compression Algorithm Support
 */
#include <stdio.h>

#ifndef PACKBITS_READ_ONLY

/**
 * @brief Allocate and cache the PackBits row byte extent.
 * @ingroup libtiff_private_basic_codecs
 *
 * Allocates a handle-owned tmsize_t slot and computes tile-row or scanline
 * size. Requires no previous unreleased tif_data allocation. The calculated
 * size is not checked here; chunk encoding requires it positive. No payload
 * I/O.
 *
 * @param[in,out] tif Non-NULL live handle with matching validated geometry
 * and codec methods; serialise access.
 * @param[in] s Sample plane (ignored).
 * @return One after allocation even if row size is zero, zero on allocation
 * failure.
 */
static int PackBitsPreEncode(TIFF *tif, uint16_t s)
{
    (void)s;

    tif->tif_data = (uint8_t *)_TIFFmallocExt(tif, sizeof(tmsize_t));
    if (tif->tif_data == NULL)
        return (0);
    /*
     * Calculate the scanline/tile-width size in bytes.
     */
    if (isTiled(tif))
        *(tmsize_t *)tif->tif_data = TIFFTileRowSize(tif);
    else
        *(tmsize_t *)tif->tif_data = TIFFScanlineSize(tif);
    return (1);
}

/**
 * @brief Release the PackBits cached row-size allocation.
 * @ingroup libtiff_private_basic_codecs
 *
 * Frees non-NULL tif_data using Ext allocation but does not clear its
 * pointer. Must be paired exactly once with a successful preencode; do not
 * repeat without resetting/reallocating state. No payload flush, I/O or
 * separate diagnostic status.
 *
 * @param[in,out] tif Non-NULL live handle whose tif_data allocation is
 * freed (the pointer is left unchanged); serialise access.
 * @return One unconditionally.
 */
static int PackBitsPostEncode(TIFF *tif)
{
    if (tif->tif_data)
        _TIFFfreeExt(tif, tif->tif_data);
    return (1);
}

/*
 * Encode a run of pixels.
 */
/**
 * @brief Encode one byte run region using PackBits packets.
 * @ingroup libtiff_private_basic_codecs
 *
 * Requires nonnegative input length and an adequately sized usable raw
 * output buffer. Combines literal and repeated runs, caps packet lengths at
 * 128, and flushes preserving pending literals as needed. Does not modify
 * caller input. Output/cursors and file bytes can change before a failed
 * flush; diagnostics follow I/O helpers. No returned ownership.
 *
 * @param[in,out] tif Non-NULL live handle with an allocated raw buffer; the
 * raw cursor and count advance and TIFFFlushData1() can be called; serialise
 * access.
 * @param[in] buf Borrowed readable input covering the requested byte
 * extent.
 * @param[in] cc Nonnegative number of input bytes to encode.
 * @param[in] s Sample plane (ignored).
 * @return One after encoding, zero on flush failure.
 */
static int PackBitsEncode(TIFF *tif, uint8_t *buf, tmsize_t cc, uint16_t s)
{
    unsigned char *bp = (unsigned char *)buf;
    uint8_t *op;
    uint8_t *ep;
    uint8_t *lastliteral;
    long n, slop;
    int b;
    enum
    {
        /** @brief Begin a new packet. */
        BASE,
        /** @brief Extend the current literal packet. */
        LITERAL,
        /** @brief Continue after a repeated-byte packet. */
        RUN,
        /** @brief Remember a literal followed by a run for possible merging. */
        LITERAL_RUN
    } state;

    (void)s;
    op = tif->tif_rawcp;
    ep = tif->tif_rawdata + tif->tif_rawdatasize;
    state = BASE;
    lastliteral = NULL;
    while (cc > 0)
    {
        /*
         * Find the longest string of identical bytes.
         */
        b = *bp++;
        cc--;
        n = 1;
        for (; cc > 0 && b == *bp; cc--, bp++)
            n++;
    again:
        if (op + 2 >= ep)
        { /* insure space for new data */
            /*
             * Be careful about writing the last
             * literal.  Must write up to that point
             * and then copy the remainder to the
             * front of the buffer.
             */
            if (state == LITERAL || state == LITERAL_RUN)
            {
                slop = (long)(op - lastliteral);
                tif->tif_rawcc += (tmsize_t)(lastliteral - tif->tif_rawcp);
                if (!TIFFFlushData1(tif))
                    return (0);
                op = tif->tif_rawcp;
                while (slop-- > 0)
                    *op++ = *lastliteral++;
                lastliteral = tif->tif_rawcp;
            }
            else
            {
                tif->tif_rawcc += (tmsize_t)(op - tif->tif_rawcp);
                if (!TIFFFlushData1(tif))
                    return (0);
                op = tif->tif_rawcp;
            }
        }
        switch (state)
        {
            case BASE: /* initial state, set run/literal */
                if (n > 1)
                {
                    state = RUN;
                    if (n > 128)
                    {
                        *op++ = (uint8_t)-127;
                        *op++ = (uint8_t)b;
                        n -= 128;
                        goto again;
                    }
                    *op++ = (uint8_t)(-(n - 1));
                    *op++ = (uint8_t)b;
                }
                else
                {
                    lastliteral = op;
                    *op++ = 0;
                    *op++ = (uint8_t)b;
                    state = LITERAL;
                }
                break;
            case LITERAL: /* last object was literal string */
                if (n > 1)
                {
                    state = LITERAL_RUN;
                    if (n > 128)
                    {
                        *op++ = (uint8_t)-127;
                        *op++ = (uint8_t)b;
                        n -= 128;
                        goto again;
                    }
                    *op++ = (uint8_t)(-(n - 1)); /* encode run */
                    *op++ = (uint8_t)b;
                }
                else
                { /* extend literal */
                    if (++(*lastliteral) == 127)
                        state = BASE;
                    *op++ = (uint8_t)b;
                }
                break;
            case RUN: /* last object was run */
                if (n > 1)
                {
                    if (n > 128)
                    {
                        *op++ = (uint8_t)-127;
                        *op++ = (uint8_t)b;
                        n -= 128;
                        goto again;
                    }
                    *op++ = (uint8_t)(-(n - 1));
                    *op++ = (uint8_t)b;
                }
                else
                {
                    lastliteral = op;
                    *op++ = 0;
                    *op++ = (uint8_t)b;
                    state = LITERAL;
                }
                break;
            case LITERAL_RUN: /* literal followed by a run */
                /*
                 * Check to see if previous run should
                 * be converted to a literal, in which
                 * case we convert literal-run-literal
                 * to a single literal.
                 */
                if (n == 1 && op[-2] == (uint8_t)-1 && *lastliteral < 126)
                {
                    state =
                        (((*lastliteral) = (uint8_t)(*lastliteral + 2)) == 127
                             ? BASE
                             : LITERAL);
                    op[-2] = op[-1]; /* replicate */
                }
                else
                    state = RUN;
                goto again;
            default:
                break;
        }
    }
    tif->tif_rawcc += (tmsize_t)(op - tif->tif_rawcp);
    tif->tif_rawcp = op;
    return (1);
}

/*
 * Encode a rectangular chunk of pixels.  We break it up
 * into row-sized pieces to insure that encoded runs do
 * not span rows.  Otherwise, there can be problems with
 * the decoder if data is read, for example, by scanlines
 * when it was encoded by strips.
 */
/**
 * @brief Split PackBits input into row-bounded encoding requests.
 * @ingroup libtiff_private_basic_codecs
 *
 * Requires positive cached rowsize and a nonnegative input extent. Breaks
 * at row boundaries, permitting a final partial row. Currently checks the
 * inner encoder for negative status even though its flush failure returns
 * zero, so such failure is not propagated and later chunks can continue.
 * Caller input remains borrowed; raw/file state can change.
 *
 * @param[in,out] tif Non-NULL live handle whose tif_data holds the cached
 * row size from PackBitsPreEncode(); serialise access.
 * @param[in] bp Borrowed readable input of cc bytes.
 * @param[in] cc Nonnegative number of input bytes, split into row-sized
 * chunks.
 * @param[in] s Sample plane, forwarded unchanged to PackBitsEncode().
 * @return One on normal loop completion; inner zero failures are not
 * reflected by this wrapper, so the -1 path is unreachable.
 */
static int PackBitsEncodeChunk(TIFF *tif, uint8_t *bp, tmsize_t cc, uint16_t s)
{
    tmsize_t rowsize = *(tmsize_t *)tif->tif_data;

    while (cc > 0)
    {
        tmsize_t chunk = rowsize;

        if (cc < chunk)
            chunk = cc;

        if (PackBitsEncode(tif, bp, chunk, s) < 0)
            return (-1);
        bp += chunk;
        cc -= chunk;
    }
    return (1);
}

#endif

/**
 * @brief Expand PackBits packets into a bounded byte destination.
 * @ingroup libtiff_private_basic_codecs
 *
 * Consumes raw cached bytes, ignores -128 no-op packets and clips overlong
 * output runs with warnings. Short input warns, clears the unfilled output
 * tail, diagnoses and returns zero. Raw cursor/counts are updated even on
 * incomplete output. No allocation/client I/O or ownership transfer;
 * requires nonnegative output length and disjoint storage.
 *
 * @param[in,out] tif Non-NULL live handle whose raw cursor and count are
 * consumed and updated; serialise access.
 * @param[out] op Borrowed writable output of occ bytes.
 * @param[in] occ Nonnegative decoded output byte extent.
 * @param[in] s Sample plane (ignored).
 * @return One when output extent is filled, zero when input cannot fill it.
 */
static int PackBitsDecode(TIFF *tif, uint8_t *op, tmsize_t occ, uint16_t s)
{
    static const char module[] = "PackBitsDecode";
    int8_t *bp;
    tmsize_t cc;
    long n;
    int b;

    (void)s;
    bp = (int8_t *)tif->tif_rawcp;
    cc = tif->tif_rawcc;
    while (cc > 0 && occ > 0)
    {
        n = (long)*bp++;
        cc--;
        if (n < 0)
        {                  /* replicate next byte -n+1 times */
            if (n == -128) /* nop */
                continue;
            n = -n + 1;
            if (occ < (tmsize_t)n)
            {
                TIFFWarningExtR(tif, module,
                                "Discarding %" TIFF_SSIZE_FORMAT
                                " bytes to avoid buffer overrun",
                                (tmsize_t)n - occ);
                n = (long)occ;
            }
            if (cc == 0)
            {
                TIFFWarningExtR(
                    tif, module,
                    "Terminating PackBitsDecode due to lack of data.");
                break;
            }
            occ -= n;
            b = *bp++;
            cc--;
            while (n-- > 0)
                *op++ = (uint8_t)b;
        }
        else
        { /* copy next n+1 bytes literally */
            if (occ < (tmsize_t)(n + 1))
            {
                TIFFWarningExtR(tif, module,
                                "Discarding %" TIFF_SSIZE_FORMAT
                                " bytes to avoid buffer overrun",
                                (tmsize_t)n - occ + 1);
                n = (long)occ - 1;
            }
            if (cc < (tmsize_t)(n + 1))
            {
                TIFFWarningExtR(
                    tif, module,
                    "Terminating PackBitsDecode due to lack of data.");
                break;
            }
            _TIFFmemcpy(op, bp, ++n);
            op += n;
            occ -= n;
            bp += n;
            cc -= n;
        }
    }
    tif->tif_rawcp = (uint8_t *)bp;
    tif->tif_rawcc = cc;
    if (occ > 0)
    {
        memset(op, 0, (size_t)occ);
        TIFFErrorExtR(tif, module, "Not enough data for scanline %" PRIu32,
                      tif->tif_dir.td_row);
        return (0);
    }
    return (1);
}

/**
 * @brief Report the PackBits expansion heuristic.
 * @ingroup libtiff_private_basic_codecs
 *
 * Returns an empirical ratio without inspecting the handle, allocating,
 * mutating state or doing I/O; not a validity proof or universal bound.
 *
 * @param[in] tif Handle (ignored).
 * @return Sixty-four.
 */
static uint64_t PackBitsGetMaxCompressionRatio(TIFF *tif)
{
    (void)tif;

    /* See README_for_libtiff_developpers.md for raw data used to estimate
     * the maximum compression rate. */

    return 64;
}

int TIFFInitPackBits(TIFF *tif, int scheme)
{
    (void)scheme;
    tif->tif_decoderow = PackBitsDecode;
    tif->tif_decodestrip = PackBitsDecode;
    tif->tif_decodetile = PackBitsDecode;
#ifndef PACKBITS_READ_ONLY
    tif->tif_preencode = PackBitsPreEncode;
    tif->tif_postencode = PackBitsPostEncode;
    tif->tif_encoderow = PackBitsEncode;
    tif->tif_encodestrip = PackBitsEncodeChunk;
    tif->tif_encodetile = PackBitsEncodeChunk;
#endif
    tif->tif_getmaxcompressionratio = PackBitsGetMaxCompressionRatio;

    return (1);
}
#endif /* PACKBITS_SUPPORT */

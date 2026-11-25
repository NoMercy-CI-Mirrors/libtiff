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
 * "Null" Compression Algorithm Support.
 */
/**
 * @file tif_dumpmode.c
 * @brief Implement the uncompressed (COMPRESSION_NONE) codec.
 */

#include "tiffiop.h"

/**
 * @brief Accept uncompressed tag fixup without changes.
 * @ingroup libtiff_private_basic_codecs
 *
 * Ignores tif; no allocation, I/O, diagnostics, ownership or state effects.
 *
 * @param[in] tif Handle (ignored).
 * @return One unconditionally.
 */
static int DumpFixupTags(TIFF *tif)
{
    (void)tif;
    return (1);
}

/*
 * Encode a hunk of pixels.
 */
/**
 * @brief Copy uncompressed bytes into the raw output buffer.
 * @ingroup libtiff_private_basic_codecs
 *
 * Requires positive usable raw capacity and coherent cursor/counts. Copies
 * input unless it already equals rawcp, advances cursors and flushes when
 * full. Flush can write/seek and diagnose; partial file/cache changes
 * survive failure. Nonidentical overlapping regions are not supported. No
 * input ownership transfer.
 *
 * @param[in,out] tif Non-NULL live handle with an allocated raw buffer; the
 * raw cursor and count advance and TIFFFlushData1() can be called; serialise
 * access.
 * @param[in] pp Borrowed readable input of cc bytes.
 * @param[in] cc Nonnegative number of input bytes to buffer.
 * @param[in] s Sample plane (ignored).
 * @return One after all requested bytes buffered, zero on flush failure.
 */
static int DumpModeEncode(TIFF *tif, uint8_t *pp, tmsize_t cc, uint16_t s)
{
    (void)s;
    while (cc > 0)
    {
        tmsize_t n;

        n = cc;
        if (tif->tif_rawcc + n > tif->tif_rawdatasize)
            n = tif->tif_rawdatasize - tif->tif_rawcc;

        assert(n > 0);

        /*
         * Avoid copy if client has setup raw
         * data buffer to avoid extra copy.
         */
        if (tif->tif_rawcp != pp)
            _TIFFmemcpy(tif->tif_rawcp, pp, n);
        tif->tif_rawcp += n;
        tif->tif_rawcc += n;
        pp += n;
        cc -= n;
        if (tif->tif_rawcc >= tif->tif_rawdatasize && !TIFFFlushData1(tif))
            return (0);
    }
    return (1);
}

/*
 * Decode a hunk of pixels.
 */
/**
 * @brief Copy uncompressed raw input into decoded output.
 * @ingroup libtiff_private_basic_codecs
 *
 * Checks available rawcc before copying, skips exact-pointer self-copy,
 * then advances rawcp and reduces rawcc. Requires nonnegative cc and
 * sufficient destination; nonidentical overlap is unsupported. Diagnoses
 * insufficient data without copying. No allocation, file I/O or ownership
 * transfer.
 *
 * @param[in,out] tif Non-NULL live handle whose raw cursor and count are
 * consumed; serialise access.
 * @param[out] buf Borrowed writable decoded output covering the requested
 * byte extent.
 * @param[in] cc Nonnegative number of bytes to copy out of the raw buffer.
 * @param[in] s Sample plane (ignored).
 * @return One after complete copy, zero for insufficient raw bytes.
 */
static int DumpModeDecode(TIFF *tif, uint8_t *buf, tmsize_t cc, uint16_t s)
{
    static const char module[] = "DumpModeDecode";
    (void)s;
    if (tif->tif_rawcc < cc)
    {
        TIFFErrorExtR(tif, module,
                      "Not enough data for scanline %" PRIu32
                      ", expected a request for at most %" TIFF_SSIZE_FORMAT
                      " bytes, got a request for %" TIFF_SSIZE_FORMAT " bytes",
                      tif->tif_dir.td_row, tif->tif_rawcc, cc);
        return (0);
    }
    /*
     * Avoid copy if client has setup raw
     * data buffer to avoid extra copy.
     */
    if (tif->tif_rawcp != buf)
        _TIFFmemcpy(buf, tif->tif_rawcp, cc);
    tif->tif_rawcp += cc;
    tif->tif_rawcc -= cc;
    return (1);
}

/*
 * Seek forwards nrows in the current strip.
 */
/**
 * @brief Advance uncompressed raw input by complete scanlines.
 * @ingroup libtiff_private_basic_codecs
 *
 * Uses cached nonnegative scanline byte size, checks multiplication and
 * available rawcc, then advances rawcp and reduces rawcc. Diagnoses
 * rejection; no client seek, allocation or ownership transfer.
 *
 * @param[in,out] tif Non-NULL live handle whose cached scanline size is read
 * and raw cursor and count are advanced; serialise access.
 * @param[in] nrows Nonnegative number of complete cached scanlines to skip.
 * @return One after cursor adjustment, zero on overflow or insufficient
 * data.
 */
static int DumpModeSeek(TIFF *tif, uint32_t nrows)
{
    tmsize_t seek_size;
    if (nrows > 0 &&
        tif->tif_dir.td_scanlinesize > (tmsize_t)(TIFF_TMSIZE_T_MAX / nrows))
    {
        TIFFErrorExtR(tif, "DumpModeSeek",
                      "Integer overflow computing seek size");
        return (0);
    }
    seek_size = (tmsize_t)nrows * tif->tif_dir.td_scanlinesize;
    if (seek_size > tif->tif_rawcc)
    {
        TIFFErrorExtR(tif, "DumpModeSeek",
                      "Seek beyond end of raw data buffer");
        return (0);
    }
    tif->tif_rawcp += seek_size;
    tif->tif_rawcc -= seek_size;
    return (1);
}

/*
 * Initialize dump mode.
 */
int TIFFInitDumpMode(TIFF *tif, int scheme)
{
    (void)scheme;
    tif->tif_fixuptags = DumpFixupTags;
    tif->tif_decoderow = DumpModeDecode;
    tif->tif_decodestrip = DumpModeDecode;
    tif->tif_decodetile = DumpModeDecode;
    tif->tif_encoderow = DumpModeEncode;
    tif->tif_encodestrip = DumpModeEncode;
    tif->tif_encodetile = DumpModeEncode;
    tif->tif_seek = DumpModeSeek;
    return (1);
}

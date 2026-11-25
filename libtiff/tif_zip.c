/*
 * Copyright (c) 1995-1997 Sam Leffler
 * Copyright (c) 1995-1997 Silicon Graphics, Inc.
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
 * @file tif_zip.c
 * @brief Implement zlib and optional libdeflate compression.
 */

#include "tiffiop.h"
#ifdef ZIP_SUPPORT
/*
 * TIFF Library.
 *
 * ZIP (aka Deflate) Compression Support
 *
 * This file is an interface to the zlib library written by
 * Jean-loup Gailly and Mark Adler.  You must use version 1.0 or later
 * of the library.
 *
 * Optionally, libdeflate (https://github.com/ebiggers/libdeflate) may be used
 * to do the compression and decompression, but only for whole strips and tiles.
 * For scanline access, zlib will be sued as a fallback.
 */
#include "tif_predict.h"
#include "zlib.h"

#if LIBDEFLATE_SUPPORT
#include "libdeflate.h"
#endif
#define LIBDEFLATE_MAX_COMPRESSION_LEVEL 12

#include <stdio.h>

/*
 * Sigh, ZLIB_VERSION is defined as a string so there's no
 * way to do a proper check here.  Instead we guess based
 * on the presence of #defines that were added between the
 * 0.95 and 1.0 distributions.
 */
#if !defined(Z_NO_COMPRESSION) || !defined(Z_DEFLATED)
#error "Antiquated ZLIB software; you must use version 1.0 or later"
#endif

#define SAFE_MSG(sp) ((sp)->stream.msg == NULL ? "" : (sp)->stream.msg)

/*
 * State block for each open TIFF
 * file using ZIP compression/decompression.
 */
/**
 * @brief Own Deflate dependency streams predictor prefix and backend selection.
 * @ingroup libtiff_private_deflate
 *
 * Codec-owned state must remain live until cleanup. Internal buffers and
 * dependency objects have the individual ownership described below;
 * borrowed parent methods must remain callable.
 */
typedef struct
{
    /** @brief Predictor prefix, first in the codec state. */
    TIFFPredictorState predict;
    /**
     * @brief Zlib stream with internal allocations owned until
     * inflateEnd/deflateEnd.
     */
    z_stream stream;
    /** @brief Same-strile decode error latch reset by successful predecode. */
    int read_error;
    /**
     * @brief Stored quality, including rejected attempted assignments; zlib
     * uses a cap of nine.
     */
    int zipquality;
    /** @brief Active zlib mode flags. */
    int state;
    /**
     * @brief Requested zlib or libdeflate backend, validated by the setter
     * after assignment.
     */
    int subcodec;
#if LIBDEFLATE_SUPPORT
    /**
     * @brief Per-strile selection: minus one undecided, zero zlib, one
     * whole-strile libdeflate.
     */
    int libdeflate_state;
    /**
     * @brief Owned optional decompressor released by
     * libdeflate_free_decompressor.
     */
    struct libdeflate_decompressor *libdeflate_dec;
    /**
     * @brief Owned optional compressor released by libdeflate_free_compressor
     * or quality change.
     */
    struct libdeflate_compressor *libdeflate_enc;
#endif
#define ZSTATE_INIT_DECODE 0x01
#define ZSTATE_INIT_ENCODE 0x02

    /** @brief Borrowed parent getter restored on cleanup. */
    TIFFVGetMethod vgetparent;
    /** @brief Borrowed parent setter restored on cleanup. */
    TIFFVSetMethod vsetparent;
} ZIPState;

#define GetZIPState(tif) ((ZIPState *)(tif)->tif_data)
#define ZIPDecoderState(tif) GetZIPState(tif)
#define ZIPEncoderState(tif) GetZIPState(tif)

static int ZIPEncode(TIFF *tif, uint8_t *bp, tmsize_t cc, uint16_t s);
static int ZIPDecode(TIFF *tif, uint8_t *op, tmsize_t occ, uint16_t s);

/**
 * @brief Accept Deflate tag fixup without changes.
 * @ingroup libtiff_private_deflate
 *
 * No allocation, I/O, diagnostics, state or ownership effects.
 *
 * @param[in] tif Unused handle.
 * @return One unconditionally.
 */
static int ZIPFixupTags(TIFF *tif)
{
    (void)tif;
    return (1);
}

/**
 * @brief Initialise zlib decode mode and end an active encoder.
 * @ingroup libtiff_private_deflate
 *
 * Ends previous encode state and lazily calls inflateInit only when needed,
 * supporting repeated successful setup calls. Dependency memory uses the
 * handle Ext allocator hooks. Diagnostics report setup failure;
 * mode/dependency changes are not rolled back. No pixel I/O.
 *
 * @param[in,out] tif Non-NULL live Deflate handle with predictor-prefixed
 * state and coherent raw buffers; serialise operations.
 * @return One after setup, zero on inflate initialisation failure.
 */
static int ZIPSetupDecode(TIFF *tif)
{
    static const char module[] = "ZIPSetupDecode";
    ZIPState *sp = ZIPDecoderState(tif);

    assert(sp != NULL);

    /* if we were last encoding, terminate this mode */
    if (sp->state & ZSTATE_INIT_ENCODE)
    {
        deflateEnd(&sp->stream);
        sp->state = 0;
    }

    /* This function can possibly be called several times by */
    /* PredictorSetupDecode() if this function succeeds but */
    /* PredictorSetup() fails */
    if ((sp->state & ZSTATE_INIT_DECODE) == 0 &&
        inflateInit(&sp->stream) != Z_OK)
    {
        TIFFErrorExtR(tif, module, "%s", SAFE_MSG(sp));
        return (0);
    }
    else
    {
        sp->state |= ZSTATE_INIT_DECODE;
        return (1);
    }
}

/**
 * @brief Select the smaller of two unsigned values.
 * @ingroup libtiff_private_deflate
 *
 * Pure comparison without allocation, I/O, diagnostics, state or ownership
 * effects.
 *
 * @param[in] a First unsigned value.
 * @param[in] b Second unsigned value.
 * @return Minimum value.
 */
static inline uint64_t TIFF_MIN_UINT64(uint64_t a, uint64_t b)
{
    return a < b ? a : b;
}

/**
 * @brief Bound a zlib chunk to the signed 32-bit maximum.
 * @ingroup libtiff_private_deflate
 *
 * Pure size conversion without allocation, I/O, diagnostics or ownership
 * effects.
 *
 * @param[in] v Unsigned size to bound for zlib.
 * @return Minimum of v and INT32_MAX as uInt.
 */
static inline uInt TIFF_CLAMP_UINT64_TO_INT32_MAX(uint64_t v)
{
    return (uInt)TIFF_MIN_UINT64(v, INT32_MAX);
}

/*
 * Setup state for decoding a strip.
 */
/**
 * @brief Reset zlib decoding and per-strile backend selection.
 * @ingroup libtiff_private_deflate
 *
 * Attempts setup when needed without checking that return directly,
 * attaches raw input, caps its initial dependency extent and resets
 * inflate. Resets libdeflate selection to undecided when compiled and
 * clears read_error only on reset success. Requires successful setup and
 * coherent nonnegative raw counts. No pixel I/O or output ownership
 * transfer.
 *
 * @param[in,out] tif Non-NULL live Deflate handle with predictor-prefixed
 * state and coherent raw buffers; serialise operations.
 * @param[in] s Unused sample-plane argument.
 * @return One after successful inflateReset, zero otherwise.
 */
static int ZIPPreDecode(TIFF *tif, uint16_t s)
{
    ZIPState *sp = ZIPDecoderState(tif);

    (void)s;
    assert(sp != NULL);

    if ((sp->state & ZSTATE_INIT_DECODE) == 0)
        tif->tif_setupdecode(tif);

#if LIBDEFLATE_SUPPORT
    sp->libdeflate_state = -1;
#endif
    sp->stream.next_in = tif->tif_rawdata;
    assert(sizeof(sp->stream.avail_in) == 4); /* if this assert gets raised,
         we need to simplify this code to reflect a ZLib that is likely updated
         to deal with 8byte memory sizes, though this code will respond
         appropriately even before we simplify it */
    sp->stream.avail_in =
        TIFF_CLAMP_UINT64_TO_INT32_MAX((uint64_t)tif->tif_rawcc);
    if (inflateReset(&sp->stream) == Z_OK)
    {
        sp->read_error = 0;
        return 1;
    }
    return 0;
}

/**
 * @brief Expand zlib or eligible whole-strile libdeflate input.
 * @ingroup libtiff_private_deflate
 *
 * Requires successful predecode and nonnegative output extent. Eligible
 * complete striles can lazily allocate libdeflate state (allocation failure
 * falls back to zlib silently); it consumes all raw bytes and accepts
 * insufficient-output-space status for oversized final-strip recovery. A
 * repeated same-strile call after the libdeflate path returns zero
 * silently, leaving output untouched and not latching read_error. Other
 * requests use bounded zlib chunks. Decode errors clear unfilled output
 * (whole output for libdeflate error), latch same-strile failure and
 * diagnose. Zlib can decrement rawcc before an error without
 * committing rawcp, so retry state is not transactional. Latched errors
 * clear requested output until predecode. No caller ownership transfer or
 * direct client I/O.
 *
 * @param[in,out] tif Non-NULL live Deflate handle with predictor-prefixed
 * state and coherent raw buffers; serialise operations.
 * @param[out] op Borrowed writable occ-byte output, disjoint from raw input
 * and state.
 * @param[in] occ Nonnegative requested decoded bytes.
 * @param[in] s Unused sample-plane argument.
 * @return One when output is accepted/filled, zero on detected decode error
 * or repeated whole-strile backend use; not proof of complete frame
 * validation.
 */
static int ZIPDecode(TIFF *tif, uint8_t *op, tmsize_t occ, uint16_t s)
{
    static const char module[] = "ZIPDecode";
    ZIPState *sp = ZIPDecoderState(tif);

    (void)s;
    assert(sp != NULL);
    assert(sp->state == ZSTATE_INIT_DECODE);

    if (sp->read_error)
    {
        memset(op, 0, (size_t)occ);
        TIFFErrorExtR(tif, module,
                      "ZIPDecode: Scanline %" PRIu32 " cannot be read due to "
                      "previous error",
                      tif->tif_dir.td_row);
        return 0;
    }

#if LIBDEFLATE_SUPPORT
    if (sp->libdeflate_state == 1)
        return 0;

    /* If we have libdeflate support and we are asked to read a whole */
    /* strip/tile, then go for using it */
    do
    {
        TIFFDirectory *td = &tif->tif_dir;

        if (sp->libdeflate_state == 0)
            break;
        if (sp->subcodec == DEFLATE_SUBCODEC_ZLIB)
            break;

        /* Check if we are in the situation where we can use libdeflate */
        if (isTiled(tif))
        {
            if (TIFFTileSize64(tif) != (uint64_t)occ)
                break;
        }
        else
        {
            uint32_t strip_height = td->td_imagelength - tif->tif_dir.td_row;
            if (strip_height > td->td_rowsperstrip)
                strip_height = td->td_rowsperstrip;
            if (TIFFVStripSize64(tif, strip_height) != (uint64_t)occ)
                break;
        }

        /* Check for overflow */
        if ((size_t)tif->tif_rawcc != (uint64_t)tif->tif_rawcc)
            break;
        if ((size_t)occ != (uint64_t)occ)
            break;

        /* Go for decompression using libdeflate */
        {
            enum libdeflate_result res;
            if (sp->libdeflate_dec == NULL)
            {
                sp->libdeflate_dec = libdeflate_alloc_decompressor();
                if (sp->libdeflate_dec == NULL)
                {
                    break;
                }
            }

            sp->libdeflate_state = 1;

            res = libdeflate_zlib_decompress(sp->libdeflate_dec, tif->tif_rawcp,
                                             (size_t)tif->tif_rawcc, op,
                                             (size_t)occ, NULL);

            tif->tif_rawcp += tif->tif_rawcc;
            tif->tif_rawcc = 0;

            /* We accept LIBDEFLATE_INSUFFICIENT_SPACE has a return */
            /* There are odd files in the wild where the last strip, when */
            /* it is smaller in height than td_rowsperstrip, actually contains
             */
            /* data for td_rowsperstrip lines. Just ignore that silently. */
            if (res != LIBDEFLATE_SUCCESS &&
                res != LIBDEFLATE_INSUFFICIENT_SPACE)
            {
                memset(op, 0, (size_t)occ);
                TIFFErrorExtR(tif, module, "Decoding error at scanline %lu",
                              (unsigned long)tif->tif_dir.td_row);
                sp->read_error = 1;
                return 0;
            }

            return 1;
        }
    } while (0);
    sp->libdeflate_state = 0;
#endif /* LIBDEFLATE_SUPPORT */

    sp->stream.next_in = tif->tif_rawcp;

    sp->stream.next_out = op;
    assert(sizeof(sp->stream.avail_out) == 4); /* if this assert gets raised,
         we need to simplify this code to reflect a ZLib that is likely updated
         to deal with 8byte memory sizes, though this code will respond
         appropriately even before we simplify it */
    do
    {
        int state;
        uInt avail_in_before =
            TIFF_CLAMP_UINT64_TO_INT32_MAX((uint64_t)tif->tif_rawcc);
        uInt avail_out_before = TIFF_CLAMP_UINT64_TO_INT32_MAX((uint64_t)occ);
        sp->stream.avail_in = avail_in_before;
        sp->stream.avail_out = avail_out_before;
        state = inflate(&sp->stream, Z_PARTIAL_FLUSH);
        tif->tif_rawcc -= (avail_in_before - sp->stream.avail_in);
        occ -= (avail_out_before - sp->stream.avail_out);
        if (state == Z_STREAM_END)
            break;
        if (state == Z_DATA_ERROR)
        {
            memset(sp->stream.next_out, 0, (size_t)occ);
            TIFFErrorExtR(tif, module, "Decoding error at scanline %lu, %s",
                          (unsigned long)tif->tif_dir.td_row, SAFE_MSG(sp));
            sp->read_error = 1;
            return (0);
        }
        if (state != Z_OK)
        {
            memset(sp->stream.next_out, 0, (size_t)occ);
            TIFFErrorExtR(tif, module, "ZLib error: %s", SAFE_MSG(sp));
            sp->read_error = 1;
            return (0);
        }
    } while (occ > 0);
    if (occ != 0)
    {
        TIFFErrorExtR(tif, module,
                      "Not enough data at scanline %lu (short %" PRIu64
                      " bytes)",
                      (unsigned long)tif->tif_dir.td_row, (uint64_t)occ);
        memset(sp->stream.next_out, 0, (size_t)occ);
        sp->read_error = 1;
        return (0);
    }

    tif->tif_rawcp = sp->stream.next_in;

    return (1);
}

/**
 * @brief Initialise zlib encoding at a capped compression quality.
 * @ingroup libtiff_private_deflate
 *
 * Ends previous decode state, caps quality above Z_BEST_COMPRESSION and
 * calls deflateInit using handle allocators. Requires no unreleased active
 * encoder initialisation. Diagnoses dependency errors; state changes are
 * not rolled back. No payload I/O.
 *
 * @param[in,out] tif Non-NULL live Deflate handle with predictor-prefixed
 * state and coherent raw buffers; serialise operations.
 * @return One after setup, zero on dependency failure.
 */
static int ZIPSetupEncode(TIFF *tif)
{
    static const char module[] = "ZIPSetupEncode";
    ZIPState *sp = ZIPEncoderState(tif);
    int cappedQuality;

    assert(sp != NULL);
    if (sp->state & ZSTATE_INIT_DECODE)
    {
        inflateEnd(&sp->stream);
        sp->state = 0;
    }

    cappedQuality = sp->zipquality;
    if (cappedQuality > Z_BEST_COMPRESSION)
        cappedQuality = Z_BEST_COMPRESSION;

    if (deflateInit(&sp->stream, cappedQuality) != Z_OK)
    {
        TIFFErrorExtR(tif, module, "%s", SAFE_MSG(sp));
        return (0);
    }
    else
    {
        sp->state |= ZSTATE_INIT_ENCODE;
        return (1);
    }
}

/*
 * Reset encoding state at the start of a strip.
 */
/**
 * @brief Reset zlib encoding and per-strile backend selection.
 * @ingroup libtiff_private_deflate
 *
 * Attempts setup when mode differs without checking its status directly,
 * attaches raw output with a 32-bit bounded extent and calls deflateReset.
 * Marks optional backend selection undecided. Requires successful setup and
 * positive usable raw capacity. No payload flush or ownership transfer.
 *
 * @param[in,out] tif Non-NULL live Deflate handle with predictor-prefixed
 * state and coherent raw buffers; serialise operations.
 * @param[in] s Unused sample-plane argument.
 * @return One on deflateReset success, zero otherwise.
 */
static int ZIPPreEncode(TIFF *tif, uint16_t s)
{
    ZIPState *sp = ZIPEncoderState(tif);

    (void)s;
    assert(sp != NULL);
    if (sp->state != ZSTATE_INIT_ENCODE)
        tif->tif_setupencode(tif);

#if LIBDEFLATE_SUPPORT
    sp->libdeflate_state = -1;
#endif
    sp->stream.next_out = tif->tif_rawdata;
    assert(sizeof(sp->stream.avail_out) == 4); /* if this assert gets raised,
         we need to simplify this code to reflect a ZLib that is likely updated
         to deal with 8byte memory sizes, though this code will respond
         appropriately even before we simplify it */
    sp->stream.avail_out = (uint64_t)tif->tif_rawdatasize <= 0xFFFFFFFFU
                               ? (uInt)tif->tif_rawdatasize
                               : 0xFFFFFFFFU;
    return (deflateReset(&sp->stream) == Z_OK);
}

/*
 * Encode a chunk of pixels.
 */
/**
 * @brief Compress input with streaming zlib or eligible whole-strile
 * libdeflate.
 * @ingroup libtiff_private_deflate
 *
 * Requires successful preencode and representable nonnegative
 * extents/capacity. Optional libdeflate is used only for full-strile input,
 * nonzero quality and a sufficient worst-case output bound; otherwise zlib
 * is used. Failure to allocate the libdeflate compressor is reported with
 * TIFFErrorExtR() but still falls back to zlib, so the call may succeed
 * after emitting that error. The libdeflate path compresses and flushes a
 * whole frame immediately; a repeated same-strile call afterwards returns
 * zero silently without consuming input. Zlib uses bounded input chunks and
 * flushes full raw storage. Caller bytes remain unchanged. Dependency/flush
 * failures can leave partial state and file bytes; no rollback or ownership
 * transfer.
 *
 * @param[in,out] tif Non-NULL live Deflate handle with predictor-prefixed
 * state and coherent raw buffers; serialise operations.
 * @param[in] bp Borrowed readable cc-byte input; not modified or retained.
 * @param[in] cc Nonnegative input bytes.
 * @param[in] s Unused sample-plane argument.
 * @return One when input accepted, zero on dependency/flush failure or
 * repeated whole-strile backend use.
 */
static int ZIPEncode(TIFF *tif, uint8_t *bp, tmsize_t cc, uint16_t s)
{
    static const char module[] = "ZIPEncode";
    ZIPState *sp = ZIPEncoderState(tif);

    assert(sp != NULL);
    assert(sp->state == ZSTATE_INIT_ENCODE);

    (void)s;

#if LIBDEFLATE_SUPPORT
    if (sp->libdeflate_state == 1)
        return 0;

    /* If we have libdeflate support and we are asked to write a whole */
    /* strip/tile, then go for using it */
    do
    {
        TIFFDirectory *td = &tif->tif_dir;

        if (sp->libdeflate_state == 0)
            break;
        if (sp->subcodec == DEFLATE_SUBCODEC_ZLIB)
            break;

        /* Libdeflate does not support the 0-compression level */
        if (sp->zipquality == Z_NO_COMPRESSION)
            break;

        /* Check if we are in the situation where we can use libdeflate */
        if (isTiled(tif))
        {
            if (TIFFTileSize64(tif) != (uint64_t)cc)
                break;
        }
        else
        {
            uint32_t strip_height = td->td_imagelength - tif->tif_dir.td_row;
            if (strip_height > td->td_rowsperstrip)
                strip_height = td->td_rowsperstrip;
            if (TIFFVStripSize64(tif, strip_height) != (uint64_t)cc)
                break;
        }

        /* Check for overflow */
        if ((size_t)tif->tif_rawdatasize != (uint64_t)tif->tif_rawdatasize)
            break;
        if ((size_t)cc != (uint64_t)cc)
            break;

        /* Go for compression using libdeflate */
        {
            size_t nCompressedBytes;
            if (sp->libdeflate_enc == NULL)
            {
                /* To get results as good as zlib, we asked for an extra */
                /* level of compression */
                sp->libdeflate_enc = libdeflate_alloc_compressor(
                    sp->zipquality == Z_DEFAULT_COMPRESSION ? 7
                    : sp->zipquality >= 6 && sp->zipquality <= 9
                        ? sp->zipquality + 1
                        : sp->zipquality);
                if (sp->libdeflate_enc == NULL)
                {
                    TIFFErrorExtR(tif, module, "Cannot allocate compressor");
                    break;
                }
            }

            /* Make sure the output buffer is large enough for the worse case.
             */
            /* In TIFFWriteBufferSetup(), when libtiff allocates the buffer */
            /* we've taken a 10% margin over the uncompressed size, which should
             */
            /* be large enough even for the the worse case scenario. */
            if (libdeflate_zlib_compress_bound(sp->libdeflate_enc, (size_t)cc) >
                (size_t)tif->tif_rawdatasize)
            {
                break;
            }

            sp->libdeflate_state = 1;
            nCompressedBytes = libdeflate_zlib_compress(
                sp->libdeflate_enc, bp, (size_t)cc, tif->tif_rawdata,
                (size_t)tif->tif_rawdatasize);

            if (nCompressedBytes == 0)
            {
                TIFFErrorExtR(tif, module, "Encoder error at scanline %lu",
                              (unsigned long)tif->tif_dir.td_row);
                return 0;
            }

            tif->tif_rawcc = (tmsize_t)nCompressedBytes;

            if (!TIFFFlushData1(tif))
                return 0;

            return 1;
        }
    } while (0);
    sp->libdeflate_state = 0;
#endif /* LIBDEFLATE_SUPPORT */

    sp->stream.next_in = bp;
    assert(sizeof(sp->stream.avail_in) == 4); /* if this assert gets raised,
         we need to simplify this code to reflect a ZLib that is likely updated
         to deal with 8byte memory sizes, though this code will respond
         appropriately even before we simplify it */
    do
    {
        uInt avail_in_before = TIFF_CLAMP_UINT64_TO_INT32_MAX((uint64_t)cc);
        sp->stream.avail_in = avail_in_before;
        if (deflate(&sp->stream, Z_NO_FLUSH) != Z_OK)
        {
            TIFFErrorExtR(tif, module, "Encoder error: %s", SAFE_MSG(sp));
            return (0);
        }
        if (sp->stream.avail_out == 0)
        {
            tif->tif_rawcc = tif->tif_rawdatasize;
            if (!TIFFFlushData1(tif))
                return 0;
            sp->stream.next_out = tif->tif_rawdata;
            sp->stream.avail_out =
                TIFF_CLAMP_UINT64_TO_INT32_MAX((uint64_t)tif->tif_rawdatasize);
        }
        cc -= (avail_in_before - sp->stream.avail_in);
    } while (cc > 0);
    return (1);
}

/*
 * Finish off an encoded strip by flushing the last
 * string and tacking on an End Of Information code.
 */
/**
 * @brief Finish zlib output or accept an already written libdeflate frame.
 * @ingroup libtiff_private_deflate
 *
 * Returns immediately for completed libdeflate mode. Otherwise runs
 * Z_FINISH until STREAM_END and flushes produced bytes. Requires successful
 * preencode and valid raw capacity. Failures can leave incomplete
 * frames/changed file state; no IFD persistence, durability guarantee or
 * ownership transfer.
 *
 * @param[in,out] tif Non-NULL live Deflate handle with predictor-prefixed
 * state and coherent raw buffers; serialise operations.
 * @return One after completion, zero on dependency or flush failure.
 */
static int ZIPPostEncode(TIFF *tif)
{
    static const char module[] = "ZIPPostEncode";
    ZIPState *sp = ZIPEncoderState(tif);
    int state;

#if LIBDEFLATE_SUPPORT
    if (sp->libdeflate_state == 1)
        return 1;
#endif

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
                        (uint64_t)tif->tif_rawdatasize <= 0xFFFFFFFFU
                            ? (uInt)tif->tif_rawdatasize
                            : 0xFFFFFFFFU;
                }
                break;
            default:
                TIFFErrorExtR(tif, module, "ZLib error: %s", SAFE_MSG(sp));
                return (0);
        }
    } while (state != Z_STREAM_END);
    return (1);
}

/**
 * @brief End Deflate dependency objects and release codec state.
 * @ingroup libtiff_private_deflate
 *
 * Restores predictor and saved tag hooks, ends the active zlib mode, frees
 * optional libdeflate objects using dependency free functions, frees outer
 * Ext state, clears tif_data and resets callbacks. Invalidates all
 * codec-owned views. Does not finish/flush pending payload or close the
 * handle; no status result.
 *
 * @param[in,out] tif Non-NULL live Deflate handle with predictor-prefixed
 * state and coherent raw buffers; serialise operations.
 */
static void ZIPCleanup(TIFF *tif)
{
    ZIPState *sp = GetZIPState(tif);

    assert(sp != 0);

    (void)TIFFPredictorCleanup(tif);

    tif->tif_tagmethods.vgetfield = sp->vgetparent;
    tif->tif_tagmethods.vsetfield = sp->vsetparent;

    if (sp->state & ZSTATE_INIT_ENCODE)
    {
        deflateEnd(&sp->stream);
        sp->state = 0;
    }
    else if (sp->state & ZSTATE_INIT_DECODE)
    {
        inflateEnd(&sp->stream);
        sp->state = 0;
    }

#if LIBDEFLATE_SUPPORT
    if (sp->libdeflate_dec)
        libdeflate_free_decompressor(sp->libdeflate_dec);
    if (sp->libdeflate_enc)
        libdeflate_free_compressor(sp->libdeflate_enc);
#endif

    _TIFFfreeExt(tif, sp);
    tif->tif_data = NULL;

    _TIFFSetDefaultCompressionState(tif);
}

/**
 * @brief Set Deflate quality or backend or delegate another tag.
 * @ingroup libtiff_private_deflate
 *
 * ZIPQUALITY and DEFLATE_SUBCODEC each consume int and store before
 * validation, so rejection retains the attempted value. Quality accepts
 * -1..12, caps zlib at nine, can call deflateParams on an active encoder,
 * and frees cached libdeflate compressor after success. Backend accepts
 * zlib/libdeflate constants but rejects unavailable libdeflate support.
 * Parent tags follow their variadic contracts. Diagnostics/state/dependency
 * changes are not rolled back; no caller ownership transfer.
 *
 * @param[in,out] tif Non-NULL live Deflate handle with predictor-prefixed
 * state and coherent raw buffers; serialise operations.
 * @param[in] tag Pseudo-tag or delegated tag identifier.
 * @param[in,out] ap Live tag-specific va_list cursor consumed
 * synchronously; not retained.
 * @return One on accepted codec assignment, zero on validation/dependency
 * failure, or parent setter status.
 */
static int ZIPVSetField(TIFF *tif, uint32_t tag, va_list ap)
{
    static const char module[] = "ZIPVSetField";
    ZIPState *sp = GetZIPState(tif);

    switch (tag)
    {
        case TIFFTAG_ZIPQUALITY:
            sp->zipquality = (int)va_arg(ap, int);
            if (sp->zipquality < Z_DEFAULT_COMPRESSION ||
                sp->zipquality > LIBDEFLATE_MAX_COMPRESSION_LEVEL)
            {
                TIFFErrorExtR(
                    tif, module,
                    "Invalid ZipQuality value. Should be in [-1,%d] range",
                    LIBDEFLATE_MAX_COMPRESSION_LEVEL);
                return 0;
            }

            if (sp->state & ZSTATE_INIT_ENCODE)
            {
                int cappedQuality = sp->zipquality;
                if (cappedQuality > Z_BEST_COMPRESSION)
                    cappedQuality = Z_BEST_COMPRESSION;
                if (deflateParams(&sp->stream, cappedQuality,
                                  Z_DEFAULT_STRATEGY) != Z_OK)
                {
                    TIFFErrorExtR(tif, module, "ZLib error: %s", SAFE_MSG(sp));
                    return (0);
                }
            }

#if LIBDEFLATE_SUPPORT
            if (sp->libdeflate_enc)
            {
                libdeflate_free_compressor(sp->libdeflate_enc);
                sp->libdeflate_enc = NULL;
            }
#endif

            return (1);

        case TIFFTAG_DEFLATE_SUBCODEC:
            sp->subcodec = (int)va_arg(ap, int);
            if (sp->subcodec != DEFLATE_SUBCODEC_ZLIB &&
                sp->subcodec != DEFLATE_SUBCODEC_LIBDEFLATE)
            {
                TIFFErrorExtR(tif, module, "Invalid DeflateCodec value.");
                return 0;
            }
#if !LIBDEFLATE_SUPPORT
            if (sp->subcodec == DEFLATE_SUBCODEC_LIBDEFLATE)
            {
                TIFFErrorExtR(tif, module,
                              "DeflateCodec = DEFLATE_SUBCODEC_LIBDEFLATE "
                              "unsupported in this build");
                return 0;
            }
#endif
            return 1;

        default:
            return (*sp->vsetparent)(tif, tag, ap);
    }
    /*NOTREACHED*/
}

/**
 * @brief Retrieve Deflate quality or backend or delegate a tag query.
 * @ingroup libtiff_private_deflate
 *
 * ZIPQUALITY and DEFLATE_SUBCODEC consume non-NULL int* outputs. Other tags
 * follow the parent getter contract. No allocation or ownership transfer
 * for codec values.
 *
 * @param[in,out] tif Non-NULL live Deflate handle with predictor-prefixed
 * state and coherent raw buffers; serialise operations.
 * @param[in] tag Pseudo-tag or delegated tag identifier.
 * @param[in,out] ap Live tag-specific va_list cursor consumed
 * synchronously; not retained.
 * @return One for codec retrieval, otherwise parent getter status.
 */
static int ZIPVGetField(TIFF *tif, uint32_t tag, va_list ap)
{
    ZIPState *sp = GetZIPState(tif);

    switch (tag)
    {
        case TIFFTAG_ZIPQUALITY:
            *va_arg(ap, int *) = sp->zipquality;
            break;

        case TIFFTAG_DEFLATE_SUBCODEC:
            *va_arg(ap, int *) = sp->subcodec;
            break;

        default:
            return (*sp->vgetparent)(tif, tag, ap);
    }
    return (1);
}

/**
 * @brief Define compression quality and backend pseudo-tags.
 * @ingroup libtiff_private_deflate
 *
 * Immutable library-lifetime metadata merged into a handle; no caller
 * ownership transfer.
 */
static const TIFFField zipFields[] = {
    {TIFFTAG_ZIPQUALITY, 0, 0, TIFF_ANY, 0, TIFF_SETGET_INT, FIELD_PSEUDO, TRUE,
     FALSE, "", NULL},
    {TIFFTAG_DEFLATE_SUBCODEC, 0, 0, TIFF_ANY, 0, TIFF_SETGET_INT, FIELD_PSEUDO,
     TRUE, FALSE, "", NULL},
};

/**
 * @brief Allocate zlib internal storage through handle Ext allocation.
 * @ingroup libtiff_private_deflate
 *
 * Requires positive size and a product representable in unsigned int and
 * tmsize_t. Checks items against SIZE_MAX/size, then multiplies
 * unsigned-int operands; that guard alone does not prevent narrower
 * multiplication wrap. Memory is not zeroed by this wrapper. Dependency
 * owns the result until TIFF_zfree. Overflow diagnoses through the handle;
 * allocation limits follow Ext semantics. No pixel I/O.
 *
 * @param[in,out] opaque Borrowed live TIFF handle supplied as zlib
 * allocator context.
 * @param[in] items Requested object count; product must fit both unsigned
 * int arithmetic and tmsize_t.
 * @param[in] size Positive bytes per object; zero would divide by zero in
 * the overflow guard.
 * @return Owned dependency allocation, or NULL for rejected
 * extent/allocation failure.
 */
static void *TIFF_zalloc(void *opaque, unsigned int items, unsigned int size)
{
    static const char module[] = "TIFF_zalloc";
    TIFF *tif = (TIFF *)opaque;

    if (items > ~(size_t)0 / size)
    {
        TIFFErrorExtR(tif, module, "Overflow");
        return NULL;
    }

    return _TIFFmallocExt(tif, items * size);
}

/**
 * @brief Release zlib storage through the matching handle Ext allocator.
 * @ingroup libtiff_private_deflate
 *
 * Consumes ptr and invalidates all views of it. Does not free the borrowed
 * handle, perform payload I/O or return status; NULL storage is allowed.
 *
 * @param[in,out] opaque Borrowed live TIFF handle supplied as zlib
 * allocator context.
 * @param[in] ptr Allocation from the matching Ext family, or NULL; consumed
 * by free.
 */
static void TIFF_zfree(void *opaque, void *ptr)
{
    _TIFFfreeExt((TIFF *)opaque, ptr);
}

/**
 * @brief Report the Deflate expansion allocation heuristic.
 * @ingroup libtiff_private_deflate
 *
 * Ignores the handle; no allocation, I/O, diagnostics, state or ownership
 * effects. Not a validation guarantee.
 *
 * @param[in] tif Unused handle.
 * @return One thousand thirty-two.
 */
static uint64_t ZIPGetMaxCompressionRatio(TIFF *tif)
{
    (void)tif;
    /* cf https://zlib.net/zlib_tech.html */
    return 1032;
}

int TIFFInitZIP(TIFF *tif, int scheme)
{
    static const char module[] = "TIFFInitZIP";
    ZIPState *sp;

    assert((scheme == COMPRESSION_DEFLATE) ||
           (scheme == COMPRESSION_ADOBE_DEFLATE));
#ifdef NDEBUG
    (void)scheme;
#endif

    /*
     * Merge codec-specific tag information.
     */
    if (!_TIFFMergeFields(tif, zipFields, TIFFArrayCount(zipFields)))
    {
        TIFFErrorExtR(tif, module,
                      "Merging Deflate codec-specific tags failed");
        return 0;
    }

    /*
     * Allocate state block so tag methods have storage to record values.
     */
    tif->tif_data = (uint8_t *)_TIFFcallocExt(tif, sizeof(ZIPState), 1);
    if (tif->tif_data == NULL)
        goto bad;
    sp = GetZIPState(tif);
    sp->stream.zalloc = TIFF_zalloc;
    sp->stream.zfree = TIFF_zfree;
    sp->stream.opaque = tif;
    sp->stream.data_type = Z_BINARY;

    /*
     * Override parent get/set field methods.
     */
    sp->vgetparent = tif->tif_tagmethods.vgetfield;
    tif->tif_tagmethods.vgetfield = ZIPVGetField; /* hook for codec tags */
    sp->vsetparent = tif->tif_tagmethods.vsetfield;
    tif->tif_tagmethods.vsetfield = ZIPVSetField; /* hook for codec tags */

    /* Default values for codec-specific fields */
    sp->zipquality = Z_DEFAULT_COMPRESSION; /* default comp. level */
    sp->state = 0;
#if LIBDEFLATE_SUPPORT
    sp->subcodec = DEFLATE_SUBCODEC_LIBDEFLATE;
#else
    sp->subcodec = DEFLATE_SUBCODEC_ZLIB;
#endif

    /*
     * Install codec methods.
     */
    tif->tif_fixuptags = ZIPFixupTags;
    tif->tif_setupdecode = ZIPSetupDecode;
    tif->tif_predecode = ZIPPreDecode;
    tif->tif_decoderow = ZIPDecode;
    tif->tif_decodestrip = ZIPDecode;
    tif->tif_decodetile = ZIPDecode;
    tif->tif_setupencode = ZIPSetupEncode;
    tif->tif_preencode = ZIPPreEncode;
    tif->tif_postencode = ZIPPostEncode;
    tif->tif_encoderow = ZIPEncode;
    tif->tif_encodestrip = ZIPEncode;
    tif->tif_encodetile = ZIPEncode;
    tif->tif_cleanup = ZIPCleanup;
    tif->tif_getmaxcompressionratio = ZIPGetMaxCompressionRatio;
    /*
     * Setup predictor setup.
     */
    (void)TIFFPredictorInit(tif);
    return (1);
bad:
    TIFFErrorExtR(tif, module, "No space for ZIP state block");
    return (0);
}
#endif /* ZIP_SUPPORT */

/**
 * @def LIBDEFLATE_MAX_COMPRESSION_LEVEL
 * @brief Highest accepted TIFFTAG_ZIPQUALITY value: 12.
 * @ingroup libtiff_private_deflate
 *
 * Matches the libdeflate maximum level; the zlib path caps the level at
 * Z_BEST_COMPRESSION (9). The tag setter rejects values outside [-1, 12].
 */

/**
 * @def SAFE_MSG(sp)
 * @brief Borrow the zlib stream error text, or an empty string if none.
 * @ingroup libtiff_private_deflate
 *
 * Evaluates sp twice, so the operand must have no side effects.
 *
 * @param[in] sp ZIPState whose stream.msg is read.
 */

/**
 * @def ZSTATE_INIT_DECODE
 * @brief Bit 0x01 of the codec state field: decoding has been set up.
 * @ingroup libtiff_private_deflate
 *
 * Set by ZIPSetupDecode(). Each setup first tears down the other mode, if
 * active, and clears the field.
 */

/**
 * @def ZSTATE_INIT_ENCODE
 * @brief Bit 0x02 of the codec state field: encoding has been set up.
 * @ingroup libtiff_private_deflate
 *
 * Set by ZIPSetupEncode(). Each setup first tears down the other mode, if
 * active, and clears the field.
 */

/**
 * @def GetZIPState(tif)
 * @brief Cast tif->tif_data to the ZIPState.
 * @ingroup libtiff_private_deflate
 *
 * tif is evaluated once. Requires the codec to be initialised (tif_data
 * non-NULL).
 *
 * @param[in] tif Handle whose codec state is returned.
 */

/**
 * @def ZIPDecoderState(tif)
 * @brief Alias of GetZIPState(), returning the ZIPState for decoding.
 * @ingroup libtiff_private_deflate
 *
 * tif is evaluated once. Requires the codec to be initialised.
 *
 * @param[in] tif Handle whose codec state is returned.
 */

/**
 * @def ZIPEncoderState(tif)
 * @brief Alias of GetZIPState(), returning the ZIPState for encoding.
 * @ingroup libtiff_private_deflate
 *
 * tif is evaluated once. Requires the codec to be initialised.
 *
 * @param[in] tif Handle whose codec state is returned.
 */

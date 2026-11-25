/*
 * Copyright (c) 2010, Andrey Kiselev <dron@ak4719.spb.edu>
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
 * @file tif_lzma.c
 * @brief Implement filtered predictor-aware LZMA2 streaming.
 */

#include "tiffiop.h"
#ifdef LZMA_SUPPORT
/*
 * TIFF Library.
 *
 * LZMA2 Compression Support
 *
 * You need an LZMA2 SDK to link with. See http://tukaani.org/xz/ for details.
 *
 * The codec is derived from ZLIB codec (tif_zip.c).
 */

#include "lzma.h"
#include "tif_predict.h"

#include <stdio.h>

/*
 * State block for each open TIFF file using LZMA2 compression/decompression.
 */
/**
 * @brief Own a filtered liblzma stream and predictor-prefixed methods.
 * @ingroup libtiff_private_lzma
 *
 * Codec-owned state must remain live until cleanup. Internal buffers and
 * dependency objects have the individual ownership described below;
 * borrowed parent methods must remain callable.
 */
typedef struct
{
    /** @brief Predictor prefix, first in codec state. */
    TIFFPredictorState predict;
    /**
     * @brief Same-strile decode failure latch reset after successful predecode.
     */
    int read_error;
    /** @brief Dependency stream owning internal allocations until lzma_end. */
    lzma_stream stream;
    /**
     * @brief Terminated delta/LZMA2 filter descriptors borrowing embedded
     * option members.
     */
    lzma_filter filters[LZMA_FILTERS_MAX + 1];
    /** @brief Embedded byte-delta options referenced by filters. */
    lzma_options_delta opt_delta;
    /** @brief Embedded LZMA2 options referenced by filters. */
    lzma_options_lzma opt_lzma;
    /**
     * @brief Cached compression preset; setter stores even when option
     * conversion fails.
     */
    int preset;
    /** @brief Dependency integrity check, default LZMA_CHECK_NONE. */
    lzma_check check;
    /** @brief Initialised decode/encode mode bits. */
    int state;
#define LSTATE_INIT_DECODE 0x01
#define LSTATE_INIT_ENCODE 0x02

    /** @brief Borrowed parent tag getter restored during cleanup. */
    TIFFVGetMethod vgetparent;
    /** @brief Borrowed parent tag setter restored during cleanup. */
    TIFFVSetMethod vsetparent;
} LZMAState;

#define GetLZMAState(tif) ((LZMAState *)(tif)->tif_data)
#define LZMADecoderState(tif) GetLZMAState(tif)
#define LZMAEncoderState(tif) GetLZMAState(tif)

static int LZMAEncode(TIFF *tif, uint8_t *bp, tmsize_t cc, uint16_t s);
static int LZMADecode(TIFF *tif, uint8_t *op, tmsize_t occ, uint16_t s);

/**
 * @brief Describe a liblzma status using immutable text.
 * @ingroup libtiff_private_lzma
 *
 * Returns static library-lifetime string literals; unknown or newly added
 * codes use a generic message. Do not modify or free the borrowed text. No
 * allocation, I/O, diagnostics or state effects.
 *
 * @param[in] ret Dependency status code to describe.
 * @return Borrowed non-NULL terminated status description.
 */
static const char *LZMAStrerror(lzma_ret ret)
{
    switch (ret)
    {
        case LZMA_OK:
            return "operation completed successfully";
        case LZMA_STREAM_END:
            return "end of stream was reached";
        case LZMA_NO_CHECK:
            return "input stream has no integrity check";
        case LZMA_UNSUPPORTED_CHECK:
            return "cannot calculate the integrity check";
        case LZMA_GET_CHECK:
            return "integrity check type is now available";
        case LZMA_MEM_ERROR:
            return "cannot allocate memory";
        case LZMA_MEMLIMIT_ERROR:
            return "memory usage limit was reached";
        case LZMA_FORMAT_ERROR:
            return "file format not recognized";
        case LZMA_OPTIONS_ERROR:
            return "invalid or unsupported options";
        case LZMA_DATA_ERROR:
            return "data is corrupt";
        case LZMA_BUF_ERROR:
            return "no progress is possible (stream is truncated or corrupt)";
        case LZMA_PROG_ERROR:
            return "programming error";
#if LZMA_VERSION >= 50040000 /* 5.4.0 */
        case LZMA_SEEK_NEEDED:
        case LZMA_RET_INTERNAL1:
        case LZMA_RET_INTERNAL2:
        case LZMA_RET_INTERNAL3:
        case LZMA_RET_INTERNAL4:
        case LZMA_RET_INTERNAL5:
        case LZMA_RET_INTERNAL6:
        case LZMA_RET_INTERNAL7:
        case LZMA_RET_INTERNAL8:
#endif
        default:
            return "unidentified liblzma error";
    }
}

/**
 * @brief Accept LZMA tag fixup without changes.
 * @ingroup libtiff_private_lzma
 *
 * No allocation, I/O, diagnostics, state or ownership effects.
 *
 * @param[in] tif Unused handle.
 * @return One unconditionally.
 */
static int LZMAFixupTags(TIFF *tif)
{
    (void)tif;
    return 1;
}

/**
 * @brief Switch the liblzma stream to decode mode.
 * @ingroup libtiff_private_lzma
 *
 * Ends a previous encode stream, clears mode and marks decode setup.
 * Dependency allocations/views from the old mode can be invalidated. No
 * payload I/O or caller storage ownership transfer.
 *
 * @param[in,out] tif Non-NULL live LZMA handle with predictor-prefixed
 * state and valid raw buffers; serialise operations.
 * @return One unconditionally.
 */
static int LZMASetupDecode(TIFF *tif)
{
    LZMAState *sp = LZMADecoderState(tif);

    assert(sp != NULL);

    /* if we were last encoding, terminate this mode */
    if (sp->state & LSTATE_INIT_ENCODE)
    {
        lzma_end(&sp->stream);
        sp->state = 0;
    }

    sp->state |= LSTATE_INIT_DECODE;
    return 1;
}

/*
 * Setup state for decoding a strip.
 */
/**
 * @brief Initialise the liblzma decoder for one strile.
 * @ingroup libtiff_private_lzma
 *
 * Ensures decode mode, attaches raw input and checks size_t round-trip
 * conversion, then calls lzma_stream_decoder with the memory limit
 * disabled. Clears read_error only after successful setup. Dependency
 * allocations remain codec-owned for cleanup; failures diagnose without
 * rollback. No direct client I/O.
 *
 * @param[in,out] tif Non-NULL live LZMA handle with predictor-prefixed
 * state and valid raw buffers; serialise operations.
 * @param[in] s Unused sample-plane compatibility argument.
 * @return One after decoder initialisation, zero on size rejection or
 * dependency setup failure.
 */
static int LZMAPreDecode(TIFF *tif, uint16_t s)
{
    static const char module[] = "LZMAPreDecode";
    LZMAState *sp = LZMADecoderState(tif);
    lzma_ret ret;

    (void)s;
    assert(sp != NULL);

    if ((sp->state & LSTATE_INIT_DECODE) == 0)
        tif->tif_setupdecode(tif);

    sp->stream.next_in = tif->tif_rawdata;
    sp->stream.avail_in = (size_t)tif->tif_rawcc;
    if ((tmsize_t)sp->stream.avail_in != tif->tif_rawcc)
    {
        TIFFErrorExtR(tif, module,
                      "Liblzma cannot deal with buffers this size");
        return 0;
    }

    /*
     * Disable memory limit when decoding. UINT64_MAX is a flag to disable
     * the limit, we are passing (uint64_t)-1 which should be the same.
     */
    ret = lzma_stream_decoder(&sp->stream, (uint64_t)-1, 0);
    if (ret != LZMA_OK)
    {
        TIFFErrorExtR(tif, module, "Error initializing the stream decoder, %s",
                      LZMAStrerror(ret));
        return 0;
    }

    sp->read_error = 0;

    return 1;
}

/**
 * @brief Expand cached liblzma input into caller output.
 * @ingroup libtiff_private_lzma
 *
 * Requires successful predecode. A latched same-strile error clears
 * requested output and fails. Configures input/output and runs until
 * filled/end/error; memory-limit recovery can reinitialise the decoder at
 * reported usage and restore saved input. Short output clears its remaining
 * tail and latches failure. Other dependency errors can diagnose yet return
 * success if output is already full. Only successful completion commits
 * rawcp/rawcc; stream and output can change before failure. No caller
 * ownership transfer or direct client I/O.
 *
 * @param[in,out] tif Non-NULL live LZMA handle with predictor-prefixed
 * state and valid raw buffers; serialise operations.
 * @param[out] op Borrowed writable occ-byte decoded output, disjoint from
 * raw/state storage.
 * @param[in] occ Nonnegative decoded byte count representable by size_t.
 * @param[in] s Unused sample-plane compatibility argument.
 * @return One if requested output fills, zero on latched error, size
 * rejection or short output; not a full-stream integrity guarantee.
 */
static int LZMADecode(TIFF *tif, uint8_t *op, tmsize_t occ, uint16_t s)
{
    static const char module[] = "LZMADecode";
    LZMAState *sp = LZMADecoderState(tif);

    (void)s;
    assert(sp != NULL);
    assert(sp->state == LSTATE_INIT_DECODE);

    if (sp->read_error)
    {
        memset(op, 0, (size_t)occ);
        TIFFErrorExtR(tif, module,
                      "LZMADecode: Scanline %" PRIu32 " cannot be read due to "
                      "previous error",
                      tif->tif_dir.td_row);
        return 0;
    }

    sp->stream.next_in = tif->tif_rawcp;
    sp->stream.avail_in = (size_t)tif->tif_rawcc;

    sp->stream.next_out = op;
    sp->stream.avail_out = (size_t)occ;
    if ((tmsize_t)sp->stream.avail_out != occ)
    {
        // read_error not set here as this is a usage issue that can be
        // recovered in a following call.
        memset(op, 0, (size_t)occ);
        TIFFErrorExtR(tif, module,
                      "Liblzma cannot deal with buffers this size");
        return 0;
    }

    do
    {
        /*
         * Save the current stream state to properly recover from the
         * decoding errors later.
         */
        const uint8_t *next_in = sp->stream.next_in;
        size_t avail_in = sp->stream.avail_in;

        lzma_ret ret = lzma_code(&sp->stream, LZMA_RUN);
        if (ret == LZMA_STREAM_END)
            break;
        if (ret == LZMA_MEMLIMIT_ERROR)
        {
            lzma_ret r =
                lzma_stream_decoder(&sp->stream, lzma_memusage(&sp->stream), 0);
            if (r != LZMA_OK)
            {
                sp->read_error = 1;
                memset(op, 0, (size_t)occ);
                TIFFErrorExtR(tif, module,
                              "Error initializing the stream decoder, %s",
                              LZMAStrerror(r));
                break;
            }
            sp->stream.next_in = next_in;
            sp->stream.avail_in = avail_in;
            continue;
        }
        if (ret != LZMA_OK)
        {
            TIFFErrorExtR(tif, module,
                          "Decoding error at scanline %" PRIu32 ", %s",
                          tif->tif_dir.td_row, LZMAStrerror(ret));
            break;
        }
    } while (sp->stream.avail_out > 0);
    if (sp->stream.avail_out != 0)
    {
        sp->read_error = 1;
        memset(sp->stream.next_out, 0, sp->stream.avail_out);
        TIFFErrorExtR(tif, module,
                      "Not enough data at scanline %" PRIu32
                      " (short %" TIFF_SIZE_FORMAT " bytes)",
                      tif->tif_dir.td_row, sp->stream.avail_out);
        return 0;
    }

    tif->tif_rawcp = (uint8_t *)sp->stream.next_in; /* cast away const */
    tif->tif_rawcc = (tmsize_t)sp->stream.avail_in;

    return 1;
}

/**
 * @brief Switch the liblzma stream to encode mode.
 * @ingroup libtiff_private_lzma
 *
 * Ends previous decode state, clears mode and marks encode setup.
 * Dependency allocations/views can be invalidated. Does not emit payload,
 * allocate caller output or transfer ownership.
 *
 * @param[in,out] tif Non-NULL live LZMA handle with predictor-prefixed
 * state and valid raw buffers; serialise operations.
 * @return One unconditionally.
 */
static int LZMASetupEncode(TIFF *tif)
{
    LZMAState *sp = LZMAEncoderState(tif);

    assert(sp != NULL);
    if (sp->state & LSTATE_INIT_DECODE)
    {
        lzma_end(&sp->stream);
        sp->state = 0;
    }

    sp->state |= LSTATE_INIT_ENCODE;
    return 1;
}

/*
 * Reset encoding state at the start of a strip.
 */
/**
 * @brief Initialise a filtered LZMA2 stream for one output strile.
 * @ingroup libtiff_private_lzma
 *
 * Ensures encode mode, attaches positive-capacity raw output, checks size_t
 * conversion and initialises the configured filter chain/integrity-check
 * mode. Dependency setup can allocate; errors diagnose with partial state
 * retained for cleanup. No payload flush or ownership transfer.
 *
 * @param[in,out] tif Non-NULL live LZMA handle with predictor-prefixed
 * state and valid raw buffers; serialise operations.
 * @param[in] s Unused sample-plane compatibility argument.
 * @return One on encoder setup, zero on size rejection or dependency
 * failure.
 */
static int LZMAPreEncode(TIFF *tif, uint16_t s)
{
    static const char module[] = "LZMAPreEncode";
    LZMAState *sp = LZMAEncoderState(tif);
    lzma_ret ret;

    (void)s;
    assert(sp != NULL);
    if (sp->state != LSTATE_INIT_ENCODE)
        tif->tif_setupencode(tif);

    sp->stream.next_out = tif->tif_rawdata;
    sp->stream.avail_out = (size_t)tif->tif_rawdatasize;
    if ((tmsize_t)sp->stream.avail_out != tif->tif_rawdatasize)
    {
        TIFFErrorExtR(tif, module,
                      "Liblzma cannot deal with buffers this size");
        return 0;
    }
    ret = lzma_stream_encoder(&sp->stream, sp->filters, sp->check);
    if (ret != LZMA_OK)
    {
        TIFFErrorExtR(tif, module, "Error in lzma_stream_encoder(): %s",
                      LZMAStrerror(ret));
        return 0;
    }
    return 1;
}

/*
 * Encode a chunk of pixels.
 */
/**
 * @brief Consume input through the active liblzma encoder.
 * @ingroup libtiff_private_lzma
 *
 * Requires successful preencode and positive raw output capacity. Checks
 * size_t input conversion, calls lzma_code with RUN and flushes full raw
 * buffers. Input remains unchanged. Dependency/flush failure can leave
 * partial stream/file/cache progress and is not transactional; diagnostics
 * follow the failing operation. No ownership transfer.
 *
 * @param[in,out] tif Non-NULL live LZMA handle with predictor-prefixed
 * state and valid raw buffers; serialise operations.
 * @param[in] bp Borrowed readable cc-byte input, consumed synchronously
 * without modification.
 * @param[in] cc Nonnegative input byte count representable by size_t.
 * @param[in] s Unused sample-plane compatibility argument.
 * @return One when input consumed, zero on size/dependency/flush failure.
 */
static int LZMAEncode(TIFF *tif, uint8_t *bp, tmsize_t cc, uint16_t s)
{
    static const char module[] = "LZMAEncode";
    LZMAState *sp = LZMAEncoderState(tif);

    assert(sp != NULL);
    assert(sp->state == LSTATE_INIT_ENCODE);

    (void)s;
    sp->stream.next_in = bp;
    sp->stream.avail_in = (size_t)cc;
    if ((tmsize_t)sp->stream.avail_in != cc)
    {
        TIFFErrorExtR(tif, module,
                      "Liblzma cannot deal with buffers this size");
        return 0;
    }
    do
    {
        lzma_ret ret = lzma_code(&sp->stream, LZMA_RUN);
        if (ret != LZMA_OK)
        {
            TIFFErrorExtR(tif, module,
                          "Encoding error at scanline %" PRIu32 ", %s",
                          tif->tif_dir.td_row, LZMAStrerror(ret));
            return 0;
        }
        if (sp->stream.avail_out == 0)
        {
            tif->tif_rawcc = tif->tif_rawdatasize;
            if (!TIFFFlushData1(tif))
                return 0;
            sp->stream.next_out = tif->tif_rawdata;
            sp->stream.avail_out =
                (size_t)
                    tif->tif_rawdatasize; /* this is a safe typecast, as check
                                             is made already in LZMAPreEncode */
        }
    } while (sp->stream.avail_in > 0);
    return 1;
}

/*
 * Finish off an encoded strip by flushing the last
 * string and tacking on an End Of Information code.
 */
/**
 * @brief Finish the LZMA2 stream and flush produced output.
 * @ingroup libtiff_private_lzma
 *
 * Runs FINISH until STREAM_END and flushes every nonempty raw extent.
 * Requires valid preencode state. Errors can leave an incomplete stream and
 * changed file/cursor state. Does not persist the IFD or promise storage
 * durability; no ownership transfer.
 *
 * @param[in,out] tif Non-NULL live LZMA handle with predictor-prefixed
 * state and valid raw buffers; serialise operations.
 * @return One after stream completion, zero on dependency or flush failure.
 */
static int LZMAPostEncode(TIFF *tif)
{
    static const char module[] = "LZMAPostEncode";
    LZMAState *sp = LZMAEncoderState(tif);
    lzma_ret ret;

    sp->stream.avail_in = 0;
    do
    {
        ret = lzma_code(&sp->stream, LZMA_FINISH);
        switch (ret)
        {
            case LZMA_STREAM_END:
            case LZMA_OK:
                if ((tmsize_t)sp->stream.avail_out != tif->tif_rawdatasize)
                {
                    tif->tif_rawcc =
                        tif->tif_rawdatasize - (tmsize_t)sp->stream.avail_out;
                    if (!TIFFFlushData1(tif))
                        return 0;
                    sp->stream.next_out = tif->tif_rawdata;
                    sp->stream.avail_out =
                        (size_t)
                            tif->tif_rawdatasize; /* this is a safe typecast, as
                                                     check is made already in
                                                     ZIPPreEncode */
                }
                break;
            case LZMA_NO_CHECK:
            case LZMA_UNSUPPORTED_CHECK:
            case LZMA_GET_CHECK:
            case LZMA_MEM_ERROR:
            case LZMA_MEMLIMIT_ERROR:
            case LZMA_FORMAT_ERROR:
            case LZMA_OPTIONS_ERROR:
            case LZMA_DATA_ERROR:
            case LZMA_BUF_ERROR:
            case LZMA_PROG_ERROR:
#if LZMA_VERSION >= 50040000 /* 5.4.0 */
            case LZMA_SEEK_NEEDED:
            case LZMA_RET_INTERNAL1:
            case LZMA_RET_INTERNAL2:
            case LZMA_RET_INTERNAL3:
            case LZMA_RET_INTERNAL4:
            case LZMA_RET_INTERNAL5:
            case LZMA_RET_INTERNAL6:
            case LZMA_RET_INTERNAL7:
            case LZMA_RET_INTERNAL8:
#endif
            default:
                TIFFErrorExtR(tif, module, "Liblzma error: %s",
                              LZMAStrerror(ret));
                return 0;
        }
    } while (ret != LZMA_STREAM_END);
    return 1;
}

/**
 * @brief End the dependency stream and release LZMA codec state.
 * @ingroup libtiff_private_lzma
 *
 * Restores predictor and saved tag hooks, ends active liblzma state, frees
 * the outer Ext allocation, clears tif_data and installs default callbacks.
 * Invalidates codec/dependency views. Does not finalise or flush pending
 * payload, close the handle, or provide an error status.
 *
 * @param[in,out] tif Non-NULL live LZMA handle with predictor-prefixed
 * state and valid raw buffers; serialise operations.
 */
static void LZMACleanup(TIFF *tif)
{
    LZMAState *sp = GetLZMAState(tif);

    assert(sp != 0);

    (void)TIFFPredictorCleanup(tif);

    tif->tif_tagmethods.vgetfield = sp->vgetparent;
    tif->tif_tagmethods.vsetfield = sp->vsetparent;

    if (sp->state)
    {
        lzma_end(&sp->stream);
        sp->state = 0;
    }
    _TIFFfreeExt(tif, sp);
    tif->tif_data = NULL;

    _TIFFSetDefaultCompressionState(tif);
}

/**
 * @brief Store LZMA preset or delegate a tag assignment.
 * @ingroup libtiff_private_lzma
 *
 * TIFFTAG_LZMAPRESET consumes int, stores it, updates LZMA options without
 * checking lzma_lzma_preset status and, in encode mode, reinitialises the
 * stream immediately. Dependency reinitialisation errors diagnose but still
 * return success. This can disturb an active frame; no rollback or
 * ownership transfer. Other tags follow parent variadic contracts.
 *
 * @param[in,out] tif Non-NULL live LZMA handle with predictor-prefixed
 * state and valid raw buffers; serialise operations.
 * @param[in] tag Codec pseudo-tag or delegated tag identifier.
 * @param[in,out] ap Live tag-specific va_list consumed synchronously; not
 * retained.
 * @return One for preset assignment including diagnosed reinitialisation
 * errors, otherwise parent setter status.
 */
static int LZMAVSetField(TIFF *tif, uint32_t tag, va_list ap)
{
    static const char module[] = "LZMAVSetField";
    LZMAState *sp = GetLZMAState(tif);

    switch (tag)
    {
        case TIFFTAG_LZMAPRESET:
            sp->preset = (int)va_arg(ap, int);
            lzma_lzma_preset(&sp->opt_lzma, (uint32_t)sp->preset);
            if (sp->state & LSTATE_INIT_ENCODE)
            {
                lzma_ret ret =
                    lzma_stream_encoder(&sp->stream, sp->filters, sp->check);
                if (ret != LZMA_OK)
                {
                    TIFFErrorExtR(tif, module, "Liblzma error: %s",
                                  LZMAStrerror(ret));
                }
            }
            return 1;
        default:
            return (*sp->vsetparent)(tif, tag, ap);
    }
    /*NOTREACHED*/
}

/**
 * @brief Retrieve LZMA preset or delegate a tag query.
 * @ingroup libtiff_private_lzma
 *
 * TIFFTAG_LZMAPRESET consumes a non-NULL int* and writes the cached preset;
 * other tags follow the parent getter contract. No allocation or ownership
 * transfer for the preset.
 *
 * @param[in,out] tif Non-NULL live LZMA handle with predictor-prefixed
 * state and valid raw buffers; serialise operations.
 * @param[in] tag Codec pseudo-tag or delegated tag identifier.
 * @param[in,out] ap Live tag-specific va_list consumed synchronously; not
 * retained.
 * @return One for preset retrieval, otherwise parent getter status.
 */
static int LZMAVGetField(TIFF *tif, uint32_t tag, va_list ap)
{
    LZMAState *sp = GetLZMAState(tif);

    switch (tag)
    {
        case TIFFTAG_LZMAPRESET:
            *va_arg(ap, int *) = sp->preset;
            break;
        default:
            return (*sp->vgetparent)(tif, tag, ap);
    }
    return 1;
}

/**
 * @brief Define the LZMA compression-preset pseudo-tag.
 * @ingroup libtiff_private_lzma
 *
 * Immutable library-lifetime metadata merged into a handle; no caller
 * ownership transfer.
 */
static const TIFFField lzmaFields[] = {
    {TIFFTAG_LZMAPRESET, 0, 0, TIFF_ANY, 0, TIFF_SETGET_INT, FIELD_PSEUDO, TRUE,
     FALSE, "LZMA2 Compression Preset", NULL},
};

/**
 * @brief Report the empirical LZMA expansion heuristic.
 * @ingroup libtiff_private_lzma
 *
 * Ignores the handle; no allocation, I/O, diagnostics, state or ownership
 * effects. This estimate is not a universal bound.
 *
 * @param[in] tif Unused handle.
 * @return Seven thousand.
 */
static uint64_t LZMAGetMaxCompressionRatio(TIFF *tif)
{
    (void)tif;

    /* See README_for_libtiff_developpers.md for raw data used to estimate
     * the maximum compression rate. */

    /* 1024x1024: 3800 */
    /* 4096x4096: 6534 */
    /* 16383x16383: 6846 */
    /* 65536x65536: 6874 */

    return 7000;
}

int TIFFInitLZMA(TIFF *tif, int scheme)
{
    static const char module[] = "TIFFInitLZMA";
    LZMAState *sp;
    lzma_stream tmp_stream = LZMA_STREAM_INIT;

    (void)scheme;
    assert(scheme == COMPRESSION_LZMA);

    /*
     * Merge codec-specific tag information.
     */
    if (!_TIFFMergeFields(tif, lzmaFields, TIFFArrayCount(lzmaFields)))
    {
        TIFFErrorExtR(tif, module, "Merging LZMA2 codec-specific tags failed");
        return 0;
    }

    /*
     * Allocate state block so tag methods have storage to record values.
     */
    tif->tif_data = (uint8_t *)_TIFFmallocExt(tif, sizeof(LZMAState));
    if (tif->tif_data == NULL)
        goto bad;
    sp = GetLZMAState(tif);
    memcpy(&sp->stream, &tmp_stream, sizeof(lzma_stream));

    /*
     * Override parent get/set field methods.
     */
    sp->vgetparent = tif->tif_tagmethods.vgetfield;
    tif->tif_tagmethods.vgetfield = LZMAVGetField; /* hook for codec tags */
    sp->vsetparent = tif->tif_tagmethods.vsetfield;
    tif->tif_tagmethods.vsetfield = LZMAVSetField; /* hook for codec tags */

    /* Default values for codec-specific fields */
    sp->preset = LZMA_PRESET_DEFAULT; /* default comp. level */
    sp->check = LZMA_CHECK_NONE;
    sp->state = 0;

    /* Data filters. So far we are using delta and LZMA2 filters only. */
    sp->opt_delta.type = LZMA_DELTA_TYPE_BYTE;
    /*
     * The sample size in bytes seems to be reasonable distance for delta
     * filter.
     */
    sp->opt_delta.dist = (tif->tif_dir.td_bitspersample % 8)
                             ? 1
                             : tif->tif_dir.td_bitspersample / 8;
    sp->filters[0].id = LZMA_FILTER_DELTA;
    sp->filters[0].options = &sp->opt_delta;

    lzma_lzma_preset(&sp->opt_lzma, (uint32_t)sp->preset);
    sp->filters[1].id = LZMA_FILTER_LZMA2;
    sp->filters[1].options = &sp->opt_lzma;

    sp->filters[2].id = LZMA_VLI_UNKNOWN;
    sp->filters[2].options = NULL;

    /*
     * Install codec methods.
     */
    tif->tif_fixuptags = LZMAFixupTags;
    tif->tif_setupdecode = LZMASetupDecode;
    tif->tif_predecode = LZMAPreDecode;
    tif->tif_decoderow = LZMADecode;
    tif->tif_decodestrip = LZMADecode;
    tif->tif_decodetile = LZMADecode;
    tif->tif_setupencode = LZMASetupEncode;
    tif->tif_preencode = LZMAPreEncode;
    tif->tif_postencode = LZMAPostEncode;
    tif->tif_encoderow = LZMAEncode;
    tif->tif_encodestrip = LZMAEncode;
    tif->tif_encodetile = LZMAEncode;
    tif->tif_cleanup = LZMACleanup;
    tif->tif_getmaxcompressionratio = LZMAGetMaxCompressionRatio;

    /*
     * Setup predictor setup.
     */
    (void)TIFFPredictorInit(tif);
    return 1;
bad:
    TIFFErrorExtR(tif, module, "No space for LZMA2 state block");
    return 0;
}
#endif /* LZMA_SUPPORT */

/**
 * @def LSTATE_INIT_DECODE
 * @brief Bit 0x01 of the codec state field: decoding has been set up.
 * @ingroup libtiff_private_lzma
 *
 * Set by LZMASetupDecode(). Each setup first tears down the other mode, if
 * active, and clears the field.
 */

/**
 * @def LSTATE_INIT_ENCODE
 * @brief Bit 0x02 of the codec state field: encoding has been set up.
 * @ingroup libtiff_private_lzma
 *
 * Set by LZMASetupEncode(). Each setup first tears down the other mode, if
 * active, and clears the field.
 */

/**
 * @def GetLZMAState(tif)
 * @brief Cast tif->tif_data to the LZMAState.
 * @ingroup libtiff_private_lzma
 *
 * tif is evaluated once. Requires the codec to be initialised (tif_data
 * non-NULL).
 *
 * @param[in] tif Handle whose codec state is returned.
 */

/**
 * @def LZMADecoderState(tif)
 * @brief Alias of GetLZMAState(), returning the LZMAState for decoding.
 * @ingroup libtiff_private_lzma
 *
 * tif is evaluated once. Requires the codec to be initialised.
 *
 * @param[in] tif Handle whose codec state is returned.
 */

/**
 * @def LZMAEncoderState(tif)
 * @brief Alias of GetLZMAState(), returning the LZMAState for encoding.
 * @ingroup libtiff_private_lzma
 *
 * tif is evaluated once. Requires the codec to be initialised.
 *
 * @param[in] tif Handle whose codec state is returned.
 */

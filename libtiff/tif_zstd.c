/*
 * Copyright (c) 2017, Planet Labs
 * Author: <even.rouault at spatialys.com>
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
 * @file tif_zstd.c
 * @brief Implement predictor-aware ZSTD streaming compression.
 */

#include "tiffiop.h"
#ifdef ZSTD_SUPPORT
/*
 * TIFF Library.
 *
 * ZSTD Compression Support
 *
 */

#include "tif_predict.h"
#include "zstd.h"

#include <stdio.h>

/*
 * State block for each open TIFF file using ZSTD compression/decompression.
 */
/**
 * @brief Own ZSTD streams predictor prefix and output cursor.
 * @ingroup libtiff_private_zstd
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
     * @brief Owned dependency decompression stream; released by
     * ZSTD_freeDStream.
     */
    ZSTD_DStream *dstream;
    /**
     * @brief Owned dependency compression stream; released by ZSTD_freeCStream.
     */
    ZSTD_CStream *cstream;
    /**
     * @brief Cached level applied on the next compression-stream
     * initialisation.
     */
    int compression_level;
    /** @brief Dependency output descriptor borrowing the handle raw buffer. */
    ZSTD_outBuffer out_buffer;
    /**
     * @brief Decode/encode initialisation flags; switching modes releases the
     * old stream.
     */
    int state;
#define LSTATE_INIT_DECODE 0x01
#define LSTATE_INIT_ENCODE 0x02

    /** @brief Borrowed parent tag getter restored during cleanup. */
    TIFFVGetMethod vgetparent;
    /** @brief Borrowed parent tag setter restored during cleanup. */
    TIFFVSetMethod vsetparent;
} ZSTDState;

#define GetZSTDState(tif) ((ZSTDState *)(tif)->tif_data)
#define ZSTDDecoderState(tif) GetZSTDState(tif)
#define ZSTDEncoderState(tif) GetZSTDState(tif)

static int ZSTDEncode(TIFF *tif, uint8_t *bp, tmsize_t cc, uint16_t s);
static int ZSTDDecode(TIFF *tif, uint8_t *op, tmsize_t occ, uint16_t s);

/**
 * @brief Accept ZSTD tag fixup without changes.
 * @ingroup libtiff_private_zstd
 *
 * No allocation, I/O, diagnostics, state or ownership effects.
 *
 * @param[in] tif Unused handle.
 * @return One unconditionally.
 */
static int ZSTDFixupTags(TIFF *tif)
{
    (void)tif;
    return 1;
}

/**
 * @brief Switch ZSTD state into decode mode.
 * @ingroup libtiff_private_zstd
 *
 * Frees any previous compression stream and clears its pointer, then sets
 * decode flags. Invalidates compression-stream views; no raw/pixel I/O,
 * diagnostics or output allocation. Requires initialised state.
 *
 * @param[in,out] tif Non-NULL live ZSTD handle with valid
 * predictor-prefixed state; serialise operations.
 * @return One unconditionally.
 */
static int ZSTDSetupDecode(TIFF *tif)
{
    ZSTDState *sp = ZSTDDecoderState(tif);

    assert(sp != NULL);

    /* if we were last encoding, terminate this mode */
    if (sp->state & LSTATE_INIT_ENCODE)
    {
        ZSTD_freeCStream(sp->cstream);
        sp->cstream = NULL;
        sp->state = 0;
    }

    sp->state |= LSTATE_INIT_DECODE;
    return 1;
}

/*
 * Setup state for decoding a strip.
 */
/**
 * @brief Create or reset the ZSTD decompression stream for a strile.
 * @ingroup libtiff_private_zstd
 *
 * Ensures decode setup, lazily creates a dependency-owned DStream and
 * resets it. Stream remains codec-owned for cleanup even after init
 * failure. Diagnoses dependency allocation/init errors; no pixel reads or
 * caller storage ownership transfer.
 *
 * @param[in,out] tif Non-NULL live ZSTD handle with valid
 * predictor-prefixed state; serialise operations.
 * @param[in] s Unused sample-plane argument.
 * @return One after reset, zero on dependency allocation/init failure.
 */
static int ZSTDPreDecode(TIFF *tif, uint16_t s)
{
    static const char module[] = "ZSTDPreDecode";
    ZSTDState *sp = ZSTDDecoderState(tif);
    size_t zstd_ret;

    (void)s;
    assert(sp != NULL);

    if ((sp->state & LSTATE_INIT_DECODE) == 0)
        tif->tif_setupdecode(tif);

    if (sp->dstream == NULL)
    {
        sp->dstream = ZSTD_createDStream();
        if (sp->dstream == NULL)
        {
            TIFFErrorExtR(tif, module, "Cannot allocate decompression stream");
            return 0;
        }
    }

    zstd_ret = ZSTD_initDStream(sp->dstream);
    if (ZSTD_isError(zstd_ret))
    {
        TIFFErrorExtR(tif, module, "Error in ZSTD_initDStream(): %s",
                      ZSTD_getErrorName(zstd_ret));
        return 0;
    }

    return 1;
}

/**
 * @brief Decompress cached ZSTD input into caller output.
 * @ingroup libtiff_private_zstd
 *
 * Requires decode mode and initialised DStream. Repeatedly decompresses
 * until output fills, frame ends or input exhausts. On dependency error or
 * short output zeroes the unfilled tail and diagnoses, without committing
 * local raw cursor consumption; stream/output may already change. On
 * success advances rawcp and reduces rawcc. Does not require consuming a
 * complete frame when the requested output fills. No allocation owned by
 * caller, client I/O or ownership transfer.
 *
 * @param[in,out] tif Non-NULL live ZSTD handle with valid
 * predictor-prefixed state; serialise operations.
 * @param[out] op Borrowed writable occ-byte decoded output, disjoint from
 * raw input/state.
 * @param[in] occ Nonnegative decoded byte extent representable by size_t.
 * @param[in] s Unused sample-plane argument.
 * @return One when the requested output is filled, zero on
 * dependency/short-output failure.
 */
static int ZSTDDecode(TIFF *tif, uint8_t *op, tmsize_t occ, uint16_t s)
{
    static const char module[] = "ZSTDDecode";
    ZSTDState *sp = ZSTDDecoderState(tif);
    ZSTD_inBuffer in_buffer;
    ZSTD_outBuffer out_buffer;
    size_t zstd_ret;

    (void)s;
    assert(sp != NULL);
    assert(sp->state == LSTATE_INIT_DECODE);

    in_buffer.src = tif->tif_rawcp;
    in_buffer.size = (size_t)tif->tif_rawcc;
    in_buffer.pos = 0;

    out_buffer.dst = op;
    out_buffer.size = (size_t)occ;
    out_buffer.pos = 0;

    do
    {
        zstd_ret = ZSTD_decompressStream(sp->dstream, &out_buffer, &in_buffer);
        if (ZSTD_isError(zstd_ret))
        {
            memset(op + out_buffer.pos, 0, out_buffer.size - out_buffer.pos);
            TIFFErrorExtR(tif, module, "Error in ZSTD_decompressStream(): %s",
                          ZSTD_getErrorName(zstd_ret));
            return 0;
        }
    } while (zstd_ret != 0 && in_buffer.pos < in_buffer.size &&
             out_buffer.pos < out_buffer.size);

    if (out_buffer.pos < (size_t)occ)
    {
        memset(op + out_buffer.pos, 0, out_buffer.size - out_buffer.pos);
        TIFFErrorExtR(tif, module,
                      "Not enough data at scanline %lu (short %lu bytes)",
                      (unsigned long)tif->tif_dir.td_row,
                      (unsigned long)((size_t)occ - out_buffer.pos));
        return 0;
    }

    tif->tif_rawcp += in_buffer.pos;
    tif->tif_rawcc -= (tmsize_t)in_buffer.pos;

    return 1;
}

/**
 * @brief Switch ZSTD state into encode mode.
 * @ingroup libtiff_private_zstd
 *
 * Frees any previous decompression stream and clears its pointer, then sets
 * encode flags. No pixel I/O or caller-owned output allocation; dependency
 * views are invalidated.
 *
 * @param[in,out] tif Non-NULL live ZSTD handle with valid
 * predictor-prefixed state; serialise operations.
 * @return One unconditionally.
 */
static int ZSTDSetupEncode(TIFF *tif)
{
    ZSTDState *sp = ZSTDEncoderState(tif);

    assert(sp != NULL);
    if (sp->state & LSTATE_INIT_DECODE)
    {
        ZSTD_freeDStream(sp->dstream);
        sp->dstream = NULL;
        sp->state = 0;
    }

    sp->state |= LSTATE_INIT_ENCODE;
    return 1;
}

/*
 * Reset encoding state at the start of a strip.
 */
/**
 * @brief Create and initialise a ZSTD compression stream for a strile.
 * @ingroup libtiff_private_zstd
 *
 * Ensures encode mode, lazily creates CStream, initialises it at the cached
 * compression level and attaches the raw output buffer. Requires positive
 * usable raw capacity. Dependency stream remains codec-owned on later
 * failure; diagnoses dependency allocation/init errors. No payload flush or
 * input ownership transfer.
 *
 * @param[in,out] tif Non-NULL live ZSTD handle with valid
 * predictor-prefixed state; serialise operations.
 * @param[in] s Unused sample-plane argument.
 * @return One after initialisation, zero on dependency allocation/init
 * failure.
 */
static int ZSTDPreEncode(TIFF *tif, uint16_t s)
{
    static const char module[] = "ZSTDPreEncode";
    ZSTDState *sp = ZSTDEncoderState(tif);
    size_t zstd_ret;

    (void)s;
    assert(sp != NULL);
    if (sp->state != LSTATE_INIT_ENCODE)
        tif->tif_setupencode(tif);

    if (sp->cstream == NULL)
    {
        sp->cstream = ZSTD_createCStream();
        if (sp->cstream == NULL)
        {
            TIFFErrorExtR(tif, module, "Cannot allocate compression stream");
            return 0;
        }
    }

    zstd_ret = ZSTD_initCStream(sp->cstream, sp->compression_level);
    if (ZSTD_isError(zstd_ret))
    {
        TIFFErrorExtR(tif, module, "Error in ZSTD_initCStream(): %s",
                      ZSTD_getErrorName(zstd_ret));
        return 0;
    }

    sp->out_buffer.dst = tif->tif_rawdata;
    sp->out_buffer.size = (size_t)tif->tif_rawdatasize;
    sp->out_buffer.pos = 0;

    return 1;
}

/*
 * Encode a chunk of pixels.
 */
/**
 * @brief Compress input through the active ZSTD stream.
 * @ingroup libtiff_private_zstd
 *
 * Requires successful preencode and live positive-capacity raw storage.
 * Consumes borrowed input synchronously, flushing full output buffers via
 * TIFFFlushData1. Dependency/flush failures diagnose where applicable and
 * leave partial stream, cache and file progress. Input bytes are not
 * changed; no ownership transfer or rollback.
 *
 * @param[in,out] tif Non-NULL live ZSTD handle with valid
 * predictor-prefixed state; serialise operations.
 * @param[in] bp Borrowed readable cc-byte input, not retained or modified.
 * @param[in] cc Nonnegative input byte extent representable by size_t.
 * @param[in] s Unused sample-plane argument.
 * @return One when input has been consumed, zero on dependency/flush
 * failure.
 */
static int ZSTDEncode(TIFF *tif, uint8_t *bp, tmsize_t cc, uint16_t s)
{
    static const char module[] = "ZSTDEncode";
    ZSTDState *sp = ZSTDEncoderState(tif);
    ZSTD_inBuffer in_buffer;
    size_t zstd_ret;

    assert(sp != NULL);
    assert(sp->state == LSTATE_INIT_ENCODE);

    (void)s;

    in_buffer.src = bp;
    in_buffer.size = (size_t)cc;
    in_buffer.pos = 0;

    do
    {
        zstd_ret =
            ZSTD_compressStream(sp->cstream, &sp->out_buffer, &in_buffer);
        if (ZSTD_isError(zstd_ret))
        {
            TIFFErrorExtR(tif, module, "Error in ZSTD_compressStream(): %s",
                          ZSTD_getErrorName(zstd_ret));
            return 0;
        }
        if (sp->out_buffer.pos == sp->out_buffer.size)
        {
            tif->tif_rawcc = tif->tif_rawdatasize;
            if (!TIFFFlushData1(tif))
                return 0;
            sp->out_buffer.dst = tif->tif_rawcp;
            sp->out_buffer.pos = 0;
        }
    } while (in_buffer.pos < in_buffer.size);

    return 1;
}

/*
 * Finish off an encoded strip by flushing it.
 */
/**
 * @brief Finish the ZSTD frame and flush its remaining output.
 * @ingroup libtiff_private_zstd
 *
 * Repeatedly ends the active stream and flushes produced bytes. Dependency
 * or I/O failure can leave an incomplete frame and changed state/file
 * bytes. Requires successful preencode and valid raw capacity. Does not
 * write the IFD or promise durability; no ownership transfer.
 *
 * @param[in,out] tif Non-NULL live ZSTD handle with valid
 * predictor-prefixed state; serialise operations.
 * @return One after frame completion and buffer flushes, zero on
 * dependency/I/O failure.
 */
static int ZSTDPostEncode(TIFF *tif)
{
    static const char module[] = "ZSTDPostEncode";
    ZSTDState *sp = ZSTDEncoderState(tif);
    size_t zstd_ret;

    do
    {
        zstd_ret = ZSTD_endStream(sp->cstream, &sp->out_buffer);
        if (ZSTD_isError(zstd_ret))
        {
            TIFFErrorExtR(tif, module, "Error in ZSTD_endStream(): %s",
                          ZSTD_getErrorName(zstd_ret));
            return 0;
        }
        if (sp->out_buffer.pos > 0)
        {
            tif->tif_rawcc = (tmsize_t)sp->out_buffer.pos;
            if (!TIFFFlushData1(tif))
                return 0;
            sp->out_buffer.dst = tif->tif_rawcp;
            sp->out_buffer.pos = 0;
        }
    } while (zstd_ret != 0);
    return 1;
}

/**
 * @brief Release ZSTD streams state and chained codec methods.
 * @ingroup libtiff_private_zstd
 *
 * Restores predictor and saved tag hooks, frees dependency streams with
 * their matching ZSTD free functions, frees outer Ext state, clears
 * tif_data and installs default callbacks. Invalidates state/stream views.
 * Does not finish frames, flush payload, close the handle or return a
 * status.
 *
 * @param[in,out] tif Non-NULL live ZSTD handle with valid
 * predictor-prefixed state; serialise operations.
 */
static void ZSTDCleanup(TIFF *tif)
{
    ZSTDState *sp = GetZSTDState(tif);

    assert(sp != 0);

    (void)TIFFPredictorCleanup(tif);

    tif->tif_tagmethods.vgetfield = sp->vgetparent;
    tif->tif_tagmethods.vsetfield = sp->vsetparent;

    if (sp->dstream)
    {
        ZSTD_freeDStream(sp->dstream);
        sp->dstream = NULL;
    }
    if (sp->cstream)
    {
        ZSTD_freeCStream(sp->cstream);
        sp->cstream = NULL;
    }
    _TIFFfreeExt(tif, sp);
    tif->tif_data = NULL;

    _TIFFSetDefaultCompressionState(tif);
}

/**
 * @brief Store ZSTD compression level or delegate another tag assignment.
 * @ingroup libtiff_private_zstd
 *
 * TIFFTAG_ZSTD_LEVEL consumes int, stores it even outside
 * 1..ZSTD_maxCLevel(), warns for that range and returns success. Stream
 * reinitialisation applies it later. Other tags follow parent variadic
 * contracts; no ownership transfer for the level. Callbacks/diagnostics can
 * mutate state.
 *
 * @param[in,out] tif Non-NULL live ZSTD handle with valid
 * predictor-prefixed state; serialise operations.
 * @param[in] tag Pseudo-tag or delegated tag identifier.
 * @param[in,out] ap Live tag-specific va_list consumed synchronously; not
 * retained.
 * @return One for level assignment including warned values, otherwise
 * parent status.
 */
static int ZSTDVSetField(TIFF *tif, uint32_t tag, va_list ap)
{
    static const char module[] = "ZSTDVSetField";
    ZSTDState *sp = GetZSTDState(tif);

    switch (tag)
    {
        case TIFFTAG_ZSTD_LEVEL:
            sp->compression_level = (int)va_arg(ap, int);
            if (sp->compression_level <= 0 ||
                sp->compression_level > ZSTD_maxCLevel())
            {
                TIFFWarningExtR(tif, module,
                                "ZSTD_LEVEL should be between 1 and %d",
                                ZSTD_maxCLevel());
            }
            return 1;
        default:
            return (*sp->vsetparent)(tif, tag, ap);
    }
    /*NOTREACHED*/
}

/**
 * @brief Retrieve ZSTD compression level or delegate another tag query.
 * @ingroup libtiff_private_zstd
 *
 * TIFFTAG_ZSTD_LEVEL consumes a non-NULL int* and writes the cached level.
 * Other tags follow parent variadic contracts. No allocation or ownership
 * transfer for the level.
 *
 * @param[in,out] tif Non-NULL live ZSTD handle with valid
 * predictor-prefixed state; serialise operations.
 * @param[in] tag Pseudo-tag or delegated tag identifier.
 * @param[in,out] ap Live tag-specific va_list consumed synchronously; not
 * retained.
 * @return One for level retrieval, otherwise parent status.
 */
static int ZSTDVGetField(TIFF *tif, uint32_t tag, va_list ap)
{
    ZSTDState *sp = GetZSTDState(tif);

    switch (tag)
    {
        case TIFFTAG_ZSTD_LEVEL:
            *va_arg(ap, int *) = sp->compression_level;
            break;
        default:
            return (*sp->vgetparent)(tif, tag, ap);
    }
    return 1;
}

/**
 * @brief Define the ZSTD compression-level pseudo-tag.
 * @ingroup libtiff_private_zstd
 *
 * Immutable library-lifetime metadata merged into a handle; no caller
 * ownership transfer.
 */
static const TIFFField ZSTDFields[] = {
    {TIFFTAG_ZSTD_LEVEL, 0, 0, TIFF_ANY, 0, TIFF_SETGET_INT, FIELD_PSEUDO, TRUE,
     FALSE, "ZSTD compression_level", NULL},
};

/**
 * @brief Report the ZSTD expansion allocation heuristic.
 * @ingroup libtiff_private_zstd
 *
 * Returns a fixed estimate without handle inspection, I/O, diagnostics,
 * allocation or state changes; not a universal bound.
 *
 * @param[in] tif Unused handle.
 * @return Thirty-three thousand.
 */
static uint64_t ZSTDGetMaxCompressionRatio(TIFF *tif)
{
    (void)tif;
    /* Value from the issue #479 tile allocation guard that previously lived
     * in _TIFFReadEncodedTileAndAllocBuffer(). */
    return 33000;
}

int TIFFInitZSTD(TIFF *tif, int scheme)
{
    static const char module[] = "TIFFInitZSTD";
    ZSTDState *sp;

    (void)scheme;
    assert(scheme == COMPRESSION_ZSTD);

    /*
     * Merge codec-specific tag information.
     */
    if (!_TIFFMergeFields(tif, ZSTDFields, TIFFArrayCount(ZSTDFields)))
    {
        TIFFErrorExtR(tif, module, "Merging ZSTD codec-specific tags failed");
        return 0;
    }

    /*
     * Allocate state block so tag methods have storage to record values.
     */
    tif->tif_data = (uint8_t *)_TIFFmallocExt(tif, sizeof(ZSTDState));
    if (tif->tif_data == NULL)
        goto bad;
    sp = GetZSTDState(tif);

    /*
     * Override parent get/set field methods.
     */
    sp->vgetparent = tif->tif_tagmethods.vgetfield;
    tif->tif_tagmethods.vgetfield = ZSTDVGetField; /* hook for codec tags */
    sp->vsetparent = tif->tif_tagmethods.vsetfield;
    tif->tif_tagmethods.vsetfield = ZSTDVSetField; /* hook for codec tags */

    /* Default values for codec-specific fields */
    sp->compression_level = 9; /* default comp. level */
    sp->state = 0;
    sp->dstream = 0;
    sp->cstream = 0;
    sp->out_buffer.dst = NULL;
    sp->out_buffer.size = 0;
    sp->out_buffer.pos = 0;

    /*
     * Install codec methods.
     */
    tif->tif_fixuptags = ZSTDFixupTags;
    tif->tif_setupdecode = ZSTDSetupDecode;
    tif->tif_predecode = ZSTDPreDecode;
    tif->tif_decoderow = ZSTDDecode;
    tif->tif_decodestrip = ZSTDDecode;
    tif->tif_decodetile = ZSTDDecode;
    tif->tif_setupencode = ZSTDSetupEncode;
    tif->tif_preencode = ZSTDPreEncode;
    tif->tif_postencode = ZSTDPostEncode;
    tif->tif_encoderow = ZSTDEncode;
    tif->tif_encodestrip = ZSTDEncode;
    tif->tif_encodetile = ZSTDEncode;
    tif->tif_cleanup = ZSTDCleanup;
    tif->tif_getmaxcompressionratio = ZSTDGetMaxCompressionRatio;
    /*
     * Setup predictor setup.
     */
    (void)TIFFPredictorInit(tif);
    return 1;
bad:
    TIFFErrorExtR(tif, module, "No space for ZSTD state block");
    return 0;
}
#endif /* ZSTD_SUPPORT */

/**
 * @def LSTATE_INIT_DECODE
 * @brief Bit 0x01 of the codec state field: decoding has been set up.
 * @ingroup libtiff_private_zstd
 *
 * Set by ZSTDSetupDecode(). Each setup first tears down the other mode, if
 * active, and clears the field.
 */

/**
 * @def LSTATE_INIT_ENCODE
 * @brief Bit 0x02 of the codec state field: encoding has been set up.
 * @ingroup libtiff_private_zstd
 *
 * Set by ZSTDSetupEncode(). Each setup first tears down the other mode, if
 * active, and clears the field.
 */

/**
 * @def GetZSTDState(tif)
 * @brief Cast tif->tif_data to the ZSTDState.
 * @ingroup libtiff_private_zstd
 *
 * tif is evaluated once. Requires the codec to be initialised (tif_data
 * non-NULL).
 *
 * @param[in] tif Handle whose codec state is returned.
 */

/**
 * @def ZSTDDecoderState(tif)
 * @brief Alias of GetZSTDState(), returning the ZSTDState for decoding.
 * @ingroup libtiff_private_zstd
 *
 * tif is evaluated once. Requires the codec to be initialised.
 *
 * @param[in] tif Handle whose codec state is returned.
 */

/**
 * @def ZSTDEncoderState(tif)
 * @brief Alias of GetZSTDState(), returning the ZSTDState for encoding.
 * @ingroup libtiff_private_zstd
 *
 * tif is evaluated once. Requires the codec to be initialised.
 *
 * @param[in] tif Handle whose codec state is returned.
 */

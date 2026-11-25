/*
 * Copyright (c) 2018, Mapbox
 * Author: <norman.barker at mapbox.com>
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
 * @file tif_webp.c
 * @brief Implement WebP strile staging and RGB or RGBA frame transfer.
 */

#include "tiffiop.h"
#ifdef WEBP_SUPPORT
/*
 * TIFF Library.
 *
 * WEBP Compression Support
 *
 */

#include "webp/decode.h"
#include "webp/encode.h"

#include <stdbool.h>
#include <stdio.h>

#define LSTATE_INIT_DECODE 0x01
#define LSTATE_INIT_ENCODE 0x02
/*
 * State block for each open TIFF
 * file using WEBP compression/decompression.
 */
/**
 * @brief Own WebP frame staging dependency objects and saved tag methods.
 * @ingroup libtiff_private_webp
 *
 * Codec-owned state must remain live until cleanup. Internal buffers and
 * dependency objects have the individual ownership described below;
 * borrowed parent methods must remain callable.
 */
typedef struct
{
    /** @brief Cached RGB/RGBA channel count. */
    uint16_t nSamples;

    /** @brief Same-strile decode failure latch reset by predecode. */
    int read_error;
    /** @brief Cached lossless option applied at encoder setup. */
    int lossless;
    /**
     * @brief Cached preservation of RGB under zero alpha in supported lossless
     * ABI.
     */
    int lossless_exact;
    /**
     * @brief Cached compression quality, including warned out-of-range
     * assignments.
     */
    int quality_level;
    /**
     * @brief Embedded dependency picture owning imported samples until
     * WebPPictureFree.
     */
    WebPPicture sPicture;
    /** @brief Embedded dependency encoder configuration. */
    WebPConfig sEncoderConfig;
    /**
     * @brief Owned Ext full-strile input/output staging, released on
     * replacement/cleanup.
     */
    uint8_t *pBuffer;
    /** @brief Number of staged input bytes. */
    unsigned int buffer_offset;
    /**
     * @brief Staging capacity in bytes, bounded by validated frame dimensions.
     */
    unsigned int buffer_size;

    /** @brief Owned incremental dependency decoder, released by WebPIDelete. */
    WebPIDecoder *psDecoder;
    /**
     * @brief External-memory descriptor borrowing pBuffer or caller
     * whole-strile output.
     */
    WebPDecBuffer sDecBuffer;
    /** @brief Number of decoded rows already returned. */
    int last_y;

    /** @brief Decode/encode initialisation flags. */
    int state;

    /** @brief Borrowed parent getter restored on cleanup. */
    TIFFVGetMethod vgetparent;
    /** @brief Borrowed parent setter restored on cleanup. */
    TIFFVSetMethod vsetparent;
} WebPState;

#define LState(tif) ((WebPState *)(tif)->tif_data)
#define DecoderState(tif) LState(tif)
#define EncoderState(tif) LState(tif)

static int TWebPEncode(TIFF *tif, uint8_t *bp, tmsize_t cc, uint16_t s);
static int TWebPDecode(TIFF *tif, uint8_t *op, tmsize_t occ, uint16_t s);

/**
 * @brief Append one dependency output fragment to the raw TIFF buffer.
 * @ingroup libtiff_private_webp
 *
 * Requires valid picture context and representable byte/cursor arithmetic.
 * Checks remaining raw capacity, copies bytes and advances rawcp/rawcc;
 * diagnoses insufficient space without flushing. No retained input,
 * allocation or ownership transfer.
 *
 * @param[in] data Borrowed dependency output valid for the duration of the
 * writer callback.
 * @param[in] data_size Output byte extent, representable by tmsize_t with
 * checked-by-caller cursor sums.
 * @param[in] picture Borrowed picture whose custom_ptr contains the live
 * TIFF handle.
 * @return One after copy, zero for insufficient capacity.
 */
static int TWebPDatasetWriter(const uint8_t *data, size_t data_size,
                              const WebPPicture *const picture)
{
    static const char module[] = "TWebPDatasetWriter";
    TIFF *tif = (TIFF *)(picture->custom_ptr);

    if ((tif->tif_rawcc + (tmsize_t)data_size) > tif->tif_rawdatasize)
    {
        TIFFErrorExtR(tif, module,
                      "Buffer too small by %" TIFF_SIZE_FORMAT " bytes.",
                      (size_t)((uint64_t)tif->tif_rawcc + (uint64_t)data_size -
                               (uint64_t)tif->tif_rawdatasize));
        return 0;
    }
    else
    {
        _TIFFmemcpy(tif->tif_rawcp, data, (tmsize_t)data_size);
        tif->tif_rawcc += (tmsize_t)data_size;
        tif->tif_rawcp += data_size;
        return 1;
    }
}

/*
 * Encode a chunk of pixels.
 */
/**
 * @brief Accumulate decoded WebP input in the strile staging buffer.
 * @ingroup libtiff_private_webp
 *
 * Requires successful preencode and nonnegative count. Bounds offset+cc
 * against buffer_size using uint64_t, copies input and advances offset.
 * Does not compress until postencode. Input remains
 * caller-owned/unmodified; rejection diagnoses, no client I/O or ownership
 * transfer.
 *
 * @param[in,out] tif Non-NULL live WebP handle with coherent raw buffers
 * and established contiguous eight-bit RGB/RGBA geometry; serialise
 * operations.
 * @param[in] bp Borrowed readable cc-byte input, copied synchronously
 * without modification.
 * @param[in] cc Nonnegative input byte extent within the remaining strile
 * allocation.
 * @param[in] s Unused sample-plane argument.
 * @return One after staging, zero when input exceeds capacity.
 */
static int TWebPEncode(TIFF *tif, uint8_t *bp, tmsize_t cc, uint16_t s)
{
    static const char module[] = "TWebPEncode";
    WebPState *sp = EncoderState(tif);
    (void)s;

    assert(sp != NULL);
    assert(sp->state == LSTATE_INIT_ENCODE);

    if ((uint64_t)sp->buffer_offset + (uint64_t)cc > sp->buffer_size)
    {
        TIFFErrorExtR(tif, module, "Too many bytes to be written");
        return 0;
    }

    memcpy(sp->pBuffer + sp->buffer_offset, bp, (size_t)cc);
    sp->buffer_offset += (unsigned)cc;

    return 1;
}

/**
 * @brief Decode a WebP strile or retrieve its next decoded rows.
 * @ingroup libtiff_private_webp
 *
 * Requires successful predecode and validated positive geometry/stride. On
 * first use checks blob dimensions and, for available ABI, channel
 * compatibility (opaque RGB blob may fill RGBA). Uses caller output
 * directly for a full-strile request or codec-owned staging for partial
 * rows; lazily creates a dependency decoder. Requires complete row extents
 * and enough newly decoded rows. Success consumes all raw input, advances
 * last_y, and deletes a completed direct-output decoder. Errors normally
 * clear requested output and latch read_error; fractional-row usage errors
 * do not latch. Decoder/stream state can change before failure, and a
 * direct-output decoder can retain the borrowed output pointer until the
 * next predecode/cleanup. No ownership transfer or direct client I/O.
 *
 * @param[in,out] tif Non-NULL live WebP handle with coherent raw buffers
 * and established contiguous eight-bit RGB/RGBA geometry; serialise
 * operations.
 * @param[out] op Borrowed writable occ-byte output; keep it live until
 * predecode/cleanup if an external-buffer decode fails before decoder
 * release.
 * @param[in] occ Nonnegative whole-row output byte extent.
 * @param[in] s Unused sample-plane argument.
 * @return One after requested rows supplied, zero on detected
 * setup/content/extent/decode failure; partial output is discarded on
 * failure.
 */
static int TWebPDecode(TIFF *tif, uint8_t *op, tmsize_t occ, uint16_t s)
{
    static const char module[] = "WebPDecode";
    VP8StatusCode status = VP8_STATUS_OK;
    WebPState *sp = DecoderState(tif);
    uint32_t segment_width, segment_height;
    bool decode_whole_strile = false;

    (void)s;

    assert(sp != NULL);
    assert(sp->state == LSTATE_INIT_DECODE);

    if (sp->read_error)
    {
        memset(op, 0, (size_t)occ);
        TIFFErrorExtR(tif, module,
                      "ZIPDecode: Scanline %" PRIu32 " cannot be read due to "
                      "previous error",
                      tif->tif_dir.td_row);
        return 0;
    }

    if (sp->psDecoder == NULL)
    {
        TIFFDirectory *td = &tif->tif_dir;
        uint32_t buffer_size;

        if (isTiled(tif))
        {
            segment_width = td->td_tilewidth;
            segment_height = td->td_tilelength;
        }
        else
        {
            segment_width = td->td_imagewidth;
            segment_height = td->td_imagelength - tif->tif_dir.td_row;
            if (segment_height > td->td_rowsperstrip)
                segment_height = td->td_rowsperstrip;
        }

        int webp_width, webp_height;
        if (!WebPGetInfo(tif->tif_rawcp,
                         (uint64_t)tif->tif_rawcc > UINT32_MAX
                             ? UINT32_MAX
                             : (uint32_t)tif->tif_rawcc,
                         &webp_width, &webp_height))
        {
            memset(op, 0, (size_t)occ);
            sp->read_error = 1;
            TIFFErrorExtR(tif, module, "WebPGetInfo() failed");
            return 0;
        }
        if ((uint32_t)webp_width != segment_width ||
            (uint32_t)webp_height != segment_height)
        {
            memset(op, 0, (size_t)occ);
            sp->read_error = 1;
            TIFFErrorExtR(
                tif, module, "WebP blob dimension is %dx%d. Expected %ux%u",
                webp_width, webp_height, segment_width, segment_height);
            return 0;
        }

#if WEBP_DECODER_ABI_VERSION >= 0x0002
        WebPDecoderConfig config;
        if (!WebPInitDecoderConfig(&config))
        {
            memset(op, 0, (size_t)occ);
            sp->read_error = 1;
            TIFFErrorExtR(tif, module, "WebPInitDecoderConfig() failed");
            return 0;
        }

        const bool bWebPGetFeaturesOK =
            WebPGetFeatures(tif->tif_rawcp,
                            (uint64_t)tif->tif_rawcc > UINT32_MAX
                                ? UINT32_MAX
                                : (uint32_t)tif->tif_rawcc,
                            &config.input) == VP8_STATUS_OK;

        WebPFreeDecBuffer(&config.output);

        if (!bWebPGetFeaturesOK)
        {
            memset(op, 0, (size_t)occ);
            sp->read_error = 1;
            TIFFErrorExtR(tif, module, "WebPInitDecoderConfig() failed");
            return 0;
        }

        const int webp_bands = config.input.has_alpha ? 4 : 3;
        if (webp_bands != sp->nSamples &&
            /* We accept the situation where the WebP blob has only 3 bands,
             * whereas the raster is 4 bands. This can happen when the alpha
             * channel is fully opaque, and WebP decoding works fine in that
             * situation.
             */
            !(webp_bands == 3 && sp->nSamples == 4))
        {
            memset(op, 0, (size_t)occ);
            sp->read_error = 1;
            TIFFErrorExtR(tif, module,
                          "WebP blob band count is %d. Expected %d", webp_bands,
                          sp->nSamples);
            return 0;
        }
#endif

        buffer_size = segment_width * segment_height * sp->nSamples;
        if (occ == (tmsize_t)buffer_size)
        {
            /* If decoding the whole strip/tile, we can directly use the */
            /* output buffer */
            decode_whole_strile = true;
        }
        else if (sp->pBuffer == NULL || buffer_size > sp->buffer_size)
        {
            if (sp->pBuffer != NULL)
            {
                _TIFFfreeExt(tif, sp->pBuffer);
                sp->pBuffer = NULL;
            }

            sp->pBuffer = (uint8_t *)_TIFFmallocExt(tif, buffer_size);
            if (!sp->pBuffer)
            {
                TIFFErrorExtR(tif, module, "Cannot allocate buffer");
                memset(op, 0, (size_t)occ);
                sp->read_error = 1;
                return 0;
            }
            sp->buffer_size = buffer_size;
        }

        sp->last_y = 0;

        WebPInitDecBuffer(&sp->sDecBuffer);

        sp->sDecBuffer.is_external_memory = 1;
        sp->sDecBuffer.width = (int)segment_width;
        sp->sDecBuffer.height = (int)segment_height;
        sp->sDecBuffer.u.RGBA.rgba = decode_whole_strile ? op : sp->pBuffer;
        sp->sDecBuffer.u.RGBA.stride = (int)(segment_width * sp->nSamples);
        sp->sDecBuffer.u.RGBA.size = buffer_size;

        if (sp->nSamples > 3)
        {
            sp->sDecBuffer.colorspace = MODE_RGBA;
        }
        else
        {
            sp->sDecBuffer.colorspace = MODE_RGB;
        }

        sp->psDecoder = WebPINewDecoder(&sp->sDecBuffer);

        if (sp->psDecoder == NULL)
        {
            memset(op, 0, (size_t)occ);
            sp->read_error = 1;
            TIFFErrorExtR(tif, module, "Unable to allocate WebP decoder.");
            return 0;
        }
    }

    if (occ % sp->sDecBuffer.u.RGBA.stride)
    {
        // read_error not set here as this is a usage issue that can be
        // recovered in a following call.
        memset(op, 0, (size_t)occ);
        /* Do not set read_error as could potentially be recovered */
        TIFFErrorExtR(tif, module, "Fractional scanlines cannot be read");
        return 0;
    }

    status = WebPIAppend(sp->psDecoder, tif->tif_rawcp, (size_t)tif->tif_rawcc);

    if (status != VP8_STATUS_OK && status != VP8_STATUS_SUSPENDED)
    {
        if (status == VP8_STATUS_INVALID_PARAM)
        {
            TIFFErrorExtR(tif, module, "Invalid parameter used.");
        }
        else if (status == VP8_STATUS_OUT_OF_MEMORY)
        {
            TIFFErrorExtR(tif, module, "Out of memory.");
        }
        else
        {
            TIFFErrorExtR(tif, module, "Unrecognized error.");
        }
        memset(op, 0, (size_t)occ);
        sp->read_error = 1;
        return 0;
    }
    else
    {
        int current_y, stride;
        uint8_t *buf;

        /* Returns the RGB/A image decoded so far */
        buf = WebPIDecGetRGB(sp->psDecoder, &current_y, NULL, NULL, &stride);

        if ((buf != NULL) &&
            (occ <= (tmsize_t)stride * (current_y - sp->last_y)))
        {
            const int numberOfExpectedLines =
                (int)(occ / sp->sDecBuffer.u.RGBA.stride);
            if (decode_whole_strile)
            {
                if (current_y != numberOfExpectedLines)
                {
                    memset(op, 0, (size_t)occ);
                    sp->read_error = 1;
                    TIFFErrorExtR(tif, module,
                                  "Unable to decode WebP data: less lines than "
                                  "expected.");
                    return 0;
                }
            }
            else
            {
                memcpy(op, buf + (sp->last_y * stride), (size_t)occ);
            }

            tif->tif_rawcp += tif->tif_rawcc;
            tif->tif_rawcc = 0;
            sp->last_y += numberOfExpectedLines;

            if (decode_whole_strile)
            {
                /* We can now free the decoder as we're completely done */
                if (sp->psDecoder != NULL)
                {
                    WebPIDelete(sp->psDecoder);
                    WebPFreeDecBuffer(&sp->sDecBuffer);
                    sp->psDecoder = NULL;
                }
            }
            return 1;
        }
        else
        {
            memset(op, 0, (size_t)occ);
            sp->read_error = 1;
            TIFFErrorExtR(tif, module, "Unable to decode WebP data.");
            return 0;
        }
    }
}

/**
 * @brief Reject noncontiguous WebP sample organisation.
 * @ingroup libtiff_private_webp
 *
 * Checks PLANARCONFIG_CONTIG and diagnoses rejection. No allocation,
 * payload I/O, state mutation or ownership transfer.
 *
 * @param[in] tif Non-NULL live WebP handle whose PlanarConfiguration is
 * checked.
 * @return One for contiguous layout, zero otherwise.
 */
static int TWebPFixupTags(TIFF *tif)
{
    (void)tif;
    if (tif->tif_dir.td_planarconfig != PLANARCONFIG_CONTIG)
    {
        static const char module[] = "TWebPFixupTags";
        TIFFErrorExtR(tif, module,
                      "TIFF WEBP requires data to be stored contiguously in "
                      "RGB e.g. RGBRGBRGB "
#if WEBP_ENCODER_ABI_VERSION >= 0x0100
                      "or RGBARGBARGBA"
#endif
        );
        return 0;
    }
    return 1;
}

/**
 * @brief Validate WebP channel support and enter decode mode.
 * @ingroup libtiff_private_webp
 *
 * Caches sample count and accepts three channels or four when encoder ABI
 * supports them. Current sample-format check rejects only when both bit
 * depth differs from eight and format differs from unsigned, so caller must
 * establish valid eight-bit unsigned data separately. Releases an old
 * encoder picture/staging buffer when switching modes. State/views can
 * change before later failure; no client I/O or caller ownership transfer.
 *
 * @param[in,out] tif Non-NULL live WebP handle with coherent raw buffers
 * and established contiguous eight-bit RGB/RGBA geometry; serialise
 * operations.
 * @return One after mode setup, zero on detected channel/format rejection.
 */
static int TWebPSetupDecode(TIFF *tif)
{
    static const char module[] = "WebPSetupDecode";
    uint16_t nBitsPerSample = tif->tif_dir.td_bitspersample;
    uint16_t sampleFormat = tif->tif_dir.td_sampleformat;

    WebPState *sp = DecoderState(tif);
    assert(sp != NULL);

    sp->nSamples = tif->tif_dir.td_samplesperpixel;

    /* check band count */
    if (sp->nSamples != 3
#if WEBP_ENCODER_ABI_VERSION >= 0x0100
        && sp->nSamples != 4
#endif
    )
    {
        TIFFErrorExtR(tif, module,
                      "WEBP driver doesn't support %d bands. Must be 3 (RGB) "
#if WEBP_ENCODER_ABI_VERSION >= 0x0100
                      "or 4 (RGBA) "
#endif
                      "bands.",
                      sp->nSamples);
        return 0;
    }

    /* check bits per sample and data type */
    if ((nBitsPerSample != 8) && (sampleFormat != 1))
    {
        TIFFErrorExtR(tif, module, "WEBP driver requires 8 bit unsigned data");
        return 0;
    }

    /* if we were last encoding, terminate this mode */
    if (sp->state & LSTATE_INIT_ENCODE)
    {
        WebPPictureFree(&sp->sPicture);
        if (sp->pBuffer != NULL)
        {
            _TIFFfreeExt(tif, sp->pBuffer);
            sp->pBuffer = NULL;
        }
        sp->buffer_offset = 0;
        sp->state = 0;
    }

    sp->state |= LSTATE_INIT_DECODE;

    return 1;
}

/*
 * Setup state for decoding a strip.
 */
/**
 * @brief Reset WebP per-strile decode state and error latch.
 * @ingroup libtiff_private_webp
 *
 * Computes tile or truncated-strip dimensions and rejects either above
 * 16383. Attempts setup without directly checking its status, deletes any
 * previous dependency decoder/buffer descriptor and clears read_error.
 * Requires positive valid geometry and successful setup from the caller.
 * Invalidates borrowed decoder views; retains codec staging allocation for
 * reuse. No pixel I/O.
 *
 * @param[in,out] tif Non-NULL live WebP handle with coherent raw buffers
 * and established contiguous eight-bit RGB/RGBA geometry; serialise
 * operations.
 * @param[in] s Unused sample-plane argument.
 * @return One after reset, zero on dimension rejection.
 */
static int TWebPPreDecode(TIFF *tif, uint16_t s)
{
    static const char module[] = "TWebPPreDecode";
    uint32_t segment_width, segment_height;
    WebPState *sp = DecoderState(tif);
    TIFFDirectory *td = &tif->tif_dir;
    (void)s;
    assert(sp != NULL);

    if (isTiled(tif))
    {
        segment_width = td->td_tilewidth;
        segment_height = td->td_tilelength;
    }
    else
    {
        segment_width = td->td_imagewidth;
        segment_height = td->td_imagelength - tif->tif_dir.td_row;
        if (segment_height > td->td_rowsperstrip)
            segment_height = td->td_rowsperstrip;
    }

    if (segment_width > 16383 || segment_height > 16383)
    {
        TIFFErrorExtR(tif, module,
                      "WEBP maximum image dimensions are 16383 x 16383.");
        return 0;
    }

    if ((sp->state & LSTATE_INIT_DECODE) == 0)
        tif->tif_setupdecode(tif);

    if (sp->psDecoder != NULL)
    {
        WebPIDelete(sp->psDecoder);
        WebPFreeDecBuffer(&sp->sDecBuffer);
        sp->psDecoder = NULL;
    }

    sp->read_error = 0;

    return 1;
}

/**
 * @brief Validate WebP input and initialise picture and encoder configuration.
 * @ingroup libtiff_private_webp
 *
 * Requires contiguous unsigned eight-bit RGB or ABI-supported RGBA. Ends
 * decoder mode, initialises dependency picture/config at cached quality,
 * applies supported lossless/exact settings and validates config. Errors
 * diagnose after some mode/config state can change. Dependency picture
 * storage is codec-owned until cleanup; no pixel I/O or caller ownership
 * transfer.
 *
 * @param[in,out] tif Non-NULL live WebP handle with coherent raw buffers
 * and established contiguous eight-bit RGB/RGBA geometry; serialise
 * operations.
 * @return One after configuration, zero on unsupported
 * geometry/ABI/dependency setup failure.
 */
static int TWebPSetupEncode(TIFF *tif)
{
    static const char module[] = "WebPSetupEncode";
    uint16_t nBitsPerSample = tif->tif_dir.td_bitspersample;
    uint16_t sampleFormat = tif->tif_dir.td_sampleformat;

    WebPState *sp = EncoderState(tif);
    assert(sp != NULL);

    sp->nSamples = tif->tif_dir.td_samplesperpixel;

    /* check band count */
    if (sp->nSamples != 3
#if WEBP_ENCODER_ABI_VERSION >= 0x0100
        && sp->nSamples != 4
#endif
    )
    {
        TIFFErrorExtR(tif, module,
                      "WEBP driver doesn't support %d bands. Must be 3 (RGB) "
#if WEBP_ENCODER_ABI_VERSION >= 0x0100
                      "or 4 (RGBA) "
#endif
                      "bands.",
                      sp->nSamples);
        return 0;
    }

    /* check bits per sample and data type */
    if ((nBitsPerSample != 8) || (sampleFormat != SAMPLEFORMAT_UINT))
    {
        TIFFErrorExtR(tif, module, "WEBP driver requires 8 bit unsigned data");
        return 0;
    }

    if (sp->state & LSTATE_INIT_DECODE)
    {
        WebPIDelete(sp->psDecoder);
        WebPFreeDecBuffer(&sp->sDecBuffer);
        sp->psDecoder = NULL;
        sp->last_y = 0;
        sp->state = 0;
    }

    sp->state |= LSTATE_INIT_ENCODE;

    if (!WebPPictureInit(&sp->sPicture))
    {
        TIFFErrorExtR(tif, module, "Error initializing WebP picture.");
        return 0;
    }

    if (!WebPConfigInitInternal(&sp->sEncoderConfig, WEBP_PRESET_DEFAULT,
                                (float)sp->quality_level,
                                WEBP_ENCODER_ABI_VERSION))
    {
        TIFFErrorExtR(tif, module,
                      "Error creating WebP encoder configuration.");
        return 0;
    }

// WebPConfigInitInternal above sets lossless to false
#if WEBP_ENCODER_ABI_VERSION >= 0x0100
    sp->sEncoderConfig.lossless = sp->lossless;
    if (sp->lossless)
    {
        sp->sPicture.use_argb = 1;
#if WEBP_ENCODER_ABI_VERSION >= 0x0209
        sp->sEncoderConfig.exact = sp->lossless_exact;
#endif
    }
#endif

    if (!WebPValidateConfig(&sp->sEncoderConfig))
    {
        TIFFErrorExtR(tif, module, "Error with WebP encoder configuration.");
        return 0;
    }

    return 1;
}

/*
 * Reset encoding state at the start of a strip.
 */
/**
 * @brief Allocate full-strile WebP input staging and attach the output writer.
 * @ingroup libtiff_private_webp
 *
 * Attempts setup without checking status directly, computes
 * tile/truncated-strip extent, rejects dimensions above 16383 and replaces
 * the old staging allocation. Requires valid positive geometry and a
 * successful encoder configuration. Size is bounded below one GiB for at
 * most four samples. Sets picture width/height, writer and borrowed handle
 * context. Failure can discard the old staging buffer; no payload I/O or
 * ownership transfer.
 *
 * @param[in,out] tif Non-NULL live WebP handle with coherent raw buffers
 * and established contiguous eight-bit RGB/RGBA geometry; serialise
 * operations.
 * @param[in] s Unused sample-plane argument.
 * @return One after allocation/attachment, zero on dimension/allocation
 * failure.
 */
static int TWebPPreEncode(TIFF *tif, uint16_t s)
{
    static const char module[] = "TWebPPreEncode";
    uint32_t segment_width, segment_height;
    WebPState *sp = EncoderState(tif);
    TIFFDirectory *td = &tif->tif_dir;

    (void)s;

    assert(sp != NULL);
    if (sp->state != LSTATE_INIT_ENCODE)
        tif->tif_setupencode(tif);

    /*
     * Set encoding parameters for this strip/tile.
     */
    if (isTiled(tif))
    {
        segment_width = td->td_tilewidth;
        segment_height = td->td_tilelength;
    }
    else
    {
        segment_width = td->td_imagewidth;
        segment_height = td->td_imagelength - tif->tif_dir.td_row;
        if (segment_height > td->td_rowsperstrip)
            segment_height = td->td_rowsperstrip;
    }

    if (segment_width > 16383 || segment_height > 16383)
    {
        TIFFErrorExtR(tif, module,
                      "WEBP maximum image dimensions are 16383 x 16383.");
        return 0;
    }

    /* set up buffer for raw data */
    /* given above check and that nSamples <= 4, buffer_size is <= 1 GB */
    sp->buffer_size = segment_width * segment_height * sp->nSamples;

    if (sp->pBuffer != NULL)
    {
        _TIFFfreeExt(tif, sp->pBuffer);
        sp->pBuffer = NULL;
    }

    sp->pBuffer = (uint8_t *)_TIFFmallocExt(tif, sp->buffer_size);
    if (!sp->pBuffer)
    {
        TIFFErrorExtR(tif, module, "Cannot allocate buffer");
        return 0;
    }
    sp->buffer_offset = 0;

    sp->sPicture.width = (int)segment_width;
    sp->sPicture.height = (int)segment_height;
    sp->sPicture.writer = TWebPDatasetWriter;
    sp->sPicture.custom_ptr = tif;

    return 1;
}

/*
 * Finish off an encoded strip by flushing it.
 */
/**
 * @brief Import staged RGB or RGBA encode it and flush the WebP frame.
 * @ingroup libtiff_private_webp
 *
 * Requires staging fully filled before this call; buffer_offset is not
 * checked against full frame size here. Imports dependency picture data,
 * encodes through the bounded raw-buffer writer then flushes.
 * Picture/custom context and raw/file state can change on failure.
 * Dependency ABI selects RGBA/error-detail support. No IFD persistence,
 * durability guarantee or input ownership transfer.
 *
 * @param[in,out] tif Non-NULL live WebP handle with coherent raw buffers
 * and established contiguous eight-bit RGB/RGBA geometry; serialise
 * operations.
 * @return One after frame encoding and flush, zero on import/encoding/flush
 * failure.
 */
static int TWebPPostEncode(TIFF *tif)
{
    static const char module[] = "WebPPostEncode";
    int64_t stride;
    WebPState *sp = EncoderState(tif);
    assert(sp != NULL);

    assert(sp->state == LSTATE_INIT_ENCODE);

    stride = (int64_t)sp->sPicture.width * sp->nSamples;

#if WEBP_ENCODER_ABI_VERSION >= 0x0100
    if (sp->nSamples == 4)
    {
        if (!WebPPictureImportRGBA(&sp->sPicture, sp->pBuffer, (int)stride))
        {
            TIFFErrorExtR(tif, module, "WebPPictureImportRGBA() failed");
            return 0;
        }
    }
    else
#endif
        if (!WebPPictureImportRGB(&sp->sPicture, sp->pBuffer, (int)stride))
    {
        TIFFErrorExtR(tif, module, "WebPPictureImportRGB() failed");
        return 0;
    }

    if (!WebPEncode(&sp->sEncoderConfig, &sp->sPicture))
    {

#if WEBP_ENCODER_ABI_VERSION >= 0x0100
        const char *pszErrorMsg = NULL;
        switch (sp->sPicture.error_code)
        {
            case VP8_ENC_ERROR_OUT_OF_MEMORY:
                pszErrorMsg = "Out of memory";
                break;
            case VP8_ENC_ERROR_BITSTREAM_OUT_OF_MEMORY:
                pszErrorMsg = "Out of memory while flushing bits";
                break;
            case VP8_ENC_ERROR_NULL_PARAMETER:
                pszErrorMsg = "A pointer parameter is NULL";
                break;
            case VP8_ENC_ERROR_INVALID_CONFIGURATION:
                pszErrorMsg = "Configuration is invalid";
                break;
            case VP8_ENC_ERROR_BAD_DIMENSION:
                pszErrorMsg = "Picture has invalid width/height";
                break;
            case VP8_ENC_ERROR_PARTITION0_OVERFLOW:
                pszErrorMsg = "Partition is bigger than 512k. Try using less "
                              "SEGMENTS, or increase PARTITION_LIMIT value";
                break;
            case VP8_ENC_ERROR_PARTITION_OVERFLOW:
                pszErrorMsg = "Partition is bigger than 16M";
                break;
            case VP8_ENC_ERROR_BAD_WRITE:
                pszErrorMsg = "Error while fludshing bytes";
                break;
            case VP8_ENC_ERROR_FILE_TOO_BIG:
                pszErrorMsg = "File is bigger than 4G";
                break;
            case VP8_ENC_ERROR_USER_ABORT:
                pszErrorMsg = "User interrupted";
                break;
            case VP8_ENC_OK:
            case VP8_ENC_ERROR_LAST:
            default:
                TIFFErrorExtR(tif, module,
                              "WebPEncode returned an unknown error code: %u",
                              sp->sPicture.error_code);
                pszErrorMsg = "Unknown WebP error type.";
                break;
        }
        TIFFErrorExtR(tif, module, "WebPEncode() failed : %s", pszErrorMsg);
#else
        TIFFErrorExtR(tif, module, "Error in WebPEncode()");
#endif
        return 0;
    }

    sp->sPicture.custom_ptr = NULL;

    if (!TIFFFlushData1(tif))
    {
        TIFFErrorExtR(tif, module, "Error flushing TIFF WebP encoder.");
        return 0;
    }

    return 1;
}

/**
 * @brief Release WebP picture decoder staging and outer codec state.
 * @ingroup libtiff_private_webp
 *
 * Restores saved tag hooks, releases dependency picture/decoder/buffer
 * descriptor and Ext staging/state allocations, clears tif_data and resets
 * methods. Invalidates state, dependency and retained borrowed-output
 * views. Does not finish or flush a pending frame or close the borrowed
 * handle; no status result.
 *
 * @param[in,out] tif Non-NULL live WebP handle with coherent raw buffers
 * and established contiguous eight-bit RGB/RGBA geometry; serialise
 * operations.
 */
static void TWebPCleanup(TIFF *tif)
{
    WebPState *sp = LState(tif);

    assert(sp != 0);

    tif->tif_tagmethods.vgetfield = sp->vgetparent;
    tif->tif_tagmethods.vsetfield = sp->vsetparent;

    if (sp->state & LSTATE_INIT_ENCODE)
    {
        WebPPictureFree(&sp->sPicture);
    }

    if (sp->psDecoder != NULL)
    {
        WebPIDelete(sp->psDecoder);
        WebPFreeDecBuffer(&sp->sDecBuffer);
        sp->psDecoder = NULL;
        sp->last_y = 0;
    }

    if (sp->pBuffer != NULL)
    {
        _TIFFfreeExt(tif, sp->pBuffer);
        sp->pBuffer = NULL;
    }

    _TIFFfreeExt(tif, tif->tif_data);
    tif->tif_data = NULL;

    _TIFFSetDefaultCompressionState(tif);
}

/**
 * @brief Store WebP quality lossless settings or delegate another tag.
 * @ingroup libtiff_private_webp
 *
 * WEBP_LEVEL consumes int and stores it even outside 1..100, warning
 * without rejection. WEBP_LOSSLESS consumes int when supported and sets
 * quality to 100 when enabled. WEBP_LOSSLESS_EXACT consumes int when
 * supported. Older ABI rejection diagnoses and returns zero. Values apply
 * at encoder setup; current active config is not generally rebuilt. Parent
 * tags follow their variadic contracts; no ownership transfer.
 *
 * @param[in,out] tif Non-NULL live WebP handle with coherent raw buffers
 * and established contiguous eight-bit RGB/RGBA geometry; serialise
 * operations.
 * @param[in] tag Codec pseudo-tag or delegated tag identifier.
 * @param[in,out] ap Live tag-specific va_list consumed synchronously; not
 * retained.
 * @return One for accepted settings including warned levels, zero for
 * unsupported ABI controls, otherwise parent status.
 */
static int TWebPVSetField(TIFF *tif, uint32_t tag, va_list ap)
{
    static const char module[] = "WebPVSetField";
    WebPState *sp = LState(tif);

    switch (tag)
    {
        case TIFFTAG_WEBP_LEVEL:
            sp->quality_level = (int)va_arg(ap, int);
            if (sp->quality_level <= 0 || sp->quality_level > 100)
            {
                TIFFWarningExtR(tif, module,
                                "WEBP_LEVEL should be between 1 and 100");
            }
            return 1;
        case TIFFTAG_WEBP_LOSSLESS:
#if WEBP_ENCODER_ABI_VERSION >= 0x0100
            sp->lossless = va_arg(ap, int);
            if (sp->lossless)
            {
                sp->quality_level = 100;
            }
            return 1;
#else
            TIFFErrorExtR(
                tif, module,
                "Need to upgrade WEBP driver, this version doesn't support "
                "lossless compression.");
            return 0;
#endif
        case TIFFTAG_WEBP_LOSSLESS_EXACT:
#if WEBP_ENCODER_ABI_VERSION >= 0x0209
            sp->lossless_exact = va_arg(ap, int);
            return 1;
#else
            TIFFErrorExtR(
                tif, module,
                "Need to upgrade WEBP driver, this version doesn't support "
                "lossless compression.");
            return 0;
#endif
        default:
            return (*sp->vsetparent)(tif, tag, ap);
    }
    /*NOTREACHED*/
}

/**
 * @brief Retrieve WebP quality or lossless settings or delegate a tag.
 * @ingroup libtiff_private_webp
 *
 * Codec pseudo-tags consume non-NULL int* outputs for cached level,
 * lossless and exact values; other tags follow parent getter contracts. No
 * allocation or ownership transfer for codec settings.
 *
 * @param[in,out] tif Non-NULL live WebP handle with coherent raw buffers
 * and established contiguous eight-bit RGB/RGBA geometry; serialise
 * operations.
 * @param[in] tag Codec pseudo-tag or delegated tag identifier.
 * @param[in,out] ap Live tag-specific va_list consumed synchronously; not
 * retained.
 * @return One for codec retrieval, otherwise parent getter status.
 */
static int TWebPVGetField(TIFF *tif, uint32_t tag, va_list ap)
{
    WebPState *sp = LState(tif);

    switch (tag)
    {
        case TIFFTAG_WEBP_LEVEL:
            *va_arg(ap, int *) = sp->quality_level;
            break;
        case TIFFTAG_WEBP_LOSSLESS:
            *va_arg(ap, int *) = sp->lossless;
            break;
        case TIFFTAG_WEBP_LOSSLESS_EXACT:
            *va_arg(ap, int *) = sp->lossless_exact;
            break;
        default:
            return (*sp->vgetparent)(tif, tag, ap);
    }
    return 1;
}

/**
 * @brief Define WebP quality lossless and exact pseudo-tags.
 * @ingroup libtiff_private_webp
 *
 * Immutable library-lifetime metadata merged into a handle; no caller
 * ownership transfer.
 */
static const TIFFField TWebPFields[] = {
    {TIFFTAG_WEBP_LEVEL, 0, 0, TIFF_ANY, 0, TIFF_SETGET_INT, FIELD_PSEUDO, TRUE,
     FALSE, "WEBP quality", NULL},
    {TIFFTAG_WEBP_LOSSLESS, 0, 0, TIFF_ANY, 0, TIFF_SETGET_INT, FIELD_PSEUDO,
     TRUE, FALSE, "WEBP lossless/lossy", NULL},
    {TIFFTAG_WEBP_LOSSLESS_EXACT, 0, 0, TIFF_ANY, 0, TIFF_SETGET_INT,
     FIELD_PSEUDO, TRUE, FALSE, "WEBP exact lossless", NULL},
};

/**
 * @brief Report the channel-dependent WebP expansion heuristic.
 * @ingroup libtiff_private_webp
 *
 * Reads sample count without allocation, diagnostics, I/O or
 * state/ownership effects. Uses lossless-derived estimates regardless of
 * selected mode; not a universal bound.
 *
 * @param[in] tif Non-NULL live WebP handle; only SamplesPerPixel is read.
 * @return 104194 for four samples, otherwise 78146.
 */
static uint64_t TWebPGetMaxCompressionRatio(TIFF *tif)
{
    /* See README_for_libtiff_developpers.md for raw data used to estimate
     * the maximum compression rate. */

    /* lossy compression: */
    /* return (tif->tif_dir.td_samplesperpixel == 4) ? 2199 : 1685; */
    /* lossless compression: */
    return (tif->tif_dir.td_samplesperpixel == 4) ? 104194 : 78146;
}

int TIFFInitWebP(TIFF *tif, int scheme)
{
    static const char module[] = "TIFFInitWebP";
    WebPState *sp;

    (void)scheme;
    assert(scheme == COMPRESSION_WEBP);

    /*
     * Merge codec-specific tag information.
     */
    if (!_TIFFMergeFields(tif, TWebPFields, TIFFArrayCount(TWebPFields)))
    {
        TIFFErrorExtR(tif, module, "Merging WebP codec-specific tags failed");
        return 0;
    }

    /*
     * Allocate state block so tag methods have storage to record values.
     */
    tif->tif_data = (uint8_t *)_TIFFmallocExt(tif, sizeof(WebPState));
    if (tif->tif_data == NULL)
        goto bad;
    sp = LState(tif);

    /*
     * Override parent get/set field methods.
     */
    sp->vgetparent = tif->tif_tagmethods.vgetfield;
    tif->tif_tagmethods.vgetfield = TWebPVGetField; /* hook for codec tags */
    sp->vsetparent = tif->tif_tagmethods.vsetfield;
    tif->tif_tagmethods.vsetfield = TWebPVSetField; /* hook for codec tags */

    /* Default values for codec-specific fields */
    sp->quality_level = 75; /* default comp. level */
    sp->lossless = 0;       /* default to false */
    sp->lossless_exact = 1; /* exact lossless mode (if lossless enabled) */
    sp->state = 0;
    sp->nSamples = 0;
    sp->psDecoder = NULL;
    sp->last_y = 0;

    sp->buffer_offset = 0;
    sp->pBuffer = NULL;

    /*
     * Install codec methods.
     * Notes:
     * encoderow is not supported
     */
    tif->tif_fixuptags = TWebPFixupTags;
    tif->tif_setupdecode = TWebPSetupDecode;
    tif->tif_predecode = TWebPPreDecode;
    tif->tif_decoderow = TWebPDecode;
    tif->tif_decodestrip = TWebPDecode;
    tif->tif_decodetile = TWebPDecode;
    tif->tif_setupencode = TWebPSetupEncode;
    tif->tif_preencode = TWebPPreEncode;
    tif->tif_postencode = TWebPPostEncode;
    tif->tif_encoderow = TWebPEncode;
    tif->tif_encodestrip = TWebPEncode;
    tif->tif_encodetile = TWebPEncode;
    tif->tif_cleanup = TWebPCleanup;
    tif->tif_getmaxcompressionratio = TWebPGetMaxCompressionRatio;

    return 1;
bad:
    TIFFErrorExtR(tif, module, "No space for WebP state block");
    return 0;
}

#endif /* WEBP_SUPPORT */

/**
 * @def LSTATE_INIT_DECODE
 * @brief Bit 0x01 of the codec state field: decoding has been set up.
 * @ingroup libtiff_private_webp
 *
 * Set by TWebPSetupDecode(). Each setup first tears down the other mode, if
 * active, and clears the field.
 */

/**
 * @def LSTATE_INIT_ENCODE
 * @brief Bit 0x02 of the codec state field: encoding has been set up.
 * @ingroup libtiff_private_webp
 *
 * Set by TWebPSetupEncode(). Each setup first tears down the other mode, if
 * active, and clears the field.
 */

/**
 * @def LState(tif)
 * @brief Cast tif->tif_data to the WebPState.
 * @ingroup libtiff_private_webp
 *
 * tif is evaluated once. Requires the codec to be initialised (tif_data
 * non-NULL).
 *
 * @param[in] tif Handle whose codec state is returned.
 */

/**
 * @def DecoderState(tif)
 * @brief Alias of LState(), returning the WebPState for decoding.
 * @ingroup libtiff_private_webp
 *
 * tif is evaluated once. Requires the codec to be initialised.
 *
 * @param[in] tif Handle whose codec state is returned.
 */

/**
 * @def EncoderState(tif)
 * @brief Alias of LState(), returning the WebPState for encoding.
 * @ingroup libtiff_private_webp
 *
 * tif is evaluated once. Requires the codec to be initialised.
 *
 * @param[in] tif Handle whose codec state is returned.
 */

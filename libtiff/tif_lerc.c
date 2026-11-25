/*
 * Copyright (c) 2018, Even Rouault
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
 * @file tif_lerc.c
 * @brief Stage LERC scalar planes masks and optional secondary compression.
 */

#include "tiffiop.h"
#ifdef LERC_SUPPORT
/*
 * TIFF Library.
 *
 * LERC Compression Support
 *
 */

#include "Lerc_c_api.h"
#include "zlib.h"
#include <math.h>
#ifdef ZSTD_SUPPORT
#include "zstd.h"
#endif

#if LIBDEFLATE_SUPPORT
#include "libdeflate.h"
#endif
#define LIBDEFLATE_MAX_COMPRESSION_LEVEL 12

#include <assert.h>

#define LSTATE_INIT_DECODE 0x01
#define LSTATE_INIT_ENCODE 0x02

#ifndef LERC_AT_LEAST_VERSION
#define LERC_AT_LEAST_VERSION(maj, min, patch) 0
#endif

/*
 * State block for each open TIFF file using LERC compression/decompression.
 */
/**
 * @brief Own LERC full-strile staging masks and secondary-compression state.
 * @ingroup libtiff_private_lerc
 *
 * Codec-owned state must remain live until cleanup. Internal buffers and
 * dependency objects have the individual ownership described below;
 * borrowed parent methods must remain callable.
 */
typedef struct
{
    /**
     * @brief Cached maximum encoding error, zero for default lossless
     * operation.
     */
    double maxzerror;
    /** @brief Cached encoded LERC version. */
    int lerc_version;
    /** @brief Secondary none/Deflate/ZSTD selection. */
    int additional_compression;
    /** @brief Cached ZSTD level, including warned values. */
    int zstd_compress_level;
    /**
     * @brief Cached Deflate quality, including rejected attempted assignments.
     */
    int zipquality;
    /** @brief Encode/decode mode flags. */
    int state;

    /** @brief Current strile width in pixels. */
    uint32_t segment_width;
    /** @brief Current tile or truncated-strip height in rows. */
    uint32_t segment_height;

    /** @brief Logical current-strile decoded extent in bytes. */
    unsigned int uncompressed_size;
    /**
     * @brief Allocated main staging capacity in bytes, including compression
     * margin.
     */
    unsigned int uncompressed_alloc;
    /**
     * @brief Owned Ext row-interleaved staging reused as secondary-compression
     * output.
     */
    uint8_t *uncompressed_buffer;
    /** @brief Next staged input/output byte offset. */
    unsigned int uncompressed_offset;

    /** @brief Owned Ext planar multiband staging for per-band masks. */
    uint8_t *uncompressed_buffer_multiband;
    /** @brief Multiband staging capacity in bytes. */
    unsigned int uncompressed_buffer_multiband_alloc;

    /** @brief Validity-mask allocation capacity in bytes. */
    unsigned int mask_size;
    /** @brief Owned Ext pixel or per-band validity masks. */
    uint8_t *mask_buffer;

    /** @brief Compressed/intermediate allocation capacity in bytes. */
    unsigned int compressed_size;
    /** @brief Owned Ext LERC blob or secondary-decompressed staging. */
    void *compressed_buffer;

#if LIBDEFLATE_SUPPORT
    /**
     * @brief Owned optional libdeflate decompressor released by its matching
     * free function.
     */
    struct libdeflate_decompressor *libdeflate_dec;
    /**
     * @brief Owned optional libdeflate compressor released on quality
     * change/cleanup.
     */
    struct libdeflate_compressor *libdeflate_enc;
#endif

    /** @brief Borrowed parent tag getter restored during cleanup. */
    TIFFVGetMethod vgetparent;
    /** @brief Borrowed parent tag setter restored during cleanup. */
    TIFFVSetMethod vsetparent;
} LERCState;

#define GetLERCState(tif) ((LERCState *)(tif)->tif_data)
#define LERCDecoderState(tif) GetLERCState(tif)
#define LERCEncoderState(tif) GetLERCState(tif)

static int LERCDecode(TIFF *tif, uint8_t *op, tmsize_t occ, uint16_t s);

/**
 * @brief Accept LERC tag fixup without changes.
 * @ingroup libtiff_private_lerc
 *
 * No allocation, I/O, diagnostics, state or ownership effects.
 *
 * @param[in] tif Unused handle.
 * @return One unconditionally.
 */
static int LERCFixupTags(TIFF *tif)
{
    (void)tif;
    return 1;
}

/**
 * @brief Select LERC decode mode.
 * @ingroup libtiff_private_lerc
 *
 * Clears previous encode mode bits and marks decode initialisation without
 * freeing reusable staging/dependency objects. No allocation, pixel I/O,
 * diagnostics or ownership transfer.
 *
 * @param[in,out] tif Non-NULL live LERC handle with valid current
 * geometry/state; serialise access.
 * @return One unconditionally.
 */
static int LERCSetupDecode(TIFF *tif)
{
    LERCState *sp = LERCDecoderState(tif);

    assert(sp != NULL);

    /* if we were last encoding, terminate this mode */
    if (sp->state & LSTATE_INIT_ENCODE)
    {
        sp->state = 0;
    }

    sp->state |= LSTATE_INIT_DECODE;
    return 1;
}

/**
 * @brief Map TIFF sample format and depth to a LERC scalar code.
 * @ingroup libtiff_private_lerc
 *
 * Recognises signed/unsigned 8/16/32-bit integers and IEEE 32/64-bit
 * floats. Diagnoses unsupported combinations; no allocation, I/O, state
 * mutation or ownership transfer.
 *
 * @param[in] tif Non-NULL live LERC handle; only its SampleFormat and
 * BitsPerSample are read (and it is passed to the error handler).
 * @return LERC data type code zero through seven, or minus one on
 * unsupported input.
 */
static int GetLercDataType(TIFF *tif)
{
    TIFFDirectory *td = &tif->tif_dir;
    static const char module[] = "GetLercDataType";

    if (td->td_sampleformat == SAMPLEFORMAT_INT && td->td_bitspersample == 8)
    {
        return 0;
    }

    if (td->td_sampleformat == SAMPLEFORMAT_UINT && td->td_bitspersample == 8)
    {
        return 1;
    }

    if (td->td_sampleformat == SAMPLEFORMAT_INT && td->td_bitspersample == 16)
    {
        return 2;
    }

    if (td->td_sampleformat == SAMPLEFORMAT_UINT && td->td_bitspersample == 16)
    {
        return 3;
    }

    if (td->td_sampleformat == SAMPLEFORMAT_INT && td->td_bitspersample == 32)
    {
        return 4;
    }

    if (td->td_sampleformat == SAMPLEFORMAT_UINT && td->td_bitspersample == 32)
    {
        return 5;
    }

    if (td->td_sampleformat == SAMPLEFORMAT_IEEEFP &&
        td->td_bitspersample == 32)
    {
        return 6;
    }

    if (td->td_sampleformat == SAMPLEFORMAT_IEEEFP &&
        td->td_bitspersample == 64)
    {
        return 7;
    }

    TIFFErrorExtR(
        tif, module,
        "Unsupported combination of SampleFormat and td_bitspersample");
    return -1;
}

/**
 * @brief Size and reserve full-strile sample and validity-mask storage.
 * @ingroup libtiff_private_lerc
 *
 * Computes tile/truncated-strip dimensions and row-interleaved sample
 * bytes, resets staging offset and reserves margin for secondary
 * compression. Requires all intermediate uint64_t products/sums, mask
 * uint32_t products and dependency size casts to be representable; the
 * final unsigned allocation check is not comprehensive overflow validation.
 * Replaces owned uncompressed storage when growing and optionally grows
 * masks for alpha/NaN/per-band validity. Failure can release prior sample
 * storage and leave capacities/state changed, with the old mask allocation
 * retained after failed realloc. Diagnostics report detected
 * extent/allocation errors; no payload I/O or ownership transfer.
 *
 * @param[in,out] tif Non-NULL live LERC handle with valid current
 * geometry/state; serialise access.
 * @param[in,out] sp Borrowed live LERC state owned by tif.
 * @param[in] module Borrowed diagnostic context string, used synchronously.
 * @return One after applicable reservations, zero on detected
 * allocation/extent failure.
 */
static int SetupBuffers(TIFF *tif, LERCState *sp, const char *module)
{
    TIFFDirectory *td = &tif->tif_dir;
    uint64_t new_size_64;
    uint64_t new_alloc_64;
    unsigned int new_size;
    unsigned int new_alloc;

    sp->uncompressed_offset = 0;

    if (isTiled(tif))
    {
        sp->segment_width = td->td_tilewidth;
        sp->segment_height = td->td_tilelength;
    }
    else
    {
        sp->segment_width = td->td_imagewidth;
        sp->segment_height = td->td_imagelength - tif->tif_dir.td_row;
        if (sp->segment_height > td->td_rowsperstrip)
            sp->segment_height = td->td_rowsperstrip;
    }

    new_size_64 = (uint64_t)sp->segment_width * sp->segment_height *
                  (td->td_bitspersample / 8);
    if (td->td_planarconfig == PLANARCONFIG_CONTIG)
    {
        new_size_64 *= td->td_samplesperpixel;
    }

    new_size = (unsigned int)new_size_64;
    sp->uncompressed_size = new_size;

    /* add some margin as we are going to use it also to store deflate/zstd
     * compressed data. We also need extra margin when writing very small
     * rasters with one mask per band. */
    new_alloc_64 = 256 + new_size_64 + new_size_64 / 3;
#ifdef ZSTD_SUPPORT
    {
        size_t zstd_max = ZSTD_compressBound((size_t)new_size_64);
        if (new_alloc_64 < zstd_max)
        {
            new_alloc_64 = zstd_max;
        }
    }
#endif
    new_alloc = (unsigned int)new_alloc_64;
    if (new_alloc != new_alloc_64)
    {
        TIFFErrorExtR(tif, module, "Too large uncompressed strip/tile");
        _TIFFfreeExt(tif, sp->uncompressed_buffer);
        sp->uncompressed_buffer = NULL;
        sp->uncompressed_alloc = 0;
        return 0;
    }

    if (sp->uncompressed_alloc < new_alloc)
    {
        _TIFFfreeExt(tif, sp->uncompressed_buffer);
        sp->uncompressed_buffer = (uint8_t *)_TIFFmallocExt(tif, new_alloc);
        if (!sp->uncompressed_buffer)
        {
            TIFFErrorExtR(tif, module, "Cannot allocate buffer");
            _TIFFfreeExt(tif, sp->uncompressed_buffer);
            sp->uncompressed_buffer = NULL;
            sp->uncompressed_alloc = 0;
            return 0;
        }
        sp->uncompressed_alloc = new_alloc;
    }

    if ((td->td_planarconfig == PLANARCONFIG_CONTIG &&
         td->td_extrasamples > 0 && td->td_sampleinfo &&
         td->td_sampleinfo[td->td_extrasamples - 1] == EXTRASAMPLE_UNASSALPHA &&
         GetLercDataType(tif) == 1) ||
        (td->td_sampleformat == SAMPLEFORMAT_IEEEFP &&
         (td->td_bitspersample == 32 || td->td_bitspersample == 64)))
    {
        unsigned int mask_size = sp->segment_width * sp->segment_height;
#if LERC_AT_LEAST_VERSION(3, 0, 0)
        if (td->td_sampleformat == SAMPLEFORMAT_IEEEFP &&
            td->td_planarconfig == PLANARCONFIG_CONTIG)
        {
            /* We may need one mask per band */
            mask_size *= td->td_samplesperpixel;
        }
#endif
        if (sp->mask_size < mask_size)
        {
            void *mask_buffer =
                _TIFFreallocExt(tif, sp->mask_buffer, mask_size);
            if (mask_buffer == NULL)
            {
                TIFFErrorExtR(tif, module, "Cannot allocate buffer");
                sp->mask_size = 0;
                _TIFFfreeExt(tif, sp->uncompressed_buffer);
                sp->uncompressed_buffer = NULL;
                sp->uncompressed_alloc = 0;
                return 0;
            }
            sp->mask_buffer = (uint8_t *)mask_buffer;
            sp->mask_size = mask_size;
        }
    }

    return 1;
}

/*
 * Setup state for decoding a strip.
 */
/**
 * @brief Decode and validate an entire LERC strile into owned staging.
 * @ingroup libtiff_private_lerc
 *
 * Selects data type, reserves buffers and optionally decompresses
 * Deflate/ZSTD into reusable compressed storage. Inspects blob type,
 * dimensions, bands, size and masks, warning on unexpected version. Decodes
 * samples, expands binary alpha masks or restores invalid floating samples
 * as NaNs; supported multi-band mask layouts depend on LERC version.
 * Requires valid representable geometry/counts and raw extents within
 * dependency unsigned sizes. Buffers/dependency state can change on failure
 * and staged data must not be used after failure. Allocations remain
 * codec-owned until replacement/cleanup. Does not copy caller output,
 * consume it, or transfer ownership; dependency/validation errors diagnose.
 * Failure to allocate the compressed or multiband staging buffer returns
 * zero without a codec diagnostic (only _TIFFmallocExt()'s memory-limit
 * error, if a configured limit was exceeded).
 *
 * @param[in,out] tif Non-NULL live LERC handle with valid current
 * geometry/state; serialise access.
 * @param[in] s Unused sample-plane compatibility argument.
 * @return One after complete staged decode, zero on detected
 * format/geometry/allocation/dependency failure.
 */
static int LERCPreDecode(TIFF *tif, uint16_t s)
{
    static const char module[] = "LERCPreDecode";
    lerc_status lerc_ret;
    TIFFDirectory *td = &tif->tif_dir;
    LERCState *sp = LERCDecoderState(tif);
    int lerc_data_type;
    unsigned int infoArray[9];
    unsigned nomask_bands = td->td_samplesperpixel;
    int ndims;
    int use_mask = 0;
    uint8_t *lerc_data = tif->tif_rawcp;
    unsigned int lerc_data_size = (unsigned int)tif->tif_rawcc;

    (void)s;
    assert(sp != NULL);
    if (sp->state != LSTATE_INIT_DECODE)
        tif->tif_setupdecode(tif);

    lerc_data_type = GetLercDataType(tif);
    if (lerc_data_type < 0)
        return 0;

    if (!SetupBuffers(tif, sp, module))
        return 0;

    if (sp->additional_compression != LERC_ADD_COMPRESSION_NONE)
    {
        if (sp->compressed_size < sp->uncompressed_alloc)
        {
            _TIFFfreeExt(tif, sp->compressed_buffer);
            sp->compressed_buffer = _TIFFmallocExt(tif, sp->uncompressed_alloc);
            if (!sp->compressed_buffer)
            {
                sp->compressed_size = 0;
                return 0;
            }
            sp->compressed_size = sp->uncompressed_alloc;
        }
    }

    if (sp->additional_compression == LERC_ADD_COMPRESSION_DEFLATE)
    {
#if LIBDEFLATE_SUPPORT
        enum libdeflate_result res;
        size_t lerc_data_sizet = 0;
        if (sp->libdeflate_dec == NULL)
        {
            sp->libdeflate_dec = libdeflate_alloc_decompressor();
            if (sp->libdeflate_dec == NULL)
            {
                TIFFErrorExtR(tif, module, "Cannot allocate decompressor");
                return 0;
            }
        }

        res = libdeflate_zlib_decompress(
            sp->libdeflate_dec, tif->tif_rawcp, (size_t)tif->tif_rawcc,
            sp->compressed_buffer, sp->compressed_size, &lerc_data_sizet);
        if (res != LIBDEFLATE_SUCCESS)
        {
            TIFFErrorExtR(tif, module, "Decoding error at scanline %lu",
                          (unsigned long)tif->tif_dir.td_row);
            return 0;
        }
        assert(lerc_data_sizet == (unsigned int)lerc_data_sizet);
        lerc_data = (uint8_t *)sp->compressed_buffer;
        lerc_data_size = (unsigned int)lerc_data_sizet;
#else
        z_stream strm;
        int zlib_ret;

        memset(&strm, 0, sizeof(strm));
        strm.zalloc = NULL;
        strm.zfree = NULL;
        strm.opaque = NULL;
        zlib_ret = inflateInit(&strm);
        if (zlib_ret != Z_OK)
        {
            TIFFErrorExtR(tif, module, "inflateInit() failed");
            inflateEnd(&strm);
            return 0;
        }

        strm.avail_in = (uInt)tif->tif_rawcc;
        strm.next_in = tif->tif_rawcp;
        strm.avail_out = sp->compressed_size;
        strm.next_out = (Bytef *)sp->compressed_buffer;
        zlib_ret = inflate(&strm, Z_FINISH);
        if (zlib_ret != Z_STREAM_END && zlib_ret != Z_OK)
        {
            TIFFErrorExtR(tif, module, "inflate() failed");
            inflateEnd(&strm);
            return 0;
        }
        lerc_data = (uint8_t *)sp->compressed_buffer;
        lerc_data_size = sp->compressed_size - strm.avail_out;
        inflateEnd(&strm);
#endif
    }
    else if (sp->additional_compression == LERC_ADD_COMPRESSION_ZSTD)
    {
#ifdef ZSTD_SUPPORT
        size_t zstd_ret;

        zstd_ret = ZSTD_decompress(sp->compressed_buffer, sp->compressed_size,
                                   tif->tif_rawcp, (size_t)tif->tif_rawcc);
        if (ZSTD_isError(zstd_ret))
        {
            TIFFErrorExtR(tif, module, "Error in ZSTD_decompress(): %s",
                          ZSTD_getErrorName(zstd_ret));
            return 0;
        }

        lerc_data = (uint8_t *)sp->compressed_buffer;
        lerc_data_size = (unsigned int)zstd_ret;
#else
        TIFFErrorExtR(tif, module, "ZSTD support missing");
        return 0;
#endif
    }
    else if (sp->additional_compression != LERC_ADD_COMPRESSION_NONE)
    {
        TIFFErrorExtR(tif, module, "Unhandled additional compression");
        return 0;
    }

    lerc_ret =
        lerc_getBlobInfo(lerc_data, lerc_data_size, infoArray, NULL, 9, 0);
    if (lerc_ret != 0)
    {
        TIFFErrorExtR(tif, module, "lerc_getBlobInfo() failed");
        return 0;
    }

    /* If the configuration is compatible of a LERC mask, and that the */
    /* LERC info has dim == samplesperpixel - 1, then there is a LERC */
    /* mask. */
    if (td->td_planarconfig == PLANARCONFIG_CONTIG && td->td_extrasamples > 0 &&
        td->td_sampleinfo &&
        td->td_sampleinfo[td->td_extrasamples - 1] == EXTRASAMPLE_UNASSALPHA &&
        GetLercDataType(tif) == 1 &&
        infoArray[2] == td->td_samplesperpixel - 1U)
    {
        use_mask = 1;
        nomask_bands--;
    }
    else if (td->td_sampleformat == SAMPLEFORMAT_IEEEFP)
    {
        use_mask = 1;
    }

    ndims =
        (int)(td->td_planarconfig == PLANARCONFIG_CONTIG ? nomask_bands : 1);

    /* Info returned in infoArray is { version, dataType, nDim/nDepth, nCols,
        nRows, nBands, nValidPixels, blobSize,
        and starting with liblerc 3.0 nRequestedMasks } */
    if (infoArray[0] != (unsigned)sp->lerc_version)
    {
        TIFFWarningExtR(tif, module,
                        "Unexpected version number: %u. Expected: %d",
                        infoArray[0], sp->lerc_version);
    }
    if (infoArray[1] != (unsigned)lerc_data_type)
    {
        TIFFErrorExtR(tif, module, "Unexpected dataType: %u. Expected: %d",
                      infoArray[1], lerc_data_type);
        return 0;
    }

    const unsigned nFoundDims = infoArray[2];
#if LERC_AT_LEAST_VERSION(3, 0, 0)
    if (td->td_sampleformat == SAMPLEFORMAT_IEEEFP &&
        td->td_planarconfig == PLANARCONFIG_CONTIG &&
        td->td_samplesperpixel > 1)
    {
        if (nFoundDims != 1 && nFoundDims != (unsigned)ndims)
        {
            TIFFErrorExtR(tif, module, "Unexpected nDim: %u. Expected: 1 or %d",
                          nFoundDims, ndims);
            return 0;
        }
    }
    else
#endif
        if (nFoundDims != (unsigned)ndims)
    {
        TIFFErrorExtR(tif, module, "Unexpected nDim: %u. Expected: %d",
                      nFoundDims, ndims);
        return 0;
    }

    if (infoArray[3] != sp->segment_width)
    {
        TIFFErrorExtR(tif, module, "Unexpected nCols: %u. Expected: %u",
                      infoArray[3], sp->segment_width);
        return 0;
    }
    if (infoArray[4] != sp->segment_height)
    {
        TIFFErrorExtR(tif, module, "Unexpected nRows: %u. Expected: %u",
                      infoArray[4], sp->segment_height);
        return 0;
    }

    const unsigned nFoundBands = infoArray[5];
    if (td->td_sampleformat == SAMPLEFORMAT_IEEEFP &&
        td->td_planarconfig == PLANARCONFIG_CONTIG &&
        td->td_samplesperpixel > 1 && nFoundDims == 1)
    {
#if !LERC_AT_LEAST_VERSION(3, 0, 0)
        if (nFoundBands == td->td_samplesperpixel)
        {
            TIFFErrorExtR(
                tif, module,
                "Unexpected nBands: %d. This file may have been generated with "
                "a liblerc version >= 3.0, with one mask per band, and is not "
                "supported by this older version of liblerc",
                nFoundBands);
            return 0;
        }
#endif
        if (nFoundBands != td->td_samplesperpixel)
        {
            TIFFErrorExtR(tif, module, "Unexpected nBands: %u. Expected: %d",
                          nFoundBands, td->td_samplesperpixel);
            return 0;
        }
    }
    else if (nFoundBands != 1)
    {
        TIFFErrorExtR(tif, module, "Unexpected nBands: %u. Expected: %d",
                      nFoundBands, 1);
        return 0;
    }

    if (infoArray[7] != lerc_data_size)
    {
        TIFFErrorExtR(tif, module, "Unexpected blobSize: %u. Expected: %u",
                      infoArray[7], lerc_data_size);
        return 0;
    }

    int nRequestedMasks = use_mask ? 1 : 0;
#if LERC_AT_LEAST_VERSION(3, 0, 0)
    const int nFoundMasks = (int)infoArray[8];
    if (td->td_sampleformat == SAMPLEFORMAT_IEEEFP &&
        td->td_planarconfig == PLANARCONFIG_CONTIG &&
        td->td_samplesperpixel > 1 && nFoundDims == 1)
    {
        if (nFoundMasks != 0 && nFoundMasks != td->td_samplesperpixel)
        {
            TIFFErrorExtR(tif, module,
                          "Unexpected nFoundMasks: %d. Expected: 0 or %d",
                          nFoundMasks, td->td_samplesperpixel);
            return 0;
        }
        nRequestedMasks = nFoundMasks;
    }
    else
    {
        if (nFoundMasks != 0 && nFoundMasks != 1)
        {
            TIFFErrorExtR(tif, module,
                          "Unexpected nFoundMasks: %d. Expected: 0 or 1",
                          nFoundMasks);
            return 0;
        }
    }
    if (td->td_sampleformat == SAMPLEFORMAT_IEEEFP && nFoundMasks == 0)
    {
        nRequestedMasks = 0;
        use_mask = 0;
    }
#endif

    const unsigned nb_pixels = sp->segment_width * sp->segment_height;

#if LERC_AT_LEAST_VERSION(3, 0, 0)
    if (nRequestedMasks > 1)
    {
        unsigned int num_bytes_needed =
            nb_pixels * td->td_samplesperpixel * (td->td_bitspersample / 8);
        if (sp->uncompressed_buffer_multiband_alloc < num_bytes_needed)
        {
            _TIFFfreeExt(tif, sp->uncompressed_buffer_multiband);
            sp->uncompressed_buffer_multiband =
                (uint8_t *)_TIFFmallocExt(tif, num_bytes_needed);
            if (!sp->uncompressed_buffer_multiband)
            {
                sp->uncompressed_buffer_multiband_alloc = 0;
                return 0;
            }
            sp->uncompressed_buffer_multiband_alloc = num_bytes_needed;
        }
        lerc_ret = lerc_decode(lerc_data, lerc_data_size, nRequestedMasks,
                               sp->mask_buffer, (int)nFoundDims,
                               (int)sp->segment_width, (int)sp->segment_height,
                               (int)nFoundBands, (unsigned int)lerc_data_type,
                               sp->uncompressed_buffer_multiband);
    }
    else
#endif
    {
        lerc_ret = lerc_decode(
            lerc_data, lerc_data_size,
#if LERC_AT_LEAST_VERSION(3, 0, 0)
            nRequestedMasks,
#endif
            use_mask ? sp->mask_buffer : NULL, (int)nFoundDims,
            (int)sp->segment_width, (int)sp->segment_height, (int)nFoundBands,
            (unsigned int)lerc_data_type, sp->uncompressed_buffer);
    }
    if (lerc_ret != 0)
    {
        TIFFErrorExtR(tif, module, "lerc_decode() failed");
        return 0;
    }

    /* Interleave alpha mask with other samples. */
    if (use_mask && GetLercDataType(tif) == 1)
    {
        unsigned src_stride = (unsigned int)((td->td_samplesperpixel - 1) *
                                             (td->td_bitspersample / 8));
        unsigned dst_stride =
            (unsigned int)(td->td_samplesperpixel * (td->td_bitspersample / 8));
        unsigned i = sp->segment_width * sp->segment_height;
        /* Operate from end to begin to be able to move in place */
        while (i > 0 && i > nomask_bands)
        {
            i--;
            sp->uncompressed_buffer[i * dst_stride + td->td_samplesperpixel -
                                    1] = (uint8_t)(255 * sp->mask_buffer[i]);
            memcpy(sp->uncompressed_buffer + i * dst_stride,
                   sp->uncompressed_buffer + i * src_stride, src_stride);
        }
        /* First pixels must use memmove due to overlapping areas */
        while (i > 0)
        {
            i--;
            sp->uncompressed_buffer[i * dst_stride + td->td_samplesperpixel -
                                    1] = (uint8_t)(255 * sp->mask_buffer[i]);
            memmove(sp->uncompressed_buffer + i * dst_stride,
                    sp->uncompressed_buffer + i * src_stride, src_stride);
        }
    }
    else if (use_mask && td->td_sampleformat == SAMPLEFORMAT_IEEEFP)
    {
        unsigned i;
#if WORDS_BIGENDIAN
        const unsigned char nan_bytes[] = {0x7f, 0xc0, 0, 0};
#else
        const unsigned char nan_bytes[] = {0, 0, 0xc0, 0x7f};
#endif
        float nan_float32;
        memcpy(&nan_float32, nan_bytes, 4);

        if (td->td_planarconfig == PLANARCONFIG_SEPARATE ||
            td->td_samplesperpixel == 1)
        {
            if (td->td_bitspersample == 32)
            {
                for (i = 0; i < nb_pixels; i++)
                {
                    if (sp->mask_buffer[i] == 0)
                        ((float *)sp->uncompressed_buffer)[i] = nan_float32;
                }
            }
            else
            {
                const double nan_float64 = (double)nan_float32;
                for (i = 0; i < nb_pixels; i++)
                {
                    if (sp->mask_buffer[i] == 0)
                        ((double *)sp->uncompressed_buffer)[i] = nan_float64;
                }
            }
        }
        else if (nRequestedMasks == 1)
        {
            assert(nFoundDims == td->td_samplesperpixel);
            assert(nFoundBands == 1);

            unsigned k = 0;
            if (td->td_bitspersample == 32)
            {
                for (i = 0; i < nb_pixels; i++)
                {
                    for (int j = 0; j < td->td_samplesperpixel; j++)
                    {
                        if (sp->mask_buffer[i] == 0)
                            ((float *)sp->uncompressed_buffer)[k] = nan_float32;
                        ++k;
                    }
                }
            }
            else
            {
                const double nan_float64 = (double)nan_float32;
                for (i = 0; i < nb_pixels; i++)
                {
                    for (int j = 0; j < td->td_samplesperpixel; j++)
                    {
                        if (sp->mask_buffer[i] == 0)
                            ((double *)sp->uncompressed_buffer)[k] =
                                nan_float64;
                        ++k;
                    }
                }
            }
        }
#if LERC_AT_LEAST_VERSION(3, 0, 0)
        else
        {
            assert(nRequestedMasks == td->td_samplesperpixel);
            assert(nFoundDims == 1);
            assert(nFoundBands == td->td_samplesperpixel);

            unsigned k = 0;
            if (td->td_bitspersample == 32)
            {
                for (i = 0; i < nb_pixels; i++)
                {
                    for (int j = 0; j < td->td_samplesperpixel; j++)
                    {
                        if (sp->mask_buffer[i + (unsigned int)j * nb_pixels] ==
                            0)
                            ((float *)sp->uncompressed_buffer)[k] = nan_float32;
                        else
                            ((float *)sp->uncompressed_buffer)[k] =
                                ((float *)sp->uncompressed_buffer_multiband)
                                    [i + (unsigned int)j * nb_pixels];
                        ++k;
                    }
                }
            }
            else
            {
                const double nan_float64 = (double)nan_float32;
                for (i = 0; i < nb_pixels; i++)
                {
                    for (int j = 0; j < td->td_samplesperpixel; j++)
                    {
                        if (sp->mask_buffer[i + (unsigned int)j * nb_pixels] ==
                            0)
                            ((double *)sp->uncompressed_buffer)[k] =
                                nan_float64;
                        else
                            ((double *)sp->uncompressed_buffer)[k] =
                                ((double *)sp->uncompressed_buffer_multiband)
                                    [i + (unsigned int)j * nb_pixels];
                        ++k;
                    }
                }
            }
        }
#endif
    }

    return 1;
}

/*
 * Decode a strip, tile or scanline.
 */
/**
 * @brief Copy the next decoded byte extent from LERC staging.
 * @ingroup libtiff_private_lerc
 *
 * Requires successful predecode and a nonnegative request. Checks staging
 * presence and remaining bounds, copies bytes and advances
 * uncompressed_offset. Rejection clears requested output and diagnoses. No
 * allocation, dependency decoding, client I/O or ownership transfer;
 * source/destination must not overlap.
 *
 * @param[in,out] tif Non-NULL live LERC handle with valid current
 * geometry/state; serialise access.
 * @param[out] op Borrowed writable occ-byte destination, disjoint from
 * codec staging.
 * @param[in] occ Nonnegative decoded byte extent within remaining staging
 * data.
 * @param[in] s Unused sample-plane compatibility argument.
 * @return One after copy, zero on missing staging or overread request.
 */
static int LERCDecode(TIFF *tif, uint8_t *op, tmsize_t occ, uint16_t s)
{
    static const char module[] = "LERCDecode";
    LERCState *sp = LERCDecoderState(tif);

    (void)s;
    assert(sp != NULL);
    assert(sp->state == LSTATE_INIT_DECODE);

    if (sp->uncompressed_buffer == NULL)
    {
        memset(op, 0, (size_t)occ);
        TIFFErrorExtR(tif, module, "Uncompressed buffer not allocated");
        return 0;
    }

    if ((uint64_t)sp->uncompressed_offset + (uint64_t)occ >
        sp->uncompressed_size)
    {
        memset(op, 0, (size_t)occ);
        TIFFErrorExtR(tif, module, "Too many bytes read");
        return 0;
    }

    memcpy(op, sp->uncompressed_buffer + sp->uncompressed_offset, (size_t)occ);
    sp->uncompressed_offset += (unsigned int)occ;

    return 1;
}

#ifndef LERC_READ_ONLY

/**
 * @brief Select LERC encode mode.
 * @ingroup libtiff_private_lerc
 *
 * Clears previous decode mode bits and marks encoding without freeing
 * reusable buffers. No allocation, diagnostics, payload I/O or ownership
 * transfer.
 *
 * @param[in,out] tif Non-NULL live LERC handle with valid current
 * geometry/state; serialise access.
 * @return One unconditionally.
 */
static int LERCSetupEncode(TIFF *tif)
{
    LERCState *sp = LERCEncoderState(tif);

    assert(sp != NULL);
    if (sp->state & LSTATE_INIT_DECODE)
    {
        sp->state = 0;
    }

    sp->state |= LSTATE_INIT_ENCODE;

    return 1;
}

/*
 * Reset encoding state at the start of a strip.
 */
/**
 * @brief Validate LERC sample type and prepare full-strile input storage.
 * @ingroup libtiff_private_lerc
 *
 * Ensures encode mode, maps sample type and reserves buffers/masks through
 * SetupBuffers. Requires valid representable geometry and successful setup;
 * helper failures can invalidate old staging. No pixel I/O or caller
 * ownership transfer.
 *
 * @param[in,out] tif Non-NULL live LERC handle with valid current
 * geometry/state; serialise access.
 * @param[in] s Unused sample-plane compatibility argument.
 * @return One after preparation, zero on sample-type or buffer setup
 * failure.
 */
static int LERCPreEncode(TIFF *tif, uint16_t s)
{
    static const char module[] = "LERCPreEncode";
    LERCState *sp = LERCEncoderState(tif);
    int lerc_data_type;

    (void)s;
    assert(sp != NULL);
    if (sp->state != LSTATE_INIT_ENCODE)
        tif->tif_setupencode(tif);

    lerc_data_type = GetLercDataType(tif);
    if (lerc_data_type < 0)
        return 0;

    if (!SetupBuffers(tif, sp, module))
        return 0;

    return 1;
}

/*
 * Encode a chunk of pixels.
 */
/**
 * @brief Stage the next LERC input bytes for later frame encoding.
 * @ingroup libtiff_private_lerc
 *
 * Requires successful preencode and nonnegative cc. Checks offset+cc
 * against decoded strile size, copies borrowed input and advances offset.
 * Does not compress until postencode; input remains unchanged. Rejection
 * diagnoses without copying. No client I/O or ownership transfer.
 *
 * @param[in,out] tif Non-NULL live LERC handle with valid current
 * geometry/state; serialise access.
 * @param[in] bp Borrowed readable cc-byte input copied synchronously; not
 * retained or modified.
 * @param[in] cc Nonnegative input byte extent within remaining staging
 * capacity.
 * @param[in] s Unused sample-plane compatibility argument.
 * @return One after staging, zero for excess input extent.
 */
static int LERCEncode(TIFF *tif, uint8_t *bp, tmsize_t cc, uint16_t s)
{
    static const char module[] = "LERCEncode";
    LERCState *sp = LERCEncoderState(tif);

    (void)s;
    assert(sp != NULL);
    assert(sp->state == LSTATE_INIT_ENCODE);

    if ((uint64_t)sp->uncompressed_offset + (uint64_t)cc >
        sp->uncompressed_size)
    {
        TIFFErrorExtR(tif, module, "Too many bytes written");
        return 0;
    }

    memcpy(sp->uncompressed_buffer + sp->uncompressed_offset, bp, (size_t)cc);
    sp->uncompressed_offset += (unsigned int)cc;

    return 1;
}

/*
 * Finish off an encoded strip by flushing it.
 */
/**
 * @brief Encode a fully staged LERC strile and flush optional secondary
 * compression.
 * @ingroup libtiff_private_lerc
 *
 * Requires exact staged byte count, valid scalar/geometry arithmetic and
 * suitable masks. Compacts binary unassociated alpha into validity masks or
 * detects floating NaNs; per-band NaN masks require supported LERC
 * versions. Can allocate multiband/compressed storage, mutate staging,
 * encode the selected version/error tolerance, then optionally compress
 * with Deflate/ZSTD. Temporarily replaces tif_rawdata during flush and
 * restores only that pointer afterward; rawcp can still refer into codec
 * staging until reset. Dependency/validation/I/O errors diagnose where
 * handled and can leave changed buffers, metadata or file bytes; no
 * rollback, IFD persistence or durability guarantee. Failure to allocate the
 * compressed or multiband staging buffer returns zero without a codec
 * diagnostic (only _TIFFmallocExt()'s memory-limit error, if a configured
 * limit was exceeded). Caller input had already been copied; no ownership
 * transfer.
 *
 * @param[in,out] tif Non-NULL live LERC handle with valid current
 * geometry/state; serialise access.
 * @return One after encoding and payload flush, zero on detected
 * extent/mask/allocation/dependency/I/O failure.
 */
static int LERCPostEncode(TIFF *tif)
{
    lerc_status lerc_ret;
    static const char module[] = "LERCPostEncode";
    LERCState *sp = LERCEncoderState(tif);
    unsigned int numBytesWritten = 0;
    TIFFDirectory *td = &tif->tif_dir;
    int use_mask = 0;
    unsigned dst_nbands = td->td_samplesperpixel;

    if (sp->uncompressed_offset != sp->uncompressed_size)
    {
        TIFFErrorExtR(tif, module, "Unexpected number of bytes in the buffer");
        return 0;
    }

    int mask_count = 1;
    const unsigned nb_pixels = sp->segment_width * sp->segment_height;

    /* Extract alpha mask (if containing only 0 and 255 values, */
    /* and compact array of regular bands */
    if (td->td_planarconfig == PLANARCONFIG_CONTIG && td->td_extrasamples > 0 &&
        td->td_sampleinfo &&
        td->td_sampleinfo[td->td_extrasamples - 1] == EXTRASAMPLE_UNASSALPHA &&
        GetLercDataType(tif) == 1)
    {
        const unsigned dst_stride =
            (unsigned int)((td->td_samplesperpixel - 1) *
                           (td->td_bitspersample / 8));
        const unsigned src_stride =
            (unsigned int)(td->td_samplesperpixel * (td->td_bitspersample / 8));
        unsigned i = 0;

        use_mask = 1;
        for (i = 0; i < nb_pixels; i++)
        {
            int v = sp->uncompressed_buffer[i * src_stride +
                                            td->td_samplesperpixel - 1];
            if (v != 0 && v != 255)
            {
                use_mask = 0;
                break;
            }
        }

        if (use_mask)
        {
            dst_nbands--;
            /* First pixels must use memmove due to overlapping areas */
            for (i = 0; i < dst_nbands && i < nb_pixels; i++)
            {
                memmove(sp->uncompressed_buffer + i * dst_stride,
                        sp->uncompressed_buffer + i * src_stride, dst_stride);
                sp->mask_buffer[i] =
                    sp->uncompressed_buffer[i * src_stride +
                                            td->td_samplesperpixel - 1];
            }
            for (; i < nb_pixels; i++)
            {
                memcpy(sp->uncompressed_buffer + i * dst_stride,
                       sp->uncompressed_buffer + i * src_stride, dst_stride);
                sp->mask_buffer[i] =
                    sp->uncompressed_buffer[i * src_stride +
                                            td->td_samplesperpixel - 1];
            }
        }
    }
    else if (td->td_sampleformat == SAMPLEFORMAT_IEEEFP &&
             (td->td_bitspersample == 32 || td->td_bitspersample == 64))
    {
        /* Check for NaN values */
        unsigned i;
        if (td->td_bitspersample == 32)
        {
            if (td->td_planarconfig == PLANARCONFIG_CONTIG && dst_nbands > 1)
            {
                unsigned k = 0;
                for (i = 0; i < nb_pixels; i++)
                {
                    int count_nan = 0;
                    for (int j = 0; j < td->td_samplesperpixel; ++j)
                    {
                        const float val = ((float *)sp->uncompressed_buffer)[k];
                        ++k;
                        if (isnan(val))
                        {
                            ++count_nan;
                        }
                    }
                    if (count_nan > 0)
                    {
                        use_mask = 1;
                        if (count_nan < td->td_samplesperpixel)
                        {
                            mask_count = td->td_samplesperpixel;
                            break;
                        }
                    }
                }
            }
            else
            {
                for (i = 0; i < nb_pixels; i++)
                {
                    const float val = ((float *)sp->uncompressed_buffer)[i];
                    if (isnan(val))
                    {
                        use_mask = 1;
                        break;
                    }
                }
            }
        }
        else
        {
            if (td->td_planarconfig == PLANARCONFIG_CONTIG && dst_nbands > 1)
            {
                unsigned k = 0;
                for (i = 0; i < nb_pixels; i++)
                {
                    int count_nan = 0;
                    for (int j = 0; j < td->td_samplesperpixel; ++j)
                    {
                        const double val =
                            ((double *)sp->uncompressed_buffer)[k];
                        ++k;
                        if (isnan(val))
                        {
                            ++count_nan;
                        }
                    }
                    if (count_nan > 0)
                    {
                        use_mask = 1;
                        if (count_nan < td->td_samplesperpixel)
                        {
                            mask_count = td->td_samplesperpixel;
                            break;
                        }
                    }
                }
            }
            else
            {
                for (i = 0; i < nb_pixels; i++)
                {
                    const double val = ((double *)sp->uncompressed_buffer)[i];
                    if (isnan(val))
                    {
                        use_mask = 1;
                        break;
                    }
                }
            }
        }

        if (use_mask)
        {
            if (mask_count > 1)
            {
#if LERC_AT_LEAST_VERSION(3, 0, 0)
                unsigned int num_bytes_needed =
                    nb_pixels * dst_nbands * (td->td_bitspersample / 8);
                if (sp->uncompressed_buffer_multiband_alloc < num_bytes_needed)
                {
                    _TIFFfreeExt(tif, sp->uncompressed_buffer_multiband);
                    sp->uncompressed_buffer_multiband =
                        (uint8_t *)_TIFFmallocExt(tif, num_bytes_needed);
                    if (!sp->uncompressed_buffer_multiband)
                    {
                        sp->uncompressed_buffer_multiband_alloc = 0;
                        return 0;
                    }
                    sp->uncompressed_buffer_multiband_alloc = num_bytes_needed;
                }

                unsigned k = 0;
                if (td->td_bitspersample == 32)
                {
                    for (i = 0; i < nb_pixels; i++)
                    {
                        for (int j = 0; j < td->td_samplesperpixel; ++j)
                        {
                            const float val =
                                ((float *)sp->uncompressed_buffer)[k];
                            ((float *)sp->uncompressed_buffer_multiband)
                                [i + (unsigned int)j * nb_pixels] = val;
                            ++k;
                            sp->mask_buffer[i + (unsigned int)j * nb_pixels] =
                                !isnan(val) ? 255 : 0;
                        }
                    }
                }
                else
                {
                    for (i = 0; i < nb_pixels; i++)
                    {
                        for (int j = 0; j < td->td_samplesperpixel; ++j)
                        {
                            const double val =
                                ((double *)sp->uncompressed_buffer)[k];
                            ((double *)sp->uncompressed_buffer_multiband)
                                [i + (unsigned int)j * nb_pixels] = val;
                            ++k;
                            sp->mask_buffer[i + (unsigned int)j * nb_pixels] =
                                !isnan(val) ? 255 : 0;
                        }
                    }
                }
#else
                TIFFErrorExtR(tif, module,
                              "lerc_encode() would need to create one mask per "
                              "sample, but this requires liblerc >= 3.0");
                return 0;
#endif
            }
            else if (td->td_planarconfig == PLANARCONFIG_CONTIG &&
                     dst_nbands > 1)
            {
                if (td->td_bitspersample == 32)
                {
                    for (i = 0; i < nb_pixels; i++)
                    {
                        const float val =
                            ((float *)sp->uncompressed_buffer)[i * dst_nbands];
                        sp->mask_buffer[i] = !isnan(val) ? 255 : 0;
                    }
                }
                else
                {
                    for (i = 0; i < nb_pixels; i++)
                    {
                        const double val =
                            ((double *)sp->uncompressed_buffer)[i * dst_nbands];
                        sp->mask_buffer[i] = !isnan(val) ? 255 : 0;
                    }
                }
            }
            else
            {
                if (td->td_bitspersample == 32)
                {
                    for (i = 0; i < nb_pixels; i++)
                    {
                        const float val = ((float *)sp->uncompressed_buffer)[i];
                        sp->mask_buffer[i] = !isnan(val) ? 255 : 0;
                    }
                }
                else
                {
                    for (i = 0; i < nb_pixels; i++)
                    {
                        const double val =
                            ((double *)sp->uncompressed_buffer)[i];
                        sp->mask_buffer[i] = !isnan(val) ? 255 : 0;
                    }
                }
            }
        }
    }

    unsigned int estimated_compressed_size = sp->uncompressed_alloc;
#if LERC_AT_LEAST_VERSION(3, 0, 0)
    if (mask_count > 1)
    {
        uint64_t mask_overhead =
            ((uint64_t)sp->segment_width * sp->segment_height *
                 (unsigned)mask_count +
             7) /
            8;
        if (mask_overhead > UINT_MAX - estimated_compressed_size)
        {
            TIFFErrorExtR(tif, module,
                          "Integer overflow in estimated compressed size");
            return 0;
        }
        estimated_compressed_size += (unsigned int)mask_overhead;
    }
#endif

    if (sp->compressed_size < estimated_compressed_size)
    {
        _TIFFfreeExt(tif, sp->compressed_buffer);
        sp->compressed_buffer = _TIFFmallocExt(tif, estimated_compressed_size);
        if (!sp->compressed_buffer)
        {
            sp->compressed_size = 0;
            return 0;
        }
        sp->compressed_size = estimated_compressed_size;
    }

#if LERC_AT_LEAST_VERSION(3, 0, 0)
    if (mask_count > 1)
    {
        lerc_ret = lerc_encodeForVersion(
            sp->uncompressed_buffer_multiband, sp->lerc_version,
            (unsigned int)GetLercDataType(tif), 1, (int)sp->segment_width,
            (int)sp->segment_height, (int)dst_nbands, (int)dst_nbands,
            sp->mask_buffer, sp->maxzerror,
            (unsigned char *)sp->compressed_buffer, sp->compressed_size,
            &numBytesWritten);
    }
    else
#endif
    {
        lerc_ret = lerc_encodeForVersion(
            sp->uncompressed_buffer, sp->lerc_version,
            (unsigned int)GetLercDataType(tif),
            (int)(td->td_planarconfig == PLANARCONFIG_CONTIG ? dst_nbands : 1),
            (int)sp->segment_width, (int)sp->segment_height, 1,
#if LERC_AT_LEAST_VERSION(3, 0, 0)
            use_mask ? 1 : 0,
#endif
            use_mask ? sp->mask_buffer : NULL, sp->maxzerror,
            (unsigned char *)sp->compressed_buffer, sp->compressed_size,
            &numBytesWritten);
    }
    if (lerc_ret != 0)
    {
        TIFFErrorExtR(tif, module, "lerc_encode() failed");
        return 0;
    }
    assert(numBytesWritten < estimated_compressed_size);

    if (sp->additional_compression == LERC_ADD_COMPRESSION_DEFLATE)
    {
#if LIBDEFLATE_SUPPORT
        if (sp->libdeflate_enc == NULL)
        {
            /* To get results as good as zlib, we ask for an extra */
            /* level of compression */
            sp->libdeflate_enc = libdeflate_alloc_compressor(
                sp->zipquality == Z_DEFAULT_COMPRESSION ? 7
                : sp->zipquality >= 6 && sp->zipquality <= 9
                    ? sp->zipquality + 1
                    : sp->zipquality);
            if (sp->libdeflate_enc == NULL)
            {
                TIFFErrorExtR(tif, module, "Cannot allocate compressor");
                return 0;
            }
        }

        /* Should not happen normally */
        if (libdeflate_zlib_compress_bound(
                sp->libdeflate_enc, numBytesWritten) > sp->uncompressed_alloc)
        {
            TIFFErrorExtR(tif, module,
                          "Output buffer for libdeflate too small");
            return 0;
        }

        tif->tif_rawcc = (tmsize_t)libdeflate_zlib_compress(
            sp->libdeflate_enc, sp->compressed_buffer, numBytesWritten,
            sp->uncompressed_buffer, sp->uncompressed_alloc);

        if (tif->tif_rawcc == 0)
        {
            TIFFErrorExtR(tif, module, "Encoder error at scanline %lu",
                          (unsigned long)tif->tif_dir.td_row);
            return 0;
        }
#else
        z_stream strm;
        int zlib_ret;
        int cappedQuality = sp->zipquality;
        if (cappedQuality > Z_BEST_COMPRESSION)
            cappedQuality = Z_BEST_COMPRESSION;

        memset(&strm, 0, sizeof(strm));
        strm.zalloc = NULL;
        strm.zfree = NULL;
        strm.opaque = NULL;
        zlib_ret = deflateInit(&strm, cappedQuality);
        if (zlib_ret != Z_OK)
        {
            TIFFErrorExtR(tif, module, "deflateInit() failed");
            return 0;
        }

        strm.avail_in = numBytesWritten;
        strm.next_in = sp->compressed_buffer;
        strm.avail_out = sp->uncompressed_alloc;
        strm.next_out = sp->uncompressed_buffer;
        zlib_ret = deflate(&strm, Z_FINISH);
        if (zlib_ret == Z_STREAM_END)
        {
            tif->tif_rawcc = sp->uncompressed_alloc - strm.avail_out;
        }
        deflateEnd(&strm);
        if (zlib_ret != Z_STREAM_END)
        {
            TIFFErrorExtR(tif, module, "deflate() failed");
            return 0;
        }
#endif
        {
            int ret;
            uint8_t *tif_rawdata_backup = tif->tif_rawdata;
            tif->tif_rawdata = sp->uncompressed_buffer;
            ret = TIFFFlushData1(tif);
            tif->tif_rawdata = tif_rawdata_backup;
            if (!ret)
            {
                return 0;
            }
        }
    }
    else if (sp->additional_compression == LERC_ADD_COMPRESSION_ZSTD)
    {
#ifdef ZSTD_SUPPORT
        size_t zstd_ret = ZSTD_compress(
            sp->uncompressed_buffer, sp->uncompressed_alloc,
            sp->compressed_buffer, numBytesWritten, sp->zstd_compress_level);
        if (ZSTD_isError(zstd_ret))
        {
            TIFFErrorExtR(tif, module, "Error in ZSTD_compress(): %s",
                          ZSTD_getErrorName(zstd_ret));
            return 0;
        }

        {
            int ret;
            uint8_t *tif_rawdata_backup = tif->tif_rawdata;
            tif->tif_rawdata = sp->uncompressed_buffer;
            tif->tif_rawcc = (tmsize_t)zstd_ret;
            ret = TIFFFlushData1(tif);
            tif->tif_rawdata = tif_rawdata_backup;
            if (!ret)
            {
                return 0;
            }
        }
#else
        TIFFErrorExtR(tif, module, "ZSTD support missing");
        return 0;
#endif
    }
    else if (sp->additional_compression != LERC_ADD_COMPRESSION_NONE)
    {
        TIFFErrorExtR(tif, module, "Unhandled additional compression");
        return 0;
    }
    else
    {
        int ret;
        uint8_t *tif_rawdata_backup = tif->tif_rawdata;
        tif->tif_rawdata = (uint8_t *)sp->compressed_buffer;
        tif->tif_rawcc = numBytesWritten;
        ret = TIFFFlushData1(tif);
        tif->tif_rawdata = tif_rawdata_backup;
        if (!ret)
            return 0;
    }

    return 1;
}

#endif /* LERC_READ_ONLY */

/**
 * @brief Release LERC staging masks secondary compressors and outer state.
 * @ingroup libtiff_private_lerc
 *
 * Restores saved tag hooks, frees sample/multiband/compressed/mask Ext
 * allocations and optional libdeflate objects, frees outer state, clears
 * tif_data and resets compression methods. Invalidates all codec-owned
 * views. Does not encode or flush pending staging, close the borrowed
 * handle or return status.
 *
 * @param[in,out] tif Non-NULL live LERC handle with valid current
 * geometry/state; serialise access.
 */
static void LERCCleanup(TIFF *tif)
{
    LERCState *sp = GetLERCState(tif);

    assert(sp != NULL);

    tif->tif_tagmethods.vgetfield = sp->vgetparent;
    tif->tif_tagmethods.vsetfield = sp->vsetparent;

    _TIFFfreeExt(tif, sp->uncompressed_buffer);
    _TIFFfreeExt(tif, sp->uncompressed_buffer_multiband);
    _TIFFfreeExt(tif, sp->compressed_buffer);
    _TIFFfreeExt(tif, sp->mask_buffer);

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
 * @brief Define LERC parameters error tolerance version and
 * secondary-compression tags.
 * @ingroup libtiff_private_lerc
 *
 * Immutable library-lifetime metadata merged into a handle; no caller
 * ownership transfer.
 */
static const TIFFField LERCFields[] = {
    {TIFFTAG_LERC_PARAMETERS, TIFF_VARIABLE2, TIFF_VARIABLE2, TIFF_LONG, 0,
     TIFF_SETGET_C32_UINT32, FIELD_CUSTOM, FALSE, TRUE,
     (char *)"LercParameters", NULL},
    {TIFFTAG_LERC_MAXZERROR, 0, 0, TIFF_ANY, 0, TIFF_SETGET_DOUBLE,
     FIELD_PSEUDO, TRUE, FALSE, (char *)"LercMaximumError", NULL},
    {TIFFTAG_LERC_VERSION, 0, 0, TIFF_ANY, 0, TIFF_SETGET_UINT32, FIELD_PSEUDO,
     FALSE, FALSE, (char *)"LercVersion", NULL},
    {TIFFTAG_LERC_ADD_COMPRESSION, 0, 0, TIFF_ANY, 0, TIFF_SETGET_UINT32,
     FIELD_PSEUDO, FALSE, FALSE, (char *)"LercAdditionalCompression", NULL},
    {TIFFTAG_ZSTD_LEVEL, 0, 0, TIFF_ANY, 0, TIFF_SETGET_INT, FIELD_PSEUDO, TRUE,
     FALSE, (char *)"ZSTD zstd_compress_level", NULL},
    {TIFFTAG_ZIPQUALITY, 0, 0, TIFF_ANY, 0, TIFF_SETGET_INT, FIELD_PSEUDO, TRUE,
     FALSE, (char *)"", NULL},
};

/**
 * @brief Forward a variadic assignment directly to the saved parent setter.
 * @ingroup libtiff_private_lerc
 *
 * Constructs a va_list and synchronously calls vsetparent, then ends the
 * list. Used to store LERC_PARAMETERS without recursive codec dispatch.
 * Tag-specific promotion, ownership and side effects follow the parent
 * setter; see @ref tag_arguments. No independent payload I/O.
 *
 * @param[in,out] tif Non-NULL live LERC handle with valid current
 * geometry/state; serialise access.
 * @param[in] tag Codec pseudo-tag/custom tag or delegated identifier.
 * @param[in] ... Promoted tag-specific values/pointers required by the
 * saved parent setter; consumed synchronously.
 * @return Saved parent setter status.
 */
static int LERCVSetFieldBase(TIFF *tif, uint32_t tag, ...)
{
    LERCState *sp = GetLERCState(tif);
    int ret;
    va_list ap;
    va_start(ap, tag);
    ret = (*sp->vsetparent)(tif, tag, ap);
    va_end(ap);
    return ret;
}

/**
 * @brief Set LERC version error tolerance or secondary-compression controls.
 * @ingroup libtiff_private_lerc
 *
 * LERC_PARAMETERS consumes int count then int*; a count below two is
 * rejected, otherwise it caches version/backend and delegates custom-array
 * storage; no validation of those array values is done here. MAXZERROR
 * consumes double without range validation. VERSION and ADD_COMPRESSION
 * consume int, validate supported values, cache and update the two-entry
 * custom tag through the parent. ZSTD_LEVEL (ZSTD_SUPPORT builds only;
 * otherwise delegated to the parent setter) consumes int and stores
 * out-of-range values with a warning. ZIPQUALITY consumes int and stores it
 * before range rejection; on every accepted assignment it frees any cached
 * libdeflate compressor. Other tags delegate. State/custom-tag allocation
 * changes can survive failure; borrowed input arrays are consumed/copied
 * through the parent contract.
 *
 * @param[in,out] tif Non-NULL live LERC handle with valid current
 * geometry/state; serialise access.
 * @param[in] tag Codec pseudo-tag/custom tag or delegated identifier.
 * @param[in,out] ap Live tag-specific va_list consumed synchronously; not
 * retained.
 * @return One on accepted pseudo-tag assignment, zero on explicit
 * validation or parent storage failure, otherwise delegated status.
 */
static int LERCVSetField(TIFF *tif, uint32_t tag, va_list ap)
{
    static const char module[] = "LERCVSetField";
    LERCState *sp = GetLERCState(tif);

    switch (tag)
    {
        case TIFFTAG_LERC_PARAMETERS:
        {
            uint32_t count = (uint32_t)va_arg(ap, int);
            int *params = va_arg(ap, int *);
            if (count < 2)
            {
                TIFFErrorExtR(tif, module,
                              "Invalid count for LercParameters: %u", count);
                return 0;
            }
            sp->lerc_version = params[0];
            sp->additional_compression = params[1];
            return LERCVSetFieldBase(tif, TIFFTAG_LERC_PARAMETERS, count,
                                     params);
        }
        case TIFFTAG_LERC_MAXZERROR:
            sp->maxzerror = va_arg(ap, double);
            return 1;
        case TIFFTAG_LERC_VERSION:
        {
            int params[2] = {0, 0};
            int version = va_arg(ap, int);
            if (version != LERC_VERSION_2_4)
            {
                TIFFErrorExtR(tif, module, "Invalid value for LercVersion: %d",
                              version);
                return 0;
            }
            sp->lerc_version = version;
            params[0] = sp->lerc_version;
            params[1] = sp->additional_compression;
            return LERCVSetFieldBase(tif, TIFFTAG_LERC_PARAMETERS, 2, params);
        }
        case TIFFTAG_LERC_ADD_COMPRESSION:
        {
            int params[2] = {0, 0};
            int additional_compression = va_arg(ap, int);
#ifndef ZSTD_SUPPORT
            if (additional_compression == LERC_ADD_COMPRESSION_ZSTD)
            {
                TIFFErrorExtR(tif, module,
                              "LERC_ZSTD requested, but ZSTD not available");
                return 0;
            }
#endif
            if (additional_compression != LERC_ADD_COMPRESSION_NONE &&
                additional_compression != LERC_ADD_COMPRESSION_DEFLATE &&
                additional_compression != LERC_ADD_COMPRESSION_ZSTD)
            {
                TIFFErrorExtR(tif, module,
                              "Invalid value for LercAdditionalCompression: %d",
                              additional_compression);
                return 0;
            }
            sp->additional_compression = additional_compression;
            params[0] = sp->lerc_version;
            params[1] = sp->additional_compression;
            return LERCVSetFieldBase(tif, TIFFTAG_LERC_PARAMETERS, 2, params);
        }
#ifdef ZSTD_SUPPORT
        case TIFFTAG_ZSTD_LEVEL:
        {
            sp->zstd_compress_level = (int)va_arg(ap, int);
            if (sp->zstd_compress_level <= 0 ||
                sp->zstd_compress_level > ZSTD_maxCLevel())
            {
                TIFFWarningExtR(tif, module,
                                "ZSTD_LEVEL should be between 1 and %d",
                                ZSTD_maxCLevel());
            }
            return 1;
        }
#endif
        case TIFFTAG_ZIPQUALITY:
        {
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

#if LIBDEFLATE_SUPPORT
            if (sp->libdeflate_enc)
            {
                libdeflate_free_compressor(sp->libdeflate_enc);
                sp->libdeflate_enc = NULL;
            }
#endif

            return (1);
        }
        default:
            return (*sp->vsetparent)(tif, tag, ap);
    }
    /*NOTREACHED*/
}

/**
 * @brief Retrieve cached LERC controls or delegate another tag query.
 * @ingroup libtiff_private_lerc
 *
 * MAXZERROR consumes non-NULL double*; VERSION, ADD_COMPRESSION, ZSTD_LEVEL
 * and ZIPQUALITY consume non-NULL int*. ZSTD_LEVEL returns the cached level
 * even in builds without ZSTD_SUPPORT, where the setter delegates it.
 * LERC_PARAMETERS and other tags follow parent variadic contracts, including
 * borrowed custom-array lifetime. No output allocation or ownership transfer
 * for scalar pseudo-tags.
 *
 * @param[in,out] tif Non-NULL live LERC handle with valid current
 * geometry/state; serialise access.
 * @param[in] tag Codec pseudo-tag/custom tag or delegated identifier.
 * @param[in,out] ap Live tag-specific va_list consumed synchronously; not
 * retained.
 * @return One for scalar retrieval, otherwise parent getter status.
 */
static int LERCVGetField(TIFF *tif, uint32_t tag, va_list ap)
{
    LERCState *sp = GetLERCState(tif);

    switch (tag)
    {
        case TIFFTAG_LERC_MAXZERROR:
            *va_arg(ap, double *) = sp->maxzerror;
            break;
        case TIFFTAG_LERC_VERSION:
            *va_arg(ap, int *) = sp->lerc_version;
            break;
        case TIFFTAG_LERC_ADD_COMPRESSION:
            *va_arg(ap, int *) = sp->additional_compression;
            break;
        case TIFFTAG_ZSTD_LEVEL:
            *va_arg(ap, int *) = sp->zstd_compress_level;
            break;
        case TIFFTAG_ZIPQUALITY:
            *va_arg(ap, int *) = sp->zipquality;
            break;
        default:
            return (*sp->vgetparent)(tif, tag, ap);
    }
    return 1;
}

int TIFFInitLERC(TIFF *tif, int scheme)
{
    static const char module[] = "TIFFInitLERC";
    LERCState *sp;

    (void)scheme;
    assert(scheme == COMPRESSION_LERC);

    /*
     * Merge codec-specific tag information.
     */
    if (!_TIFFMergeFields(tif, LERCFields, TIFFArrayCount(LERCFields)))
    {
        TIFFErrorExtR(tif, module, "Merging LERC codec-specific tags failed");
        return 0;
    }

    /*
     * Allocate state block so tag methods have storage to record values.
     */
    tif->tif_data = (uint8_t *)_TIFFcallocExt(tif, 1, sizeof(LERCState));
    if (tif->tif_data == NULL)
        goto bad;
    sp = GetLERCState(tif);

    /*
     * Override parent get/set field methods.
     */
    sp->vgetparent = tif->tif_tagmethods.vgetfield;
    tif->tif_tagmethods.vgetfield = LERCVGetField; /* hook for codec tags */
    sp->vsetparent = tif->tif_tagmethods.vsetfield;
    tif->tif_tagmethods.vsetfield = LERCVSetField; /* hook for codec tags */

    /*
     * Install codec methods.
     */
    tif->tif_fixuptags = LERCFixupTags;
    tif->tif_setupdecode = LERCSetupDecode;
    tif->tif_predecode = LERCPreDecode;
    tif->tif_decoderow = LERCDecode;
    tif->tif_decodestrip = LERCDecode;
    tif->tif_decodetile = LERCDecode;
#ifndef LERC_READ_ONLY
    tif->tif_setupencode = LERCSetupEncode;
    tif->tif_preencode = LERCPreEncode;
    tif->tif_postencode = LERCPostEncode;
    tif->tif_encoderow = LERCEncode;
    tif->tif_encodestrip = LERCEncode;
    tif->tif_encodetile = LERCEncode;
#endif
    tif->tif_cleanup = LERCCleanup;

    /* LERC compression ratio can grow to several millions */
    /* eg. 5703725 for Lerc deflate on 16383x16383 array */
    /* or 3829644 for regular Lerc */
    /* See README_for_libtiff_developpers.md for raw data used to estimate
     * the maximum compression rate. */
    /* so we don't define tif->tif_getmaxcompressionratio */

    /* Default values for codec-specific fields */
    TIFFSetField(tif, TIFFTAG_LERC_VERSION, LERC_VERSION_2_4);
    TIFFSetField(tif, TIFFTAG_LERC_ADD_COMPRESSION, LERC_ADD_COMPRESSION_NONE);
    sp->maxzerror = 0.0;
    sp->zstd_compress_level = 9;            /* default comp. level */
    sp->zipquality = Z_DEFAULT_COMPRESSION; /* default comp. level */
    sp->state = 0;

    return 1;
bad:
    TIFFErrorExtR(tif, module, "No space for LERC state block");
    return 0;
}
#endif /* LERC_SUPPORT */

/**
 * @def LIBDEFLATE_MAX_COMPRESSION_LEVEL
 * @brief Highest accepted TIFFTAG_ZIPQUALITY value: 12.
 * @ingroup libtiff_private_lerc
 *
 * Matches the libdeflate maximum level; the zlib path caps the level at
 * Z_BEST_COMPRESSION (9). The tag setter rejects values outside [-1, 12].
 */

/**
 * @def LSTATE_INIT_DECODE
 * @brief Bit 0x01 of the codec state field: decoding has been set up.
 * @ingroup libtiff_private_lerc
 *
 * Set by LERCSetupDecode(). Each setup first tears down the other mode, if
 * active, and clears the field.
 */

/**
 * @def LSTATE_INIT_ENCODE
 * @brief Bit 0x02 of the codec state field: encoding has been set up.
 * @ingroup libtiff_private_lerc
 *
 * Set by LERCSetupEncode(). Each setup first tears down the other mode, if
 * active, and clears the field.
 */

/**
 * @def LERC_AT_LEAST_VERSION(maj, min, patch)
 * @brief Fallback used when the LERC header lacks LERC_AT_LEAST_VERSION.
 * @ingroup libtiff_private_lerc
 *
 * Always expands to 0, so code guarded by it treats the dependency as
 * older than any tested version. The operands are not evaluated.
 *
 * @param[in] maj Major version (unused).
 * @param[in] min Minor version (unused).
 * @param[in] patch Patch version (unused).
 */

/**
 * @def GetLERCState(tif)
 * @brief Cast tif->tif_data to the LERCState.
 * @ingroup libtiff_private_lerc
 *
 * tif is evaluated once. Requires the codec to be initialised (tif_data
 * non-NULL).
 *
 * @param[in] tif Handle whose codec state is returned.
 */

/**
 * @def LERCDecoderState(tif)
 * @brief Alias of GetLERCState(), returning the LERCState for decoding.
 * @ingroup libtiff_private_lerc
 *
 * tif is evaluated once. Requires the codec to be initialised.
 *
 * @param[in] tif Handle whose codec state is returned.
 */

/**
 * @def LERCEncoderState(tif)
 * @brief Alias of GetLERCState(), returning the LERCState for encoding.
 * @ingroup libtiff_private_lerc
 *
 * tif is evaluated once. Requires the codec to be initialised.
 *
 * @param[in] tif Handle whose codec state is returned.
 */

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
 * TIFF Library
 *
 * Builtin Compression Scheme Configuration Support.
 */
/**
 * @file tif_codec.c
 * @brief Define the built-in codec table and unconfigured-codec stubs.
 */

#include "tiffiop.h"

static int NotConfigured(TIFF *, int);

#ifndef LZW_SUPPORT
#define TIFFInitLZW NotConfigured
#endif
#ifndef PACKBITS_SUPPORT
#define TIFFInitPackBits NotConfigured
#endif
#ifndef THUNDER_SUPPORT
#define TIFFInitThunderScan NotConfigured
#endif
#ifndef NEXT_SUPPORT
#define TIFFInitNeXT NotConfigured
#endif
#ifndef JPEG_SUPPORT
#define TIFFInitJPEG NotConfigured
#endif
#ifndef OJPEG_SUPPORT
#define TIFFInitOJPEG NotConfigured
#endif
#ifndef CCITT_SUPPORT
#define TIFFInitCCITTRLE NotConfigured
#define TIFFInitCCITTRLEW NotConfigured
#define TIFFInitCCITTFax3 NotConfigured
#define TIFFInitCCITTFax4 NotConfigured
#endif
#ifndef JBIG_SUPPORT
#define TIFFInitJBIG NotConfigured
#endif
#ifndef ZIP_SUPPORT
#define TIFFInitZIP NotConfigured
#endif
#ifndef PIXARLOG_SUPPORT
#define TIFFInitPixarLog NotConfigured
#endif
#ifndef LOGLUV_SUPPORT
#define TIFFInitSGILog NotConfigured
#endif
#ifndef LERC_SUPPORT
#define TIFFInitLERC NotConfigured
#endif
#ifndef LZMA_SUPPORT
#define TIFFInitLZMA NotConfigured
#endif
#ifndef ZSTD_SUPPORT
#define TIFFInitZSTD NotConfigured
#endif
#ifndef WEBP_SUPPORT
#define TIFFInitWebP NotConfigured
#endif

/*
 * Compression schemes statically built into the library.
 */
const TIFFCodec _TIFFBuiltinCODECS[] = {
    {"None", COMPRESSION_NONE, TIFFInitDumpMode},
    {"LZW", COMPRESSION_LZW, TIFFInitLZW},
    {"PackBits", COMPRESSION_PACKBITS, TIFFInitPackBits},
    {"ThunderScan", COMPRESSION_THUNDERSCAN, TIFFInitThunderScan},
    {"NeXT", COMPRESSION_NEXT, TIFFInitNeXT},
    {"JPEG", COMPRESSION_JPEG, TIFFInitJPEG},
    {"Old-style JPEG", COMPRESSION_OJPEG, TIFFInitOJPEG},
    {"CCITT RLE", COMPRESSION_CCITTRLE, TIFFInitCCITTRLE},
    {"CCITT RLE/W", COMPRESSION_CCITTRLEW, TIFFInitCCITTRLEW},
    {"CCITT Group 3", COMPRESSION_CCITTFAX3, TIFFInitCCITTFax3},
    {"CCITT Group 4", COMPRESSION_CCITTFAX4, TIFFInitCCITTFax4},
    {"ISO JBIG", COMPRESSION_JBIG, TIFFInitJBIG},
    {"Deflate", COMPRESSION_DEFLATE, TIFFInitZIP},
    {"AdobeDeflate", COMPRESSION_ADOBE_DEFLATE, TIFFInitZIP},
    {"PixarLog", COMPRESSION_PIXARLOG, TIFFInitPixarLog},
    {"SGILog", COMPRESSION_SGILOG, TIFFInitSGILog},
    {"SGILog24", COMPRESSION_SGILOG24, TIFFInitSGILog},
    {"LZMA", COMPRESSION_LZMA, TIFFInitLZMA},
    {"ZSTD", COMPRESSION_ZSTD, TIFFInitZSTD},
    {"WEBP", COMPRESSION_WEBP, TIFFInitWebP},
    {"LERC", COMPRESSION_LERC, TIFFInitLERC},
    {NULL, 0, NULL}};

/**
 * @brief Report that the current compression support was not built.
 * @ingroup libtiff_private_codecs
 *
 * Looks up a display name and dispatches handle diagnostics; no allocation,
 * pixel I/O, or buffer ownership transfer.
 *
 * @param[in] tif Non-NULL live handle; td_compression and tif_name are read
 * for the diagnostic and codec state is unchanged.
 * @return Zero unconditionally.
 */
static int _notConfigured(TIFF *tif)
{
    const TIFFCodec *c = TIFFFindCODEC(tif->tif_dir.td_compression);
    char compression_code[20];

    snprintf(compression_code, sizeof(compression_code), "%" PRIu16,
             tif->tif_dir.td_compression);
    TIFFErrorExtR(tif, tif->tif_name,
                  "%s compression support is not configured",
                  c ? c->name : compression_code);
    return (0);
}

/**
 * @brief Install unavailable-codec setup methods while accepting metadata
 * access.
 * @ingroup libtiff_private_codecs
 *
 * Sets fixup/setup callbacks to _notConfigured and clears encode/decode
 * status. Does not allocate state, access payload, or diagnose until
 * callbacks are used.
 *
 * @param[in,out] tif Non-NULL live handle whose fixup/setup callbacks and
 * encode/decode status flags are replaced; serialise access externally.
 * @param[in] scheme Unused compression identifier.
 * @return One unconditionally.
 */
static int NotConfigured(TIFF *tif, int scheme)
{
    (void)scheme;

    tif->tif_fixuptags = _notConfigured;
    tif->tif_decodestatus = FALSE;
    tif->tif_setupdecode = _notConfigured;
    tif->tif_encodestatus = FALSE;
    tif->tif_setupencode = _notConfigured;
    return (1);
}

/************************************************************************/
/*                       TIFFIsCODECConfigured()                        */
/************************************************************************/

/* The public contract is documented with the declaration in tiffio.h. */

int TIFFIsCODECConfigured(uint16_t scheme)
{
    const TIFFCodec *codec = TIFFFindCODEC(scheme);

    if (codec == NULL)
    {
        return 0;
    }
    if (codec->init == NULL)
    {
        return 0;
    }
    if (codec->init != NotConfigured)
    {
        return 1;
    }
    return 0;
}

/**
 * @def TIFFInitLZW
 * @brief When LZW_SUPPORT is not defined, alias TIFFInitLZW to NotConfigured().
 * @ingroup libtiff_private_codecs
 */
/**
 * @def TIFFInitPackBits
 * @brief When PACKBITS_SUPPORT is not defined, alias TIFFInitPackBits to
 * NotConfigured().
 * @ingroup libtiff_private_codecs
 */
/**
 * @def TIFFInitThunderScan
 * @brief When THUNDER_SUPPORT is not defined, alias TIFFInitThunderScan to
 * NotConfigured().
 * @ingroup libtiff_private_codecs
 */
/**
 * @def TIFFInitNeXT
 * @brief When NEXT_SUPPORT is not defined, alias TIFFInitNeXT to
 * NotConfigured().
 * @ingroup libtiff_private_codecs
 */
/**
 * @def TIFFInitJPEG
 * @brief When JPEG_SUPPORT is not defined, alias TIFFInitJPEG to
 * NotConfigured().
 * @ingroup libtiff_private_codecs
 */
/**
 * @def TIFFInitOJPEG
 * @brief When OJPEG_SUPPORT is not defined, alias TIFFInitOJPEG to
 * NotConfigured().
 * @ingroup libtiff_private_codecs
 */
/**
 * @def TIFFInitCCITTRLE
 * @brief When CCITT_SUPPORT is not defined, alias TIFFInitCCITTRLE to
 * NotConfigured().
 * @ingroup libtiff_private_codecs
 */
/**
 * @def TIFFInitCCITTRLEW
 * @brief When CCITT_SUPPORT is not defined, alias TIFFInitCCITTRLEW to
 * NotConfigured().
 * @ingroup libtiff_private_codecs
 */
/**
 * @def TIFFInitCCITTFax3
 * @brief When CCITT_SUPPORT is not defined, alias TIFFInitCCITTFax3 to
 * NotConfigured().
 * @ingroup libtiff_private_codecs
 */
/**
 * @def TIFFInitCCITTFax4
 * @brief When CCITT_SUPPORT is not defined, alias TIFFInitCCITTFax4 to
 * NotConfigured().
 * @ingroup libtiff_private_codecs
 */
/**
 * @def TIFFInitJBIG
 * @brief When JBIG_SUPPORT is not defined, alias TIFFInitJBIG to
 * NotConfigured().
 * @ingroup libtiff_private_codecs
 */
/**
 * @def TIFFInitZIP
 * @brief When ZIP_SUPPORT is not defined, alias TIFFInitZIP to NotConfigured().
 * @ingroup libtiff_private_codecs
 */
/**
 * @def TIFFInitPixarLog
 * @brief When PIXARLOG_SUPPORT is not defined, alias TIFFInitPixarLog to
 * NotConfigured().
 * @ingroup libtiff_private_codecs
 */
/**
 * @def TIFFInitSGILog
 * @brief When LOGLUV_SUPPORT is not defined, alias TIFFInitSGILog to
 * NotConfigured().
 * @ingroup libtiff_private_codecs
 */
/**
 * @def TIFFInitLERC
 * @brief When LERC_SUPPORT is not defined, alias TIFFInitLERC to
 * NotConfigured().
 * @ingroup libtiff_private_codecs
 */
/**
 * @def TIFFInitLZMA
 * @brief When LZMA_SUPPORT is not defined, alias TIFFInitLZMA to
 * NotConfigured().
 * @ingroup libtiff_private_codecs
 */
/**
 * @def TIFFInitZSTD
 * @brief When ZSTD_SUPPORT is not defined, alias TIFFInitZSTD to
 * NotConfigured().
 * @ingroup libtiff_private_codecs
 */
/**
 * @def TIFFInitWebP
 * @brief When WEBP_SUPPORT is not defined, alias TIFFInitWebP to
 * NotConfigured().
 * @ingroup libtiff_private_codecs
 */

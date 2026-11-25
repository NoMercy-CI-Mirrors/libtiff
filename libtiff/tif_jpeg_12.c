
/**
 * @file tif_jpeg_12.c
 * @brief Reuse the JPEG bridge with the twelve-bit dependency ABI.
 * @ingroup libtiff_private_jpeg
 */

#include "tiffiop.h"

#if defined(HAVE_JPEGTURBO_DUAL_MODE_8_12)
/**
 * @brief Enable the dual-precision bridge when configured.
 * @ingroup libtiff_private_jpeg
 *
 * Affects this source translation only; does not allocate, do I/O or
 * transfer ownership.
 */
#define JPEG_DUAL_MODE_8_12
#endif

#if defined(JPEG_DUAL_MODE_8_12)

/**
 * @brief Select twelve-bit types in the included common implementation.
 * @ingroup libtiff_private_jpeg
 *
 * Affects this source translation only; does not allocate, do I/O or
 * transfer ownership.
 */
#define FROM_TIF_JPEG_12

#ifdef TIFFInitJPEG
#undef TIFFInitJPEG
#endif
/**
 * @brief Rename the shared initialiser to TIFFInitJPEG_12.
 * @ingroup libtiff_private_jpeg
 *
 * Affects this source translation only; does not allocate, do I/O or
 * transfer ownership.
 */
#define TIFFInitJPEG TIFFInitJPEG_12

#ifdef TIFFJPEGIsFullStripRequired
#undef TIFFJPEGIsFullStripRequired
#endif
/**
 * @brief Rename the shared whole-strile probe to its twelve-bit counterpart.
 * @ingroup libtiff_private_jpeg
 *
 * Affects this source translation only; does not allocate, do I/O or
 * transfer ownership.
 */
#define TIFFJPEGIsFullStripRequired TIFFJPEGIsFullStripRequired_12

/**
 * @brief Twelve-bit copy of TIFFInitJPEG() produced by the rename above.
 * @ingroup libtiff_private_jpeg
 *
 * Emitted as a by-product of compiling tif_jpeg.c under the twelve-bit ABI,
 * where the shared TIFFInitJPEG definition is renamed so the symbols do not
 * clash. It is not registered in the codec table and not called by the
 * library; twelve-bit switching uses TIFFReInitJPEG_12() instead. If called,
 * it would behave as TIFFInitJPEG() with twelve-bit dependency types.
 * Compiled only when the dual-mode bridge is enabled.
 *
 * @param[in,out] tif Borrowed live handle whose codec-owned state and
 * methods are installed.
 * @param[in] scheme COMPRESSION_JPEG.
 * @return One after installation, zero on detected field/allocation
 * failure.
 */
int TIFFInitJPEG_12(TIFF *tif, int scheme);

#if !defined(HAVE_JPEGTURBO_DUAL_MODE_8_12)
#include LIBJPEG_12_PATH
#endif

#include "tif_jpeg.c"

int TIFFReInitJPEG_12(TIFF *tif, const JPEGOtherSettings *otherSettings,
                      int scheme, int is_encode)
{
    JPEGState *sp;
    uint8_t *new_tif_data;

    (void)scheme;
    assert(scheme == COMPRESSION_JPEG);

    new_tif_data =
        (uint8_t *)_TIFFreallocExt(tif, tif->tif_data, sizeof(JPEGState));

    if (new_tif_data == NULL)
    {
        TIFFErrorExtR(tif, "TIFFReInitJPEG_12",
                      "No space for JPEG state block");
        return 0;
    }

    tif->tif_data = new_tif_data;
    _TIFFmemset(tif->tif_data, 0, sizeof(JPEGState));

    TIFFInitJPEGCommon(tif);

    sp = JState(tif);
    sp->otherSettings = *otherSettings;

    if (is_encode)
        return JPEGSetupEncode(tif);
    else
        return JPEGSetupDecode(tif);
}

#endif /* defined(JPEG_DUAL_MODE_8_12) */

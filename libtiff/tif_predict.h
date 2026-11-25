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
 * @file tif_predict.h
 * @brief Define the predictor prefix and method-chain contracts.
 */

#ifndef _TIFFPREDICT_
#define _TIFFPREDICT_

#include "tiffio.h"
#include "tiffiop.h"

/*
 * ``Library-private'' Support for the Predictor Tag
 */

/**
 * @brief Transform one decoded row in place for a Predictor wrapper.
 * @ingroup libtiff_private_predictor
 *
 * Called synchronously with a selected integer or floating-point transform.
 * Borrows storage and handle; no returned ownership. Integer transforms
 * generally diagnose divisibility errors without allocation; floating
 * transforms allocate temporary storage. Buffer can change before failure;
 * callers must honour the transform-specific alignment/extent requirements.
 *
 * @param[in,out] tif Non-NULL live handle whose tif_data begins with a
 * valid TIFFPredictorState; serialise codec operations.
 * @param[in,out] buf Borrowed writable complete row in the
 * transform-specific byte/sample order.
 * @param[in] size Positive complete row byte count satisfying
 * stride/sample-size divisibility.
 * @return One on transformation, zero on detected extent/allocation
 * failure.
 */
typedef int (*TIFFEncodeDecodeMethod)(TIFF *tif, uint8_t *buf, tmsize_t size);

/*
 * Codecs that want to support the Predictor tag must place
 * this structure first in their private state block so that
 * the predictor code can cast tif_data to find its state.
 */
/**
 * @brief Prefix codec state with Predictor method chains and row geometry.
 * @ingroup libtiff_private_predictor
 *
 * Must be the first member of the codec-owned tif_data allocation.
 * Initialisation borrows saved callbacks; keep state and callback code live
 * until predictor and outer codec cleanup finish. PredictorCleanup does not
 * free the prefix or restore every transfer/postdecode callback.
 */
typedef struct
{
    /** @brief Predictor tag value; validated at setup. */
    int predictor;
    /**
     * @brief Positive channel stride in samples, not bytes, for active
     * transforms.
     */
    tmsize_t stride;
    /** @brief Positive decoded row byte extent for active strile transforms. */
    tmsize_t rowsize;

    /** @brief Saved parent row encoder. */
    TIFFCodeMethod encoderow;
    /**
     * @brief Saved parent strip encoder; shared strile wrapper dispatch
     * currently uses encodetile.
     */
    TIFFCodeMethod encodestrip;
    /** @brief Saved parent strile encoder invoked by the shared wrapper. */
    TIFFCodeMethod encodetile;
    /** @brief Selected in-place row differencer, NULL before setup. */
    TIFFEncodeDecodeMethod encodepfunc;

    /** @brief Saved parent row decoder. */
    TIFFCodeMethod decoderow;
    /**
     * @brief Saved parent strip decoder; shared strile wrapper dispatch
     * currently uses decodetile.
     */
    TIFFCodeMethod decodestrip;
    /** @brief Saved parent strile decoder invoked by the shared wrapper. */
    TIFFCodeMethod decodetile;
    /** @brief Selected in-place row accumulator, NULL before setup. */
    TIFFEncodeDecodeMethod decodepfunc;

    /** @brief Saved borrowed tag getter. */
    TIFFVGetMethod vgetparent;
    /** @brief Saved borrowed tag setter. */
    TIFFVSetMethod vsetparent;
    /** @brief Saved optional borrowed directory printer. */
    TIFFPrintMethod printdir;
    /** @brief Saved borrowed decoder setup method. */
    TIFFBoolMethod setupdecode;
    /** @brief Saved borrowed encoder setup method. */
    TIFFBoolMethod setupencode;
} TIFFPredictorState;

#if defined(__cplusplus)
extern "C"
{
#endif
    /**
     * @brief Install Predictor tag and setup hooks around a codec.
     * @ingroup libtiff_private_predictor
     *
     * Requires predictor state first in an existing codec-owned tif_data block
     * and initialised parent methods. Merges field metadata, saves the parent
     * tag get/set, print and setup-decode/encode methods and installs
     * predictor wrappers for them, sets the predictor to one and clears the
     * transform functions. The row/strip/tile decode and encode wrappers are
     * installed later by the setup wrappers, only when a predictor is active.
     * Must not be called repeatedly on active chains. Field merge failure
     * is diagnosed; allocations/state changes are not transactional. Does not
     * allocate the outer codec state or perform pixel I/O.
     *
     * @param[in,out] tif Non-NULL live handle whose tif_data begins with a
     * valid TIFFPredictorState; serialise codec operations.
     * @return One after installation, zero on field-merge failure.
     */
    extern int TIFFPredictorInit(TIFF *tif);
    /**
     * @brief Restore saved Predictor tag print and setup hooks.
     * @ingroup libtiff_private_predictor
     *
     * Requires initialised predictor state and still-valid saved callbacks.
     * Restores tag get/set, printer and setup hooks; does not restore wrapped
     * row/strile methods, postdecode swapping, or remove field definitions.
     * Does not free tif_data, flush, allocate, or perform pixel I/O. The outer
     * codec must complete teardown before state is released.
     *
     * @param[in,out] tif Non-NULL live handle whose tif_data begins with a
     * valid TIFFPredictorState; serialise codec operations.
     * @return One unconditionally.
     */
    extern int TIFFPredictorCleanup(TIFF *tif);
#if defined(__cplusplus)
}
#endif
#endif /* _TIFFPREDICT_ */

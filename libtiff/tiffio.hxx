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
 * @file tiffio.hxx
 * @brief Declare TIFF adapters for borrowed C++ input and output streams.
 */

#ifndef _TIFFIO_HXX_
#define _TIFFIO_HXX_

/*
 * TIFF I/O library definitions which provide C++ streams API.
 */

#include "tiff.h"
#include "tiffio.h"
#include <iostream>

/**
 * @brief Open a TIFF writer over a caller-owned C++ stream.
 * @ingroup libtiff_public_streams
 *
 * Creates a library-owned adapter around a borrowed seekable stream. The
 * stream must outlive the TIFF handle and any explicit adapter-release call.
 * Use a binary stream positioned at zero for supported operation; nonzero-
 * origin adapter limitations are described in @ref lifecycle_limits.
 *
 * Opens in wm mode, initialising a classic TIFF header at the stream origin.
 * Output callbacks cannot read; operations requiring readback are
 * unsupported. For an initially empty string stream with a failing tellp(),
 * the wrapper may write a dummy null byte and seek to zero before opening.
 * Opening and later operations can change bytes, position, and stream
 * status. TIFFClose() deletes the adapter but neither closes the stream nor
 * explicitly calls ostream::flush(); flush or close the caller-owned stream
 * as needed after TIFFFlush()/TIFFClose().
 *
 * A normal NULL return releases the adapter without destroying the stream,
 * but does not roll back stream bytes, position, or flags. Library
 * diagnostics report TIFF errors. C++ allocation/stream exceptions
 * propagate; this implementation supplies no exception cleanup/rollback
 * guarantee. Use streams with exceptions disabled when relying on status-
 * based failure handling. TIFFCleanup() alone does not invoke the adapter
 * close callback; save and invoke it explicitly if bypassing TIFFClose().
 *
 * @param[in] name Non-NULL diagnostic name; copied into the handle.
 * @param[in,out] os Non-NULL caller-owned, seekable binary output stream.
 * @return An owned TIFF handle for TIFFClose(), or NULL on an ordinary
 * library failure. Exceptions are not converted to NULL.
 */
extern TIFF *TIFFStreamOpen(const char *name, std::ostream *os);
/**
 * @brief Open a TIFF reader over a caller-owned C++ stream.
 * @ingroup libtiff_public_streams
 *
 * Creates a library-owned adapter around a borrowed seekable stream. The
 * stream must outlive the TIFF handle and any explicit adapter-release call.
 * Use a binary stream positioned at zero for supported operation; nonzero-
 * origin adapter limitations are described in @ref lifecycle_limits.
 *
 * Opens in rm mode and reads the first TIFF directory. Mapping is disabled,
 * and the write callback does not write. Reads and seeks change position and
 * stream status. TIFFClose() deletes the adapter but leaves the stream open.
 *
 * A normal NULL return releases the adapter without destroying the stream,
 * but does not restore the stream position or flags. Library
 * diagnostics report TIFF errors. C++ allocation/stream exceptions
 * propagate; this implementation supplies no exception cleanup/rollback
 * guarantee. Use streams with exceptions disabled when relying on status-
 * based failure handling. TIFFCleanup() alone does not invoke the adapter
 * close callback; save and invoke it explicitly if bypassing TIFFClose().
 *
 * @param[in] name Non-NULL diagnostic name; copied into the handle.
 * @param[in,out] is Non-NULL caller-owned, seekable binary input stream.
 * @return An owned TIFF handle for TIFFClose(), or NULL on an ordinary
 * library failure. Exceptions are not converted to NULL.
 */
extern TIFF *TIFFStreamOpen(const char *name, std::istream *is);

#endif /* _TIFFIO_HXX_ */

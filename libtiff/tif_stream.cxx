/*
 * Copyright (c) 1988-1996 Sam Leffler
 * Copyright (c) 1991-1996 Silicon Graphics, Inc.
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
 * TIFF Library UNIX-specific Routines.
 */
/**
 * @file tif_stream.cxx
 * @brief Adapt borrowed C++ streams to the TIFF client-I/O interface.
 *
 * Allocation and stream exceptions are not caught by this adapter.
 */

#include "tiffiop.h"
#include <iostream>

using namespace std;

/*
  ISO C++ uses a 'std::streamsize' type to define counts.  This makes
  it similar to, (but perhaps not the same as) size_t.

  The std::ios::pos_type is used to represent stream positions as used
  by tellg(), tellp(), seekg(), and seekp().  This makes it similar to
  (but perhaps not the same as) 'off_t'.  The std::ios::streampos type
  is used for character streams, but is documented to not be an
  integral type anymore, so it should *not* be assigned to an integral
  type.

  The std::ios::off_type is used to specify relative offsets needed by
  the variants of seekg() and seekp() which accept a relative offset
  argument.

  Useful prototype knowledge:

  Obtain read position
    ios::pos_type basic_istream::tellg()

  Set read position
    basic_istream& basic_istream::seekg(ios::pos_type)
    basic_istream& basic_istream::seekg(ios::off_type, ios_base::seekdir)

  Read data
    basic_istream& istream::read(char *str, streamsize count)

  Number of characters read in last unformatted read
    streamsize istream::gcount();

  Obtain write position
    ios::pos_type basic_ostream::tellp()

  Set write position
    basic_ostream& basic_ostream::seekp(ios::pos_type)
    basic_ostream& basic_ostream::seekp(ios::off_type, ios_base::seekdir)

  Write data
    basic_ostream& ostream::write(const char *str, streamsize count)
*/

struct tiffis_data;
struct tiffos_data;

extern "C"
{

    /**
     * @brief Refuse reads on an output-stream adapter.
     * @ingroup libtiff_private_streams
     *
     * Performs no I/O, allocation, release, or diagnostic; an output adapter
     * cannot read.
     *
     * @param[in] fd Unused client context.
     * @param[in] buf Unused caller buffer; not written.
     * @param[in] size Unused requested byte count.
     * @return Zero for every request.
     */
    static tmsize_t _tiffosReadProc(thandle_t fd, void *buf, tmsize_t size);
    /**
     * @brief Read bytes from the borrowed input stream.
     * @ingroup libtiff_private_streams
     *
     * Reads at the current get position, which advances by the transferred
     * bytes. Stream fail/eof state may change, and a failed/short read may
     * modify part of buf. Does not emit a library diagnostic or take buffer
     * ownership. Stream exceptions propagate without cleanup or status
     * conversion.
     *
     * @param[in,out] fd Live tiffis_data context.
     * @param[out] buf Destination with at least size writable bytes.
     * @param[in] size Requested nonnegative byte count.
     * @return gcount() after the read, or -1 if conversion to streamsize
     * fails.
     */
    static tmsize_t _tiffisReadProc(thandle_t fd, void *buf, tmsize_t size);
    /**
     * @brief Write bytes to the borrowed output stream.
     * @ingroup libtiff_private_streams
     *
     * Writes at the current put position without explicitly flushing the
     * stream. Failure changes stream state and may follow partial output;
     * failed tellp() values are not independently checked here, so the
     * difference is not a reliable general failure sentinel. Buffer
     * ownership is unchanged. Stream exceptions propagate.
     *
     * @param[in,out] fd Live tiffos_data context.
     * @param[in] buf Source with at least size readable bytes.
     * @param[in] size Requested nonnegative byte count.
     * @return The post-write tellp() minus its pre-write value, cast to
     * tmsize_t; -1 if the request cannot be represented by streamsize.
     */
    static tmsize_t _tiffosWriteProc(thandle_t fd, void *buf, tmsize_t size);
    /**
     * @brief Refuse writes on an input-stream adapter.
     * @ingroup libtiff_private_streams
     *
     * Performs no I/O, allocation, release, or diagnostic; an input adapter
     * cannot write.
     *
     * @param[in] fd Unused client context.
     * @param[in] buf Unused caller buffer; not read.
     * @param[in] size Unused requested byte count.
     * @return Zero for every request.
     */
    static tmsize_t _tiffisWriteProc(thandle_t fd, void *buf, tmsize_t size);
    /**
     * @brief Seek the borrowed output stream, with a zero-fill extension
     * fallback.
     * @ingroup libtiff_private_streams
     *
     * SEEK_SET adds start_pos. If the seek fails, clears failbit and, when
     * the target lies beyond start_pos, extends the stream with null bytes
     * up to the target and seeks again. This can change bytes, position, and
     * flags even on failure. An unrecognised whence performs no seek. Final
     * tellp() is not normalised to the TIFF origin; nonzero-origin output is
     * not reliably supported. No library diagnostic or allocation ownership
     * transfer occurs; exceptions propagate.
     *
     * @param[in,out] fd Live tiffos_data context.
     * @param[in] off Unsigned byte offset interpreted by whence.
     * @param[in] whence SEEK_SET, SEEK_CUR, or SEEK_END; any other value
     * leaves the position unchanged.
     * @return The absolute underlying tellp() cast to uint64_t, or
     * UINT64_MAX for an already-failed stream, a rejected offset conversion,
     * or a stream that is still failed after the fallback.
     */
    static uint64_t _tiffosSeekProc(thandle_t fd, uint64_t off, int whence);
    /**
     * @brief Seek the borrowed input stream relative to its recorded TIFF
     * origin.
     * @ingroup libtiff_private_streams
     *
     * SEEK_SET adds start_pos; other origins use stream-relative seeking.
     * Does not clear an existing fail state, restore position on failure,
     * allocate, or emit a library diagnostic. Input stream state can change
     * and exceptions propagate.
     *
     * @param[in,out] fd Live tiffis_data context.
     * @param[in] off Unsigned byte offset interpreted by whence.
     * @param[in] whence SEEK_SET, SEEK_CUR, or SEEK_END; any other value
     * performs no seek and returns the current relative position.
     * @return The resulting tellg() minus start_pos, or UINT64_MAX if the
     * start position could not be recorded, an offset conversion fails,
     * tellg() fails, or the position precedes start_pos.
     */
    static uint64_t _tiffisSeekProc(thandle_t fd, uint64_t off, int whence);
    /**
     * @brief Query the absolute end position of the borrowed stream.
     * @ingroup libtiff_private_streams
     *
     * Saves the put position, seeks to the end, then restores the saved
     * position. If the initial tellp() fails, returns zero without seeking. If
     * the end seek or end query fails, temporarily clears failbit to restore
     * the position, then reinstates the failed stream state; restoration
     * itself is not guaranteed. The size does not subtract start_pos. Does not
     * release or explicitly flush the stream, and emits no library
     * diagnostic. Stream exceptions propagate.
     *
     * @param[in,out] fd Live tiffos_data context.
     * @return The absolute end position as uint64_t, or zero if the
     * initial/end position query or end seek fails.
     */
    static uint64_t _tiffosSizeProc(thandle_t fd);
    /**
     * @brief Query the absolute end position of the borrowed stream.
     * @ingroup libtiff_private_streams
     *
     * Saves the get position, seeks to the end, then restores the saved
     * position. If the initial tellg() fails, returns zero without seeking. If
     * the end seek or end query fails, temporarily clears failbit to restore
     * the position, then reinstates the failed stream state; restoration
     * itself is not guaranteed. The size does not subtract start_pos. Does not
     * release or explicitly flush the stream, and emits no library
     * diagnostic. Stream exceptions propagate.
     *
     * @param[in,out] fd Live tiffis_data context.
     * @return The absolute end position as uint64_t, or zero if the
     * initial/end position query or end seek fails.
     */
    static uint64_t _tiffisSizeProc(thandle_t fd);
    /**
     * @brief Delete a stream adapter without closing the borrowed stream.
     * @ingroup libtiff_private_streams
     *
     * Deletes only the adapter allocated by _tiffStreamOpen(). Does not
     * close, flush, or delete the caller-owned stream and does not emit a
     * diagnostic.
     *
     * @param[in] fd Live tiffos_data allocation; invalid after return.
     * @return Zero.
     */
    static int _tiffosCloseProc(thandle_t fd);
    /**
     * @brief Delete a stream adapter without closing the borrowed stream.
     * @ingroup libtiff_private_streams
     *
     * Deletes only the adapter allocated by _tiffStreamOpen(). Does not
     * close, flush, or delete the caller-owned stream and does not emit a
     * diagnostic.
     *
     * @param[in] fd Live tiffis_data allocation; invalid after return.
     * @return Zero.
     */
    static int _tiffisCloseProc(thandle_t fd);
    /**
     * @brief Decline mapping for the C++ stream adapter.
     * @ingroup libtiff_private_streams
     *
     * Performs no I/O, allocation, or diagnostic; outputs must not be used
     * as a view.
     *
     * @param[in] fd Unused client context.
     * @param[in] base Unused view output; not written.
     * @param[in] size Unused length output; not written.
     * @return Zero, indicating mapping is unavailable.
     */
    static int _tiffDummyMapProcCxx(thandle_t fd, void **base, toff_t *size);
    /**
     * @brief Provide a no-op unmapper for the C++ stream adapter.
     * @ingroup libtiff_private_streams
     *
     * Does not release storage, perform I/O, or emit diagnostics. Returns no
     * status; the stream mapper never supplies a view.
     *
     * @param[in] fd Unused client context.
     * @param[in] base Unused view address.
     * @param[in] size Unused view byte length.
     */
    static void _tiffDummyUnmapProcCxx(thandle_t fd, void *base, toff_t size);
    /**
     * @brief Allocate a stream context and attach it through TIFFClientOpen().
     * @ingroup libtiff_private_streams
     *
     * Allocates the appropriate tiffos_data/tiffis_data object, records
     * tellp()/tellg(), and installs its callbacks. Successful handles
     * release the adapter through the client close callback. A normal NULL
     * return deletes the adapter; the borrowed stream remains alive but may
     * have changed bytes, position, or flags. No exception cleanup guard
     * protects an adapter already allocated before a throwing stream/library
     * callback.
     *
     * @param[in] name Non-NULL copied diagnostic name.
     * @param[in] mode Internal mode string; presence of w selects output,
     * otherwise input.
     * @param[in,out] fd Borrowed ostream or istream pointer matching mode.
     * @return An owned TIFF handle, or NULL on ordinary client-open failure.
     * C++ exceptions propagate.
     */
    static TIFF *_tiffStreamOpen(const char *name, const char *mode, void *fd);

    /**
     * @brief Track a borrowed input stream and its initial position.
     * @ingroup libtiff_private_streams
     *
     * Allocated with new by _tiffStreamOpen() and deleted by the matching
     * close callback, or by the normal NULL-return opening failure path.
     * The stream itself remains caller-owned; exceptions have no cleanup guard.
     */
    struct tiffis_data
    {
        istream
            *stream; /**< @brief Borrowed stream that outlives this adapter. */
        ios::pos_type start_pos; /**< @brief Initial tell position, possibly the
                                    failure value -1. */
    };

    /**
     * @brief Track a borrowed output stream and its initial position.
     * @ingroup libtiff_private_streams
     *
     * Allocated with new by _tiffStreamOpen() and deleted by the matching
     * close callback, or by the normal NULL-return opening failure path.
     * The stream itself remains caller-owned; exceptions have no cleanup guard.
     */
    struct tiffos_data
    {
        ostream
            *stream; /**< @brief Borrowed stream that outlives this adapter. */
        ios::pos_type start_pos; /**< @brief Initial tell position, possibly the
                                    failure value -1. */
    };

    static tmsize_t _tiffosReadProc(thandle_t, void *, tmsize_t) { return 0; }

    static tmsize_t _tiffisReadProc(thandle_t fd, void *buf, tmsize_t size)
    {
        tiffis_data *data = reinterpret_cast<tiffis_data *>(fd);

        // Verify that type does not overflow.
        streamsize request_size = size;
        if (static_cast<tmsize_t>(request_size) != size)
            return static_cast<tmsize_t>(-1);

        data->stream->read((char *)buf, request_size);

        return static_cast<tmsize_t>(data->stream->gcount());
    }

    static tmsize_t _tiffosWriteProc(thandle_t fd, void *buf, tmsize_t size)
    {
        tiffos_data *data = reinterpret_cast<tiffos_data *>(fd);
        ostream *os = data->stream;
        ios::pos_type pos = os->tellp();

        // Verify that type does not overflow.
        streamsize request_size = size;
        if (static_cast<tmsize_t>(request_size) != size)
            return static_cast<tmsize_t>(-1);

        os->write(reinterpret_cast<const char *>(buf), request_size);

        return static_cast<tmsize_t>(os->tellp() - pos);
    }

    static tmsize_t _tiffisWriteProc(thandle_t, void *, tmsize_t) { return 0; }

    static uint64_t _tiffosSeekProc(thandle_t fd, uint64_t off, int whence)
    {
        tiffos_data *data = reinterpret_cast<tiffos_data *>(fd);
        ostream *os = data->stream;

        // if the stream has already failed, don't do anything
        if (os->fail())
            return static_cast<uint64_t>(-1);

        switch (whence)
        {
            case SEEK_SET:
            {
                // Compute 64-bit offset
                uint64_t new_offset =
                    static_cast<uint64_t>(data->start_pos) + off;

                // Verify that value does not overflow
                ios::off_type offset = static_cast<ios::off_type>(new_offset);
                if (static_cast<uint64_t>(offset) != new_offset)
                    return static_cast<uint64_t>(-1);

                os->seekp(offset, ios::beg);
                break;
            }
            case SEEK_CUR:
            {
                // Verify that value does not overflow
                ios::off_type offset = static_cast<ios::off_type>(off);
                if (static_cast<uint64_t>(offset) != off)
                    return static_cast<uint64_t>(-1);

                os->seekp(offset, ios::cur);
                break;
            }
            case SEEK_END:
            {
                // Verify that value does not overflow
                ios::off_type offset = static_cast<ios::off_type>(off);
                if (static_cast<uint64_t>(offset) != off)
                    return static_cast<uint64_t>(-1);

                os->seekp(offset, ios::end);
                break;
            }
        }

        // Attempt to workaround problems with seeking past the end of the
        // stream.  ofstream doesn't have a problem with this but
        // ostrstream/ostringstream does. In that situation, add intermediate
        // '\0' characters.
        if (os->fail())
        {
            ios::iostate old_state;
            ios::pos_type origin;

            old_state = os->rdstate();
            // reset the fail bit or else tellp() won't work below
            os->clear(os->rdstate() & ~ios::failbit);
            switch (whence)
            {
                case SEEK_SET:
                default:
                    origin = data->start_pos;
                    break;
                case SEEK_CUR:
                    origin = os->tellp();
                    break;
                case SEEK_END:
                    os->seekp(0, ios::end);
                    origin = os->tellp();
                    break;
            }
            // restore original stream state
            os->clear(old_state);

            // only do something if desired seek position is valid
            if ((static_cast<uint64_t>(origin) + off) >
                static_cast<uint64_t>(data->start_pos))
            {
                uint64_t num_fill;

                // clear the fail bit
                os->clear(os->rdstate() & ~ios::failbit);

                // extend the stream to the expected size
                os->seekp(0, ios::end);
                num_fill = (static_cast<uint64_t>(origin)) + off - os->tellp();
                for (uint64_t i = 0; i < num_fill; i++)
                    os->put('\0');

                // retry the seek
                os->seekp(static_cast<ios::off_type>(
                              static_cast<uint64_t>(origin) + off),
                          ios::beg);
            }
        }

        return static_cast<uint64_t>(os->tellp());
    }

    static uint64_t _tiffisSeekProc(thandle_t fd, uint64_t off, int whence)
    {
        tiffis_data *data = reinterpret_cast<tiffis_data *>(fd);

        if (data->start_pos == ios::pos_type(-1))
            return static_cast<uint64_t>(-1);

        switch (whence)
        {
            case SEEK_SET:
            {
                // Compute 64-bit offset
                uint64_t new_offset =
                    static_cast<uint64_t>(data->start_pos) + off;

                // Verify that value does not overflow
                ios::off_type offset = static_cast<ios::off_type>(new_offset);
                if (static_cast<uint64_t>(offset) != new_offset)
                    return static_cast<uint64_t>(-1);

                data->stream->seekg(offset, ios::beg);
                break;
            }
            case SEEK_CUR:
            {
                // Verify that value does not overflow
                ios::off_type offset = static_cast<ios::off_type>(off);
                if (static_cast<uint64_t>(offset) != off)
                    return static_cast<uint64_t>(-1);

                data->stream->seekg(offset, ios::cur);
                break;
            }
            case SEEK_END:
            {
                // Verify that value does not overflow
                ios::off_type offset = static_cast<ios::off_type>(off);
                if (static_cast<uint64_t>(offset) != off)
                    return static_cast<uint64_t>(-1);

                data->stream->seekg(offset, ios::end);
                break;
            }
        }

        ios::pos_type pos = data->stream->tellg();
        if (pos == ios::pos_type(-1))
            return static_cast<uint64_t>(-1);

        ios::off_type relative_pos = pos - data->start_pos;
        if (relative_pos < 0)
            return static_cast<uint64_t>(-1);

        return static_cast<uint64_t>(relative_pos);
    }

    static uint64_t _tiffosSizeProc(thandle_t fd)
    {
        tiffos_data *data = reinterpret_cast<tiffos_data *>(fd);
        ostream *os = data->stream;
        ios::pos_type pos = os->tellp();
        ios::pos_type len;

        // Failed stream positions must not be converted to unsigned sizes.
        if (pos == ios::pos_type(-1))
            return 0;

        os->seekp(0, ios::end);
        if (os->fail())
        {
            ios::iostate old_state = os->rdstate();
            // Temporarily clear failbit to restore the original position.
            os->clear(os->rdstate() & ~ios::failbit);
            os->seekp(pos);
            os->clear(old_state);
            return 0;
        }
        len = os->tellp();
        if (len == ios::pos_type(-1))
        {
            ios::iostate old_state = os->rdstate();
            // Temporarily clear failbit to restore the original position.
            os->clear(os->rdstate() & ~ios::failbit);
            os->seekp(pos);
            os->clear(old_state);
            return 0;
        }
        os->seekp(pos);

        return static_cast<uint64_t>(len);
    }

    static uint64_t _tiffisSizeProc(thandle_t fd)
    {
        tiffis_data *data = reinterpret_cast<tiffis_data *>(fd);
        ios::pos_type pos = data->stream->tellg();
        ios::pos_type len;

        // Failed stream positions must not be converted to unsigned sizes.
        if (pos == ios::pos_type(-1))
            return 0;

        data->stream->seekg(0, ios::end);
        if (data->stream->fail())
        {
            ios::iostate old_state = data->stream->rdstate();
            // Temporarily clear failbit to restore the original position.
            data->stream->clear(data->stream->rdstate() & ~ios::failbit);
            data->stream->seekg(pos);
            data->stream->clear(old_state);
            return 0;
        }
        len = data->stream->tellg();
        if (len == ios::pos_type(-1))
        {
            ios::iostate old_state = data->stream->rdstate();
            // Temporarily clear failbit to restore the original position.
            data->stream->clear(data->stream->rdstate() & ~ios::failbit);
            data->stream->seekg(pos);
            data->stream->clear(old_state);
            return 0;
        }
        data->stream->seekg(pos);

        return static_cast<uint64_t>(len);
    }

    static int _tiffosCloseProc(thandle_t fd)
    {
        // Our stream was not allocated by us, so it shouldn't be closed by us.
        delete reinterpret_cast<tiffos_data *>(fd);
        return 0;
    }

    static int _tiffisCloseProc(thandle_t fd)
    {
        // Our stream was not allocated by us, so it shouldn't be closed by us.
        delete reinterpret_cast<tiffis_data *>(fd);
        return 0;
    }

    static int _tiffDummyMapProcCxx(thandle_t, void **base, toff_t *size)
    {
        (void)base;
        (void)size;
        return (0);
    }

    static void _tiffDummyUnmapProcCxx(thandle_t, void *base, toff_t size)
    {
        (void)base;
        (void)size;
    }

    /*
     * Open a TIFF file descriptor for read/writing.
     */
    static TIFF *_tiffStreamOpen(const char *name, const char *mode, void *fd)
    {
        TIFF *tif;

        if (strchr(mode, 'w'))
        {
            tiffos_data *data = new tiffos_data;
            data->stream = reinterpret_cast<ostream *>(fd);
            data->start_pos = data->stream->tellp();

            // Open for writing.
            tif = TIFFClientOpen(
                name, mode, reinterpret_cast<thandle_t>(data), _tiffosReadProc,
                _tiffosWriteProc, _tiffosSeekProc, _tiffosCloseProc,
                _tiffosSizeProc, _tiffDummyMapProcCxx, _tiffDummyUnmapProcCxx);
            if (!tif)
            {
                delete data;
            }
        }
        else
        {
            tiffis_data *data = new tiffis_data;
            data->stream = reinterpret_cast<istream *>(fd);
            data->start_pos = data->stream->tellg();
            // Open for reading.
            tif = TIFFClientOpen(
                name, mode, reinterpret_cast<thandle_t>(data), _tiffisReadProc,
                _tiffisWriteProc, _tiffisSeekProc, _tiffisCloseProc,
                _tiffisSizeProc, _tiffDummyMapProcCxx, _tiffDummyUnmapProcCxx);
            if (!tif)
            {
                delete data;
            }
        }

        return (tif);
    }

} /* extern "C" */

TIFF *TIFFStreamOpen(const char *name, std::ostream *os)
{
    // If os is either a ostrstream or ostringstream, and has no data
    // written to it yet, then tellp() will return -1 which will break us.
    // We workaround this by writing out a dummy character and
    // then seek back to the beginning.
    if (!os->fail() && static_cast<int>(os->tellp()) < 0)
    {
        *os << '\0';
        os->seekp(0);
    }

    // NB: We don't support mapped files with streams so add 'm'
    return _tiffStreamOpen(name, "wm", os);
}

TIFF *TIFFStreamOpen(const char *name, std::istream *is)
{
    // NB: We don't support mapped files with streams so add 'm'
    return _tiffStreamOpen(name, "rm", is);
}

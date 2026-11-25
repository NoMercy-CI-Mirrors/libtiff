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
 * TIFF Library.
 * Scanline-oriented Read Support
 */
/**
 * @file tif_read.c
 * @brief Transfer image bytes through raw buffers and codec methods.
 */

#include "tiffiop.h"
#include <limits.h>
#include <stdio.h>

int TIFFFillStrip(TIFF *tif, uint32_t strip);
int TIFFFillTile(TIFF *tif, uint32_t tile);
static int TIFFStartStrip(TIFF *tif, uint32_t strip);
static int TIFFStartTile(TIFF *tif, uint32_t tile);
static int TIFFCheckRead(TIFF *, int);
static tmsize_t TIFFReadRawStrip1(TIFF *tif, uint32_t strip, void *buf,
                                  tmsize_t size, const char *module);
static tmsize_t TIFFReadRawTile1(TIFF *tif, uint32_t tile, void *buf,
                                 tmsize_t size, const char *module);

/**
 * @brief Start progressive raw-buffer reads at one MiB.
 * @ingroup libtiff_private_image_transfers
 *
 * Size in bytes of the first chunk TIFFReadAndRealloc() reads on 64-bit
 * hosts when the raw buffer must grow, so a truncated file does not force
 * one large allocation up front.
 */
#define INITIAL_THRESHOLD (1024 * 1024)
/**
 * @brief Grow raw-buffer read allocation stages by a factor of ten.
 * @ingroup libtiff_private_image_transfers
 *
 * Each chunk TIFFReadAndRealloc() reads is ten times the previous one
 * (1, 10, 100 MiB).
 */
#define THRESHOLD_MULTIPLIER 10
/**
 * @brief Set the allocation-growth cutoff to 1000 MiB.
 * @ingroup libtiff_private_image_transfers
 *
 * Once the chunk size reaches this value, TIFFReadAndRealloc() reads the
 * whole remaining request at once; it is not a maximum read size.
 */
#define MAX_THRESHOLD                                                          \
    (THRESHOLD_MULTIPLIER * THRESHOLD_MULTIPLIER * THRESHOLD_MULTIPLIER *      \
     INITIAL_THRESHOLD)

/**
 * @brief Bound accepted raw strile byte counts to INT64_MAX.
 * @ingroup libtiff_private_image_transfers
 *
 * Expands to INT64_MAX; TIFFFillStrip() and TIFFFillTile() reject strile
 * byte counts beyond the signed 64-bit range.
 */
#define TIFF_INT64_MAX ((((int64_t)0x7FFFFFFF) << 32) | 0xFFFFFFFF)
/**
 * @brief Grow and fill the raw buffer from the current client position.
 * @ingroup libtiff_private_image_transfers
 *
 * Reads from the current client position, which the caller has already
 * set; this function does not seek. Requires nonnegative representable
 * size/offset, and a library-owned raw buffer (TIFF_MYBUFFER) if it must
 * grow. On 64-bit hosts the buffer grows in progressive 1/10/100 MiB read
 * stages; the cutoff is not a maximum read size. On 32-bit hosts, requests
 * above 1e9 bytes that are not smaller than the file size are rejected.
 * Realloc failure frees the old raw allocation and clears capacity;
 * short/error reads zero the remaining allocated tail but retain partial
 * data. tif_rawcp is not updated and can point into freed storage after
 * growth; callers must reset it. Failures report through TIFFErrorExtR();
 * no errno contract. See @ref image_transfers.
 *
 * @param[in,out] tif Non-NULL live handle whose raw buffer can be grown or
 * freed; the client file position moves; serialise access externally.
 * @param[in] size Nonnegative number of bytes to read at rawdata_offset; zero
 * reads nothing and succeeds.
 * @param[in] rawdata_offset Nonnegative raw-buffer destination byte offset
 * with representable sums.
 * @param[in] is_strip Nonzero for strip wording in diagnostics, zero for tile
 * wording.
 * @param[in] strip_or_tile Strile index, used only in diagnostics.
 * @param[in] module Borrowed diagnostic context string.
 * @return One after all bytes read, zero on size/allocation/short-read
 * failure.
 */

/* Read 'size' bytes in tif_rawdata buffer starting at offset 'rawdata_offset'
 * Returns 1 in case of success, 0 otherwise. */
static int TIFFReadAndRealloc(TIFF *tif, tmsize_t size, tmsize_t rawdata_offset,
                              int is_strip, uint32_t strip_or_tile,
                              const char *module)
{
#if SIZEOF_SIZE_T == 8
    tmsize_t threshold = INITIAL_THRESHOLD;
#endif
    tmsize_t already_read = 0;

#if SIZEOF_SIZE_T != 8
    /* On 32 bit processes, if the request is large enough, check against */
    /* file size */
    if (size > 1000 * 1000 * 1000)
    {
        uint64_t filesize = TIFFGetFileSize(tif);
        if ((uint64_t)size >= filesize)
        {
            TIFFErrorExtR(tif, module,
                          "Chunk size requested is larger than file size.");
            return 0;
        }
    }
#endif

    /* On 64 bit processes, read first a maximum of 1 MB, then 10 MB, etc */
    /* so as to avoid allocating too much memory in case the file is too */
    /* short. We could ask for the file size, but this might be */
    /* expensive with some I/O layers (think of reading a gzipped file) */
    /* Restrict to 64 bit processes, so as to avoid reallocs() */
    /* on 32 bit processes where virtual memory is scarce.  */
    while (already_read < size)
    {
        tmsize_t bytes_read;
        tmsize_t to_read = size - already_read;
#if SIZEOF_SIZE_T == 8
        if (to_read >= threshold && threshold < MAX_THRESHOLD &&
            already_read + to_read + rawdata_offset > tif->tif_rawdatasize)
        {
            to_read = threshold;
            threshold *= THRESHOLD_MULTIPLIER;
        }
#endif
        if (already_read + to_read + rawdata_offset > tif->tif_rawdatasize)
        {
            uint8_t *new_rawdata;
            assert((tif->tif_flags & TIFF_MYBUFFER) != 0);
            tif->tif_rawdatasize = (tmsize_t)TIFFroundup_64(
                (uint64_t)already_read + (uint64_t)to_read +
                    (uint64_t)rawdata_offset,
                1024);
            if (tif->tif_rawdatasize == 0)
            {
                TIFFErrorExtR(tif, module, "Invalid buffer size");
                return 0;
            }
            new_rawdata = (uint8_t *)_TIFFreallocExt(tif, tif->tif_rawdata,
                                                     tif->tif_rawdatasize);
            if (new_rawdata == 0)
            {
                TIFFErrorExtR(tif, module,
                              "No space for data buffer at scanline %" PRIu32,
                              tif->tif_dir.td_row);
                _TIFFfreeExt(tif, tif->tif_rawdata);
                tif->tif_rawdata = 0;
                tif->tif_rawdatasize = 0;
                return 0;
            }
            tif->tif_rawdata = new_rawdata;
        }
        if (tif->tif_rawdata == NULL)
        {
            /* should not happen in practice but helps CoverityScan */
            return 0;
        }

        bytes_read = TIFFReadFile(
            tif, tif->tif_rawdata + rawdata_offset + already_read, to_read);
        if (bytes_read < 0)
            /* Treat read errors as short reads before updating offsets. */
            bytes_read = 0;
        already_read += bytes_read;
        if (bytes_read != to_read)
        {
            memset(
                tif->tif_rawdata + rawdata_offset + already_read, 0,
                (size_t)(tif->tif_rawdatasize - rawdata_offset - already_read));
            if (is_strip)
            {
                TIFFErrorExtR(tif, module,
                              "Read error at scanline %" PRIu32
                              "; got %" TIFF_SSIZE_FORMAT " bytes, "
                              "expected %" TIFF_SSIZE_FORMAT,
                              tif->tif_dir.td_row, already_read, size);
            }
            else
            {
                TIFFErrorExtR(tif, module,
                              "Read error at row %" PRIu32 ", col %" PRIu32
                              ", tile %" PRIu32 "; "
                              "got %" TIFF_SSIZE_FORMAT
                              " bytes, expected %" TIFF_SSIZE_FORMAT "",
                              tif->tif_dir.td_row, tif->tif_dir.td_col,
                              strip_or_tile, already_read, size);
            }
            return 0;
        }
    }
    return 1;
}
/**
 * @brief Refill a partial raw strip while retaining unconsumed bytes.
 * @ingroup libtiff_private_image_transfers
 *
 * Requires a representable nonnegative int strip index, non-mapped raw buffer,
 * valid read-ahead arithmetic, and coherent cursor/loaded counts. Moves unread
 * bytes, grows owned storage, reads the next extent, optionally reverses bits,
 * and restarts decoding when requested. A multi-scan JPEG restart can require a
 * whole-strip fallback. Borrowed undersized buffers fail instead of growing.
 * May grow the handle-owned raw buffer, load deferred strile metadata, seek and
 * read through the client procedures, and invoke codec and diagnostic
 * callbacks; it never writes to the file. Failure is not transactional:
 * buffers, codec progress, indices, flags, or file position can already have
 * changed. No errno contract. See @ref image_transfers.
 *
 * @param[in,out] tif Non-NULL live handle with matching current geometry
 * and codec methods; serialise access externally.
 * @param[in] strip Valid zero-based strip index; the parameter is int, so
 * the index must be representable by int.
 * @param[in] read_ahead Nonnegative desired ahead extent with representable
 * cursor arithmetic.
 * @param[in] restart Nonzero to reset partial-strip decoding; zero to
 * retain unread bytes.
 * @return One after refill/start, zero on detected buffer/seek/read/start
 * failure.
 */

static int TIFFFillStripPartial(TIFF *tif, int strip, tmsize_t read_ahead,
                                int restart)
{
    static const char module[] = "TIFFFillStripPartial";
    TIFFDirectory *td = &tif->tif_dir;
    tmsize_t unused_data;
    uint64_t read_offset;
    tmsize_t to_read;
    tmsize_t read_ahead_mod;
    /* tmsize_t bytecountm; */

    /*
     * Expand raw data buffer, if needed, to hold data
     * strip coming from file (perhaps should set upper
     * bound on the size of a buffer we'll use?).
     */

    /* bytecountm=(tmsize_t) TIFFGetStrileByteCount(tif, strip); */

    /* Not completely sure where the * 2 comes from, but probably for */
    /* an exponentional growth strategy of tif_rawdatasize */
    if (read_ahead < TIFF_TMSIZE_T_MAX / 2)
        read_ahead_mod = read_ahead * 2;
    else
        read_ahead_mod = read_ahead;
    if (read_ahead_mod > tif->tif_rawdatasize)
    {
        assert(restart);

        tif->tif_dir.td_curstrip = NOSTRIP;
        if ((tif->tif_flags & TIFF_MYBUFFER) == 0)
        {
            TIFFErrorExtR(tif, module,
                          "Data buffer too small to hold part of strip %d",
                          strip);
            return (0);
        }
    }

    if (restart)
    {
        tif->tif_rawdataloaded = 0;
        tif->tif_rawdataoff = 0;
    }

    /*
    ** If we are reading more data, move any unused data to the
    ** start of the buffer.
    */
    if (tif->tif_rawdataloaded > 0)
        unused_data =
            tif->tif_rawdataloaded - (tif->tif_rawcp - tif->tif_rawdata);
    else
        unused_data = 0;

    if (unused_data > 0)
    {
        assert((tif->tif_flags & TIFF_BUFFERMMAP) == 0);
        memmove(tif->tif_rawdata, tif->tif_rawcp, (size_t)unused_data);
    }

    /*
    ** Seek to the point in the file where more data should be read.
    */
    read_offset = TIFFGetStrileOffset(tif, (uint32_t)strip);
    if (read_offset > UINT64_MAX - (uint64_t)tif->tif_rawdataoff ||
        read_offset + (uint64_t)tif->tif_rawdataoff >
            UINT64_MAX - (uint64_t)tif->tif_rawdataloaded)
    {
        TIFFErrorExtR(tif, module,
                      "Seek error at scanline %" PRIu32 ", strip %d",
                      tif->tif_dir.td_row, strip);
        return 0;
    }
    read_offset +=
        (uint64_t)tif->tif_rawdataoff + (uint64_t)tif->tif_rawdataloaded;

    if (!SeekOK(tif, read_offset))
    {
        TIFFErrorExtR(tif, module,
                      "Seek error at scanline %" PRIu32 ", strip %d",
                      tif->tif_dir.td_row, strip);
        return 0;
    }

    /*
    ** How much do we want to read?
    */
    if (read_ahead_mod > tif->tif_rawdatasize)
        to_read = read_ahead_mod - unused_data;
    else
        to_read = tif->tif_rawdatasize - unused_data;
    if ((uint64_t)to_read > TIFFGetStrileByteCount(tif, (uint32_t)strip) -
                                (uint64_t)tif->tif_rawdataoff -
                                (uint64_t)tif->tif_rawdataloaded)
    {
        to_read = (tmsize_t)(TIFFGetStrileByteCount(tif, (uint32_t)strip) -
                             (uint64_t)tif->tif_rawdataoff -
                             (uint64_t)tif->tif_rawdataloaded);
    }

    assert((tif->tif_flags & TIFF_BUFFERMMAP) == 0);
    if (!TIFFReadAndRealloc(tif, to_read, unused_data, 1, /* is_strip */
                            0,                            /* strip_or_tile */
                            module))
    {
        return 0;
    }

    tif->tif_rawdataoff =
        tif->tif_rawdataoff + tif->tif_rawdataloaded - unused_data;
    tif->tif_rawdataloaded = unused_data + to_read;

    tif->tif_rawcc = tif->tif_rawdataloaded;
    tif->tif_rawcp = tif->tif_rawdata;

    if (!isFillOrder(tif, td->td_fillorder) &&
        (tif->tif_flags & TIFF_NOBITREV) == 0)
    {
        assert((tif->tif_flags & TIFF_BUFFERMMAP) == 0);
        TIFFReverseBits(tif->tif_rawdata + unused_data, to_read);
    }

    /*
    ** When starting a strip from the beginning we need to
    ** restart the decoder.
    */
    if (restart)
    {

#ifdef JPEG_SUPPORT
        /* A bit messy since breaks the codec abstraction. Ultimately */
        /* there should be a function pointer for that, but it seems */
        /* only JPEG is affected. */
        /* For JPEG, if there are multiple scans (can generally be known */
        /* with the  read_ahead used), we need to read the whole strip */
        if (tif->tif_dir.td_compression == COMPRESSION_JPEG &&
            (uint64_t)tif->tif_rawcc <
                TIFFGetStrileByteCount(tif, (uint32_t)strip))
        {
            if (TIFFJPEGIsFullStripRequired(tif))
            {
                return TIFFFillStrip(tif, (uint32_t)strip);
            }
        }
#endif

        return TIFFStartStrip(tif, (uint32_t)strip);
    }
    else
    {
        return 1;
    }
}
/**
 * @brief Position strip decoding at a requested scanline.
 * @ingroup libtiff_private_image_transfers
 *
 * Validates row, RowsPerStrip and separate sample, computes strip, fills
 * whole/partial raw input, restarts for backward access, and uses the codec
 * seek callback to skip forward. Backward/random access may be unsupported by
 * the codec. May grow or replace the handle-owned raw buffer, load deferred
 * strile metadata, seek and read through the client procedures, and invoke
 * codec and diagnostic callbacks; it never writes to the file. Failure is not
 * transactional: buffers, codec progress, indices, flags, or file position can
 * already have changed. No errno contract. See @ref image_transfers.
 *
 * @param[in,out] tif Non-NULL live handle with matching current geometry
 * and codec methods; serialise access externally.
 * @param[in] row Requested row; values not below ImageLength are diagnosed
 * and rejected.
 * @param[in] sample Sample plane for separate planar images (range-checked
 * against SamplesPerPixel); ignored where contiguous.
 * @return One on positioned decode state, zero on detected
 * bounds/fill/start/codec-seek failure.
 */

/*
 * Seek to a random row+sample in a file.
 *
 * Only used by TIFFReadScanline, and is only used on
 * strip organized files.  We do some tricky stuff to try
 * and avoid reading the whole compressed raw data for big
 * strips.
 */
static int TIFFSeek(TIFF *tif, uint32_t row, uint16_t sample)
{
    TIFFDirectory *td = &tif->tif_dir;
    uint32_t strip;
    int whole_strip;
    tmsize_t read_ahead = 0;

    /*
    ** Establish what strip we are working from.
    */
    if (row >= td->td_imagelength)
    { /* out of range */
        TIFFErrorExtR(tif, tif->tif_name,
                      "%" PRIu32 ": Row out of range, max %" PRIu32 "", row,
                      td->td_imagelength);
        return (0);
    }
    if (td->td_rowsperstrip == 0)
    {
        TIFFErrorExtR(tif, tif->tif_name,
                      "Cannot compute strip: RowsPerStrip is zero");
        return (0);
    }
    if (td->td_planarconfig == PLANARCONFIG_SEPARATE)
    {
        uint64_t sample_offset;
        uint64_t strip64;
        if (sample >= td->td_samplesperpixel)
        {
            TIFFErrorExtR(tif, tif->tif_name,
                          "%" PRIu16 ": Sample out of range, max %" PRIu16 "",
                          sample, td->td_samplesperpixel);
            return (0);
        }
        sample_offset =
            _TIFFMultiply64(tif, sample, td->td_stripsperimage, "TIFFSeek");
        if (sample_offset == 0 && sample != 0 && td->td_stripsperimage != 0)
            return (0);
        strip64 = _TIFFAdd64(tif, sample_offset, row / td->td_rowsperstrip,
                             "TIFFSeek");
        if (strip64 == 0 &&
            (sample_offset != 0 || (row / td->td_rowsperstrip) != 0))
            return (0);
        strip = _TIFFCastUInt64ToUInt32(tif, strip64, "TIFFSeek");
        if (strip == 0 && strip64 != 0)
            return (0);
    }
    else
        strip = row / td->td_rowsperstrip;

    /*
     * Do we want to treat this strip as one whole chunk or
     * read it a few lines at a time?
     */
#if defined(CHUNKY_STRIP_READ_SUPPORT)
    whole_strip = TIFFGetStrileByteCount(tif, strip) < 10 || isMapped(tif);
    if (td->td_compression == COMPRESSION_LERC ||
        td->td_compression == COMPRESSION_JBIG)
    {
        /* Ideally plugins should have a way to declare they don't support
         * chunk strip */
        whole_strip = 1;
    }

    if (!whole_strip)
    {
        /* 16 is for YCbCr mode where we may need to read 16 */
        /* lines at a time to get a decompressed line, and 5000 */
        /* is some constant value, for example for JPEG tables */

        /* coverity[dead_error_line:SUPPRESS] */
        if (tif->tif_dir.td_scanlinesize < TIFF_TMSIZE_T_MAX / 16 &&
            tif->tif_dir.td_scanlinesize * 16 < TIFF_TMSIZE_T_MAX - 5000)
        {
            read_ahead = tif->tif_dir.td_scanlinesize * 16 + 5000;
        }
        else
        {
            read_ahead = tif->tif_dir.td_scanlinesize;
        }
    }
#else
    whole_strip = 1;
#endif

    /*
     * If we haven't loaded this strip, do so now, possibly
     * only reading the first part.
     */
    if (strip != tif->tif_dir.td_curstrip)
    { /* different strip, refill */

        if (whole_strip)
        {
            if (!TIFFFillStrip(tif, strip))
                return (0);
        }
#if defined(CHUNKY_STRIP_READ_SUPPORT)
        else
        {
            if (!TIFFFillStripPartial(tif, strip, read_ahead, 1))
                return 0;
        }
#endif
    }

#if defined(CHUNKY_STRIP_READ_SUPPORT)
    /*
    ** If we already have some data loaded, do we need to read some more?
    */
    else if (!whole_strip)
    {
        /* coverity[dead_error_line:SUPPRESS] */
        if (((tif->tif_rawdata + tif->tif_rawdataloaded) - tif->tif_rawcp) <
                read_ahead &&
            (uint64_t)tif->tif_rawdataoff + tif->tif_rawdataloaded <
                TIFFGetStrileByteCount(tif, strip))
        {
            if (!TIFFFillStripPartial(tif, strip, read_ahead, 0))
                return 0;
        }
    }
#endif

    if (row < tif->tif_dir.td_row)
    {
        /*
         * Moving backwards within the same strip: backup
         * to the start and then decode forward (below).
         *
         * NB: If you're planning on lots of random access within a
         * strip, it's better to just read and decode the entire
         * strip, and then access the decoded data in a random fashion.
         */

        if (tif->tif_rawdataoff != 0)
        {
            if (!TIFFFillStripPartial(tif, (int)strip, read_ahead, 1))
                return 0;
        }
        else
        {
            if (!TIFFStartStrip(tif, strip))
                return (0);
        }
    }

    if (row != tif->tif_dir.td_row)
    {
        /*
         * Seek forward to the desired row.
         */

        /* TODO: Will this really work with partial buffers? */

        if (!(*tif->tif_seek)(tif, row - tif->tif_dir.td_row))
            return (0);
        tif->tif_dir.td_row = row;
    }

    return (1);
}

int TIFFReadScanline(TIFF *tif, void *buf, uint32_t row, uint16_t sample)
{
    int e;

    if (!TIFFCheckRead(tif, 0))
        return (-1);
    if ((e = TIFFSeek(tif, row, sample)) != 0)
    {
        /*
         * Decompress desired row into user buffer.
         */
        e = (*tif->tif_decoderow)(tif, (uint8_t *)buf,
                                  tif->tif_dir.td_scanlinesize, sample);

        /* we are now poised at the beginning of the next row */
        tif->tif_dir.td_row = row + 1;

        if (e)
            (*tif->tif_postdecode)(tif, (uint8_t *)buf,
                                   tif->tif_dir.td_scanlinesize);
    }
    else
    {
        /* See TIFFReadEncodedStrip comment regarding TIFFTAG_FAXFILLFUNC. */
        if (buf)
            memset(buf, 0, (size_t)tif->tif_dir.td_scanlinesize);
    }
    return (e > 0 ? 1 : -1);
}
/**
 * @brief Validate a strip and compute its effective decoded extent.
 * @ingroup libtiff_private_image_transfers
 *
 * Checks readable strip organisation/index and nonzero row geometry,
 * computes truncated last-strip rows and optional plane, and calls the
 * layout calculator. Does not load/decode pixels or acquire output storage;
 * diagnostics can occur.
 *
 * @param[in] tif Non-NULL live handle whose mode, organisation and geometry
 * are read; serialise access externally.
 * @param[in] strip Zero-based strip index; values not below td_nstrips are
 * diagnosed and rejected.
 * @param[out] pplane Optional plane-number output; written once the strip
 * index and RowsPerStrip checks pass, before the size computation, so it can
 * be set even when -1 is returned.
 * @return Positive effective decoded byte size, or minus one on rejected
 * state/geometry.
 */

/*
 * Calculate the strip size according to the number of
 * rows in the strip (check for truncated last strip on any
 * of the separations).
 */
static tmsize_t TIFFReadEncodedStripGetStripSize(TIFF *tif, uint32_t strip,
                                                 uint16_t *pplane)
{
    static const char module[] = "TIFFReadEncodedStrip";
    TIFFDirectory *td = &tif->tif_dir;
    uint32_t rowsperstrip;
    uint32_t stripsperplane;
    uint32_t stripinplane;
    uint32_t rows;
    tmsize_t stripsize;
    if (!TIFFCheckRead(tif, 0))
        return ((tmsize_t)(-1));
    if (strip >= td->td_nstrips)
    {
        TIFFErrorExtR(tif, module,
                      "%" PRIu32 ": Strip out of range, max %" PRIu32, strip,
                      td->td_nstrips);
        return ((tmsize_t)(-1));
    }

    rowsperstrip = td->td_rowsperstrip;
    if (rowsperstrip > td->td_imagelength)
        rowsperstrip = td->td_imagelength;
    if (rowsperstrip == 0)
    {
        TIFFErrorExtR(tif, module, "rowsperstrip is zero");
        return ((tmsize_t)(-1));
    }
    stripsperplane =
        TIFFhowmany_32_maxuint_compat(td->td_imagelength, rowsperstrip);
    stripinplane = (strip % stripsperplane);
    if (pplane)
        *pplane = (uint16_t)(strip / stripsperplane);
    rows = td->td_imagelength - stripinplane * rowsperstrip;
    if (rows > rowsperstrip)
        rows = rowsperstrip;
    stripsize = TIFFVStripSize(tif, rows);
    if (stripsize == 0)
        return ((tmsize_t)(-1));
    return stripsize;
}

/*
 * Read a strip of data and decompress the specified
 * amount into the user-supplied buffer.
 */
tmsize_t TIFFReadEncodedStrip(TIFF *tif, uint32_t strip, void *buf,
                              tmsize_t size)
{
    static const char module[] = "TIFFReadEncodedStrip";
    TIFFDirectory *td = &tif->tif_dir;
    tmsize_t stripsize;
    uint16_t plane;

    stripsize = TIFFReadEncodedStripGetStripSize(tif, strip, &plane);
    if (stripsize == ((tmsize_t)(-1)))
        return ((tmsize_t)(-1));

    /* shortcut to avoid an extra memcpy() */
    if (td->td_compression == COMPRESSION_NONE && size != (tmsize_t)(-1) &&
        size >= stripsize && !isMapped(tif) &&
        ((tif->tif_flags & TIFF_NOREADRAW) == 0))
    {
        if (TIFFReadRawStrip1(tif, strip, buf, stripsize, module) != stripsize)
            return ((tmsize_t)(-1));

        if (!isFillOrder(tif, td->td_fillorder) &&
            (tif->tif_flags & TIFF_NOBITREV) == 0)
            TIFFReverseBits((uint8_t *)buf, stripsize);

        (*tif->tif_postdecode)(tif, (uint8_t *)buf, stripsize);
        return (stripsize);
    }

    if ((size != (tmsize_t)(-1)) && (size < stripsize))
        stripsize = size;
    if (!TIFFFillStrip(tif, strip))
    {
        /* The output buf may be NULL, in particular if TIFFTAG_FAXFILLFUNC
           is being used. Thus, memset must be conditional on buf not NULL. */
        if (buf)
            memset(buf, 0, (size_t)stripsize);
        return ((tmsize_t)(-1));
    }
    if ((*tif->tif_decodestrip)(tif, (uint8_t *)buf, stripsize, plane) <= 0)
        return ((tmsize_t)(-1));
    (*tif->tif_postdecode)(tif, (uint8_t *)buf, stripsize);
    return (stripsize);
}

/* Variant of TIFFReadEncodedStrip() that does
 * * if *buf == NULL, *buf = _TIFFmallocExt(tif, bufsizetoalloc) only after
 * TIFFFillStrip() has succeeded. This avoid excessive memory allocation in case
 * of truncated file.
 * * calls regular TIFFReadEncodedStrip() if *buf != NULL
 */
tmsize_t _TIFFReadEncodedStripAndAllocBuffer(TIFF *tif, uint32_t strip,
                                             void **buf,
                                             tmsize_t bufsizetoalloc,
                                             tmsize_t size_to_read)
{
    tmsize_t this_stripsize;
    uint16_t plane;

    if (*buf != NULL)
    {
        return TIFFReadEncodedStrip(tif, strip, *buf, size_to_read);
    }

    this_stripsize = TIFFReadEncodedStripGetStripSize(tif, strip, &plane);
    if (this_stripsize == ((tmsize_t)(-1)))
        return ((tmsize_t)(-1));

    if ((size_to_read != (tmsize_t)(-1)) && (size_to_read < this_stripsize))
        this_stripsize = size_to_read;
    if (!TIFFFillStrip(tif, strip))
        return ((tmsize_t)(-1));

    /* Sanity checks to avoid excessive memory allocation */
    /* Max compression ratio experimentally determined. Might be fragile...
     * Only apply this heuristics to situations where the memory allocation
     * would be big, to avoid breaking nominal use cases.
     */
    if (bufsizetoalloc > 100 * 1024 * 1024)
    {
        const uint64_t maxCompressionRatio = TIFFGetMaxCompressionRatio(tif);
        if (maxCompressionRatio > 0 &&
            (uint64_t)tif->tif_rawdatasize <
                (uint64_t)this_stripsize / maxCompressionRatio)
        {
            TIFFErrorExtR(tif, TIFFFileName(tif),
                          "Likely invalid strip byte count for strip %u. "
                          "Uncompressed strip size is %" PRIu64 ", "
                          "compressed one is %" PRIu64,
                          strip, (uint64_t)this_stripsize,
                          (uint64_t)tif->tif_rawdatasize);
            return ((tmsize_t)(-1));
        }
    }

    *buf = _TIFFcallocExt(tif, 1, bufsizetoalloc);
    if (*buf == NULL)
    {
        TIFFErrorExtR(tif, TIFFFileName(tif), "No space for strip buffer");
        return ((tmsize_t)(-1));
    }

    if ((*tif->tif_decodestrip)(tif, (uint8_t *)*buf, this_stripsize, plane) <=
        0)
        return ((tmsize_t)(-1));
    (*tif->tif_postdecode)(tif, (uint8_t *)*buf, this_stripsize);
    return (this_stripsize);
}
/**
 * @brief Read an exact raw-strip byte extent into caller storage.
 * @ingroup libtiff_private_image_transfers
 *
 * Caller validates organisation/index/raw access and positive destination
 * extent. Copies mapped bytes after range checks or seeks/reads exact client
 * bytes. Does not bit-reverse/decode/own the destination; partial stream reads
 * can modify it before failure. Loading the strile offset may read deferred
 * metadata. Allocates nothing, never writes to the file, and leaves the raw
 * buffer and codec state alone; a failed stream read can leave the file
 * position moved. Failures report through TIFFErrorExtR(); no errno contract.
 * See @ref image_transfers.
 *
 * @param[in,out] tif Non-NULL live handle; deferred strile offsets can be
 * loaded and the client file position moves; serialise access externally.
 * @param[in] strip Valid zero-based strip index; not range-checked here.
 * @param[out] buf Borrowed writable caller buffer large enough for size
 * bytes.
 * @param[in] size Positive number of raw bytes to copy into buf.
 * @param[in] module Borrowed diagnostic context string.
 * @return Requested size on complete copy, minus one on seek/range/short
 * read.
 */

static tmsize_t TIFFReadRawStrip1(TIFF *tif, uint32_t strip, void *buf,
                                  tmsize_t size, const char *module)
{
    assert((tif->tif_flags & TIFF_NOREADRAW) == 0);
    if (!isMapped(tif))
    {
        tmsize_t cc;

        if (!SeekOK(tif, TIFFGetStrileOffset(tif, strip)))
        {
            TIFFErrorExtR(tif, module,
                          "Seek error at scanline %" PRIu32 ", strip %" PRIu32,
                          tif->tif_dir.td_row, strip);
            return ((tmsize_t)(-1));
        }
        cc = TIFFReadFile(tif, buf, size);
        if (cc != size)
        {
            TIFFErrorExtR(tif, module,
                          "Read error at scanline %" PRIu32
                          "; got %" TIFF_SSIZE_FORMAT
                          " bytes, expected %" TIFF_SSIZE_FORMAT,
                          tif->tif_dir.td_row, cc, size);
            return ((tmsize_t)(-1));
        }
    }
    else
    {
        tmsize_t ma = 0;
        tmsize_t n;
        if ((TIFFGetStrileOffset(tif, strip) > (uint64_t)TIFF_TMSIZE_T_MAX) ||
            ((ma = (tmsize_t)TIFFGetStrileOffset(tif, strip)) > tif->tif_size))
        {
            n = 0;
        }
        else if (ma > TIFF_TMSIZE_T_MAX - size)
        {
            n = 0;
        }
        else
        {
            tmsize_t mb = ma + size;
            if (mb > tif->tif_size)
                n = tif->tif_size - ma;
            else
                n = size;
        }
        if (n != size)
        {
            TIFFErrorExtR(tif, module,
                          "Read error at scanline %" PRIu32 ", strip %" PRIu32
                          "; got %" TIFF_SSIZE_FORMAT
                          " bytes, expected %" TIFF_SSIZE_FORMAT,
                          tif->tif_dir.td_row, strip, n, size);
            return ((tmsize_t)(-1));
        }
        _TIFFmemcpy(buf, tif->tif_base + ma, size);
    }
    return (size);
}
/**
 * @brief Seek to a strile and read bytes into the expandable raw cache.
 * @ingroup libtiff_private_image_transfers
 *
 * Requires non-mapped raw access and valid metadata/index. Delegates
 * allocation/read to TIFFReadAndRealloc(); old owned cache can be lost on
 * failure. Moves file position and reports contextual strip/tile failures.
 * Loading the strile offset may read deferred metadata. Never writes to the
 * file and invokes no codec callbacks; tif_rawcp is not updated. No errno
 * contract. See @ref image_transfers.
 *
 * @param[in,out] tif Non-NULL live handle whose raw buffer can be grown or
 * freed, deferred strile offsets loaded and file position moved; serialise
 * access externally.
 * @param[in] strip_or_tile Valid strile index whose offset is sought to; not
 * range-checked here.
 * @param[in] is_strip Nonzero for strip wording in diagnostics, zero for tile
 * wording.
 * @param[in] size Nonnegative number of raw bytes to read into the raw
 * buffer from its start.
 * @param[in] module Borrowed diagnostic context string.
 * @return Requested size on success, minus one on seek/allocation/read
 * failure.
 */

static tmsize_t TIFFReadRawStripOrTile2(TIFF *tif, uint32_t strip_or_tile,
                                        int is_strip, tmsize_t size,
                                        const char *module)
{
    assert(!isMapped(tif));
    assert((tif->tif_flags & TIFF_NOREADRAW) == 0);

    if (!SeekOK(tif, TIFFGetStrileOffset(tif, strip_or_tile)))
    {
        if (is_strip)
        {
            TIFFErrorExtR(tif, module,
                          "Seek error at scanline %" PRIu32 ", strip %" PRIu32,
                          tif->tif_dir.td_row, strip_or_tile);
        }
        else
        {
            TIFFErrorExtR(
                tif, module,
                "Seek error at row %" PRIu32 ", col %" PRIu32 ", tile %" PRIu32,
                tif->tif_dir.td_row, tif->tif_dir.td_col, strip_or_tile);
        }
        return ((tmsize_t)(-1));
    }

    if (!TIFFReadAndRealloc(tif, size, 0, is_strip, strip_or_tile, module))
    {
        return ((tmsize_t)(-1));
    }

    return (size);
}

/*
 * Read a strip of data from the file.
 */
tmsize_t TIFFReadRawStrip(TIFF *tif, uint32_t strip, void *buf, tmsize_t size)
{
    static const char module[] = "TIFFReadRawStrip";
    TIFFDirectory *td = &tif->tif_dir;
    uint64_t bytecount64;
    tmsize_t bytecountm;

    if (!TIFFCheckRead(tif, 0))
        return ((tmsize_t)(-1));
    if (strip >= td->td_nstrips)
    {
        TIFFErrorExtR(tif, module,
                      "%" PRIu32 ": Strip out of range, max %" PRIu32, strip,
                      td->td_nstrips);
        return ((tmsize_t)(-1));
    }
    if (tif->tif_flags & TIFF_NOREADRAW)
    {
        TIFFErrorExtR(tif, module,
                      "Compression scheme does not support access to raw "
                      "uncompressed data");
        return ((tmsize_t)(-1));
    }
    bytecount64 = TIFFGetStrileByteCount(tif, strip);
    if (size != (tmsize_t)(-1) && (uint64_t)size <= bytecount64)
        bytecountm = size;
    else
        bytecountm = _TIFFCastUInt64ToSSize(tif, bytecount64, module);
    if (bytecountm == 0)
    {
        return ((tmsize_t)(-1));
    }
    return (TIFFReadRawStrip1(tif, strip, buf, bytecountm, module));
}
/**
 * @brief Subtract unsigned offsets with intentional modular arithmetic.
 * @ingroup libtiff_private_image_transfers
 *
 * Pure modulo-2^64 subtraction, without overflow rejection, diagnostics,
 * allocation, state, or ownership effects; unsigned-overflow
 * instrumentation is disabled for this helper.
 *
 * @param[in] a Unsigned minuend.
 * @param[in] b Unsigned subtrahend.
 * @return Wrapped unsigned difference.
 */

TIFF_NOSANITIZE_UNSIGNED_INT_OVERFLOW
static uint64_t NoSanitizeSubUInt64(uint64_t a, uint64_t b) { return a - b; }
/**
 * @brief Load raw strip bytes and initialise codec decoding.
 * @ingroup libtiff_private_image_transfers
 *
 * Caller supplies valid strip metadata/index. Validates byte-count bounds,
 * applies recovery size/ratio heuristics, uses mapped bytes when fill order
 * allows or replaces/grows raw storage, reverses bits when needed, and calls
 * TIFFStartStrip(). Non-raw codecs skip loading. Borrowed raw data can be
 * detached but is not freed as owned storage. Mapping/cache/codec state can
 * change on failure. May allocate or replace the handle-owned raw buffer, load
 * deferred strile metadata, seek and read through the client procedures, and
 * invoke codec and diagnostic callbacks; it never writes to the file. No handle
 * or caller-buffer ownership transfers. Failure is not transactional: buffers,
 * codec progress, indices, flags, or file position can already have changed. No
 * errno contract. See @ref image_transfers.
 *
 * @param[in,out] tif Non-NULL live handle with matching current geometry
 * and codec methods; serialise access externally.
 * @param[in] strip Valid zero-based strip index; not range-checked here.
 * @return One after load/start, zero on detected byte-count, buffer,
 * allocation, I/O, or codec failure.
 */

/*
 * Read the specified strip and setup for decoding. The data buffer is
 * expanded, as necessary, to hold the strip's data.
 */
int TIFFFillStrip(TIFF *tif, uint32_t strip)
{
    static const char module[] = "TIFFFillStrip";
    TIFFDirectory *td = &tif->tif_dir;

    if ((tif->tif_flags & TIFF_NOREADRAW) == 0)
    {
        uint64_t bytecount = TIFFGetStrileByteCount(tif, strip);
        if (bytecount == 0 || bytecount > (uint64_t)TIFF_INT64_MAX)
        {
            TIFFErrorExtR(tif, module,
                          "Invalid strip byte count %" PRIu64
                          ", strip %" PRIu32,
                          bytecount, strip);
            return (0);
        }

        /* To avoid excessive memory allocations: */
        const tmsize_t stripsize = TIFFStripSize(tif);
        if (stripsize > 0)
        {
            if (bytecount > 1024 * 1024 &&
                (bytecount - 4096) / 10 > (uint64_t)stripsize)
            {
                /* Byte count should normally not be larger than a number of */
                /* times the uncompressed size plus some margin */
                /* 10 and 4096 are just values that could be adjusted. */
                /* Hopefully they are safe enough for all codecs */
                /* What happens next will depend on whether only the bytecount
                 */
                /* was corrupted to a large value but the strip/tile data is */
                /* fine. In that situation most codecs should work fine and */
                /* only used part of the tile/strip data. If the strip/tile */
                /* data is corrupted too, then codecs will later error out. */
                uint64_t newbytecount = (uint64_t)stripsize * 10 + 4096;
                TIFFWarningExtR(tif, module,
                                "Too large strip byte count %" PRIu64
                                ", strip %" PRIu32 ". Limiting to %" PRIu64,
                                bytecount, strip, newbytecount);
                bytecount = newbytecount;
            }
            else if (stripsize > 100 * 1024 * 1024)
            {
                /* Max compression ratio experimentally determined. Might be
                 * fragile... Only apply this heuristics to situations where the
                 * memory allocation would be big, to avoid breaking nominal use
                 * cases.
                 */
                const uint64_t maxCompressionRatio =
                    TIFFGetMaxCompressionRatio(tif);
                if (maxCompressionRatio > 0 &&
                    bytecount < (uint64_t)stripsize / maxCompressionRatio)
                {
                    TIFFErrorExtR(
                        tif, module,
                        "Likely invalid strip byte count for strip %u. "
                        "Uncompressed strip size is %" PRIu64 ", "
                        "compressed one is %" PRIu64,
                        strip, (uint64_t)stripsize, bytecount);
                    return 0;
                }
            }
        }

        if (isMapped(tif))
        {
            /*
             * We must check for overflow, potentially causing
             * an OOB read. Instead of simple
             *
             *  TIFFGetStrileOffset(tif, strip)+bytecount > tif->tif_size
             *
             * comparison (which can overflow) we do the following
             * two comparisons:
             */
            if (bytecount > (uint64_t)tif->tif_size ||
                TIFFGetStrileOffset(tif, strip) >
                    (uint64_t)tif->tif_size - bytecount)
            {
                /*
                 * This error message might seem strange, but
                 * it's what would happen if a read were done
                 * instead.
                 */
                TIFFErrorExtR(
                    tif, module,

                    "Read error on strip %" PRIu32 "; "
                    "got %" PRIu64 " bytes, expected %" PRIu64,
                    strip,
                    NoSanitizeSubUInt64((uint64_t)tif->tif_size,
                                        TIFFGetStrileOffset(tif, strip)),
                    bytecount);
                tif->tif_dir.td_curstrip = NOSTRIP;
                return (0);
            }
        }

        if (isMapped(tif) && (isFillOrder(tif, td->td_fillorder) ||
                              (tif->tif_flags & TIFF_NOBITREV)))
        {
            /*
             * The image is mapped into memory and we either don't
             * need to flip bits or the compression routine is
             * going to handle this operation itself.  In this
             * case, avoid copying the raw data and instead just
             * reference the data from the memory mapped file
             * image.  This assumes that the decompression
             * routines do not modify the contents of the raw data
             * buffer (if they try to, the application will get a
             * fault since the file is mapped read-only).
             */
            if ((tif->tif_flags & TIFF_MYBUFFER) && tif->tif_rawdata)
            {
                _TIFFfreeExt(tif, tif->tif_rawdata);
                tif->tif_rawdata = NULL;
                tif->tif_rawdatasize = 0;
            }
            tif->tif_flags &= ~TIFF_MYBUFFER;
            tif->tif_rawdatasize = (tmsize_t)bytecount;
            tif->tif_rawdata =
                tif->tif_base + (tmsize_t)TIFFGetStrileOffset(tif, strip);
            tif->tif_rawdataoff = 0;
            tif->tif_rawdataloaded = (tmsize_t)bytecount;

            /*
             * When we have tif_rawdata reference directly into the memory
             * mapped file we need to be pretty careful about how we use the
             * rawdata.  It is not a general purpose working buffer as it
             * normally otherwise is.  So we keep track of this fact to avoid
             * using it improperly.
             */
            tif->tif_flags |= TIFF_BUFFERMMAP;
        }
        else
        {
            /*
             * Expand raw data buffer, if needed, to hold data
             * strip coming from file (perhaps should set upper
             * bound on the size of a buffer we'll use?).
             */
            tmsize_t bytecountm;
            bytecountm = (tmsize_t)bytecount;
            if ((uint64_t)bytecountm != bytecount)
            {
                TIFFErrorExtR(tif, module, "Integer overflow");
                return (0);
            }
            if (bytecountm > tif->tif_rawdatasize)
            {
                tif->tif_dir.td_curstrip = NOSTRIP;
                if ((tif->tif_flags & TIFF_MYBUFFER) == 0)
                {
                    TIFFErrorExtR(
                        tif, module,
                        "Data buffer too small to hold strip %" PRIu32, strip);
                    return (0);
                }
            }
            if (tif->tif_flags & TIFF_BUFFERMMAP)
            {
                tif->tif_dir.td_curstrip = NOSTRIP;
                tif->tif_rawdata = NULL;
                tif->tif_rawdatasize = 0;
                tif->tif_flags &= ~TIFF_BUFFERMMAP;
            }

            if (isMapped(tif))
            {
                if (bytecountm > tif->tif_rawdatasize &&
                    !TIFFReadBufferSetup(tif, 0, bytecountm))
                {
                    return (0);
                }
                if (TIFFReadRawStrip1(tif, strip, tif->tif_rawdata, bytecountm,
                                      module) != bytecountm)
                {
                    return (0);
                }
            }
            else
            {
                if (TIFFReadRawStripOrTile2(tif, strip, 1, bytecountm,
                                            module) != bytecountm)
                {
                    return (0);
                }
            }

            tif->tif_rawdataoff = 0;
            tif->tif_rawdataloaded = bytecountm;

            if (!isFillOrder(tif, td->td_fillorder) &&
                (tif->tif_flags & TIFF_NOBITREV) == 0)
                TIFFReverseBits(tif->tif_rawdata, bytecountm);
        }
    }
    return (TIFFStartStrip(tif, strip));
}

/*
 * Tile-oriented Read Support
 * Contributed by Nancy Cam (Silicon Graphics).
 */

/*
 * Read and decompress a tile of data.  The
 * tile is selected by the (x,y,z,s) coordinates.
 */
tmsize_t TIFFReadTile(TIFF *tif, void *buf, uint32_t x, uint32_t y, uint32_t z,
                      uint16_t s)
{
    if (!TIFFCheckRead(tif, 1) || !TIFFCheckTile(tif, x, y, z, s))
        return ((tmsize_t)(-1));
    return (TIFFReadEncodedTile(tif, TIFFComputeTile(tif, x, y, z, s), buf,
                                (tmsize_t)(-1)));
}

/*
 * Read a tile of data and decompress the specified
 * amount into the user-supplied buffer.
 */
tmsize_t TIFFReadEncodedTile(TIFF *tif, uint32_t tile, void *buf, tmsize_t size)
{
    static const char module[] = "TIFFReadEncodedTile";
    TIFFDirectory *td = &tif->tif_dir;
    tmsize_t tilesize = tif->tif_dir.td_tilesize;

    if (!TIFFCheckRead(tif, 1))
        return ((tmsize_t)(-1));
    if (tile >= td->td_nstrips)
    {
        TIFFErrorExtR(tif, module,
                      "%" PRIu32 ": Tile out of range, max %" PRIu32, tile,
                      td->td_nstrips);
        return ((tmsize_t)(-1));
    }

    /* shortcut to avoid an extra memcpy() */
    if (td->td_compression == COMPRESSION_NONE && size != (tmsize_t)(-1) &&
        size >= tilesize && !isMapped(tif) &&
        ((tif->tif_flags & TIFF_NOREADRAW) == 0))
    {
        if (TIFFReadRawTile1(tif, tile, buf, tilesize, module) != tilesize)
            return ((tmsize_t)(-1));

        if (!isFillOrder(tif, td->td_fillorder) &&
            (tif->tif_flags & TIFF_NOBITREV) == 0)
            TIFFReverseBits((uint8_t *)buf, tilesize);

        (*tif->tif_postdecode)(tif, (uint8_t *)buf, tilesize);
        return (tilesize);
    }

    if (size == (tmsize_t)(-1))
        size = tilesize;
    else if (size > tilesize)
        size = tilesize;
    if (!TIFFFillTile(tif, tile))
    {
        /* See TIFFReadEncodedStrip comment regarding TIFFTAG_FAXFILLFUNC. */
        if (buf)
            memset(buf, 0, (size_t)size);
        return ((tmsize_t)(-1));
    }
    else if ((*tif->tif_decodetile)(tif, (uint8_t *)buf, size,
                                    (uint16_t)(tile / td->td_stripsperimage)))
    {
        (*tif->tif_postdecode)(tif, (uint8_t *)buf, size);
        return (size);
    }
    else
        return ((tmsize_t)(-1));
}

/* Variant of TIFFReadTile() that does
 * * if *buf == NULL, *buf = _TIFFmallocExt(tif, bufsizetoalloc) only after
 * TIFFFillTile() has succeeded. This avoid excessive memory allocation in case
 * of truncated file.
 * * calls regular TIFFReadEncodedTile() if *buf != NULL
 */
tmsize_t _TIFFReadTileAndAllocBuffer(TIFF *tif, void **buf,
                                     tmsize_t bufsizetoalloc, uint32_t x,
                                     uint32_t y, uint32_t z, uint16_t s)
{
    if (!TIFFCheckRead(tif, 1) || !TIFFCheckTile(tif, x, y, z, s))
        return ((tmsize_t)(-1));
    return (_TIFFReadEncodedTileAndAllocBuffer(
        tif, TIFFComputeTile(tif, x, y, z, s), buf, bufsizetoalloc,
        (tmsize_t)(-1)));
}

/* Variant of TIFFReadEncodedTile() that does
 * * if *buf == NULL, *buf = _TIFFmallocExt(tif, bufsizetoalloc) only after
 * TIFFFillTile() has succeeded. This avoid excessive memory allocation in case
 * of truncated file.
 * * calls regular TIFFReadEncodedTile() if *buf != NULL
 */
tmsize_t _TIFFReadEncodedTileAndAllocBuffer(TIFF *tif, uint32_t tile,
                                            void **buf, tmsize_t bufsizetoalloc,
                                            tmsize_t size_to_read)
{
    static const char module[] = "_TIFFReadEncodedTileAndAllocBuffer";
    TIFFDirectory *td = &tif->tif_dir;
    tmsize_t tilesize = tif->tif_dir.td_tilesize;

    if (*buf != NULL)
    {
        return TIFFReadEncodedTile(tif, tile, *buf, size_to_read);
    }

    if (!TIFFCheckRead(tif, 1))
        return ((tmsize_t)(-1));
    if (tile >= td->td_nstrips)
    {
        TIFFErrorExtR(tif, module,
                      "%" PRIu32 ": Tile out of range, max %" PRIu32, tile,
                      td->td_nstrips);
        return ((tmsize_t)(-1));
    }

    if (!TIFFFillTile(tif, tile))
        return ((tmsize_t)(-1));

    /* Sanity checks to avoid excessive memory allocation */
    /* Cf https://gitlab.com/libtiff/libtiff/-/issues/479 */
    if (td->td_compression == COMPRESSION_NONE)
    {
        if (tif->tif_rawdatasize != tilesize)
        {
            TIFFErrorExtR(tif, TIFFFileName(tif),
                          "Invalid tile byte count for tile %u. "
                          "Expected %" PRIu64 ", got %" PRIu64,
                          tile, (uint64_t)tilesize,
                          (uint64_t)tif->tif_rawdatasize);
            return ((tmsize_t)(-1));
        }
    }
    else
    {
        /* Max compression ratio experimentally determined. Might be fragile...
         * Only apply this heuristics to situations where the memory allocation
         * would be big, to avoid breaking nominal use cases.
         */
        if (bufsizetoalloc > 100 * 1024 * 1024)
        {
            const uint64_t maxCompressionRatio =
                TIFFGetMaxCompressionRatio(tif);
            if (maxCompressionRatio > 0 &&
                (uint64_t)tif->tif_rawdatasize <
                    (uint64_t)tilesize / maxCompressionRatio)
            {
                TIFFErrorExtR(tif, TIFFFileName(tif),
                              "Likely invalid tile byte count for tile %u. "
                              "Uncompressed tile size is %" PRIu64 ", "
                              "compressed one is %" PRIu64,
                              tile, (uint64_t)tilesize,
                              (uint64_t)tif->tif_rawdatasize);
                return ((tmsize_t)(-1));
            }
        }
    }

    *buf = _TIFFmallocExt(tif, bufsizetoalloc);
    if (*buf == NULL)
    {
        TIFFErrorExtR(tif, TIFFFileName(tif), "No space for tile buffer");
        return ((tmsize_t)(-1));
    }
    _TIFFmemset(*buf, 0, bufsizetoalloc);

    if (size_to_read == (tmsize_t)(-1))
        size_to_read = tilesize;
    else if (size_to_read > tilesize)
        size_to_read = tilesize;
    if ((*tif->tif_decodetile)(tif, (uint8_t *)*buf, size_to_read,
                               (uint16_t)(tile / td->td_stripsperimage)))
    {
        (*tif->tif_postdecode)(tif, (uint8_t *)*buf, size_to_read);
        return (size_to_read);
    }
    else
        return ((tmsize_t)(-1));
}
/**
 * @brief Read an exact raw-tile byte extent into caller storage.
 * @ingroup libtiff_private_image_transfers
 *
 * Caller validates organisation/index/raw access and positive destination
 * extent. Copies mapped bytes after range checks or seeks/reads exact client
 * bytes. Does not bit-reverse/decode/own the destination; partial stream reads
 * can modify it before failure. Loading the strile offset may read deferred
 * metadata. Allocates nothing, never writes to the file, and leaves the raw
 * buffer and codec state alone; a failed stream read can leave the file
 * position moved. Failures report through TIFFErrorExtR(); no errno contract.
 * See @ref image_transfers.
 *
 * @param[in,out] tif Non-NULL live handle; deferred strile offsets can be
 * loaded and the client file position moves; serialise access externally.
 * @param[in] tile Valid zero-based tile index; not range-checked here.
 * @param[out] buf Borrowed writable caller buffer large enough for size
 * bytes.
 * @param[in] size Positive number of raw bytes to copy into buf.
 * @param[in] module Borrowed diagnostic context string.
 * @return Requested size on complete copy, minus one on seek/range/short
 * read.
 */

static tmsize_t TIFFReadRawTile1(TIFF *tif, uint32_t tile, void *buf,
                                 tmsize_t size, const char *module)
{
    assert((tif->tif_flags & TIFF_NOREADRAW) == 0);
    if (!isMapped(tif))
    {
        tmsize_t cc;

        if (!SeekOK(tif, TIFFGetStrileOffset(tif, tile)))
        {
            TIFFErrorExtR(tif, module,
                          "Seek error at row %" PRIu32 ", col %" PRIu32
                          ", tile %" PRIu32,
                          tif->tif_dir.td_row, tif->tif_dir.td_col, tile);
            return ((tmsize_t)(-1));
        }
        cc = TIFFReadFile(tif, buf, size);
        if (cc != size)
        {
            TIFFErrorExtR(tif, module,
                          "Read error at row %" PRIu32 ", col %" PRIu32
                          "; got %" TIFF_SSIZE_FORMAT
                          " bytes, expected %" TIFF_SSIZE_FORMAT,
                          tif->tif_dir.td_row, tif->tif_dir.td_col, cc, size);
            return ((tmsize_t)(-1));
        }
    }
    else
    {
        tmsize_t ma = 0; /* assigned in if-condition below */
        tmsize_t n;
        if ((TIFFGetStrileOffset(tif, tile) > (uint64_t)TIFF_TMSIZE_T_MAX) ||
            ((ma = (tmsize_t)TIFFGetStrileOffset(tif, tile)) > tif->tif_size))
        {
            n = 0;
        }
        else if (ma > TIFF_TMSIZE_T_MAX - size)
        {
            n = 0;
        }
        else
        {
            tmsize_t mb = ma + size;
            if (mb > tif->tif_size)
                n = tif->tif_size - ma;
            else
                n = size;
        }
        if (n != size)
        {
            TIFFErrorExtR(tif, module,
                          "Read error at row %" PRIu32 ", col %" PRIu32
                          ", tile %" PRIu32 "; got %" TIFF_SSIZE_FORMAT
                          " bytes, expected %" TIFF_SSIZE_FORMAT,
                          tif->tif_dir.td_row, tif->tif_dir.td_col, tile, n,
                          size);
            return ((tmsize_t)(-1));
        }
        _TIFFmemcpy(buf, tif->tif_base + ma, size);
    }
    return (size);
}

/*
 * Read a tile of data from the file.
 */
tmsize_t TIFFReadRawTile(TIFF *tif, uint32_t tile, void *buf, tmsize_t size)
{
    static const char module[] = "TIFFReadRawTile";
    TIFFDirectory *td = &tif->tif_dir;
    uint64_t bytecount64;
    tmsize_t bytecountm;

    if (!TIFFCheckRead(tif, 1))
        return ((tmsize_t)(-1));
    if (tile >= td->td_nstrips)
    {
        TIFFErrorExtR(tif, module,
                      "%" PRIu32 ": Tile out of range, max %" PRIu32, tile,
                      td->td_nstrips);
        return ((tmsize_t)(-1));
    }
    if (tif->tif_flags & TIFF_NOREADRAW)
    {
        TIFFErrorExtR(tif, module,
                      "Compression scheme does not support access to raw "
                      "uncompressed data");
        return ((tmsize_t)(-1));
    }
    bytecount64 = TIFFGetStrileByteCount(tif, tile);
    if (size != (tmsize_t)(-1) && (uint64_t)size <= bytecount64)
        bytecountm = size;
    else
        bytecountm = _TIFFCastUInt64ToSSize(tif, bytecount64, module);
    if (bytecountm == 0)
    {
        return ((tmsize_t)(-1));
    }
    return (TIFFReadRawTile1(tif, tile, buf, bytecountm, module));
}
/**
 * @brief Load raw tile bytes and initialise codec decoding.
 * @ingroup libtiff_private_image_transfers
 *
 * Caller supplies valid tile metadata/index. Validates byte-count bounds,
 * applies recovery size/ratio heuristics, uses mapped bytes when fill order
 * allows or replaces/grows raw storage, reverses bits when needed, and calls
 * TIFFStartTile(). Non-raw codecs skip loading. Cache/codec state can change
 * before failure. May allocate or replace the handle-owned raw buffer, load
 * deferred strile metadata, seek and read through the client procedures, and
 * invoke codec and diagnostic callbacks; it never writes to the file. No handle
 * or caller-buffer ownership transfers. Failure is not transactional: buffers,
 * codec progress, indices, flags, or file position can already have changed. No
 * errno contract. See @ref image_transfers.
 *
 * @param[in,out] tif Non-NULL live handle with matching current geometry
 * and codec methods; serialise access externally.
 * @param[in] tile Valid zero-based tile index; not range-checked here.
 * @return One after load/start, zero on detected byte-count, buffer,
 * allocation, I/O, or codec failure.
 */

/*
 * Read the specified tile and setup for decoding. The data buffer is
 * expanded, as necessary, to hold the tile's data.
 */
int TIFFFillTile(TIFF *tif, uint32_t tile)
{
    static const char module[] = "TIFFFillTile";
    TIFFDirectory *td = &tif->tif_dir;

    if ((tif->tif_flags & TIFF_NOREADRAW) == 0)
    {
        uint64_t bytecount = TIFFGetStrileByteCount(tif, tile);
        if (bytecount == 0 || bytecount > (uint64_t)TIFF_INT64_MAX)
        {
            TIFFErrorExtR(tif, module,
                          "%" PRIu64 ": Invalid tile byte count, tile %" PRIu32,
                          bytecount, tile);
            return (0);
        }

        /* To avoid excessive memory allocations: */
        const tmsize_t tilesize = TIFFTileSize(tif);
        if (tilesize > 0)
        {
            if (bytecount > 1024 * 1024 &&
                (bytecount - 4096) / 10 > (uint64_t)tilesize)
            {
                /* Byte count should normally not be larger than a number of */
                /* times the uncompressed size plus some margin */
                /* 10 and 4096 are just values that could be adjusted. */
                /* Hopefully they are safe enough for all codecs */
                /* What happens next will depend on whether only the bytecount
                 */
                /* was corrupted to a large value but the strip/tile data is */
                /* fine. In that situation most codecs should work fine and */
                /* only used part of the tile/strip data. If the strip/tile */
                /* data is corrupted too, then codecs will later error out. */
                uint64_t newbytecount = (uint64_t)tilesize * 10 + 4096;
                TIFFWarningExtR(tif, module,
                                "Too large tile byte count %" PRIu64
                                ", tile %" PRIu32 ". Limiting to %" PRIu64,
                                bytecount, tile, newbytecount);
                bytecount = newbytecount;
            }
            else if (tilesize > 100 * 1024 * 1024)
            {
                /* Max compression ratio experimentally determined. Might be
                 * fragile... Only apply this heuristics to situations where the
                 * memory allocation would be big, to avoid breaking nominal use
                 * cases.
                 */
                const uint64_t maxCompressionRatio =
                    TIFFGetMaxCompressionRatio(tif);
                if (maxCompressionRatio > 0 &&
                    bytecount < (uint64_t)tilesize / maxCompressionRatio)
                {
                    TIFFErrorExtR(tif, module,
                                  "Likely invalid tile byte count for tile %u. "
                                  "Uncompressed tile size is %" PRIu64 ", "
                                  "compressed one is %" PRIu64,
                                  tile, (uint64_t)tilesize, bytecount);
                    return 0;
                }
            }
        }

        if (isMapped(tif))
        {
            /*
             * We must check for overflow, potentially causing
             * an OOB read. Instead of simple
             *
             *  TIFFGetStrileOffset(tif, tile)+bytecount > tif->tif_size
             *
             * comparison (which can overflow) we do the following
             * two comparisons:
             */
            if (bytecount > (uint64_t)tif->tif_size ||
                TIFFGetStrileOffset(tif, tile) >
                    (uint64_t)tif->tif_size - bytecount)
            {
                tif->tif_dir.td_curtile = NOTILE;
                return (0);
            }
        }

        if (isMapped(tif) && (isFillOrder(tif, td->td_fillorder) ||
                              (tif->tif_flags & TIFF_NOBITREV)))
        {
            /*
             * The image is mapped into memory and we either don't
             * need to flip bits or the compression routine is
             * going to handle this operation itself.  In this
             * case, avoid copying the raw data and instead just
             * reference the data from the memory mapped file
             * image.  This assumes that the decompression
             * routines do not modify the contents of the raw data
             * buffer (if they try to, the application will get a
             * fault since the file is mapped read-only).
             */
            if ((tif->tif_flags & TIFF_MYBUFFER) && tif->tif_rawdata)
            {
                _TIFFfreeExt(tif, tif->tif_rawdata);
                tif->tif_rawdata = NULL;
                tif->tif_rawdatasize = 0;
            }
            tif->tif_flags &= ~TIFF_MYBUFFER;

            tif->tif_rawdatasize = (tmsize_t)bytecount;
            tif->tif_rawdata =
                tif->tif_base + (tmsize_t)TIFFGetStrileOffset(tif, tile);
            tif->tif_rawdataoff = 0;
            tif->tif_rawdataloaded = (tmsize_t)bytecount;
            tif->tif_flags |= TIFF_BUFFERMMAP;
        }
        else
        {
            /*
             * Expand raw data buffer, if needed, to hold data
             * tile coming from file (perhaps should set upper
             * bound on the size of a buffer we'll use?).
             */
            tmsize_t bytecountm;
            bytecountm = (tmsize_t)bytecount;
            if ((uint64_t)bytecountm != bytecount)
            {
                TIFFErrorExtR(tif, module, "Integer overflow");
                return (0);
            }
            if (bytecountm > tif->tif_rawdatasize)
            {
                tif->tif_dir.td_curtile = NOTILE;
                if ((tif->tif_flags & TIFF_MYBUFFER) == 0)
                {
                    TIFFErrorExtR(tif, module,
                                  "Data buffer too small to hold tile %" PRIu32,
                                  tile);
                    return (0);
                }
            }
            if (tif->tif_flags & TIFF_BUFFERMMAP)
            {
                tif->tif_dir.td_curtile = NOTILE;
                tif->tif_rawdata = NULL;
                tif->tif_rawdatasize = 0;
                tif->tif_flags &= ~TIFF_BUFFERMMAP;
            }

            if (isMapped(tif))
            {
                if (bytecountm > tif->tif_rawdatasize &&
                    !TIFFReadBufferSetup(tif, 0, bytecountm))
                {
                    return (0);
                }
                if (TIFFReadRawTile1(tif, tile, tif->tif_rawdata, bytecountm,
                                     module) != bytecountm)
                {
                    return (0);
                }
            }
            else
            {
                if (TIFFReadRawStripOrTile2(tif, tile, 0, bytecountm, module) !=
                    bytecountm)
                {
                    return (0);
                }
            }

            tif->tif_rawdataoff = 0;
            tif->tif_rawdataloaded = bytecountm;

            if (tif->tif_rawdata != NULL &&
                !isFillOrder(tif, td->td_fillorder) &&
                (tif->tif_flags & TIFF_NOBITREV) == 0)
                TIFFReverseBits(tif->tif_rawdata, tif->tif_rawdataloaded);
        }
    }
    return (TIFFStartTile(tif, tile));
}

/*
 * Setup the raw data buffer in preparation for
 * reading a strip of raw data.  If the buffer
 * is specified as zero, then a buffer of appropriate
 * size is allocated by the library.  Otherwise,
 * the client must guarantee that the buffer is
 * large enough to hold any individual strip of
 * raw data.
 */
int TIFFReadBufferSetup(TIFF *tif, void *bp, tmsize_t size)
{
    static const char module[] = "TIFFReadBufferSetup";

    if (tif->tif_flags & TIFF_NOREADRAW)
    {
        TIFFErrorExtR(tif, module,
                      "Compression scheme does not support access to raw "
                      "uncompressed data");
        return (0);
    }
    tif->tif_flags &= ~TIFF_BUFFERMMAP;

    if (tif->tif_rawdata)
    {
        if (tif->tif_flags & TIFF_MYBUFFER)
            _TIFFfreeExt(tif, tif->tif_rawdata);
        tif->tif_rawdata = NULL;
        tif->tif_rawdatasize = 0;
    }
    if (bp)
    {
        tif->tif_rawdatasize = size;
        tif->tif_rawdata = (uint8_t *)bp;
        tif->tif_flags &= ~TIFF_MYBUFFER;
    }
    else
    {
        tif->tif_rawdatasize = (tmsize_t)TIFFroundup_64((uint64_t)size, 1024);
        if (tif->tif_rawdatasize == 0)
        {
            TIFFErrorExtR(tif, module, "Invalid buffer size");
            return (0);
        }
        /* Initialize to zero to avoid uninitialized buffers in case of */
        /* short reads (http://bugzilla.maptools.org/show_bug.cgi?id=2651) */
        tif->tif_rawdata =
            (uint8_t *)_TIFFcallocExt(tif, 1, tif->tif_rawdatasize);
        tif->tif_flags |= TIFF_MYBUFFER;
    }
    if (tif->tif_rawdata == NULL)
    {
        TIFFErrorExtR(tif, module,
                      "No space for data buffer at scanline %" PRIu32,
                      tif->tif_dir.td_row);
        tif->tif_rawdatasize = 0;
        return (0);
    }
    return (1);
}
/**
 * @brief Prepare the codec and raw cursor for strip decoding.
 * @ingroup libtiff_private_image_transfers
 *
 * Runs setupdecode once, validates strips-per-plane, sets current strip/row and
 * raw input state, then calls predecode for its plane. Predecode failure
 * invalidates current strip; other setup/state changes survive. Reads no strip
 * payload and allocates no raw buffer itself; when no data is loaded, the raw
 * byte count comes from TIFFGetStrileByteCount(), which may load deferred
 * metadata. The setupdecode and predecode callbacks can allocate codec state,
 * and some codecs (OJPEG) seek and read the file there. No errno contract. See
 * @ref image_transfers.
 *
 * @param[in,out] tif Non-NULL live handle with matching current geometry
 * and codec methods; serialise access externally.
 * @param[in] strip Valid zero-based strip index; not range-checked here.
 * @return One on successful predecode, zero on detected
 * setup/geometry/predecode failure.
 */

/*
 * Set state to appear as if a
 * strip has just been read in.
 */
static int TIFFStartStrip(TIFF *tif, uint32_t strip)
{
    static const char module[] = "TIFFStartStrip";
    TIFFDirectory *td = &tif->tif_dir;

    if ((tif->tif_flags & TIFF_CODERSETUP) == 0)
    {
        if (!(*tif->tif_setupdecode)(tif))
            return (0);
        tif->tif_flags |= TIFF_CODERSETUP;
    }
    if (td->td_stripsperimage == 0)
    {
        TIFFErrorExtR(tif, module, "Zero strips per image");
        return 0;
    }
    tif->tif_dir.td_curstrip = strip;
    if (TIFFSafeModMul32(tif, module, strip, td->td_stripsperimage,
                         td->td_rowsperstrip, tif->tif_dir.td_row))
        return 0;
    tif->tif_flags &= ~TIFF_BUF4WRITE;

    if (tif->tif_flags & TIFF_NOREADRAW)
    {
        tif->tif_rawcp = NULL;
        tif->tif_rawcc = 0;
    }
    else
    {
        tif->tif_rawcp = tif->tif_rawdata;
        if (tif->tif_rawdataloaded > 0)
            tif->tif_rawcc = tif->tif_rawdataloaded;
        else
            tif->tif_rawcc = (tmsize_t)TIFFGetStrileByteCount(tif, strip);
    }
    if ((*tif->tif_predecode)(tif, (uint16_t)(strip / td->td_stripsperimage)) ==
        0)
    {
        /* Needed for example for scanline access, if tif_predecode */
        /* fails, and we try to read the same strip again. Without invalidating
         */
        /* tif_curstrip, we'd call tif_decoderow() on a possibly invalid */
        /* codec state. */
        tif->tif_dir.td_curstrip = NOSTRIP;
        return 0;
    }
    return 1;
}
/**
 * @brief Prepare the codec and raw cursor for tile decoding.
 * @ingroup libtiff_private_image_transfers
 *
 * Runs setupdecode once, sets current tile and row/column caches using tile
 * geometry, selects raw input, and invokes predecode. Requires nonzero
 * strips-per-plane and valid index from the caller. Unlike strip start,
 * predecode failure does not explicitly invalidate the current tile. Reads no
 * tile payload and allocates no raw buffer itself; when no data is loaded, the
 * raw byte count comes from TIFFGetStrileByteCount(), which may load deferred
 * metadata. The setupdecode and predecode callbacks can allocate codec state,
 * and some codecs (OJPEG) seek and read the file there. State is not rolled
 * back; no errno contract. See @ref image_transfers.
 *
 * @param[in,out] tif Non-NULL live handle with matching current geometry
 * and codec methods; serialise access externally.
 * @param[in] tile Valid zero-based tile index; not range-checked here.
 * @return Predecode callback status, or zero for detected setup/geometry
 * failure.
 */

/*
 * Set state to appear as if a
 * tile has just been read in.
 */
static int TIFFStartTile(TIFF *tif, uint32_t tile)
{
    static const char module[] = "TIFFStartTile";
    TIFFDirectory *td = &tif->tif_dir;
    uint32_t howmany32;

    if ((tif->tif_flags & TIFF_CODERSETUP) == 0)
    {
        if (!(*tif->tif_setupdecode)(tif))
            return (0);
        tif->tif_flags |= TIFF_CODERSETUP;
    }
    tif->tif_dir.td_curtile = tile;
    if (td->td_tilewidth == 0)
    {
        TIFFErrorExtR(tif, module, "Zero tilewidth");
        return 0;
    }
    howmany32 = TIFFhowmany_32(td->td_imagewidth, td->td_tilewidth);
    if (howmany32 == 0)
    {
        TIFFErrorExtR(tif, module, "Zero tiles");
        return 0;
    }
    if (td->td_tilelength == 0)
    {
        TIFFErrorExtR(tif, module, "Zero tilelength");
        return 0;
    }
    if (TIFFSafeModMul32(tif, module, tile, howmany32, td->td_tilelength,
                         tif->tif_dir.td_row))
        return 0;
    howmany32 = TIFFhowmany_32(td->td_imagelength, td->td_tilelength);
    if (howmany32 == 0)
    {
        TIFFErrorExtR(tif, module, "Zero tiles");
        return 0;
    }
    if (TIFFSafeModMul32(tif, module, tile, howmany32, td->td_tilewidth,
                         tif->tif_dir.td_col))
        return 0;
    tif->tif_flags &= ~TIFF_BUF4WRITE;
    if (tif->tif_flags & TIFF_NOREADRAW)
    {
        tif->tif_rawcp = NULL;
        tif->tif_rawcc = 0;
    }
    else
    {
        tif->tif_rawcp = tif->tif_rawdata;
        if (tif->tif_rawdataloaded > 0)
            tif->tif_rawcc = tif->tif_rawdataloaded;
        else
            tif->tif_rawcc = (tmsize_t)TIFFGetStrileByteCount(tif, tile);
    }
    return (
        (*tif->tif_predecode)(tif, (uint16_t)(tile / td->td_stripsperimage)));
}
/**
 * @brief Check readable mode and expected image organisation.
 * @ingroup libtiff_private_image_transfers
 *
 * Only checks O_WRONLY rejection and whether tiles matches isTiled(). No
 * allocation, pixel/codec validation, file I/O, or ownership transfer;
 * errors dispatch diagnostics.
 *
 * @param[in] tif Non-NULL live handle whose mode and organisation are read;
 * serialise access externally.
 * @param[in] tiles Exactly zero for strips or one for tiles.
 * @return One for compatible mode/organisation, zero otherwise.
 */

static int TIFFCheckRead(TIFF *tif, int tiles)
{
    if (tif->tif_mode == O_WRONLY)
    {
        TIFFErrorExtR(tif, tif->tif_name, "File not open for reading");
        return (0);
    }
    if (tiles ^ isTiled(tif))
    {
        TIFFErrorExtR(tif, tif->tif_name,
                      tiles ? "Can not read tiles from a striped image"
                            : "Can not read scanlines from a tiled image");
        return (0);
    }
    return (1);
}

/* Use the provided input buffer (inbuf, insize) and decompress it into
 * (outbuf, outsize).
 * This function replaces the use of
 * TIFFReadEncodedStrip()/TIFFReadEncodedTile() when the user can provide the
 * buffer for the input data, for example when he wants to avoid libtiff to read
 * the strile offset/count values from the [Strip|Tile][Offsets/ByteCounts]
 * array. inbuf content must be writable (if bit reversal is needed) Returns 1
 * in case of success, 0 otherwise.
 */
int TIFFReadFromUserBuffer(TIFF *tif, uint32_t strile, void *inbuf,
                           tmsize_t insize, void *outbuf, tmsize_t outsize)
{
    static const char module[] = "TIFFReadFromUserBuffer";
    TIFFDirectory *td = &tif->tif_dir;
    int ret = 1;
    uint32_t old_tif_flags = tif->tif_flags;
    tmsize_t old_rawdatasize = tif->tif_rawdatasize;
    void *old_rawdata = tif->tif_rawdata;

    if (tif->tif_mode == O_WRONLY)
    {
        TIFFErrorExtR(tif, tif->tif_name, "File not open for reading");
        return 0;
    }
    if (tif->tif_flags & TIFF_NOREADRAW)
    {
        TIFFErrorExtR(tif, module,
                      "Compression scheme does not support access to raw "
                      "uncompressed data");
        return 0;
    }

    tif->tif_flags &= ~TIFF_MYBUFFER;
    tif->tif_flags |= TIFF_BUFFERMMAP;
    tif->tif_rawdatasize = insize;
    tif->tif_rawdata = (uint8_t *)inbuf;
    tif->tif_rawdataoff = 0;
    tif->tif_rawdataloaded = insize;

    if (!isFillOrder(tif, td->td_fillorder) &&
        (tif->tif_flags & TIFF_NOBITREV) == 0)
    {
        TIFFReverseBits((uint8_t *)inbuf, insize);
    }

    if (TIFFIsTiled(tif))
    {
        if (!TIFFStartTile(tif, strile))
        {
            ret = 0;
            /* See related TIFFReadEncodedStrip comment. */
            if (outbuf)
                memset(outbuf, 0, (size_t)outsize);
        }
        else if (!(*tif->tif_decodetile)(
                     tif, (uint8_t *)outbuf, outsize,
                     (uint16_t)(strile / td->td_stripsperimage)))
        {
            ret = 0;
        }
    }
    else
    {
        uint32_t rowsperstrip = td->td_rowsperstrip;
        uint32_t stripsperplane;
        if (rowsperstrip > td->td_imagelength)
            rowsperstrip = td->td_imagelength;
        if (rowsperstrip == 0)
        {
            TIFFErrorExtR(tif, module, "rowsperstrip is zero");
            ret = 0;
        }
        else
        {
            stripsperplane =
                TIFFhowmany_32_maxuint_compat(td->td_imagelength, rowsperstrip);
            if (!TIFFStartStrip(tif, strile))
            {
                ret = 0;
                /* See related TIFFReadEncodedStrip comment. */
                if (outbuf)
                    memset(outbuf, 0, (size_t)outsize);
            }
            else if (!(*tif->tif_decodestrip)(
                         tif, (uint8_t *)outbuf, outsize,
                         (uint16_t)(strile / stripsperplane)))
            {
                ret = 0;
            }
        }
    }
    if (ret)
    {
        (*tif->tif_postdecode)(tif, (uint8_t *)outbuf, outsize);
    }

    if (!isFillOrder(tif, td->td_fillorder) &&
        (tif->tif_flags & TIFF_NOBITREV) == 0)
    {
        TIFFReverseBits((uint8_t *)inbuf, insize);
    }

    tif->tif_flags = (old_tif_flags & (TIFF_MYBUFFER | TIFF_BUFFERMMAP)) |
                     (tif->tif_flags & ~(TIFF_MYBUFFER | TIFF_BUFFERMMAP));
    tif->tif_rawdatasize = old_rawdatasize;
    tif->tif_rawdata = (uint8_t *)old_rawdata;
    tif->tif_rawdataoff = 0;
    tif->tif_rawdataloaded = 0;

    return ret;
}

void _TIFFNoPostDecode(TIFF *tif, uint8_t *buf, tmsize_t cc)
{
    (void)tif;
    (void)buf;
    (void)cc;
}

void _TIFFSwab16BitData(TIFF *tif, uint8_t *buf, tmsize_t cc)
{
    (void)tif;
    assert((cc & 1) == 0);
    TIFFSwabArrayOfShort((uint16_t *)buf, cc / 2);
}

void _TIFFSwab24BitData(TIFF *tif, uint8_t *buf, tmsize_t cc)
{
    (void)tif;
    assert((cc % 3) == 0);
    TIFFSwabArrayOfTriples((uint8_t *)buf, cc / 3);
}

void _TIFFSwab32BitData(TIFF *tif, uint8_t *buf, tmsize_t cc)
{
    (void)tif;
    assert((cc & 3) == 0);
    TIFFSwabArrayOfLong((uint32_t *)buf, cc / 4);
}

void _TIFFSwab64BitData(TIFF *tif, uint8_t *buf, tmsize_t cc)
{
    (void)tif;
    assert((cc & 7) == 0);
    TIFFSwabArrayOfDouble((double *)buf, cc / 8);
}

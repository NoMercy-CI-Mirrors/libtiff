/* WARNING: The type of JPEG encapsulation defined by the TIFF Version 6.0
   specification is now totally obsolete and deprecated for new applications and
   images. This file was was created solely in order to read unconverted images
   still present on some users' computer systems. It will never be extended
   to write such files. Writing new-style JPEG compressed TIFFs is implemented
   in tif_jpeg.c.

   The code is carefully crafted to robustly read all gathered JPEG-in-TIFF
   testfiles, and anticipate as much as possible all other... But still, it may
   fail on some. If you encounter problems, please report them on the TIFF
   mailing list and/or to Joris Van Damme <info@awaresystems.be>.

   Please read the file called "TIFF Technical Note #2" if you need to be
   convinced this compression scheme is bad and breaks TIFF. That document
   is linked to from the LibTiff site <http://www.remotesensing.org/libtiff/>
   and from AWare Systems' TIFF section
   <http://www.awaresystems.be/imaging/tiff.html>. It is also absorbed
   in Adobe's specification supplements, marked "draft" up to this day, but
   supported by the TIFF community.

   This file interfaces with Release 6B of the JPEG Library written by the
   Independent JPEG Group. Previous versions of this file required a hack inside
   the LibJpeg library. This version no longer requires that. Remember to
   remove the hack if you update from the old version.

   Copyright (c) Joris Van Damme <info@awaresystems.be>
   Copyright (c) AWare Systems <http://www.awaresystems.be/>

   The licence agreement for this file is the same as the rest of the LibTiff
   library.

   IN NO EVENT SHALL JORIS VAN DAMME OR AWARE SYSTEMS BE LIABLE FOR
   ANY SPECIAL, INCIDENTAL, INDIRECT OR CONSEQUENTIAL DAMAGES OF ANY KIND,
   OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
   WHETHER OR NOT ADVISED OF THE POSSIBILITY OF DAMAGE, AND ON ANY THEORY OF
   LIABILITY, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
   OF THIS SOFTWARE.

   Joris Van Damme and/or AWare Systems may be available for custom
   development. If you like what you see, and need anything similar or related,
   contact <info@awaresystems.be>.
*/

/* What is what, and what is not?

   This decoder starts with an input stream, that is essentially the
   JpegInterchangeFormat stream, if any, followed by the strile data, if any.
   This stream is read in OJPEGReadByte and related functions.

   It analyzes the start of this stream, until it encounters non-marker data,
   i.e. compressed image data. Some of the header markers it sees have no actual
   content, like the SOI marker, and APP/COM markers that really shouldn't even
   be there. Some other markers do have content, and the valuable bits and
   pieces of information in these markers are saved, checking all to verify that
   the stream is more or less within expected bounds. This happens inside the
   OJPEGReadHeaderInfoSecStreamXxx functions.

   Some OJPEG imagery contains no valid JPEG header markers. This situation is
   picked up on if we've seen no SOF marker when we're at the start of the
   compressed image data. In this case, the tables are read from JpegXxxTables
   tags, and the other bits and pieces of information is initialized to its most
   basic value. This is implemented in the OJPEGReadHeaderInfoSecTablesXxx
   functions.

   When this is complete, a good and valid JPEG header can be assembled, and
   this is passed through to LibJpeg. When that's done, the remainder of the
   input stream, i.e. the compressed image data, can be passed through
   unchanged. This is done in OJPEGWriteStream functions.

   LibTiff rightly expects to know the subsampling values before decompression.
   Just like in new-style JPEG-in-TIFF, though, or even more so, actually, the
   YCbCrsubsampling tag is notoriously unreliable. To correct these tag values
   with the ones inside the JPEG stream, the first part of the input stream is
   pre-scanned in OJPEGSubsamplingCorrect, making no note of any other data,
   reporting no warnings or errors, up to the point where either these values
   are read, or it's clear they aren't there. This means that some of the data
   is read twice, but we feel speed in correcting these values is important
   enough to warrant this sacrifice. Although there is currently no define or
   other configuration mechanism to disable this behavior, the actual header
   scanning is build to robustly respond with error report if it should
   encounter an uncorrected mismatch of subsampling values. See
   OJPEGReadHeaderInfoSecStreamSof.

   The restart interval and restart markers are the most tricky part... The
   restart interval can be specified in a tag. It can also be set inside the
   input JPEG stream. It can be used inside the input JPEG stream. If reading
   from strile data, we've consistently discovered the need to insert restart
   markers in between the different striles, as is also probably the most likely
   interpretation of the original TIFF 6.0 specification. With all this setting
   of interval, and actual use of markers that is not predictable at the time of
   valid JPEG header assembly, the restart thing may turn out the Achilles heel
   of this implementation. Fortunately, most OJPEG writer vendors succeed in
   reading back what they write, which may be the reason why we've been able to
   discover ways that seem to work.

   Some special provision is made for planarconfig separate OJPEG files. These
   seem to consistently contain header info, a SOS marker, a plane, SOS marker,
   plane, SOS, and plane. This may or may not be a valid JPEG configuration, we
   don't know and don't care. We want LibTiff to be able to access the planes
   individually, without huge buffering inside LibJpeg, anyway. So we compose
   headers to feed to LibJpeg, in this case, that allow us to pass a single
   plane such that LibJpeg sees a valid single-channel JPEG stream. Locating
   subsequent SOS markers, and thus subsequent planes, is done inside
   OJPEGReadSecondarySos.

   The benefit of the scheme is... that it works, basically. We know of no other
   that does. It works without checking software tag, or otherwise going about
   things in an OJPEG flavor specific manner. Instead, it is a single scheme,
   that covers the cases with and without JpegInterchangeFormat, with and
   without striles, with part of the header in JpegInterchangeFormat and
   remainder in first strile, etc. It is forgiving and robust, may likely work
   with OJPEG flavors we've not seen yet, and makes most out of the data.

   Another nice side-effect is that a complete JPEG single valid stream is build
   if planarconfig is not separate (vast majority). We may one day use that to
   build converters to JPEG, and/or to new-style JPEG compression inside TIFF.

   A disadvantage is the lack of random access to the individual striles. This
   is the reason for much of the complicated restart-and-position stuff inside
   OJPEGPreDecode. Applications would do well accessing all striles in order, as
   this will result in a single sequential scan of the input stream, and no
   restarting of LibJpeg decoding session.
*/

/**
 * @file tif_ojpeg.c
 * @brief Decode legacy TIFF JPEG by reconstructing a dependency input stream.
 */

#include "tiffiop.h"
#ifdef OJPEG_SUPPORT

/* Configuration defines here are:
 * JPEG_ENCAP_EXTERNAL: The normal way to call libjpeg, uses longjump. In some
 * environments, like eg LibTiffDelphi, this is not possible. For this reason,
 * the actual calls to libjpeg, with longjump stuff, are encapsulated in
 * dedicated functions. When JPEG_ENCAP_EXTERNAL is defined, these encapsulating
 * functions are declared external to this unit, and can be defined elsewhere to
 * use stuff other then longjump. The default mode, without JPEG_ENCAP_EXTERNAL,
 * implements the call encapsulators here, internally, with normal longjump.
 * SETJMP, LONGJMP, JMP_BUF: On some machines/environments a longjump equivalent
 * is conveniently available, but still it may be worthwhile to use _setjmp or
 * sigsetjmp in place of plain setjmp. These macros will make it easier. It is
 * useless to fiddle with these if you define JPEG_ENCAP_EXTERNAL. OJPEG_BUFFER:
 * Define the size of the desired buffer here. Should be small enough so as to
 * guarantee instant processing, optimal streaming and optimal use of processor
 * cache, but also big enough so as to not result in significant call overhead.
 * It should be at least a few bytes to accommodate some structures (this is
 * verified in asserts), but it would not be sensible to make it this small
 * anyway, and it should be at most 64K since it is indexed with uint16_t. We
 * recommend 2K. EGYPTIANWALK: You could also define EGYPTIANWALK here, but it
 * is not used anywhere and has absolutely no effect. That is why most people
 * insist the EGYPTIANWALK is a bit silly.
 */

/* define LIBJPEG_ENCAP_EXTERNAL */
/**
 * @brief Save the calling environment for a later LONGJMP (plain setjmp).
 * @ingroup libtiff_private_ojpeg
 *
 * Expands to setjmp(jbuf), evaluating jbuf once. Used as the controlling
 * expression of an if in each jpeg_*_encap wrapper on sp->exit_jmpbuf;
 * the saved frame is valid only until that wrapper returns.
 *
 * @param[out] jbuf JMP_BUF lvalue that receives the saved environment.
 * @return 0 when called directly; the nonzero LONGJMP code (always 1 in
 * this file) when control returns here through LONGJMP.
 */
#define SETJMP(jbuf) setjmp(jbuf)
/**
 * @brief Jump back to the frame saved by SETJMP (plain longjmp); never
 * returns.
 * @ingroup libtiff_private_ojpeg
 *
 * Expands to longjmp(jbuf, code). Used only by jpeg_encap_unwind(), with
 * sp->exit_jmpbuf and code 1; control resumes in the jpeg_*_encap wrapper
 * that filled the buffer, which then returns 0. The behaviour is undefined
 * if that wrapper has already returned.
 *
 * @param[in] jbuf JMP_BUF filled by a still-active SETJMP.
 * @param[in] code Value SETJMP returns after the jump (0 is turned into 1
 * by longjmp).
 */
#define LONGJMP(jbuf, code) longjmp(jbuf, code)
/**
 * @brief Type used to store the libjpeg error jump frame.
 * @ingroup libtiff_private_ojpeg
 *
 * Expands to jmp_buf; used for OJPEGState::exit_jmpbuf, which SETJMP fills
 * and LONGJMP unwinds to. Not used when LIBJPEG_ENCAP_EXTERNAL is defined.
 */
#define JMP_BUF jmp_buf
/**
 * @brief Capacity in bytes of the embedded input and synthetic marker buffers.
 * @ingroup libtiff_private_ojpeg
 *
 * Sizes OJPEGState::in_buffer and OJPEGState::out_buffer. Must be at most
 * 65535 because buffer counters are uint16_t, and at least as large as the
 * largest synthetic marker (checked by asserts in the OJPEGWrite* routines).
 */
#define OJPEG_BUFFER 2048
/* define EGYPTIANWALK */

/**
 * @brief JPEG SOF0 (baseline DCT start-of-frame) marker code.
 * @ingroup libtiff_private_ojpeg
 *
 * Marker code byte following 0xFF. Parsed by OJPEGReadHeaderInfoSecStreamSof
 * and used as the SOF marker of the synthetic stream when the stream has no
 * SOF marker.
 */
#define JPEG_MARKER_SOF0 0xC0
/**
 * @brief JPEG SOF1 (extended sequential DCT start-of-frame) marker code.
 * @ingroup libtiff_private_ojpeg
 *
 * Marker code byte following 0xFF; parsed by
 * OJPEGReadHeaderInfoSecStreamSof.
 */
#define JPEG_MARKER_SOF1 0xC1
/**
 * @brief JPEG SOF3 (lossless start-of-frame) marker code.
 * @ingroup libtiff_private_ojpeg
 *
 * Marker code byte following 0xFF; passed to
 * OJPEGReadHeaderInfoSecStreamSof.
 */
#define JPEG_MARKER_SOF3 0xC3
/**
 * @brief JPEG DHT (define Huffman table) marker code.
 * @ingroup libtiff_private_ojpeg
 *
 * Marker code byte following 0xFF; parsed from the stream and used as the
 * marker of the stored Huffman tables, whether taken from the stream or
 * from the JpegDcTables/JpegAcTables tags.
 */
#define JPEG_MARKER_DHT 0xC4
/**
 * @brief JPEG RST0 (restart) marker code.
 * @ingroup libtiff_private_ojpeg
 *
 * Marker code byte following 0xFF. RST0..RST7 are written as
 * JPEG_MARKER_RST0 + restart_index by OJPEGWriteStreamRst.
 */
#define JPEG_MARKER_RST0 0XD0
/**
 * @brief JPEG SOI (start of image) marker code.
 * @ingroup libtiff_private_ojpeg
 *
 * Marker code byte following 0xFF; skipped when parsing and written by
 * OJPEGWriteStreamSoi.
 */
#define JPEG_MARKER_SOI 0xD8
/**
 * @brief JPEG EOI (end of image) marker code.
 * @ingroup libtiff_private_ojpeg
 *
 * Marker code byte following 0xFF; written by OJPEGWriteStreamEoi.
 */
#define JPEG_MARKER_EOI 0xD9
/**
 * @brief JPEG SOS (start of scan) marker code.
 * @ingroup libtiff_private_ojpeg
 *
 * Marker code byte following 0xFF. Ends header parsing and is written by
 * OJPEGWriteStreamSos.
 */
#define JPEG_MARKER_SOS 0xDA
/**
 * @brief JPEG DQT (define quantization table) marker code.
 * @ingroup libtiff_private_ojpeg
 *
 * Marker code byte following 0xFF; parsed from the stream and used as the
 * marker of the stored quantization tables, whether taken from the stream
 * or from the JpegQTables tag.
 */
#define JPEG_MARKER_DQT 0xDB
/**
 * @brief JPEG DRI (define restart interval) marker code.
 * @ingroup libtiff_private_ojpeg
 *
 * Marker code byte following 0xFF; parsed from the stream and written by
 * OJPEGWriteStreamDri.
 */
#define JPEG_MARKER_DRI 0xDD
/**
 * @brief JPEG APP0 marker code; APP0..APP15 are this value plus 0..15.
 * @ingroup libtiff_private_ojpeg
 *
 * Marker code byte following 0xFF. Application segments are skipped when
 * parsing.
 */
#define JPEG_MARKER_APP0 0xE0
/**
 * @brief JPEG COM (comment) marker code.
 * @ingroup libtiff_private_ojpeg
 *
 * Marker code byte following 0xFF. Comment segments are skipped when
 * parsing.
 */
#define JPEG_MARKER_COM 0xFE

/**
 * @brief TIFFField field bit for the codec-private JpegInterchangeFormat tag.
 * @ingroup libtiff_private_ojpeg
 *
 * Set by OJPEGVSetField when the tag is stored; tested with TIFFFieldSet
 * by OJPEGPrintDir.
 */
#define FIELD_OJPEG_JPEGINTERCHANGEFORMAT (FIELD_CODEC + 0)
/**
 * @brief TIFFField field bit for the codec-private JpegInterchangeFormatLength
 * tag.
 * @ingroup libtiff_private_ojpeg
 *
 * Set by OJPEGVSetField when the tag is stored; tested with TIFFFieldSet
 * by OJPEGPrintDir.
 */
#define FIELD_OJPEG_JPEGINTERCHANGEFORMATLENGTH (FIELD_CODEC + 1)
/**
 * @brief TIFFField field bit for the codec-private JpegQTables tag.
 * @ingroup libtiff_private_ojpeg
 *
 * Set by OJPEGVSetField when the tag is stored; tested with TIFFFieldSet
 * by OJPEGPrintDir.
 */
#define FIELD_OJPEG_JPEGQTABLES (FIELD_CODEC + 2)
/**
 * @brief TIFFField field bit for the codec-private JpegDcTables tag.
 * @ingroup libtiff_private_ojpeg
 *
 * Set by OJPEGVSetField when the tag is stored; tested with TIFFFieldSet
 * by OJPEGPrintDir.
 */
#define FIELD_OJPEG_JPEGDCTABLES (FIELD_CODEC + 3)
/**
 * @brief TIFFField field bit for the codec-private JpegAcTables tag.
 * @ingroup libtiff_private_ojpeg
 *
 * Set by OJPEGVSetField when the tag is stored; tested with TIFFFieldSet
 * by OJPEGPrintDir.
 */
#define FIELD_OJPEG_JPEGACTABLES (FIELD_CODEC + 4)
/**
 * @brief TIFFField field bit for the codec-private JpegProc tag.
 * @ingroup libtiff_private_ojpeg
 *
 * Set by OJPEGVSetField when the tag is stored; tested with TIFFFieldSet
 * by OJPEGPrintDir.
 */
#define FIELD_OJPEG_JPEGPROC (FIELD_CODEC + 5)
/**
 * @brief TIFFField field bit for the codec-private JpegRestartInterval tag.
 * @ingroup libtiff_private_ojpeg
 *
 * Set by OJPEGVSetField when the tag is stored; tested with TIFFFieldSet
 * by OJPEGPrintDir.
 */
#define FIELD_OJPEG_JPEGRESTARTINTERVAL (FIELD_CODEC + 6)

/**
 * @brief Describe legacy JPEG tags merged into the handle.
 * @ingroup libtiff_private_ojpeg
 *
 * Immutable library-lifetime metadata merged into a handle; no caller
 * ownership transfer.
 */
static const TIFFField ojpegFields[] = {
    {TIFFTAG_JPEGIFOFFSET, 1, 1, TIFF_LONG8, 0, TIFF_SETGET_UINT64,
     FIELD_OJPEG_JPEGINTERCHANGEFORMAT, TRUE, FALSE, "JpegInterchangeFormat",
     NULL},
    {TIFFTAG_JPEGIFBYTECOUNT, 1, 1, TIFF_LONG8, 0, TIFF_SETGET_UINT64,
     FIELD_OJPEG_JPEGINTERCHANGEFORMATLENGTH, TRUE, FALSE,
     "JpegInterchangeFormatLength", NULL},
    {TIFFTAG_JPEGQTABLES, TIFF_VARIABLE2, TIFF_VARIABLE2, TIFF_LONG8, 0,
     TIFF_SETGET_C32_UINT64, FIELD_OJPEG_JPEGQTABLES, FALSE, TRUE,
     "JpegQTables", NULL},
    {TIFFTAG_JPEGDCTABLES, TIFF_VARIABLE2, TIFF_VARIABLE2, TIFF_LONG8, 0,
     TIFF_SETGET_C32_UINT64, FIELD_OJPEG_JPEGDCTABLES, FALSE, TRUE,
     "JpegDcTables", NULL},
    {TIFFTAG_JPEGACTABLES, TIFF_VARIABLE2, TIFF_VARIABLE2, TIFF_LONG8, 0,
     TIFF_SETGET_C32_UINT64, FIELD_OJPEG_JPEGACTABLES, FALSE, TRUE,
     "JpegAcTables", NULL},
    {TIFFTAG_JPEGPROC, 1, 1, TIFF_SHORT, 0, TIFF_SETGET_UINT16,
     FIELD_OJPEG_JPEGPROC, FALSE, FALSE, "JpegProc", NULL},
    {TIFFTAG_JPEGRESTARTINTERVAL, 1, 1, TIFF_SHORT, 0, TIFF_SETGET_UINT16,
     FIELD_OJPEG_JPEGRESTARTINTERVAL, FALSE, FALSE, "JpegRestartInterval",
     NULL},
};

#ifndef LIBJPEG_ENCAP_EXTERNAL
#include <setjmp.h>
#endif

#include "jerror.h"
#include "jpeglib.h"

/**
 * @brief Alias for the libjpeg source-manager structure.
 * @ingroup libtiff_private_ojpeg
 *
 * Embedded in OJPEGState as libjpeg_jpeg_source_mgr; its callbacks feed
 * libjpeg the reconstructed stream produced by OJPEGWriteStream.
 */
typedef struct jpeg_source_mgr tiff_ojpeg_source_mgr;

/**
 * @brief Alias for the libjpeg error-manager structure.
 * @ingroup libtiff_private_ojpeg
 *
 * Embedded in OJPEGState as libjpeg_jpeg_error_mgr.
 */
typedef struct jpeg_error_mgr tiff_ojpeg_error_mgr;

/**
 * @brief Alias for the libjpeg common context structure.
 * @ingroup libtiff_private_ojpeg
 *
 * Parameter type of the error-manager callbacks; its client_data points to
 * the owning TIFF handle.
 */
typedef struct jpeg_common_struct tiff_ojpeg_common_struct;
/**
 * @brief Alias for the libjpeg decompressor context structure.
 * @ingroup libtiff_private_ojpeg
 *
 * Embedded in OJPEGState as libjpeg_jpeg_decompress_struct; the libjpeg
 * session it holds is destroyed by OJPEGLibjpegSessionAbort.
 */
typedef struct jpeg_decompress_struct tiff_ojpeg_decompress_struct;

/**
 * @brief Source of the bytes currently read into the input buffer.
 * @ingroup libtiff_private_ojpeg
 *
 * Stored in OJPEGState::in_buffer_source and advanced by
 * OJPEGReadBufferFill from the interchange-format extent to the striles.
 */
typedef enum
{
    /** @brief No input extent selected yet. */
    osibsNotSetYet,
    /** @brief Read bytes from the interchange-format extent. */
    osibsJpegInterchangeFormat,
    /** @brief Read bytes from the current strip or tile extent. */
    osibsStrile,
    /** @brief No further input extents remain. */
    osibsEof
} OJPEGStateInBufferSource;

/**
 * @brief Next item to emit in the reconstructed JPEG stream.
 * @ingroup libtiff_private_ojpeg
 *
 * Stored in OJPEGState::out_state. OJPEGWriteStream emits items in
 * declaration order, skipping absent tables.
 */
typedef enum
{
    /** @brief Emit the synthetic start-of-image marker. */
    ososSoi,
    /** @brief Emit quantisation table slot 0 when present. */
    ososQTable0,
    /** @brief Emit quantisation table slot 1 when present. */
    ososQTable1,
    /** @brief Emit quantisation table slot 2 when present. */
    ososQTable2,
    /** @brief Emit quantisation table slot 3 when present. */
    ososQTable3,
    /** @brief Emit DC Huffman table slot 0 when present. */
    ososDcTable0,
    /** @brief Emit DC Huffman table slot 1 when present. */
    ososDcTable1,
    /** @brief Emit DC Huffman table slot 2 when present. */
    ososDcTable2,
    /** @brief Emit DC Huffman table slot 3 when present. */
    ososDcTable3,
    /** @brief Emit AC Huffman table slot 0 when present. */
    ososAcTable0,
    /** @brief Emit AC Huffman table slot 1 when present. */
    ososAcTable1,
    /** @brief Emit AC Huffman table slot 2 when present. */
    ososAcTable2,
    /** @brief Emit AC Huffman table slot 3 when present. */
    ososAcTable3,
    /** @brief Emit the cached restart interval. */
    ososDri,
    /** @brief Emit the reconstructed frame header. */
    ososSof,
    /** @brief Emit the current scan header. */
    ososSos,
    /** @brief Borrow the next compressed input chunk. */
    ososCompressed,
    /** @brief Emit the next cyclic restart marker. */
    ososRst,
    /** @brief Emit the synthetic end-of-image marker. */
    ososEoi
} OJPEGStateOutState;

/**
 * @brief Own old-style JPEG reconstruction tables buffers and dependency
 * sessions.
 * @ingroup libtiff_private_ojpeg
 *
 * Allocated by TIFFInitOJPEG and freed by cleanup. All parent
 * methods/handle references and interior buffer views are borrowed; table
 * blobs, combined samples, row pointers and skip storage use Ext
 * allocation. Serialize access; failures can retain partial state.
 */
typedef struct
{
    /** @brief Borrowed owning TIFF handle. */
    TIFF *tif;
    /**
     * @brief Latched nonzero by the first successful predecode; never cleared.
     */
    int decoder_ok;
    /** @brief Latched raw decode error until reset. */
    int error_in_raw_data_decoding;
#ifndef LIBJPEG_ENCAP_EXTERNAL
    /** @brief Active synchronous dependency error frame. */
    JMP_BUF exit_jmpbuf;
#endif
    /** @brief Borrowed saved parent getter. */
    TIFFVGetMethod vgetparent;
    /** @brief Borrowed saved parent setter. */
    TIFFVSetMethod vsetparent;
    /** @brief Borrowed saved parent printer. */
    TIFFPrintMethod printdir;
    /** @brief Cached client file extent. */
    uint64_t file_size;
    /** @brief Cached image width in samples. */
    uint32_t image_width;
    /** @brief Cached image height in rows. */
    uint32_t image_length;
    /** @brief Cached strile width. */
    uint32_t strile_width;
    /** @brief Cached strile height. */
    uint32_t strile_length;
    /** @brief Height of reconstructed strile stream. */
    uint32_t strile_length_total;
    /** @brief Cached component count, at most three. */
    uint8_t samples_per_pixel;
    /** @brief First component of the selected scan/plane. */
    uint8_t plane_sample_offset;
    /** @brief Components in the current scan. */
    uint8_t samples_per_pixel_per_plane;
    /** @brief Cached interchange stream offset. */
    uint64_t jpeg_interchange_format;
    /** @brief Normalized interchange byte extent. */
    uint64_t jpeg_interchange_format_length;
    /** @brief Cached JPEG process tag. */
    uint8_t jpeg_proc;
    /** @brief Nonzero during sampling-only header probing. */
    uint8_t subsamplingcorrect;
    /** @brief Nonzero after the one-shot correction attempt. */
    uint8_t subsamplingcorrect_done;
    /** @brief Nonzero when sampling was explicitly tagged. */
    uint8_t subsampling_tag;
    /** @brief Cached horizontal Y sampling factor. */
    uint8_t subsampling_hor;
    /** @brief Cached vertical Y sampling factor. */
    uint8_t subsampling_ver;
    /** @brief Nonzero requests ordinary dependency upsampling. */
    uint8_t subsampling_force_desubsampling_inside_decompression;
    /** @brief Number of stored quantisation offsets. */
    uint8_t qtable_offset_count;
    /** @brief Number of stored DC Huffman offsets. */
    uint8_t dctable_offset_count;
    /** @brief Number of stored AC Huffman offsets. */
    uint8_t actable_offset_count;
    /** @brief Embedded quantisation offsets, borrowed by tag getters. */
    uint64_t qtable_offset[3];
    /** @brief Embedded DC Huffman offsets, borrowed by getters. */
    uint64_t dctable_offset[3];
    /** @brief Embedded AC Huffman offsets, borrowed by getters. */
    uint64_t actable_offset[3];
    /** @brief Owned Ext length-prefixed quantisation marker blobs. */
    uint8_t *qtable[4];
    /** @brief Owned Ext length-prefixed DC Huffman marker blobs. */
    uint8_t *dctable[4];
    /** @brief Owned Ext length-prefixed AC Huffman marker blobs. */
    uint8_t *actable[4];
    /** @brief Cached restart MCU interval. */
    uint16_t restart_interval;
    /** @brief Next cyclic restart marker index. */
    uint8_t restart_index;
    /** @brief Nonzero when frame details are cached. */
    uint8_t sof_log;
    /** @brief Cached SOF marker type. */
    uint8_t sof_marker_id;
    /** @brief Cached frame width. */
    uint32_t sof_x;
    /** @brief Cached frame height. */
    uint32_t sof_y;
    /** @brief Frame component identifiers. */
    uint8_t sof_c[3];
    /** @brief Packed per-component sampling factors. */
    uint8_t sof_hv[3];
    /** @brief Frame quantisation selectors. */
    uint8_t sof_tq[3];
    /** @brief Scan component selectors. */
    uint8_t sos_cs[3];
    /** @brief Packed scan DC/AC table selectors. */
    uint8_t sos_tda[3];
    /** @brief Save the input position following one plane scan header. */
    struct
    {
        /** @brief Nonzero when this scan snapshot is valid. */
        uint8_t log;
        /** @brief Current/snapshotted interchange or strile input source. */
        OJPEGStateInBufferSource in_buffer_source;
        /** @brief Next/snapshotted strile input index. */
        uint32_t in_buffer_next_strile;
        /** @brief Current/snapshotted next refill offset. */
        uint64_t in_buffer_file_pos;
        /** @brief Current/snapshotted remaining bounded extent. */
        uint64_t in_buffer_file_togo;
    } sos_end[3];
    /**< @brief Embedded per-plane snapshots of input state after SOS. */
    /** @brief Nonzero after initial header caching. */
    uint8_t readheader_done;
    /** @brief Nonzero after reconstructed decoder session setup. */
    uint8_t writeheader_done;
    /** @brief Current reconstructed separate-plane sample. */
    uint16_t write_cursample;
    /** @brief Current reconstructed strile counter. */
    uint32_t write_curstrile;
    /** @brief Nonzero while dependency context needs destruction. */
    uint8_t libjpeg_session_active;
    /** @brief Zero for raw planes, one for scanlines. */
    uint8_t libjpeg_jpeg_query_style;
    /** @brief Embedded dependency diagnostic callbacks. */
    tiff_ojpeg_error_mgr libjpeg_jpeg_error_mgr;
    /** @brief Embedded context owning dependency session resources. */
    tiff_ojpeg_decompress_struct libjpeg_jpeg_decompress_struct;
    /** @brief Embedded source manager borrowing reconstruction chunks. */
    tiff_ojpeg_source_mgr libjpeg_jpeg_source_mgr;
    /** @brief Nonzero after conversion geometry/storage setup. */
    uint8_t subsampling_convert_log;
    /** @brief Allocated Y-plane bytes per padded row. */
    uint32_t subsampling_convert_ylinelen;
    /** @brief Y rows per raw MCU group. */
    uint32_t subsampling_convert_ylines;
    /** @brief Allocated chroma-plane bytes per row. */
    uint32_t subsampling_convert_clinelen;
    /** @brief Chroma rows per raw MCU group. */
    uint32_t subsampling_convert_clines;
    /** @brief Y-plane byte capacity. */
    uint32_t subsampling_convert_ybuflen;
    /** @brief Each chroma-plane byte capacity. */
    uint32_t subsampling_convert_cbuflen;
    /** @brief Combined sample allocation byte capacity. */
    uint32_t subsampling_convert_ycbcrbuflen;
    /** @brief Owned Ext combined Y/Cb/Cr sample allocation. */
    uint8_t *subsampling_convert_ycbcrbuf;
    /** @brief Borrowed Y view into the combined allocation. */
    uint8_t *subsampling_convert_ybuf;
    /** @brief Borrowed Cb view into the combined allocation. */
    uint8_t *subsampling_convert_cbbuf;
    /** @brief Borrowed Cr view into the combined allocation. */
    uint8_t *subsampling_convert_crbuf;
    /** @brief Owned pointer-array entry count. */
    uint32_t subsampling_convert_ycbcrimagelen;
    /**
     * @brief Owned Ext dependency plane/row pointer allocation, borrowing
     * sample storage.
     */
    uint8_t **subsampling_convert_ycbcrimage;
    /** @brief Output chroma samples per clump row. */
    uint32_t subsampling_convert_clinelenout;
    /** @brief Next chroma row in the current MCU group. */
    uint32_t subsampling_convert_state;
    /**
     * @brief Output bytes per ordinary row or subsampling_ver-row clump unit.
     */
    uint32_t bytes_per_line;
    /** @brief Output ordinary rows or clump units per strile. */
    uint32_t lines_per_strile;
    /** @brief Current/snapshotted interchange or strile input source. */
    OJPEGStateInBufferSource in_buffer_source;
    /** @brief Next/snapshotted strile input index. */
    uint32_t in_buffer_next_strile;
    /** @brief Total input strile count. */
    uint32_t in_buffer_strile_count;
    /** @brief Current/snapshotted next refill offset. */
    uint64_t in_buffer_file_pos;
    /** @brief Nonzero when client position matches refill offset. */
    uint8_t in_buffer_file_pos_log;
    /** @brief Current/snapshotted remaining bounded extent. */
    uint64_t in_buffer_file_togo;
    /** @brief Unread bytes in the embedded input window. */
    uint16_t in_buffer_togo;
    /** @brief Borrowed cursor into in_buffer. */
    uint8_t *in_buffer_cur;
    /** @brief Embedded client input window. */
    uint8_t in_buffer[OJPEG_BUFFER];
    /** @brief Next synthetic output state. */
    OJPEGStateOutState out_state;
    /** @brief Embedded synthetic marker storage, borrowed by source manager. */
    uint8_t out_buffer[OJPEG_BUFFER];
    /** @brief Owned Ext discarded-scanline scratch allocation. */
    uint8_t *skip_buffer;
} OJPEGState;

static int OJPEGVGetField(TIFF *tif, uint32_t tag, va_list ap);
static int OJPEGVSetField(TIFF *tif, uint32_t tag, va_list ap);
static void OJPEGPrintDir(TIFF *tif, FILE *fd, long flags);

static int OJPEGFixupTags(TIFF *tif);
static int OJPEGSetupDecode(TIFF *tif);
static int OJPEGPreDecode(TIFF *tif, uint16_t s);
static int OJPEGPreDecodeSkipRaw(TIFF *tif);
static int OJPEGPreDecodeSkipScanlines(TIFF *tif);
static int OJPEGDecode(TIFF *tif, uint8_t *buf, tmsize_t cc, uint16_t s);
static int OJPEGDecodeRaw(TIFF *tif, uint8_t *buf, tmsize_t cc);
static int OJPEGDecodeScanlines(TIFF *tif, uint8_t *buf, tmsize_t cc);
static void OJPEGPostDecode(TIFF *tif, uint8_t *buf, tmsize_t cc);
static int OJPEGSetupEncode(TIFF *tif);
static int OJPEGPreEncode(TIFF *tif, uint16_t s);
static int OJPEGEncode(TIFF *tif, uint8_t *buf, tmsize_t cc, uint16_t s);
static int OJPEGPostEncode(TIFF *tif);
static void OJPEGCleanup(TIFF *tif);

static void OJPEGSubsamplingCorrect(TIFF *tif);
static int OJPEGReadHeaderInfo(TIFF *tif);
static int OJPEGReadSecondarySos(TIFF *tif, uint16_t s);
static int OJPEGWriteHeaderInfo(TIFF *tif);
static void OJPEGLibjpegSessionAbort(TIFF *tif);

static int OJPEGReadHeaderInfoSec(TIFF *tif);
static int OJPEGReadHeaderInfoSecStreamDri(TIFF *tif);
static int OJPEGReadHeaderInfoSecStreamDqt(TIFF *tif);
static int OJPEGReadHeaderInfoSecStreamDht(TIFF *tif);
static int OJPEGReadHeaderInfoSecStreamSof(TIFF *tif, uint8_t marker_id);
static int OJPEGReadHeaderInfoSecStreamSos(TIFF *tif);
static int OJPEGReadHeaderInfoSecTablesQTable(TIFF *tif);
static int OJPEGReadHeaderInfoSecTablesDcTable(TIFF *tif);
static int OJPEGReadHeaderInfoSecTablesAcTable(TIFF *tif);

static int OJPEGReadBufferFill(OJPEGState *sp);
static int OJPEGReadByte(OJPEGState *sp, uint8_t *byte);
static int OJPEGReadBytePeek(OJPEGState *sp, uint8_t *byte);
static void OJPEGReadByteAdvance(OJPEGState *sp);
static int OJPEGReadWord(OJPEGState *sp, uint16_t *word);
static int OJPEGReadBlock(OJPEGState *sp, uint16_t len, void *mem);
static void OJPEGReadSkip(OJPEGState *sp, uint16_t len);

static int OJPEGWriteStream(TIFF *tif, void **mem, uint32_t *len);
static void OJPEGWriteStreamSoi(TIFF *tif, void **mem, uint32_t *len);
static void OJPEGWriteStreamQTable(TIFF *tif, uint8_t table_index, void **mem,
                                   uint32_t *len);
static void OJPEGWriteStreamDcTable(TIFF *tif, uint8_t table_index, void **mem,
                                    uint32_t *len);
static void OJPEGWriteStreamAcTable(TIFF *tif, uint8_t table_index, void **mem,
                                    uint32_t *len);
static void OJPEGWriteStreamDri(TIFF *tif, void **mem, uint32_t *len);
static void OJPEGWriteStreamSof(TIFF *tif, void **mem, uint32_t *len);
static void OJPEGWriteStreamSos(TIFF *tif, void **mem, uint32_t *len);
static int OJPEGWriteStreamCompressed(TIFF *tif, void **mem, uint32_t *len);
static void OJPEGWriteStreamRst(TIFF *tif, void **mem, uint32_t *len);
static void OJPEGWriteStreamEoi(TIFF *tif, void **mem, uint32_t *len);

#ifdef LIBJPEG_ENCAP_EXTERNAL
/**
 * @brief Trap dependency fatal errors while attempting to create a
 * decompressor.
 * @ingroup libtiff_private_ojpeg
 *
 * Requires cinfo->err and cinfo->client_data to be set; libjpeg clears the rest
 * of the context, so the source manager is installed after this call succeeds.
 * Defined outside this unit; must call jpeg_create_decompress() and return 0 if
 * libjpeg raises a fatal error through jpeg_encap_unwind. Any state libjpeg
 * allocated before the error remains in cinfo.
 *
 * @param[in,out] sp Codec state for the handle.
 * @param[in,out] cinfo libjpeg decompressor context, normally
 * sp->libjpeg_jpeg_decompress_struct.
 * @return One if the dependency returns normally, zero after a fatal error
 * jump; not the dependency operation result or row count.
 */
extern int jpeg_create_decompress_encap(OJPEGState *sp,
                                        tiff_ojpeg_decompress_struct *cinfo);
/**
 * @brief Trap dependency fatal errors while attempting to read a JPEG header.
 * @ingroup libtiff_private_ojpeg
 *
 * Requires a context created by jpeg_create_decompress_encap with the source
 * manager installed. Defined outside this unit; must call jpeg_read_header()
 * and return 0 if libjpeg raises a fatal error through jpeg_encap_unwind. The
 * libjpeg return value is discarded. State libjpeg changed before an error
 * remains in cinfo.
 *
 * @param[in,out] sp Codec state for the handle.
 * @param[in,out] cinfo libjpeg decompressor context, normally
 * sp->libjpeg_jpeg_decompress_struct.
 * @param[in] require_image Nonzero requires an image header rather than
 * tables only.
 * @return One if the dependency returns normally, zero after a fatal error
 * jump; not the dependency operation result or row count.
 */
extern int jpeg_read_header_encap(OJPEGState *sp,
                                  tiff_ojpeg_decompress_struct *cinfo,
                                  uint8_t require_image);
/**
 * @brief Trap dependency fatal errors while attempting to start decompression.
 * @ingroup libtiff_private_ojpeg
 *
 * Requires a context created by jpeg_create_decompress_encap with the source
 * manager installed. Defined outside this unit; must call
 * jpeg_start_decompress() and return 0 if libjpeg raises a fatal error through
 * jpeg_encap_unwind. The libjpeg return value is discarded. State libjpeg
 * changed before an error remains in cinfo.
 *
 * @param[in,out] sp Codec state for the handle.
 * @param[in,out] cinfo libjpeg decompressor context, normally
 * sp->libjpeg_jpeg_decompress_struct.
 * @return One if the dependency returns normally, zero after a fatal error
 * jump; not the dependency operation result or row count.
 */
extern int jpeg_start_decompress_encap(OJPEGState *sp,
                                       tiff_ojpeg_decompress_struct *cinfo);
/**
 * @brief Trap dependency fatal errors while attempting to read scanlines.
 * @ingroup libtiff_private_ojpeg
 *
 * Requires a context created by jpeg_create_decompress_encap with the source
 * manager installed. Defined outside this unit; must call jpeg_read_scanlines()
 * and return 0 if libjpeg raises a fatal error through jpeg_encap_unwind. The
 * libjpeg return value is discarded. State libjpeg changed before an error
 * remains in cinfo.
 *
 * @param[in,out] sp Codec state for the handle.
 * @param[in,out] cinfo libjpeg decompressor context, normally
 * sp->libjpeg_jpeg_decompress_struct.
 * @param[out] scanlines Borrowed dependency-compatible writable row-pointer
 * array and sample storage for max_lines rows.
 * @param[in] max_lines Maximum number of rows to read; the codec always
 * passes 1.
 * @return One if the dependency returns normally, zero after a fatal error
 * jump; not the dependency operation result or row count.
 */
extern int jpeg_read_scanlines_encap(OJPEGState *sp,
                                     tiff_ojpeg_decompress_struct *cinfo,
                                     void *scanlines, uint32_t max_lines);
/**
 * @brief Trap dependency fatal errors while attempting to read component
 * planes.
 * @ingroup libtiff_private_ojpeg
 *
 * Requires a context created by jpeg_create_decompress_encap with the source
 * manager installed. Defined outside this unit; must call jpeg_read_raw_data()
 * and return 0 if libjpeg raises a fatal error through jpeg_encap_unwind. The
 * libjpeg return value is discarded. State libjpeg changed before an error
 * remains in cinfo.
 *
 * @param[in,out] sp Codec state for the handle.
 * @param[in,out] cinfo libjpeg decompressor context, normally
 * sp->libjpeg_jpeg_decompress_struct.
 * @param[out] data Borrowed dependency-compatible writable component/row
 * arrays and storage for max_lines rows.
 * @param[in] max_lines Number of rows to read; the codec passes
 * subsampling_ver * 8 (one MCU row).
 * @return One if the dependency returns normally, zero after a fatal error
 * jump; not the dependency operation result or row count.
 */
extern int jpeg_read_raw_data_encap(OJPEGState *sp,
                                    tiff_ojpeg_decompress_struct *cinfo,
                                    void *data, uint32_t max_lines);
/**
 * @brief Jump to the active old-style JPEG dependency wrapper.
 * @ingroup libtiff_private_ojpeg
 *
 * Requires a live synchronous encapsulation jump frame for this handle on
 * the same thread. Performs a nonlocal return; never returns normally,
 * frees no state and supplies no additional diagnostic. An external
 * encapsulation build must provide the corresponding implementation.
 *
 * @param[in] tif Handle whose codec state identifies the active wrapper
 * frame.
 */
extern void jpeg_encap_unwind(TIFF *tif);
#else
/**
 * @brief Trap dependency fatal errors while attempting to create a
 * decompressor.
 * @ingroup libtiff_private_ojpeg
 *
 * Requires cinfo->err and cinfo->client_data to be set; libjpeg clears the rest
 * of the context, so the source manager is installed after this call succeeds.
 * Sets sp->exit_jmpbuf with SETJMP and calls jpeg_create_decompress(); a
 * libjpeg fatal error returns here through jpeg_encap_unwind. Any state libjpeg
 * allocated before the error remains in cinfo.
 *
 * @param[in,out] sp Codec state whose exit_jmpbuf holds the jump frame.
 * @param[in,out] cinfo libjpeg decompressor context, normally
 * sp->libjpeg_jpeg_decompress_struct.
 * @return One if the dependency returns normally, zero after a fatal error
 * jump; not the dependency operation result or row count.
 */
static int jpeg_create_decompress_encap(OJPEGState *sp,
                                        tiff_ojpeg_decompress_struct *cinfo);
/**
 * @brief Trap dependency fatal errors while attempting to read a JPEG header.
 * @ingroup libtiff_private_ojpeg
 *
 * Requires a context created by jpeg_create_decompress_encap with the source
 * manager installed. Sets sp->exit_jmpbuf with SETJMP and calls
 * jpeg_read_header(); a libjpeg fatal error returns here through
 * jpeg_encap_unwind. The libjpeg return value is discarded. State libjpeg
 * changed before an error remains in cinfo.
 *
 * @param[in,out] sp Codec state whose exit_jmpbuf holds the jump frame.
 * @param[in,out] cinfo libjpeg decompressor context, normally
 * sp->libjpeg_jpeg_decompress_struct.
 * @param[in] require_image Nonzero requires an image header rather than
 * tables only.
 * @return One if the dependency returns normally, zero after a fatal error
 * jump; not the dependency operation result or row count.
 */
static int jpeg_read_header_encap(OJPEGState *sp,
                                  tiff_ojpeg_decompress_struct *cinfo,
                                  uint8_t require_image);
/**
 * @brief Trap dependency fatal errors while attempting to start decompression.
 * @ingroup libtiff_private_ojpeg
 *
 * Requires a context created by jpeg_create_decompress_encap with the source
 * manager installed. Sets sp->exit_jmpbuf with SETJMP and calls
 * jpeg_start_decompress(); a libjpeg fatal error returns here through
 * jpeg_encap_unwind. The libjpeg return value is discarded. State libjpeg
 * changed before an error remains in cinfo.
 *
 * @param[in,out] sp Codec state whose exit_jmpbuf holds the jump frame.
 * @param[in,out] cinfo libjpeg decompressor context, normally
 * sp->libjpeg_jpeg_decompress_struct.
 * @return One if the dependency returns normally, zero after a fatal error
 * jump; not the dependency operation result or row count.
 */
static int jpeg_start_decompress_encap(OJPEGState *sp,
                                       tiff_ojpeg_decompress_struct *cinfo);
/**
 * @brief Trap dependency fatal errors while attempting to read scanlines.
 * @ingroup libtiff_private_ojpeg
 *
 * Requires a context created by jpeg_create_decompress_encap with the source
 * manager installed. Sets sp->exit_jmpbuf with SETJMP and calls
 * jpeg_read_scanlines(); a libjpeg fatal error returns here through
 * jpeg_encap_unwind. The libjpeg return value is discarded. State libjpeg
 * changed before an error remains in cinfo.
 *
 * @param[in,out] sp Codec state whose exit_jmpbuf holds the jump frame.
 * @param[in,out] cinfo libjpeg decompressor context, normally
 * sp->libjpeg_jpeg_decompress_struct.
 * @param[out] scanlines Borrowed dependency-compatible writable row-pointer
 * array and sample storage for max_lines rows.
 * @param[in] max_lines Maximum number of rows to read; the codec always
 * passes 1.
 * @return One if the dependency returns normally, zero after a fatal error
 * jump; not the dependency operation result or row count.
 */
static int jpeg_read_scanlines_encap(OJPEGState *sp,
                                     tiff_ojpeg_decompress_struct *cinfo,
                                     void *scanlines, uint32_t max_lines);
/**
 * @brief Trap dependency fatal errors while attempting to read component
 * planes.
 * @ingroup libtiff_private_ojpeg
 *
 * Requires a context created by jpeg_create_decompress_encap with the source
 * manager installed. Sets sp->exit_jmpbuf with SETJMP and calls
 * jpeg_read_raw_data(); a libjpeg fatal error returns here through
 * jpeg_encap_unwind. The libjpeg return value is discarded. State libjpeg
 * changed before an error remains in cinfo.
 *
 * @param[in,out] sp Codec state whose exit_jmpbuf holds the jump frame.
 * @param[in,out] cinfo libjpeg decompressor context, normally
 * sp->libjpeg_jpeg_decompress_struct.
 * @param[out] data Borrowed dependency-compatible writable component/row
 * arrays and storage for max_lines rows.
 * @param[in] max_lines Number of rows to read; the codec passes
 * subsampling_ver * 8 (one MCU row).
 * @return One if the dependency returns normally, zero after a fatal error
 * jump; not the dependency operation result or row count.
 */
static int jpeg_read_raw_data_encap(OJPEGState *sp,
                                    tiff_ojpeg_decompress_struct *cinfo,
                                    void *data, uint32_t max_lines);
/**
 * @brief Jump to the active old-style JPEG dependency wrapper.
 * @ingroup libtiff_private_ojpeg
 *
 * Requires a live synchronous encapsulation jump frame for this handle on
 * the same thread. Performs a nonlocal return; never returns normally,
 * frees no state and supplies no additional diagnostic. An external
 * encapsulation build must provide the corresponding implementation.
 *
 * @param[in] tif Handle whose codec state identifies the active wrapper
 * frame.
 */
static void jpeg_encap_unwind(TIFF *tif);
#endif

static void
OJPEGLibjpegJpegErrorMgrOutputMessage(tiff_ojpeg_common_struct *cinfo);
static void OJPEGLibjpegJpegErrorMgrErrorExit(tiff_ojpeg_common_struct *cinfo);
static void
OJPEGLibjpegJpegSourceMgrInitSource(tiff_ojpeg_decompress_struct *cinfo);
static boolean
OJPEGLibjpegJpegSourceMgrFillInputBuffer(tiff_ojpeg_decompress_struct *cinfo);
static void
OJPEGLibjpegJpegSourceMgrSkipInputData(tiff_ojpeg_decompress_struct *cinfo,
                                       long num_bytes);
static boolean
OJPEGLibjpegJpegSourceMgrResyncToRestart(tiff_ojpeg_decompress_struct *cinfo,
                                         int desired);
static void
OJPEGLibjpegJpegSourceMgrTermSource(tiff_ojpeg_decompress_struct *cinfo);

int TIFFInitOJPEG(TIFF *tif, int scheme)
{
    static const char module[] = "TIFFInitOJPEG";
    OJPEGState *sp;

    (void)scheme;
    assert(scheme == COMPRESSION_OJPEG);

    /*
     * Merge codec-specific tag information.
     */
    if (!_TIFFMergeFields(tif, ojpegFields, TIFFArrayCount(ojpegFields)))
    {
        TIFFErrorExtR(tif, module,
                      "Merging Old JPEG codec-specific tags failed");
        return 0;
    }

    /* state block */
    sp = (OJPEGState *)_TIFFmallocExt(tif, sizeof(OJPEGState));
    if (sp == NULL)
    {
        TIFFErrorExtR(tif, module, "No space for OJPEG state block");
        return (0);
    }
    _TIFFmemset(sp, 0, sizeof(OJPEGState));
    sp->tif = tif;
    sp->jpeg_proc = 1;
    sp->subsampling_hor = 2;
    sp->subsampling_ver = 2;
    TIFFSetField(tif, TIFFTAG_YCBCRSUBSAMPLING, 2, 2);
    /* tif codec methods */
    tif->tif_fixuptags = OJPEGFixupTags;
    tif->tif_setupdecode = OJPEGSetupDecode;
    tif->tif_predecode = OJPEGPreDecode;
    tif->tif_postdecode = OJPEGPostDecode;
    tif->tif_decoderow = OJPEGDecode;
    tif->tif_decodestrip = OJPEGDecode;
    tif->tif_decodetile = OJPEGDecode;
    tif->tif_setupencode = OJPEGSetupEncode;
    tif->tif_preencode = OJPEGPreEncode;
    tif->tif_postencode = OJPEGPostEncode;
    tif->tif_encoderow = OJPEGEncode;
    tif->tif_encodestrip = OJPEGEncode;
    tif->tif_encodetile = OJPEGEncode;
    tif->tif_cleanup = OJPEGCleanup;
    tif->tif_data = (uint8_t *)sp;
    /* tif tag methods */
    sp->vgetparent = tif->tif_tagmethods.vgetfield;
    tif->tif_tagmethods.vgetfield = OJPEGVGetField;
    sp->vsetparent = tif->tif_tagmethods.vsetfield;
    tif->tif_tagmethods.vsetfield = OJPEGVSetField;
    sp->printdir = tif->tif_tagmethods.printdir;
    tif->tif_tagmethods.printdir = OJPEGPrintDir;
    /* Some OJPEG files don't have strip or tile offsets or bytecounts tags.
       Some others do, but have totally meaningless or corrupt values
       in these tags. In these cases, the JpegInterchangeFormat stream is
       reliable. In any case, this decoder reads the compressed data itself,
       from the most reliable locations, and we need to notify encapsulating
       LibTiff not to read raw strips or tiles for us. */
    tif->tif_flags |= TIFF_NOREADRAW;
    return (1);
}

/**
 * @brief Retrieve old-style JPEG metadata or delegate to the parent getter.
 * @ingroup libtiff_private_ojpeg
 *
 * IFOFFSET/IFBYTECOUNT consume uint64_t*. YCBCRSUBSAMPLING consumes two
 * uint16_t*; on the first query it runs OJPEGSubsamplingCorrect, which can
 * seek and read the stream header, and it returns the codec-cached factors
 * rather than the directory's td_ycbcrsubsampling, which is not updated.
 * Q/DC/AC table tags consume uint32_t* count then const void** pointing to
 * the embedded uint64_t offset arrays in the codec state; the pointer is
 * valid until the codec state is freed. PROC/RESTART consume uint16_t*.
 * Other tags are passed to the parent getter.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 * @param[in] tag Handled old-style JPEG/related tag or parent-delegated
 * identifier.
 * @param[in,out] ap Live tag-specific va_list cursor, consumed
 * synchronously without retention.
 * @return One for a handled tag, otherwise parent status.
 */
static int OJPEGVGetField(TIFF *tif, uint32_t tag, va_list ap)
{
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    switch (tag)
    {
        case TIFFTAG_JPEGIFOFFSET:
            *va_arg(ap, uint64_t *) = (uint64_t)sp->jpeg_interchange_format;
            break;
        case TIFFTAG_JPEGIFBYTECOUNT:
            *va_arg(ap, uint64_t *) =
                (uint64_t)sp->jpeg_interchange_format_length;
            break;
        case TIFFTAG_YCBCRSUBSAMPLING:
            if (sp->subsamplingcorrect_done == 0)
                OJPEGSubsamplingCorrect(tif);
            *va_arg(ap, uint16_t *) = (uint16_t)sp->subsampling_hor;
            *va_arg(ap, uint16_t *) = (uint16_t)sp->subsampling_ver;
            break;
        case TIFFTAG_JPEGQTABLES:
            *va_arg(ap, uint32_t *) = (uint32_t)sp->qtable_offset_count;
            *va_arg(ap, const void **) = (const void *)sp->qtable_offset;
            break;
        case TIFFTAG_JPEGDCTABLES:
            *va_arg(ap, uint32_t *) = (uint32_t)sp->dctable_offset_count;
            *va_arg(ap, const void **) = (const void *)sp->dctable_offset;
            break;
        case TIFFTAG_JPEGACTABLES:
            *va_arg(ap, uint32_t *) = (uint32_t)sp->actable_offset_count;
            *va_arg(ap, const void **) = (const void *)sp->actable_offset;
            break;
        case TIFFTAG_JPEGPROC:
            *va_arg(ap, uint16_t *) = (uint16_t)sp->jpeg_proc;
            break;
        case TIFFTAG_JPEGRESTARTINTERVAL:
            *va_arg(ap, uint16_t *) = sp->restart_interval;
            break;
        default:
            return (*sp->vgetparent)(tif, tag, ap);
    }
    return (1);
}

/**
 * @brief Cache old-style JPEG tag values or delegate to the parent setter.
 * @ingroup libtiff_private_ojpeg
 *
 * IFOFFSET/IFBYTECOUNT consume uint64_t. SUBSAMPLING consumes two
 * uint16_vap promoted values, narrows to eight bits and updates directory
 * sampling. Q/DC/AC table tags consume uint32_t count: nonzero counts must
 * be at most three, then a borrowed uint64_t* is copied; zero consumes no
 * pointer and preserves prior count. PROC/RESTART consume uint16_vap and
 * narrow to eight/sixteen bits. Marks field/dirty after assignment; later
 * lookup failure need not roll back cached values. Retains no caller
 * storage.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 * @param[in] tag Handled old-style JPEG/related tag or parent-delegated
 * identifier.
 * @param[in,out] ap Live tag-specific va_list cursor, consumed
 * synchronously without retention.
 * @return One after handled tag bookkeeping, zero on rejected count/field
 * lookup, otherwise parent status.
 */
static int OJPEGVSetField(TIFF *tif, uint32_t tag, va_list ap)
{
    static const char module[] = "OJPEGVSetField";
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    uint32_t ma;
    uint64_t *mb;
    uint32_t n;
    const TIFFField *fip;

    switch (tag)
    {
        case TIFFTAG_JPEGIFOFFSET:
            sp->jpeg_interchange_format = (uint64_t)va_arg(ap, uint64_t);
            break;
        case TIFFTAG_JPEGIFBYTECOUNT:
            sp->jpeg_interchange_format_length = (uint64_t)va_arg(ap, uint64_t);
            break;
        case TIFFTAG_YCBCRSUBSAMPLING:
            sp->subsampling_tag = 1;
            sp->subsampling_hor = (uint8_t)va_arg(ap, uint16_vap);
            sp->subsampling_ver = (uint8_t)va_arg(ap, uint16_vap);
            tif->tif_dir.td_ycbcrsubsampling[0] = sp->subsampling_hor;
            tif->tif_dir.td_ycbcrsubsampling[1] = sp->subsampling_ver;
            break;
        case TIFFTAG_JPEGQTABLES:
            ma = (uint32_t)va_arg(ap, uint32_t);
            if (ma != 0)
            {
                if (ma > 3)
                {
                    TIFFErrorExtR(tif, module,
                                  "JpegQTables tag has incorrect count");
                    return (0);
                }
                sp->qtable_offset_count = (uint8_t)ma;
                mb = (uint64_t *)va_arg(ap, uint64_t *);
                for (n = 0; n < ma; n++)
                    sp->qtable_offset[n] = mb[n];
            }
            break;
        case TIFFTAG_JPEGDCTABLES:
            ma = (uint32_t)va_arg(ap, uint32_t);
            if (ma != 0)
            {
                if (ma > 3)
                {
                    TIFFErrorExtR(tif, module,
                                  "JpegDcTables tag has incorrect count");
                    return (0);
                }
                sp->dctable_offset_count = (uint8_t)ma;
                mb = (uint64_t *)va_arg(ap, uint64_t *);
                for (n = 0; n < ma; n++)
                    sp->dctable_offset[n] = mb[n];
            }
            break;
        case TIFFTAG_JPEGACTABLES:
            ma = (uint32_t)va_arg(ap, uint32_t);
            if (ma != 0)
            {
                if (ma > 3)
                {
                    TIFFErrorExtR(tif, module,
                                  "JpegAcTables tag has incorrect count");
                    return (0);
                }
                sp->actable_offset_count = (uint8_t)ma;
                mb = (uint64_t *)va_arg(ap, uint64_t *);
                for (n = 0; n < ma; n++)
                    sp->actable_offset[n] = mb[n];
            }
            break;
        case TIFFTAG_JPEGPROC:
            sp->jpeg_proc = (uint8_t)va_arg(ap, uint16_vap);
            break;
        case TIFFTAG_JPEGRESTARTINTERVAL:
            sp->restart_interval = (uint16_t)va_arg(ap, uint16_vap);
            break;
        default:
            return (*sp->vsetparent)(tif, tag, ap);
    }
    fip = TIFFFieldWithTag(tif, tag);
    if (fip == NULL) /* shouldn't happen */
        return (0);
    TIFFSetFieldBit(tif, fip->field_bit);
    tif->tif_flags |= TIFF_DIRTYDIRECT;
    return (1);
}

/**
 * @brief Print cached old-style JPEG fields before the optional parent printer.
 * @ingroup libtiff_private_ojpeg
 *
 * Writes selected metadata to the borrowed FILE, synchronously invokes the
 * saved parent and ignores stream error status. No pixel decoding,
 * allocation or ownership transfer.
 *
 * @param[in] tif Handle whose codec state supplies the printed values.
 * @param[in,out] fd Borrowed writable FILE, retained by caller.
 * @param[in] flags Print flags forwarded to parent.
 */
static void OJPEGPrintDir(TIFF *tif, FILE *fd, long flags)
{
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    uint8_t m;
    (void)flags;
    assert(sp != NULL);
    if (TIFFFieldSet(tif, FIELD_OJPEG_JPEGINTERCHANGEFORMAT))
        fprintf(fd, "  JpegInterchangeFormat: %" PRIu64 "\n",
                (uint64_t)sp->jpeg_interchange_format);
    if (TIFFFieldSet(tif, FIELD_OJPEG_JPEGINTERCHANGEFORMATLENGTH))
        fprintf(fd, "  JpegInterchangeFormatLength: %" PRIu64 "\n",
                (uint64_t)sp->jpeg_interchange_format_length);
    if (TIFFFieldSet(tif, FIELD_OJPEG_JPEGQTABLES))
    {
        fprintf(fd, "  JpegQTables:");
        for (m = 0; m < sp->qtable_offset_count; m++)
            fprintf(fd, " %" PRIu64, (uint64_t)sp->qtable_offset[m]);
        fprintf(fd, "\n");
    }
    if (TIFFFieldSet(tif, FIELD_OJPEG_JPEGDCTABLES))
    {
        fprintf(fd, "  JpegDcTables:");
        for (m = 0; m < sp->dctable_offset_count; m++)
            fprintf(fd, " %" PRIu64, (uint64_t)sp->dctable_offset[m]);
        fprintf(fd, "\n");
    }
    if (TIFFFieldSet(tif, FIELD_OJPEG_JPEGACTABLES))
    {
        fprintf(fd, "  JpegAcTables:");
        for (m = 0; m < sp->actable_offset_count; m++)
            fprintf(fd, " %" PRIu64, (uint64_t)sp->actable_offset[m]);
        fprintf(fd, "\n");
    }
    if (TIFFFieldSet(tif, FIELD_OJPEG_JPEGPROC))
        fprintf(fd, "  JpegProc: %" PRIu8 "\n", sp->jpeg_proc);
    if (TIFFFieldSet(tif, FIELD_OJPEG_JPEGRESTARTINTERVAL))
        fprintf(fd, "  JpegRestartInterval: %" PRIu16 "\n",
                sp->restart_interval);
    if (sp->printdir)
        (*sp->printdir)(tif, fd, flags);
}

/**
 * @brief Accept the old-style JPEG directory without eager fixups.
 * @ingroup libtiff_private_ojpeg
 *
 * No-op compatibility hook; does not inspect input, allocate or alter
 * metadata.
 *
 * @param[in] tif Unused.
 * @return One unconditionally.
 */
static int OJPEGFixupTags(TIFF *tif)
{
    (void)tif;
    return (1);
}

/**
 * @brief Warn that old-style JPEG compression is deprecated.
 * @ingroup libtiff_private_ojpeg
 *
 * Emits a per-handle warning recommending modern JPEG; no dependency
 * creation, pixel I/O or ownership transfer.
 *
 * @param[in] tif Handle used for the warning.
 * @return One unconditionally.
 */
static int OJPEGSetupDecode(TIFF *tif)
{
    static const char module[] = "OJPEGSetupDecode";
    TIFFWarningExtR(tif, module,
                    "Deprecated and troublesome old-style JPEG compression "
                    "mode, please convert to new-style JPEG compression and "
                    "notify vendor of writing software");
    return (1);
}

/**
 * @brief Position a reconstructed JPEG session at the requested strile and
 * plane.
 * @ingroup libtiff_private_ojpeg
 *
 * Lazily corrects sampling and reads headers, restores cached SOS input
 * snapshots and restarts a session for backward/plane changes. Requires a
 * valid sample/strile with at most three components and representable
 * geometry. Forward positioning decodes/discards intervening striles; this
 * is sequential reconstruction rather than independent strile decoding. Can
 * seek/read, allocate and replace dependency state; failures leave partial
 * state for cleanup. Sets decoder_ok on success.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 * @param[in] s Sample (plane) index: 0 for contiguous data, 0..2 for
 * separate planes; selects the cached scan in sos_end.
 * @return One on completion, zero on detected malformed input, allocation
 * or dependency/I/O failure; partial state and client effects remain.
 */
static int OJPEGPreDecode(TIFF *tif, uint16_t s)
{
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    uint32_t m;
    if (sp->subsamplingcorrect_done == 0)
        OJPEGSubsamplingCorrect(tif);
    if (sp->readheader_done == 0)
    {
        if (OJPEGReadHeaderInfo(tif) == 0)
            return (0);
    }
    if (sp->sos_end[s].log == 0)
    {
        if (OJPEGReadSecondarySos(tif, s) == 0)
            return (0);
    }
    if (isTiled(tif))
        m = tif->tif_dir.td_curtile;
    else
        m = tif->tif_dir.td_curstrip;
    if ((sp->writeheader_done != 0) &&
        ((sp->write_cursample != s) || (sp->write_curstrile > m)))
    {
        if (sp->libjpeg_session_active != 0)
            OJPEGLibjpegSessionAbort(tif);
        sp->writeheader_done = 0;
    }
    if (sp->writeheader_done == 0)
    {
        sp->plane_sample_offset = (uint8_t)s;
        sp->write_cursample = s;
        sp->write_curstrile = s * tif->tif_dir.td_stripsperimage;
        if ((sp->in_buffer_file_pos_log == 0) ||
            (sp->in_buffer_file_pos - sp->in_buffer_togo !=
             sp->sos_end[s].in_buffer_file_pos))
        {
            sp->in_buffer_source = sp->sos_end[s].in_buffer_source;
            sp->in_buffer_next_strile = sp->sos_end[s].in_buffer_next_strile;
            sp->in_buffer_file_pos = sp->sos_end[s].in_buffer_file_pos;
            sp->in_buffer_file_pos_log = 0;
            sp->in_buffer_file_togo = sp->sos_end[s].in_buffer_file_togo;
            sp->in_buffer_togo = 0;
            sp->in_buffer_cur = 0;
        }
        if (OJPEGWriteHeaderInfo(tif) == 0)
            return (0);
    }

    sp->subsampling_convert_state = 0;

    while (sp->write_curstrile < m)
    {
        if (sp->libjpeg_jpeg_query_style == 0)
        {
            if (OJPEGPreDecodeSkipRaw(tif) == 0)
                return (0);
        }
        else
        {
            if (OJPEGPreDecodeSkipScanlines(tif) == 0)
                return (0);
        }
        sp->write_curstrile++;
    }
    sp->decoder_ok = 1;
    return (1);
}

/**
 * @brief Decode and discard one strile in raw mode.
 * @ingroup libtiff_private_ojpeg
 *
 * Consumes complete raw MCU row groups into codec-owned conversion planes,
 * retaining any partially consumed group. Requires an active raw-mode
 * session and nonzero conversion geometry. Clears error_in_raw_data_decoding
 * when it completes a partially consumed group. Does not allocate; a libjpeg
 * failure is not latched in error_in_raw_data_decoding, and partial
 * advancement remains.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 * @return One on completion, zero if libjpeg reports a fatal error.
 */
static int OJPEGPreDecodeSkipRaw(TIFF *tif)
{
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    uint32_t m;
    m = sp->lines_per_strile;
    if (sp->subsampling_convert_state != 0)
    {
        if (sp->subsampling_convert_clines - sp->subsampling_convert_state >= m)
        {
            sp->subsampling_convert_state += m;
            if (sp->subsampling_convert_state == sp->subsampling_convert_clines)
                sp->subsampling_convert_state = 0;
            return (1);
        }
        m -= sp->subsampling_convert_clines - sp->subsampling_convert_state;
        sp->subsampling_convert_state = 0;
        sp->error_in_raw_data_decoding = 0;
    }
    while (m >= sp->subsampling_convert_clines)
    {
        if (jpeg_read_raw_data_encap(sp, &(sp->libjpeg_jpeg_decompress_struct),
                                     sp->subsampling_convert_ycbcrimage,
                                     (uint32_t)sp->subsampling_ver * 8) == 0)
            return (0);
        m -= sp->subsampling_convert_clines;
    }
    if (m > 0)
    {
        if (jpeg_read_raw_data_encap(sp, &(sp->libjpeg_jpeg_decompress_struct),
                                     sp->subsampling_convert_ycbcrimage,
                                     (uint32_t)sp->subsampling_ver * 8) == 0)
            return (0);
        sp->subsampling_convert_state = m;
    }
    return (1);
}

/**
 * @brief Decode and discard one strile in scanlines mode.
 * @ingroup libtiff_private_ojpeg
 *
 * Lazily allocates a codec-owned bytes_per_line skip buffer and decodes
 * lines_per_strile rows into it. Requires an active scanline session.
 * The skip buffer is kept for later calls; partial advancement remains on
 * failure.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 * @return One on completion, zero on allocation failure or if libjpeg
 * reports a fatal error.
 */
static int OJPEGPreDecodeSkipScanlines(TIFF *tif)
{
    static const char module[] = "OJPEGPreDecodeSkipScanlines";
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    uint32_t m;
    if (sp->skip_buffer == NULL)
    {
        sp->skip_buffer = (uint8_t *)_TIFFmallocExt(tif, sp->bytes_per_line);
        if (sp->skip_buffer == NULL)
        {
            TIFFErrorExtR(tif, module, "Out of memory");
            return (0);
        }
    }
    for (m = 0; m < sp->lines_per_strile; m++)
    {
        if (jpeg_read_scanlines_encap(sp, &(sp->libjpeg_jpeg_decompress_struct),
                                      &sp->skip_buffer, 1) == 0)
            return (0);
    }
    return (1);
}

/**
 * @brief Decode complete output rows from the active old-style JPEG session.
 * @ingroup libtiff_private_ojpeg
 *
 * Requires nonnegative cc, a writable cc-byte buffer and valid setup with
 * nonzero bytes_per_line. Dispatches raw clump or scanline mode. On failed
 * dispatch, invalid/inactive session or latched raw error, zeroes the
 * entire requested buffer. Dependency/cursor progress and diagnostics
 * remain; no caller buffer retention or ownership transfer. A positive
 * result does not validate discarded dependency row-count results.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 * @param[out] buf Writable output of cc bytes.
 * @param[in] cc Number of bytes to decode; unless it is a positive multiple
 * of bytes_per_line the raw or scanline decoder fails.
 * @param[in] s Sample index; unused (the plane is selected by
 * OJPEGPreDecode).
 * @return One on wrapper completion, zero on detected failure with
 * requested output cleared.
 */
static int OJPEGDecode(TIFF *tif, uint8_t *buf, tmsize_t cc, uint16_t s)
{
    static const char module[] = "OJPEGDecode";
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    (void)s;
    if (!sp->decoder_ok)
    {
        memset(buf, 0, (size_t)cc);
        TIFFErrorExtR(tif, module,
                      "Cannot decode: decoder not correctly initialized");
        return 0;
    }
    if (sp->libjpeg_session_active == 0)
    {
        memset(buf, 0, (size_t)cc);
        /* This should normally not happen, except that it does when */
        /* using TIFFReadScanline() which calls OJPEGPostDecode() for */
        /* each scanline, which assumes that a whole strile was read */
        /* and may thus incorrectly consider it has read the whole image,
         * causing */
        /* OJPEGLibjpegSessionAbort() to be called prematurely. */
        /* Triggered by https://gitlab.com/libtiff/libtiff/-/issues/337 */
        TIFFErrorExtR(tif, module,
                      "Cannot decode: libjpeg_session_active == 0");
        return 0;
    }
    if (sp->error_in_raw_data_decoding)
    {
        memset(buf, 0, (size_t)cc);
        return 0;
    }
    if (sp->libjpeg_jpeg_query_style == 0)
    {
        if (OJPEGDecodeRaw(tif, buf, cc) == 0)
        {
            memset(buf, 0, (size_t)cc);
            return (0);
        }
    }
    else
    {
        if (OJPEGDecodeScanlines(tif, buf, cc) == 0)
        {
            memset(buf, 0, (size_t)cc);
            return (0);
        }
    }
    return (1);
}

/**
 * @brief Decode complete subsampled clump rows.
 * @ingroup libtiff_private_ojpeg
 *
 * Requires a live raw-mode session. Rejects cc not divisible by
 * bytes_per_line with an error, and returns 0 without a diagnostic for
 * cc <= 0. Reads MCU planes as needed and interleaves subsampled Y/Cb/Cr
 * clumps, advancing conversion state. A trapped dependency failure latches
 * error_in_raw_data_decoding. Partial output/cursor changes survive failure
 * here; OJPEGDecode clears output when propagating failure. Borrows caller
 * output synchronously; no ownership transfer.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 * @param[out] buf Writable output of cc bytes.
 * @param[in] cc Number of bytes to decode; zero is returned unless it is
 * a positive multiple of bytes_per_line.
 * @return One after decoding cc bytes, zero on an invalid cc or if libjpeg
 * reports a fatal error; partial output remains.
 */
static int OJPEGDecodeRaw(TIFF *tif, uint8_t *buf, tmsize_t cc)
{
    static const char module[] = "OJPEGDecodeRaw";
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    uint8_t *m;
    tmsize_t n;
    uint8_t *oy;
    uint8_t *ocb;
    uint8_t *ocr;
    uint8_t *p;
    uint32_t q;
    uint8_t *r;
    uint8_t sx, sy;
    if (cc % sp->bytes_per_line != 0)
    {
        TIFFErrorExtR(tif, module, "Fractional scanline not read");
        return (0);
    }
    if (cc <= 0)
        return (0);
    m = buf;
    n = cc;
    do
    {
        if (sp->subsampling_convert_state == 0)
        {
            if (jpeg_read_raw_data_encap(
                    sp, &(sp->libjpeg_jpeg_decompress_struct),
                    sp->subsampling_convert_ycbcrimage,
                    (uint32_t)sp->subsampling_ver * 8) == 0)
            {
                sp->error_in_raw_data_decoding = 1;
                return (0);
            }
        }
        oy = sp->subsampling_convert_ybuf +
             sp->subsampling_convert_state * sp->subsampling_ver *
                 sp->subsampling_convert_ylinelen;
        ocb = sp->subsampling_convert_cbbuf +
              sp->subsampling_convert_state * sp->subsampling_convert_clinelen;
        ocr = sp->subsampling_convert_crbuf +
              sp->subsampling_convert_state * sp->subsampling_convert_clinelen;
        p = m;
        for (q = 0; q < sp->subsampling_convert_clinelenout; q++)
        {
            r = oy;
            for (sy = 0; sy < sp->subsampling_ver; sy++)
            {
                for (sx = 0; sx < sp->subsampling_hor; sx++)
                    *p++ = *r++;
                r += sp->subsampling_convert_ylinelen - sp->subsampling_hor;
            }
            oy += sp->subsampling_hor;
            *p++ = *ocb++;
            *p++ = *ocr++;
        }
        sp->subsampling_convert_state++;
        if (sp->subsampling_convert_state == sp->subsampling_convert_clines)
            sp->subsampling_convert_state = 0;
        m += sp->bytes_per_line;
        n -= sp->bytes_per_line;
    } while (n > 0);
    return (1);
}

/**
 * @brief Decode complete ordinary sample rows.
 * @ingroup libtiff_private_ojpeg
 *
 * Requires a live scanline session. Rejects cc not divisible by
 * bytes_per_line with an error, and returns 0 without a diagnostic for
 * cc <= 0. Requests one row at a time and advances output by
 * bytes_per_line; the encapsulation discards dependency row counts. Partial
 * output/cursor changes survive failure here; OJPEGDecode clears output
 * when propagating failure. Borrows caller output synchronously; no
 * ownership transfer.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 * @param[out] buf Writable output of cc bytes.
 * @param[in] cc Number of bytes to decode; zero is returned unless it is
 * a positive multiple of bytes_per_line.
 * @return One after decoding cc bytes, zero on an invalid cc or if libjpeg
 * reports a fatal error; partial output remains.
 */
static int OJPEGDecodeScanlines(TIFF *tif, uint8_t *buf, tmsize_t cc)
{
    static const char module[] = "OJPEGDecodeScanlines";
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    uint8_t *m;
    tmsize_t n;
    if (cc % sp->bytes_per_line != 0)
    {
        TIFFErrorExtR(tif, module, "Fractional scanline not read");
        return (0);
    }
    if (cc <= 0)
        return (0);
    m = buf;
    n = cc;
    do
    {
        if (jpeg_read_scanlines_encap(sp, &(sp->libjpeg_jpeg_decompress_struct),
                                      &m, 1) == 0)
            return (0);
        m += sp->bytes_per_line;
        n -= sp->bytes_per_line;
    } while (n > 0);
    return (1);
}

/**
 * @brief Advance the cached strile count and end a completed plane session.
 * @ingroup libtiff_private_ojpeg
 *
 * Ignores buf and cc and assumes each call completed a whole strile.
 * Requires nonzero stripsperimage and a live session when reaching the
 * plane boundary; destroys that session and clears writeheader_done. This
 * existing assumption is invalid for scanline-by-scanline calls and can
 * abort decoding prematurely (issue 337). No caller buffer access or
 * ownership transfer.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 * @param[in] buf Unused.
 * @param[in] cc Unused.
 */
static void OJPEGPostDecode(TIFF *tif, uint8_t *buf, tmsize_t cc)
{
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    (void)buf;
    (void)cc;
    /* This function somehow incorrectly assumes that a whole strile was read,
     */
    /* which is not true when TIFFReadScanline() is called, */
    /* and may thus incorrectly consider it has read the whole image, causing */
    /* OJPEGLibjpegSessionAbort() to be called prematurely. */
    /* So this logic should be fixed to take into account cc, or disable */
    /* the scan line reading interface. */
    /* Triggered by https://gitlab.com/libtiff/libtiff/-/issues/337 */
    sp->write_curstrile++;
    if (sp->write_curstrile % tif->tif_dir.td_stripsperimage == 0)
    {
        assert(sp->libjpeg_session_active != 0);
        OJPEGLibjpegSessionAbort(tif);
        sp->writeheader_done = 0;
    }
}

/**
 * @brief Reject unsupported old-style JPEG encoding.
 * @ingroup libtiff_private_ojpeg
 *
 * Always reports an error recommending new-style JPEG compression.
 *
 * @param[in] tif Handle used for the error report.
 * @return Zero unconditionally.
 */
static int OJPEGSetupEncode(TIFF *tif)
{
    static const char module[] = "OJPEGSetupEncode";
    TIFFErrorExtR(
        tif, module,
        "OJPEG encoding not supported; use new-style JPEG compression instead");
    return (0);
}

/**
 * @brief Reject unsupported old-style JPEG encoding.
 * @ingroup libtiff_private_ojpeg
 *
 * Always reports an error recommending new-style JPEG compression.
 *
 * @param[in] tif Handle used for the error report.
 * @param[in] s Unused.
 * @return Zero unconditionally.
 */
static int OJPEGPreEncode(TIFF *tif, uint16_t s)
{
    static const char module[] = "OJPEGPreEncode";
    (void)s;
    TIFFErrorExtR(
        tif, module,
        "OJPEG encoding not supported; use new-style JPEG compression instead");
    return (0);
}

/**
 * @brief Reject unsupported old-style JPEG encoding.
 * @ingroup libtiff_private_ojpeg
 *
 * Always reports an error recommending new-style JPEG compression.
 *
 * @param[in] tif Handle used for the error report.
 * @param[in] buf Unused.
 * @param[in] cc Unused.
 * @param[in] s Unused.
 * @return Zero unconditionally.
 */
static int OJPEGEncode(TIFF *tif, uint8_t *buf, tmsize_t cc, uint16_t s)
{
    static const char module[] = "OJPEGEncode";
    (void)buf;
    (void)cc;
    (void)s;
    TIFFErrorExtR(
        tif, module,
        "OJPEG encoding not supported; use new-style JPEG compression instead");
    return (0);
}

/**
 * @brief Reject unsupported old-style JPEG encoding.
 * @ingroup libtiff_private_ojpeg
 *
 * Always reports an error recommending new-style JPEG compression.
 *
 * @param[in] tif Handle used for the error report.
 * @return Zero unconditionally.
 */
static int OJPEGPostEncode(TIFF *tif)
{
    static const char module[] = "OJPEGPostEncode";
    TIFFErrorExtR(
        tif, module,
        "OJPEG encoding not supported; use new-style JPEG compression instead");
    return (0);
}

/**
 * @brief Release old-style JPEG state and restore parent methods.
 * @ingroup libtiff_private_ojpeg
 *
 * Restores saved tag/print hooks and default compression methods, aborts an
 * active dependency session, frees all owned table blobs, combined
 * conversion storage, pointer arrays and skip buffer, then clears tif_data.
 * Interior component views borrow the combined buffer and are not freed
 * separately. Invalidates every borrowed table/state view; caller handle
 * remains live.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 */
static void OJPEGCleanup(TIFF *tif)
{
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    if (sp != 0)
    {
        tif->tif_tagmethods.vgetfield = sp->vgetparent;
        tif->tif_tagmethods.vsetfield = sp->vsetparent;
        tif->tif_tagmethods.printdir = sp->printdir;
        if (sp->qtable[0] != 0)
            _TIFFfreeExt(tif, sp->qtable[0]);
        if (sp->qtable[1] != 0)
            _TIFFfreeExt(tif, sp->qtable[1]);
        if (sp->qtable[2] != 0)
            _TIFFfreeExt(tif, sp->qtable[2]);
        if (sp->qtable[3] != 0)
            _TIFFfreeExt(tif, sp->qtable[3]);
        if (sp->dctable[0] != 0)
            _TIFFfreeExt(tif, sp->dctable[0]);
        if (sp->dctable[1] != 0)
            _TIFFfreeExt(tif, sp->dctable[1]);
        if (sp->dctable[2] != 0)
            _TIFFfreeExt(tif, sp->dctable[2]);
        if (sp->dctable[3] != 0)
            _TIFFfreeExt(tif, sp->dctable[3]);
        if (sp->actable[0] != 0)
            _TIFFfreeExt(tif, sp->actable[0]);
        if (sp->actable[1] != 0)
            _TIFFfreeExt(tif, sp->actable[1]);
        if (sp->actable[2] != 0)
            _TIFFfreeExt(tif, sp->actable[2]);
        if (sp->actable[3] != 0)
            _TIFFfreeExt(tif, sp->actable[3]);
        if (sp->libjpeg_session_active != 0)
            OJPEGLibjpegSessionAbort(tif);
        if (sp->subsampling_convert_ycbcrbuf != 0)
            _TIFFfreeExt(tif, sp->subsampling_convert_ycbcrbuf);
        if (sp->subsampling_convert_ycbcrimage != 0)
            _TIFFfreeExt(tif, sp->subsampling_convert_ycbcrimage);
        if (sp->skip_buffer != 0)
            _TIFFfreeExt(tif, sp->skip_buffer);
        _TIFFfreeExt(tif, sp);
        tif->tif_data = NULL;
        _TIFFSetDefaultCompressionState(tif);
    }
}

/**
 * @brief Probe and reconcile old-style JPEG subsampling once.
 * @ingroup libtiff_private_ojpeg
 *
 * Called once, from OJPEGVGetField (YCbCrSubsampling) or OJPEGPreDecode, and
 * sets subsamplingcorrect_done. Only the codec-cached values
 * sp->subsampling_hor, sp->subsampling_ver and
 * sp->subsampling_force_desubsampling_inside_decompression change;
 * td_ycbcrsubsampling in the directory is not modified.
 *
 * If SamplesPerPixel is not 3 or Photometric is neither YCbCr nor ITULAB, no
 * input is read: the factors are set to 1x1 and the force flag is cleared,
 * with a warning if the YCbCrSubsampling tag was set. Otherwise the stream
 * header is parsed once by OJPEGReadHeaderInfoSec in sampling-only mode
 * (sp->subsamplingcorrect set), which stops at the first SOF or SOS marker
 * and suppresses most diagnostics; its result is ignored. The first SOF
 * component's factors replace the cached ones, and factors other than 1, 2
 * or 4, or chroma factors other than 1x1, set the force flag, in which case
 * the cached factors become 1x1 and libjpeg upsamples. Warns when the stream
 * factors differ from the tag (or the 2x2 default) and when horizontal is
 * less than vertical. The probe seeks and reads the file and leaves the
 * input cursor wherever it stopped (a DRI marker before the SOF also sets
 * restart_interval); OJPEGReadHeaderInfo parses the header again later.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 */
static void OJPEGSubsamplingCorrect(TIFF *tif)
{
    static const char module[] = "OJPEGSubsamplingCorrect";
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    uint8_t mh;
    uint8_t mv;

    assert(sp->subsamplingcorrect_done == 0);
    if ((tif->tif_dir.td_samplesperpixel != 3) ||
        ((tif->tif_dir.td_photometric != PHOTOMETRIC_YCBCR) &&
         (tif->tif_dir.td_photometric != PHOTOMETRIC_ITULAB)))
    {
        if (sp->subsampling_tag != 0)
            TIFFWarningExtR(tif, module,
                            "Subsampling tag not appropriate for this "
                            "Photometric and/or SamplesPerPixel");
        sp->subsampling_hor = 1;
        sp->subsampling_ver = 1;
        sp->subsampling_force_desubsampling_inside_decompression = 0;
    }
    else
    {
        sp->subsamplingcorrect_done = 1;
        mh = sp->subsampling_hor;
        mv = sp->subsampling_ver;
        sp->subsamplingcorrect = 1;
        OJPEGReadHeaderInfoSec(tif);
        if (sp->subsampling_force_desubsampling_inside_decompression != 0)
        {
            sp->subsampling_hor = 1;
            sp->subsampling_ver = 1;
        }
        sp->subsamplingcorrect = 0;
        if (((sp->subsampling_hor != mh) || (sp->subsampling_ver != mv)) &&
            (sp->subsampling_force_desubsampling_inside_decompression == 0))
        {
            if (sp->subsampling_tag == 0)
                TIFFWarningExtR(
                    tif, module,
                    "Subsampling tag is not set, yet subsampling inside JPEG "
                    "data [%" PRIu8 ",%" PRIu8
                    "] does not match default values [2,2]; assuming "
                    "subsampling inside JPEG data is correct",
                    sp->subsampling_hor, sp->subsampling_ver);
            else
                TIFFWarningExtR(
                    tif, module,
                    "Subsampling inside JPEG data [%" PRIu8 ",%" PRIu8
                    "] does not match subsampling tag values [%" PRIu8
                    ",%" PRIu8
                    "]; assuming subsampling inside JPEG data is correct",
                    sp->subsampling_hor, sp->subsampling_ver, mh, mv);
        }
        if (sp->subsampling_force_desubsampling_inside_decompression != 0)
        {
            if (sp->subsampling_tag == 0)
                TIFFWarningExtR(
                    tif, module,
                    "Subsampling tag is not set, yet subsampling inside JPEG "
                    "data does not match default values [2,2] (nor any other "
                    "values allowed in TIFF); assuming subsampling inside JPEG "
                    "data is correct and desubsampling inside JPEG "
                    "decompression");
            else
                TIFFWarningExtR(
                    tif, module,
                    "Subsampling inside JPEG data does not match subsampling "
                    "tag values [%" PRIu8 ",%" PRIu8
                    "] (nor any other values allowed in TIFF); assuming "
                    "subsampling inside JPEG data is correct and desubsampling "
                    "inside JPEG decompression",
                    mh, mv);
        }
        if (sp->subsampling_force_desubsampling_inside_decompression == 0)
        {
            if (sp->subsampling_hor < sp->subsampling_ver)
                TIFFWarningExtR(tif, module,
                                "Subsampling values [%" PRIu8 ",%" PRIu8
                                "] are not allowed in TIFF",
                                sp->subsampling_hor, sp->subsampling_ver);
        }
    }
    sp->subsamplingcorrect_done = 1;
}

/**
 * @brief Validate geometry and cache the first old-style JPEG scan position.
 * @ingroup libtiff_private_ojpeg
 *
 * Accepts SamplesPerPixel 1 or 3 and contiguous/separate planes; sample
 * precision is checked only when the stream has an SOF marker. For
 * multi-strile images, validates sampling factors (1, 2 or 4) and
 * strile/MCU alignment, and replaces the cached restart interval (including
 * a JpegRestartInterval tag value) with the per-strile MCU count, which a
 * stream DRI marker may override. Reads stream or offset-tag tables, caches
 * SOS state and marks readheader_done. Requires valid nonzero strile
 * geometry. Allocated tables remain codec-owned;
 * client position and partial caches persist on failure.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 * @return One on completion, zero on unsupported SamplesPerPixel or
 * sampling/strile geometry, malformed header data, allocation or read
 * failure; partial state and file position changes remain.
 */
static int OJPEGReadHeaderInfo(TIFF *tif)
{
    static const char module[] = "OJPEGReadHeaderInfo";
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    assert(sp->readheader_done == 0);
    sp->image_width = tif->tif_dir.td_imagewidth;
    sp->image_length = tif->tif_dir.td_imagelength;
    if (isTiled(tif))
    {
        sp->strile_width = tif->tif_dir.td_tilewidth;
        sp->strile_length = tif->tif_dir.td_tilelength;
        sp->strile_length_total =
            ((sp->image_length + sp->strile_length - 1) / sp->strile_length) *
            sp->strile_length;
    }
    else
    {
        sp->strile_width = sp->image_width;
        sp->strile_length = tif->tif_dir.td_rowsperstrip;
        if (sp->strile_length == (uint32_t)-1)
            sp->strile_length = sp->image_length;
        sp->strile_length_total = sp->image_length;
    }
    if (tif->tif_dir.td_samplesperpixel == 1)
    {
        sp->samples_per_pixel = 1;
        sp->plane_sample_offset = 0;
        sp->samples_per_pixel_per_plane = sp->samples_per_pixel;
        sp->subsampling_hor = 1;
        sp->subsampling_ver = 1;
    }
    else
    {
        if (tif->tif_dir.td_samplesperpixel != 3)
        {
            TIFFErrorExtR(tif, module,
                          "SamplesPerPixel %" PRIu8
                          " not supported for this compression scheme",
                          sp->samples_per_pixel);
            return (0);
        }
        sp->samples_per_pixel = 3;
        sp->plane_sample_offset = 0;
        if (tif->tif_dir.td_planarconfig == PLANARCONFIG_CONTIG)
            sp->samples_per_pixel_per_plane = 3;
        else
            sp->samples_per_pixel_per_plane = 1;
    }
    if (sp->strile_length < sp->image_length)
    {
        if (((sp->subsampling_hor != 1) && (sp->subsampling_hor != 2) &&
             (sp->subsampling_hor != 4)) ||
            ((sp->subsampling_ver != 1) && (sp->subsampling_ver != 2) &&
             (sp->subsampling_ver != 4)))
        {
            TIFFErrorExtR(tif, module, "Invalid subsampling values");
            return (0);
        }
        if (sp->strile_length % ((uint32_t)sp->subsampling_ver * 8) != 0)
        {
            TIFFErrorExtR(tif, module,
                          "Incompatible vertical subsampling and image "
                          "strip/tile length");
            return (0);
        }
        {
            /* Values were validated above as 1, 2, or 4. */
            uint32_t mcu_width = (uint32_t)sp->subsampling_hor * 8;
            uint32_t mcu_height = (uint32_t)sp->subsampling_ver * 8;
            uint32_t mcu_cols =
                TIFFhowmany_32_maxuint_compat(sp->strile_width, mcu_width);
            uint32_t mcu_rows = sp->strile_length / mcu_height;
            if (mcu_cols == 0 || mcu_rows == 0)
            {
                sp->restart_interval = 0;
            }
            else
            {
                uint32_t restart_interval =
                    _TIFFMultiply32(tif, mcu_cols, mcu_rows, module);
                if (restart_interval == 0 || restart_interval > UINT16_MAX)
                {
                    TIFFErrorExtR(tif, module,
                                  "Integer overflow in restart interval");
                    return (0);
                }
                sp->restart_interval = (uint16_t)restart_interval;
            }
        }
    }
    if (OJPEGReadHeaderInfoSec(tif) == 0)
        return (0);
    sp->sos_end[0].log = 1;
    sp->sos_end[0].in_buffer_source = sp->in_buffer_source;
    sp->sos_end[0].in_buffer_next_strile = sp->in_buffer_next_strile;
    sp->sos_end[0].in_buffer_file_pos =
        sp->in_buffer_file_pos - sp->in_buffer_togo;
    sp->sos_end[0].in_buffer_file_togo =
        sp->in_buffer_file_togo + sp->in_buffer_togo;
    sp->readheader_done = 1;
    return (1);
}

/**
 * @brief Locate and cache a later separate-plane scan header.
 * @ingroup libtiff_private_ojpeg
 *
 * Requires sample s in 1..2 and valid preceding snapshots. Restores nearest
 * known scan, scans markers and caches each subsequent SOS stream position;
 * can seek/read and change plane offsets. Cached positions and partial
 * progress persist on failure; no caller ownership transfer.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 * @param[in] s Separate-plane sample index in 1..2 (asserted).
 * @return One on success, zero on a short read or a malformed SOS marker;
 * does not allocate.
 */
static int OJPEGReadSecondarySos(TIFF *tif, uint16_t s)
{
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    uint8_t m;
    assert(s > 0);
    assert(s < 3);
    assert(sp->sos_end[0].log != 0);
    assert(sp->sos_end[s].log == 0);
    sp->plane_sample_offset = (uint8_t)(s - 1);
    while (sp->sos_end[sp->plane_sample_offset].log == 0)
        sp->plane_sample_offset--;
    sp->in_buffer_source =
        sp->sos_end[sp->plane_sample_offset].in_buffer_source;
    sp->in_buffer_next_strile =
        sp->sos_end[sp->plane_sample_offset].in_buffer_next_strile;
    sp->in_buffer_file_pos =
        sp->sos_end[sp->plane_sample_offset].in_buffer_file_pos;
    sp->in_buffer_file_pos_log = 0;
    sp->in_buffer_file_togo =
        sp->sos_end[sp->plane_sample_offset].in_buffer_file_togo;
    sp->in_buffer_togo = 0;
    sp->in_buffer_cur = 0;
    while (sp->plane_sample_offset < s)
    {
        do
        {
            if (OJPEGReadByte(sp, &m) == 0)
                return (0);
            if (m == 255)
            {
                do
                {
                    if (OJPEGReadByte(sp, &m) == 0)
                        return (0);
                    if (m != 255)
                        break;
                } while (1);
                if (m == JPEG_MARKER_SOS)
                    break;
            }
        } while (1);
        sp->plane_sample_offset++;
        if (OJPEGReadHeaderInfoSecStreamSos(tif) == 0)
            return (0);
        sp->sos_end[sp->plane_sample_offset].log = 1;
        sp->sos_end[sp->plane_sample_offset].in_buffer_source =
            sp->in_buffer_source;
        sp->sos_end[sp->plane_sample_offset].in_buffer_next_strile =
            sp->in_buffer_next_strile;
        sp->sos_end[sp->plane_sample_offset].in_buffer_file_pos =
            sp->in_buffer_file_pos - sp->in_buffer_togo;
        sp->sos_end[sp->plane_sample_offset].in_buffer_file_togo =
            sp->in_buffer_file_togo + sp->in_buffer_togo;
    }
    return (1);
}

/**
 * @brief Start libjpeg with a synthesized old-style JPEG input header.
 * @ingroup libtiff_private_ojpeg
 *
 * This builds decoder input, not a TIFF output stream. Rejects a previously
 * active failed session, installs managers, creates the dependency
 * decompressor and chooses raw subsampled or ordinary scanlines. Allocates
 * checked conversion storage and row-pointer views in raw mode, validates
 * dependency width/sampling, and starts decoding. Tiny malformed tile
 * layouts retain existing conversion limitations; failed setup can retain
 * an active session/owned buffers for cleanup. No caller storage retention.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 * @return One once decompression has started; zero if a session from an
 * earlier attempt is still active, on allocation failure or size overflow,
 * on a libjpeg fatal error, or when libjpeg reports a width or sampling
 * mismatch. An active session and buffers are left for cleanup.
 */
static int OJPEGWriteHeaderInfo(TIFF *tif)
{
    static const char module[] = "OJPEGWriteHeaderInfo";
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    uint8_t **m;
    uint32_t n;
    /* if a previous attempt failed, don't try again */
    if (sp->libjpeg_session_active != 0)
        return 0;
    sp->out_state = ososSoi;
    sp->restart_index = 0;
    jpeg_std_error(&(sp->libjpeg_jpeg_error_mgr));
    sp->libjpeg_jpeg_error_mgr.output_message =
        OJPEGLibjpegJpegErrorMgrOutputMessage;
    sp->libjpeg_jpeg_error_mgr.error_exit = OJPEGLibjpegJpegErrorMgrErrorExit;
    sp->libjpeg_jpeg_decompress_struct.err = &(sp->libjpeg_jpeg_error_mgr);
    sp->libjpeg_jpeg_decompress_struct.client_data = (void *)tif;
    if (jpeg_create_decompress_encap(
            sp, &(sp->libjpeg_jpeg_decompress_struct)) == 0)
        return (0);
    sp->libjpeg_session_active = 1;
    sp->libjpeg_jpeg_source_mgr.bytes_in_buffer = 0;
    sp->libjpeg_jpeg_source_mgr.init_source =
        OJPEGLibjpegJpegSourceMgrInitSource;
    sp->libjpeg_jpeg_source_mgr.fill_input_buffer =
        OJPEGLibjpegJpegSourceMgrFillInputBuffer;
    sp->libjpeg_jpeg_source_mgr.skip_input_data =
        OJPEGLibjpegJpegSourceMgrSkipInputData;
    sp->libjpeg_jpeg_source_mgr.resync_to_restart =
        OJPEGLibjpegJpegSourceMgrResyncToRestart;
    sp->libjpeg_jpeg_source_mgr.term_source =
        OJPEGLibjpegJpegSourceMgrTermSource;
    sp->libjpeg_jpeg_decompress_struct.src = &(sp->libjpeg_jpeg_source_mgr);
    if (jpeg_read_header_encap(sp, &(sp->libjpeg_jpeg_decompress_struct), 1) ==
        0)
        return (0);
    if ((sp->subsampling_force_desubsampling_inside_decompression == 0) &&
        (sp->samples_per_pixel_per_plane > 1))
    {
        sp->libjpeg_jpeg_decompress_struct.raw_data_out = 1;
#if JPEG_LIB_VERSION >= 70
        sp->libjpeg_jpeg_decompress_struct.do_fancy_upsampling = FALSE;
#endif
        sp->libjpeg_jpeg_query_style = 0;
        if (sp->subsampling_convert_log == 0)
        {
            assert(sp->subsampling_convert_ycbcrbuf == 0);
            assert(sp->subsampling_convert_ycbcrimage == 0);
            /* Check for division by zero. */
            if (sp->subsampling_hor == 0 || sp->subsampling_ver == 0)
                return (0);
            /* Check for potential overflow in subsampling_convert_ylinelen
             * computation.
             */
            if (sp->strile_width >
                UINT32_MAX - ((uint32_t)sp->subsampling_hor * 8 - 1))
                return (0);
            sp->subsampling_convert_ylinelen =
                ((sp->strile_width + (uint32_t)sp->subsampling_hor * 8 - 1) /
                 ((uint32_t)sp->subsampling_hor * 8) *
                 ((uint32_t)sp->subsampling_hor * 8));
            sp->subsampling_convert_ylines = (uint32_t)sp->subsampling_ver * 8;
            sp->subsampling_convert_clinelen =
                sp->subsampling_convert_ylinelen / sp->subsampling_hor;
            sp->subsampling_convert_clines = 8;
            /* Check for potential overflow in buffer length computations.
             * Use 64-bit intermediates to detect uint32_t overflow in
             * ylinelen * ylines, clinelen * clines, and their sum.
             */
            {
                uint64_t ybuflen64 =
                    (uint64_t)sp->subsampling_convert_ylinelen *
                    sp->subsampling_convert_ylines;
                uint64_t cbuflen64 =
                    (uint64_t)sp->subsampling_convert_clinelen *
                    sp->subsampling_convert_clines;
                uint64_t ycbcrbuflen64 = ybuflen64 + 2 * cbuflen64;
                if (ybuflen64 > UINT32_MAX || cbuflen64 > UINT32_MAX ||
                    ycbcrbuflen64 > UINT32_MAX)
                {
                    TIFFErrorExtR(tif, module,
                                  "Integer overflow in OJPEG buffer size");
                    return (0);
                }
                sp->subsampling_convert_ybuflen = (uint32_t)ybuflen64;
                sp->subsampling_convert_cbuflen = (uint32_t)cbuflen64;
                sp->subsampling_convert_ycbcrbuflen = (uint32_t)ycbcrbuflen64;
            }
            /* The calloc is not normally necessary, except in some edge/broken
             * cases */
            /* for example for a tiled image of height 1 with a tile height of 1
             * and subsampling_hor=subsampling_ver=2 */
            /* In that case, libjpeg will only fill the 8 first lines of the 16
             * lines */
            /* See https://bugs.chromium.org/p/oss-fuzz/issues/detail?id=16844
             */
            /* Even if this case is allowed (?), its handling is broken because
             * OJPEGPreDecode() should also likely */
            /* reset subsampling_convert_state to 0 when changing tile. */
            sp->subsampling_convert_ycbcrbuf = (uint8_t *)_TIFFcallocExt(
                tif, 1, sp->subsampling_convert_ycbcrbuflen);
            if (sp->subsampling_convert_ycbcrbuf == 0)
            {
                TIFFErrorExtR(tif, module, "Out of memory");
                return (0);
            }
            sp->subsampling_convert_ybuf = sp->subsampling_convert_ycbcrbuf;
            sp->subsampling_convert_cbbuf =
                sp->subsampling_convert_ybuf + sp->subsampling_convert_ybuflen;
            sp->subsampling_convert_crbuf =
                sp->subsampling_convert_cbbuf + sp->subsampling_convert_cbuflen;
            sp->subsampling_convert_ycbcrimagelen =
                3 + sp->subsampling_convert_ylines +
                2 * sp->subsampling_convert_clines;
            sp->subsampling_convert_ycbcrimage = (uint8_t **)_TIFFmallocExt(
                tif, (tmsize_t)((size_t)sp->subsampling_convert_ycbcrimagelen *
                                sizeof(uint8_t *)));
            if (sp->subsampling_convert_ycbcrimage == 0)
            {
                TIFFErrorExtR(tif, module, "Out of memory");
                return (0);
            }
            m = sp->subsampling_convert_ycbcrimage;
            *m++ = (uint8_t *)(sp->subsampling_convert_ycbcrimage + 3);
            *m++ = (uint8_t *)(sp->subsampling_convert_ycbcrimage + 3 +
                               sp->subsampling_convert_ylines);
            *m++ = (uint8_t *)(sp->subsampling_convert_ycbcrimage + 3 +
                               sp->subsampling_convert_ylines +
                               sp->subsampling_convert_clines);
            for (n = 0; n < sp->subsampling_convert_ylines; n++)
                *m++ = sp->subsampling_convert_ybuf +
                       n * sp->subsampling_convert_ylinelen;
            for (n = 0; n < sp->subsampling_convert_clines; n++)
                *m++ = sp->subsampling_convert_cbbuf +
                       n * sp->subsampling_convert_clinelen;
            for (n = 0; n < sp->subsampling_convert_clines; n++)
                *m++ = sp->subsampling_convert_crbuf +
                       n * sp->subsampling_convert_clinelen;
            sp->subsampling_convert_clinelenout =
                sp->strile_width / sp->subsampling_hor +
                ((sp->strile_width % sp->subsampling_hor) != 0 ? 1 : 0);
            sp->subsampling_convert_state = 0;
            sp->error_in_raw_data_decoding = 0;

            const uint64_t bpl =
                (uint64_t)sp->subsampling_convert_clinelenout *
                ((uint64_t)sp->subsampling_ver * sp->subsampling_hor + 2);
            if (bpl > UINT32_MAX)
                return (0);
            sp->bytes_per_line = (uint32_t)bpl;

            sp->lines_per_strile =
                sp->strile_length / sp->subsampling_ver +
                ((sp->strile_length % sp->subsampling_ver) != 0 ? 1 : 0);
            sp->subsampling_convert_log = 1;
        }
    }
    else
    {
        sp->libjpeg_jpeg_decompress_struct.jpeg_color_space = JCS_UNKNOWN;
        sp->libjpeg_jpeg_decompress_struct.out_color_space = JCS_UNKNOWN;
        sp->libjpeg_jpeg_query_style = 1;
        sp->bytes_per_line =
            _TIFFMultiply32(tif, sp->samples_per_pixel_per_plane,
                            sp->strile_width, "OJPEGWriteHeaderInfo");
        if (sp->bytes_per_line == 0)
            return (0);
        sp->lines_per_strile = sp->strile_length;
    }
    if (jpeg_start_decompress_encap(sp,
                                    &(sp->libjpeg_jpeg_decompress_struct)) == 0)
        return (0);
    if (sp->libjpeg_jpeg_decompress_struct.image_width != sp->strile_width)
    {
        TIFFErrorExtR(tif, module,
                      "jpeg_start_decompress() returned image_width = %u, "
                      "expected %" PRIu32,
                      sp->libjpeg_jpeg_decompress_struct.image_width,
                      sp->strile_width);
        return 0;
    }
    if (sp->libjpeg_jpeg_decompress_struct.max_h_samp_factor !=
            sp->subsampling_hor ||
        sp->libjpeg_jpeg_decompress_struct.max_v_samp_factor !=
            sp->subsampling_ver)
    {
        TIFFErrorExtR(tif, module,
                      "jpeg_start_decompress() returned max_h_samp_factor = %d "
                      "and max_v_samp_factor = %d, expected %" PRIu8
                      " and %" PRIu8,
                      sp->libjpeg_jpeg_decompress_struct.max_h_samp_factor,
                      sp->libjpeg_jpeg_decompress_struct.max_v_samp_factor,
                      sp->subsampling_hor, sp->subsampling_ver);
        return 0;
    }

    sp->writeheader_done = 1;
    return (1);
}

/**
 * @brief Destroy the active old-style JPEG dependency session.
 * @ingroup libtiff_private_ojpeg
 *
 * Requires an active session, calls jpeg_destroy and clears the active
 * flag. Invalidates dependency-owned views; retains outer codec
 * tables/buffers for reuse or cleanup. No independent error status.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 */
static void OJPEGLibjpegSessionAbort(TIFF *tif)
{
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    assert(sp->libjpeg_session_active != 0);
    jpeg_destroy(
        (tiff_ojpeg_common_struct *)(&(sp->libjpeg_jpeg_decompress_struct)));
    sp->libjpeg_session_active = 0;
}

/**
 * @brief Parse a bounded interchange/strile JPEG header or synthesize it from
 * tags.
 * @ingroup libtiff_private_ojpeg
 *
 * Normalizes invalid/out-of-file interchange extents, initializes input
 * traversal, dispatches marker parsers and skips APP/COM payloads. Probe
 * mode may stop after SOF/SOS without full parsing. Missing SOF falls back
 * to offset-tag tables and synthesized baseline component headers. Can
 * allocate, read/seek and replace cached tables; no position restore or
 * rollback on failure.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 * @return One on completion (in sampling-only mode, once an SOF or SOS
 * marker is reached), zero on a malformed or unknown marker, allocation or
 * read failure; partial state and file position changes remain.
 */
static int OJPEGReadHeaderInfoSec(TIFF *tif)
{
    static const char module[] = "OJPEGReadHeaderInfoSec";
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    uint8_t m;
    uint16_t n;
    uint8_t o;
    if (sp->file_size == 0)
        sp->file_size = TIFFGetFileSize(tif);
    if (sp->jpeg_interchange_format != 0)
    {
        if (sp->jpeg_interchange_format >= sp->file_size)
        {
            sp->jpeg_interchange_format = 0;
            sp->jpeg_interchange_format_length = 0;
        }
        else
        {
            if ((sp->jpeg_interchange_format_length == 0) ||
                (sp->jpeg_interchange_format >
                 UINT64_MAX - sp->jpeg_interchange_format_length) ||
                (sp->jpeg_interchange_format +
                     sp->jpeg_interchange_format_length >
                 sp->file_size))
                sp->jpeg_interchange_format_length =
                    sp->file_size - sp->jpeg_interchange_format;
        }
    }
    sp->in_buffer_source = osibsNotSetYet;
    sp->in_buffer_next_strile = 0;
    sp->in_buffer_strile_count = tif->tif_dir.td_nstrips;
    sp->in_buffer_file_togo = 0;
    sp->in_buffer_togo = 0;
    do
    {
        if (OJPEGReadBytePeek(sp, &m) == 0)
            return (0);
        if (m != 255)
            break;
        OJPEGReadByteAdvance(sp);
        do
        {
            if (OJPEGReadByte(sp, &m) == 0)
                return (0);
        } while (m == 255);
        switch (m)
        {
            case JPEG_MARKER_SOI:
                /* this type of marker has no data, and should be skipped */
                break;
            case JPEG_MARKER_COM:
            case JPEG_MARKER_APP0:
            case JPEG_MARKER_APP0 + 1:
            case JPEG_MARKER_APP0 + 2:
            case JPEG_MARKER_APP0 + 3:
            case JPEG_MARKER_APP0 + 4:
            case JPEG_MARKER_APP0 + 5:
            case JPEG_MARKER_APP0 + 6:
            case JPEG_MARKER_APP0 + 7:
            case JPEG_MARKER_APP0 + 8:
            case JPEG_MARKER_APP0 + 9:
            case JPEG_MARKER_APP0 + 10:
            case JPEG_MARKER_APP0 + 11:
            case JPEG_MARKER_APP0 + 12:
            case JPEG_MARKER_APP0 + 13:
            case JPEG_MARKER_APP0 + 14:
            case JPEG_MARKER_APP0 + 15:
                /* this type of marker has data, but it has no use to us (and no
                 * place here) and should be skipped */
                if (OJPEGReadWord(sp, &n) == 0)
                    return (0);
                if (n < 2)
                {
                    if (sp->subsamplingcorrect == 0)
                        TIFFErrorExtR(tif, module, "Corrupt JPEG data");
                    return (0);
                }
                if (n > 2)
                    OJPEGReadSkip(sp, (uint16_t)(n - 2));
                break;
            case JPEG_MARKER_DRI:
                if (OJPEGReadHeaderInfoSecStreamDri(tif) == 0)
                    return (0);
                break;
            case JPEG_MARKER_DQT:
                if (OJPEGReadHeaderInfoSecStreamDqt(tif) == 0)
                    return (0);
                break;
            case JPEG_MARKER_DHT:
                if (OJPEGReadHeaderInfoSecStreamDht(tif) == 0)
                    return (0);
                break;
            case JPEG_MARKER_SOF0:
            case JPEG_MARKER_SOF1:
            case JPEG_MARKER_SOF3:
                if (OJPEGReadHeaderInfoSecStreamSof(tif, m) == 0)
                    return (0);
                if (sp->subsamplingcorrect != 0)
                    return (1);
                break;
            case JPEG_MARKER_SOS:
                if (sp->subsamplingcorrect != 0)
                    return (1);
                assert(sp->plane_sample_offset == 0);
                if (OJPEGReadHeaderInfoSecStreamSos(tif) == 0)
                    return (0);
                break;
            default:
                TIFFErrorExtR(tif, module,
                              "Unknown marker type %" PRIu8 " in JPEG data", m);
                return (0);
        }
    } while (m != JPEG_MARKER_SOS);
    if (sp->subsamplingcorrect)
        return (1);
    if (sp->sof_log == 0)
    {
        if (OJPEGReadHeaderInfoSecTablesQTable(tif) == 0)
            return (0);
        sp->sof_marker_id = JPEG_MARKER_SOF0;
        for (o = 0; o < sp->samples_per_pixel; o++)
            sp->sof_c[o] = (uint8_t)o;
        sp->sof_hv[0] =
            (uint8_t)((sp->subsampling_hor << 4) | sp->subsampling_ver);
        for (o = 1; o < sp->samples_per_pixel; o++)
            sp->sof_hv[o] = 17;
        sp->sof_x = sp->strile_width;
        sp->sof_y = sp->strile_length_total;
        sp->sof_log = 1;
        if (OJPEGReadHeaderInfoSecTablesDcTable(tif) == 0)
            return (0);
        if (OJPEGReadHeaderInfoSecTablesAcTable(tif) == 0)
            return (0);
        for (o = 1; o < sp->samples_per_pixel; o++)
            sp->sos_cs[o] = o;
    }
    return (1);
}

/**
 * @brief Parse and cache a JPEG restart interval.
 * @ingroup libtiff_private_ojpeg
 *
 * Consumes a big-endian length and interval, requires marker length four,
 * diagnoses a bad length and stores the interval after both reads. A short
 * read fails without a codec diagnostic. No allocation; input progress
 * remains on failure.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 * @return One on success, zero on a bad marker length or a short read.
 */
static int OJPEGReadHeaderInfoSecStreamDri(TIFF *tif)
{
    /* This could easily cause trouble in some cases... but no such cases have
       occurred so far */
    static const char module[] = "OJPEGReadHeaderInfoSecStreamDri";
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    uint16_t m;
    if (OJPEGReadWord(sp, &m) == 0)
        return (0);
    if (m != 4)
    {
        TIFFErrorExtR(tif, module, "Corrupt DRI marker in JPEG data");
        return (0);
    }
    if (OJPEGReadWord(sp, &m) == 0)
        return (0);
    sp->restart_interval = m;
    return (1);
}

/**
 * @brief Capture eight-bit JPEG quantisation table markers.
 * @ingroup libtiff_private_ojpeg
 *
 * Validates marker length and table slot 0..3, allocates owned
 * length-prefixed marker blobs and replaces each previous slot only after
 * reading that table. Probe mode skips payload instead. Earlier table
 * replacements and input progress remain if a later table fails; temporary
 * failed blobs are freed. No caller ownership transfer.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 * @return One on completion, zero on a malformed marker, allocation
 * failure or short read; earlier table replacements and input progress
 * remain.
 */
static int OJPEGReadHeaderInfoSecStreamDqt(TIFF *tif)
{
    /* this is a table marker, and it is to be saved as a whole for exact
     * pushing on the jpeg stream later on */
    static const char module[] = "OJPEGReadHeaderInfoSecStreamDqt";
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    uint16_t m;
    uint32_t na;
    uint8_t *nb;
    uint8_t o;
    if (OJPEGReadWord(sp, &m) == 0)
        return (0);
    if (m <= 2)
    {
        if (sp->subsamplingcorrect == 0)
            TIFFErrorExtR(tif, module, "Corrupt DQT marker in JPEG data");
        return (0);
    }
    if (sp->subsamplingcorrect != 0)
        OJPEGReadSkip(sp, (uint16_t)(m - 2));
    else
    {
        m = (uint16_t)(m - 2);
        do
        {
            if (m < 65)
            {
                TIFFErrorExtR(tif, module, "Corrupt DQT marker in JPEG data");
                return (0);
            }
            na = sizeof(uint32_t) + 69;
            nb = (uint8_t *)_TIFFmallocExt(tif, na);
            if (nb == 0)
            {
                TIFFErrorExtR(tif, module, "Out of memory");
                return (0);
            }
            *(uint32_t *)nb = na;
            nb[sizeof(uint32_t)] = 255;
            nb[sizeof(uint32_t) + 1] = JPEG_MARKER_DQT;
            nb[sizeof(uint32_t) + 2] = 0;
            nb[sizeof(uint32_t) + 3] = 67;
            if (OJPEGReadBlock(sp, 65, &nb[sizeof(uint32_t) + 4]) == 0)
            {
                _TIFFfreeExt(tif, nb);
                return (0);
            }
            o = nb[sizeof(uint32_t) + 4] & 15;
            if (3 < o)
            {
                TIFFErrorExtR(tif, module, "Corrupt DQT marker in JPEG data");
                _TIFFfreeExt(tif, nb);
                return (0);
            }
            if (sp->qtable[o] != 0)
                _TIFFfreeExt(tif, sp->qtable[o]);
            sp->qtable[o] = nb;
            m = (uint16_t)(m - 65);
        } while (m > 0);
    }
    return (1);
}

/**
 * @brief Capture a JPEG Huffman marker in its selected table slot.
 * @ingroup libtiff_private_ojpeg
 *
 * Copies the complete marker into an owned length-prefixed blob, validates
 * the first class/slot and replaces the old DC/AC slot. Retains the
 * existing assumption that one marker describes one table. Probe mode skips
 * the payload. Failure frees the new temporary blob but preserves consumed
 * input and earlier changes.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 * @return One on completion, zero on a malformed marker, allocation
 * failure or short read; earlier table replacements and input progress
 * remain.
 */
static int OJPEGReadHeaderInfoSecStreamDht(TIFF *tif)
{
    /* this is a table marker, and it is to be saved as a whole for exact
     * pushing on the jpeg stream later on */
    /* TODO: the following assumes there is only one table in this marker... but
     * i'm not quite sure that assumption is guaranteed correct */
    static const char module[] = "OJPEGReadHeaderInfoSecStreamDht";
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    uint16_t m;
    uint32_t na;
    uint8_t *nb;
    uint8_t o;
    if (OJPEGReadWord(sp, &m) == 0)
        return (0);
    if (m <= 2)
    {
        if (sp->subsamplingcorrect == 0)
            TIFFErrorExtR(tif, module, "Corrupt DHT marker in JPEG data");
        return (0);
    }
    if (sp->subsamplingcorrect != 0)
    {
        OJPEGReadSkip(sp, (uint16_t)(m - 2));
    }
    else
    {
        na = (uint32_t)(sizeof(uint32_t) + 2 + m);
        nb = (uint8_t *)_TIFFmallocExt(tif, na);
        if (nb == 0)
        {
            TIFFErrorExtR(tif, module, "Out of memory");
            return (0);
        }
        *(uint32_t *)nb = na;
        nb[sizeof(uint32_t)] = 255;
        nb[sizeof(uint32_t) + 1] = JPEG_MARKER_DHT;
        nb[sizeof(uint32_t) + 2] = (uint8_t)(m >> 8);
        nb[sizeof(uint32_t) + 3] = (uint8_t)(m & 255);
        if (OJPEGReadBlock(sp, (uint16_t)(m - 2), &nb[sizeof(uint32_t) + 4]) ==
            0)
        {
            _TIFFfreeExt(tif, nb);
            return (0);
        }
        o = nb[sizeof(uint32_t) + 4];
        if ((o & 240) == 0)
        {
            if (3 < o)
            {
                TIFFErrorExtR(tif, module, "Corrupt DHT marker in JPEG data");
                _TIFFfreeExt(tif, nb);
                return (0);
            }
            if (sp->dctable[o] != 0)
                _TIFFfreeExt(tif, sp->dctable[o]);
            sp->dctable[o] = nb;
        }
        else
        {
            if ((o & 240) != 16)
            {
                TIFFErrorExtR(tif, module, "Corrupt DHT marker in JPEG data");
                _TIFFfreeExt(tif, nb);
                return (0);
            }
            o &= 15;
            if (3 < o)
            {
                TIFFErrorExtR(tif, module, "Corrupt DHT marker in JPEG data");
                _TIFFfreeExt(tif, nb);
                return (0);
            }
            if (sp->actable[o] != 0)
                _TIFFfreeExt(tif, sp->actable[o]);
            sp->actable[o] = nb;
        }
    }
    return (1);
}

/**
 * @brief Validate a JPEG frame and cache its component selectors.
 * @ingroup libtiff_private_ojpeg
 *
 * Requires supported marker_id and bounded component count consistent with
 * the image outside probe mode. Validates eight-bit precision, dimensions
 * and sampling; probe mode instead discovers factors and can force
 * dependency desubsampling. Reads/cache assignments are incremental, with
 * selected diagnostics suppressed during probing. No allocation or
 * ownership transfer.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 * @param[in] marker_id Supported SOF marker code.
 * @return One on success, zero on an invalid or unsupported frame header or
 * a short read.
 */
static int OJPEGReadHeaderInfoSecStreamSof(TIFF *tif, uint8_t marker_id)
{
    /* this marker needs to be checked, and part of its data needs to be saved
     * for regeneration later on */
    static const char module[] = "OJPEGReadHeaderInfoSecStreamSof";
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    uint16_t m;
    uint16_t n;
    uint8_t o;
    uint16_t p;
    uint16_t q;
    if (sp->sof_log != 0)
    {
        TIFFErrorExtR(tif, module, "Corrupt JPEG data");
        return (0);
    }
    if (sp->subsamplingcorrect == 0)
        sp->sof_marker_id = marker_id;
    /* Lf: data length */
    if (OJPEGReadWord(sp, &m) == 0)
        return (0);
    if (m < 11)
    {
        if (sp->subsamplingcorrect == 0)
            TIFFErrorExtR(tif, module, "Corrupt SOF marker in JPEG data");
        return (0);
    }
    m = (uint16_t)(m - 8);
    if (m % 3 != 0)
    {
        if (sp->subsamplingcorrect == 0)
            TIFFErrorExtR(tif, module, "Corrupt SOF marker in JPEG data");
        return (0);
    }
    n = m / 3;
    if (sp->subsamplingcorrect == 0)
    {
        if (n != sp->samples_per_pixel)
        {
            TIFFErrorExtR(
                tif, module,
                "JPEG compressed data indicates unexpected number of samples");
            return (0);
        }
    }
    /* P: Sample precision */
    if (OJPEGReadByte(sp, &o) == 0)
        return (0);
    if (o != 8)
    {
        if (sp->subsamplingcorrect == 0)
            TIFFErrorExtR(tif, module,
                          "JPEG compressed data indicates unexpected number of "
                          "bits per sample");
        return (0);
    }
    /* Y: Number of lines, X: Number of samples per line */
    if (sp->subsamplingcorrect)
        OJPEGReadSkip(sp, 4);
    else
    {
        /* Y: Number of lines */
        if (OJPEGReadWord(sp, &p) == 0)
            return (0);
        if (((uint32_t)p < sp->image_length) &&
            ((uint32_t)p < sp->strile_length_total))
        {
            TIFFErrorExtR(tif, module,
                          "JPEG compressed data indicates unexpected height");
            return (0);
        }
        sp->sof_y = p;
        /* X: Number of samples per line */
        if (OJPEGReadWord(sp, &p) == 0)
            return (0);
        if (((uint32_t)p < sp->image_width) && ((uint32_t)p < sp->strile_width))
        {
            TIFFErrorExtR(tif, module,
                          "JPEG compressed data indicates unexpected width");
            return (0);
        }
        if ((uint32_t)p > sp->strile_width)
        {
            TIFFErrorExtR(tif, module,
                          "JPEG compressed data image width exceeds expected "
                          "image width");
            return (0);
        }
        sp->sof_x = p;
    }
    /* Nf: Number of image components in frame */
    if (OJPEGReadByte(sp, &o) == 0)
        return (0);
    if (o != n)
    {
        if (sp->subsamplingcorrect == 0)
            TIFFErrorExtR(tif, module, "Corrupt SOF marker in JPEG data");
        return (0);
    }
    /* per component stuff */
    /* TODO: double-check that flow implies that n cannot be as big as to make
     * us overflow sof_c, sof_hv and sof_tq arrays */
    for (q = 0; q < n; q++)
    {
        /* C: Component identifier */
        if (OJPEGReadByte(sp, &o) == 0)
            return (0);
        if (sp->subsamplingcorrect == 0)
            sp->sof_c[q] = o;
        /* H: Horizontal sampling factor, and V: Vertical sampling factor */
        if (OJPEGReadByte(sp, &o) == 0)
            return (0);
        if (sp->subsamplingcorrect != 0)
        {
            if (q == 0)
            {
                sp->subsampling_hor = (o >> 4);
                sp->subsampling_ver = (o & 15);
                if (((sp->subsampling_hor != 1) && (sp->subsampling_hor != 2) &&
                     (sp->subsampling_hor != 4)) ||
                    ((sp->subsampling_ver != 1) && (sp->subsampling_ver != 2) &&
                     (sp->subsampling_ver != 4)))
                    sp->subsampling_force_desubsampling_inside_decompression =
                        1;
            }
            else
            {
                if (o != 17)
                    sp->subsampling_force_desubsampling_inside_decompression =
                        1;
            }
        }
        else
        {
            sp->sof_hv[q] = o;
            if (sp->subsampling_force_desubsampling_inside_decompression == 0)
            {
                if (q == 0)
                {
                    if (o != ((sp->subsampling_hor << 4) | sp->subsampling_ver))
                    {
                        TIFFErrorExtR(tif, module,
                                      "JPEG compressed data indicates "
                                      "unexpected subsampling values");
                        return (0);
                    }
                }
                else
                {
                    if (o != 17)
                    {
                        TIFFErrorExtR(tif, module,
                                      "JPEG compressed data indicates "
                                      "unexpected subsampling values");
                        return (0);
                    }
                }
            }
        }
        /* Tq: Quantization table destination selector */
        if (OJPEGReadByte(sp, &o) == 0)
            return (0);
        if (sp->subsamplingcorrect == 0)
            sp->sof_tq[q] = o;
    }
    if (sp->subsamplingcorrect == 0)
        sp->sof_log = 1;
    return (1);
}

/**
 * @brief Validate and cache a JPEG scan component header.
 * @ingroup libtiff_private_ojpeg
 *
 * Requires non-probe mode, a parsed frame and valid plane/component bounds
 * at most three. Validates scan length/count, stores component/table
 * selectors and skips the final three spectral/approximation bytes without
 * validating them. Partial cache/input changes survive failure; no
 * allocation.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 * @return One on success, zero on a malformed SOS marker (diagnosed) or a
 * short read (no codec diagnostic).
 */
static int OJPEGReadHeaderInfoSecStreamSos(TIFF *tif)
{
    /* this marker needs to be checked, and part of its data needs to be saved
     * for regeneration later on */
    static const char module[] = "OJPEGReadHeaderInfoSecStreamSos";
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    uint16_t m;
    uint8_t n;
    uint8_t o;
    assert(sp->subsamplingcorrect == 0);
    if (sp->sof_log == 0)
    {
        TIFFErrorExtR(tif, module, "Corrupt SOS marker in JPEG data");
        return (0);
    }
    /* Ls */
    if (OJPEGReadWord(sp, &m) == 0)
        return (0);
    if (m != 6 + sp->samples_per_pixel_per_plane * 2)
    {
        TIFFErrorExtR(tif, module, "Corrupt SOS marker in JPEG data");
        return (0);
    }
    /* Ns */
    if (OJPEGReadByte(sp, &n) == 0)
        return (0);
    if (n != sp->samples_per_pixel_per_plane)
    {
        TIFFErrorExtR(tif, module, "Corrupt SOS marker in JPEG data");
        return (0);
    }
    /* Cs, Td, and Ta */
    for (o = 0; o < sp->samples_per_pixel_per_plane; o++)
    {
        /* Cs */
        if (OJPEGReadByte(sp, &n) == 0)
            return (0);
        sp->sos_cs[sp->plane_sample_offset + o] = n;
        /* Td and Ta */
        if (OJPEGReadByte(sp, &n) == 0)
            return (0);
        sp->sos_tda[sp->plane_sample_offset + o] = n;
    }
    /* skip Ss, Se, Ah, en Al -> no check, as per Tom Lane recommendation, as
     * per LibJpeg source */
    OJPEGReadSkip(sp, 3);
    return (1);
}

/**
 * @brief Build quantisation markers from TIFF table-offset tags.
 * @ingroup libtiff_private_ojpeg
 *
 * Requires one to three valid components. Reports a missing first offset
 * with an error. Reads client data directly at offsets, builds owned
 * length-prefixed marker blobs and caches selectors, reusing adjacent
 * equal/omitted offsets. Non-adjacent duplicates can be rejected. Seek return
 * status is not checked separately; a short read fails without a codec
 * diagnostic. Frees a failed temporary blob, retains earlier replacements and
 * leaves file position changed.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 * @return One on success; zero on a missing or duplicated offset or
 * allocation failure (diagnosed), or on a short read (not diagnosed).
 */
static int OJPEGReadHeaderInfoSecTablesQTable(TIFF *tif)
{
    static const char module[] = "OJPEGReadHeaderInfoSecTablesQTable";
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    uint8_t m;
    uint8_t n;
    uint32_t oa;
    uint8_t *ob;
    uint32_t p;
    if (sp->qtable_offset[0] == 0)
    {
        TIFFErrorExtR(tif, module, "Missing JPEG tables");
        return (0);
    }
    sp->in_buffer_file_pos_log = 0;
    for (m = 0; m < sp->samples_per_pixel; m++)
    {
        if ((sp->qtable_offset[m] != 0) &&
            ((m == 0) || (sp->qtable_offset[m] != sp->qtable_offset[m - 1])))
        {
            for (n = 0; n < m - 1; n++)
            {
                if (sp->qtable_offset[m] == sp->qtable_offset[n])
                {
                    TIFFErrorExtR(tif, module, "Corrupt JpegQTables tag value");
                    return (0);
                }
            }
            oa = sizeof(uint32_t) + 69;
            ob = (uint8_t *)_TIFFmallocExt(tif, oa);
            if (ob == 0)
            {
                TIFFErrorExtR(tif, module, "Out of memory");
                return (0);
            }
            *(uint32_t *)ob = oa;
            ob[sizeof(uint32_t)] = 255;
            ob[sizeof(uint32_t) + 1] = JPEG_MARKER_DQT;
            ob[sizeof(uint32_t) + 2] = 0;
            ob[sizeof(uint32_t) + 3] = 67;
            ob[sizeof(uint32_t) + 4] = m;
            TIFFSeekFile(tif, sp->qtable_offset[m], SEEK_SET);
            p = (uint32_t)TIFFReadFile(tif, &ob[sizeof(uint32_t) + 5], 64);
            if (p != 64)
            {
                _TIFFfreeExt(tif, ob);
                return (0);
            }
            if (sp->qtable[m] != 0)
                _TIFFfreeExt(tif, sp->qtable[m]);
            sp->qtable[m] = ob;
            sp->sof_tq[m] = m;
        }
        else
            sp->sof_tq[m] = sp->sof_tq[m - 1];
    }
    return (1);
}

/**
 * @brief Build DC Huffman markers from TIFF table-offset tags.
 * @ingroup libtiff_private_ojpeg
 *
 * Requires one to three valid components. Reports a missing first offset
 * with an error. Reads client data directly at offsets, builds owned
 * length-prefixed marker blobs and caches selectors, reusing adjacent
 * equal/omitted offsets. Non-adjacent duplicates can be rejected. Seek return
 * status is not checked separately; a short read fails without a codec
 * diagnostic. Frees a failed temporary blob, retains earlier replacements and
 * leaves file position changed.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 * @return One on success; zero on a missing or duplicated offset or
 * allocation failure (diagnosed), or on a short read (not diagnosed).
 */
static int OJPEGReadHeaderInfoSecTablesDcTable(TIFF *tif)
{
    static const char module[] = "OJPEGReadHeaderInfoSecTablesDcTable";
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    uint8_t m;
    uint8_t n;
    uint8_t o[16];
    uint32_t p;
    uint32_t q;
    uint32_t ra;
    uint8_t *rb;
    if (sp->dctable_offset[0] == 0)
    {
        TIFFErrorExtR(tif, module, "Missing JPEG tables");
        return (0);
    }
    sp->in_buffer_file_pos_log = 0;
    for (m = 0; m < sp->samples_per_pixel; m++)
    {
        if ((sp->dctable_offset[m] != 0) &&
            ((m == 0) || (sp->dctable_offset[m] != sp->dctable_offset[m - 1])))
        {
            for (n = 0; n < m - 1; n++)
            {
                if (sp->dctable_offset[m] == sp->dctable_offset[n])
                {
                    TIFFErrorExtR(tif, module,
                                  "Corrupt JpegDcTables tag value");
                    return (0);
                }
            }
            TIFFSeekFile(tif, sp->dctable_offset[m], SEEK_SET);
            p = (uint32_t)TIFFReadFile(tif, o, 16);
            if (p != 16)
                return (0);
            q = 0;
            for (n = 0; n < 16; n++)
                q += o[n];
            ra = (uint32_t)(sizeof(uint32_t) + 21 + q);
            rb = (uint8_t *)_TIFFmallocExt(tif, ra);
            if (rb == 0)
            {
                TIFFErrorExtR(tif, module, "Out of memory");
                return (0);
            }
            *(uint32_t *)rb = ra;
            rb[sizeof(uint32_t)] = 255;
            rb[sizeof(uint32_t) + 1] = JPEG_MARKER_DHT;
            rb[sizeof(uint32_t) + 2] = (uint8_t)((19 + q) >> 8);
            rb[sizeof(uint32_t) + 3] = ((19 + q) & 255);
            rb[sizeof(uint32_t) + 4] = m;
            for (n = 0; n < 16; n++)
                rb[sizeof(uint32_t) + 5 + n] = o[n];
            p = (uint32_t)TIFFReadFile(tif, &(rb[sizeof(uint32_t) + 21]), q);
            if (p != q)
            {
                _TIFFfreeExt(tif, rb);
                return (0);
            }
            if (sp->dctable[m] != 0)
                _TIFFfreeExt(tif, sp->dctable[m]);
            sp->dctable[m] = rb;
            sp->sos_tda[m] = (uint8_t)(m << 4);
        }
        else
            sp->sos_tda[m] = sp->sos_tda[m - 1];
    }
    return (1);
}

/**
 * @brief Build AC Huffman markers from TIFF table-offset tags.
 * @ingroup libtiff_private_ojpeg
 *
 * Requires one to three valid components. Reports a missing first offset
 * with an error. Reads client data directly at offsets, builds owned
 * length-prefixed marker blobs and caches selectors, reusing adjacent
 * equal/omitted offsets. Non-adjacent duplicates can be rejected. Seek return
 * status is not checked separately; a short read fails without a codec
 * diagnostic. Frees a failed temporary blob, retains earlier replacements and
 * leaves file position changed.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 * @return One on success; zero on a missing or duplicated offset or
 * allocation failure (diagnosed), or on a short read (not diagnosed).
 */
static int OJPEGReadHeaderInfoSecTablesAcTable(TIFF *tif)
{
    static const char module[] = "OJPEGReadHeaderInfoSecTablesAcTable";
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    uint8_t m;
    uint8_t n;
    uint8_t o[16];
    uint32_t p;
    uint32_t q;
    uint32_t ra;
    uint8_t *rb;
    if (sp->actable_offset[0] == 0)
    {
        TIFFErrorExtR(tif, module, "Missing JPEG tables");
        return (0);
    }
    sp->in_buffer_file_pos_log = 0;
    for (m = 0; m < sp->samples_per_pixel; m++)
    {
        if ((sp->actable_offset[m] != 0) &&
            ((m == 0) || (sp->actable_offset[m] != sp->actable_offset[m - 1])))
        {
            for (n = 0; n < m - 1; n++)
            {
                if (sp->actable_offset[m] == sp->actable_offset[n])
                {
                    TIFFErrorExtR(tif, module,
                                  "Corrupt JpegAcTables tag value");
                    return (0);
                }
            }
            TIFFSeekFile(tif, sp->actable_offset[m], SEEK_SET);
            p = (uint32_t)TIFFReadFile(tif, o, 16);
            if (p != 16)
                return (0);
            q = 0;
            for (n = 0; n < 16; n++)
                q += o[n];
            ra = (uint32_t)(sizeof(uint32_t) + 21 + q);
            rb = (uint8_t *)_TIFFmallocExt(tif, ra);
            if (rb == 0)
            {
                TIFFErrorExtR(tif, module, "Out of memory");
                return (0);
            }
            *(uint32_t *)rb = ra;
            rb[sizeof(uint32_t)] = 255;
            rb[sizeof(uint32_t) + 1] = JPEG_MARKER_DHT;
            rb[sizeof(uint32_t) + 2] = (uint8_t)((19 + q) >> 8);
            rb[sizeof(uint32_t) + 3] = ((19 + q) & 255);
            rb[sizeof(uint32_t) + 4] = (16 | m);
            for (n = 0; n < 16; n++)
                rb[sizeof(uint32_t) + 5 + n] = o[n];
            p = (uint32_t)TIFFReadFile(tif, &(rb[sizeof(uint32_t) + 21]), q);
            if (p != q)
            {
                _TIFFfreeExt(tif, rb);
                return (0);
            }
            if (sp->actable[m] != 0)
                _TIFFfreeExt(tif, sp->actable[m]);
            sp->actable[m] = rb;
            sp->sos_tda[m] = (sp->sos_tda[m] | m);
        }
        else
            sp->sos_tda[m] = (sp->sos_tda[m] | (sp->sos_tda[m - 1] & 15));
    }
    return (1);
}

/**
 * @brief Refill the old-style JPEG input window across interchange and strile
 * extents.
 * @ingroup libtiff_private_ojpeg
 *
 * Requires no unread bytes. Traverses normalized interchange data then
 * bounded strile offsets/counts, skipping empty offsets and clamping
 * lengths to file size. Zero bytecount uses remaining file extent. Can
 * fetch strile metadata, seek and read; seek status is not checked
 * separately and positive short reads are accepted. Maintains embedded
 * storage/cursors without transferring ownership.
 *
 * @param[in,out] sp Codec state whose input window, source and extent
 * cursors are updated.
 * @return One with nonempty input, zero at EOF or detected read/metadata
 * failure.
 */
static int OJPEGReadBufferFill(OJPEGState *sp)
{
    uint16_t m;
    tmsize_t n;
    /* TODO: double-check: when subsamplingcorrect is set, no call to
     * TIFFErrorExt or TIFFWarningExt should be made in any other case, seek or
     * read errors should be passed through */
    do
    {
        if (sp->in_buffer_file_togo != 0)
        {
            if (sp->in_buffer_file_pos_log == 0)
            {
                TIFFSeekFile(sp->tif, sp->in_buffer_file_pos, SEEK_SET);
                sp->in_buffer_file_pos_log = 1;
            }
            m = OJPEG_BUFFER;
            if ((uint64_t)m > sp->in_buffer_file_togo)
                m = (uint16_t)sp->in_buffer_file_togo;
            n = TIFFReadFile(sp->tif, sp->in_buffer, (tmsize_t)m);
            if (n <= 0)
                return (0);
            assert(n > 0);
            assert(n <= OJPEG_BUFFER);
            assert(n < 65536);
            assert((uint64_t)n <= sp->in_buffer_file_togo);
            m = (uint16_t)n;
            sp->in_buffer_togo = m;
            sp->in_buffer_cur = sp->in_buffer;
            sp->in_buffer_file_togo -= m;
            sp->in_buffer_file_pos += m;
            break;
        }
        sp->in_buffer_file_pos_log = 0;
        switch (sp->in_buffer_source)
        {
            case osibsNotSetYet:
                if (sp->jpeg_interchange_format != 0)
                {
                    sp->in_buffer_file_pos = sp->jpeg_interchange_format;
                    sp->in_buffer_file_togo =
                        sp->jpeg_interchange_format_length;
                }
                sp->in_buffer_source = osibsJpegInterchangeFormat;
                break;
            case osibsJpegInterchangeFormat:
                sp->in_buffer_source = osibsStrile;
                break;
            case osibsStrile:
                if (sp->in_buffer_next_strile == sp->in_buffer_strile_count)
                    sp->in_buffer_source = osibsEof;
                else
                {
                    int err = 0;
                    sp->in_buffer_file_pos = TIFFGetStrileOffsetWithErr(
                        sp->tif, sp->in_buffer_next_strile, &err);
                    if (err)
                        return 0;
                    if (sp->in_buffer_file_pos != 0)
                    {
                        uint64_t bytecount = TIFFGetStrileByteCountWithErr(
                            sp->tif, sp->in_buffer_next_strile, &err);
                        if (err)
                            return 0;
                        if (sp->in_buffer_file_pos >= sp->file_size)
                            sp->in_buffer_file_pos = 0;
                        else if (bytecount == 0)
                            sp->in_buffer_file_togo =
                                sp->file_size - sp->in_buffer_file_pos;
                        else
                        {
                            sp->in_buffer_file_togo = bytecount;
                            if (sp->in_buffer_file_togo == 0)
                                sp->in_buffer_file_pos = 0;
                            else if (sp->in_buffer_file_pos >
                                         UINT64_MAX - sp->in_buffer_file_togo ||
                                     sp->in_buffer_file_pos +
                                             sp->in_buffer_file_togo >
                                         sp->file_size)
                                sp->in_buffer_file_togo =
                                    sp->file_size - sp->in_buffer_file_pos;
                        }
                    }
                    sp->in_buffer_next_strile++;
                }
                break;
            case osibsEof:
            default:
                return (0);
        }
    } while (1);
    return (1);
}

/**
 * @brief Consume one buffered byte.
 * @ingroup libtiff_private_ojpeg
 *
 * Refills as needed and advances input; assigns byte only after success.
 * Caller owns result storage; no retained caller pointer. Read failures may
 * be diagnosed by lower layers; no rollback.
 *
 * @param[in,out] sp Codec state whose input window is consumed and
 * refilled as needed.
 * @param[out] byte Non-NULL caller-owned byte result.
 * @return One after complete output, zero on refill/read failure.
 */
static int OJPEGReadByte(OJPEGState *sp, uint8_t *byte)
{
    if (sp->in_buffer_togo == 0)
    {
        if (OJPEGReadBufferFill(sp) == 0)
            return (0);
        assert(sp->in_buffer_togo > 0);
    }
    *byte = *(sp->in_buffer_cur);
    sp->in_buffer_cur++;
    sp->in_buffer_togo--;
    return (1);
}

/**
 * @brief Peek at the next buffered byte.
 * @ingroup libtiff_private_ojpeg
 *
 * May refill and perform client I/O but leaves the returned byte
 * unconsumed; assigns byte only after success. Caller owns result storage;
 * no retained caller pointer. Read failures may be diagnosed by lower
 * layers; no rollback.
 *
 * @param[in,out] sp Codec state whose input window may be refilled; the
 * cursor is not advanced.
 * @param[out] byte Non-NULL caller-owned byte result.
 * @return One after complete output, zero on refill/read failure.
 */
static int OJPEGReadBytePeek(OJPEGState *sp, uint8_t *byte)
{
    if (sp->in_buffer_togo == 0)
    {
        if (OJPEGReadBufferFill(sp) == 0)
            return (0);
        assert(sp->in_buffer_togo > 0);
    }
    *byte = *(sp->in_buffer_cur);
    return (1);
}

/**
 * @brief Consume one already buffered byte.
 * @ingroup libtiff_private_ojpeg
 *
 * Requires in_buffer_togo greater than zero, asserted in debug builds.
 * Advances the embedded cursor/count without refilling, allocation,
 * diagnostics or ownership transfer.
 *
 * @param[in,out] sp Codec state whose input cursor and count are
 * advanced.
 */
static void OJPEGReadByteAdvance(OJPEGState *sp)
{
    assert(sp->in_buffer_togo > 0);
    sp->in_buffer_cur++;
    sp->in_buffer_togo--;
}

/**
 * @brief Consume a big-endian sixteen-bit word.
 * @ingroup libtiff_private_ojpeg
 *
 * Reads two bytes; after the first succeeds, word already contains its high
 * byte, so second-byte failure leaves a partial result. Caller owns result
 * storage; no retained caller pointer. Read failures may be diagnosed by
 * lower layers; no rollback.
 *
 * @param[in,out] sp Codec state whose input window is consumed and
 * refilled as needed.
 * @param[out] word Non-NULL caller-owned word result; can be partial on
 * failure.
 * @return One after complete output, zero on refill/read failure.
 */
static int OJPEGReadWord(OJPEGState *sp, uint16_t *word)
{
    uint8_t m;
    if (OJPEGReadByte(sp, &m) == 0)
        return (0);
    *word = (uint16_t)(m << 8);
    if (OJPEGReadByte(sp, &m) == 0)
        return (0);
    *word |= m;
    return (1);
}

/**
 * @brief Copy a positive byte block from the input stream.
 * @ingroup libtiff_private_ojpeg
 *
 * Requires len greater than zero and mem writable for len bytes. Can refill
 * repeatedly; failure retains partial output and consumed bytes. Caller
 * owns result storage; no retained caller pointer. Read failures may be
 * diagnosed by lower layers; no rollback.
 *
 * @param[in,out] sp Codec state whose input window is consumed and
 * refilled as needed.
 * @param[in] len Number of bytes to copy; must be positive (asserted).
 * @param[out] mem Destination of at least len writable bytes.
 * @return One after complete output, zero on refill/read failure.
 */
static int OJPEGReadBlock(OJPEGState *sp, uint16_t len, void *mem)
{
    uint16_t mlen;
    uint8_t *mmem;
    uint16_t n;
    assert(len > 0);
    mlen = len;
    mmem = (uint8_t *)mem;
    do
    {
        if (sp->in_buffer_togo == 0)
        {
            if (OJPEGReadBufferFill(sp) == 0)
                return (0);
            assert(sp->in_buffer_togo > 0);
        }
        n = mlen;
        if (n > sp->in_buffer_togo)
            n = sp->in_buffer_togo;
        _TIFFmemcpy(mmem, sp->in_buffer_cur, n);
        sp->in_buffer_cur += n;
        sp->in_buffer_togo = (uint16_t)(sp->in_buffer_togo - n);
        mlen = (uint16_t)(mlen - n);
        mmem += n;
    } while (mlen > 0);
    return (1);
}

/**
 * @brief Skip up to a bounded input extent without fetching skipped bytes.
 * @ingroup libtiff_private_ojpeg
 *
 * Consumes buffered data first, then advances file-window offsets and marks
 * repositioning required. An oversized skip silently exhausts the available
 * extent. No immediate client read, allocation, status result or ownership
 * transfer.
 *
 * @param[in,out] sp Codec state whose input window and file extent
 * cursors are advanced.
 * @param[in] len Number of bytes to skip; clamped to the remaining extent.
 */
static void OJPEGReadSkip(OJPEGState *sp, uint16_t len)
{
    uint16_t m;
    uint16_t n;
    m = len;
    n = m;
    if (n > sp->in_buffer_togo)
        n = sp->in_buffer_togo;
    sp->in_buffer_cur += n;
    sp->in_buffer_togo = (uint16_t)(sp->in_buffer_togo - n);
    m = (uint16_t)(m - n);
    if (m > 0)
    {
        assert(sp->in_buffer_togo == 0);
        n = m;
        if ((uint64_t)n > sp->in_buffer_file_togo)
            n = (uint16_t)sp->in_buffer_file_togo;
        sp->in_buffer_file_pos += n;
        sp->in_buffer_file_togo -= n;
        sp->in_buffer_file_pos_log = 0;
        /* we don't skip past jpeginterchangeformat/strile block...
         * if that is asked from us, we're dealing with totally bazurk
         * data anyway, and we've not seen this happening on any
         * testfile, so we might as well likely cause some other
         * meaningless error to be passed at some later time
         */
    }
}

/**
 * @brief Produce the next borrowed chunk of reconstructed decoder input.
 * @ingroup libtiff_private_ojpeg
 *
 * Advances the output state through synthetic markers, owned table blobs
 * and client compressed input. Skips absent tables internally. Can
 * read/seek and diagnose through refill. Returns pointers into embedded
 * buffers or owned tables, valid until next producer/refill, table
 * replacement or cleanup; consume synchronously and never free them. Does
 * not write TIFF output. Outputs need not be assigned on failure.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 * @param[out] mem Set to the start of the produced chunk on success.
 * @param[out] len Set to 0 on entry, then to the nonzero chunk length on
 * success; not preserved on failure.
 * @return One after producing a chunk, zero on compressed-input failure.
 */
static int OJPEGWriteStream(TIFF *tif, void **mem, uint32_t *len)
{
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    *len = 0;
    do
    {
        assert(sp->out_state <= ososEoi);
        switch (sp->out_state)
        {
            case ososSoi:
                OJPEGWriteStreamSoi(tif, mem, len);
                break;
            case ososQTable0:
                OJPEGWriteStreamQTable(tif, 0, mem, len);
                break;
            case ososQTable1:
                OJPEGWriteStreamQTable(tif, 1, mem, len);
                break;
            case ososQTable2:
                OJPEGWriteStreamQTable(tif, 2, mem, len);
                break;
            case ososQTable3:
                OJPEGWriteStreamQTable(tif, 3, mem, len);
                break;
            case ososDcTable0:
                OJPEGWriteStreamDcTable(tif, 0, mem, len);
                break;
            case ososDcTable1:
                OJPEGWriteStreamDcTable(tif, 1, mem, len);
                break;
            case ososDcTable2:
                OJPEGWriteStreamDcTable(tif, 2, mem, len);
                break;
            case ososDcTable3:
                OJPEGWriteStreamDcTable(tif, 3, mem, len);
                break;
            case ososAcTable0:
                OJPEGWriteStreamAcTable(tif, 0, mem, len);
                break;
            case ososAcTable1:
                OJPEGWriteStreamAcTable(tif, 1, mem, len);
                break;
            case ososAcTable2:
                OJPEGWriteStreamAcTable(tif, 2, mem, len);
                break;
            case ososAcTable3:
                OJPEGWriteStreamAcTable(tif, 3, mem, len);
                break;
            case ososDri:
                OJPEGWriteStreamDri(tif, mem, len);
                break;
            case ososSof:
                OJPEGWriteStreamSof(tif, mem, len);
                break;
            case ososSos:
                OJPEGWriteStreamSos(tif, mem, len);
                break;
            case ososCompressed:
                if (OJPEGWriteStreamCompressed(tif, mem, len) == 0)
                    return (0);
                break;
            case ososRst:
                OJPEGWriteStreamRst(tif, mem, len);
                break;
            case ososEoi:
                OJPEGWriteStreamEoi(tif, mem, len);
                break;
            default:
                break;
        }
    } while (*len == 0);
    return (1);
}

/**
 * @brief Produce a Soi reconstruction chunk.
 * @ingroup libtiff_private_ojpeg
 *
 * Writes SOI into out_buffer and advances to the quantisation tables. The
 * chunk is valid until the next producer overwrites out_buffer.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 * @param[out] mem Set to sp->out_buffer, which holds the marker.
 * @param[out] len Set to the marker length in bytes.
 */
static void OJPEGWriteStreamSoi(TIFF *tif, void **mem, uint32_t *len)
{
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    assert(OJPEG_BUFFER >= 2);
    sp->out_buffer[0] = 255;
    sp->out_buffer[1] = JPEG_MARKER_SOI;
    *len = 2;
    *mem = (void *)sp->out_buffer;
    sp->out_state = (OJPEGStateOutState)(sp->out_state + 1);
}

/**
 * @brief Produce a QTable reconstruction chunk.
 * @ingroup libtiff_private_ojpeg
 *
 * Borrows the selected quantisation marker blob, omitting its stored length
 * prefix. An absent table advances state without assigning outputs.
 * Requires table_index in 0..3. The chunk points into the stored table and
 * is valid until the table is replaced or the codec is cleaned up.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 * @param[in] table_index Table slot in 0..3.
 * @param[out] mem Set to the stored marker when the table is present;
 * otherwise unchanged.
 * @param[out] len Set to the marker length when the table is present;
 * otherwise unchanged.
 */
static void OJPEGWriteStreamQTable(TIFF *tif, uint8_t table_index, void **mem,
                                   uint32_t *len)
{
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    if (sp->qtable[table_index] != 0)
    {
        *mem = (void *)(sp->qtable[table_index] + sizeof(uint32_t));
        *len = (uint32_t)(*((uint32_t *)sp->qtable[table_index]) -
                          sizeof(uint32_t));
    }
    sp->out_state = (OJPEGStateOutState)(sp->out_state + 1);
}

/**
 * @brief Produce a DcTable reconstruction chunk.
 * @ingroup libtiff_private_ojpeg
 *
 * Borrows the selected DC Huffman marker blob, omitting its stored length
 * prefix. An absent table advances state without assigning outputs.
 * Requires table_index in 0..3. The chunk points into the stored table and
 * is valid until the table is replaced or the codec is cleaned up.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 * @param[in] table_index Table slot in 0..3.
 * @param[out] mem Set to the stored marker when the table is present;
 * otherwise unchanged.
 * @param[out] len Set to the marker length when the table is present;
 * otherwise unchanged.
 */
static void OJPEGWriteStreamDcTable(TIFF *tif, uint8_t table_index, void **mem,
                                    uint32_t *len)
{
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    if (sp->dctable[table_index] != 0)
    {
        *mem = (void *)(sp->dctable[table_index] + sizeof(uint32_t));
        *len = (uint32_t)(*((uint32_t *)sp->dctable[table_index]) -
                          sizeof(uint32_t));
    }
    sp->out_state = (OJPEGStateOutState)(sp->out_state + 1);
}

/**
 * @brief Produce a AcTable reconstruction chunk.
 * @ingroup libtiff_private_ojpeg
 *
 * Borrows the selected AC Huffman marker blob, omitting its stored length
 * prefix. An absent table advances state without assigning outputs.
 * Requires table_index in 0..3. The chunk points into the stored table and
 * is valid until the table is replaced or the codec is cleaned up.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 * @param[in] table_index Table slot in 0..3.
 * @param[out] mem Set to the stored marker when the table is present;
 * otherwise unchanged.
 * @param[out] len Set to the marker length when the table is present;
 * otherwise unchanged.
 */
static void OJPEGWriteStreamAcTable(TIFF *tif, uint8_t table_index, void **mem,
                                    uint32_t *len)
{
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    if (sp->actable[table_index] != 0)
    {
        *mem = (void *)(sp->actable[table_index] + sizeof(uint32_t));
        *len = (uint32_t)(*((uint32_t *)sp->actable[table_index]) -
                          sizeof(uint32_t));
    }
    sp->out_state = (OJPEGStateOutState)(sp->out_state + 1);
}

/**
 * @brief Produce a Dri reconstruction chunk.
 * @ingroup libtiff_private_ojpeg
 *
 * Writes a DRI marker for the cached restart interval into out_buffer, or
 * emits nothing when the interval is 0, and advances to frame output. The
 * chunk is valid until the next producer overwrites out_buffer.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 * @param[out] mem Set to sp->out_buffer when a DRI marker is emitted;
 * otherwise unchanged.
 * @param[out] len Set to 6 when a DRI marker is emitted; otherwise
 * unchanged.
 */
static void OJPEGWriteStreamDri(TIFF *tif, void **mem, uint32_t *len)
{
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    assert(OJPEG_BUFFER >= 6);
    if (sp->restart_interval != 0)
    {
        sp->out_buffer[0] = 255;
        sp->out_buffer[1] = JPEG_MARKER_DRI;
        sp->out_buffer[2] = 0;
        sp->out_buffer[3] = 4;
        sp->out_buffer[4] = (uint8_t)(sp->restart_interval >> 8);
        sp->out_buffer[5] = (uint8_t)(sp->restart_interval & 255);
        *len = 6;
        *mem = (void *)sp->out_buffer;
    }
    sp->out_state = (OJPEGStateOutState)(sp->out_state + 1);
}

/**
 * @brief Produce a Sof reconstruction chunk.
 * @ingroup libtiff_private_ojpeg
 *
 * Emits cached eight-bit frame/component geometry with representable marker
 * dimensions and at most three components into out_buffer, then advances
 * to SOS. The chunk is valid until the next producer overwrites
 * out_buffer.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 * @param[out] mem Set to sp->out_buffer, which holds the marker.
 * @param[out] len Set to the marker length in bytes.
 */
static void OJPEGWriteStreamSof(TIFF *tif, void **mem, uint32_t *len)
{
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    uint8_t m;
    assert(OJPEG_BUFFER >= 2 + 8 + sp->samples_per_pixel_per_plane * 3);
    assert(255 >= 8 + sp->samples_per_pixel_per_plane * 3);
    sp->out_buffer[0] = 255;
    sp->out_buffer[1] = sp->sof_marker_id;
    /* Lf */
    sp->out_buffer[2] = 0;
    sp->out_buffer[3] = (uint8_t)(8 + sp->samples_per_pixel_per_plane * 3);
    /* P */
    sp->out_buffer[4] = 8;
    /* Y */
    sp->out_buffer[5] = (uint8_t)(sp->sof_y >> 8);
    sp->out_buffer[6] = (uint8_t)(sp->sof_y & 255);
    /* X */
    sp->out_buffer[7] = (uint8_t)(sp->sof_x >> 8);
    sp->out_buffer[8] = (uint8_t)(sp->sof_x & 255);
    /* Nf */
    sp->out_buffer[9] = sp->samples_per_pixel_per_plane;
    for (m = 0; m < sp->samples_per_pixel_per_plane; m++)
    {
        /* C */
        sp->out_buffer[10 + m * 3] = sp->sof_c[sp->plane_sample_offset + m];
        /* H and V */
        sp->out_buffer[10 + m * 3 + 1] =
            sp->sof_hv[sp->plane_sample_offset + m];
        /* Tq */
        sp->out_buffer[10 + m * 3 + 2] =
            sp->sof_tq[sp->plane_sample_offset + m];
    }
    *len = 10 + (uint32_t)sp->samples_per_pixel_per_plane * 3;
    *mem = (void *)sp->out_buffer;
    sp->out_state = (OJPEGStateOutState)(sp->out_state + 1);
}

/**
 * @brief Produce a Sos reconstruction chunk.
 * @ingroup libtiff_private_ojpeg
 *
 * Writes an SOS marker with the current plane scan selectors into
 * out_buffer, requiring valid plane/component bounds, then advances to
 * compressed input. The chunk is valid until the next producer overwrites
 * out_buffer.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 * @param[out] mem Set to sp->out_buffer, which holds the marker.
 * @param[out] len Set to the marker length in bytes.
 */
static void OJPEGWriteStreamSos(TIFF *tif, void **mem, uint32_t *len)
{
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    uint8_t m;
    assert(OJPEG_BUFFER >= 2 + 6 + sp->samples_per_pixel_per_plane * 2);
    assert(255 >= 6 + sp->samples_per_pixel_per_plane * 2);
    sp->out_buffer[0] = 255;
    sp->out_buffer[1] = JPEG_MARKER_SOS;
    /* Ls */
    sp->out_buffer[2] = 0;
    sp->out_buffer[3] = (uint8_t)(6 + sp->samples_per_pixel_per_plane * 2);
    /* Ns */
    sp->out_buffer[4] = sp->samples_per_pixel_per_plane;
    for (m = 0; m < sp->samples_per_pixel_per_plane; m++)
    {
        /* Cs */
        sp->out_buffer[5 + m * 2] = sp->sos_cs[sp->plane_sample_offset + m];
        /* Td and Ta */
        sp->out_buffer[5 + m * 2 + 1] =
            sp->sos_tda[sp->plane_sample_offset + m];
    }
    /* Ss */
    sp->out_buffer[5 + sp->samples_per_pixel_per_plane * 2] = 0;
    /* Se */
    sp->out_buffer[5 + sp->samples_per_pixel_per_plane * 2 + 1] = 63;
    /* Ah and Al */
    sp->out_buffer[5 + sp->samples_per_pixel_per_plane * 2 + 2] = 0;
    *len = 8 + (uint32_t)sp->samples_per_pixel_per_plane * 2;
    *mem = (void *)sp->out_buffer;
    sp->out_state = (OJPEGStateOutState)(sp->out_state + 1);
}

/**
 * @brief Produce a Compressed reconstruction chunk.
 * @ingroup libtiff_private_ojpeg
 *
 * Refills input if needed, exposes the unread window and consumes it
 * logically. At strile boundaries selects restart or EOI output. Can
 * seek/read and fail; output arguments are not assigned on failure. The
 * chunk points into in_buffer and is valid until the next refill. Does not
 * allocate.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 * @param[out] mem Set to the unread part of in_buffer on success.
 * @param[out] len Set to the number of unread bytes on success.
 * @return One on success, zero if refilling the input buffer fails (seek or
 * read failure, or no more input), with no codec diagnostic.
 */
static int OJPEGWriteStreamCompressed(TIFF *tif, void **mem, uint32_t *len)
{
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    if (sp->in_buffer_togo == 0)
    {
        if (OJPEGReadBufferFill(sp) == 0)
            return (0);
        assert(sp->in_buffer_togo > 0);
    }
    *len = sp->in_buffer_togo;
    *mem = (void *)sp->in_buffer_cur;
    sp->in_buffer_togo = 0;
    if (sp->in_buffer_file_togo == 0)
    {
        switch (sp->in_buffer_source)
        {
            case osibsStrile:
                if (sp->in_buffer_next_strile < sp->in_buffer_strile_count)
                    sp->out_state = ososRst;
                else
                    sp->out_state = ososEoi;
                break;
            case osibsEof:
                sp->out_state = ososEoi;
                break;
            case osibsNotSetYet:
            case osibsJpegInterchangeFormat:
            default:
                break;
        }
    }
    return (1);
}

/**
 * @brief Produce a Rst reconstruction chunk.
 * @ingroup libtiff_private_ojpeg
 *
 * Writes the next cyclic RST0..RST7 marker into out_buffer and returns to
 * compressed input. The chunk is valid until the next producer overwrites
 * out_buffer.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 * @param[out] mem Set to sp->out_buffer, which holds the marker.
 * @param[out] len Set to the marker length in bytes.
 */
static void OJPEGWriteStreamRst(TIFF *tif, void **mem, uint32_t *len)
{
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    assert(OJPEG_BUFFER >= 2);
    sp->out_buffer[0] = 255;
    sp->out_buffer[1] = (uint8_t)(JPEG_MARKER_RST0 + sp->restart_index);
    sp->restart_index++;
    if (sp->restart_index == 8)
        sp->restart_index = 0;
    *len = 2;
    *mem = (void *)sp->out_buffer;
    sp->out_state = ososCompressed;
}

/**
 * @brief Produce a Eoi reconstruction chunk.
 * @ingroup libtiff_private_ojpeg
 *
 * Writes EOI into out_buffer without advancing to a further state. The
 * chunk is valid until the next producer overwrites out_buffer.
 *
 * @param[in,out] tif Non-NULL live handle with compatible old-style JPEG
 * state and directory; serialise access.
 * @param[out] mem Set to sp->out_buffer, which holds the marker.
 * @param[out] len Set to the marker length in bytes.
 */
static void OJPEGWriteStreamEoi(TIFF *tif, void **mem, uint32_t *len)
{
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    assert(OJPEG_BUFFER >= 2);
    sp->out_buffer[0] = 255;
    sp->out_buffer[1] = JPEG_MARKER_EOI;
    *len = 2;
    *mem = (void *)sp->out_buffer;
}

#ifndef LIBJPEG_ENCAP_EXTERNAL
static int jpeg_create_decompress_encap(OJPEGState *sp,
                                        tiff_ojpeg_decompress_struct *cinfo)
{
    if (SETJMP(sp->exit_jmpbuf))
        return 0;
    else
    {
        jpeg_create_decompress(cinfo);
        return 1;
    }
}
#endif

#ifndef LIBJPEG_ENCAP_EXTERNAL
static int jpeg_read_header_encap(OJPEGState *sp,
                                  tiff_ojpeg_decompress_struct *cinfo,
                                  uint8_t require_image)
{
    if (SETJMP(sp->exit_jmpbuf))
        return 0;
    else
    {
        jpeg_read_header(cinfo, require_image);
        return 1;
    }
}
#endif

#ifndef LIBJPEG_ENCAP_EXTERNAL
static int jpeg_start_decompress_encap(OJPEGState *sp,
                                       tiff_ojpeg_decompress_struct *cinfo)
{
    if (SETJMP(sp->exit_jmpbuf))
        return 0;
    else
    {
        jpeg_start_decompress(cinfo);
        return 1;
    }
}
#endif

#ifndef LIBJPEG_ENCAP_EXTERNAL
static int jpeg_read_scanlines_encap(OJPEGState *sp,
                                     tiff_ojpeg_decompress_struct *cinfo,
                                     void *scanlines, uint32_t max_lines)
{
    if (SETJMP(sp->exit_jmpbuf))
        return 0;
    else
    {
        jpeg_read_scanlines(cinfo, (JSAMPARRAY)scanlines, max_lines);
        return 1;
    }
}
#endif

#ifndef LIBJPEG_ENCAP_EXTERNAL
static int jpeg_read_raw_data_encap(OJPEGState *sp,
                                    tiff_ojpeg_decompress_struct *cinfo,
                                    void *data, uint32_t max_lines)
{
    if (SETJMP(sp->exit_jmpbuf))
        return 0;
    else
    {
        jpeg_read_raw_data(cinfo, (JSAMPIMAGE)data, max_lines);
        return 1;
    }
}
#endif

#ifndef LIBJPEG_ENCAP_EXTERNAL
static void jpeg_encap_unwind(TIFF *tif)
{
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    LONGJMP(sp->exit_jmpbuf, 1);
}
#endif

/**
 * @brief Route a dependency message through the handle warning handler.
 * @ingroup libtiff_private_ojpeg
 *
 * Requires cinfo->client_data to borrow the live TIFF handle. Formats into
 * temporary stack storage and synchronously emits a warning. No retained
 * string, ownership transfer or status result.
 *
 * @param[in] cinfo libjpeg context whose error manager formats the
 * message and whose client_data is the TIFF handle.
 */
static void
OJPEGLibjpegJpegErrorMgrOutputMessage(tiff_ojpeg_common_struct *cinfo)
{
    char buffer[JMSG_LENGTH_MAX];
    (*cinfo->err->format_message)(cinfo, buffer);
    TIFFWarningExtR(((TIFF *)(cinfo->client_data)), "LibJpeg", "%s", buffer);
}

/**
 * @brief Report a fatal dependency error and unwind the active wrapper.
 * @ingroup libtiff_private_ojpeg
 *
 * Formats a stack message, diagnoses through the borrowed handle and calls
 * jpeg_encap_unwind. Requires an active wrapper frame; no ordinary return
 * or automatic codec cleanup.
 *
 * @param[in] cinfo libjpeg context whose error manager formats the
 * message and whose client_data is the TIFF handle.
 */
static void OJPEGLibjpegJpegErrorMgrErrorExit(tiff_ojpeg_common_struct *cinfo)
{
    char buffer[JMSG_LENGTH_MAX];
    (*cinfo->err->format_message)(cinfo, buffer);
    TIFFErrorExtR(((TIFF *)(cinfo->client_data)), "LibJpeg", "%s", buffer);
    jpeg_encap_unwind((TIFF *)(cinfo->client_data));
}

/**
 * @brief Accept the dependency source initialization.
 * @ingroup libtiff_private_ojpeg
 *
 * No-op callback; cinfo is unused. Does not allocate, perform I/O, free
 * borrowed input or transfer ownership.
 *
 * @param[in] cinfo Unused.
 */
static void
OJPEGLibjpegJpegSourceMgrInitSource(tiff_ojpeg_decompress_struct *cinfo)
{
    (void)cinfo;
}

/**
 * @brief Supply the next reconstructed JPEG chunk to libjpeg.
 * @ingroup libtiff_private_ojpeg
 *
 * Calls OJPEGWriteStream and assigns its borrowed storage to the source
 * manager. On failure diagnoses premature input and unwinds the active
 * wrapper. Can read/seek; dependency must consume the chunk before the next
 * refill/cleanup. Does not allocate a caller-owned result.
 *
 * @param[in,out] cinfo libjpeg context whose client_data is the TIFF
 * handle; its source manager (sp->libjpeg_jpeg_source_mgr) receives the
 * chunk.
 * @return One after assigning input; failure leaves by nonlocal jump.
 */
static boolean
OJPEGLibjpegJpegSourceMgrFillInputBuffer(tiff_ojpeg_decompress_struct *cinfo)
{
    TIFF *tif = (TIFF *)cinfo->client_data;
    OJPEGState *sp = (OJPEGState *)tif->tif_data;
    void *mem = 0;
    uint32_t len = 0U;
    if (OJPEGWriteStream(tif, &mem, &len) == 0)
    {
        TIFFErrorExtR(tif, "LibJpeg", "Premature end of JPEG data");
        jpeg_encap_unwind(tif);
    }
    sp->libjpeg_jpeg_source_mgr.bytes_in_buffer = len;
    sp->libjpeg_jpeg_source_mgr.next_input_byte = (const JOCTET *)mem;
    return (1);
}

/**
 * @brief Reject an unexpected dependency skip request.
 * @ingroup libtiff_private_ojpeg
 *
 * Ignores the requested count/marker, diagnoses an unexpected error and
 * unwinds the active wrapper. Performs no recovery or ownership transfer;
 * requires a live wrapper frame.
 *
 * @param[in] cinfo libjpeg context whose client_data is the TIFF handle.
 * @param[in] num_bytes Unused dependency skip request.
 */
static void
OJPEGLibjpegJpegSourceMgrSkipInputData(tiff_ojpeg_decompress_struct *cinfo,
                                       long num_bytes)
{
    TIFF *tif = (TIFF *)cinfo->client_data;
    (void)num_bytes;
    TIFFErrorExtR(tif, "LibJpeg", "Unexpected error");
    jpeg_encap_unwind(tif);
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4702) /* unreachable code */
#endif
/**
 * @brief Reject an unexpected dependency restart resynchronization request.
 * @ingroup libtiff_private_ojpeg
 *
 * Ignores the requested count/marker, diagnoses an unexpected error and
 * unwinds the active wrapper. Performs no recovery or ownership transfer;
 * requires a live wrapper frame.
 *
 * @param[in] cinfo libjpeg context whose client_data is the TIFF handle.
 * @param[in] desired Unused desired restart marker.
 * @return Zero only as an unreachable fallback; ordinary failure leaves by
 * nonlocal jump.
 */
static boolean
OJPEGLibjpegJpegSourceMgrResyncToRestart(tiff_ojpeg_decompress_struct *cinfo,
                                         int desired)
{
    TIFF *tif = (TIFF *)cinfo->client_data;
    (void)desired;
    TIFFErrorExtR(tif, "LibJpeg", "Unexpected error");
    jpeg_encap_unwind(tif);
    return (0);
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

/**
 * @brief Accept the dependency source termination.
 * @ingroup libtiff_private_ojpeg
 *
 * No-op callback; cinfo is unused. Does not allocate, perform I/O, free
 * borrowed input or transfer ownership.
 *
 * @param[in] cinfo Unused.
 */
static void
OJPEGLibjpegJpegSourceMgrTermSource(tiff_ojpeg_decompress_struct *cinfo)
{
    (void)cinfo;
}

#endif

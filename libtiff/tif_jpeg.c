/*
 * Copyright (c) 1994-1997 Sam Leffler
 * Copyright (c) 1994-1997 Silicon Graphics, Inc.
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
 * @file tif_jpeg.c
 * @brief Bridge libjpeg error frames managers and JPEG-in-TIFF transfers.
 */

#include "tiffiop.h"
#include <stdlib.h>

#ifdef JPEG_SUPPORT

/*
 * TIFF Library
 *
 * JPEG Compression support per TIFF Technical Note #2
 * (*not* per the original TIFF 6.0 spec).
 *
 * This file is simply an interface to the libjpeg library written by
 * the Independent JPEG Group.  You need release 5 or later of the IJG
 * code, which you can find on the Internet at ftp.uu.net:/graphics/jpeg/.
 *
 * Contributed by Tom Lane <tgl@sss.pgh.pa.us>.
 */
#include <setjmp.h>

/* Settings that are independent of libjpeg ABI. Used when reinitializing the */
/* JPEGState from libjpegs 8 bit to libjpeg 12 bits, which have potentially */
/* different ABI */
/**
 * @brief Preserve ABI-independent JPEG options and saved parent hooks.
 * @ingroup libtiff_private_jpeg
 *
 * Codec-owned state must remain live until cleanup. Internal buffers and
 * dependency objects have the individual ownership described below;
 * borrowed parent methods must remain callable.
 */
typedef struct
{
    /** @brief Borrowed parent getter restored during cleanup. */
    TIFFVGetMethod vgetparent;
    /** @brief Borrowed parent setter restored during cleanup. */
    TIFFVSetMethod vsetparent;
    /** @brief Borrowed optional parent printer. */
    TIFFPrintMethod printdir;
    /** @brief Borrowed parent strip-size recommender. */
    TIFFStripMethod defsparent;
    /** @brief Borrowed parent tile-size recommender. */
    TIFFTileMethod deftparent;

    /**
     * @brief Owned Ext JPEG tables or placeholder; replaced by table
     * building/tag assignment and freed on cleanup.
     */
    void *jpegtables;
    /**
     * @brief Table byte extent, temporarily allocation capacity during
     * generation.
     */
    uint32_t jpegtables_length;
    /** @brief Cached dependency compression quality, default 75. */
    int jpegquality;
    /** @brief Cached raw/RGB colour-conversion policy. */
    int jpegcolormode;
    /** @brief Cached quantisation/Huffman table emission mask. */
    int jpegtablesmode;

    /**
     * @brief Set when YCbCrSubsampling is assigned through the tag setter (even
     * if the parent rejects it); suppresses the first-strile sampling probe.
     */
    int ycbcrsampling_fetched;
    /** @brief Progressive scan threshold set at decompression start. */
    int max_allowed_scan_number;
    /** @brief Latch suppressing repeated progressive-mode warnings. */
    int has_warned_about_progressive_mode;
} JPEGOtherSettings;

int TIFFFillStrip(TIFF *tif, uint32_t strip);
int TIFFFillTile(TIFF *tif, uint32_t tile);
/**
 * @brief Replace JPEG state with the twelve-bit ABI and restore saved options.
 * @ingroup libtiff_private_jpeg
 *
 * Reallocates outer Ext storage, zeroes it, installs common methods,
 * shallow-copies saved options/table ownership, then calls encode/decode
 * setup. otherSettings must be a live disjoint copy outside the allocation
 * being reallocated/cleared. Caller must account for previous dependency
 * resources before replacement; this function does not destroy them itself.
 * Realloc failure leaves the old block allocated; later failure can leave
 * replaced state/methods for cleanup. No pixel I/O or caller storage
 * ownership transfer.
 *
 * @param[in,out] tif Non-NULL handle whose old JPEG allocation may be
 * replaced.
 * @param[in] otherSettings Borrowed disjoint ABI-independent snapshot,
 * including live parent hooks/table ownership.
 * @param[in] scheme COMPRESSION_JPEG, asserted in debug builds.
 * @param[in] is_encode Nonzero invokes encoder setup, zero decoder setup.
 * @return Setup status after replacement, or zero on outer realloc failure.
 */
int TIFFReInitJPEG_12(TIFF *tif, const JPEGOtherSettings *otherSettings,
                      int scheme, int is_encode);
/**
 * @brief Probe cached twelve-bit JPEG input for whole-strile requirements.
 * @ingroup libtiff_private_jpeg
 *
 * Twelve-bit ABI counterpart of TIFFJPEGIsFullStripRequired(); same
 * borrowed input, temporary dependency lifetime, diagnostic and
 * ambiguous-zero status contract.
 *
 * @param[in,out] tif Non-NULL live handle with compatible twelve-bit raw
 * input.
 * @return One for multiple scans, zero for single-scan or trapped/header
 * failure.
 */
int TIFFJPEGIsFullStripRequired_12(TIFF *tif);

#include "jerror.h"
#include "jpeglib.h"

/* Do optional compile-time version check */
#if defined(EXPECTED_JPEG_LIB_VERSION) && !defined(LIBJPEG_12_PATH)
#if EXPECTED_JPEG_LIB_VERSION != JPEG_LIB_VERSION
#error EXPECTED_JPEG_LIB_VERSION != JPEG_LIB_VERSION
#endif
#endif

/*
 * Do we want to do special processing suitable for when JSAMPLE is a
 * 16bit value?
 */

/* HAVE_JPEGTURBO_DUAL_MODE_8_12 is defined for libjpeg-turbo >= 3.0 which
 * adds a dual-mode 8/12 bit API in the same library.
 * (note: libjpeg-turbo 2.2 was actually released as 3.0)
 */

#if defined(HAVE_JPEGTURBO_DUAL_MODE_8_12)
/**
 * @brief Enable the dual-precision dependency bridge.
 * @ingroup libtiff_private_jpeg
 *
 * Defined when HAVE_JPEGTURBO_DUAL_MODE_8_12 is set (libjpeg-turbo 3.0
 * or later, which provides the eight-bit and J12-prefixed twelve-bit
 * APIs in one library). The eight-bit translation then hands
 * twelve-bit directories to tif_jpeg_12.c through TIFFReInitJPEG_12().
 */
#define JPEG_DUAL_MODE_8_12
/* Start by undefining BITS_IN_JSAMPLE which is always set to 8 in libjpeg-turbo
 * >= 3.0 Cf
 * https://github.com/libjpeg-turbo/libjpeg-turbo/commit/8b9bc4b9635a2a047fb23ebe70c9acd728d3f99b
 */
#undef BITS_IN_JSAMPLE
/* libjpeg-turbo >= 3.0 adds J12xxxx datatypes for the 12-bit mode. */
#if defined(FROM_TIF_JPEG_12)
/**
 * @brief Select the twelve-bit sample ABI for this translation.
 * @ingroup libtiff_private_jpeg
 *
 * Set in the tif_jpeg_12.c translation (FROM_TIF_JPEG_12) of a
 * dual-mode build, replacing the header's value of 8; this also
 * defines JPEG_LIB_MK1_OR_12BIT.
 */
#define BITS_IN_JSAMPLE 12
/**
 * @brief Select the dependency scalar sample type.
 * @ingroup libtiff_private_jpeg
 *
 * Maps to libjpeg-turbo's J12SAMPLE in the dual-mode twelve-bit
 * translation (J12SAMPLE is a short).
 */
#define TIFF_JSAMPLE J12SAMPLE
/**
 * @brief Select the dependency row-array sample type.
 * @ingroup libtiff_private_jpeg
 *
 * Maps to libjpeg-turbo's J12SAMPARRAY in the dual-mode twelve-bit
 * translation (J12SAMPLE is a short).
 */
#define TIFF_JSAMPARRAY J12SAMPARRAY
/**
 * @brief Select the dependency component-plane sample type.
 * @ingroup libtiff_private_jpeg
 *
 * Maps to libjpeg-turbo's J12SAMPIMAGE in the dual-mode twelve-bit
 * translation (J12SAMPLE is a short).
 */
#define TIFF_JSAMPIMAGE J12SAMPIMAGE
/**
 * @brief Select the dependency row-pointer sample type.
 * @ingroup libtiff_private_jpeg
 *
 * Maps to libjpeg-turbo's J12SAMPROW in the dual-mode twelve-bit
 * translation (J12SAMPLE is a short).
 */
#define TIFF_JSAMPROW J12SAMPROW
#else
/**
 * @brief Select the eight-bit sample ABI for this translation.
 * @ingroup libtiff_private_jpeg
 *
 * Set in the main tif_jpeg.c translation of a dual-mode build after
 * the header's value has been undefined.
 */
#define BITS_IN_JSAMPLE 8
/**
 * @brief Select the dependency scalar sample type.
 * @ingroup libtiff_private_jpeg
 *
 * Maps to the eight-bit JSAMPLE in the main translation of a
 * dual-mode build.
 */
#define TIFF_JSAMPLE JSAMPLE
/**
 * @brief Select the dependency row-array sample type.
 * @ingroup libtiff_private_jpeg
 *
 * Maps to the eight-bit JSAMPARRAY in the main translation of a
 * dual-mode build.
 */
#define TIFF_JSAMPARRAY JSAMPARRAY
/**
 * @brief Select the dependency component-plane sample type.
 * @ingroup libtiff_private_jpeg
 *
 * Maps to the eight-bit JSAMPIMAGE in the main translation of a
 * dual-mode build.
 */
#define TIFF_JSAMPIMAGE JSAMPIMAGE
/**
 * @brief Select the dependency row-pointer sample type.
 * @ingroup libtiff_private_jpeg
 *
 * Maps to the eight-bit JSAMPROW in the main translation of a
 * dual-mode build.
 */
#define TIFF_JSAMPROW JSAMPROW
#endif
#else
/**
 * @brief Select the dependency scalar sample type.
 * @ingroup libtiff_private_jpeg
 *
 * Maps to the library's JSAMPLE; its sample width is fixed by the
 * libjpeg build.
 */
#define TIFF_JSAMPLE JSAMPLE
/**
 * @brief Select the dependency row-array sample type.
 * @ingroup libtiff_private_jpeg
 *
 * Maps to the library's JSAMPARRAY; its sample width is fixed by the
 * libjpeg build.
 */
#define TIFF_JSAMPARRAY JSAMPARRAY
/**
 * @brief Select the dependency component-plane sample type.
 * @ingroup libtiff_private_jpeg
 *
 * Maps to the library's JSAMPIMAGE; its sample width is fixed by the
 * libjpeg build.
 */
#define TIFF_JSAMPIMAGE JSAMPIMAGE
/**
 * @brief Select the dependency row-pointer sample type.
 * @ingroup libtiff_private_jpeg
 *
 * Maps to the library's JSAMPROW; its sample width is fixed by the
 * libjpeg build.
 */
#define TIFF_JSAMPROW JSAMPROW
#endif

#if defined(JPEG_LIB_MK1)
/**
 * @brief Enable wide-sample repacking for the selected dependency ABI.
 * @ingroup libtiff_private_jpeg
 *
 * Defined for the MK1 libjpeg variant (JPEG_LIB_MK1), whose JSAMPLE
 * is wider than eight bits. Selects the decode and encode paths that
 * repack between TIFF packed samples and dependency samples through
 * temporary row buffers.
 */
#define JPEG_LIB_MK1_OR_12BIT 1
#elif BITS_IN_JSAMPLE == 12
/**
 * @brief Enable wide-sample repacking for the selected dependency ABI.
 * @ingroup libtiff_private_jpeg
 *
 * Defined when BITS_IN_JSAMPLE is 12; selects the same repacking
 * decode and encode paths as the MK1 variant.
 */
#define JPEG_LIB_MK1_OR_12BIT 1
#endif

/*
 * We are using width_in_blocks which is supposed to be private to
 * libjpeg. Unfortunately, the libjpeg delivered with Cygwin has
 * renamed this member to width_in_data_units.  Since the header has
 * also renamed a define, use that unique define name in order to
 * detect the problem header and adjust to suit.
 */
#if defined(D_MAX_DATA_UNITS_IN_MCU)
/**
 * @brief Adapt the private component field name for a compatible dependency
 * header.
 * @ingroup libtiff_private_jpeg
 *
 * Defined when D_MAX_DATA_UNITS_IN_MCU identifies a header (such as
 * Cygwin's libjpeg) that renamed jpeg_component_info::width_in_blocks
 * to width_in_data_units, so the code can keep the original name.
 */
#define width_in_blocks width_in_data_units
#endif

/*
 * On some machines it may be worthwhile to use _setjmp or sigsetjmp
 * in place of plain setjmp.  These macros will make it easier.
 */
/**
 * @brief Save the calling environment for a later LONGJMP (plain setjmp).
 * @ingroup libtiff_private_jpeg
 *
 * Expands to setjmp(jbuf), evaluating jbuf once. Used only inside CALLJPEG
 * on JPEGState::exit_jmpbuf. The saved frame is valid only until the
 * function that expanded the macro returns.
 *
 * @param[out] jbuf JMP_BUF lvalue that receives the saved environment.
 * @return 0 when called directly; the nonzero LONGJMP code (always 1 in
 * this file) when control returns here through LONGJMP.
 */
#define SETJMP(jbuf) setjmp(jbuf)
/**
 * @brief Jump back to the frame saved by SETJMP (plain longjmp); never
 * returns.
 * @ingroup libtiff_private_jpeg
 *
 * Expands to longjmp(jbuf, code). Called with sp->exit_jmpbuf and code 1
 * by TIFFjpeg_error_exit() and TIFFjpeg_progress_monitor(), after they
 * have reported the error with TIFFErrorExtR() and called jpeg_abort().
 * Control resumes in the CALLJPEG expansion that filled jbuf, which then
 * yields its fail value. The behaviour is undefined if the function that
 * saved jbuf has already returned.
 *
 * @param[in] jbuf JMP_BUF filled by a still-active SETJMP.
 * @param[in] code Value SETJMP returns after the jump (0 is turned into 1
 * by longjmp).
 */
#define LONGJMP(jbuf, code) longjmp(jbuf, code)
/**
 * @brief Select jump-buffer storage for dependency failures.
 * @ingroup libtiff_private_jpeg
 *
 * Type of JPEGState::exit_jmpbuf; a plain jmp_buf used with SETJMP
 * and LONGJMP.
 */
#define JMP_BUF jmp_buf

/**
 * @brief Name the embedded dependency jpeg destination mgr type.
 * @ingroup libtiff_private_jpeg
 *
 * Layout and callback fields follow the selected libjpeg ABI; owning
 * JPEGState retains the structure.
 */
typedef struct jpeg_destination_mgr tiff_jpeg_destination_mgr;
/**
 * @brief Name the embedded dependency jpeg source mgr type.
 * @ingroup libtiff_private_jpeg
 *
 * Layout and callback fields follow the selected libjpeg ABI; owning
 * JPEGState retains the structure.
 */
typedef struct jpeg_source_mgr tiff_jpeg_source_mgr;
/**
 * @brief Name the embedded dependency jpeg error mgr type.
 * @ingroup libtiff_private_jpeg
 *
 * Layout and callback fields follow the selected libjpeg ABI; owning
 * JPEGState retains the structure.
 */
typedef struct jpeg_error_mgr tiff_jpeg_error_mgr;

/*
 * State block for each open TIFF file using
 * libjpeg to do JPEG compression/decompression.
 *
 * libjpeg's visible state is either a jpeg_compress_struct
 * or jpeg_decompress_struct depending on which way we
 * are going.  comm can be used to refer to the fields
 * which are common to both.
 *
 * NB: cinfo is required to be the first member of JPEGState,
 *     so we can safely cast JPEGState* -> jpeg_xxx_struct*
 *     and vice versa!
 */
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324) /* structure padding due to alignment */
#endif
/**
 * @brief Own the leading libjpeg ABI context managers and TIFF transfer state.
 * @ingroup libtiff_private_jpeg
 *
 * The dependency context union must be first so callbacks can cast it to
 * JPEGState. Outer state and table storage use Ext allocation; dependency
 * pool storage belongs to libjpeg. Keep the handle and all installed
 * callbacks live until cleanup; longjmp frames are strictly synchronous.
 */
typedef struct
{
    /** @brief Overlay the leading compression decompression and common
     * dependency contexts. */
    union
    {
        /** @brief Compression view of the leading dependency context union. */
        struct jpeg_compress_struct c;
        /**
         * @brief Decompression view of the leading dependency context union.
         */
        struct jpeg_decompress_struct d;
        /**
         * @brief Common dependency view; leading layout supports callback
         * casts.
         */
        struct jpeg_common_struct comm;
    } cinfo;
    /**< @brief Leading dependency ABI union. */
    /** @brief Whether dependency creation succeeded and destroy is required. */
    int cinfo_initialized;

    /** @brief Embedded dependency error manager routing to TIFF diagnostics. */
    tiff_jpeg_error_mgr err;
    /**
     * @brief Jump buffer valid only during a synchronous wrapper invocation.
     */
    JMP_BUF exit_jmpbuf;

    /** @brief Embedded dependency scan-progress monitor. */
    struct jpeg_progress_mgr progress;

    /** @brief Embedded output manager borrowing raw or table storage. */
    tiff_jpeg_destination_mgr dest;
    /**
     * @brief Embedded input manager borrowing raw/table or static EOI bytes.
     */
    tiff_jpeg_source_mgr src;

    /** @brief Borrowed handle backlink, live for all callbacks. */
    TIFF *tif;
    /** @brief Photometric copied at codec setup. */
    uint16_t photometric;
    /** @brief Cached positive horizontal luminance sampling. */
    uint16_t h_sampling;
    /** @brief Cached positive vertical luminance sampling. */
    uint16_t v_sampling;
    /** @brief Decoded line or raw clump-row byte extent for transfer. */
    tmsize_t bytesperline;
    /** @brief Current tile/truncated-strip pixel width. */
    uint32_t strile_width;
    /** @brief Current tile/truncated-strip row count. */
    uint32_t strile_height;

    /**
     * @brief Borrowed dependency image-pool component-row arrays, invalidated
     * by image-pool release.
     */
    TIFF_JSAMPARRAY ds_buffer[MAX_COMPONENTS];
    /** @brief Count of accumulated downsampled row groups. */
    int scancount;
    /** @brief Samples per raw sampled clump. */
    int samplesperclump;

    /**
     * @brief Owned settings/table pointers and borrowed parent hooks retained
     * across ABI reinitialisation.
     */
    JPEGOtherSettings otherSettings;

    /** @brief Latched failure preventing unsafe raw-encoder retry. */
    int encode_raw_error;
} JPEGState;
#ifdef _MSC_VER
#pragma warning(pop)
#endif

/**
 * @brief Borrow the handle-owned JPEG state.
 * @ingroup libtiff_private_jpeg
 *
 * Casts tif->tif_data without checking it or the codec; valid only
 * while the JPEG codec state is installed.
 *
 * @param[in] tif Handle whose tif_data holds the JPEGState; evaluated once.
 */
#define JState(tif) ((JPEGState *)(tif)->tif_data)

static int JPEGDecode(TIFF *tif, uint8_t *buf, tmsize_t cc, uint16_t s);
static int JPEGDecodeRaw(TIFF *tif, uint8_t *buf, tmsize_t cc, uint16_t s);
static int JPEGEncode(TIFF *tif, uint8_t *buf, tmsize_t cc, uint16_t s);
static int JPEGEncodeRaw(TIFF *tif, uint8_t *buf, tmsize_t cc, uint16_t s);
static int JPEGInitializeLibJPEG(TIFF *tif, int decompress);
static int DecodeRowError(TIFF *tif, uint8_t *buf, tmsize_t cc, uint16_t s);

/**
 * @brief Reserve the codec field bit for JPEGTables.
 * @ingroup libtiff_private_jpeg
 *
 * First codec-private field bit (FIELD_CODEC + 0). Set when
 * JPEGTables holds tables and tested before they are written or
 * loaded.
 */
#define FIELD_JPEGTABLES (FIELD_CODEC + 0)

/**
 * @brief Define JPEG table and quality/colour/table-mode tag metadata.
 * @ingroup libtiff_private_jpeg
 *
 * Immutable library-lifetime metadata merged into a handle; no caller
 * ownership transfer.
 */
static const TIFFField jpegFields[] = {
    {TIFFTAG_JPEGTABLES, -3, -3, TIFF_UNDEFINED, 0, TIFF_SETGET_C32_UINT8,
     FIELD_JPEGTABLES, FALSE, TRUE, "JPEGTables", NULL},
    {TIFFTAG_JPEGQUALITY, 0, 0, TIFF_ANY, 0, TIFF_SETGET_INT, FIELD_PSEUDO,
     TRUE, FALSE, "", NULL},
    {TIFFTAG_JPEGCOLORMODE, 0, 0, TIFF_ANY, 0, TIFF_SETGET_INT, FIELD_PSEUDO,
     FALSE, FALSE, "", NULL},
    {TIFFTAG_JPEGTABLESMODE, 0, 0, TIFF_ANY, 0, TIFF_SETGET_INT, FIELD_PSEUDO,
     FALSE, FALSE, "", NULL}};

/*
 * libjpeg interface layer.
 *
 * We use setjmp/longjmp to return control to libtiff
 * when a fatal error is encountered within the JPEG
 * library.  We also direct libjpeg error and warning
 * messages through the appropriate libtiff handlers.
 */

/*
 * Error handling routines (these replace corresponding
 * IJG routines from jerror.c).  These are used for both
 * compression and decompression.
 */
/**
 * @brief Route a fatal JPEG error abort the operation and jump to its wrapper.
 * @ingroup libtiff_private_jpeg
 *
 * Requires cinfo first within live JPEGState and an active CALLJPEG setjmp
 * frame. Formats into stack text, dispatches handle error, calls jpeg_abort
 * and longjmps. Does not normally return; dependency image storage/views
 * can be invalidated. Application diagnostic callbacks must not
 * close/reconfigure the active handle. No caller ownership transfer.
 *
 * @param[in,out] cinfo Borrowed dependency context at the beginning of a
 * live JPEGState; callbacks are synchronous under its wrapper frame.
 */
static void TIFFjpeg_error_exit(j_common_ptr cinfo)
{
    JPEGState *sp = (JPEGState *)cinfo; /* NB: cinfo assumed first */
    char buffer[JMSG_LENGTH_MAX];

    (*cinfo->err->format_message)(cinfo, buffer);
    TIFFErrorExtR(sp->tif, "JPEGLib", "%s",
                  buffer);       /* display the error message */
    jpeg_abort(cinfo);           /* clean up libjpeg state */
    LONGJMP(sp->exit_jmpbuf, 1); /* return to libtiff caller */
}

/*
 * This routine is invoked only for warning messages,
 * since error_exit does its own thing and trace_level
 * is never set > 0.
 */
/**
 * @brief Route a dependency warning through the TIFF handle.
 * @ingroup libtiff_private_jpeg
 *
 * Requires cinfo first in live JPEGState. Formats borrowed error state into
 * stack text and invokes handle warning callbacks synchronously. No output
 * allocation, payload I/O, pointer retention or ownership transfer.
 *
 * @param[in] cinfo Borrowed dependency context at the beginning of a live
 * JPEGState; only read to format the message.
 */
static void TIFFjpeg_output_message(j_common_ptr cinfo)
{
    char buffer[JMSG_LENGTH_MAX];

    (*cinfo->err->format_message)(cinfo, buffer);
    TIFFWarningExtR(((JPEGState *)cinfo)->tif, "JPEGLib", "%s", buffer);
}

/* Avoid the risk of denial-of-service on crafted JPEGs with an insane */
/* number of scans. */
/* See
 * http://www.libjpeg-turbo.org/pmwiki/uploads/About/TwoIssueswiththeJPEGStandard.pdf
 */
/**
 * @brief Abort JPEG decompression when its scan count reaches the configured
 * limit.
 * @ingroup libtiff_private_jpeg
 *
 * Reads dependency scan progress and cached limit; on threshold dispatches
 * an error, aborts dependency image state and longjmps to an active
 * wrapper. Otherwise returns without mutation. Requires an active wrapper
 * frame and live state; no caller ownership transfer.
 *
 * @param[in,out] cinfo Borrowed dependency context at the beginning of a
 * live JPEGState; callbacks are synchronous under its wrapper frame.
 */
static void TIFFjpeg_progress_monitor(j_common_ptr cinfo)
{
    JPEGState *sp = (JPEGState *)cinfo; /* NB: cinfo assumed first */
    if (cinfo->is_decompressor)
    {
        const int scan_no = ((j_decompress_ptr)cinfo)->input_scan_number;
        if (scan_no >= sp->otherSettings.max_allowed_scan_number)
        {
            TIFFErrorExtR(
                ((JPEGState *)cinfo)->tif, "TIFFjpeg_progress_monitor",
                "Scan number %d exceeds maximum scans (%d). This limit "
                "can be raised through the "
                "LIBTIFF_JPEG_MAX_ALLOWED_SCAN_NUMBER "
                "environment variable.",
                scan_no, sp->otherSettings.max_allowed_scan_number);

            jpeg_abort(cinfo);           /* clean up libjpeg state */
            LONGJMP(sp->exit_jmpbuf, 1); /* return to libtiff caller */
        }
    }
}

/*
 * Interface routines.  This layer of routines exists
 * primarily to limit side-effects from using setjmp.
 * Also, normal/error returns are converted into return
 * values per libtiff practice.
 */
/**
 * @brief Evaluate a libjpeg call, yielding a failure value if libjpeg
 * raises a fatal error.
 * @ingroup libtiff_private_jpeg
 *
 * Saves the environment in (sp)->exit_jmpbuf with SETJMP, then evaluates
 * op and yields its value. If libjpeg raises a fatal error during op,
 * TIFFjpeg_error_exit() or TIFFjpeg_progress_monitor() reports it, calls
 * jpeg_abort() and LONGJMPs back here; the expression then evaluates and
 * yields fail instead. sp is evaluated once by the macro itself (op
 * usually refers to it again); op is evaluated at most once, and fail only
 * after a jump. Non-volatile locals of the enclosing function that op
 * modifies are indeterminate after a jump. The frame is valid only while
 * the enclosing function runs, so each TIFFjpeg_* wrapper returns the
 * expansion directly. setjmp is used as the condition of ?:, which is not
 * one of the contexts ISO C lists for setjmp; this relies on usual
 * compiler behaviour.
 *
 * @param[in,out] sp JPEGState pointer; its exit_jmpbuf is overwritten.
 * @param[in] fail Value yielded after a fatal libjpeg error.
 * @param[in] op libjpeg call expression evaluated under the saved frame.
 * @return The value of op, or fail after a fatal libjpeg error.
 */
#define CALLJPEG(sp, fail, op) (SETJMP((sp)->exit_jmpbuf) ? (fail) : (op))
/**
 * @brief Evaluate a void libjpeg call under CALLJPEG, yielding 1 on
 * completion or 0 after a fatal libjpeg error.
 * @ingroup libtiff_private_jpeg
 *
 * Expands to CALLJPEG(sp, 0, ((op), 1)); the jump, evaluation-count and
 * indeterminate-local rules of CALLJPEG apply. Any value of op is
 * discarded.
 *
 * @param[in,out] sp JPEGState pointer; its exit_jmpbuf is overwritten.
 * @param[in] op libjpeg call expression evaluated under the saved frame.
 * @return 1 if op completed, 0 if libjpeg raised a fatal error.
 */
#define CALLVJPEG(sp, op) CALLJPEG(sp, 0, ((op), 1))

/**
 * @brief Create a libjpeg compressor and install TIFF error routing.
 * @ingroup libtiff_private_jpeg
 *
 * Runs the libjpeg call under CALLJPEG()/CALLVJPEG(), which set a jump point on
 * sp->exit_jmpbuf. A fatal libjpeg error is reported by TIFFjpeg_error_exit(),
 * which aborts the libjpeg object and longjmps back here; work done before the
 * error (allocations, buffer and manager state, I/O) is not rolled back.
 * Requires an uncreated/destroyed dependency object; installs error manager and
 * clears client_data before creation.
 *
 * @param[in,out] sp Borrowed live JPEGState with cinfo at offset zero,
 * valid dependency mode and saved handle context.
 * @return One if the dependency call returns without a fatal longjmp, zero
 * on trapped failure; any Boolean return from the dependency operation is
 * discarded by this void-style wrapper.
 */
static int TIFFjpeg_create_compress(JPEGState *sp)
{
    /* initialize JPEG error handling */
    sp->cinfo.c.err = jpeg_std_error(&sp->err);
    sp->err.error_exit = TIFFjpeg_error_exit;
    sp->err.output_message = TIFFjpeg_output_message;

    /* set client_data to avoid UMR warning from tools like Purify */
    sp->cinfo.c.client_data = NULL;

    return CALLVJPEG(sp, jpeg_create_compress(&sp->cinfo.c));
}

/**
 * @brief Create a libjpeg decompressor and install TIFF error routing.
 * @ingroup libtiff_private_jpeg
 *
 * Runs the libjpeg call under CALLJPEG()/CALLVJPEG(), which set a jump point on
 * sp->exit_jmpbuf. A fatal libjpeg error is reported by TIFFjpeg_error_exit(),
 * which aborts the libjpeg object and longjmps back here; work done before the
 * error (allocations, buffer and manager state, I/O) is not rolled back.
 * Requires an uncreated/destroyed dependency object; installs error manager and
 * clears client_data before creation.
 *
 * @param[in,out] sp Borrowed live JPEGState with cinfo at offset zero,
 * valid dependency mode and saved handle context.
 * @return One if the dependency call returns without a fatal longjmp, zero
 * on trapped failure; any Boolean return from the dependency operation is
 * discarded by this void-style wrapper.
 */
static int TIFFjpeg_create_decompress(JPEGState *sp)
{
    /* initialize JPEG error handling */
    sp->cinfo.d.err = jpeg_std_error(&sp->err);
    sp->err.error_exit = TIFFjpeg_error_exit;
    sp->err.output_message = TIFFjpeg_output_message;

    /* set client_data to avoid UMR warning from tools like Purify */
    sp->cinfo.d.client_data = NULL;

    return CALLVJPEG(sp, jpeg_create_decompress(&sp->cinfo.d));
}

/**
 * @brief Apply dependency default compression parameters.
 * @ingroup libtiff_private_jpeg
 *
 * Runs the libjpeg call under CALLJPEG()/CALLVJPEG(), which set a jump point on
 * sp->exit_jmpbuf. A fatal libjpeg error is reported by TIFFjpeg_error_exit(),
 * which aborts the libjpeg object and longjmps back here; work done before the
 * error (allocations, buffer and manager state, I/O) is not rolled back.
 *
 * @param[in,out] sp Borrowed live JPEGState with cinfo at offset zero,
 * valid dependency mode and saved handle context.
 * @return One if the dependency call returns without a fatal longjmp, zero
 * on trapped failure; any Boolean return from the dependency operation is
 * discarded by this void-style wrapper.
 */
static int TIFFjpeg_set_defaults(JPEGState *sp)
{
    return CALLVJPEG(sp, jpeg_set_defaults(&sp->cinfo.c));
}

/**
 * @brief Select the dependency compression colour space.
 * @ingroup libtiff_private_jpeg
 *
 * Runs the libjpeg call under CALLJPEG()/CALLVJPEG(), which set a jump point on
 * sp->exit_jmpbuf. A fatal libjpeg error is reported by TIFFjpeg_error_exit(),
 * which aborts the libjpeg object and longjmps back here; work done before the
 * error (allocations, buffer and manager state, I/O) is not rolled back.
 *
 * @param[in,out] sp Borrowed live JPEGState with cinfo at offset zero,
 * valid dependency mode and saved handle context.
 * @param[in] colorspace Dependency colour-space value for compression.
 * @return One if the dependency call returns without a fatal longjmp, zero
 * on trapped failure; any Boolean return from the dependency operation is
 * discarded by this void-style wrapper.
 */
static int TIFFjpeg_set_colorspace(JPEGState *sp, J_COLOR_SPACE colorspace)
{
    return CALLVJPEG(sp, jpeg_set_colorspace(&sp->cinfo.c, colorspace));
}

/**
 * @brief Set dependency quantisation quality and baseline policy.
 * @ingroup libtiff_private_jpeg
 *
 * Runs the libjpeg call under CALLJPEG()/CALLVJPEG(), which set a jump point on
 * sp->exit_jmpbuf. A fatal libjpeg error is reported by TIFFjpeg_error_exit(),
 * which aborts the libjpeg object and longjmps back here; work done before the
 * error (allocations, buffer and manager state, I/O) is not rolled back.
 *
 * @param[in,out] sp Borrowed live JPEGState with cinfo at offset zero,
 * valid dependency mode and saved handle context.
 * @param[in] quality Dependency quality setting; dependency policy
 * determines clipping/validation.
 * @param[in] force_baseline Dependency Boolean restricting generated
 * quantisation for baseline compatibility.
 * @return One if the dependency call returns without a fatal longjmp, zero
 * on trapped failure; any Boolean return from the dependency operation is
 * discarded by this void-style wrapper.
 */
static int TIFFjpeg_set_quality(JPEGState *sp, int quality,
                                boolean force_baseline)
{
    return CALLVJPEG(sp,
                     jpeg_set_quality(&sp->cinfo.c, quality, force_baseline));
}

/**
 * @brief Set dependency table-emission suppression.
 * @ingroup libtiff_private_jpeg
 *
 * Runs the libjpeg call under CALLJPEG()/CALLVJPEG(), which set a jump point on
 * sp->exit_jmpbuf. A fatal libjpeg error is reported by TIFFjpeg_error_exit(),
 * which aborts the libjpeg object and longjmps back here; work done before the
 * error (allocations, buffer and manager state, I/O) is not rolled back.
 *
 * @param[in,out] sp Borrowed live JPEGState with cinfo at offset zero,
 * valid dependency mode and saved handle context.
 * @param[in] suppress Dependency Boolean table-suppression policy.
 * @return One if the dependency call returns without a fatal longjmp, zero
 * on trapped failure; any Boolean return from the dependency operation is
 * discarded by this void-style wrapper.
 */
static int TIFFjpeg_suppress_tables(JPEGState *sp, boolean suppress)
{
    return CALLVJPEG(sp, jpeg_suppress_tables(&sp->cinfo.c, suppress));
}

/**
 * @brief Start the dependency compressor for a strile.
 * @ingroup libtiff_private_jpeg
 *
 * Runs the libjpeg call under CALLJPEG()/CALLVJPEG(), which set a jump point on
 * sp->exit_jmpbuf. A fatal libjpeg error is reported by TIFFjpeg_error_exit(),
 * which aborts the libjpeg object and longjmps back here; work done before the
 * error (allocations, buffer and manager state, I/O) is not rolled back.
 *
 * @param[in,out] sp Borrowed live JPEGState with cinfo at offset zero,
 * valid dependency mode and saved handle context.
 * @param[in] write_all_tables Dependency Boolean controlling complete table
 * emission.
 * @return One if the dependency call returns without a fatal longjmp, zero
 * on trapped failure; any Boolean return from the dependency operation is
 * discarded by this void-style wrapper.
 */
static int TIFFjpeg_start_compress(JPEGState *sp, boolean write_all_tables)
{
    return CALLVJPEG(sp, jpeg_start_compress(&sp->cinfo.c, write_all_tables));
}

/**
 * @brief Encode dependency scanline rows.
 * @ingroup libtiff_private_jpeg
 *
 * Runs the libjpeg call under CALLJPEG()/CALLVJPEG(), which set a jump point on
 * sp->exit_jmpbuf. A fatal libjpeg error is reported by TIFFjpeg_error_exit(),
 * which aborts the libjpeg object and longjmps back here; work done before the
 * error (allocations, buffer and manager state, I/O) is not rolled back. Uses
 * the twelve-bit dependency entry point in the enabled dual-mode twelve-bit
 * translation. Buffers must match that compile-time sample ABI and requested
 * row capacities.
 *
 * @param[in,out] sp Borrowed live JPEGState with cinfo at offset zero,
 * valid dependency mode and saved handle context.
 * @param[in] scanlines Borrowed row-pointer array whose num_lines rows of
 * compile-time-ABI samples are read and encoded.
 * @param[in] num_lines Nonnegative row count fitting JDIMENSION and the
 * supplied buffers.
 * @return Nonnegative row count returned by the dependency (possibly zero),
 * or minus one on trapped fatal error.
 */
static int TIFFjpeg_write_scanlines(JPEGState *sp, TIFF_JSAMPARRAY scanlines,
                                    int num_lines)
{
#if defined(HAVE_JPEGTURBO_DUAL_MODE_8_12) && BITS_IN_JSAMPLE == 12
    return CALLJPEG(sp, -1,
                    (int)jpeg12_write_scanlines(&sp->cinfo.c, scanlines,
                                                (JDIMENSION)num_lines));
#else
    return CALLJPEG(sp, -1,
                    (int)jpeg_write_scanlines(&sp->cinfo.c, scanlines,
                                              (JDIMENSION)num_lines));
#endif
}

/**
 * @brief Encode dependency downsampled component rows.
 * @ingroup libtiff_private_jpeg
 *
 * Runs the libjpeg call under CALLJPEG()/CALLVJPEG(), which set a jump point on
 * sp->exit_jmpbuf. A fatal libjpeg error is reported by TIFFjpeg_error_exit(),
 * which aborts the libjpeg object and longjmps back here; work done before the
 * error (allocations, buffer and manager state, I/O) is not rolled back. Uses
 * the twelve-bit dependency entry point in the enabled dual-mode twelve-bit
 * translation. Buffers must match that compile-time sample ABI and requested
 * row capacities.
 *
 * @param[in,out] sp Borrowed live JPEGState with cinfo at offset zero,
 * valid dependency mode and saved handle context.
 * @param[in] data Borrowed component sample planes, read by the encoder;
 * each must hold the dependency-required padded rows.
 * @param[in] num_lines Nonnegative row count fitting JDIMENSION and the
 * supplied buffers.
 * @return Nonnegative row count returned by the dependency (possibly zero),
 * or minus one on trapped fatal error.
 */
static int TIFFjpeg_write_raw_data(JPEGState *sp, TIFF_JSAMPIMAGE data,
                                   int num_lines)
{
#if defined(HAVE_JPEGTURBO_DUAL_MODE_8_12) && BITS_IN_JSAMPLE == 12
    return CALLJPEG(
        sp, -1,
        (int)jpeg12_write_raw_data(&sp->cinfo.c, data, (JDIMENSION)num_lines));
#else
    return CALLJPEG(
        sp, -1,
        (int)jpeg_write_raw_data(&sp->cinfo.c, data, (JDIMENSION)num_lines));
#endif
}

/**
 * @brief Finish dependency compression through the attached destination.
 * @ingroup libtiff_private_jpeg
 *
 * Runs the libjpeg call under CALLJPEG()/CALLVJPEG(), which set a jump point on
 * sp->exit_jmpbuf. A fatal libjpeg error is reported by TIFFjpeg_error_exit(),
 * which aborts the libjpeg object and longjmps back here; work done before the
 * error (allocations, buffer and manager state, I/O) is not rolled back.
 *
 * @param[in,out] sp Borrowed live JPEGState with cinfo at offset zero,
 * valid dependency mode and saved handle context.
 * @return One if the dependency call returns without a fatal longjmp, zero
 * on trapped failure; any Boolean return from the dependency operation is
 * discarded by this void-style wrapper.
 */
static int TIFFjpeg_finish_compress(JPEGState *sp)
{
    return CALLVJPEG(sp, jpeg_finish_compress(&sp->cinfo.c));
}

/**
 * @brief Emit a tables-only JPEG stream through the destination.
 * @ingroup libtiff_private_jpeg
 *
 * Runs the libjpeg call under CALLJPEG()/CALLVJPEG(), which set a jump point on
 * sp->exit_jmpbuf. A fatal libjpeg error is reported by TIFFjpeg_error_exit(),
 * which aborts the libjpeg object and longjmps back here; work done before the
 * error (allocations, buffer and manager state, I/O) is not rolled back.
 *
 * @param[in,out] sp Borrowed live JPEGState with cinfo at offset zero,
 * valid dependency mode and saved handle context.
 * @return One if the dependency call returns without a fatal longjmp, zero
 * on trapped failure; any Boolean return from the dependency operation is
 * discarded by this void-style wrapper.
 */
static int TIFFjpeg_write_tables(JPEGState *sp)
{
    return CALLVJPEG(sp, jpeg_write_tables(&sp->cinfo.c));
}

/**
 * @brief Parse a dependency JPEG header or tables-only stream.
 * @ingroup libtiff_private_jpeg
 *
 * Runs the libjpeg call under CALLJPEG()/CALLVJPEG(), which set a jump point on
 * sp->exit_jmpbuf. A fatal libjpeg error is reported by TIFFjpeg_error_exit(),
 * which aborts the libjpeg object and longjmps back here; work done before the
 * error (allocations, buffer and manager state, I/O) is not rolled back. Source
 * manager state can advance even on a rejected header.
 *
 * @param[in,out] sp Borrowed live JPEGState with cinfo at offset zero,
 * valid dependency mode and saved handle context.
 * @param[in] require_image Dependency Boolean requiring a complete image
 * header rather than tables only.
 * @return Dependency JPEG_HEADER result (including suspension), or minus
 * one on trapped fatal error.
 */
static int TIFFjpeg_read_header(JPEGState *sp, boolean require_image)
{
    return CALLJPEG(sp, -1, jpeg_read_header(&sp->cinfo.d, require_image));
}

/**
 * @brief Query whether dependency input uses multiple scans.
 * @ingroup libtiff_private_jpeg
 *
 * Runs the libjpeg call under CALLJPEG()/CALLVJPEG(), which set a jump point on
 * sp->exit_jmpbuf. A fatal libjpeg error is reported by TIFFjpeg_error_exit(),
 * which aborts the libjpeg object and longjmps back here; work done before the
 * error (allocations, buffer and manager state, I/O) is not rolled back.
 * Requires a parsed decompression header.
 *
 * @param[in,out] sp Borrowed live JPEGState with cinfo at offset zero,
 * valid dependency mode and saved handle context.
 * @return Dependency Boolean result, or zero on trapped fatal error; zero
 * does not distinguish single-scan from failure.
 */
static int TIFFjpeg_has_multiple_scans(JPEGState *sp)
{
    return CALLJPEG(sp, 0, jpeg_has_multiple_scans(&sp->cinfo.d));
}

/**
 * @brief Start the dependency decompressor and progress monitor.
 * @ingroup libtiff_private_jpeg
 *
 * Runs the libjpeg call under CALLJPEG()/CALLVJPEG(), which set a jump point on
 * sp->exit_jmpbuf. A fatal libjpeg error is reported by TIFFjpeg_error_exit(),
 * which aborts the libjpeg object and longjmps back here; work done before the
 * error (allocations, buffer and manager state, I/O) is not rolled back.
 * Installs a scan-count progress guard defaulting to 100, overridden with atoi
 * of LIBTIFF_JPEG_MAX_ALLOWED_SCAN_NUMBER; the override is not separately
 * validated. TIFFjpeg_progress_monitor() also reports an error and longjmps
 * here once the scan limit is reached.
 *
 * @param[in,out] sp Borrowed live JPEGState with cinfo at offset zero,
 * valid dependency mode and saved handle context.
 * @return One if the dependency call returns without a fatal longjmp, zero
 * on trapped failure; any Boolean return from the dependency operation is
 * discarded by this void-style wrapper.
 */
static int TIFFjpeg_start_decompress(JPEGState *sp)
{
    const char *sz_max_allowed_scan_number;
    /* progress monitor */
    sp->cinfo.d.progress = &sp->progress;
    sp->progress.progress_monitor = TIFFjpeg_progress_monitor;
    sp->otherSettings.max_allowed_scan_number = 100;
    sz_max_allowed_scan_number = getenv("LIBTIFF_JPEG_MAX_ALLOWED_SCAN_NUMBER");
    if (sz_max_allowed_scan_number)
        sp->otherSettings.max_allowed_scan_number =
            atoi(sz_max_allowed_scan_number);

    return CALLVJPEG(sp, jpeg_start_decompress(&sp->cinfo.d));
}

/**
 * @brief Decode dependency scanline rows.
 * @ingroup libtiff_private_jpeg
 *
 * Runs the libjpeg call under CALLJPEG()/CALLVJPEG(), which set a jump point on
 * sp->exit_jmpbuf. A fatal libjpeg error is reported by TIFFjpeg_error_exit(),
 * which aborts the libjpeg object and longjmps back here; work done before the
 * error (allocations, buffer and manager state, I/O) is not rolled back. Uses
 * the twelve-bit dependency entry point in the enabled dual-mode twelve-bit
 * translation. Buffers must match that compile-time sample ABI and requested
 * row capacities.
 *
 * @param[in,out] sp Borrowed live JPEGState with cinfo at offset zero,
 * valid dependency mode and saved handle context.
 * @param[in,out] scanlines Borrowed row-pointer array (read) whose rows
 * receive up to max_lines decoded rows of compile-time-ABI samples.
 * @param[in] max_lines Nonnegative maximum row count fitting JDIMENSION and
 * the supplied buffers.
 * @return Nonnegative row count returned by the dependency (possibly zero),
 * or minus one on trapped fatal error.
 */
static int TIFFjpeg_read_scanlines(JPEGState *sp, TIFF_JSAMPARRAY scanlines,
                                   int max_lines)
{
#if defined(HAVE_JPEGTURBO_DUAL_MODE_8_12) && BITS_IN_JSAMPLE == 12
    return CALLJPEG(sp, -1,
                    (int)jpeg12_read_scanlines(&sp->cinfo.d, scanlines,
                                               (JDIMENSION)max_lines));
#else
    return CALLJPEG(sp, -1,
                    (int)jpeg_read_scanlines(&sp->cinfo.d, scanlines,
                                             (JDIMENSION)max_lines));
#endif
}

/**
 * @brief Decode dependency downsampled component rows.
 * @ingroup libtiff_private_jpeg
 *
 * Runs the libjpeg call under CALLJPEG()/CALLVJPEG(), which set a jump point on
 * sp->exit_jmpbuf. A fatal libjpeg error is reported by TIFFjpeg_error_exit(),
 * which aborts the libjpeg object and longjmps back here; work done before the
 * error (allocations, buffer and manager state, I/O) is not rolled back. Uses
 * the twelve-bit dependency entry point in the enabled dual-mode twelve-bit
 * translation. Buffers must match that compile-time sample ABI and requested
 * row capacities.
 *
 * @param[in,out] sp Borrowed live JPEGState with cinfo at offset zero,
 * valid dependency mode and saved handle context.
 * @param[in,out] data Borrowed component sample planes (pointer arrays
 * read) whose padded rows receive the decoded samples.
 * @param[in] max_lines Nonnegative maximum row count fitting JDIMENSION and
 * the supplied buffers.
 * @return Nonnegative row count returned by the dependency (possibly zero),
 * or minus one on trapped fatal error.
 */
static int TIFFjpeg_read_raw_data(JPEGState *sp, TIFF_JSAMPIMAGE data,
                                  int max_lines)
{
#if defined(HAVE_JPEGTURBO_DUAL_MODE_8_12) && BITS_IN_JSAMPLE == 12
    return CALLJPEG(
        sp, -1,
        (int)jpeg12_read_raw_data(&sp->cinfo.d, data, (JDIMENSION)max_lines));
#else
    return CALLJPEG(
        sp, -1,
        (int)jpeg_read_raw_data(&sp->cinfo.d, data, (JDIMENSION)max_lines));
#endif
}

/**
 * @brief Calculate packed bytes for one horizontally sampled JPEG clump row.
 * @ingroup libtiff_private_jpeg
 *
 * Requires nonzero horizontal/vertical sampling and positive precision.
 * Uses ceil(width/h), (h*v+2) samples per clump, checked sixty-four-bit
 * products/addition and signed-size conversion. Rounds the complete bit row
 * once to bytes. Diagnoses invalid sampling/overflow; conservative ceiling
 * guards can reject silently and final rounding is not a general
 * independent overflow proof. No allocation, payload I/O or ownership
 * transfer.
 *
 * @param[in] tif Handle used only for diagnostics and the checked
 * arithmetic helpers; no state is changed.
 * @param[in] width Pixel width for the complete packed clump row.
 * @param[in] h_sampling Positive horizontal luminance sampling factor.
 * @param[in] v_sampling Positive vertical luminance sampling factor.
 * @param[in] data_precision Positive bits per sample.
 * @param[in] module Borrowed diagnostic label.
 * @return Positive packed row extent in bytes, or zero for
 * empty/rejected/overflowed calculation.
 */
static tmsize_t JPEGRawBytesPerClumpLine(TIFF *tif, uint32_t width,
                                         uint16_t h_sampling,
                                         uint16_t v_sampling,
                                         int data_precision, const char *module)
{
    uint64_t clumps_per_line;
    uint64_t samples_per_clump;
    uint64_t samples_per_clumpline;
    uint64_t bits_per_clumpline;
    uint64_t bytes_per_clumpline;

    if (h_sampling == 0 || v_sampling == 0 || data_precision <= 0)
    {
        TIFFErrorExtR(tif, module, "Invalid JPEG sampling or precision");
        return 0;
    }

    clumps_per_line = TIFFhowmany_64(width, h_sampling);
    samples_per_clump = _TIFFAdd64(
        tif, _TIFFMultiply64(tif, h_sampling, v_sampling, module), 2, module);
    samples_per_clumpline =
        _TIFFMultiply64(tif, clumps_per_line, samples_per_clump, module);
    bits_per_clumpline = _TIFFMultiply64(tif, samples_per_clumpline,
                                         (uint64_t)data_precision, module);
    bytes_per_clumpline = TIFFhowmany8_64(bits_per_clumpline);
    return _TIFFCastUInt64ToSSize(tif, bytes_per_clumpline, module);
}

/*
 * Read/write the packed 12-bit byte layout used by the existing JPEG
 * 12-bit pair-based conversion:
 *
 *   sample0: byte0[7:0] + byte1[7:4]
 *   sample1: byte1[3:0] + byte2[7:0]
 *
 * This is packed sample layout, not a host-endian typed load.  These helpers
 * preserve the previous byte layout and only make odd final samples explicit.
 * FillOrder bit reversal is not applied here.
 */
/**
 * @brief Read one sample from TIFF packed twelve-bit bytes.
 * @ingroup libtiff_private_jpeg
 *
 * Requires nonnegative index and enough live input for its pair layout.
 * Even samples use byte0 plus high nibble of byte1; odd samples use low
 * nibble of byte1 plus byte2. No host-endian typed load, fill-order
 * conversion, allocation, I/O, diagnostics or ownership effects.
 *
 * @param[in] buf Borrowed packed twelve-bit bytes; only read, and must hold
 * the indexed sample pair.
 * @param[in] sample_index Nonnegative sample index with representable
 * paired-byte offset.
 * @return Decoded twelve-bit sample under the selected sample type.
 */
static TIFF_JSAMPLE JPEGReadPacked12Sample(const uint8_t *buf,
                                           tmsize_t sample_index)
{
    const uint8_t *p = buf + (sample_index / 2) * 3;
    if ((sample_index & 1) != 0)
        return (TIFF_JSAMPLE)(((p[1] & 0x0f) << 8) | p[2]);
    return (TIFF_JSAMPLE)((p[0] << 4) | ((p[1] & 0xf0) >> 4));
}

/**
 * @brief Write one sample to TIFF packed twelve-bit bytes.
 * @ingroup libtiff_private_jpeg
 *
 * Requires nonnegative index and enough writable paired-byte storage. Masks
 * to twelve bits. An even write assigns the shared byte and clears its low
 * nibble; the following odd write preserves the high nibble. Write pairs in
 * even-then-odd order when retaining both samples. No allocation, I/O,
 * diagnostics, retained pointer or ownership transfer.
 *
 * @param[in,out] buf Borrowed packed twelve-bit byte storage with capacity
 * for the indexed sample pair.
 * @param[in] sample_index Nonnegative sample index with representable
 * paired-byte offset.
 * @param[in] sample Sample value; low twelve bits are stored.
 */
static void TIFF_ATTRIBUTE((unused))
    JPEGWritePacked12Sample(uint8_t *buf, tmsize_t sample_index,
                            TIFF_JSAMPLE sample)
{
    uint16_t v = (uint16_t)sample & 0x0fff;
    uint8_t *p = buf + (sample_index / 2) * 3;
    if ((sample_index & 1) != 0)
    {
        p[1] = (uint8_t)((p[1] & 0xf0) | ((v >> 8) & 0x0f));
        p[2] = (uint8_t)(v & 0xff);
    }
    else
    {
        p[0] = (uint8_t)(v >> 4);
        p[1] = (uint8_t)((v & 0x0f) << 4);
    }
}

/**
 * @brief Finish dependency decompression after output rows.
 * @ingroup libtiff_private_jpeg
 *
 * Runs the libjpeg call under CALLJPEG()/CALLVJPEG(), which set a jump point on
 * sp->exit_jmpbuf. A fatal libjpeg error is reported by TIFFjpeg_error_exit(),
 * which aborts the libjpeg object and longjmps back here; work done before the
 * error (allocations, buffer and manager state, I/O) is not rolled back.
 *
 * @param[in,out] sp Borrowed live JPEGState with cinfo at offset zero,
 * valid dependency mode and saved handle context.
 * @return Dependency Boolean result, or minus one on trapped fatal error;
 * suspension/false is zero.
 */
static int TIFFjpeg_finish_decompress(JPEGState *sp)
{
    return CALLJPEG(sp, -1, (int)jpeg_finish_decompress(&sp->cinfo.d));
}

/**
 * @brief Abort the current dependency operation and release image-pool storage.
 * @ingroup libtiff_private_jpeg
 *
 * Runs the libjpeg call under CALLJPEG()/CALLVJPEG(), which set a jump point on
 * sp->exit_jmpbuf. A fatal libjpeg error is reported by TIFFjpeg_error_exit(),
 * which aborts the libjpeg object and longjmps back here; work done before the
 * error (allocations, buffer and manager state, I/O) is not rolled back.
 *
 * @param[in,out] sp Borrowed live JPEGState with cinfo at offset zero,
 * valid dependency mode and saved handle context.
 * @return One if the dependency call returns without a fatal longjmp, zero
 * on trapped failure; any Boolean return from the dependency operation is
 * discarded by this void-style wrapper.
 */
static int TIFFjpeg_abort(JPEGState *sp)
{
    return CALLVJPEG(sp, jpeg_abort(&sp->cinfo.comm));
}

/**
 * @brief Destroy dependency resources while retaining outer JPEGState.
 * @ingroup libtiff_private_jpeg
 *
 * Runs the libjpeg call under CALLJPEG()/CALLVJPEG(), which set a jump point on
 * sp->exit_jmpbuf. A fatal libjpeg error is reported by TIFFjpeg_error_exit(),
 * which aborts the libjpeg object and longjmps back here; work done before the
 * error (allocations, buffer and manager state, I/O) is not rolled back.
 *
 * @param[in,out] sp Borrowed live JPEGState with cinfo at offset zero,
 * valid dependency mode and saved handle context.
 * @return One if the dependency call returns without a fatal longjmp, zero
 * on trapped failure; any Boolean return from the dependency operation is
 * discarded by this void-style wrapper.
 */
static int TIFFjpeg_destroy(JPEGState *sp)
{
    return CALLVJPEG(sp, jpeg_destroy(&sp->cinfo.comm));
}

/**
 * @brief Allocate sample-row storage in a dependency memory pool.
 * @ingroup libtiff_private_jpeg
 *
 * Runs the libjpeg call under CALLJPEG()/CALLVJPEG(), which set a jump point on
 * sp->exit_jmpbuf. A fatal libjpeg error is reported by TIFFjpeg_error_exit(),
 * which aborts the libjpeg object and longjmps back here; work done before the
 * error (allocations, buffer and manager state, I/O) is not rolled back.
 * Requires a valid pool and representable row dimensions/capacity under the
 * dependency ABI. Returned rows belong to that pool: image-pool rows expire on
 * abort/finish/destroy, permanent-pool rows on destroy. Do not free individual
 * rows or retain views past pool release.
 *
 * @param[in,out] sp Borrowed live JPEGState with cinfo at offset zero,
 * valid dependency mode and saved handle context.
 * @param[in] pool_id Valid dependency memory pool identifier.
 * @param[in] samplesperrow Samples per allocated row under the dependency
 * allocation ABI; products must fit.
 * @param[in] numrows Number of allocated rows; products must fit.
 * @return Borrowed dependency-owned sample-row array, or NULL on trapped
 * allocation/dependency error.
 */
static JSAMPARRAY TIFFjpeg_alloc_sarray(JPEGState *sp, int pool_id,
                                        JDIMENSION samplesperrow,
                                        JDIMENSION numrows)
{
    return CALLJPEG(sp, (JSAMPARRAY)NULL,
                    (*sp->cinfo.comm.mem->alloc_sarray)(
                        &sp->cinfo.comm, pool_id, samplesperrow, numrows));
}

/*
 * JPEG library destination data manager.
 * These routines direct compressed data from libjpeg into the
 * libtiff output buffer.
 */

/**
 * @brief Attach libjpeg output to the handle raw buffer.
 * @ingroup libtiff_private_jpeg
 *
 * Borrows positive-capacity raw storage and resets the dependency output
 * pointer/count. Does not flush, allocate or transfer ownership; caller
 * must keep raw storage live throughout compression.
 *
 * @param[in,out] cinfo Borrowed dependency context at the beginning of a
 * live JPEGState; callbacks are synchronous under its wrapper frame.
 */
static void std_init_destination(j_compress_ptr cinfo)
{
    JPEGState *sp = (JPEGState *)cinfo;
    TIFF *tif = sp->tif;

    sp->dest.next_output_byte = (JOCTET *)tif->tif_rawdata;
    sp->dest.free_in_buffer = (size_t)tif->tif_rawdatasize;
}

/**
 * @brief Flush produced JPEG payload and reset the dependency output window.
 * @ingroup libtiff_private_jpeg
 *
 * Sets rawcc from the produced extent (with IPP-specific adjustment when
 * compiled), invokes TIFFFlushData1 then reattaches raw storage.
 * I/O/diagnostics and partial file/cache changes can survive failure; no
 * allocation owned by caller or ownership transfer.
 *
 * @param[in,out] cinfo Borrowed dependency context at the beginning of a
 * live JPEGState; callbacks are synchronous under its wrapper frame.
 * @return TRUE after flush/reset, FALSE on flush failure.
 */
static boolean std_empty_output_buffer(j_compress_ptr cinfo)
{
    JPEGState *sp = (JPEGState *)cinfo;
    TIFF *tif = sp->tif;

    /* the entire buffer has been filled */
    tif->tif_rawcc = tif->tif_rawdatasize;

#ifdef IPPJ_HUFF
    /*
     * The Intel IPP performance library does not necessarily fill up
     * the whole output buffer on each pass, so only dump out the parts
     * that have been filled.
     *   http://trac.osgeo.org/gdal/wiki/JpegIPP
     */
    if (sp->dest.free_in_buffer >= 0)
    {
        tif->tif_rawcc = tif->tif_rawdatasize - sp->dest.free_in_buffer;
    }
#endif

    if (!TIFFFlushData1(tif))
        return FALSE;
    sp->dest.next_output_byte = (JOCTET *)tif->tif_rawdata;
    sp->dest.free_in_buffer = (size_t)tif->tif_rawdatasize;

    return (TRUE);
}

/**
 * @brief Commit dependency output cursor and pending bytes to the TIFF cache.
 * @ingroup libtiff_private_jpeg
 *
 * Sets rawcp/rawcc from dependency state without flushing the final buffer.
 * No allocation, diagnostics, direct client I/O or ownership transfer;
 * buffered bytes remain handle-owned.
 *
 * @param[in,out] cinfo Borrowed dependency context at the beginning of a
 * live JPEGState; callbacks are synchronous under its wrapper frame.
 */
static void std_term_destination(j_compress_ptr cinfo)
{
    JPEGState *sp = (JPEGState *)cinfo;
    TIFF *tif = sp->tif;

    tif->tif_rawcp = (uint8_t *)sp->dest.next_output_byte;
    tif->tif_rawcc = tif->tif_rawdatasize - (tmsize_t)sp->dest.free_in_buffer;
    /* NB: libtiff does the final buffer flush */
}

/**
 * @brief Install raw-buffer JPEG output manager callbacks.
 * @ingroup libtiff_private_jpeg
 *
 * Attaches the embedded destination manager to compressor state and assigns
 * init/flush/term functions. Requires live state/handle context and raw
 * buffers when later invoked. No immediate allocation, I/O or ownership
 * transfer.
 *
 * @param[in,out] sp Borrowed live JPEGState with cinfo at offset zero,
 * valid dependency mode and saved handle context.
 * @param[in] tif Unused.
 */
static void TIFFjpeg_data_dest(JPEGState *sp, TIFF *tif)
{
    (void)tif;
    sp->cinfo.c.dest = &sp->dest;
    sp->dest.init_destination = std_init_destination;
    sp->dest.empty_output_buffer = std_empty_output_buffer;
    sp->dest.term_destination = std_term_destination;
}

/*
 * Alternate destination manager for outputting to JPEGTables field.
 */

/**
 * @brief Begin writing JPEG tables into the codec-owned table allocation.
 * @ingroup libtiff_private_jpeg
 *
 * Borrows jpegtables and its current capacity and resets dependency output
 * pointers. Requires valid live positive storage; no allocation, I/O,
 * diagnostics or ownership transfer.
 *
 * @param[in,out] cinfo Borrowed dependency context at the beginning of a
 * live JPEGState; callbacks are synchronous under its wrapper frame.
 */
static void tables_init_destination(j_compress_ptr cinfo)
{
    JPEGState *sp = (JPEGState *)cinfo;

    /* while building, otherSettings.jpegtables_length is allocated buffer size
     */
    sp->dest.next_output_byte = (JOCTET *)sp->otherSettings.jpegtables;
    sp->dest.free_in_buffer = (size_t)sp->otherSettings.jpegtables_length;
}

/**
 * @brief Grow the codec-owned JPEG table allocation by one thousand bytes.
 * @ingroup libtiff_private_jpeg
 *
 * Requires representable capacity growth and active CALLJPEG frame. Ext
 * realloc can invalidate old table/output views; updates pointer/capacity
 * and returns TRUE after growth. Failure raises a fatal dependency
 * out-of-memory error and longjmps through the installed handler. No client
 * I/O or caller ownership transfer.
 *
 * @param[in,out] cinfo Borrowed dependency context at the beginning of a
 * live JPEGState; callbacks are synchronous under its wrapper frame.
 * @return TRUE on growth; allocation failure normally exits through the
 * error handler.
 */
static boolean tables_empty_output_buffer(j_compress_ptr cinfo)
{
    JPEGState *sp = (JPEGState *)cinfo;
    void *newbuf;

    /* the entire buffer has been filled; enlarge it by 1000 bytes */
    newbuf =
        _TIFFreallocExt(sp->tif, (void *)sp->otherSettings.jpegtables,
                        (tmsize_t)sp->otherSettings.jpegtables_length + 1000);
    if (newbuf == NULL)
        ERREXIT1(cinfo, JERR_OUT_OF_MEMORY, 100);
    sp->dest.next_output_byte =
        (JOCTET *)newbuf + sp->otherSettings.jpegtables_length;
    sp->dest.free_in_buffer = (size_t)1000;
    sp->otherSettings.jpegtables = newbuf;
    sp->otherSettings.jpegtables_length += 1000;
    return (TRUE);
}

/**
 * @brief Replace JPEG table capacity with the emitted table-stream length.
 * @ingroup libtiff_private_jpeg
 *
 * Subtracts unused destination bytes from jpegtables_length. Requires
 * coherent capacity/count arithmetic. No allocation, I/O, diagnostics or
 * ownership transfer.
 *
 * @param[in,out] cinfo Borrowed dependency context at the beginning of a
 * live JPEGState; callbacks are synchronous under its wrapper frame.
 */
static void tables_term_destination(j_compress_ptr cinfo)
{
    JPEGState *sp = (JPEGState *)cinfo;

    /* set tables length to number of bytes actually emitted */
    sp->otherSettings.jpegtables_length -= (uint32_t)sp->dest.free_in_buffer;
}

/**
 * @brief Replace table storage and install tables-only output callbacks.
 * @ingroup libtiff_private_jpeg
 *
 * Frees previous table storage, allocates one thousand Ext bytes and
 * installs the embedded destination manager. Failure clears length and
 * diagnoses after old views are invalidated. Codec owns the allocation
 * until replacement/cleanup; no client I/O or caller ownership transfer.
 *
 * @param[in,out] sp Borrowed live JPEGState with cinfo at offset zero,
 * valid dependency mode and saved handle context.
 * @param[in] tif Live handle whose memory accounting is used for the table
 * allocation; must be the handle that owns sp.
 * @return One after allocation/installation, zero on allocation failure.
 */
static int TIFFjpeg_tables_dest(JPEGState *sp, TIFF *tif)
{
    (void)tif;
    /*
     * Allocate a working buffer for building tables.
     * Initial size is 1000 bytes, which is usually adequate.
     */
    if (sp->otherSettings.jpegtables)
        _TIFFfreeExt(tif, sp->otherSettings.jpegtables);
    sp->otherSettings.jpegtables_length = 1000;
    sp->otherSettings.jpegtables = (void *)_TIFFmallocExt(
        tif, (tmsize_t)sp->otherSettings.jpegtables_length);
    if (sp->otherSettings.jpegtables == NULL)
    {
        sp->otherSettings.jpegtables_length = 0;
        TIFFErrorExtR(sp->tif, "TIFFjpeg_tables_dest",
                      "No space for JPEGTables");
        return (0);
    }
    sp->cinfo.c.dest = &sp->dest;
    sp->dest.init_destination = tables_init_destination;
    sp->dest.empty_output_buffer = tables_empty_output_buffer;
    sp->dest.term_destination = tables_term_destination;
    return (1);
}

/*
 * JPEG library source data manager.
 * These routines supply compressed data to libjpeg.
 */

/**
 * @brief Attach libjpeg input to cached raw strile bytes.
 * @ingroup libtiff_private_jpeg
 *
 * Borrows rawdata and nonnegative rawcc; does not read client bytes or
 * advance TIFF raw cursors directly. Keep storage live until dependency
 * consumption completes. No allocation, diagnostics or ownership transfer.
 *
 * @param[in,out] cinfo Borrowed dependency context at the beginning of a
 * live JPEGState; callbacks are synchronous under its wrapper frame.
 */
static void std_init_source(j_decompress_ptr cinfo)
{
    JPEGState *sp = (JPEGState *)cinfo;
    TIFF *tif = sp->tif;

    sp->src.next_input_byte = (const JOCTET *)tif->tif_rawdata;
    sp->src.bytes_in_buffer = (size_t)tif->tif_rawcc;
}

/**
 * @brief Supply a synthetic JPEG end marker when cached input is exhausted.
 * @ingroup libtiff_private_jpeg
 *
 * Normally warns through dependency routing then points the source manager
 * at immutable two-byte EOI storage. Optional IPP behavior retains nonempty
 * input instead. Does not refill client I/O or prove valid/truly complete
 * input; truncated input can therefore recover. No allocation or ownership
 * transfer.
 *
 * @param[in,out] cinfo Borrowed dependency context at the beginning of a
 * live JPEGState; callbacks are synchronous under its wrapper frame.
 * @return TRUE unconditionally.
 */
static boolean std_fill_input_buffer(j_decompress_ptr cinfo)
{
    JPEGState *sp = (JPEGState *)cinfo;
    static const JOCTET dummy_EOI[2] = {0xFF, JPEG_EOI};

#ifdef IPPJ_HUFF
    /*
     * The Intel IPP performance library does not necessarily read the whole
     * input buffer in one pass, so it is possible to get here with data
     * yet to read.
     *
     * We just return without doing anything, until the entire buffer has
     * been read.
     * http://trac.osgeo.org/gdal/wiki/JpegIPP
     */
    if (sp->src.bytes_in_buffer > 0)
    {
        return (TRUE);
    }
#endif

    /*
     * Normally the whole strip/tile is read and so we don't need to do
     * a fill.  In the case of CHUNKY_STRIP_READ_SUPPORT we might not have
     * all the data, but the rawdata is refreshed between scanlines and
     * we push this into the io machinery in JPEGDecode().
     * http://trac.osgeo.org/gdal/ticket/3894
     */

    WARNMS(cinfo, JWRN_JPEG_EOF);
    /* insert a fake EOI marker */
    sp->src.next_input_byte = dummy_EOI;
    sp->src.bytes_in_buffer = 2;
    return (TRUE);
}

/**
 * @brief Skip cached JPEG input or substitute synthetic end-of-image.
 * @ingroup libtiff_private_jpeg
 *
 * Nonpositive counts do nothing. A count within cached bytes advances
 * source cursor; a larger count invokes the fake-EOI fill without seeking
 * the underlying stream or consuming the original requested count. Can
 * warn, no allocation or ownership transfer.
 *
 * @param[in,out] cinfo Borrowed dependency context at the beginning of a
 * live JPEGState; callbacks are synchronous under its wrapper frame.
 * @param[in] num_bytes Requested cached input skip in bytes; nonpositive
 * means no change.
 */
static void std_skip_input_data(j_decompress_ptr cinfo, long num_bytes)
{
    JPEGState *sp = (JPEGState *)cinfo;

    if (num_bytes > 0)
    {
        if ((size_t)num_bytes > sp->src.bytes_in_buffer)
        {
            /* oops, buffer overrun */
            (void)std_fill_input_buffer(cinfo);
        }
        else
        {
            sp->src.next_input_byte += (size_t)num_bytes;
            sp->src.bytes_in_buffer -= (size_t)num_bytes;
        }
    }
}

/**
 * @brief Complete JPEG source management without additional work.
 * @ingroup libtiff_private_jpeg
 *
 * Ignores the context; does not free borrowed input, allocate, do I/O,
 * diagnose or transfer ownership.
 *
 * @param[in] cinfo Unused.
 */
static void std_term_source(j_decompress_ptr cinfo)
{
    /* No work necessary here */
    (void)cinfo;
}

/**
 * @brief Install cached-strile JPEG source manager callbacks.
 * @ingroup libtiff_private_jpeg
 *
 * Attaches embedded source manager, sets init/fill/skip/term and dependency
 * restart resynchronisation methods, and clears input pointer/count for
 * safety. No immediate allocation, I/O, diagnostics or ownership transfer.
 *
 * @param[in,out] sp Borrowed live JPEGState with cinfo at offset zero,
 * valid dependency mode and saved handle context.
 */
static void TIFFjpeg_data_src(JPEGState *sp)
{
    sp->cinfo.d.src = &sp->src;
    sp->src.init_source = std_init_source;
    sp->src.fill_input_buffer = std_fill_input_buffer;
    sp->src.skip_input_data = std_skip_input_data;
    sp->src.resync_to_restart = jpeg_resync_to_restart;
    sp->src.term_source = std_term_source;
    sp->src.bytes_in_buffer = 0; /* for safety */
    sp->src.next_input_byte = NULL;
}

/*
 * Alternate source manager for reading from JPEGTables.
 * We can share all the code except for the init routine.
 */

/**
 * @brief Attach libjpeg input to codec-owned JPEG table bytes.
 * @ingroup libtiff_private_jpeg
 *
 * Borrows jpegtables/length and resets source cursor. Keep table storage
 * live throughout parsing; no allocation, client I/O, diagnostics or
 * ownership transfer.
 *
 * @param[in,out] cinfo Borrowed dependency context at the beginning of a
 * live JPEGState; callbacks are synchronous under its wrapper frame.
 */
static void tables_init_source(j_decompress_ptr cinfo)
{
    JPEGState *sp = (JPEGState *)cinfo;

    sp->src.next_input_byte = (const JOCTET *)sp->otherSettings.jpegtables;
    sp->src.bytes_in_buffer = (size_t)sp->otherSettings.jpegtables_length;
}

/**
 * @brief Install the JPEG source manager for tables-only input.
 * @ingroup libtiff_private_jpeg
 *
 * Installs normal source callbacks then substitutes the table init
 * callback. No immediate allocation, I/O, diagnostics or ownership
 * transfer; table bytes remain codec-owned.
 *
 * @param[in,out] sp Borrowed live JPEGState with cinfo at offset zero,
 * valid dependency mode and saved handle context.
 */
static void TIFFjpeg_tables_src(JPEGState *sp)
{
    TIFFjpeg_data_src(sp);
    sp->src.init_source = tables_init_source;
}

/*
 * Allocate downsampled-data buffers needed for downsampled I/O.
 * We use values computed in jpeg_start_compress or jpeg_start_decompress.
 * We use libjpeg's allocator so that buffers will be released automatically
 * when done with strip/tile.
 * This is also a handy place to compute samplesperclump, bytesperline.
 */
/**
 * @brief Allocate dependency-owned component rows for raw sampled JPEG
 * transfer.
 * @ingroup libtiff_private_jpeg
 *
 * Requires parsed/started dependency component geometry, num_components
 * within MAX_COMPONENTS and representable positive dimensions. Allocates
 * JPOOL_IMAGE rows through trapped dependency allocation, stores borrowed
 * row pointers and sums samples per clump. Partial allocations remain
 * pool-owned on failure and expire at image-pool release. No caller
 * ownership transfer or direct client I/O.
 *
 * @param[in,out] tif Live JPEG handle; its codec state receives the row
 * pointers in ds_buffer and the samples-per-clump total.
 * @param[in] comp_info Borrowed dependency component-info array with
 * num_components valid entries.
 * @param[in] num_components Component count from zero through
 * MAX_COMPONENTS, with valid padded geometry.
 * @return One after all component buffers, zero on allocation/dependency
 * failure.
 */
static int alloc_downsampled_buffers(TIFF *tif, jpeg_component_info *comp_info,
                                     int num_components)
{
    JPEGState *sp = JState(tif);
    int ci;
    jpeg_component_info *compptr;
    TIFF_JSAMPARRAY buf;
    int samples_per_clump = 0;

    for (ci = 0, compptr = comp_info; ci < num_components; ci++, compptr++)
    {
        samples_per_clump += compptr->h_samp_factor * compptr->v_samp_factor;
        buf = (TIFF_JSAMPARRAY)TIFFjpeg_alloc_sarray(
            sp, JPOOL_IMAGE, compptr->width_in_blocks * DCTSIZE,
            (JDIMENSION)(compptr->v_samp_factor * DCTSIZE));
        if (buf == NULL)
            return (0);
        sp->ds_buffer[ci] = buf;
    }
    sp->samplesperclump = samples_per_clump;
    return (1);
}

/*
 * JPEG Decoding.
 */

#ifdef CHECK_JPEG_YCBCR_SUBSAMPLING

/**
 * @brief Recognise the JPEG baseline sequential Huffman frame marker.
 * @ingroup libtiff_private_jpeg
 *
 * Marker code (the byte after 0xFF). Its frame header supplies the
 * sampling factors read by JPEGFixupTagsSubsamplingSec().
 */
#define JPEG_MARKER_SOF0 0xC0
/**
 * @brief Recognise the JPEG extended sequential Huffman frame marker.
 * @ingroup libtiff_private_jpeg
 *
 * Marker code (the byte after 0xFF). Its frame header supplies the
 * sampling factors read by JPEGFixupTagsSubsamplingSec().
 */
#define JPEG_MARKER_SOF1 0xC1
/**
 * @brief Recognise the JPEG progressive Huffman frame marker.
 * @ingroup libtiff_private_jpeg
 *
 * Marker code (the byte after 0xFF). Its frame header supplies the
 * sampling factors read by JPEGFixupTagsSubsamplingSec().
 */
#define JPEG_MARKER_SOF2 0xC2
/**
 * @brief Recognise the JPEG extended sequential arithmetic frame marker.
 * @ingroup libtiff_private_jpeg
 *
 * Marker code (the byte after 0xFF). Its frame header supplies the
 * sampling factors read by JPEGFixupTagsSubsamplingSec().
 */
#define JPEG_MARKER_SOF9 0xC9
/**
 * @brief Recognise the JPEG progressive arithmetic frame marker.
 * @ingroup libtiff_private_jpeg
 *
 * Marker code (the byte after 0xFF). Its frame header supplies the
 * sampling factors read by JPEGFixupTagsSubsamplingSec().
 */
#define JPEG_MARKER_SOF10 0xCA
/**
 * @brief Recognise the JPEG Huffman table marker.
 * @ingroup libtiff_private_jpeg
 *
 * Marker code (the byte after 0xFF). The segment is skipped by
 * JPEGFixupTagsSubsamplingSec() using its length field.
 */
#define JPEG_MARKER_DHT 0xC4
/**
 * @brief Recognise the JPEG start of image marker.
 * @ingroup libtiff_private_jpeg
 *
 * Marker code (the byte after 0xFF). Has no payload and is skipped
 * by JPEGFixupTagsSubsamplingSec().
 */
#define JPEG_MARKER_SOI 0xD8
/**
 * @brief Recognise the JPEG start of scan marker.
 * @ingroup libtiff_private_jpeg
 *
 * Marker code (the byte after 0xFF). The segment is skipped by
 * JPEGFixupTagsSubsamplingSec() using its length field.
 */
#define JPEG_MARKER_SOS 0xDA
/**
 * @brief Recognise the JPEG quantisation table marker.
 * @ingroup libtiff_private_jpeg
 *
 * Marker code (the byte after 0xFF). The segment is skipped by
 * JPEGFixupTagsSubsamplingSec() using its length field.
 */
#define JPEG_MARKER_DQT 0xDB
/**
 * @brief Recognise the JPEG restart interval marker.
 * @ingroup libtiff_private_jpeg
 *
 * Marker code (the byte after 0xFF). The segment is skipped by
 * JPEGFixupTagsSubsamplingSec() using its length field.
 */
#define JPEG_MARKER_DRI 0xDD
/**
 * @brief Recognise the JPEG first application (APP0) marker.
 * @ingroup libtiff_private_jpeg
 *
 * Marker code (the byte after 0xFF). APP0 to APP15 (0xE0 to 0xEF)
 * segments are skipped by JPEGFixupTagsSubsamplingSec() using their
 * length field.
 */
#define JPEG_MARKER_APP0 0xE0
/**
 * @brief Recognise the JPEG comment marker.
 * @ingroup libtiff_private_jpeg
 *
 * Marker code (the byte after 0xFF). The segment is skipped by
 * JPEGFixupTagsSubsamplingSec() using its length field.
 */
#define JPEG_MARKER_COM 0xFE
/**
 * @brief Track a bounded first-strile window for JPEG sampling inspection.
 * @ingroup libtiff_private_jpeg
 *
 * Temporary probe-owned buffer is freed on ordinary probe exit; all cursors
 * borrow it and the handle must remain live. Client file position and
 * corrected directory sampling are not rolled back.
 */
struct JPEGFixupTagsSubsamplingData
{
    /** @brief Borrowed handle used by marker reads and sampling correction. */
    TIFF *tif;
    /** @brief Temporary owned Ext window, freed by the outer probe. */
    void *buffer;
    /** @brief Allocated window bytes, normally 2048. */
    uint32_t buffersize;
    /** @brief Borrowed next-byte cursor within buffer. */
    uint8_t *buffercurrentbyte;
    /** @brief Unread buffered bytes. */
    uint32_t bufferbytesleft;
    /** @brief Next unread window offset in the first strile. */
    uint64_t fileoffset;
    /** @brief Remaining bounded file extent. */
    uint64_t filebytesleft;
    /** @brief Nonzero when client position matches the refill offset. */
    uint8_t filepositioned;
};
static void JPEGFixupTagsSubsampling(TIFF *tif);
static int
JPEGFixupTagsSubsamplingSec(struct JPEGFixupTagsSubsamplingData *data);
static int
JPEGFixupTagsSubsamplingReadByte(struct JPEGFixupTagsSubsamplingData *data,
                                 uint8_t *result);
static int
JPEGFixupTagsSubsamplingReadWord(struct JPEGFixupTagsSubsamplingData *data,
                                 uint16_t *result);
static void
JPEGFixupTagsSubsamplingSkip(struct JPEGFixupTagsSubsamplingData *data,
                             uint16_t skiplength);

#endif

/**
 * @brief Attempt JPEG-stream YCbCr subsampling correction before ordinary
 * access.
 * @ingroup libtiff_private_jpeg
 *
 * For enabled subsampling checks and eligible contiguous three-sample YCbCr
 * input, probes the first strile unless sampling was already
 * supplied/fetched. Helper failures can warn while this routine still
 * succeeds. Can allocate, seek/read client data and update cached sampling;
 * file position is not restored. No caller ownership transfer.
 *
 * @param[in,out] tif Non-NULL live JPEG handle with compatible current
 * directory/state; serialise operations.
 * @return One unconditionally.
 */
static int JPEGFixupTags(TIFF *tif)
{
#ifdef CHECK_JPEG_YCBCR_SUBSAMPLING
    JPEGState *sp = JState(tif);
    if ((tif->tif_dir.td_photometric == PHOTOMETRIC_YCBCR) &&
        (tif->tif_dir.td_planarconfig == PLANARCONFIG_CONTIG) &&
        (tif->tif_dir.td_samplesperpixel == 3) &&
        !sp->otherSettings.ycbcrsampling_fetched)
        JPEGFixupTagsSubsampling(tif);
#endif

    return (1);
}

#ifdef CHECK_JPEG_YCBCR_SUBSAMPLING

/**
 * @brief Probe first-strile markers to recover JPEG sampling factors.
 * @ingroup libtiff_private_jpeg
 *
 * Returns silently when the first offset is zero; otherwise allocates a
 * temporary 2048-byte Ext read window, scans the bounded first strile and
 * frees the window on ordinary exit. Allocation/parse failure warns without
 * separate status. Can seek/read and update directory sampling; does not
 * restore file position or roll back corrections. No retained caller
 * storage.
 *
 * @param[in,out] tif Non-NULL live JPEG handle with compatible current
 * directory/state; serialise operations.
 */
static void JPEGFixupTagsSubsampling(TIFF *tif)
{
    /*
     * Some JPEG-in-TIFF produces do not emit the YCBCRSUBSAMPLING values in
     * the TIFF tags, but still use non-default (2,2) values within the jpeg
     * data stream itself.  In order for TIFF applications to work properly
     * - for instance to get the strip buffer size right - it is imperative
     * that the subsampling be available before we start reading the image
     * data normally.  This function will attempt to analyze the first strip in
     * order to get the sampling values from the jpeg data stream.
     *
     * Note that JPEGPreDeocode() will produce a fairly loud warning when the
     * discovered sampling does not match the default sampling (2,2) or whatever
     * was actually in the tiff tags.
     *
     * See the bug in bugzilla for details:
     *
     * http://bugzilla.remotesensing.org/show_bug.cgi?id=168
     *
     * Frank Warmerdam, July 2002
     * Joris Van Damme, May 2007
     */
    static const char module[] = "JPEGFixupTagsSubsampling";
    struct JPEGFixupTagsSubsamplingData m;
    uint64_t fileoffset = TIFFGetStrileOffset(tif, 0);

    if (fileoffset == 0)
    {
        /* Do not even try to check if the first strip/tile does not
           yet exist, as occurs when GDAL has created a new NULL file
           for instance. */
        return;
    }

    m.tif = tif;
    m.buffersize = 2048;
    m.buffer = _TIFFmallocExt(tif, m.buffersize);
    if (m.buffer == NULL)
    {
        TIFFWarningExtR(tif, module,
                        "Unable to allocate memory for auto-correcting of "
                        "subsampling values; auto-correcting skipped");
        return;
    }
    m.buffercurrentbyte = NULL;
    m.bufferbytesleft = 0;
    m.fileoffset = fileoffset;
    m.filepositioned = 0;
    m.filebytesleft = TIFFGetStrileByteCount(tif, 0);
    if (!JPEGFixupTagsSubsamplingSec(&m))
        TIFFWarningExtR(
            tif, module,
            "Unable to auto-correct subsampling values, likely corrupt JPEG "
            "compressed data in first strip/tile; auto-correcting skipped");
    _TIFFfreeExt(tif, m.buffer);
}

/**
 * @brief Scan marker segments for TIFF-compatible component sampling.
 * @ingroup libtiff_private_jpeg
 *
 * Consumes a bounded buffered client stream until a supported SOF marker.
 * Skips irrelevant length-delimited segments, validates segment layout and
 * compares three-component sampling. Compatible factors can update cached
 * directory subsampling with a warning; unrepresentable factors warn and
 * return one without correction. No result allocation or ownership
 * transfer; file/cursor changes remain on failure.
 *
 * @param[in,out] data Borrowed marker-window state with bounded
 * first-strile input and live temporary buffer/handle.
 * @return One after a handled SOF, including warned unsupported sampling;
 * zero on unhandled marker/truncated or invalid layout.
 */
static int
JPEGFixupTagsSubsamplingSec(struct JPEGFixupTagsSubsamplingData *data)
{
    static const char module[] = "JPEGFixupTagsSubsamplingSec";
    uint8_t m;
    while (1)
    {
        while (1)
        {
            if (!JPEGFixupTagsSubsamplingReadByte(data, &m))
                return (0);
            if (m == 255)
                break;
        }
        while (1)
        {
            if (!JPEGFixupTagsSubsamplingReadByte(data, &m))
                return (0);
            if (m != 255)
                break;
        }
        switch (m)
        {
            case JPEG_MARKER_SOI:
                /* this type of marker has no data and should be skipped */
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
            case JPEG_MARKER_DQT:
            case JPEG_MARKER_SOS:
            case JPEG_MARKER_DHT:
            case JPEG_MARKER_DRI:
                /* this type of marker has data, but it has no use to us and
                 * should be skipped */
                {
                    uint16_t n;
                    if (!JPEGFixupTagsSubsamplingReadWord(data, &n))
                        return (0);
                    if (n < 2)
                        return (0);
                    n = (uint16_t)(n - 2);
                    if (n > 0)
                        JPEGFixupTagsSubsamplingSkip(data, n);
                }
                break;
            case JPEG_MARKER_SOF0:  /* Baseline sequential Huffman */
            case JPEG_MARKER_SOF1:  /* Extended sequential Huffman */
            case JPEG_MARKER_SOF2:  /* Progressive Huffman: normally not allowed
                                       by  TechNote, but that doesn't hurt
                                       supporting it */
            case JPEG_MARKER_SOF9:  /* Extended sequential arithmetic */
            case JPEG_MARKER_SOF10: /* Progressive arithmetic: normally not
                                       allowed by TechNote, but that doesn't
                                       hurt supporting it */
                /* this marker contains the subsampling factors we're scanning
                 * for */
                {
                    uint16_t n;
                    uint16_t o;
                    uint8_t p;
                    uint8_t ph, pv;
                    if (!JPEGFixupTagsSubsamplingReadWord(data, &n))
                        return (0);
                    if (n != 8 + data->tif->tif_dir.td_samplesperpixel * 3)
                        return (0);
                    JPEGFixupTagsSubsamplingSkip(data, 7);
                    if (!JPEGFixupTagsSubsamplingReadByte(data, &p))
                        return (0);
                    ph = (p >> 4);
                    pv = (p & 15);
                    JPEGFixupTagsSubsamplingSkip(data, 1);
                    for (o = 1; o < data->tif->tif_dir.td_samplesperpixel; o++)
                    {
                        JPEGFixupTagsSubsamplingSkip(data, 1);
                        if (!JPEGFixupTagsSubsamplingReadByte(data, &p))
                            return (0);
                        if (p != 0x11)
                        {
                            TIFFWarningExtR(data->tif, module,
                                            "Subsampling values inside JPEG "
                                            "compressed data "
                                            "have no TIFF equivalent, "
                                            "auto-correction of TIFF "
                                            "subsampling values failed");
                            return (1);
                        }
                        JPEGFixupTagsSubsamplingSkip(data, 1);
                    }
                    if (((ph != 1) && (ph != 2) && (ph != 4)) ||
                        ((pv != 1) && (pv != 2) && (pv != 4)))
                    {
                        TIFFWarningExtR(data->tif, module,
                                        "Subsampling values inside JPEG "
                                        "compressed data have no TIFF "
                                        "equivalent, auto-correction of TIFF "
                                        "subsampling values failed");
                        return (1);
                    }
                    if ((ph != data->tif->tif_dir.td_ycbcrsubsampling[0]) ||
                        (pv != data->tif->tif_dir.td_ycbcrsubsampling[1]))
                    {
                        TIFFWarningExtR(
                            data->tif, module,
                            "Auto-corrected former TIFF subsampling values "
                            "[%" PRIu16 ",%" PRIu16
                            "] to match subsampling values inside JPEG "
                            "compressed data [%" PRIu8 ",%" PRIu8 "]",
                            data->tif->tif_dir.td_ycbcrsubsampling[0],
                            data->tif->tif_dir.td_ycbcrsubsampling[1], ph, pv);
                        data->tif->tif_dir.td_ycbcrsubsampling[0] = ph;
                        data->tif->tif_dir.td_ycbcrsubsampling[1] = pv;
                    }
                }
                return (1);
            default:
                return (0);
        }
    }
}

/**
 * @brief Read one byte through the bounded JPEG marker window.
 * @ingroup libtiff_private_jpeg
 *
 * Refills from remaining strile extent, seeking when not positioned, then
 * advances buffer/file cursors. Requires valid positive buffer capacity
 * below 2^31 and representable offsets. Short/failed I/O returns zero
 * without a local diagnostic; client callback effects and partial file
 * position remain. No allocation or ownership transfer.
 *
 * @param[in,out] data Borrowed marker-window state with bounded
 * first-strile input and live temporary buffer/handle.
 * @param[out] result Non-NULL caller-owned byte, assigned only on success.
 * @return One after assigning result, zero on extent/seek/read failure;
 * result unchanged on rejection.
 */
static int
JPEGFixupTagsSubsamplingReadByte(struct JPEGFixupTagsSubsamplingData *data,
                                 uint8_t *result)
{
    if (data->bufferbytesleft == 0)
    {
        uint32_t m;
        if (data->filebytesleft == 0)
            return (0);
        if (!data->filepositioned)
        {
            if (TIFFSeekFile(data->tif, data->fileoffset, SEEK_SET) ==
                (toff_t)-1)
            {
                return 0;
            }
            data->filepositioned = 1;
        }
        m = data->buffersize;
        if ((uint64_t)m > data->filebytesleft)
            m = (uint32_t)data->filebytesleft;
        assert(m < 0x80000000UL);
        if (TIFFReadFile(data->tif, data->buffer, (tmsize_t)m) != (tmsize_t)m)
            return (0);
        data->buffercurrentbyte = (uint8_t *)data->buffer;
        data->bufferbytesleft = m;
        data->fileoffset += m;
        data->filebytesleft -= m;
    }
    *result = *data->buffercurrentbyte;
    data->buffercurrentbyte++;
    data->bufferbytesleft--;
    return (1);
}

/**
 * @brief Read a big-endian sixteen-bit marker value.
 * @ingroup libtiff_private_jpeg
 *
 * Reads two bytes through the marker window and assigns result only after
 * both succeed. First-byte consumption can remain after a second-byte
 * failure. Client I/O/cursor effects follow the byte reader; no allocation
 * or ownership transfer.
 *
 * @param[in,out] data Borrowed marker-window state with bounded
 * first-strile input and live temporary buffer/handle.
 * @param[out] result Non-NULL caller-owned sixteen-bit value, assigned
 * only after both bytes are read.
 * @return One on assignment, zero on byte-reader failure.
 */
static int
JPEGFixupTagsSubsamplingReadWord(struct JPEGFixupTagsSubsamplingData *data,
                                 uint16_t *result)
{
    uint8_t ma;
    uint8_t mb;
    if (!JPEGFixupTagsSubsamplingReadByte(data, &ma))
        return (0);
    if (!JPEGFixupTagsSubsamplingReadByte(data, &mb))
        return (0);
    *result = (uint16_t)((ma << 8) | mb);
    return (1);
}

/**
 * @brief Skip bounded marker bytes without immediately reading them.
 * @ingroup libtiff_private_jpeg
 *
 * Advances buffered cursor when possible; otherwise consumes buffered
 * remainder, advances the file-offset window and marks repositioning
 * required. A skip beyond remaining data exhausts the window silently. No
 * allocation, immediate client I/O, diagnostics, status result or ownership
 * transfer.
 *
 * @param[in,out] data Borrowed marker-window state with bounded
 * first-strile input and live temporary buffer/handle.
 * @param[in] skiplength Marker payload bytes to skip within the bounded
 * window.
 */
static void
JPEGFixupTagsSubsamplingSkip(struct JPEGFixupTagsSubsamplingData *data,
                             uint16_t skiplength)
{
    if ((uint32_t)skiplength <= data->bufferbytesleft)
    {
        data->buffercurrentbyte += skiplength;
        data->bufferbytesleft -= skiplength;
    }
    else
    {
        uint16_t m;
        m = (uint16_t)(skiplength - data->bufferbytesleft);
        if (m <= data->filebytesleft)
        {
            data->bufferbytesleft = 0;
            data->fileoffset += m;
            data->filebytesleft -= m;
            data->filepositioned = 0;
        }
        else
        {
            data->bufferbytesleft = 0;
            data->filebytesleft = 0;
        }
    }
}

#endif

/**
 * @brief Initialise JPEG decompression tables sampling and source managers.
 * @ingroup libtiff_private_jpeg
 *
 * For enabled twelve-bit dispatch can replace outer state through
 * TIFFReInitJPEG_12. Otherwise creates/switches dependency mode, parses
 * JPEGTables as a tables-only stream when present, caches
 * photometric/sampling, installs raw input manager and disables ordinary
 * postdecode swapping. Dependency allocations/diagnostics and changed
 * methods/state can remain after failure; tables/state retain codec
 * ownership. No direct pixel decoding.
 *
 * @param[in,out] tif Non-NULL live JPEG handle with compatible current
 * directory/state; serialise operations.
 * @return One after setup, zero on dependency/table/ABI setup failure.
 */
static int JPEGSetupDecode(TIFF *tif)
{
    JPEGState *sp = JState(tif);
    TIFFDirectory *td = &tif->tif_dir;

#if defined(JPEG_DUAL_MODE_8_12) && !defined(FROM_TIF_JPEG_12)
    if (tif->tif_dir.td_bitspersample == 12)
    {
        /* We pass a pointer to a copy of otherSettings, since */
        /* TIFFReInitJPEG_12() will clear sp */
        JPEGOtherSettings savedOtherSettings = sp->otherSettings;
        return TIFFReInitJPEG_12(tif, &savedOtherSettings, COMPRESSION_JPEG, 0);
    }
#endif

    if (!JPEGInitializeLibJPEG(tif, TRUE))
    {
        TIFFErrorExtR(tif, "JPEGSetupDecode",
                      "Failed to initialize libjpeg decompressor");
        return (0);
    }

    assert(sp != NULL);
    assert(sp->cinfo.comm.is_decompressor);

    /* Read JPEGTables if it is present */
    if (TIFFFieldSet(tif, FIELD_JPEGTABLES))
    {
        TIFFjpeg_tables_src(sp);
        if (TIFFjpeg_read_header(sp, FALSE) != JPEG_HEADER_TABLES_ONLY)
        {
            TIFFErrorExtR(tif, "JPEGSetupDecode", "Bogus JPEGTables field");
            return (0);
        }
    }

    /* Grab parameters that are same for all strips/tiles */
    sp->photometric = td->td_photometric;
    switch (sp->photometric)
    {
        case PHOTOMETRIC_YCBCR:
            sp->h_sampling = td->td_ycbcrsubsampling[0];
            sp->v_sampling = td->td_ycbcrsubsampling[1];
            break;
        default:
            /* TIFF 6.0 forbids subsampling of all other color spaces */
            sp->h_sampling = 1;
            sp->v_sampling = 1;
            break;
    }

    /* Set up for reading normal data */
    TIFFjpeg_data_src(sp);
    tif->tif_postdecode = _TIFFNoPostDecode; /* override byte swapping */
    return (1);
}

/* Returns 1 if the full strip should be read, even when doing scanline per */
/* scanline decoding. This happens when the JPEG stream uses multiple scans. */
/* Currently only called in CHUNKY_STRIP_READ_SUPPORT mode through */
/* scanline interface. */
/* Only reads tif->tif_dir.td_bitspersample, tif->tif_rawdata and */
/* tif->tif_rawcc members. */
/* Can be called independently of the usual setup/predecode/decode states */
int TIFFJPEGIsFullStripRequired(TIFF *tif)
{
    int ret;
    JPEGState state;

#if defined(JPEG_DUAL_MODE_8_12) && !defined(FROM_TIF_JPEG_12)
    if (tif->tif_dir.td_bitspersample == 12)
        return TIFFJPEGIsFullStripRequired_12(tif);
#endif

    memset(&state, 0, sizeof(JPEGState));
    state.tif = tif;

    TIFFjpeg_create_decompress(&state);

    TIFFjpeg_data_src(&state);

    if (TIFFjpeg_read_header(&state, TRUE) != JPEG_HEADER_OK)
    {
        TIFFjpeg_destroy(&state);
        return (0);
    }
    ret = TIFFjpeg_has_multiple_scans(&state);

    TIFFjpeg_destroy(&state);

    return ret;
}

/**
 * @brief Cache JPEG strile geometry and decoded row extent.
 * @ingroup libtiff_private_jpeg
 *
 * Uses tile dimensions or image width/truncated remaining strip height and
 * the layout byte query. Separate chroma planes scale expected width/height
 * by positive sampling factors. Requires coherent current row/geometry and
 * representable ceiling arithmetic. Zero sampling diagnoses after caches
 * may change; does not independently reject every zero byte size. No pixel
 * I/O, result allocation or ownership transfer.
 *
 * @param[in,out] tif Non-NULL live JPEG handle with compatible current
 * directory/state; serialise operations.
 * @param[in] s Sample plane index; with separate planar configuration a
 * nonzero value selects a chroma plane, whose width and height are divided
 * by the cached sampling factors.
 * @return One after geometry selection, zero on rejected separate-plane
 * sampling.
 */
static int JPEGComputeStrileWidthHeightBytesPerLine(TIFF *tif, uint16_t s)
{
    JPEGState *sp = JState(tif);
    TIFFDirectory *td = &tif->tif_dir;

    /*
     * Check image parameters and set decompression parameters.
     */
    if (isTiled(tif))
    {
        sp->strile_width = td->td_tilewidth;
        sp->strile_height = td->td_tilelength;
        sp->bytesperline = TIFFTileRowSize(tif);
    }
    else
    {
        sp->strile_width = td->td_imagewidth;
        sp->strile_height = td->td_imagelength - tif->tif_dir.td_row;
        if (sp->strile_height > td->td_rowsperstrip)
            sp->strile_height = td->td_rowsperstrip;
        sp->bytesperline = TIFFScanlineSize(tif);
    }
    if (td->td_planarconfig == PLANARCONFIG_SEPARATE && s > 0)
    {
        /*
         * For PC 2, scale down the expected strip/tile size
         * to match a downsampled component
         */
        if (sp->h_sampling == 0 || sp->v_sampling == 0)
        {
            TIFFErrorExtR(tif, "JPEGComputeStrileWidthHeightBytesPerLine",
                          "JPEG horizontal or vertical sampling is zero");
            return (0);
        }
        sp->strile_width = TIFFhowmany_32(sp->strile_width, sp->h_sampling);
        sp->strile_height = TIFFhowmany_32(sp->strile_height, sp->v_sampling);
    }

    return 1;
}

/*
 * Set up for decoding a strip or tile.
 */
/*ARGSUSED*/ /**
              * @brief Validate one JPEG strile header and start its chosen
              * decoder.
              * @ingroup libtiff_private_jpeg
              *
              * If the dependency context is not in decompression mode,
              * first invokes tif_setupdecode without checking its result
              * (which may reallocate the codec state for a twelve-bit ABI
              * switch). Aborts prior dependency image state, parses the new
              * header and
              * commits raw header consumption. Validates precision, component
              * count, expected dimensions and sampling; smaller images warn,
              * and an oversized-height last strip is tolerated under the
              * specific truncated-strip recovery case. Progressive input warns
              * once and checks estimated coefficient memory against the
              * dependency limit unless the environment override is present.
              * Selects RGB conversion or raw downsampled transfer; raw mode
              * disables scanline access and allocates image-pool component
              * rows. Setup/validation/longjmp failures can leave changed state,
              * caches and borrowed dependency views invalidated. No caller
              * ownership transfer.
              *
              * @param[in,out] tif Non-NULL live JPEG handle with compatible
              * current directory/state; serialise operations.
              * @param[in] s Sample plane index, passed to
              * JPEGComputeStrileWidthHeightBytesPerLine() to size a separate
              * chroma plane.
              * @return One after decompression start/buffer setup, zero on
              * detected validation/dependency/allocation failure.
              */
static int JPEGPreDecode(TIFF *tif, uint16_t s)
{
    JPEGState *sp = JState(tif);
    TIFFDirectory *td = &tif->tif_dir;
    static const char module[] = "JPEGPreDecode";
    int downsampled_output;
    int ci;

    assert(sp != NULL);

    if (sp->cinfo.comm.is_decompressor == 0)
    {
        tif->tif_setupdecode(tif);
    }

    assert(sp->cinfo.comm.is_decompressor);
    /*
     * Reset decoder state from any previous strip/tile,
     * in case application didn't read the whole strip.
     */
    if (!TIFFjpeg_abort(sp))
        return (0);
    /*
     * Read the header for this strip/tile.
     */

    if (TIFFjpeg_read_header(sp, TRUE) != JPEG_HEADER_OK)
        return (0);

    tif->tif_rawcp = (uint8_t *)sp->src.next_input_byte;
    tif->tif_rawcc = (tmsize_t)sp->src.bytes_in_buffer;

    if (!JPEGComputeStrileWidthHeightBytesPerLine(tif, s))
        return 0;

    if (sp->cinfo.d.image_width < sp->strile_width ||
        sp->cinfo.d.image_height < sp->strile_height)
    {
        TIFFWarningExtR(tif, module,
                        "Improper JPEG strip/tile size, "
                        "expected %" PRIu32 "x%" PRIu32 ", got %ux%u",
                        sp->strile_width, sp->strile_height,
                        sp->cinfo.d.image_width, sp->cinfo.d.image_height);
    }
    if (sp->cinfo.d.image_width == sp->strile_width &&
        sp->cinfo.d.image_height > sp->strile_height &&
        tif->tif_dir.td_row + sp->strile_height == td->td_imagelength &&
        !isTiled(tif))
    {
        /* Some files have a last strip, that should be truncated, */
        /* but their JPEG codestream has still the maximum strip */
        /* height. Warn about this as this is non compliant, but */
        /* we can safely recover from that. */
        TIFFWarningExtR(tif, module,
                        "JPEG strip size exceeds expected dimensions,"
                        " expected %" PRIu32 "x%" PRIu32 ", got %ux%u",
                        sp->strile_width, sp->strile_height,
                        sp->cinfo.d.image_width, sp->cinfo.d.image_height);
    }
    else if (sp->cinfo.d.image_width > sp->strile_width ||
             sp->cinfo.d.image_height > sp->strile_height)
    {
        /*
         * This case could be dangerous, if the strip or tile size has
         * been reported as less than the amount of data jpeg will
         * return, some potential security issues arise. Catch this
         * case and error out.
         */
        TIFFErrorExtR(tif, module,
                      "JPEG strip/tile size exceeds expected dimensions,"
                      " expected %" PRIu32 "x%" PRIu32 ", got %ux%u",
                      sp->strile_width, sp->strile_height,
                      sp->cinfo.d.image_width, sp->cinfo.d.image_height);
        return (0);
    }
    if (sp->cinfo.d.num_components !=
        (td->td_planarconfig == PLANARCONFIG_CONTIG ? td->td_samplesperpixel
                                                    : 1))
    {
        TIFFErrorExtR(tif, module, "Improper JPEG component count");
        return (0);
    }
#ifdef JPEG_LIB_MK1
    if (12 != td->td_bitspersample && 8 != td->td_bitspersample)
    {
        TIFFErrorExtR(tif, module, "Improper JPEG data precision");
        return (0);
    }
    sp->cinfo.d.data_precision = td->td_bitspersample;
    sp->cinfo.d.bits_in_jsample = td->td_bitspersample;
#else
    if (td->td_bitspersample != BITS_IN_JSAMPLE ||
        sp->cinfo.d.data_precision != td->td_bitspersample)
    {
        TIFFErrorExtR(tif, module, "Improper JPEG data precision");
        return (0);
    }
#endif

    if (sp->cinfo.d.progressive_mode &&
        !sp->otherSettings.has_warned_about_progressive_mode)
    {
        TIFFWarningExtR(tif, module,
                        "The JPEG strip/tile is encoded with progressive mode, "
                        "which is normally not legal for JPEG-in-TIFF.\n"
                        "libtiff should be able to decode it, but it might "
                        "cause compatibility issues with other readers");
        sp->otherSettings.has_warned_about_progressive_mode = TRUE;
    }

    /* In some cases, libjpeg needs to allocate a lot of memory */
    /* http://www.libjpeg-turbo.org/pmwiki/uploads/About/TwoIssueswiththeJPEGStandard.pdf
     */
    if (TIFFjpeg_has_multiple_scans(sp))
    {
        /* In this case libjpeg will need to allocate memory or backing */
        /* store for all coefficients */
        /* See call to jinit_d_coef_controller() from master_selection() */
        /* in libjpeg */

        /* 1 MB for regular libjpeg usage */
        toff_t nRequiredMemory = 1024 * 1024;

        for (ci = 0; ci < sp->cinfo.d.num_components; ci++)
        {
            const jpeg_component_info *compptr = &(sp->cinfo.d.comp_info[ci]);
            if (compptr->h_samp_factor > 0 && compptr->v_samp_factor > 0)
            {
                nRequiredMemory +=
                    (toff_t)((JDIMENSION)(((int)compptr->width_in_blocks +
                                           compptr->h_samp_factor - 1) /
                                          compptr->h_samp_factor)) *
                    (JDIMENSION)(((int)compptr->height_in_blocks +
                                  compptr->v_samp_factor - 1) /
                                 compptr->v_samp_factor) *
                    sizeof(JBLOCK);
            }
        }

        if (sp->cinfo.d.mem->max_memory_to_use > 0 &&
            nRequiredMemory > (toff_t)(sp->cinfo.d.mem->max_memory_to_use) &&
            getenv("LIBTIFF_ALLOW_LARGE_LIBJPEG_MEM_ALLOC") == NULL)
        {
            TIFFErrorExtR(
                tif, module,
                "Reading this image would require libjpeg to allocate "
                "at least %" PRIu64 " bytes. "
                "This is disabled since above the %ld threshold. "
                "You may override this restriction by defining the "
                "LIBTIFF_ALLOW_LARGE_LIBJPEG_MEM_ALLOC environment variable, "
                "or setting the JPEGMEM environment variable to a value "
                "greater "
                "or equal to '%" PRIu64 "M'",
                nRequiredMemory, sp->cinfo.d.mem->max_memory_to_use,
                (nRequiredMemory + 1000000u - 1u) / 1000000u);
            return 0;
        }
    }

    if (td->td_planarconfig == PLANARCONFIG_CONTIG)
    {
        /* Component 0 should have expected sampling factors */
        if (sp->cinfo.d.comp_info[0].h_samp_factor != sp->h_sampling ||
            sp->cinfo.d.comp_info[0].v_samp_factor != sp->v_sampling)
        {
            TIFFErrorExtR(tif, module,
                          "Improper JPEG sampling factors %d,%d\n"
                          "Apparently should be %" PRIu16 ",%" PRIu16 ".",
                          sp->cinfo.d.comp_info[0].h_samp_factor,
                          sp->cinfo.d.comp_info[0].v_samp_factor,
                          sp->h_sampling, sp->v_sampling);
            return (0);
        }
        /* Rest should have sampling factors 1,1 */
        for (ci = 1; ci < sp->cinfo.d.num_components; ci++)
        {
            if (sp->cinfo.d.comp_info[ci].h_samp_factor != 1 ||
                sp->cinfo.d.comp_info[ci].v_samp_factor != 1)
            {
                TIFFErrorExtR(tif, module, "Improper JPEG sampling factors");
                return (0);
            }
        }
    }
    else
    {
        /* PC 2's single component should have sampling factors 1,1 */
        if (sp->cinfo.d.comp_info[0].h_samp_factor != 1 ||
            sp->cinfo.d.comp_info[0].v_samp_factor != 1)
        {
            TIFFErrorExtR(tif, module, "Improper JPEG sampling factors");
            return (0);
        }
    }
    downsampled_output = FALSE;
    if (td->td_planarconfig == PLANARCONFIG_CONTIG &&
        sp->photometric == PHOTOMETRIC_YCBCR &&
        sp->otherSettings.jpegcolormode == JPEGCOLORMODE_RGB)
    {
        /* Convert YCbCr to RGB */
        sp->cinfo.d.jpeg_color_space = JCS_YCbCr;
        sp->cinfo.d.out_color_space = JCS_RGB;
    }
    else
    {
        /* Suppress colorspace handling */
        sp->cinfo.d.jpeg_color_space = JCS_UNKNOWN;
        sp->cinfo.d.out_color_space = JCS_UNKNOWN;
        if (td->td_planarconfig == PLANARCONFIG_CONTIG &&
            (sp->h_sampling != 1 || sp->v_sampling != 1))
            downsampled_output = TRUE;
        /* XXX what about up-sampling? */
    }
    if (downsampled_output)
    {
        /* Need to use raw-data interface to libjpeg */
        sp->cinfo.d.raw_data_out = TRUE;
#if JPEG_LIB_VERSION >= 70
        sp->cinfo.d.do_fancy_upsampling = FALSE;
#endif /* JPEG_LIB_VERSION >= 70 */
        tif->tif_decoderow = DecodeRowError;
        tif->tif_decodestrip = JPEGDecodeRaw;
        tif->tif_decodetile = JPEGDecodeRaw;
    }
    else
    {
        /* Use normal interface to libjpeg */
        sp->cinfo.d.raw_data_out = FALSE;
        tif->tif_decoderow = JPEGDecode;
        tif->tif_decodestrip = JPEGDecode;
        tif->tif_decodetile = JPEGDecode;
    }
    /* Start JPEG decompressor */
    if (!TIFFjpeg_start_decompress(sp))
        return (0);
    /* Allocate downsampled-data buffers if needed */
    if (downsampled_output)
    {
        if (!alloc_downsampled_buffers(tif, sp->cinfo.d.comp_info,
                                       sp->cinfo.d.num_components))
            return (0);
        sp->scancount = DCTSIZE; /* mark buffer empty */
    }
    return (1);
}

/*
 * Decode a chunk of pixels.
 * "Standard" case: returned data is not downsampled.
 */
#if !defined(JPEG_LIB_MK1_OR_12BIT)
/**
 * @brief Decode whole JPEG output rows into caller bytes.
 * @ingroup libtiff_private_jpeg
 *
 * Requires successful predecode and nonnegative extent with sufficient row
 * capacity. Refreshes dependency raw input; warns/discards a fractional
 * trailing row, zeroes padding for smaller codestream extents and advances
 * current row after supplied rows. Reads rows directly into buf; no
 * temporary allocation or repacking. A dependency row failure clears the
 * whole cc-byte output; state from earlier rows remains. Normal completion
 * commits raw cursors and may finish decompression. A negative finish result
 * is accepted by the Boolean completion expression, so success is not proof
 * of an error-free finish. No ownership transfer.
 *
 * @param[in,out] tif Non-NULL live JPEG handle with compatible current
 * directory/state; serialise operations.
 * @param[out] buf Borrowed writable cc-byte storage for decoded rows;
 * disjoint from codec buffers.
 * @param[in] cc Nonnegative byte count; whole rows of bytesperline are
 * decoded and a fractional remainder is warned about and left unread.
 * @param[in] s Sample plane index; unused.
 * @return Boolean completion, zero on explicit decode rejection/failure;
 * warned truncation or accepted negative finish can still yield one.
 */
static int JPEGDecode(TIFF *tif, uint8_t *buf, tmsize_t cc, uint16_t s)
{
    JPEGState *sp = JState(tif);
    tmsize_t nrows;
    (void)s;

    /*
    ** Update available information, buffer may have been refilled
    ** between decode requests
    */
    sp->src.next_input_byte = (const JOCTET *)tif->tif_rawcp;
    sp->src.bytes_in_buffer = (size_t)tif->tif_rawcc;

    if (sp->bytesperline == 0)
    {
        memset(buf, 0, (size_t)cc);
        return 0;
    }

    nrows = cc / sp->bytesperline;
    if (cc % sp->bytesperline)
        TIFFWarningExtR(tif, tif->tif_name, "fractional scanline not read");

    if (nrows > (tmsize_t)sp->cinfo.d.image_height)
    {
        /* Initialize the output buffer if the JPEG image is smaller than the
         * strile height. */
        memset(buf, 0, (size_t)cc);
        nrows = sp->cinfo.d.image_height;
    }
    else if (sp->strile_width > sp->cinfo.d.image_width)
    {
        /* Initialize the output buffer if the JPEG image is smaller than the
         * strile width. */
        memset(buf, 0, (size_t)cc);
    }

    /* data is expected to be read in multiples of a scanline */
    if (nrows)
    {
        do
        {
            /*
             * In the libjpeg6b-9a 8bit case.  We read directly into
             * the TIFF buffer.
             */
            JSAMPROW bufptr = (JSAMPROW)buf;

            if (TIFFjpeg_read_scanlines(sp, &bufptr, 1) != 1)
            {
                memset(buf, 0, (size_t)cc);
                return (0);
            }

            ++tif->tif_dir.td_row;
            buf += sp->bytesperline;
            cc -= sp->bytesperline;
        } while (--nrows > 0);
    }

    /* Update information on consumed data */
    tif->tif_rawcp = (uint8_t *)sp->src.next_input_byte;
    tif->tif_rawcc = (tmsize_t)sp->src.bytes_in_buffer;

    /* Close down the decompressor if we've finished the strip or tile. */
    return sp->cinfo.d.output_scanline < sp->cinfo.d.output_height ||
           TIFFjpeg_finish_decompress(sp);
}
#endif /* !defined(JPEG_LIB_MK1_OR_12BIT) */

#if defined(JPEG_LIB_MK1_OR_12BIT)
/*ARGSUSED*/ /**
              * @brief Decode whole JPEG output rows into caller bytes.
              * @ingroup libtiff_private_jpeg
              *
              * Requires successful predecode and nonnegative extent with
              * sufficient row capacity. Refreshes dependency raw input;
              * warns/discards a fractional trailing row, zeroes padding for
              * smaller codestream extents and advances current row after
              * supplied rows. For twelve-bit data, allocates a temporary row
              * of short samples, reads each row into it and packs it into
              * buf as twelve-bit pairs; the temporary row is freed before
              * return. Rows are read only when that temporary row exists: if
              * its allocation fails (or the precision is not twelve), the
              * loop still advances td_row without reading or writing, and the
              * function can still return one. Dependency row failure clears
              * the whole cc-byte output; state from earlier rows remains.
              * Normal completion commits raw cursors and may finish
              * decompression. A negative finish result is accepted by the
              * Boolean completion expression, so success is not proof of an
              * error-free finish. No ownership transfer.
              *
              * @param[in,out] tif Non-NULL live JPEG handle with compatible
              * current directory/state; serialise operations.
              * @param[out] buf Borrowed writable cc-byte storage for decoded
              * rows; disjoint from codec buffers.
              * @param[in] cc Nonnegative byte count; whole rows of
              * bytesperline are decoded and a fractional remainder is warned
              * about and left unread.
              * @param[in] s Sample plane index; unused.
              * @return Boolean completion, zero on explicit decode
              * rejection/failure; warned truncation or accepted negative finish
              * can still yield one.
              */
static int JPEGDecode(TIFF *tif, uint8_t *buf, tmsize_t cc, uint16_t s)
{
    JPEGState *sp = JState(tif);
    tmsize_t nrows;
    (void)s;

    /*
    ** Update available information, buffer may have been refilled
    ** between decode requests
    */
    sp->src.next_input_byte = (const JOCTET *)tif->tif_rawcp;
    sp->src.bytes_in_buffer = (size_t)tif->tif_rawcc;

    if (sp->bytesperline == 0)
    {
        memset(buf, 0, (size_t)cc);
        return 0;
    }

    nrows = cc / sp->bytesperline;
    if (cc % sp->bytesperline)
        TIFFWarningExtR(tif, tif->tif_name, "fractional scanline not read");

    if (nrows > (tmsize_t)sp->cinfo.d.image_height)
    {
        /* Initialize the output buffer if the JPEG image is smaller than the
         * strile height. */
        memset(buf, 0, (size_t)cc);
        nrows = sp->cinfo.d.image_height;
    }
    else if (sp->strile_width > sp->cinfo.d.image_width)
    {
        /* Initialize the output buffer if the JPEG image is smaller than the
         * strile width. */
        memset(buf, 0, (size_t)cc);
    }

    /* data is expected to be read in multiples of a scanline */
    if (nrows)
    {
        TIFF_JSAMPROW line_work_buf = NULL;

        /*
         * For 6B, only use temporary buffer for 12 bit imagery.
         * For Mk1 always use it.
         */
        if (sp->cinfo.d.data_precision == 12)
        {
            line_work_buf = (TIFF_JSAMPROW)_TIFFmallocExt(
                tif, (tmsize_t)((size_t)sizeof(short) *
                                (size_t)sp->cinfo.d.output_width *
                                (size_t)sp->cinfo.d.num_components));
        }

        do
        {
            if (line_work_buf != NULL)
            {
                /*
                 * In the MK1 case, we always read into a 16bit
                 * buffer, and then pack down to 12bit or 8bit.
                 * In 6B case we only read into 16 bit buffer
                 * for 12bit data, which we need to repack.
                 */
                if (TIFFjpeg_read_scanlines(sp, &line_work_buf, 1) != 1)
                {
                    memset(buf, 0, (size_t)cc);
                    _TIFFfreeExt(tif, line_work_buf);
                    return (0);
                }

                if (sp->cinfo.d.data_precision == 12)
                {
                    tmsize_t value_count =
                        (tmsize_t)((JDIMENSION)sp->cinfo.d.output_width *
                                   (JDIMENSION)sp->cinfo.d.num_components);
                    tmsize_t iPair;

                    for (iPair = 0; iPair < value_count / 2; iPair++)
                    {
                        unsigned char *out_ptr =
                            ((unsigned char *)buf) + iPair * 3;
                        TIFF_JSAMPLE *in_ptr = line_work_buf + iPair * 2;

                        out_ptr[0] = (unsigned char)((in_ptr[0] & 0xff0) >> 4);
                        out_ptr[1] =
                            (unsigned char)(((in_ptr[0] & 0xf) << 4) |
                                            ((in_ptr[1] & 0xf00) >> 8));
                        out_ptr[2] = (unsigned char)(((in_ptr[1] & 0xff) >> 0));
                    }
                    if ((value_count & 1) != 0)
                        JPEGWritePacked12Sample((uint8_t *)buf, value_count - 1,
                                                line_work_buf[value_count - 1]);
                }
                else if (sp->cinfo.d.data_precision == 8)
                {
                    int value_count =
                        (int)((JDIMENSION)sp->cinfo.d.output_width *
                              (JDIMENSION)sp->cinfo.d.num_components);
                    int iValue;

                    for (iValue = 0; iValue < value_count; iValue++)
                    {
                        ((unsigned char *)buf)[iValue] =
                            (unsigned char)(line_work_buf[iValue] & 0xff);
                    }
                }
            }

            ++tif->tif_dir.td_row;
            buf += sp->bytesperline;
            cc -= sp->bytesperline;
        } while (--nrows > 0);

        if (line_work_buf != NULL)
            _TIFFfreeExt(tif, line_work_buf);
    }

    /* Update information on consumed data */
    tif->tif_rawcp = (uint8_t *)sp->src.next_input_byte;
    tif->tif_rawcc = (tmsize_t)sp->src.bytes_in_buffer;

    /* Close down the decompressor if we've finished the strip or tile. */
    return sp->cinfo.d.output_scanline < sp->cinfo.d.output_height ||
           TIFFjpeg_finish_decompress(sp);
}
#endif /* defined(JPEG_LIB_MK1_OR_12BIT) */

/*ARGSUSED*/ /**
              * @brief Reject scanline access to raw downsampled JPEG clumps.
              * @ingroup libtiff_private_jpeg
              *
              * Diagnoses unsupported access and suggests RGB colour mode. Does
              * not touch output, allocate, perform client I/O or transfer
              * ownership.
              *
              * @param[in] tif Non-NULL live JPEG handle; used only to report
              * the error.
              * @param[in] buf Unused; not written.
              * @param[in] cc Unused.
              * @param[in] s Unused.
              * @return Zero unconditionally.
              */
static int DecodeRowError(TIFF *tif, uint8_t *buf, tmsize_t cc, uint16_t s)

{
    (void)buf;
    (void)cc;
    (void)s;

    TIFFErrorExtR(
        tif, "TIFFReadScanline",
        "scanline oriented access is not supported for downsampled JPEG "
        "compressed images, consider enabling TIFFTAG_JPEGCOLORMODE as "
        "JPEGCOLORMODE_RGB.");
    return 0;
}

/*
 * Decode a chunk of pixels.
 * Returned data is downsampled per sampling factors.
 */
/*ARGSUSED*/ /**
              * @brief Decode a whole JPEG strile into packed sampled clump
              * rows.
              * @ingroup libtiff_private_jpeg
              *
              * Requires successful raw predecode, valid component buffers and
              * nonnegative caller extent for all remaining strile rows. Clears
              * the entire output first, computes checked clump/packed byte
              * extents, gathers component rows and repacks ABI-specific samples
              * with optional temporary staging. Clamps the last strip to image
              * rows and rejects insufficient per-clump capacity. Existing byte
              * casts in component gathering remain part of wide-sample
              * behavior; this contract is not a precision proof. Frees
              * temporary staging on ordinary exits. Advances row/scancount, but
              * does not generally commit raw cursors as the normal decoder
              * does. Failure leaves partial output/state; a negative dependency
              * finish can be accepted by Boolean completion. No caller
              * ownership transfer.
              *
              * @param[in,out] tif Non-NULL live JPEG handle with compatible
              * current directory/state; serialise operations.
              * @param[out] buf Borrowed writable cc-byte storage for packed
              * clump rows; zeroed first, then filled; disjoint from codec
              * buffers.
              * @param[in] cc Nonnegative byte count; must hold the packed
              * clump rows for the rest of the strile.
              * @param[in] s Sample plane index; unused.
              * @return One on accepted completion, zero on explicit
              * extent/allocation/row failure; accepted negative finish can
              * still produce one.
              */
static int JPEGDecodeRaw(TIFF *tif, uint8_t *buf, tmsize_t cc, uint16_t s)
{
    JPEGState *sp = JState(tif);
    tmsize_t nrows;
    TIFFDirectory *td = &tif->tif_dir;
    (void)s;

    nrows = sp->cinfo.d.image_height;
    /* For last strip, limit number of rows to its truncated height */
    /* even if the codestream height is larger (which is not compliant, */
    /* but that we tolerate) */
    if ((uint32_t)nrows > td->td_imagelength - tif->tif_dir.td_row &&
        !isTiled(tif))
        nrows = td->td_imagelength - tif->tif_dir.td_row;

    /* The downsampled-data decode loop below only writes up to the JPEG
     * codestream dimensions (and, due to the clump stride, may not cover the
     * full caller buffer even for compliant files). Zero the whole buffer so
     * no uninitialised heap is ever returned, mirroring the guard JPEGDecode
     * received in 65759931ab6e (#826). */
    memset(buf, 0, (size_t)cc);

#if defined(JPEG_LIB_MK1_OR_12BIT)
    unsigned short *tmpbuf = NULL;
#endif

    /* data is expected to be read in multiples of a scanline */
    if (nrows != 0)
    {

        /* Cb,Cr both have sampling factors 1, so this is correct */
        JDIMENSION clumps_per_line = sp->cinfo.d.comp_info[1].downsampled_width;
        int samples_per_clump = sp->samplesperclump;
        tmsize_t samplesperclumpline;
        tmsize_t bytesperclumpline;
        uint64_t samplesperclumpline64;

        /* The loop below writes tmpbuf in raw clump layout: each clump holds
         * samples_per_clump samples and there are clumps_per_line clumps.
         * Thus the required number of elements is their product, rather than
         * output_width * num_components.  The two expressions happen to be
         * equal for some subsampling configurations (for example 2x2), but
         * are not equivalent in general. */
        samplesperclumpline64 =
            _TIFFMultiply64(tif, (uint64_t)clumps_per_line,
                            (uint64_t)samples_per_clump, "JPEGDecodeRaw");
        samplesperclumpline =
            _TIFFCastUInt64ToSSize(tif, samplesperclumpline64, "JPEGDecodeRaw");
        bytesperclumpline = JPEGRawBytesPerClumpLine(
            tif, sp->strile_width, sp->h_sampling, sp->v_sampling,
            sp->cinfo.d.data_precision, "JPEGDecodeRaw");
        if (samplesperclumpline == 0 || bytesperclumpline == 0)
            return 0;

#if defined(JPEG_LIB_MK1_OR_12BIT)
        tmpbuf = (unsigned short *)_TIFFCheckMalloc(
            tif, samplesperclumpline, sizeof(unsigned short), "JPEGDecodeRaw");
        if (tmpbuf == NULL)
        {
            TIFFErrorExtR(tif, "JPEGDecodeRaw", "Out of memory");
            return 0;
        }
#endif

        do
        {
            jpeg_component_info *compptr;
            int ci, clumpoffset;

            if (cc < bytesperclumpline)
            {
                TIFFErrorExtR(
                    tif, "JPEGDecodeRaw",
                    "application buffer not large enough for all data.");
                goto error;
            }

            /* Reload downsampled-data buffer if needed */
            if (sp->scancount >= DCTSIZE)
            {
                int n = sp->cinfo.d.max_v_samp_factor * DCTSIZE;
                if (TIFFjpeg_read_raw_data(sp, sp->ds_buffer, n) != n)
                    goto error;
                sp->scancount = 0;
            }
            /*
             * Fastest way to unseparate data is to make one pass
             * over the scanline for each row of each component.
             */
            clumpoffset = 0; /* first sample in clump */
            for (ci = 0, compptr = sp->cinfo.d.comp_info;
                 ci < sp->cinfo.d.num_components; ci++, compptr++)
            {
                int hsamp = compptr->h_samp_factor;
                int vsamp = compptr->v_samp_factor;
                int ypos;

                for (ypos = 0; ypos < vsamp; ypos++)
                {
                    TIFF_JSAMPLE *inptr =
                        sp->ds_buffer[ci][sp->scancount * vsamp + ypos];
                    JDIMENSION nclump;
#if defined(JPEG_LIB_MK1_OR_12BIT)
                    TIFF_JSAMPLE *outptr = (TIFF_JSAMPLE *)tmpbuf + clumpoffset;
#else
                    uint8_t *outptr = buf + clumpoffset;
                    if (cc < (tmsize_t)(clumpoffset +
                                        (tmsize_t)samples_per_clump *
                                            (clumps_per_line - 1) +
                                        hsamp))
                    {
                        TIFFErrorExtR(
                            tif, "JPEGDecodeRaw",
                            "application buffer not large enough for all data, "
                            "possible subsampling issue");
                        goto error;
                    }
#endif

                    if (hsamp == 1)
                    {
                        /* fast path for at least Cb and Cr */
                        for (nclump = clumps_per_line; nclump-- > 0;)
                        {
                            outptr[0] = (uint8_t)*inptr++;
                            outptr += samples_per_clump;
                        }
                    }
                    else
                    {
                        int xpos;

                        /* general case */
                        for (nclump = clumps_per_line; nclump-- > 0;)
                        {
                            for (xpos = 0; xpos < hsamp; xpos++)
                                outptr[xpos] = (uint8_t)*inptr++;
                            outptr += samples_per_clump;
                        }
                    }
                    clumpoffset += hsamp;
                }
            }

#if defined(JPEG_LIB_MK1_OR_12BIT)
            {
                if (sp->cinfo.d.data_precision == 8)
                {
                    tmsize_t i = 0;
                    tmsize_t len = samplesperclumpline;
                    for (i = 0; i < len; i++)
                    {
                        ((unsigned char *)buf)[i] =
                            (unsigned char)(tmpbuf[i] & 0xff);
                    }
                }
                else
                { /* 12-bit */
                    tmsize_t iPair;
                    for (iPair = 0; iPair < samplesperclumpline / 2; iPair++)
                    {
                        unsigned char *out_ptr =
                            ((unsigned char *)buf) + iPair * 3;
                        TIFF_JSAMPLE *in_ptr =
                            (TIFF_JSAMPLE *)(tmpbuf + iPair * 2);

                        out_ptr[0] = (unsigned char)((in_ptr[0] & 0xff0) >> 4);
                        out_ptr[1] =
                            (unsigned char)(((in_ptr[0] & 0xf) << 4) |
                                            ((in_ptr[1] & 0xf00) >> 8));
                        out_ptr[2] = (unsigned char)(((in_ptr[1] & 0xff) >> 0));
                    }
                    if ((samplesperclumpline & 1) != 0)
                        JPEGWritePacked12Sample(
                            (uint8_t *)buf, samplesperclumpline - 1,
                            (TIFF_JSAMPLE)tmpbuf[samplesperclumpline - 1]);
                }
            }
#endif

            sp->scancount++;
            tif->tif_dir.td_row += sp->v_sampling;

            buf += bytesperclumpline;
            cc -= bytesperclumpline;

            nrows -= sp->v_sampling;
        } while (nrows > 0);

#if defined(JPEG_LIB_MK1_OR_12BIT)
        _TIFFfreeExt(tif, tmpbuf);
#endif
    }

    /* Close down the decompressor if done. */
    return sp->cinfo.d.output_scanline < sp->cinfo.d.output_height ||
           TIFFjpeg_finish_decompress(sp);

error:
#if defined(JPEG_LIB_MK1_OR_12BIT)
    _TIFFfreeExt(tif, tmpbuf);
#endif
    return 0;
}

/*
 * JPEG Encoding.
 */

/**
 * @brief Enable emission of an existing JPEG quant table.
 * @ingroup libtiff_private_jpeg
 *
 * Requires a valid table index in dependency bounds. If present, sets its
 * sent_table flag to FALSE. No allocation, client I/O, diagnostics,
 * ownership transfer or status result.
 *
 * @param[in,out] sp Borrowed live JPEG state and dependency-owned table
 * views.
 * @param[in] tblno Quantisation table index, 0 to NUM_QUANT_TBLS-1.
 */
static void unsuppress_quant_table(JPEGState *sp, int tblno)
{
    JQUANT_TBL *qtbl;

    if ((qtbl = sp->cinfo.c.quant_tbl_ptrs[tblno]) != NULL)
        qtbl->sent_table = FALSE;
}

/**
 * @brief Suppress emission of an existing JPEG quant table.
 * @ingroup libtiff_private_jpeg
 *
 * Requires a valid table index in dependency bounds. If present, sets its
 * sent_table flag to TRUE. No allocation, client I/O, diagnostics,
 * ownership transfer or status result.
 *
 * @param[in,out] sp Borrowed live JPEG state and dependency-owned table
 * views.
 * @param[in] tblno Quantisation table index, 0 to NUM_QUANT_TBLS-1.
 */
static void suppress_quant_table(JPEGState *sp, int tblno)
{
    JQUANT_TBL *qtbl;

    if ((qtbl = sp->cinfo.c.quant_tbl_ptrs[tblno]) != NULL)
        qtbl->sent_table = TRUE;
}

/**
 * @brief Enable emission of an existing JPEG huff table.
 * @ingroup libtiff_private_jpeg
 *
 * Requires a valid table index in dependency bounds. If present, sets its
 * sent_table flag to FALSE. Applies to both DC and AC Huffman entries. No
 * allocation, client I/O, diagnostics, ownership transfer or status result.
 *
 * @param[in,out] sp Borrowed live JPEG state and dependency-owned table
 * views.
 * @param[in] tblno Huffman table index, 0 to NUM_HUFF_TBLS-1; selects
 * both the DC and the AC table.
 */
static void unsuppress_huff_table(JPEGState *sp, int tblno)
{
    JHUFF_TBL *htbl;

    if ((htbl = sp->cinfo.c.dc_huff_tbl_ptrs[tblno]) != NULL)
        htbl->sent_table = FALSE;
    if ((htbl = sp->cinfo.c.ac_huff_tbl_ptrs[tblno]) != NULL)
        htbl->sent_table = FALSE;
}

/**
 * @brief Suppress emission of an existing JPEG huff table.
 * @ingroup libtiff_private_jpeg
 *
 * Requires a valid table index in dependency bounds. If present, sets its
 * sent_table flag to TRUE. Applies to both DC and AC Huffman entries. No
 * allocation, client I/O, diagnostics, ownership transfer or status result.
 *
 * @param[in,out] sp Borrowed live JPEG state and dependency-owned table
 * views.
 * @param[in] tblno Huffman table index, 0 to NUM_HUFF_TBLS-1; selects
 * both the DC and the AC table.
 */
static void suppress_huff_table(JPEGState *sp, int tblno)
{
    JHUFF_TBL *htbl;

    if ((htbl = sp->cinfo.c.dc_huff_tbl_ptrs[tblno]) != NULL)
        htbl->sent_table = TRUE;
    if ((htbl = sp->cinfo.c.ac_huff_tbl_ptrs[tblno]) != NULL)
        htbl->sent_table = TRUE;
}

/**
 * @brief Generate selected JPEG quantisation and Huffman tables.
 * @ingroup libtiff_private_jpeg
 *
 * Applies quality, suppresses all tables then selectively enables requested
 * luma/chroma entries. Replaces owned table storage with a tables-only
 * destination and emits the stream. Failures can invalidate old table views
 * and retain partial new tables/dependency state; wrapper diagnostics
 * apply. No client payload I/O or caller ownership transfer.
 *
 * @param[in,out] tif Non-NULL live JPEG handle with compatible current
 * directory/state; serialise operations.
 * @return One after emission, zero on dependency/table-buffer failure.
 */
static int prepare_JPEGTables(TIFF *tif)
{
    JPEGState *sp = JState(tif);

    /* Initialize quant tables for current quality setting */
    if (!TIFFjpeg_set_quality(sp, sp->otherSettings.jpegquality, FALSE))
        return (0);
    /* Mark only the tables we want for output */
    /* NB: chrominance tables are currently used only with YCbCr */
    if (!TIFFjpeg_suppress_tables(sp, TRUE))
        return (0);
    if (sp->otherSettings.jpegtablesmode & JPEGTABLESMODE_QUANT)
    {
        unsuppress_quant_table(sp, 0);
        if (sp->photometric == PHOTOMETRIC_YCBCR)
            unsuppress_quant_table(sp, 1);
    }
    if (sp->otherSettings.jpegtablesmode & JPEGTABLESMODE_HUFF)
    {
        unsuppress_huff_table(sp, 0);
        if (sp->photometric == PHOTOMETRIC_YCBCR)
            unsuppress_huff_table(sp, 1);
    }
    /* Direct libjpeg output into otherSettings.jpegtables */
    if (!TIFFjpeg_tables_dest(sp, tif))
        return (0);
    /* Emit tables-only datastream */
    if (!TIFFjpeg_write_tables(sp))
        return (0);

    return (1);
}

#if defined(JPEG_LIB_VERSION_MAJOR) &&                                         \
    (JPEG_LIB_VERSION_MAJOR > 9 ||                                             \
     (JPEG_LIB_VERSION_MAJOR == 9 && JPEG_LIB_VERSION_MINOR >= 4))
/* This is a modified version of std_huff_tables() from jcparam.c
 * in libjpeg-9d because it no longer initializes default Huffman
 * tables in jpeg_set_defaults(). */
/**
 * @brief Populate missing standard JPEG Huffman tables for relevant dependency
 * versions.
 * @ingroup libtiff_private_jpeg
 *
 * Copies built-in standard bit-count/value arrays into dependency-owned
 * table objects, allocating absent entries directly through libjpeg.
 * Requires a live compressor and valid installed error context; this helper
 * establishes no new local setjmp frame. Dependency
 * allocation/fatal-handler effects apply. No client payload I/O, retained
 * caller pointer or ownership transfer; no status result.
 *
 * @param[in,out] cinfo Borrowed live dependency compressor context with
 * installed fatal-error routing.
 */
static void TIFF_std_huff_tables(j_compress_ptr cinfo)
{

    if (cinfo->dc_huff_tbl_ptrs[0] == NULL)
    {
        (void)jpeg_std_huff_table((j_common_ptr)cinfo, TRUE, 0);
    }
    if (cinfo->ac_huff_tbl_ptrs[0] == NULL)
    {
        (void)jpeg_std_huff_table((j_common_ptr)cinfo, FALSE, 0);
    }
    if (cinfo->dc_huff_tbl_ptrs[1] == NULL)
    {
        (void)jpeg_std_huff_table((j_common_ptr)cinfo, TRUE, 1);
    }
    if (cinfo->ac_huff_tbl_ptrs[1] == NULL)
    {
        (void)jpeg_std_huff_table((j_common_ptr)cinfo, FALSE, 1);
    }
}
#endif

/**
 * @brief Initialise JPEG compression parameters tables and output manager.
 * @ingroup libtiff_private_jpeg
 *
 * Can switch twelve-bit ABI through reinitialisation. Creates compression
 * mode, establishes colour defaults, disables progressive output, handles
 * dependency-specific Huffman policy, validates
 * photometric/precision/sampling and MCU-aligned strip/tile dimensions. May
 * supply ReferenceBlackWhite via a nested setter whose status is ignored.
 * Table generation can replace owned storage and mark directory metadata
 * dirty; placeholder detection reads eight bytes, so existing table storage
 * must cover that prefix. Failure can leave changed tags/methods/dependency
 * allocations. No caller ownership transfer or pixel transfer.
 *
 * @param[in,out] tif Non-NULL live JPEG handle with compatible current
 * directory/state; serialise operations.
 * @return One after setup, zero on detected validation/dependency/table
 * allocation failure.
 */
static int JPEGSetupEncode(TIFF *tif)
{
    JPEGState *sp = JState(tif);
    TIFFDirectory *td = &tif->tif_dir;
    static const char module[] = "JPEGSetupEncode";

#if defined(JPEG_DUAL_MODE_8_12) && !defined(FROM_TIF_JPEG_12)
    if (tif->tif_dir.td_bitspersample == 12)
    {
        /* We pass a pointer to a copy of otherSettings, since */
        /* TIFFReInitJPEG_12() will clear sp */
        JPEGOtherSettings savedOtherSettings = sp->otherSettings;
        return TIFFReInitJPEG_12(tif, &savedOtherSettings, COMPRESSION_JPEG, 1);
    }
#endif

    if (!JPEGInitializeLibJPEG(tif, FALSE))
    {
        TIFFErrorExtR(tif, module, "Failed to initialize libjpeg compressor");
        return (0);
    }

    assert(sp != NULL);
    assert(!sp->cinfo.comm.is_decompressor);

    sp->photometric = td->td_photometric;

    /*
     * Initialize all JPEG parameters to default values.
     * Note that jpeg_set_defaults needs legal values for
     * in_color_space and input_components.
     */
    if (td->td_planarconfig == PLANARCONFIG_CONTIG)
    {
        sp->cinfo.c.input_components = td->td_samplesperpixel;
        if (sp->photometric == PHOTOMETRIC_YCBCR)
        {
            if (sp->otherSettings.jpegcolormode == JPEGCOLORMODE_RGB)
            {
                sp->cinfo.c.in_color_space = JCS_RGB;
            }
            else
            {
                sp->cinfo.c.in_color_space = JCS_YCbCr;
            }
        }
        else
        {
            if ((td->td_photometric == PHOTOMETRIC_MINISWHITE ||
                 td->td_photometric == PHOTOMETRIC_MINISBLACK) &&
                td->td_samplesperpixel == 1)
                sp->cinfo.c.in_color_space = JCS_GRAYSCALE;
            else if (td->td_photometric == PHOTOMETRIC_RGB &&
                     td->td_samplesperpixel == 3)
                sp->cinfo.c.in_color_space = JCS_RGB;
            else if (td->td_photometric == PHOTOMETRIC_SEPARATED &&
                     td->td_samplesperpixel == 4)
                sp->cinfo.c.in_color_space = JCS_CMYK;
            else
                sp->cinfo.c.in_color_space = JCS_UNKNOWN;
        }
    }
    else
    {
        sp->cinfo.c.input_components = 1;
        sp->cinfo.c.in_color_space = JCS_UNKNOWN;
    }
    if (!TIFFjpeg_set_defaults(sp))
        return (0);

    /* mozjpeg by default enables progressive JPEG, which is illegal in
     * JPEG-in-TIFF */
    /* So explicitly disable it. */
    if (sp->cinfo.c.num_scans != 0 &&
        (sp->otherSettings.jpegtablesmode & JPEGTABLESMODE_HUFF) != 0)
    {
        /* it has been found that mozjpeg could create corrupt strips/tiles */
        /* in non optimize_coding mode. */
        TIFFWarningExtR(
            tif, module,
            "mozjpeg library likely detected. Disable emission of "
            "Huffman tables in JpegTables tag, and use optimize_coding "
            "to avoid potential issues");
        sp->otherSettings.jpegtablesmode &= ~JPEGTABLESMODE_HUFF;
    }
    sp->cinfo.c.num_scans = 0;
    sp->cinfo.c.scan_info = NULL;

    /* Set per-file parameters */
    switch (sp->photometric)
    {
        case PHOTOMETRIC_YCBCR:
            sp->h_sampling = td->td_ycbcrsubsampling[0];
            sp->v_sampling = td->td_ycbcrsubsampling[1];
            if (sp->h_sampling == 0 || sp->v_sampling == 0)
            {
                TIFFErrorExtR(tif, module,
                              "Invalig horizontal/vertical sampling value");
                return (0);
            }
            if (td->td_bitspersample > 16)
            {
                TIFFErrorExtR(tif, module,
                              "BitsPerSample %" PRIu16 " not allowed for JPEG",
                              td->td_bitspersample);
                return (0);
            }

            /*
             * A ReferenceBlackWhite field *must* be present since the
             * default value is inappropriate for YCbCr.  Fill in the
             * proper value if application didn't set it.
             */
            {
                float *ref;
                if (!TIFFGetField(tif, TIFFTAG_REFERENCEBLACKWHITE, &ref))
                {
                    float refbw[6];
                    uint32_t top = 1U << td->td_bitspersample;
                    refbw[0] = 0;
                    refbw[1] = (float)(top - 1);
                    refbw[2] = (float)(top >> 1);
                    refbw[3] = refbw[1];
                    refbw[4] = refbw[2];
                    refbw[5] = refbw[1];
                    TIFFSetField(tif, TIFFTAG_REFERENCEBLACKWHITE, refbw);
                }
            }
            break;
        case PHOTOMETRIC_PALETTE: /* disallowed by Tech Note */
        case PHOTOMETRIC_MASK:
            TIFFErrorExtR(tif, module,
                          "PhotometricInterpretation %" PRIu16
                          " not allowed for JPEG",
                          sp->photometric);
            return (0);
        default:
            /* TIFF 6.0 forbids subsampling of all other color spaces */
            sp->h_sampling = 1;
            sp->v_sampling = 1;
            break;
    }

        /* Verify miscellaneous parameters */

        /*
         * This would need work if libtiff ever supports different
         * depths for different components, or if libjpeg ever supports
         * run-time selection of depth.  Neither is imminent.
         */
#ifdef JPEG_LIB_MK1
    /* BITS_IN_JSAMPLE now permits 8 and 12 --- dgilbert */
    if (td->td_bitspersample != 8 && td->td_bitspersample != 12)
#else
    if (td->td_bitspersample != BITS_IN_JSAMPLE)
#endif
    {
        TIFFErrorExtR(tif, module,
                      "BitsPerSample %" PRIu16 " not allowed for JPEG",
                      td->td_bitspersample);
        return (0);
    }
    sp->cinfo.c.data_precision = td->td_bitspersample;
#ifdef JPEG_LIB_MK1
    sp->cinfo.c.bits_in_jsample = td->td_bitspersample;
#endif
    if (isTiled(tif))
    {
        if ((td->td_tilelength % ((uint32_t)sp->v_sampling * DCTSIZE)) != 0)
        {
            TIFFErrorExtR(tif, module,
                          "JPEG tile height must be multiple of %" PRIu32,
                          (uint32_t)sp->v_sampling * DCTSIZE);
            return (0);
        }
        if ((td->td_tilewidth % ((uint32_t)sp->h_sampling * DCTSIZE)) != 0)
        {
            TIFFErrorExtR(tif, module,
                          "JPEG tile width must be multiple of %" PRIu32,
                          (uint32_t)sp->h_sampling * DCTSIZE);
            return (0);
        }
    }
    else
    {
        if (td->td_rowsperstrip < td->td_imagelength &&
            (td->td_rowsperstrip % ((uint32_t)sp->v_sampling * DCTSIZE)) != 0)
        {
            TIFFErrorExtR(tif, module,
                          "RowsPerStrip must be multiple of %" PRIu32
                          " for JPEG",
                          (uint32_t)sp->v_sampling * DCTSIZE);
            return (0);
        }
    }

    /* Create a JPEGTables field if appropriate */
    if (sp->otherSettings.jpegtablesmode &
        (JPEGTABLESMODE_QUANT | JPEGTABLESMODE_HUFF))
    {
        if (sp->otherSettings.jpegtables == NULL ||
            memcmp(sp->otherSettings.jpegtables, "\0\0\0\0\0\0\0\0\0", 8) == 0)
        {
#if defined(JPEG_LIB_VERSION_MAJOR) &&                                         \
    (JPEG_LIB_VERSION_MAJOR > 9 ||                                             \
     (JPEG_LIB_VERSION_MAJOR == 9 && JPEG_LIB_VERSION_MINOR >= 4))
            if ((sp->otherSettings.jpegtablesmode & JPEGTABLESMODE_HUFF) != 0 &&
                (sp->cinfo.c.dc_huff_tbl_ptrs[0] == NULL ||
                 sp->cinfo.c.dc_huff_tbl_ptrs[1] == NULL ||
                 sp->cinfo.c.ac_huff_tbl_ptrs[0] == NULL ||
                 sp->cinfo.c.ac_huff_tbl_ptrs[1] == NULL))
            {
                /* libjpeg-9d no longer initializes default Huffman tables in */
                /* jpeg_set_defaults() */
                TIFF_std_huff_tables(&sp->cinfo.c);
            }
#endif

            if (!prepare_JPEGTables(tif))
                return (0);
            /* Mark the field present */
            /* Can't use TIFFSetField since BEENWRITING is already set! */
            tif->tif_flags |= TIFF_DIRTYDIRECT;
            TIFFSetFieldBit(tif, FIELD_JPEGTABLES);
        }
    }
    else
    {
        /* We do not support application-supplied JPEGTables, */
        /* so mark the field not present */
        TIFFClrFieldBit(tif, FIELD_JPEGTABLES);
    }

    /* Direct libjpeg output to libtiff's output buffer */
    TIFFjpeg_data_dest(sp, tif);

    return (1);
}

/*
 * Set encoding state at the start of a strip or tile.
 */
/**
 * @brief Select per-strile JPEG sampling table emission and row encoding.
 * @ingroup libtiff_private_jpeg
 *
 * Computes strile geometry, validates JPEG_MAX_DIMENSION, sets
 * colour/sample tables and suppresses extraneous JFIF/Adobe markers.
 * Selects normal or raw sampled input, starts compression, allocates
 * downsampled image-pool buffers where needed and resets row/error state. A
 * mode-switch setup call is not directly checked before subsequent
 * assertions/use. Dependency or allocation failure can leave partial
 * state/output; caller retains input ownership.
 *
 * @param[in,out] tif Non-NULL live JPEG handle with compatible current
 * directory/state; serialise operations.
 * @param[in] s Sample plane index; sizes a separate chroma plane and, for
 * separate planes, becomes the component id (nonzero YCbCr planes use the
 * chroma tables).
 * @return One after start/buffer selection, zero on detected
 * geometry/dependency/allocation failure.
 */
static int JPEGPreEncode(TIFF *tif, uint16_t s)
{
    JPEGState *sp = JState(tif);
    TIFFDirectory *td = &tif->tif_dir;
    static const char module[] = "JPEGPreEncode";
    int downsampled_input;

    assert(sp != NULL);

    if (sp->cinfo.comm.is_decompressor == 1)
    {
        tif->tif_setupencode(tif);
    }

    assert(!sp->cinfo.comm.is_decompressor);

    if (!JPEGComputeStrileWidthHeightBytesPerLine(tif, s))
        return 0;

    if (sp->strile_width > (uint32_t)JPEG_MAX_DIMENSION ||
        sp->strile_height > (uint32_t)JPEG_MAX_DIMENSION)
    {
        TIFFErrorExtR(tif, module,
                      "Strip/tile too large for JPEG. Maximum dimension is %d",
                      (int)JPEG_MAX_DIMENSION);
        return (0);
    }
    sp->cinfo.c.image_width = sp->strile_width;
    sp->cinfo.c.image_height = sp->strile_height;
    downsampled_input = FALSE;
    if (td->td_planarconfig == PLANARCONFIG_CONTIG)
    {
        sp->cinfo.c.input_components = td->td_samplesperpixel;
        if (sp->photometric == PHOTOMETRIC_YCBCR)
        {
            if (sp->otherSettings.jpegcolormode != JPEGCOLORMODE_RGB)
            {
                if (sp->h_sampling != 1 || sp->v_sampling != 1)
                    downsampled_input = TRUE;
            }
            if (!TIFFjpeg_set_colorspace(sp, JCS_YCbCr))
                return (0);
            /*
             * Set Y sampling factors;
             * we assume jpeg_set_colorspace() set the rest to 1
             */
            sp->cinfo.c.comp_info[0].h_samp_factor = sp->h_sampling;
            sp->cinfo.c.comp_info[0].v_samp_factor = sp->v_sampling;
        }
        else
        {
            if (!TIFFjpeg_set_colorspace(sp, sp->cinfo.c.in_color_space))
                return (0);
            /* jpeg_set_colorspace set all sampling factors to 1 */
        }
    }
    else
    {
        if (!TIFFjpeg_set_colorspace(sp, JCS_UNKNOWN))
            return (0);
        sp->cinfo.c.comp_info[0].component_id = s;
        /* jpeg_set_colorspace() set sampling factors to 1 */
        if (sp->photometric == PHOTOMETRIC_YCBCR && s > 0)
        {
            sp->cinfo.c.comp_info[0].quant_tbl_no = 1;
            sp->cinfo.c.comp_info[0].dc_tbl_no = 1;
            sp->cinfo.c.comp_info[0].ac_tbl_no = 1;
        }
    }
    /* ensure libjpeg won't write any extraneous markers */
    sp->cinfo.c.write_JFIF_header = FALSE;
    sp->cinfo.c.write_Adobe_marker = FALSE;
    /* set up table handling correctly */
    /* calling TIFFjpeg_set_quality() causes quantization tables to be flagged
     */
    /* as being to be emitted, which we don't want in the JPEGTABLESMODE_QUANT
     */
    /* mode, so we must manually suppress them. However TIFFjpeg_set_quality()
     */
    /* should really be called when dealing with files with directories with */
    /* mixed qualities. see http://trac.osgeo.org/gdal/ticket/3539 */
    if (!TIFFjpeg_set_quality(sp, sp->otherSettings.jpegquality, FALSE))
        return (0);
    if (sp->otherSettings.jpegtablesmode & JPEGTABLESMODE_QUANT)
    {
        suppress_quant_table(sp, 0);
        suppress_quant_table(sp, 1);
    }
    else
    {
        unsuppress_quant_table(sp, 0);
        unsuppress_quant_table(sp, 1);
    }
    if (sp->otherSettings.jpegtablesmode & JPEGTABLESMODE_HUFF)
    {
        /* Explicit suppression is only needed if we did not go through the */
        /* prepare_JPEGTables() code path, which may be the case if updating */
        /* an existing file */
        suppress_huff_table(sp, 0);
        suppress_huff_table(sp, 1);

        /* We want to keep optimize_coding = TRUE for 12-bit JPEG */
        /* See lengthy explanation at
         * https://gitlab.com/libtiff/libtiff/-/work_items/773#note_3009836854
         */
        if (sp->cinfo.c.data_precision == 8)
            sp->cinfo.c.optimize_coding = FALSE;
    }
    else
        sp->cinfo.c.optimize_coding = TRUE;
    if (downsampled_input)
    {
        /* Need to use raw-data interface to libjpeg */
        sp->cinfo.c.raw_data_in = TRUE;
        tif->tif_encoderow = JPEGEncodeRaw;
        tif->tif_encodestrip = JPEGEncodeRaw;
        tif->tif_encodetile = JPEGEncodeRaw;
    }
    else
    {
        /* Use normal interface to libjpeg */
        sp->cinfo.c.raw_data_in = FALSE;
        tif->tif_encoderow = JPEGEncode;
        tif->tif_encodestrip = JPEGEncode;
        tif->tif_encodetile = JPEGEncode;
    }
    /* Start JPEG compressor */
    if (!TIFFjpeg_start_compress(sp, FALSE))
        return (0);
    /* Allocate downsampled-data buffers if needed */
    if (downsampled_input)
    {
        if (!alloc_downsampled_buffers(tif, sp->cinfo.c.comp_info,
                                       sp->cinfo.c.num_components))
            return (0);
    }
    sp->scancount = 0;
    sp->encode_raw_error = FALSE;

    return (1);
}

/*
 * Encode a chunk of pixels.
 * "Standard" case: incoming data is not downsampled.
 */
/**
 * @brief Encode complete normal JPEG rows from caller bytes.
 * @ingroup libtiff_private_jpeg
 *
 * Requires successful preencode and positive bytesperline. Warns/discards
 * fractional input and clamps the final strip to remaining image rows.
 * Twelve-bit input is unpacked into a temporary short row, including an odd
 * final sample; that allocation is freed on normal completion but not every
 * early dependency-write failure. Caller samples remain unchanged.
 * Dependency output can flush client bytes and leave partial file/cache/row
 * progress on failure; no ownership transfer.
 *
 * @param[in,out] tif Non-NULL live JPEG handle with compatible current
 * directory/state; serialise operations.
 * @param[in] buf Borrowed readable cc-byte complete input; disjoint from
 * codec buffers.
 * @param[in] cc Nonnegative byte count; whole rows of bytesperline are
 * encoded and a fractional remainder is warned about and discarded.
 * @param[in] s Sample plane index; unused.
 * @return One after accepted rows, zero on allocation/dependency row-write
 * failure; warned truncation can still succeed.
 */
static int JPEGEncode(TIFF *tif, uint8_t *buf, tmsize_t cc, uint16_t s)
{
    JPEGState *sp = JState(tif);
    tmsize_t nrows;
    TIFF_JSAMPROW bufptr[1];
    short *line16 = NULL;
    int line16_count = 0;

    (void)s;
    assert(sp != NULL);
    /* data is expected to be supplied in multiples of a scanline */
    nrows = cc / sp->bytesperline;
    if (cc % sp->bytesperline)
        TIFFWarningExtR(tif, tif->tif_name, "fractional scanline discarded");

    /* The last strip will be limited to image size */
    if (!isTiled(tif) &&
        tif->tif_dir.td_row + nrows > tif->tif_dir.td_imagelength)
        nrows = tif->tif_dir.td_imagelength - tif->tif_dir.td_row;

    if (sp->cinfo.c.data_precision == 12)
    {
        line16_count = (int)((sp->bytesperline * 2) / 3);
        line16 = (short *)_TIFFmallocExt(
            tif, (tmsize_t)(sizeof(short) * (size_t)line16_count));
        if (!line16)
        {
            TIFFErrorExtR(tif, "JPEGEncode", "Failed to allocate memory");

            return 0;
        }
    }

    while (nrows-- > 0)
    {

        if (sp->cinfo.c.data_precision == 12)
        {

            int value_pairs = line16_count / 2;
            int iPair;

            bufptr[0] = (TIFF_JSAMPROW)line16;

            for (iPair = 0; iPair < value_pairs; iPair++)
            {
                unsigned char *in_ptr = ((unsigned char *)buf) + iPair * 3;
                TIFF_JSAMPLE *out_ptr = (TIFF_JSAMPLE *)(line16 + iPair * 2);

                out_ptr[0] = (TIFF_JSAMPLE)((in_ptr[0] << 4) |
                                            ((in_ptr[1] & 0xf0) >> 4));
                out_ptr[1] =
                    (TIFF_JSAMPLE)(((in_ptr[1] & 0x0f) << 8) | in_ptr[2]);
            }
            if ((line16_count & 1) != 0)
            {
                line16[line16_count - 1] = (short)JPEGReadPacked12Sample(
                    (const uint8_t *)buf, line16_count - 1);
            }
        }
        else
        {
            bufptr[0] = (TIFF_JSAMPROW)buf;
        }
        if (TIFFjpeg_write_scanlines(sp, bufptr, 1) != 1)
            return (0);
        if (nrows > 0)
            tif->tif_dir.td_row++;
        buf += sp->bytesperline;
    }

    if (sp->cinfo.c.data_precision == 12)
    {
        _TIFFfreeExt(tif, line16);
    }

    return (1);
}

/*
 * Encode a chunk of pixels.
 * Incoming data is expected to be downsampled per sampling factors.
 */
/**
 * @brief Gather packed sampled clumps into dependency raw JPEG component rows.
 * @ingroup libtiff_private_jpeg
 *
 * Requires successful raw preencode, positive sampling/clump geometry and
 * complete nonnegative input clump rows. Computes checked clump extents,
 * validates available input, handles packed twelve-bit odd samples,
 * horizontally pads component rows and writes full MCU groups. Latches
 * encode_raw_error after selected failures to prevent unsafe retry;
 * row/scancount/file state can already change. Caller input remains
 * borrowed/unmodified; no rollback or ownership transfer.
 *
 * @param[in,out] tif Non-NULL live JPEG handle with compatible current
 * directory/state; serialise operations.
 * @param[in] buf Borrowed readable cc-byte complete input; disjoint from
 * codec buffers.
 * @param[in] cc Nonnegative byte count; whole packed clump lines are
 * encoded and a fractional remainder is warned about and discarded.
 * @param[in] s Sample plane index; unused.
 * @return One after accepted groups, zero on latched error, extent/geometry
 * rejection or dependency raw-write failure.
 */
static int JPEGEncodeRaw(TIFF *tif, uint8_t *buf, tmsize_t cc, uint16_t s)
{
    JPEGState *sp = JState(tif);
    TIFF_JSAMPLE *outptr;
    tmsize_t nrows;
    JDIMENSION clumps_per_line, nclump;
    int clumpoffset, ci, xpos, ypos;
    jpeg_component_info *compptr;
    int samples_per_clump = sp->samplesperclump;
    tmsize_t bytesperclumpline;

    (void)s;
    assert(sp != NULL);

    if (sp->encode_raw_error)
    {
        TIFFErrorExtR(tif, tif->tif_name, "JPEGEncodeRaw() already failed");
        return 0;
    }

    /* data is expected to be supplied in multiples of a clumpline */
    /* a clumpline is equivalent to v_sampling desubsampled scanlines */
    /* TODO: the following calculation of bytesperclumpline, should substitute
     * calculation of sp->bytesperline, except that it is per v_sampling lines
     */
    bytesperclumpline = JPEGRawBytesPerClumpLine(
        tif, sp->cinfo.c.image_width, sp->h_sampling, sp->v_sampling,
        sp->cinfo.c.data_precision, "JPEGEncodeRaw");
    if (bytesperclumpline == 0)
        return 0;

    nrows = (cc / bytesperclumpline) * sp->v_sampling;
    if (cc % bytesperclumpline)
        TIFFWarningExtR(tif, tif->tif_name, "fractional scanline discarded");

    /* Cb,Cr both have sampling factors 1, so this is correct */
    clumps_per_line = sp->cinfo.c.comp_info[1].downsampled_width;

    while (nrows > 0)
    {
        /*
         * Fastest way to separate the data is to make one pass
         * over the scanline for each row of each component.
         */
        clumpoffset = 0; /* first sample in clump */
        for (ci = 0, compptr = sp->cinfo.c.comp_info;
             ci < sp->cinfo.c.num_components; ci++, compptr++)
        {
            int hsamp = compptr->h_samp_factor;
            int vsamp = compptr->v_samp_factor;
            int padding =
                (int)(compptr->width_in_blocks * DCTSIZE -
                      (JDIMENSION)clumps_per_line * (JDIMENSION)hsamp);
            for (ypos = 0; ypos < vsamp; ypos++)
            {
                outptr = sp->ds_buffer[ci][sp->scancount * vsamp + ypos];
                if (sp->cinfo.c.data_precision == 12)
                {
                    tmsize_t sample_index = clumpoffset;
                    if (hsamp == 1)
                    {
                        for (nclump = clumps_per_line; nclump-- > 0;)
                        {
                            *outptr++ =
                                JPEGReadPacked12Sample(buf, sample_index);
                            sample_index += samples_per_clump;
                        }
                    }
                    else
                    {
                        for (nclump = clumps_per_line; nclump-- > 0;)
                        {
                            for (xpos = 0; xpos < hsamp; xpos++)
                                *outptr++ = JPEGReadPacked12Sample(
                                    buf, sample_index + xpos);
                            sample_index += samples_per_clump;
                        }
                    }
                }
                else
                {
                    const uint8_t *inptr8 = buf + clumpoffset;
                    if (hsamp == 1)
                    {
                        /* fast path for at least Cb and Cr */
                        for (nclump = clumps_per_line; nclump-- > 0;)
                        {
                            *outptr++ = (TIFF_JSAMPLE)inptr8[0];
                            inptr8 += samples_per_clump;
                        }
                    }
                    else
                    {
                        /* general case */
                        for (nclump = clumps_per_line; nclump-- > 0;)
                        {
                            for (xpos = 0; xpos < hsamp; xpos++)
                                *outptr++ = (TIFF_JSAMPLE)inptr8[xpos];
                            inptr8 += samples_per_clump;
                        }
                    }
                }
                /* pad each scanline as needed */
                for (xpos = 0; xpos < padding; xpos++)
                {
                    *outptr = outptr[-1];
                    outptr++;
                }
                clumpoffset += hsamp;
            }
        }
        sp->scancount++;
        if (sp->scancount >= DCTSIZE)
        {
            int n = sp->cinfo.c.max_v_samp_factor * DCTSIZE;
            if (TIFFjpeg_write_raw_data(sp, sp->ds_buffer, n) != n)
            {
                sp->encode_raw_error = TRUE;
                return (0);
            }
            sp->scancount = 0;
        }
        tif->tif_dir.td_row += sp->v_sampling;
        buf += bytesperclumpline;
        nrows -= sp->v_sampling;
    }
    return (1);
}

/*
 * Finish up at the end of a strip or tile.
 */
/**
 * @brief Pad pending raw component rows and finish JPEG compression.
 * @ingroup libtiff_private_jpeg
 *
 * When raw rows are pending, repeats the last component row to complete the
 * MCU group, emits it and then invokes the compression finish wrapper. Each
 * repeated row copies width_in_blocks * DCTSIZE * sizeof(JSAMPLE) bytes; in
 * dual-mode twelve-bit builds, where ds_buffer rows hold 16-bit samples but
 * JSAMPLE is still 8-bit, this covers only the first half of each row (a
 * known defect; the size should use TIFF_JSAMPLE). Dependency callbacks can
 * flush client output; failure leaves partial state/file data. Does not
 * persist IFD or promise durability; no ownership transfer.
 *
 * @param[in,out] tif Non-NULL live JPEG handle with compatible current
 * directory/state; serialise operations.
 * @return One when the finish call returns normally, zero on
 * component-write or trapped finish failure.
 */
static int JPEGPostEncode(TIFF *tif)
{
    JPEGState *sp = JState(tif);

    if (sp->scancount > 0)
    {
        /*
         * Need to emit a partial bufferload of downsampled data.
         * Pad the data vertically.
         */
        int ci, ypos, n;
        jpeg_component_info *compptr;

        for (ci = 0, compptr = sp->cinfo.c.comp_info;
             ci < sp->cinfo.c.num_components; ci++, compptr++)
        {
            int vsamp = compptr->v_samp_factor;
            tmsize_t row_width =
                compptr->width_in_blocks * DCTSIZE * sizeof(JSAMPLE);
            for (ypos = sp->scancount * vsamp; ypos < DCTSIZE * vsamp; ypos++)
            {
                _TIFFmemcpy((void *)sp->ds_buffer[ci][ypos],
                            (void *)sp->ds_buffer[ci][ypos - 1], row_width);
            }
        }
        n = sp->cinfo.c.max_v_samp_factor * DCTSIZE;
        if (TIFFjpeg_write_raw_data(sp, sp->ds_buffer, n) != n)
            return (0);
    }

    return (TIFFjpeg_finish_compress(JState(tif)));
}

/**
 * @brief Restore JPEG tag hooks and release dependency tables and outer state.
 * @ingroup libtiff_private_jpeg
 *
 * Destroys initialised libjpeg resources (discarding destroy status), frees
 * owned Ext JPEGTables and outer state, clears tif_data and resets
 * compression methods. Invalidates codec/pool/table views. Does not
 * finish/flush payload or close the handle; no status result. Saved sizing
 * hooks are not explicitly restored before default-method reset.
 *
 * @param[in,out] tif Non-NULL live JPEG handle with compatible current
 * directory/state; serialise operations.
 */
static void JPEGCleanup(TIFF *tif)
{
    JPEGState *sp = JState(tif);

    assert(sp != 0);

    tif->tif_tagmethods.vgetfield = sp->otherSettings.vgetparent;
    tif->tif_tagmethods.vsetfield = sp->otherSettings.vsetparent;
    tif->tif_tagmethods.printdir = sp->otherSettings.printdir;
    if (sp->cinfo_initialized)
        TIFFjpeg_destroy(sp);         /* release libjpeg resources */
    if (sp->otherSettings.jpegtables) /* tag value */
        _TIFFfreeExt(tif, sp->otherSettings.jpegtables);
    _TIFFfreeExt(tif, tif->tif_data); /* release local state */
    tif->tif_data = NULL;

    _TIFFSetDefaultCompressionState(tif);
}

/**
 * @brief Update upsampling flags and cached layout sizes after colour-mode
 * changes.
 * @ingroup libtiff_private_jpeg
 *
 * Sets UPSAMPLED for contiguous YCbCr with RGB mode, clears it otherwise,
 * and recomputes positive cached tile/scanline sizes. Getters/layout
 * arithmetic can diagnose; no pixel conversion, payload I/O or ownership
 * transfer. Failed prior tag changes do not prevent these cache updates.
 *
 * @param[in,out] tif Non-NULL live JPEG handle with compatible current
 * directory/state; serialise operations.
 */
static void JPEGResetUpsampled(TIFF *tif)
{
    JPEGState *sp = JState(tif);
    TIFFDirectory *td = &tif->tif_dir;

    /*
     * Mark whether returned data is up-sampled or not so TIFFStripSize
     * and TIFFTileSize return values that reflect the true amount of
     * data.
     */
    tif->tif_flags &= ~TIFF_UPSAMPLED;
    if (td->td_planarconfig == PLANARCONFIG_CONTIG)
    {
        if (td->td_photometric == PHOTOMETRIC_YCBCR &&
            sp->otherSettings.jpegcolormode == JPEGCOLORMODE_RGB)
        {
            tif->tif_flags |= TIFF_UPSAMPLED;
        }
        else
        {
#ifdef notdef
            if (td->td_ycbcrsubsampling[0] != 1 ||
                td->td_ycbcrsubsampling[1] != 1)
                ; /* XXX what about up-sampling? */
#endif
        }
    }

    /*
     * Must recalculate cached tile size in case sampling state changed.
     * Should we really be doing this now if image size isn't set?
     */
    if (tif->tif_dir.td_tilesize > 0)
        tif->tif_dir.td_tilesize =
            isTiled(tif) ? TIFFTileSize(tif) : (tmsize_t)(-1);
    if (tif->tif_dir.td_scanlinesize > 0)
        tif->tif_dir.td_scanlinesize = TIFFScanlineSize(tif);
}

/**
 * @brief Set JPEG tables or pseudo-options and chain related tag changes.
 * @ingroup libtiff_private_jpeg
 *
 * JPEGTABLES consumes uint32_t byte count then void*; zero rejects. The old
 * table is freed before the copy; if the allocation fails (or the pointer is
 * NULL) the table pointer is left NULL while the length, the field-present
 * bit and TIFF_DIRTYDIRECT are still set and the function returns one, so
 * callers cannot detect the failure. Supply a valid JPEG tables stream with
 * sufficient setup prefix storage, not merely a positive length. QUALITY,
 * COLORMODE and TABLESMODE consume int without comprehensive validation; colour
 * mode updates upsampling caches. PHOTOMETRIC delegates then updates caches
 * even on rejection; YCBCRSUBSAMPLING marks fetched before parent status. Other
 * tags follow parent variadic contracts. Input table bytes are copied
 * synchronously; old borrowed views can expire on failure.
 *
 * @param[in,out] tif Non-NULL live JPEG handle with compatible current
 * directory/state; serialise operations.
 * @param[in] tag JPEG/related tag identifier or parent-delegated tag.
 * @param[in,out] ap Live tag-specific va_list cursor consumed
 * synchronously; not retained.
 * @return One for handled pseudo-tags/table state, zero for explicit
 * count/field rejection, or parent status; allocation failure need not be
 * reflected.
 */
static int JPEGVSetField(TIFF *tif, uint32_t tag, va_list ap)
{
    JPEGState *sp = JState(tif);
    const TIFFField *fip;
    uint32_t v32;

    assert(sp != NULL);

    switch (tag)
    {
        case TIFFTAG_JPEGTABLES:
            v32 = (uint32_t)va_arg(ap, uint32_t);
            if (v32 == 0)
            {
                /* XXX */
                return (0);
            }
            _TIFFsetByteArrayExt(tif, &sp->otherSettings.jpegtables,
                                 va_arg(ap, void *), v32);
            sp->otherSettings.jpegtables_length = v32;
            TIFFSetFieldBit(tif, FIELD_JPEGTABLES);
            break;
        case TIFFTAG_JPEGQUALITY:
            sp->otherSettings.jpegquality = (int)va_arg(ap, int);
            return (1); /* pseudo tag */
        case TIFFTAG_JPEGCOLORMODE:
            sp->otherSettings.jpegcolormode = (int)va_arg(ap, int);
            JPEGResetUpsampled(tif);
            return (1); /* pseudo tag */
        case TIFFTAG_PHOTOMETRIC:
        {
            int ret_value = (*sp->otherSettings.vsetparent)(tif, tag, ap);
            JPEGResetUpsampled(tif);
            return ret_value;
        }
        case TIFFTAG_JPEGTABLESMODE:
            sp->otherSettings.jpegtablesmode = (int)va_arg(ap, int);
            return (1); /* pseudo tag */
        case TIFFTAG_YCBCRSUBSAMPLING:
            /* mark the fact that we have a real ycbcrsubsampling! */
            sp->otherSettings.ycbcrsampling_fetched = 1;
            /* should we be recomputing upsampling info here? */
            return (*sp->otherSettings.vsetparent)(tif, tag, ap);
        default:
            return (*sp->otherSettings.vsetparent)(tif, tag, ap);
    }

    if ((fip = TIFFFieldWithTag(tif, tag)) != NULL)
    {
        TIFFSetFieldBit(tif, fip->field_bit);
    }
    else
    {
        return (0);
    }

    tif->tif_flags |= TIFF_DIRTYDIRECT;
    return (1);
}

/**
 * @brief Retrieve JPEG tables/options or delegate related tag queries.
 * @ingroup libtiff_private_jpeg
 *
 * JPEGTABLES consumes non-NULL uint32_t* length and const void** pointer
 * outputs returning borrowed codec-owned storage, invalidated by
 * replacement/build/cleanup. QUALITY, COLORMODE and TABLESMODE consume
 * int*. Other tags, including YCbCrSubsampling, are delegated unchanged to
 * the saved parent getter. This hook itself performs no allocation or client
 * I/O; the parent getter's effects apply to delegated tags.
 *
 * @param[in,out] tif Non-NULL live JPEG handle with compatible current
 * directory/state; serialise operations.
 * @param[in] tag JPEG/related tag identifier or parent-delegated tag.
 * @param[in,out] ap Live tag-specific va_list cursor consumed
 * synchronously; not retained.
 * @return One for codec retrieval, otherwise parent getter status.
 */
static int JPEGVGetField(TIFF *tif, uint32_t tag, va_list ap)
{
    JPEGState *sp = JState(tif);

    assert(sp != NULL);

    switch (tag)
    {
        case TIFFTAG_JPEGTABLES:
            *va_arg(ap, uint32_t *) = sp->otherSettings.jpegtables_length;
            *va_arg(ap, const void **) = sp->otherSettings.jpegtables;
            break;
        case TIFFTAG_JPEGQUALITY:
            *va_arg(ap, int *) = sp->otherSettings.jpegquality;
            break;
        case TIFFTAG_JPEGCOLORMODE:
            *va_arg(ap, int *) = sp->otherSettings.jpegcolormode;
            break;
        case TIFFTAG_JPEGTABLESMODE:
            *va_arg(ap, int *) = sp->otherSettings.jpegtablesmode;
            break;
        default:
            return (*sp->otherSettings.vgetparent)(tif, tag, ap);
    }
    return (1);
}

/**
 * @brief Print JPEG table extent then invoke the saved directory printer.
 * @ingroup libtiff_private_jpeg
 *
 * Prints present table length to borrowed FILE and chains the optional
 * parent with flags. Does not check stdio failures or close the stream. No
 * ownership transfer or separate error status; parent callback effects
 * apply.
 *
 * @param[in,out] tif Non-NULL live JPEG handle with compatible current
 * directory/state; serialise operations.
 * @param[in,out] fd Borrowed writable FILE; remains caller-owned.
 * @param[in] flags Print flags forwarded to the parent printer.
 */
static void JPEGPrintDir(TIFF *tif, FILE *fd, long flags)
{
    JPEGState *sp = JState(tif);

    assert(sp != NULL);
    (void)flags;

    if (sp != NULL)
    {
        if (TIFFFieldSet(tif, FIELD_JPEGTABLES))
            fprintf(fd, "  JPEG Tables: (%" PRIu32 " bytes)\n",
                    sp->otherSettings.jpegtables_length);
        if (sp->otherSettings.printdir)
            (*sp->otherSettings.printdir)(tif, fd, flags);
    }
}

/**
 * @brief Round the parent strip recommendation to JPEG MCU height.
 * @ingroup libtiff_private_jpeg
 *
 * Calls the saved sizing method and, when the result is below ImageLength,
 * rounds it up to a multiple of the directory YCbCrSubsampling vertical
 * factor (default 2) times DCTSIZE. This rounding is applied for every
 * photometric, not only YCbCr, and does not use the cached sampling in the
 * codec state. Requires positive representable
 * sampling/rounding arithmetic; does not apply the tag or validate the
 * final recommendation. Parent callback effects/diagnostics apply; no
 * ownership transfer.
 *
 * @param[in,out] tif Non-NULL live JPEG handle with compatible current
 * directory/state; serialise operations.
 * @param[in] s Requested rows per strip (0 lets the parent choose); passed
 * to the saved parent recommender.
 * @return Adjusted recommended rows per strip; no independent error
 * sentinel.
 */
static uint32_t JPEGDefaultStripSize(TIFF *tif, uint32_t s)
{
    JPEGState *sp = JState(tif);
    TIFFDirectory *td = &tif->tif_dir;

    s = (*sp->otherSettings.defsparent)(tif, s);
    if (s < td->td_imagelength)
        s = TIFFroundup_32(s, td->td_ycbcrsubsampling[1] * DCTSIZE);
    return (s);
}

/**
 * @brief Round parent tile recommendations to JPEG MCU dimensions.
 * @ingroup libtiff_private_jpeg
 *
 * Calls the saved method and rounds width/height up by the directory
 * YCbCrSubsampling horizontal/vertical factors (default 2) times DCTSIZE,
 * for every photometric. Requires live writable slots and nonzero
 * representable rounding units. Does not set tags, validate recommendations
 * or provide separate status; parent effects apply, no ownership transfer.
 *
 * @param[in,out] tif Non-NULL live JPEG handle with compatible current
 * directory/state; serialise operations.
 * @param[in,out] tw Non-NULL caller-owned tile-width request/result.
 * @param[in,out] th Non-NULL caller-owned tile-height request/result.
 */
static void JPEGDefaultTileSize(TIFF *tif, uint32_t *tw, uint32_t *th)
{
    JPEGState *sp = JState(tif);
    TIFFDirectory *td = &tif->tif_dir;

    (*sp->otherSettings.deftparent)(tif, tw, th);
    *tw = TIFFroundup_32(*tw, td->td_ycbcrsubsampling[0] * DCTSIZE);
    *th = TIFFroundup_32(*th, td->td_ycbcrsubsampling[1] * DCTSIZE);
}

/*
 * The JPEG library initialized used to be done in TIFFInitJPEG(), but
 * now that we allow a TIFF file to be opened in update mode it is necessary
 * to have some way of deciding whether compression or decompression is
 * desired other than looking at tif->tif_mode.  We accomplish this by
 * examining {TILE/STRIP}BYTECOUNTS to see if there is a non-zero entry.
 * If so, we assume decompression is desired.
 *
 * This is tricky, because TIFFInitJPEG() is called while the directory is
 * being read, and generally speaking the BYTECOUNTS tag won't have been read
 * at that point.  So we try to defer jpeg library initialization till we
 * do have that tag ... basically any access that might require the compressor
 * or decompressor that occurs after the reading of the directory.
 *
 * In an ideal world compressors or decompressors would be setup
 * at the point where a single tile or strip was accessed (for read or write)
 * so that stuff like update of missing tiles, or replacement of tiles could
 * be done. However, we aren't trying to crack that nut just yet ...
 *
 * NFW, Feb 3rd, 2003.
 */

/**
 * @brief Create or switch the embedded dependency compressor/decompressor.
 * @ingroup libtiff_private_jpeg
 *
 * Reuses an already matching mode; otherwise attempts destroy (ignoring its
 * result), clears creation flag and creates the new mode. For compressor
 * memory limits, raises an existing positive limit toward
 * TIFF_JPEG_MAX_MEMORY_TO_USE unless JPEGMEM is set; does not impose a new
 * positive limit where dependency uses zero. Dependency allocation/error
 * routing can invalidate old views and leave partial state. No caller
 * ownership transfer.
 *
 * @param[in,out] tif Non-NULL live JPEG handle with compatible current
 * directory/state; serialise operations.
 * @param[in] decompress Nonzero selects decompression, zero compression.
 * @return One for ready matching/created mode, zero on trapped creation
 * failure.
 */
static int JPEGInitializeLibJPEG(TIFF *tif, int decompress)
{
    JPEGState *sp = JState(tif);

    if (sp->cinfo_initialized)
    {
        if (!decompress && sp->cinfo.comm.is_decompressor)
            TIFFjpeg_destroy(sp);
        else if (decompress && !sp->cinfo.comm.is_decompressor)
            TIFFjpeg_destroy(sp);
        else
            return 1;

        sp->cinfo_initialized = 0;
    }

    /*
     * Initialize libjpeg.
     */
    if (decompress)
    {
        if (!TIFFjpeg_create_decompress(sp))
            return (0);
    }
    else
    {
        if (!TIFFjpeg_create_compress(sp))
            return (0);
#ifndef TIFF_JPEG_MAX_MEMORY_TO_USE
#define TIFF_JPEG_MAX_MEMORY_TO_USE (10 * 1024 * 1024)
#endif
        /* libjpeg turbo 1.5.2 honours max_memory_to_use, but has no backing */
        /* store implementation, so better not set max_memory_to_use ourselves.
         */
        /* See https://github.com/libjpeg-turbo/libjpeg-turbo/issues/162 */
        if (sp->cinfo.c.mem->max_memory_to_use > 0)
        {
            /* This is to address bug related in ticket GDAL #1795. */
            if (getenv("JPEGMEM") == NULL)
            {
                /* Increase the max memory usable. This helps when creating
                 * files */
                /* with "big" tile, without using libjpeg temporary files. */
                /* For example a 512x512 tile with 3 bands */
                /* requires 1.5 MB which is above libjpeg 1MB default */
                if (sp->cinfo.c.mem->max_memory_to_use <
                    TIFF_JPEG_MAX_MEMORY_TO_USE)
                    sp->cinfo.c.mem->max_memory_to_use =
                        TIFF_JPEG_MAX_MEMORY_TO_USE;
            }
        }
    }

    sp->cinfo_initialized = TRUE;

    return 1;
}

/**
 * @brief Estimate JPEG expansion from precision and sampled layout.
 * @ingroup libtiff_private_jpeg
 *
 * Reads handle/state without allocation, I/O, diagnostics or ownership
 * transfer. Contiguous three-sample YCbCr reports a known estimate only for
 * 2x2 sampling; other layouts use precision-specific estimates. This is an
 * allocation heuristic, not a content/precision guarantee.
 *
 * @param[in] tif Non-NULL live JPEG handle; directory and codec state are
 * only read.
 * @return 512 or 768 for eight/twelve-bit 2x2 YCbCr; zero for other YCbCr
 * sampling; otherwise 256 or 384.
 */
static uint64_t JPEGGetMaxCompressionRatio(TIFF *tif)
{
    /* See README_for_libtiff_developpers.md for raw data used to estimate
     * the maximum compression rate. */

    const JPEGState *sp = JState(tif);
    if ((tif->tif_dir.td_photometric == PHOTOMETRIC_YCBCR) &&
        (tif->tif_dir.td_planarconfig == PLANARCONFIG_CONTIG) &&
        (tif->tif_dir.td_samplesperpixel == 3))
    {
        if (sp->h_sampling == 2 && sp->v_sampling == 2)
        {
            if (tif->tif_dir.td_bitspersample == 12)
                return 768;
            else
                return 512;
        }

        return 0; /* unknown */
    }

    if (tif->tif_dir.td_bitspersample == 12)
        return 384;
    else
        return 256;
}

/* Common to tif_jpeg.c and tif_jpeg_12.c */
/**
 * @brief Install common JPEG callbacks defaults and handle backlink.
 * @ingroup libtiff_private_jpeg
 *
 * Requires a fresh/zeroed ABI-appropriate state with parent hooks preserved
 * separately. Resets table pointer/length and codec defaults, installs
 * tag/setup/transfer/sizing methods, sets NOBITREV and clears
 * dependency-created flag. Does not release any old tables/dependency
 * objects or allocate/read/write pixels. Twelve-bit reinitialisation
 * restores preserved otherSettings afterward; no caller ownership transfer.
 *
 * @param[in,out] tif Non-NULL live JPEG handle with compatible current
 * directory/state; serialise operations.
 */
static void TIFFInitJPEGCommon(TIFF *tif)
{
    JPEGState *sp;

    sp = JState(tif);
    sp->tif = tif; /* back link */

    /* Default values for codec-specific fields */
    sp->otherSettings.jpegtables = NULL;
    sp->otherSettings.jpegtables_length = 0;
    sp->otherSettings.jpegquality = 75; /* Default IJG quality */
    sp->otherSettings.jpegcolormode = JPEGCOLORMODE_RAW;
    sp->otherSettings.jpegtablesmode =
        JPEGTABLESMODE_QUANT | JPEGTABLESMODE_HUFF;
    sp->otherSettings.ycbcrsampling_fetched = 0;

    tif->tif_tagmethods.vgetfield = JPEGVGetField; /* hook for codec tags */
    tif->tif_tagmethods.vsetfield = JPEGVSetField; /* hook for codec tags */
    tif->tif_tagmethods.printdir = JPEGPrintDir;   /* hook for codec tags */

    /*
     * Install codec methods.
     */
    tif->tif_fixuptags = JPEGFixupTags;
    tif->tif_setupdecode = JPEGSetupDecode;
    tif->tif_predecode = JPEGPreDecode;
    tif->tif_decoderow = JPEGDecode;
    tif->tif_decodestrip = JPEGDecode;
    tif->tif_decodetile = JPEGDecode;
    tif->tif_setupencode = JPEGSetupEncode;
    tif->tif_preencode = JPEGPreEncode;
    tif->tif_postencode = JPEGPostEncode;
    tif->tif_encoderow = JPEGEncode;
    tif->tif_encodestrip = JPEGEncode;
    tif->tif_encodetile = JPEGEncode;
    tif->tif_getmaxcompressionratio = JPEGGetMaxCompressionRatio;
    tif->tif_cleanup = JPEGCleanup;

    tif->tif_defstripsize = JPEGDefaultStripSize;
    tif->tif_deftilesize = JPEGDefaultTileSize;
    tif->tif_flags |= TIFF_NOBITREV; /* no bit reversal, please */
    sp->cinfo_initialized = FALSE;
}

int TIFFInitJPEG(TIFF *tif, int scheme)
{
    JPEGState *sp;

    (void)scheme;
    assert(scheme == COMPRESSION_JPEG);

    /*
     * Merge codec-specific tag information.
     */
    if (!_TIFFMergeFields(tif, jpegFields, TIFFArrayCount(jpegFields)))
    {
        TIFFErrorExtR(tif, "TIFFInitJPEG",
                      "Merging JPEG codec-specific tags failed");
        return 0;
    }

    /*
     * Allocate state block so tag methods have storage to record values.
     */
    tif->tif_data = (uint8_t *)_TIFFmallocExt(tif, sizeof(JPEGState));

    if (tif->tif_data == NULL)
    {
        TIFFErrorExtR(tif, "TIFFInitJPEG", "No space for JPEG state block");
        return 0;
    }
    _TIFFmemset(tif->tif_data, 0, sizeof(JPEGState));

    sp = JState(tif);
    /*
     * Override parent get/set field methods.
     */
    sp->otherSettings.vgetparent = tif->tif_tagmethods.vgetfield;
    sp->otherSettings.vsetparent = tif->tif_tagmethods.vsetfield;
    sp->otherSettings.printdir = tif->tif_tagmethods.printdir;

    sp->otherSettings.defsparent = tif->tif_defstripsize;
    sp->otherSettings.deftparent = tif->tif_deftilesize;

    TIFFInitJPEGCommon(tif);

    /*
    ** Create a JPEGTables field if no directory has yet been created.
    ** We do this just to ensure that sufficient space is reserved for
    ** the JPEGTables field.  It will be properly created the right
    ** size later.
    */
    if (tif->tif_diroff == 0)
    {
#define SIZE_OF_JPEGTABLES 2000
        /*
        The following line assumes incorrectly that all JPEG-in-TIFF files will
        have a JPEGTABLES tag generated and causes null-filled JPEGTABLES tags
        to be written when the JPEG data is placed with TIFFWriteRawStrip.  The
        field bit should be set, anyway, later when actual JPEGTABLES header is
        generated, so removing it here hopefully is harmless.
        TIFFSetFieldBit(tif, FIELD_JPEGTABLES);
        */
        sp->otherSettings.jpegtables_length = SIZE_OF_JPEGTABLES;
        sp->otherSettings.jpegtables =
            (void *)_TIFFmallocExt(tif, sp->otherSettings.jpegtables_length);
        if (sp->otherSettings.jpegtables)
        {
            _TIFFmemset(sp->otherSettings.jpegtables, 0, SIZE_OF_JPEGTABLES);
        }
        else
        {
            TIFFErrorExtR(tif, "TIFFInitJPEG",
                          "Failed to allocate memory for JPEG tables");
            return 0;
        }
#undef SIZE_OF_JPEGTABLES
    }
    return 1;
}
#endif /* JPEG_SUPPORT */

/**
 * @def SIZE_OF_JPEGTABLES
 * @brief Size of the zero-filled placeholder JPEGTables buffer.
 * @ingroup libtiff_private_jpeg
 *
 * TIFFInitJPEG() allocates this many zero bytes for JPEGTables when no
 * directory has been written yet (tif_diroff == 0), to reserve space
 * for the real tables written later. Undefined after use.
 */

/**
 * @def TIFF_JPEG_MAX_MEMORY_TO_USE
 * @brief Preferred libjpeg compressor memory limit.
 * @ingroup libtiff_private_jpeg
 *
 * Defaults to 10 MiB unless predefined. JPEGInitializeLibJPEG()
 * raises a compressor's positive max_memory_to_use to this value
 * when JPEGMEM is not set; a limit of zero is left unchanged.
 */

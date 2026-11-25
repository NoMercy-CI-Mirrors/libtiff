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
 * @file tiffiop.h
 * @brief Declare private library state, callbacks, and implementation helpers.
 */

#ifndef _TIFFIOP_
#define _TIFFIOP_
/*
 * ``Library-private'' definitions.
 */

#include "tif_config.h"

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#include <math.h>
#include <string.h>

#ifdef HAVE_ASSERT_H
#include <assert.h>
#else
/**
 * @brief Provide the no-op assertion fallback when assert.h is unavailable.
 * @ingroup libtiff_private_state
 *
 * Performs no runtime operation and does not report assertion failures.
 * Use side-effect-free macro arguments.
 *
 * @param[in] x Assertion expression; not evaluated by this fallback.
 * @return No tokens.
 */
#define assert(x)
#endif

#include "tif_hash_set.h"
#include "tiffio.h"

#include "tif_dir.h"

#include <limits.h>

#ifndef STRIP_SIZE_DEFAULT
/**
 * @brief Provide the fallback target strip size in bytes.
 * @ingroup libtiff_private_configuration
 */
#define STRIP_SIZE_DEFAULT 8192
#endif

#ifndef TIFF_MAX_DIR_COUNT
/**
 * @brief Provide the fallback directory-traversal limit.
 * @ingroup libtiff_private_configuration
 */
#define TIFF_MAX_DIR_COUNT 1048576
#endif

/**
 * @brief Mark an unknown or nonexistent directory index.
 * @ingroup libtiff_private_state
 */
#define TIFF_NON_EXISTENT_DIR_NUMBER UINT_MAX

/**
 * @brief Compare two null-terminated strings for equality.
 * @ingroup libtiff_private_state
 *
 * No ownership transfer or library diagnostics occur.
 * Use side-effect-free macro arguments.
 *
 * @param[in] a First non-NULL string.
 * @param[in] b Second non-NULL string.
 * @return Nonzero if strcmp() reports equality; zero otherwise.
 */
#define streq(a, b) (strcmp(a, b) == 0)
/**
 * @brief Compare up to n bytes of two strings for equality.
 * @ingroup libtiff_private_state
 *
 * Reads at most n bytes from either argument, stopping at a null byte.
 * No ownership transfer or library diagnostics occur.
 * Use side-effect-free macro arguments.
 *
 * @param[in] a First string or byte sequence readable through the
 * comparison.
 * @param[in] b Second string or byte sequence readable through the
 * comparison.
 * @param[in] n Maximum number of bytes to compare.
 * @return Nonzero if strncmp() reports equality; zero otherwise.
 */
#define strneq(a, b, n) (strncmp(a, b, n) == 0)

#ifndef TRUE
/**
 * @brief Represent the legacy internal true value.
 * @ingroup libtiff_private_state
 */
#define TRUE 1
/**
 * @brief Represent the legacy internal false value.
 * @ingroup libtiff_private_state
 */
#define FALSE 0
#endif

/**
 * @brief Link a named client-data association to a TIFF handle.
 * @ingroup libtiff_private_state
 *
 * TIFFSetClientInfo() allocates the node and copies its name, but retains the
 * payload pointer without taking ownership. TIFFCleanup() frees the name and
 * node; the client remains responsible for payload lifetime and release.
 */
typedef struct client_info
{
    /** @brief Next handle-owned association node, or NULL. */
    struct client_info *next;
    /** @brief Borrowed client payload; the library does not free it. */
    void *data;
    /** @brief Owned copy of the null-terminated association name. */
    char *name;
} TIFFClientInfoLink;

/*
 * Typedefs for ``method pointers'' used internally.
 * these are deprecated and provided only for backwards compatibility.
 */
/** @brief Name a legacy internal image byte.
 * @ingroup libtiff_private_state
 * @deprecated Use an explicit byte type in new internal code.
 */
typedef unsigned char tidataval_t;
/** @brief Name a legacy pointer to internal image bytes.
 * @ingroup libtiff_private_state
 * @deprecated Use an explicitly typed pointer in new internal code.
 */
typedef tidataval_t *tidata_t;

/**
 * @brief Run a codec close or cleanup operation.
 * @ingroup libtiff_private_image_transfers
 *
 * May release codec-owned state, restore method chains, and change the
 * handle; client resources remain governed by lifecycle ownership. No
 * status result is available; implementations must report failures through
 * diagnostics where applicable. Invoked synchronously through a borrowed
 * method; keep code/state live for calls, serialise handle access, and
 * avoid recursive dispatch through the same installed slot.
 *
 * @param[in,out] tif Non-NULL live handle with matching current geometry
 * and codec methods; serialise access externally.
 */
typedef void (*TIFFVoidMethod)(TIFF *tif);
/**
 * @brief Run a codec setup or completion operation.
 * @ingroup libtiff_private_image_transfers
 *
 * Used for fixuptags/setupdecode/setupencode/postencode. May allocate state or
 * flush encoded bytes according to its installed role; failure need not undo
 * changes. Invoked synchronously through a borrowed method; keep code/state
 * live for calls, serialise handle access, and avoid recursive dispatch
 * through the same installed slot.
 *
 * @param[in,out] tif Non-NULL live handle with matching current geometry
 * and codec methods; serialise access externally.
 * @return One on accepted completion, zero on failure; use Boolean results
 * rather than negative error codes.
 */
typedef int (*TIFFBoolMethod)(TIFF *tif);
/**
 * @brief Prepare a codec for one sample plane.
 * @ingroup libtiff_private_image_transfers
 *
 * Called after raw input/output state is selected. Borrows the handle;
 * plane-specific allocations/state remain codec-owned until its cleanup.
 * Invoked synchronously through a borrowed method; keep code/state live for
 * calls, serialise handle access, and avoid recursive dispatch through the
 * same installed slot.
 *
 * @param[in,out] tif Non-NULL live handle with matching current geometry
 * and codec methods; serialise access externally.
 * @param[in] sample Selected sample plane.
 * @return One on readiness, zero on failure; state can already change.
 */
typedef int (*TIFFPreMethod)(TIFF *tif, uint16_t sample);
/**
 * @brief Encode or decode a requested image-data extent.
 * @ingroup libtiff_private_image_transfers
 *
 * Direction is selected by the installed encoder/decoder slot. Decoders
 * write caller output; encoders read input that postprocessing/codec paths
 * may mutate. Implementations must respect the supplied nonnegative extent
 * and their format preconditions. No ownership transfer, capacity
 * discovery, or rollback is supplied by the pointer type. Invoked
 * synchronously through a borrowed method; keep code/state live for calls,
 * serialise handle access, and avoid recursive dispatch through the same
 * installed slot.
 *
 * @param[in,out] tif Non-NULL live handle with matching current geometry
 * and codec methods; serialise access externally.
 * @param[in,out] buf Borrowed readable/writable image buffer for the
 * installed method role.
 * @param[in] size Nonnegative valid byte extent.
 * @param[in] sample Selected sample plane.
 * @return One on success, zero on failure; negative values are not
 * uniformly rejected by all dispatchers.
 */
typedef int (*TIFFCodeMethod)(TIFF *tif, uint8_t *buf, tmsize_t size,
                              uint16_t sample);
/**
 * @brief Advance codec decoding or encoding by a row count.
 * @ingroup libtiff_private_image_transfers
 *
 * Updates codec/raw state without transferring ownership. Unsupported skips
 * must return zero; a failed skip need not preserve the starting state.
 * Invoked synchronously through a borrowed method; keep code/state live for
 * calls, serialise handle access, and avoid recursive dispatch through the
 * same installed slot.
 *
 * @param[in,out] tif Non-NULL live handle with matching current geometry
 * and codec methods; serialise access externally.
 * @param[in] nrows Number of rows to skip forward.
 * @return One on supported advancement, zero on failure/unsupported
 * operation.
 */
typedef int (*TIFFSeekMethod)(TIFF *tif, uint32_t nrows);
/**
 * @brief Transform image bytes in place after decoding or before encoding.
 * @ingroup libtiff_private_image_transfers
 *
 * Typically swaps sample byte order. Requires writable aligned storage and
 * format-specific whole-sample multiples; no independent failure/status
 * channel or ownership transfer. No dispatcher automatically restores
 * source bytes. Invoked synchronously through a borrowed method; keep
 * code/state live for calls, serialise handle access, and avoid recursive
 * dispatch through the same installed slot.
 *
 * @param[in,out] tif Non-NULL live handle with matching current geometry
 * and codec methods; serialise access externally.
 * @param[in,out] buf Borrowed writable image bytes.
 * @param[in] size Nonnegative byte extent satisfying method-specific
 * alignment/multiple rules.
 */
typedef void (*TIFFPostMethod)(TIFF *tif, uint8_t *buf, tmsize_t size);
/**
 * @brief Recommend rows per strip for the current geometry and codec.
 * @ingroup libtiff_private_image_layout
 *
 * Invoked synchronously by TIFFDefaultStripSize() through a borrowed
 * installed method. Return a recommendation without transferring ownership;
 * dispatch does not apply it to the directory or validate it. The
 * implementation may inspect/change codec state, call getters, allocate, or
 * report diagnostics. Preserve the established geometry unless documented
 * by the codec and keep callback code/context live while installed/in
 * flight. No status channel separate from the row count is provided.
 *
 * @param[in,out] tif Non-NULL live handle with consistent current geometry;
 * queries may use getters/callbacks. Serialise access externally.
 * @param[in] request Requested row count, typically zero for a heuristic;
 * default method uses signed 32-bit interpretation.
 * @return Recommended row count under the method's codec-specific rules;
 * dispatch imposes no universal error sentinel.
 */
typedef uint32_t (*TIFFStripMethod)(TIFF *tif, uint32_t request);
/**
 * @brief Adjust tile width and height for the current codec.
 * @ingroup libtiff_private_image_layout
 *
 * Invoked synchronously by TIFFDefaultTileSize() with borrowed writable
 * slots. The installed method must leave a documented recommendation,
 * normally valid multiples of sixteen, without transferring ownership.
 * Dispatch does not apply tags or validate results. Codec state,
 * allocation, getter, and diagnostic effects depend on the method; there is
 * no return status or automatic recovery. Keep code/context live while
 * installed/in flight and serialise handle access.
 *
 * @param[in,out] tif Non-NULL live handle with consistent current geometry;
 * queries may use getters/callbacks. Serialise access externally.
 * @param[in,out] tw Non-NULL caller-owned width request/result in pixels.
 * @param[in,out] th Non-NULL caller-owned height request/result in rows.
 */
typedef void (*TIFFTileMethod)(TIFF *tif, uint32_t *tw, uint32_t *th);
/**
 * @brief Return the codec allocation-sanity compression-ratio heuristic.
 * @ingroup libtiff_private_image_transfers
 *
 * Used for selected large-allocation checks, not a guaranteed codec
 * expansion/capacity bound. No returned ownership; method code and any
 * state must remain live while installed/in flight. Invoked synchronously
 * through a borrowed method; keep code/state live for calls, serialise
 * handle access, and avoid recursive dispatch through the same installed
 * slot.
 *
 * @param[in,out] tif Non-NULL live handle with matching current geometry
 * and codec methods; serialise access externally.
 * @return Heuristic ratio; zero disables this bound.
 */
typedef uint64_t (*TIFFGetMaxCompressionRatioMethod)(TIFF *tif);

/**
 * @brief Pair a directory file offset with its main-IFD index.
 * @ingroup libtiff_private_state
 *
 * Used by the handle's directory offset/number maps. This record contains
 * inline values, not separately owned resources.
 */
struct TIFFOffsetAndDirNumber
{
    /** @brief Directory byte offset from the beginning of the file. */
    uint64_t offset;
    /** @brief Zero-based main-directory index associated with offset. */
    tdir_t dirNumber;
};
/** @brief Name the TIFFOffsetAndDirNumber record type.
 * @ingroup libtiff_private_state
 */
typedef struct TIFFOffsetAndDirNumber TIFFOffsetAndDirNumber;

/**
 * @brief Store either TIFF header layout in the library's in-memory state.
 * @ingroup libtiff_private_foundations
 *
 * The common version field selects the classic or big member. After decoding,
 * multibyte numeric fields are in host byte order; the magic field still
 * identifies the file's byte order. Members contain values, not owned pointers.
 */
typedef union
{
    /** @brief Shared byte-order and version fields. */
    TIFFHeaderCommon common;
    /** @brief Header when version is TIFF_VERSION_CLASSIC. */
    TIFFHeaderClassic classic;
    /** @brief Header when version is TIFF_VERSION_BIG. */
    TIFFHeaderBig big;
} TIFFHeaderUnion;

/**
 * @brief Hold private state for one open TIFF library instance.
 * @ingroup libtiff_private_state
 *
 * Applications see only the opaque TIFF typedef. This layout is private and
 * must not be used as an external ABI. Mutable state belongs to the handle;
 * simultaneous operations on the same handle require external coordination.
 *
 * TIFFCleanup() releases handle-managed directory data, field tables,
 * client-info nodes, owned raw buffers, and the mapped-file view while
 * TIFF_MAPPED is still set, then invalidates the handle. TIFFClose() also
 * invokes the client close callback. Client payloads and callback contexts
 * remain client-owned. Codec state and callbacks follow the codec module's
 * lifecycle; ownership cannot be inferred from pointer type alone.
 *
 * Pointers into raw buffers, field tables, and directory storage may be
 * invalidated by operations that replace that storage, not only by close.
 */
struct tiff
{
    /**
     * @brief Current filename pointer; may refer to inline or client-supplied
     * storage.
     */
    char *tif_name;
    /**
     * @brief File-descriptor metadata; client I/O still uses tif_clientdata.
     */
    int tif_fd;
    /** @brief Current access mode using the O_* constants. */
    int tif_mode;
    /**
     * @brief Bitmask combining the TIFF_* handle-state flags declared below.
     */
    uint32_t tif_flags;
/**
 * @brief Mask the selected data bit-fill-order bits in tif_flags.
 * @ingroup libtiff_private_state
 */
#define TIFF_FILLORDER 0x00003U
/**
 * @brief Mark a file header that needs writing.
 * @ingroup libtiff_private_state
 *
 * Currently unused; reserved bit value retained for compatibility.
 */
#define TIFF_DIRTYHEADER 0x00004U
/**
 * @brief Mark a current directory that needs writing.
 * @ingroup libtiff_private_state
 */
#define TIFF_DIRTYDIRECT 0x00008U
/**
 * @brief Mark completed data-buffer setup.
 * @ingroup libtiff_private_state
 */
#define TIFF_BUFFERSETUP 0x00010U
/**
 * @brief Mark completed codec setup.
 * @ingroup libtiff_private_state
 */
#define TIFF_CODERSETUP 0x00020U
/**
 * @brief Record that scanline or image-data writing has begun.
 * @ingroup libtiff_private_state
 */
#define TIFF_BEENWRITING 0x00040U
/**
 * @brief Request swapping multibyte file values to or from host order.
 * @ingroup libtiff_private_state
 */
#define TIFF_SWAB 0x00080U
/**
 * @brief Suppress automatic bit reversal.
 * @ingroup libtiff_private_state
 */
#define TIFF_NOBITREV 0x00100U
/**
 * @brief Mark a raw buffer owned and released by the handle.
 * @ingroup libtiff_private_state
 */
#define TIFF_MYBUFFER 0x00200U
/**
 * @brief Mark tile-based rather than strip-based image storage.
 * @ingroup libtiff_private_state
 */
#define TIFF_ISTILED 0x00400U
/**
 * @brief Request or record memory-mapped file access.
 * @ingroup libtiff_private_state
 *
 * Set during opening for read-only modes (unless 'm' is given) before mapping
 * is attempted, and cleared if mapping fails, so it can be set while tif_base
 * is still NULL. After a successful open it is set only while tif_base and
 * tif_size describe a view. TIFFInitJBIG() clears it without unmapping.
 */
#define TIFF_MAPPED 0x00800U
/**
 * @brief Request a pending codec postencode call.
 * @ingroup libtiff_private_state
 */
#define TIFF_POSTENCODE 0x01000U
/**
 * @brief Mark writing within a SubIFD sequence.
 * @ingroup libtiff_private_state
 */
#define TIFF_INSUBIFD 0x02000U
/**
 * @brief Mark codec output that has already been upsampled.
 * @ingroup libtiff_private_state
 */
#define TIFF_UPSAMPLED 0x04000U
/**
 * @brief Enable internal chopping of a large uncompressed strip.
 * @ingroup libtiff_private_state
 */
#define TIFF_STRIPCHOP 0x08000U
/**
 * @brief Request header-only opening without reading the first IFD.
 * @ingroup libtiff_private_state
 */
#define TIFF_HEADERONLY 0x10000U
/**
 * @brief Suppress the normal raw-data read path.
 * @ingroup libtiff_private_state
 */
#define TIFF_NOREADRAW 0x20000U
/**
 * @brief Mark writing of a custom IFD.
 * @ingroup libtiff_private_state
 *
 * Currently unused; reserved bit value retained for compatibility.
 */
#define TIFF_INCUSTOMIFD 0x40000U
/**
 * @brief Mark the BigTIFF file format.
 * @ingroup libtiff_private_state
 */
#define TIFF_BIGTIFF 0x80000U
/**
 * @brief Interpret the raw buffer and its byte count as pending output.
 * @ingroup libtiff_private_state
 */
#define TIFF_BUF4WRITE 0x100000U
/**
 * @brief Mark strip/tile offset or byte-count arrays that need writing.
 * @ingroup libtiff_private_state
 */
#define TIFF_DIRTYSTRIP 0x200000U
/**
 * @brief Select per-sample arrays for supported sample-valued fields.
 * @ingroup libtiff_private_state
 */
#define TIFF_PERSAMPLE 0x400000U
/**
 * @brief Mark raw data that borrows mapped-file storage.
 * @ingroup libtiff_private_state
 */
#define TIFF_BUFFERMMAP 0x800000U
/**
 * @brief Defer loading strip/tile offset and byte-count arrays.
 * @ingroup libtiff_private_state
 */
#define TIFF_DEFERSTRILELOAD 0x1000000U
/**
 * @brief Record a switch from on-demand to full strip/tile-array loading.
 * @ingroup libtiff_private_state
 *
 * Set by _TIFFFillStriles() when it discards the entries loaded on demand for
 * an 'O'-mode directory so that the full arrays are loaded; cleared by
 * TIFFReadDirectory().
 */
#define TIFF_LAZYSTRILELOAD_DONE 0x2000000U
/**
 * @brief Mark strip arrays replaced by the strip-chopping operation.
 * @ingroup libtiff_private_state
 */
#define TIFF_CHOPPEDUPARRAYS 0x4000000U
/**
 * @brief Record a lazy strip/tile loading request from the O open flag.
 * @ingroup libtiff_private_state
 */
#define TIFF_LAZYSTRILELOAD_ASKED 0x8000000U

    /**
     * @brief Current directory byte offset, or zero for an unlinked in-memory
     * IFD.
     */
    uint64_t tif_diroff;
    /** @brief Next directory byte offset from the current chain link. */
    uint64_t tif_nextdiroff;
    /** @brief Byte offset of the last directory written so far. */
    uint64_t tif_lastdiroff;
    /** @brief Handle-managed directory-offset-to-index lookup map. */
    TIFFHashSet *tif_map_dir_offset_to_number;
    /** @brief Handle-managed directory-index-to-offset lookup map. */
    TIFFHashSet *tif_map_dir_number_to_offset;
    /**
     * @brief Nonzero to force absolute rather than relative directory stepping.
     */
    int tif_setdirectory_force_absolute;
    /** @brief Inline representation of the current image directory. */
    TIFFDirectory tif_dir;
    /**
     * @brief Unused legacy member; custom directories are read into tif_dir.
     */
    TIFFDirectory tif_customdir;
    /** @brief Inline decoded classic TIFF or BigTIFF file header. */
    TIFFHeaderUnion tif_header;
    /** @brief File-header length in bytes for the selected format. */
    uint16_t tif_header_size;

    /** @brief Track the current or last-written directory index.
     *
     * There are IFDs in the file and an "active" IFD in memory,
     * from which fields are "set" and "get".
     * tif_curdir is set to:
     *   a) TIFF_NON_EXISTENT_DIR_NUMBER if there is no IFD in the file
     *      or the state is unknown,
     *      or the last read (i.e. TIFFFetchDirectory()) failed,
     *      or a custom directory was written.
     *   b) IFD index of last IFD written in the file. In this case the
     *      active IFD is a new (empty) one and tif_diroff is zero.
     *      If writing fails, tif_curdir is not changed.
     *   c) IFD index of IFD read from file into memory (=active IFD),
     *      even if IFD is corrupt and TIFFReadDirectory() returns 0.
     *      Then tif_diroff contains the offset of the IFD in the file.
     *   d) IFD index 0, whenever a custom directory or an unchained SubIFD
     *      was read. */
    tdir_t tif_curdir; /* current directory (index) */
    /** @brief Cache the number of main IFDs in the file.
     *
     * - TIFF_NON_EXISTENT_DIR_NUMBER means 'dont know number of IFDs'.
     * - 0 means 'empty file opened for writing, but no IFD written yet' */
    tdir_t tif_curdircount;
    /** @brief Tracked current file byte offset for read/write operations. */
    uint64_t tif_curoff;
    /**
     * @brief Last byte-offset boundary permitted for rewriting strip data in
     * place.
     */
    uint64_t tif_lastvalidoff;
    /** @brief Byte offset used when placing directory data in the file. */
    uint64_t tif_dataoff;
    /* SubIFD support */
    /** @brief Number of remaining SubIFD links to write. */
    uint16_t tif_nsubifd;
    /** @brief File byte offset at which the next SubIFD link is patched. */
    uint64_t tif_subifdoff;
    /* compression scheme hooks */
    /**
     * @brief Nonzero unless the selected scheme is a known but unconfigured
     * codec; not a setup result.
     */
    int tif_decodestatus;
    /** @brief Codec callback for directory-dependent tag corrections. */
    TIFFBoolMethod tif_fixuptags;
    /** @brief Codec setup callback run before predecode. */
    TIFFBoolMethod tif_setupdecode;
    /** @brief Codec callback preparing a row, strip, or tile decode. */
    TIFFPreMethod tif_predecode;
    /** @brief Codec setup callback run before preencode. */
    TIFFBoolMethod tif_setupencode;
    /**
     * @brief Nonzero unless the selected scheme is a known but unconfigured
     * codec; never read.
     */
    int tif_encodestatus;
    /** @brief Codec callback preparing a row, strip, or tile encode. */
    TIFFPreMethod tif_preencode;
    /** @brief Codec callback completing buffered encoding. */
    TIFFBoolMethod tif_postencode;
    /** @brief Codec callback decoding a scanline. */
    TIFFCodeMethod tif_decoderow;
    /** @brief Codec callback encoding a scanline. */
    TIFFCodeMethod tif_encoderow;
    /** @brief Codec callback decoding strip data. */
    TIFFCodeMethod tif_decodestrip;
    /** @brief Codec callback encoding strip data. */
    TIFFCodeMethod tif_encodestrip;
    /** @brief Codec callback decoding tile data. */
    TIFFCodeMethod tif_decodetile;
    /** @brief Codec callback encoding tile data. */
    TIFFCodeMethod tif_encodetile;
    /**
     * @brief Codec close callback; distinct from the client I/O close callback.
     */
    TIFFVoidMethod tif_close;
    /** @brief Codec callback advancing within a strip. */
    TIFFSeekMethod tif_seek;
    /** @brief Codec callback releasing codec-private state. */
    TIFFVoidMethod tif_cleanup;
    /** @brief Codec callback computing or constraining a default strip size. */
    TIFFStripMethod tif_defstripsize;
    /**
     * @brief Codec callback computing or constraining default tile dimensions.
     */
    TIFFTileMethod tif_deftilesize;
    /* returns maximum compression ratio for current compression method */
    /** @brief Codec callback returning its maximum compression ratio. */
    TIFFGetMaxCompressionRatioMethod tif_getmaxcompressionratio;
    /**
     * @brief Codec-private storage; allocation and cleanup follow the selected
     * codec.
     */
    uint8_t *tif_data;
    /* input/output buffering */
    /**
     * @brief Raw buffer; ownership/mapping follow TIFF_MYBUFFER and
     * TIFF_BUFFERMMAP.
     */
    uint8_t *tif_rawdata;
    /** @brief Raw-buffer capacity in bytes. */
    tmsize_t tif_rawdatasize;
    /** @brief Byte offset of the raw-buffer window within the strip. */
    tmsize_t tif_rawdataoff;
    /** @brief Number of bytes loaded into the raw-buffer window. */
    tmsize_t tif_rawdataloaded;
    /** @brief Borrowed current-position pointer into the raw buffer. */
    uint8_t *tif_rawcp;
    /**
     * @brief Raw-buffer byte count interpreted according to read/write state.
     */
    tmsize_t tif_rawcc;
    /* memory-mapped file support */
    /**
     * @brief Mapped-file base provided by tif_mapproc; released through
     * tif_unmapproc by TIFFCleanup() only while TIFF_MAPPED is set, so clearing
     * that flag (as TIFFInitJBIG() does) leaves the view unreleased.
     */
    uint8_t *tif_base;
    /** @brief Mapped-file region length in bytes. */
    tmsize_t tif_size;
    /** @brief Client callback providing a mapped-file view. */
    TIFFMapFileProc tif_mapproc;
    /** @brief Client callback releasing a mapped-file view. */
    TIFFUnmapFileProc tif_unmapproc;
    /* input/output callback methods */
    /** @brief Client I/O context passed unchanged to the I/O callbacks. */
    thandle_t tif_clientdata;
    /** @brief Client read callback. */
    TIFFReadWriteProc tif_readproc;
    /** @brief Client write callback. */
    TIFFReadWriteProc tif_writeproc;
    /** @brief Client seek callback. */
    TIFFSeekProc tif_seekproc;
    /** @brief Client I/O close callback. */
    TIFFCloseProc tif_closeproc;
    /** @brief Client file-size callback. */
    TIFFSizeProc tif_sizeproc;
    /* post-decoding support */
    /**
     * @brief Callback transforming decoded data, for example by byte swapping.
     */
    TIFFPostMethod tif_postdecode;
    /* tag support */
    /**
     * @brief Owned sorted array of field pointers; descriptors have mixed
     * ownership.
     */
    TIFFField **tif_fields;
    /** @brief Number of entries in tif_fields. */
    size_t tif_nfields;
    /**
     * @brief Borrowed cached field descriptor; valid only while that descriptor
     * lives.
     */
    const TIFFField *tif_foundfield;
    /** @brief Inline table of directory field and print callbacks. */
    TIFFTagMethods tif_tagmethods;
    /**
     * @brief Head of the owned named-association list; payloads remain
     * borrowed.
     */
    TIFFClientInfoLink *tif_clientinfo;
    /* Backward compatibility stuff. We need these two fields for
     * setting up an old tag extension scheme. */
    /** @brief Owned array of legacy field-registration batches. */
    TIFFFieldArray *tif_fieldscompat;
    /** @brief Number of entries in tif_fieldscompat. */
    size_t tif_nfieldscompat;
    /* Error handler support */
    /**
     * @brief Per-handle error callback, or NULL for the default handling path.
     */
    TIFFErrorHandlerExtR tif_errorhandler;
    /** @brief Borrowed context passed to tif_errorhandler. */
    void *tif_errorhandler_user_data;
    /**
     * @brief Per-handle warning callback, or NULL for the default handling
     * path.
     */
    TIFFErrorHandlerExtR tif_warnhandler;
    /** @brief Borrowed context passed to tif_warnhandler. */
    void *tif_warnhandler_user_data;
    /** @brief Single-allocation byte limit; zero means unlimited. */
    tmsize_t tif_max_single_mem_alloc;
    /**
     * @brief Cumulative tracked-allocation byte limit; zero means unlimited.
     */
    tmsize_t tif_max_cumulated_mem_alloc;
    /** @brief Current tracked allocation total in bytes. */
    tmsize_t tif_cur_cumulated_mem_alloc;
    /** @brief Nonzero to emit warnings for unknown tags. */
    int tif_warn_about_unknown_tags;
};

/**
 * @brief Hold allocation limits and diagnostics used when opening a TIFF
 * handle.
 * @ingroup libtiff_private_state
 *
 * The open-options API allocates and frees this object. Opening copies its
 * settings into the handle; callback contexts are copied as pointers, not
 * cloned or retained. They must remain valid while the resulting handle can
 * invoke the callbacks, even after the options object has been freed.
 */
struct TIFFOpenOptions
{
    /** @brief Optional per-handle error callback, or NULL. */
    TIFFErrorHandlerExtR errorhandler;
    /** @brief Borrowed error-callback context, or NULL. */
    void *errorhandler_user_data;
    /** @brief Optional per-handle warning callback, or NULL. */
    TIFFErrorHandlerExtR warnhandler;
    /** @brief Borrowed warning-callback context, or NULL. */
    void *warnhandler_user_data;
    /** @brief Maximum individual allocation in bytes; zero means unlimited. */
    tmsize_t max_single_mem_alloc;
    /**
     * @brief Maximum tracked allocation total in bytes; zero means unlimited.
     */
    tmsize_t max_cumulated_mem_alloc;
    /** @brief Nonzero to warn about unknown tags. */
    int warn_about_unknown_tags;
};

/**
 * @brief Test whether a tag identifier is outside the on-disk 16-bit range.
 * @ingroup libtiff_private_state
 *
 * The macro does not fully parenthesise its argument. Use a simple value or
 * explicit parentheses around a compound expression. No diagnostics occur.
 * Use side-effect-free macro arguments.
 *
 * @param[in] t Unsigned tag identifier; pass a simple value expression.
 * @return Nonzero for identifiers above 65535; zero otherwise.
 */
#define isPseudoTag(t) (t > 0xffff) /* is tag value normal or pseudo */

/**
 * @brief Test whether a handle uses tiled storage.
 * @ingroup libtiff_private_state
 *
 * Reads handle flags without modifying state or transferring ownership.
 * Use side-effect-free macro arguments.
 *
 * @param[in] tif Non-NULL TIFF handle.
 * @return Nonzero if TIFF_ISTILED is set; zero otherwise.
 */
#define isTiled(tif) (((tif)->tif_flags & TIFF_ISTILED) != 0)
/**
 * @brief Test whether TIFF_MAPPED is set on a handle.
 * @ingroup libtiff_private_state
 *
 * See TIFF_MAPPED: during opening the flag may be set before a view exists.
 * Reads handle flags without modifying state or transferring ownership.
 * Use side-effect-free macro arguments.
 *
 * @param[in] tif Non-NULL TIFF handle.
 * @return Nonzero if TIFF_MAPPED is set; zero otherwise.
 */
#define isMapped(tif) (((tif)->tif_flags & TIFF_MAPPED) != 0)
/**
 * @brief Test whether selected bit-fill-order bits are present in a handle.
 * @ingroup libtiff_private_state
 *
 * Uses a bitwise intersection rather than comparing the masked value for
 * equality. Does not modify state or transfer ownership.
 * Use side-effect-free macro arguments.
 *
 * @param[in] tif Non-NULL TIFF handle.
 * @param[in] o Fill-order bit value to test.
 * @return Nonzero if any tested bits are set; zero otherwise.
 */
#define isFillOrder(tif, o) (((tif)->tif_flags & (o)) != 0)
/**
 * @brief Test whether the codec reports upsampled output.
 * @ingroup libtiff_private_state
 *
 * Reads handle flags without modifying state or transferring ownership.
 * Use side-effect-free macro arguments.
 *
 * @param[in] tif Non-NULL TIFF handle.
 * @return Nonzero if TIFF_UPSAMPLED is set; zero otherwise.
 */
#define isUpSampled(tif) (((tif)->tif_flags & TIFF_UPSAMPLED) != 0)
/**
 * @brief Invoke the handle's client read callback.
 * @ingroup libtiff_private_state
 *
 * The callback may advance file position and modify client state. This wrapper
 * adds no diagnostics, retry, or ownership transfer.
 * Use side-effect-free macro arguments.
 *
 * @param[in,out] tif Non-NULL handle with an installed read callback.
 * @param[out] buf Caller-owned destination with at least size writable
 * bytes.
 * @param[in] size Requested nonnegative byte count.
 * @return The transferred byte count, or the callback's negative error result,
 * unchanged; a short count is a partial read.
 */
#define TIFFReadFile(tif, buf, size)                                           \
    ((*(tif)->tif_readproc)((tif)->tif_clientdata, (buf), (size)))
/**
 * @brief Invoke the handle's client write callback.
 * @ingroup libtiff_private_state
 *
 * The callback may write data, advance file position, and modify client state.
 * This wrapper adds no diagnostics, retry, or ownership transfer.
 * Use side-effect-free macro arguments.
 *
 * @param[in,out] tif Non-NULL handle with an installed write callback.
 * @param[in] buf Caller-owned source containing at least size readable
 * bytes.
 * @param[in] size Requested nonnegative byte count.
 * @return The transferred byte count, or the callback's negative error result,
 * unchanged; a short count is a partial write.
 */
#define TIFFWriteFile(tif, buf, size)                                          \
    ((*(tif)->tif_writeproc)((tif)->tif_clientdata, (buf), (size)))
/**
 * @brief Invoke the handle's client seek callback.
 * @ingroup libtiff_private_state
 *
 * May change file position and client state. The wrapper adds no error handling
 * or ownership transfer.
 * Use side-effect-free macro arguments.
 *
 * @param[in,out] tif Non-NULL handle with an installed seek callback.
 * @param[in] off Byte offset interpreted according to whence.
 * @param[in] whence SEEK_SET, SEEK_CUR, or SEEK_END as supported by the
 * callback.
 * @return The callback's resulting byte position, unchanged; the failure
 * value is callback-defined (the POSIX backend returns (toff_t)-1, the Win32
 * backend 0).
 */
#define TIFFSeekFile(tif, off, whence)                                         \
    ((*(tif)->tif_seekproc)((tif)->tif_clientdata, (off), (whence)))
/**
 * @brief Invoke the handle's client close callback.
 * @ingroup libtiff_private_state
 *
 * May release the underlying client I/O resource. This does not free the TIFF
 * handle and adds no diagnostics or error handling.
 * Use side-effect-free macro arguments.
 *
 * @param[in,out] tif Non-NULL handle with an installed close callback.
 * @return The client close callback's status, unchanged; the conventional
 * file-close result is zero on success and -1 on failure.
 */
#define TIFFCloseFile(tif) ((*(tif)->tif_closeproc)((tif)->tif_clientdata))
/**
 * @brief Invoke the handle's client size callback.
 * @ingroup libtiff_private_state
 *
 * Client-state and file-position effects follow the callback implementation.
 * The wrapper adds no diagnostics or ownership transfer.
 * Use side-effect-free macro arguments.
 *
 * @param[in,out] tif Non-NULL handle with an installed size callback.
 * @return The size callback's result, unchanged.
 */
#define TIFFGetFileSize(tif) ((*(tif)->tif_sizeproc)((tif)->tif_clientdata))
/**
 * @brief Invoke the handle's client mapping callback.
 * @ingroup libtiff_private_state
 *
 * On success the returned view has the lifetime and release contract of the
 * client mapping callback. On failure output validity is defined by that
 * callback. The wrapper performs no independent allocation or error handling.
 * Use side-effect-free macro arguments.
 *
 * @param[in,out] tif Non-NULL handle with an installed mapping callback.
 * @param[out] paddr Non-NULL output pointer for the mapped-file base.
 * @param[out] psize Non-NULL output pointer for the mapped length in
 * bytes.
 * @return Nonzero on callback success, zero on callback failure, unchanged.
 */
#define TIFFMapFileContents(tif, paddr, psize)                                 \
    ((*(tif)->tif_mapproc)((tif)->tif_clientdata, (paddr), (psize)))
/**
 * @brief Invoke the handle's client unmapping callback.
 * @ingroup libtiff_private_state
 *
 * Invalidates the mapped view according to the callback contract. Adds no
 * independent diagnostics or error handling.
 * Use side-effect-free macro arguments.
 *
 * @param[in,out] tif Non-NULL handle with an installed unmapping callback.
 * @param[in] addr Mapped-file base, or NULL during early failed-open cleanup.
 * @param[in] size Mapped length in bytes, or zero during no-view cleanup.
 * @return No value; expands to the unmapping callback invocation.
 */
#define TIFFUnmapFileContents(tif, addr, size)                                 \
    ((*(tif)->tif_unmapproc)((tif)->tif_clientdata, (addr), (size)))

/*
 * Default Read/Seek/Write definitions.
 */
#ifndef ReadOK
/**
 * @brief Test whether one client read transfers the entire requested byte
 * count.
 * @ingroup libtiff_private_state
 *
 * Partial transfers and errors both produce zero; partial output remains in
 * buf according to the callback. No retry or extra diagnostic is performed.
 * Use side-effect-free macro arguments.
 *
 * @param[in,out] tif Non-NULL TIFF handle.
 * @param[out] buf Caller-owned destination with at least size writable
 * bytes.
 * @param[in] size Requested nonnegative byte count representable by
 * tmsize_t.
 * @return Nonzero only if the callback returns exactly size bytes.
 */
#define ReadOK(tif, buf, size)                                                 \
    (TIFFReadFile((tif), (buf), (size)) == (tmsize_t)(size))
#endif
#ifndef SeekOK
/**
 * @brief Test whether an absolute seek reaches the requested file position.
 * @ingroup libtiff_private_state
 *
 * Rejects offsets above UINT64_MAX / 2 without calling the client. Otherwise
 * performs one SEEK_SET callback and tests that its result equals off. The
 * helper emits no additional diagnostics and does not transfer ownership.
 * Use side-effect-free macro arguments.
 *
 * @param[in,out] tif Non-NULL TIFF handle.
 * @param[in] off Absolute byte offset from the start of the file.
 * @return Nonzero if an allowed offset is reached exactly; zero otherwise.
 */
#define SeekOK(tif, off) _TIFFSeekOK(tif, off)
#endif
#ifndef WriteOK
/**
 * @brief Test whether one client write transfers the entire requested byte
 * count.
 * @ingroup libtiff_private_state
 *
 * Partial transfers and errors both produce zero; partial writes are not rolled
 * back. No retry or extra diagnostic is performed.
 * Use side-effect-free macro arguments.
 *
 * @param[in,out] tif Non-NULL TIFF handle.
 * @param[in] buf Caller-owned source with at least size readable bytes.
 * @param[in] size Requested nonnegative byte count representable by
 * tmsize_t.
 * @return Nonzero only if the callback returns exactly size bytes.
 */
#define WriteOK(tif, buf, size)                                                \
    (TIFFWriteFile((tif), (buf), (size)) == (tmsize_t)(size))
#endif

/* NB: the uint32_t casts are to silence certain ANSI-C compilers */
/**
 * @brief Compute a guarded 32-bit ceiling quotient.
 * @ingroup libtiff_private_state
 *
 * Uses the strict guard x < UINT32_MAX - (y - 1), so the equality boundary
 * also returns zero. A zero result is not a unique error sentinel. The guard
 * does not parenthesise x and subtracts one from y before conversion to
 * uint32_t, so pass simple uint32_t-typed expressions. No diagnostic or
 * ownership transfer occurs.
 * Use side-effect-free macro arguments.
 *
 * @param[in] x Nonnegative value representable by uint32_t.
 * @param[in] y Positive divisor representable by uint32_t.
 * @return The ceiling quotient, or zero when the conservative addition
 * guard rejects it.
 */
#define TIFFhowmany_32(x, y)                                                   \
    (((uint32_t)x < (0xffffffff - (uint32_t)(y - 1)))                          \
         ? ((((uint32_t)(x)) + (((uint32_t)(y)) - 1)) / ((uint32_t)(y)))       \
         : 0U)
/* Variant of TIFFhowmany_32() that doesn't return 0 if x close to MAXUINT. */
/* Caution: TIFFhowmany_32_maxuint_compat(x,y)*y might overflow */
/**
 * @brief Compute a 32-bit ceiling quotient without adding x and y first.
 * @ingroup libtiff_private_state
 *
 * Handles x near UINT32_MAX, but multiplying the result by y can still
 * overflow. No diagnostic or ownership transfer occurs.
 * Use side-effect-free macro arguments.
 *
 * @param[in] x Nonnegative value representable by uint32_t.
 * @param[in] y Positive divisor representable by uint32_t.
 * @return The ceiling of x divided by y.
 */
#define TIFFhowmany_32_maxuint_compat(x, y)                                    \
    (((uint32_t)(x) / (uint32_t)(y)) +                                         \
     ((((uint32_t)(x) % (uint32_t)(y)) != 0) ? 1 : 0))
/**
 * @brief Compute the byte count needed to hold a 32-bit bit count.
 * @ingroup libtiff_private_state
 *
 * No allocation, library-state changes, or error reporting occurs.
 * Use side-effect-free macro arguments.
 *
 * @param[in] x Nonnegative bit count representable by uint32_t.
 * @return The ceiling of x divided by eight, in bytes.
 */
#define TIFFhowmany8_32(x)                                                     \
    (((x) & 0x07) ? ((uint32_t)(x) >> 3) + 1 : (uint32_t)(x) >> 3)
/**
 * @brief Round a 32-bit value up to a multiple using the guarded quotient.
 * @ingroup libtiff_private_state
 *
 * Inherits the conservative guard and ambiguous zero result of
 * TIFFhowmany_32(). No allocation or diagnostic occurs.
 * Use side-effect-free macro arguments.
 *
 * @param[in] x Nonnegative value representable by uint32_t.
 * @param[in] y Positive alignment representable by uint32_t.
 * @return The rounded value, or zero if TIFFhowmany_32() rejects the
 * addition.
 */
#define TIFFroundup_32(x, y) ((uint32_t)(TIFFhowmany_32(x, y) * (uint32_t)(y)))
/**
 * @brief Compute a 64-bit ceiling quotient using an unchecked addition.
 * @ingroup libtiff_private_state
 *
 * The caller must ensure x + y - 1 fits uint64_t. Overflow wraps without
 * diagnostics. No allocation or ownership transfer occurs.
 * Use side-effect-free macro arguments.
 *
 * @param[in] x Nonnegative value representable by uint64_t.
 * @param[in] y Positive divisor representable by uint64_t.
 * @return The ceiling quotient when the addition is representable.
 */
#define TIFFhowmany_64(x, y)                                                   \
    ((((uint64_t)(x)) + (((uint64_t)(y)) - 1)) / ((uint64_t)(y)))
/**
 * @brief Compute the byte count needed to hold a 64-bit bit count.
 * @ingroup libtiff_private_state
 *
 * No allocation, library-state changes, or error reporting occurs.
 * Use side-effect-free macro arguments.
 *
 * @param[in] x Nonnegative bit count representable by uint64_t.
 * @return The ceiling of x divided by eight, in bytes.
 */
#define TIFFhowmany8_64(x)                                                     \
    (((x) & 0x07) ? ((uint64_t)(x) >> 3) + 1 : (uint64_t)(x) >> 3)
/**
 * @brief Round a 64-bit value up to a multiple without overflow checks.
 * @ingroup libtiff_private_state
 *
 * The caller must ensure x + y - 1 and the final product fit uint64_t.
 * Overflow wraps without diagnostics. No allocation occurs.
 * Use side-effect-free macro arguments.
 *
 * @param[in] x Nonnegative value representable by uint64_t.
 * @param[in] y Positive alignment representable by uint64_t.
 * @return The rounded value when both addition and multiplication are
 * representable.
 */
#define TIFFroundup_64(x, y) ((uint64_t)(TIFFhowmany_64(x, y) * (uint64_t)(y)))

/* Safe multiply which returns zero if there is an *unsigned* integer overflow.
 * This macro is not safe for *signed* integer types */
/**
 * @brief Multiply in a caller-selected unsigned type with a reverse-division
 * check.
 * @ingroup libtiff_private_state
 *
 * Use an unsigned type of at least unsigned-int rank. Signed types and narrow
 * unsigned types subject to signed integer promotion are not covered by this
 * check. Zero can be a valid product and is not a unique error sentinel.
 * The macro emits no diagnostic and transfers no ownership.
 * Use side-effect-free macro arguments.
 *
 * @param[in] t Unsigned integer type whose multiplication is performed
 * unsigned.
 * @param[in] v First operand, representable in t.
 * @param[in] m Second operand, representable in t.
 * @return The product in t, or zero if m is zero or the reverse-division
 * check fails.
 */
#define TIFFSafeMultiply(t, v, m)                                              \
    ((((t)(m) != (t)0) && (((t)((((t)(v)) * ((t)(m))) / ((t)(m)))) == (t)(v))) \
         ? (t)(((t)(v)) * ((t)(m)))                                            \
         : (t)0)

/*
 * Overflow-safe computation of uint32_t parameters: (index % count) *
 * dimension. Sets `result` to the product, or 0 when either operand is 0.
 * Evaluates to 0 on success, nonzero on overflow.
 */
/**
 * @brief Compute (index modulo count) times dimension with a checked product.
 * @ingroup libtiff_private_state
 *
 * If dimension is zero or index % count is zero, sets result to zero and
 * returns zero without calling _TIFFMultiply32(); count must be nonzero unless
 * dimension is zero. index and count may be evaluated twice. On overflow
 * _TIFFMultiply32() emits an error and result becomes zero. Does not allocate
 * or transfer storage ownership.
 * Use side-effect-free macro arguments.
 *
 * @param[in,out] tif Handle used by _TIFFMultiply32() for error reporting.
 * @param[in] module Non-NULL diagnostic module name.
 * @param[in] index Unsigned 32-bit index.
 * @param[in] count Positive unsigned divisor unless dimension is zero.
 * @param[in] dimension Unsigned 32-bit multiplier.
 * @param[out] result Assignable uint32_t lvalue receiving the product or
 * zero.
 * @return Zero on success; nonzero if the checked nonzero product
 * overflows.
 */
#define TIFFSafeModMul32(tif, module, index, count, dimension, result)         \
    ((dimension) == 0 || ((index) % (count) == 0)                              \
         ? ((result) = 0, 0)                                                   \
         : (((result) = _TIFFMultiply32((tif), (index) % (count), (dimension), \
                                        (module))) == 0))

/**
 * @brief Select the first operand if it is greater, otherwise the second.
 * @ingroup libtiff_private_state
 *
 * The selected operand is evaluated again after comparison. No library
 * diagnostic or ownership transfer occurs.
 * Use side-effect-free macro arguments.
 *
 * @param[in] A First comparable value.
 * @param[in] B Second comparable value.
 * @return A when A > B; B otherwise, using the usual C conversions.
 */
#define TIFFmax(A, B) ((A) > (B) ? (A) : (B))
/**
 * @brief Select the first operand if it is smaller, otherwise the second.
 * @ingroup libtiff_private_state
 *
 * The selected operand is evaluated again after comparison. No library
 * diagnostic or ownership transfer occurs.
 * Use side-effect-free macro arguments.
 *
 * @param[in] A First comparable value.
 * @param[in] B Second comparable value.
 * @return A when A < B; B otherwise, using the usual C conversions.
 */
#define TIFFmin(A, B) ((A) < (B) ? (A) : (B))

/**
 * @brief Compute the element count of an actual array.
 * @ingroup libtiff_private_state
 *
 * Does not validate that the argument is an array. Normal sizeof rules apply,
 * including runtime evaluation for variably modified types. No allocation
 * occurs. Use side-effect-free macro arguments.
 *
 * @param[in] a Array object, not a pointer to its first element.
 * @return The array size divided by its element size, as size_t.
 */
#define TIFFArrayCount(a) (sizeof(a) / sizeof((a)[0]))

/* Float/double equality macros that suppress -Wfloat-equal warnings */
/**
 * @brief Test whether a float difference is not greater than zero in magnitude.
 * @ingroup libtiff_private_state
 *
 * This is not a general IEEE equality test: a NaN difference also yields
 * nonzero. Normal floating-point exception behaviour applies; no library
 * diagnostic or ownership transfer occurs.
 * Use side-effect-free macro arguments.
 *
 * @param[in] x First floating-point value.
 * @param[in] y Second floating-point value.
 * @return Nonzero if fabsf(x - y) is not greater than zero; zero
 * otherwise.
 */
#define TIFF_FLOAT_EQ(x, y) (!(fabsf((x) - (y)) > 0.0f))
/**
 * @brief Test whether a double difference is not greater than zero in
 * magnitude.
 * @ingroup libtiff_private_state
 *
 * This is not a general IEEE equality test: a NaN difference also yields
 * nonzero. Normal floating-point exception behaviour applies; no library
 * diagnostic or ownership transfer occurs.
 * Use side-effect-free macro arguments.
 *
 * @param[in] x First floating-point value.
 * @param[in] y Second floating-point value.
 * @return Nonzero if fabs(x - y) is not greater than zero; zero otherwise.
 */
#define TIFF_DOUBLE_EQ(x, y) (!(fabs((x) - (y)) > 0.0))

/*
  Support for large files.

  Windows read/write APIs support only 'unsigned int' rather than 'size_t'.
  Windows off_t is only 32-bit, even in 64-bit builds.
*/
#if defined(HAVE_FSEEKO)
/*
  Use fseeko() and ftello() if they are available since they use
  'off_t' rather than 'long'.  It is wrong to use fseeko() and
  ftello() only on systems with special LFS support since some systems
  (e.g. FreeBSD) support a 64-bit off_t by default.

  For MinGW, __MSVCRT_VERSION__ must be at least 0x800 to expose these
  interfaces. The MinGW compiler must support the requested version.  MinGW
  does not distribute the CRT (it is supplied by Microsoft) so the correct CRT
  must be available on the target computer in order for the program to run.
*/
#if defined(HAVE_FSEEKO)
/**
 * @brief Map stream seeking to the available large-file stdio interface.
 * @ingroup libtiff_private_state
 *
 * Forwards arguments and results to the selected C runtime interface. File
 * position, errno, output validity after failure, and other effects follow
 * that interface. This wrapper adds no library diagnostics or ownership
 * transfer. Each argument is evaluated once.
 *
 * @param[in,out] stream Valid FILE stream.
 * @param[in] offset Byte offset interpreted by whence.
 * @param[in] whence Standard seek origin.
 * @return Zero on success; nonzero on failure, with errno set according to
 * the selected C runtime interface.
 */
#define fseek(stream, offset, whence) fseeko(stream, offset, whence)
/**
 * @brief Map stream-position queries to the large-file stdio interface.
 * @ingroup libtiff_private_state
 *
 * Forwards arguments and results to the selected C runtime interface. File
 * position, errno, output validity after failure, and other effects follow
 * that interface. This wrapper adds no library diagnostics or ownership
 * transfer. Each argument is evaluated once.
 *
 * @param[in,out] stream Valid FILE stream.
 * @return The current stream byte position, or -1 on failure; errno follows
 * the selected C runtime interface.
 */
#define ftell(stream) ftello(stream)
#endif
#endif
#if defined(_WIN32) &&                                                         \
    !(defined(__MSVCRT_VERSION__) && __MSVCRT_VERSION__ < 0x800)
/** @brief Represent a native Windows I/O byte count.
 * @ingroup libtiff_private_state
 */
typedef unsigned int TIFFIOSize_t;
/**
 * @brief Seek a file descriptor through the platform large-file interface.
 * @ingroup libtiff_private_state
 *
 * Forwards arguments and results to the selected C runtime interface. File
 * position, errno, output validity after failure, and other effects follow
 * that interface. This wrapper adds no library diagnostics or ownership
 * transfer. Each argument is evaluated once.
 *
 * @param[in] fildes Valid file descriptor.
 * @param[in] offset Offset in bytes interpreted by whence.
 * @param[in] whence Standard seek origin.
 * @return The resulting byte position, or -1 on failure; errno follows the
 * selected C runtime interface.
 */
#define _TIFF_lseek_f(fildes, offset, whence)                                  \
    _lseeki64(fildes, /* __int64 */ offset, whence)
/* #define _TIFF_tell_f(fildes) /\* __int64 *\/ _telli64(fildes) */
/**
 * @brief Seek a stream through the platform large-file interface.
 * @ingroup libtiff_private_state
 *
 * Forwards arguments and results to the selected C runtime interface. File
 * position, errno, output validity after failure, and other effects follow
 * that interface. This wrapper adds no library diagnostics or ownership
 * transfer. Each argument is evaluated once.
 *
 * @param[in,out] stream Valid FILE stream.
 * @param[in] offset Offset in bytes interpreted by whence.
 * @param[in] whence Standard seek origin.
 * @return Zero on success; nonzero on failure, with errno set according to
 * the selected C runtime interface.
 */
#define _TIFF_fseek_f(stream, offset, whence)                                  \
    _fseeki64(stream, /* __int64 */ offset, whence)
/**
 * @brief Query file status through the platform large-file interface.
 * @ingroup libtiff_private_state
 *
 * Forwards arguments and results to the selected C runtime interface. File
 * position, errno, output validity after failure, and other effects follow
 * that interface. This wrapper adds no library diagnostics or ownership
 * transfer. Each argument is evaluated once.
 *
 * @param[in] fildes Valid file descriptor.
 * @param[out] stat_buff Non-NULL pointer to the platform status structure.
 * @return Zero on success, or -1 on failure with errno set by the selected
 * C runtime interface; output validity on failure follows that interface.
 */
#define _TIFF_fstat_f(fildes, stat_buff)                                       \
    _fstati64(fildes, /* struct _stati64 */ stat_buff)
/* #define _TIFF_ftell_f(stream) /\* __int64 *\/ _ftelli64(stream) */
/* #define _TIFF_stat_f(path,stat_buff) _stati64(path,/\* struct _stati64 *\/
 * stat_buff) */
/**
 * @brief Select the platform file-status structure type.
 * @ingroup libtiff_private_state
 */
#define _TIFF_stat_s struct _stati64
/**
 * @brief Select the platform large-file offset type.
 * @ingroup libtiff_private_state
 */
#define _TIFF_off_t __int64
#else
/** @brief Represent a native POSIX I/O byte count.
 * @ingroup libtiff_private_state
 */
typedef size_t TIFFIOSize_t;
/**
 * @brief Seek a file descriptor through the platform large-file interface.
 * @ingroup libtiff_private_state
 *
 * Forwards arguments and results to the selected C runtime interface. File
 * position, errno, output validity after failure, and other effects follow
 * that interface. This wrapper adds no library diagnostics or ownership
 * transfer. Each argument is evaluated once.
 *
 * @param[in] fildes Valid file descriptor.
 * @param[in] offset Offset in bytes interpreted by whence.
 * @param[in] whence Standard seek origin.
 * @return The resulting byte position, or -1 on failure; errno follows the
 * selected C runtime interface.
 */
#define _TIFF_lseek_f(fildes, offset, whence) lseek(fildes, offset, whence)
/* #define _TIFF_tell_f(fildes) (_TIFF_lseek_f(fildes,0,SEEK_CUR)) */
/**
 * @brief Seek a stream through the platform large-file interface.
 * @ingroup libtiff_private_state
 *
 * Forwards arguments and results to the selected C runtime interface. File
 * position, errno, output validity after failure, and other effects follow
 * that interface. This wrapper adds no library diagnostics or ownership
 * transfer. Each argument is evaluated once.
 *
 * @param[in,out] stream Valid FILE stream.
 * @param[in] offset Offset in bytes interpreted by whence.
 * @param[in] whence Standard seek origin.
 * @return Zero on success; nonzero on failure, with errno set according to
 * the selected C runtime interface.
 */
#define _TIFF_fseek_f(stream, offset, whence) fseek(stream, offset, whence)
/**
 * @brief Query file status through the platform large-file interface.
 * @ingroup libtiff_private_state
 *
 * Forwards arguments and results to the selected C runtime interface. File
 * position, errno, output validity after failure, and other effects follow
 * that interface. This wrapper adds no library diagnostics or ownership
 * transfer. Each argument is evaluated once.
 *
 * @param[in] fildes Valid file descriptor.
 * @param[out] stat_buff Non-NULL pointer to the platform status structure.
 * @return Zero on success, or -1 on failure with errno set by the selected
 * C runtime interface; output validity on failure follows that interface.
 */
#define _TIFF_fstat_f(fildes, stat_buff) fstat(fildes, stat_buff)
/* #define _TIFF_ftell_f(stream) ftell(stream) */
/* #define _TIFF_stat_f(path,stat_buff) stat(path,stat_buff) */
/**
 * @brief Select the platform file-status structure type.
 * @ingroup libtiff_private_state
 */
#define _TIFF_stat_s struct stat
/**
 * @brief Select the platform large-file offset type.
 * @ingroup libtiff_private_state
 */
#define _TIFF_off_t off_t
#endif

#if defined(__has_attribute) && defined(__clang__)
#if __has_attribute(no_sanitize)
/**
 * @brief Suppress unsigned-overflow instrumentation on supported Clang
 * functions.
 * @ingroup libtiff_private_state
 */
#define TIFF_NOSANITIZE_UNSIGNED_INT_OVERFLOW                                  \
    __attribute__((no_sanitize("unsigned-integer-overflow")))
#else
/**
 * @brief Suppress unsigned-overflow instrumentation on supported Clang
 * functions.
 * @ingroup libtiff_private_state
 */
#define TIFF_NOSANITIZE_UNSIGNED_INT_OVERFLOW
#endif
#else
/**
 * @brief Suppress unsigned-overflow instrumentation on supported Clang
 * functions.
 * @ingroup libtiff_private_state
 */
#define TIFF_NOSANITIZE_UNSIGNED_INT_OVERFLOW
#endif

#if defined(__cplusplus)
extern "C"
{
#endif
    /**
     * @brief Decline mapping when the client supplies no mapping
     * implementation.
     * @ingroup libtiff_private_lifecycle
     *
     * Allocates nothing, performs no I/O, and emits no diagnostic. The
     * unchanged outputs must not be used as a mapped view.
     *
     * @param[in] fd Unused client context.
     * @param[out] pbase Unused view-base output; left unchanged.
     * @param[out] psize Unused size output; left unchanged.
     * @return Zero, indicating no mapping is available.
     */
    extern int _tiffDummyMapProc(thandle_t fd, void **pbase, toff_t *psize);
    /**
     * @brief Provide a no-op unmapper when the client omits one.
     * @ingroup libtiff_private_lifecycle
     *
     * Does not release storage, perform I/O, or emit diagnostics. Returns no
     * status; pairing this with a real mapper is safe only if the view needs
     * no explicit release.
     *
     * @param[in] fd Unused client context.
     * @param[in] base Unused view base.
     * @param[in] size Unused byte length.
     */
    extern void _tiffDummyUnmapProc(thandle_t fd, void *base, toff_t size);
    /**
     * @brief Translate the leading open-mode characters into OS access flags.
     * @ingroup libtiff_private_lifecycle
     *
     * Inspects only the first character and an immediately following + after
     * r; library modifiers are processed later. An invalid first character
     * emits _TIFFErrorEarly() and returns -1. Does not open, close,
     * allocate, or take ownership of a resource. Callback contexts remain
     * borrowed.
     *
     * @param[in] opts Optional borrowed settings for early error reporting.
     * @param[in] clientdata Client context passed to legacy extended early-
     * error handling.
     * @param[in] mode Non-NULL null-terminated mode string.
     * @param[in] module Borrowed diagnostic module name.
     * @return O_RDONLY for r, O_RDWR for r+ immediately following r, or
     * O_RDWR | O_CREAT with O_TRUNC added for w; a selects O_RDWR | O_CREAT.
     * Returns -1 for any other first character.
     */
    extern int _TIFFgetMode(TIFFOpenOptions *opts, thandle_t clientdata,
                            const char *mode, const char *module);
    /**
     * @brief Report unsupported codec encoding.
     * @ingroup libtiff_private_codecs
     *
     * No pixel buffer access, allocation, I/O, or ownership transfer.
     * Dispatches handle diagnostics; registry lookup supplies the name when
     * available.
     *
     * @param[in] tif Non-NULL live handle; only td_compression and tif_name
     * are read, for the diagnostic. Not modified.
     * @param[in] pp Unused compatibility argument; no buffer access occurs.
     * @param[in] cc Unused compatibility argument; does not select a working
     * codec operation.
     * @param[in] s Unused compatibility argument; does not select a working
     * codec operation.
     * @return Minus one unconditionally.
     */
    extern int _TIFFNoRowEncode(TIFF *tif, uint8_t *pp, tmsize_t cc,
                                uint16_t s);
    /**
     * @brief Report unsupported codec encoding.
     * @ingroup libtiff_private_codecs
     *
     * No pixel buffer access, allocation, I/O, or ownership transfer.
     * Dispatches handle diagnostics; registry lookup supplies the name when
     * available.
     *
     * @param[in] tif Non-NULL live handle; only td_compression and tif_name
     * are read, for the diagnostic. Not modified.
     * @param[in] pp Unused compatibility argument; no buffer access occurs.
     * @param[in] cc Unused compatibility argument; does not select a working
     * codec operation.
     * @param[in] s Unused compatibility argument; does not select a working
     * codec operation.
     * @return Minus one unconditionally.
     */
    extern int _TIFFNoStripEncode(TIFF *tif, uint8_t *pp, tmsize_t cc,
                                  uint16_t s);
    /**
     * @brief Report unsupported codec encoding.
     * @ingroup libtiff_private_codecs
     *
     * No pixel buffer access, allocation, I/O, or ownership transfer.
     * Dispatches handle diagnostics; registry lookup supplies the name when
     * available.
     *
     * @param[in] tif Non-NULL live handle; only td_compression and tif_name
     * are read, for the diagnostic. Not modified.
     * @param[in] pp Unused compatibility argument; no buffer access occurs.
     * @param[in] cc Unused compatibility argument; does not select a working
     * codec operation.
     * @param[in] s Unused compatibility argument; does not select a working
     * codec operation.
     * @return Minus one unconditionally.
     */
    extern int _TIFFNoTileEncode(TIFF *tif, uint8_t *pp, tmsize_t cc,
                                 uint16_t s);
    /**
     * @brief Report unsupported codec decoding.
     * @ingroup libtiff_private_codecs
     *
     * No pixel buffer access, allocation, I/O, or ownership transfer.
     * Dispatches handle diagnostics; registry lookup supplies the name when
     * available.
     *
     * @param[in] tif Non-NULL live handle; only td_compression and tif_name
     * are read, for the diagnostic. Not modified.
     * @param[in] pp Unused compatibility argument; no buffer access occurs.
     * @param[in] cc Unused compatibility argument; does not select a working
     * codec operation.
     * @param[in] s Unused compatibility argument; does not select a working
     * codec operation.
     * @return Zero unconditionally.
     */
    extern int _TIFFNoRowDecode(TIFF *tif, uint8_t *pp, tmsize_t cc,
                                uint16_t s);
    /**
     * @brief Report unsupported codec decoding.
     * @ingroup libtiff_private_codecs
     *
     * No pixel buffer access, allocation, I/O, or ownership transfer.
     * Dispatches handle diagnostics; registry lookup supplies the name when
     * available.
     *
     * @param[in] tif Non-NULL live handle; only td_compression and tif_name
     * are read, for the diagnostic. Not modified.
     * @param[in] pp Unused compatibility argument; no buffer access occurs.
     * @param[in] cc Unused compatibility argument; does not select a working
     * codec operation.
     * @param[in] s Unused compatibility argument; does not select a working
     * codec operation.
     * @return Zero unconditionally.
     */
    extern int _TIFFNoStripDecode(TIFF *tif, uint8_t *pp, tmsize_t cc,
                                  uint16_t s);
    /**
     * @brief Report unsupported codec decoding.
     * @ingroup libtiff_private_codecs
     *
     * No pixel buffer access, allocation, I/O, or ownership transfer.
     * Dispatches handle diagnostics; registry lookup supplies the name when
     * available.
     *
     * @param[in] tif Non-NULL live handle; only td_compression and tif_name
     * are read, for the diagnostic. Not modified.
     * @param[in] pp Unused compatibility argument; no buffer access occurs.
     * @param[in] cc Unused compatibility argument; does not select a working
     * codec operation.
     * @param[in] s Unused compatibility argument; does not select a working
     * codec operation.
     * @return Zero unconditionally.
     */
    extern int _TIFFNoTileDecode(TIFF *tif, uint8_t *pp, tmsize_t cc,
                                 uint16_t s);
    /**
     * @brief Leave decoded bytes unchanged.
     * @ingroup libtiff_private_image_transfers
     *
     * No-op: all arguments are ignored and no allocation, diagnostics, state,
     * ownership, or byte changes occur.
     *
     * @param[in] tif Unused context, may be NULL.
     * @param[in] buf Ignored; not dereferenced.
     * @param[in] cc Ignored.
     */
    extern void _TIFFNoPostDecode(TIFF *tif, uint8_t *buf, tmsize_t cc);
    /**
     * @brief Accept a no-op codec preparation.
     * @ingroup libtiff_private_codecs
     *
     * No pixel buffer access, allocation, I/O, or ownership transfer. Ignores
     * all arguments without diagnostics.
     *
     * @param[in] tif Unused handle.
     * @param[in] s Unused compatibility argument; does not select a working
     * codec operation.
     * @return One unconditionally.
     */
    extern int _TIFFNoPreCode(TIFF *tif, uint16_t s);
    /**
     * @brief Report unsupported codec seeking.
     * @ingroup libtiff_private_codecs
     *
     * No pixel buffer access, allocation, I/O, or ownership transfer. Reports
     * that the compression algorithm does not support random access through
     * the handle's error handler.
     *
     * @param[in] tif Non-NULL live handle used for the diagnostic (tif_name);
     * not modified.
     * @param[in] off Unused compatibility argument; does not select a working
     * codec operation.
     * @return Zero unconditionally.
     */
    extern int _TIFFNoSeek(TIFF *tif, uint32_t off);
    /**
     * @brief Swap each 2-byte sample in place.
     * @ingroup libtiff_private_image_transfers
     *
     * Requires nonnegative cc divisible by 2, and alignment suitable for the
     * typed swap primitive. Debug builds assert the multiple; release builds do
     * not validate the extent. Borrows/modifies bytes without allocation,
     * ownership transfer, I/O, or diagnostics; tif is unused. It is an
     * involution on valid input, not general sample-format conversion.
     *
     * @param[in] tif Unused context, may be NULL.
     * @param[in,out] buf Writable storage of cc bytes with alignment suitable
     * for uint16_t.
     * @param[in] cc Nonnegative byte count, a multiple of 2; any remainder is
     * left unswapped in release builds.
     */
    extern void _TIFFSwab16BitData(TIFF *tif, uint8_t *buf, tmsize_t cc);
    /**
     * @brief Swap each 3-byte sample in place.
     * @ingroup libtiff_private_image_transfers
     *
     * Requires nonnegative cc divisible by 3, and byte alignment. Debug builds
     * assert the multiple; release builds do not validate the extent.
     * Borrows/modifies bytes without allocation, ownership transfer, I/O, or
     * diagnostics; tif is unused. It is an involution on valid input, not
     * general sample-format conversion.
     *
     * @param[in] tif Unused context, may be NULL.
     * @param[in,out] buf Writable storage of cc bytes with any alignment.
     * @param[in] cc Nonnegative byte count, a multiple of 3; any remainder is
     * left unswapped in release builds.
     */
    extern void _TIFFSwab24BitData(TIFF *tif, uint8_t *buf, tmsize_t cc);
    /**
     * @brief Swap each 4-byte sample in place.
     * @ingroup libtiff_private_image_transfers
     *
     * Requires nonnegative cc divisible by 4, and alignment suitable for the
     * typed swap primitive. Debug builds assert the multiple; release builds do
     * not validate the extent. Borrows/modifies bytes without allocation,
     * ownership transfer, I/O, or diagnostics; tif is unused. It is an
     * involution on valid input, not general sample-format conversion.
     *
     * @param[in] tif Unused context, may be NULL.
     * @param[in,out] buf Writable storage of cc bytes with alignment suitable
     * for uint32_t.
     * @param[in] cc Nonnegative byte count, a multiple of 4; any remainder is
     * left unswapped in release builds.
     */
    extern void _TIFFSwab32BitData(TIFF *tif, uint8_t *buf, tmsize_t cc);
    /**
     * @brief Swap each 8-byte sample in place.
     * @ingroup libtiff_private_image_transfers
     *
     * Requires nonnegative cc divisible by 8, and alignment suitable for the
     * typed swap primitive. Debug builds assert the multiple; release builds do
     * not validate the extent. Borrows/modifies bytes without allocation,
     * ownership transfer, I/O, or diagnostics; tif is unused. It is an
     * involution on valid input, not general sample-format conversion.
     *
     * @param[in] tif Unused context, may be NULL.
     * @param[in,out] buf Writable storage of cc bytes with alignment suitable
     * for double.
     * @param[in] cc Nonnegative byte count, a multiple of 8; any remainder is
     * left unswapped in release builds.
     */
    extern void _TIFFSwab64BitData(TIFF *tif, uint8_t *buf, tmsize_t cc);
    /**
     * @brief Append buffered encoded bytes without invoking postencode
     * recursively.
     * @ingroup libtiff_private_image_transfers
     *
     * When rawcc is positive and BUF4WRITE set, may reverse raw-buffer bits in
     * place and append to current strip/tile. Resets rawcc/rawcp even after
     * append failure, so failed flush loses pending-buffer retry state. Does
     * not write the directory, close/free the handle, or finish codec
     * postencoding. No output ownership transfers; diagnostics/file effects
     * follow append. No rollback.
     *
     * @param[in,out] tif Non-NULL live handle with matching current geometry
     * and codec methods; serialise access externally.
     * @return One for successful append or no applicable buffered bytes, zero
     * on detected append failure; buffer contents/cursor may already change.
     */
    extern int TIFFFlushData1(TIFF *tif);
    /**
     * @brief Install the image schema, default values, and core tag methods.
     * @ingroup libtiff_private_directory_navigation
     *
     * Requires already released directory allocations: resets the directory
     * structure without freeing its old pointer members. Replaces field
     * schemas, frees compatibility descriptor batches, restores core get/set
     * methods and postdecode handling, invokes the global extender, and
     * attempts COMPRESSION_NONE setup. Clears dirty-directory and tiled flags
     * afterwards. Can allocate and invalidate descriptors; extender/codec calls
     * can alter state and report diagnostics. Schema/setup and compression-set
     * failures are not propagated; this is initialisation, not a checked
     * transaction.
     *
     * @param[in,out] tif Non-NULL live handle; caller must serialise access.
     * @return One unconditionally, even if nested setup fails; no reliable
     * success status.
     */
    extern int TIFFDefaultDirectory(TIFF *tif);
    /**
     * @brief Install fallback codec callbacks and reset raw-access flags.
     * @ingroup libtiff_private_codecs
     *
     * Assigns no-op setup/close/cleanup, unsupported encode/decode/seek
     * methods, default sizing, and unknown ratio. Sets tif_decodestatus and
     * tif_encodestatus to TRUE and clears NOBITREV/NOREADRAW.
     * Does not free old state, flush data, allocate, read/write bytes, or
     * report status. Caller must clean up previous codec first.
     *
     * @param[in,out] tif Non-NULL live handle whose codec methods and flags
     * are overwritten; serialise access externally.
     */
    extern void _TIFFSetDefaultCompressionState(TIFF *tif);
    /**
     * @brief Patch an existing field in the current on-file IFD.
     * @ingroup libtiff_private_ifd_writing
     *
     * Requires a writable, non-mapped handle with a nonzero current IFD offset
     * and an existing entry (including a deferred placeholder). Searches
     * entries, allocates a copy, range-checks supported 64-to-32/16 integer/IFD
     * conversions, swaps the copy, and overwrites matching payload storage or
     * appends/replaces it and patches the entry. Data remains borrowed and
     * unchanged. Moves I/O position and can update cached deferred-strile
     * type/count metadata before write success; does not reconcile general
     * in-memory tag values. No atomicity/rollback or fsync guarantee. Entry,
     * allocation, range, and I/O failures report diagnostics, but some seeks
     * are unchecked and the unsupported-conversion path leaks its temporary
     * buffer. Caller must provide a valid positive representable element extent
     * and a supported integer/IFD or same-type representation; byte-width
     * copying/swapping alone is not general rational conversion.
     *
     * @warning The entry scan is not bounded by the IFD entry count (the loop
     * never decrements it). If the tag is absent it reads beyond the IFD and
     * either fails with a read error or matches unrelated bytes, which it then
     * patches. Callers must ensure the tag is present.
     *
     * @param[in,out] tif Non-NULL live handle with matching schema/state;
     * serialise access externally.
     * @param[in] tag On-disk 16-bit field identifier already present in the
     * IFD.
     * @param[in] in_datatype Type of input elements; supported narrowing is
     * LONG8/SLONG8/IFD8 to matching shorter integer/IFD types.
     * @param[in] count Positive element count with representable
     * allocation/file entry size.
     * @param[in] data Readable borrowed input array of count elements; no alias
     * with mutable handle bookkeeping.
     * @return One after payload/entry patch completion, zero on a detected
     * failure; file and cached bookkeeping may already be changed.
     */
    extern int _TIFFRewriteField(TIFF *tif, uint16_t tag,
                                 TIFFDataType in_datatype, tmsize_t count,
                                 void *data);
    /**
     * @brief Reset methods and install the selected compression implementation.
     * @ingroup libtiff_private_codecs
     *
     * Looks up the uint16_t scheme, resets default callbacks/flags, sets ratio
     * one for COMPRESSION_NONE, and invokes the selected init if present.
     * Unknown schemes return success with fallback methods so metadata can
     * still be inspected. Requires previous codec cleanup by the caller; this
     * function does not release prior tif_data. Initialiser
     * allocation/diagnostic/state effects can survive failure; no pixel I/O is
     * performed directly. The process-global registry is unsynchronised;
     * serialise queries and modifications externally. No registry operation
     * manages active codec state or unloads callback code.
     *
     * @param[in,out] tif Non-NULL live handle whose codec state can be changed;
     * caller serialises handle and registry access.
     * @param[in] scheme Requested compression identifier; lookup narrows to
     * uint16_t.
     * @return Initialiser status for a known descriptor, otherwise one; not
     * proof that pixel I/O is supported.
     */
    extern int TIFFSetCompressionScheme(TIFF *tif, int scheme);
    /**
     * @brief Retain an obsolete declaration with no library implementation.
     * @ingroup libtiff_private_codecs
     *
     * This spelling has no definition in this source tree and must not be
     * called. The implemented private reset helper is
     * _TIFFSetDefaultCompressionState(). No error, ownership, lifetime or
     * side-effect contract exists for this unimplemented declaration.
     *
     * @param[in,out] tif Historical handle argument; no valid invocation is
     * available in this library.
     * @return No defined result: this declaration has no implementation.
     * @deprecated Use _TIFFSetDefaultCompressionState() for the implemented
     * private reset operation.
     */
    extern int TIFFSetDefaultCompressionState(TIFF *tif);
    /**
     * @brief Choose a default strip height near STRIP_SIZE_DEFAULT bytes.
     * @ingroup libtiff_private_image_layout
     *
     * Interprets s through int32_t: zero or high-bit unsigned requests trigger
     * the heuristic; positive signed-range requests pass through. Divides
     * STRIP_SIZE_DEFAULT by TIFFScanlineSize64(), using one byte if that query
     * returned zero, then clamps rows to [1,UINT32_MAX]. A diagnostic from
     * scanline sizing does not prevent a recommendation. Does not set tags or
     * allocate output; the scanline query may use getters/callbacks. No
     * ownership transfer.
     *
     * @param[in,out] tif Non-NULL live handle with consistent current geometry;
     * queries may use getters/callbacks. Serialise access externally.
     * @param[in] s Requested rows; interpreted as a signed 32-bit value for
     * heuristic selection.
     * @return Input request when positive in signed interpretation, otherwise a
     * bounded positive heuristic row count.
     */
    extern uint32_t _TIFFDefaultStripSize(TIFF *tif, uint32_t s);
    /**
     * @brief Supply default tile dimensions and round them to multiples of
     * sixteen.
     * @ingroup libtiff_private_image_layout
     *
     * Reads each uint32_t request through its signed 32-bit representation.
     * Nonpositive values, including high-bit unsigned requests, become 256;
     * positive requests are rounded upward to a multiple of sixteen. Does not
     * inspect tif, set tags, allocate, perform I/O, issue diagnostics, or
     * transfer ownership. Both result slots remain caller-owned.
     *
     * @param[in] tif Unused compatibility context; may be NULL for this default
     * implementation.
     * @param[in,out] tw Non-NULL writable width request/result in pixels.
     * @param[in,out] th Non-NULL writable height request/result in rows.
     */
    extern void _TIFFDefaultTileSize(TIFF *tif, uint32_t *tw, uint32_t *th);

    /**
     * @brief Replace an owned byte array with a copied array.
     * @ingroup libtiff_private_memory
     *
     * Frees the old destination before validating the new size or allocating
     * its replacement. A NULL source, zero/rejected byte product, or
     * allocation failure therefore leaves the destination NULL; the old data
     * is lost. This is not a failure-preserving realloc operation. The
     * source pointer is borrowed only during the copy and must not alias the
     * allocation freed first.
     *
     * Uses the untracked family through a NULL handle. Release the replacement
     * with _TIFFfree(). Returns no status, and no diagnostic is emitted on any
     * failure. No file I/O occurs.
     *
     * @param[in,out] vpp Non-NULL pointer to an owned compatible array or
     * NULL array pointer; replaced or cleared.
     * @param[in] vp Readable source for n elements, or NULL to clear; must
     * not overlap the old destination allocation.
     * @param[in] n Element count in bytes.
     */
    extern void _TIFFsetByteArray(void **vpp, const void *vp, uint32_t n);

    /**
     * @brief Replace an owned byte array with a copied array.
     * @ingroup libtiff_private_memory
     *
     * Frees the old destination before validating the new size or allocating
     * its replacement. A NULL source, zero/rejected byte product, or
     * allocation failure therefore leaves the destination NULL; the old data
     * is lost. This is not a failure-preserving realloc operation. The
     * source pointer is borrowed only during the copy and must not alias the
     * allocation freed first.
     *
     * Uses handle-aware allocation/free and must preserve the same
     * accounting context. Release the replacement with _TIFFfreeExt() using
     * that context. Returns no status. Product rejection and ordinary
     * allocation failure have no direct diagnostic; handle limit failures
     * may report through the allocation helpers. No file I/O occurs.
     *
     * @param[in,out] tif Matching live accounting handle, or NULL for
     * untracked storage.
     * @param[in,out] vpp Non-NULL pointer to an owned compatible array or
     * NULL array pointer; replaced or cleared.
     * @param[in] vp Readable source for n elements, or NULL to clear; must
     * not overlap the old destination allocation.
     * @param[in] n Element count in bytes.
     */
    extern void _TIFFsetByteArrayExt(TIFF *tif, void **vpp, const void *vp,
                                     uint32_t n);

    /**
     * @brief Replace an owned short array with a copied array.
     * @ingroup libtiff_private_memory
     *
     * Frees the old destination before validating the new size or allocating
     * its replacement. A NULL source, zero/rejected byte product, or
     * allocation failure therefore leaves the destination NULL; the old data
     * is lost. This is not a failure-preserving realloc operation. The
     * source pointer is borrowed only during the copy and must not alias the
     * allocation freed first.
     *
     * Uses the untracked family through a NULL handle. Release the replacement
     * with _TIFFfree(). Returns no status, and no diagnostic is emitted on any
     * failure. No file I/O occurs.
     *
     * @param[in,out] wpp Non-NULL pointer to an owned compatible array or
     * NULL array pointer; replaced or cleared.
     * @param[in] wp Readable source for n elements, or NULL to clear; must
     * not overlap the old destination allocation.
     * @param[in] n Element count.
     */
    extern void _TIFFsetShortArray(uint16_t **wpp, const uint16_t *wp,
                                   uint32_t n);

    /**
     * @brief Replace an owned short array with a copied array.
     * @ingroup libtiff_private_memory
     *
     * Frees the old destination before validating the new size or allocating
     * its replacement. A NULL source, zero/rejected byte product, or
     * allocation failure therefore leaves the destination NULL; the old data
     * is lost. This is not a failure-preserving realloc operation. The
     * source pointer is borrowed only during the copy and must not alias the
     * allocation freed first.
     *
     * Uses handle-aware allocation/free and must preserve the same
     * accounting context. Release the replacement with _TIFFfreeExt() using
     * that context. Returns no status. Product rejection and ordinary
     * allocation failure have no direct diagnostic; handle limit failures
     * may report through the allocation helpers. No file I/O occurs.
     *
     * @param[in,out] tif Matching live accounting handle, or NULL for
     * untracked storage.
     * @param[in,out] wpp Non-NULL pointer to an owned compatible array or
     * NULL array pointer; replaced or cleared.
     * @param[in] wp Readable source for n elements, or NULL to clear; must
     * not overlap the old destination allocation.
     * @param[in] n Element count.
     */
    extern void _TIFFsetShortArrayExt(TIFF *tif, uint16_t **wpp,
                                      const uint16_t *wp, uint32_t n);

    /**
     * @brief Replace an owned long array with a copied array.
     * @ingroup libtiff_private_memory
     *
     * Frees the old destination before validating the new size or allocating
     * its replacement. A NULL source, zero/rejected byte product, or
     * allocation failure therefore leaves the destination NULL; the old data
     * is lost. This is not a failure-preserving realloc operation. The
     * source pointer is borrowed only during the copy and must not alias the
     * allocation freed first.
     *
     * Uses the untracked family through a NULL handle. Release the replacement
     * with _TIFFfree(). Returns no status, and no diagnostic is emitted on any
     * failure. No file I/O occurs.
     *
     * @param[in,out] lpp Non-NULL pointer to an owned compatible array or
     * NULL array pointer; replaced or cleared.
     * @param[in] lp Readable source for n elements, or NULL to clear; must
     * not overlap the old destination allocation.
     * @param[in] n Element count.
     */
    extern void _TIFFsetLongArray(uint32_t **lpp, const uint32_t *lp,
                                  uint32_t n);

    /**
     * @brief Replace an owned long array with a copied array.
     * @ingroup libtiff_private_memory
     *
     * Frees the old destination before validating the new size or allocating
     * its replacement. A NULL source, zero/rejected byte product, or
     * allocation failure therefore leaves the destination NULL; the old data
     * is lost. This is not a failure-preserving realloc operation. The
     * source pointer is borrowed only during the copy and must not alias the
     * allocation freed first.
     *
     * Uses handle-aware allocation/free and must preserve the same
     * accounting context. Release the replacement with _TIFFfreeExt() using
     * that context. Returns no status. Product rejection and ordinary
     * allocation failure have no direct diagnostic; handle limit failures
     * may report through the allocation helpers. No file I/O occurs.
     *
     * @param[in,out] tif Matching live accounting handle, or NULL for
     * untracked storage.
     * @param[in,out] lpp Non-NULL pointer to an owned compatible array or
     * NULL array pointer; replaced or cleared.
     * @param[in] lp Readable source for n elements, or NULL to clear; must
     * not overlap the old destination allocation.
     * @param[in] n Element count.
     */
    extern void _TIFFsetLongArrayExt(TIFF *tif, uint32_t **lpp,
                                     const uint32_t *lp, uint32_t n);

    /**
     * @brief Replace an owned float array with a copied array.
     * @ingroup libtiff_private_memory
     *
     * Frees the old destination before validating the new size or allocating
     * its replacement. A NULL source, zero/rejected byte product, or
     * allocation failure therefore leaves the destination NULL; the old data
     * is lost. This is not a failure-preserving realloc operation. The
     * source pointer is borrowed only during the copy and must not alias the
     * allocation freed first.
     *
     * Uses the untracked family through a NULL handle. Release the replacement
     * with _TIFFfree(). Returns no status, and no diagnostic is emitted on any
     * failure. No file I/O occurs.
     *
     * @param[in,out] fpp Non-NULL pointer to an owned compatible array or
     * NULL array pointer; replaced or cleared.
     * @param[in] fp Readable source for n elements, or NULL to clear; must
     * not overlap the old destination allocation.
     * @param[in] n Element count.
     */
    extern void _TIFFsetFloatArray(float **fpp, const float *fp, uint32_t n);

    /**
     * @brief Replace an owned float array with a copied array.
     * @ingroup libtiff_private_memory
     *
     * Frees the old destination before validating the new size or allocating
     * its replacement. A NULL source, zero/rejected byte product, or
     * allocation failure therefore leaves the destination NULL; the old data
     * is lost. This is not a failure-preserving realloc operation. The
     * source pointer is borrowed only during the copy and must not alias the
     * allocation freed first.
     *
     * Uses handle-aware allocation/free and must preserve the same
     * accounting context. Release the replacement with _TIFFfreeExt() using
     * that context. Returns no status. Product rejection and ordinary
     * allocation failure have no direct diagnostic; handle limit failures
     * may report through the allocation helpers. No file I/O occurs.
     *
     * @param[in,out] tif Matching live accounting handle, or NULL for
     * untracked storage.
     * @param[in,out] fpp Non-NULL pointer to an owned compatible array or
     * NULL array pointer; replaced or cleared.
     * @param[in] fp Readable source for n elements, or NULL to clear; must
     * not overlap the old destination allocation.
     * @param[in] n Element count.
     */
    extern void _TIFFsetFloatArrayExt(TIFF *tif, float **fpp, const float *fp,
                                      uint32_t n);

    /**
     * @brief Replace an owned double array with a copied array.
     * @ingroup libtiff_private_memory
     *
     * Frees the old destination before validating the new size or allocating
     * its replacement. A NULL source, zero/rejected byte product, or
     * allocation failure therefore leaves the destination NULL; the old data
     * is lost. This is not a failure-preserving realloc operation. The
     * source pointer is borrowed only during the copy and must not alias the
     * allocation freed first.
     *
     * Uses the untracked family through a NULL handle. Release the replacement
     * with _TIFFfree(). Returns no status, and no diagnostic is emitted on any
     * failure. No file I/O occurs.
     *
     * @param[in,out] dpp Non-NULL pointer to an owned compatible array or
     * NULL array pointer; replaced or cleared.
     * @param[in] dp Readable source for n elements, or NULL to clear; must
     * not overlap the old destination allocation.
     * @param[in] n Element count.
     */
    extern void _TIFFsetDoubleArray(double **dpp, const double *dp, uint32_t n);

    /**
     * @brief Replace an owned double array with a copied array.
     * @ingroup libtiff_private_memory
     *
     * Frees the old destination before validating the new size or allocating
     * its replacement. A NULL source, zero/rejected byte product, or
     * allocation failure therefore leaves the destination NULL; the old data
     * is lost. This is not a failure-preserving realloc operation. The
     * source pointer is borrowed only during the copy and must not alias the
     * allocation freed first.
     *
     * Uses handle-aware allocation/free and must preserve the same
     * accounting context. Release the replacement with _TIFFfreeExt() using
     * that context. Returns no status. Product rejection and ordinary
     * allocation failure have no direct diagnostic; handle limit failures
     * may report through the allocation helpers. No file I/O occurs.
     *
     * @param[in,out] tif Matching live accounting handle, or NULL for
     * untracked storage.
     * @param[in,out] dpp Non-NULL pointer to an owned compatible array or
     * NULL array pointer; replaced or cleared.
     * @param[in] dp Readable source for n elements, or NULL to clear; must
     * not overlap the old destination allocation.
     * @param[in] n Element count.
     */
    extern void _TIFFsetDoubleArrayExt(TIFF *tif, double **dpp,
                                       const double *dp, uint32_t n);

    /**
     * @brief Print a terminated string using the private ASCII escape
     * convention.
     * @ingroup libtiff_private_directory_printing
     *
     * Writes at the stream's current position without flushing, closing,
     * allocating, or taking ownership. Printable bytes depend on the current C
     * locale; quotes and backslashes are written unchanged. Nonprintable tab,
     * backspace, carriage return, newline, and vertical tab use named backslash
     * escapes; other bytes use three octal digits. The implementation passes
     * plain char to isprint(), so portable input is seven-bit ASCII; high-bit
     * bytes on signed-char hosts are outside the classifier's valid domain
     * except EOF. Does not check/report stdio failures and returns no status.
     * Computes strlen() before formatting, so input must terminate in readable
     * storage. Adds no quotes or newline.
     *
     * @param[in,out] fd Non-NULL writable caller-owned stdio stream; not closed
     * or flushed by this helper.
     * @param[in] cp Non-NULL borrowed terminated string satisfying the
     * classifier domain; no alias with stream storage.
     */
    extern void _TIFFprintAscii(FILE *fd, const char *cp);
    /**
     * @brief Print a labelled quoted value using the private ASCII formatter.
     * @ingroup libtiff_private_directory_printing
     *
     * Writes at the stream's current position without flushing, closing,
     * allocating, or taking ownership. Printable bytes depend on the current C
     * locale; quotes and backslashes are written unchanged. Nonprintable tab,
     * backspace, carriage return, newline, and vertical tab use named backslash
     * escapes; other bytes use three octal digits. The implementation passes
     * plain char to isprint(), so portable input is seven-bit ASCII; high-bit
     * bytes on signed-char hosts are outside the classifier's valid domain
     * except EOF. Does not check/report stdio failures and returns no status.
     * Writes two leading spaces, the raw label, colon/space, quote-delimited
     * escaped value, and a final newline. The label is not escaped; unchanged
     * quotes/backslashes in the value prevent reliable round-trip parsing.
     *
     * @param[in,out] fd Non-NULL writable caller-owned stdio stream; not closed
     * or flushed by this helper.
     * @param[in] name Non-NULL borrowed null-terminated label, printed raw.
     * @param[in] value Non-NULL borrowed terminated value satisfying
     * _TIFFprintAscii() preconditions.
     */
    extern void _TIFFprintAsciiTag(FILE *fd, const char *name,
                                   const char *value);

    /**
     * @brief Store the global warning callback.
     * @ingroup libtiff_private_diagnostics
     *
     * Initially the platform stderr formatter in the standard backends.
     * TIFFSetWarningHandler() replaces this process-wide slot without
     * locking or lifetime management. It does not own callback code/context
     * and is not a per-handle setting.
     *
     */
    extern TIFFErrorHandler _TIFFwarningHandler;
    /**
     * @brief Store the global error callback.
     * @ingroup libtiff_private_diagnostics
     *
     * Initially the platform stderr formatter in the standard backends.
     * TIFFSetErrorHandler() replaces this process-wide slot without locking
     * or lifetime management. It does not own callback code/context and is
     * not a per-handle setting.
     *
     */
    extern TIFFErrorHandler _TIFFerrorHandler;
    /**
     * @brief Store the global extended warning callback.
     * @ingroup libtiff_private_diagnostics
     *
     * Initially NULL. TIFFSetWarningHandlerExt() replaces this process-wide
     * slot without locking or lifetime management. It does not own callback
     * code/context and is not a per-handle setting.
     *
     */
    extern TIFFErrorHandlerExt _TIFFwarningHandlerExt;
    /**
     * @brief Store the global extended error callback.
     * @ingroup libtiff_private_diagnostics
     *
     * Initially NULL. TIFFSetErrorHandlerExt() replaces this process-wide
     * slot without locking or lifetime management. It does not own callback
     * code/context and is not a per-handle setting.
     *
     */
    extern TIFFErrorHandlerExt _TIFFerrorHandlerExt;
    /**
     * @brief Report an opening error before a complete TIFF handle is
     * available.
     * @ingroup libtiff_private_lifecycle
     *
     * Invokes opts->errorhandler, if present, with tif = NULL and the
     * borrowed error context. A nonzero callback result suppresses the
     * legacy global handlers for this message; otherwise the global ordinary
     * and extended handlers run if installed. The extended handler receives
     * clientdata. Returns no status and does not itself abort opening or own
     * any argument storage. Callback effects and exceptions are not rolled
     * back.
     *
     * @param[in] opts Optional borrowed open options containing a per-handle
     * error callback.
     * @param[in] clientdata Opaque context supplied to the legacy extended
     * handler.
     * @param[in] module Borrowed diagnostic module name, possibly NULL.
     * @param[in] fmt Non-NULL printf-style format string.
     * @param[in] ... Arguments matching the format string and default
     * variadic promotions.
     */
    void _TIFFErrorEarly(TIFFOpenOptions *opts, thandle_t clientdata,
                         const char *module, const char *fmt, ...)
        TIFF_ATTRIBUTE((__format__(__printf__, 4, 5)));

    /**
     * @brief Multiply two unsigned values with an overflow check.
     * @ingroup libtiff_private_arithmetic
     *
     * Checks division bounds before multiplying. Overflow always invokes
     * TIFFErrorExtR(), including when tif is NULL; where is also formatted
     * as a string, so it must be valid on that path. Does not allocate,
     * mutate handle fields, retain arguments, or perform I/O except through
     * diagnostic callbacks.
     *
     * @param[in] tif Diagnostic handle, or NULL for global diagnostic
     * dispatch.
     * @param[in] first First factor.
     * @param[in] second Second factor.
     * @param[in] where Non-NULL diagnostic context name when an overflow can
     * occur.
     * @return The product, or zero if it exceeds UINT32_MAX; zero is also a
     * valid product.
     */
    extern uint32_t _TIFFMultiply32(TIFF *tif, uint32_t first, uint32_t second,
                                    const char *where);

    /**
     * @brief Multiply two unsigned values with an overflow check.
     * @ingroup libtiff_private_arithmetic
     *
     * Checks division bounds before multiplying. Overflow always invokes
     * TIFFErrorExtR(), including when tif is NULL; where is also formatted
     * as a string, so it must be valid on that path. Does not allocate,
     * mutate handle fields, retain arguments, or perform I/O except through
     * diagnostic callbacks.
     *
     * @param[in] tif Diagnostic handle, or NULL for global diagnostic
     * dispatch.
     * @param[in] first First factor.
     * @param[in] second Second factor.
     * @param[in] where Non-NULL diagnostic context name when an overflow can
     * occur.
     * @return The product, or zero if it exceeds UINT64_MAX; zero is also a
     * valid product.
     */
    extern uint64_t _TIFFMultiply64(TIFF *tif, uint64_t first, uint64_t second,
                                    const char *where);

    /**
     * @brief Check and add two unsigned operands.
     * @ingroup libtiff_private_arithmetic
     *
     * Unsigned operands. A rejection emits an error only when both tif and
     * where are non-NULL; otherwise it returns zero silently. Uses bounds
     * checks before the arithmetic operation. Does not allocate or change
     * handle fields; diagnostics can have callback-defined effects.
     *
     * @param[in] tif Optional diagnostic handle.
     * @param[in] first First operand.
     * @param[in] second Second operand.
     * @param[in] where Optional borrowed diagnostic context name.
     * @return The result, or zero for invalid operands or a result beyond
     * UINT64_MAX; a valid zero result is indistinguishable from rejection
     * where zero is allowed.
     */
    extern uint64_t _TIFFAdd64(TIFF *tif, uint64_t first, uint64_t second,
                               const char *where);

    /**
     * @brief Check and multiply two signed-size operands.
     * @ingroup libtiff_private_arithmetic
     *
     * Both operands must be positive; zero and negatives are rejected. A
     * rejection emits an error only when both tif and where are non-NULL;
     * otherwise it returns zero silently. Uses bounds checks before the
     * arithmetic operation. Does not allocate or change handle fields;
     * diagnostics can have callback-defined effects.
     *
     * @param[in] tif Optional diagnostic handle.
     * @param[in] first First operand.
     * @param[in] second Second operand.
     * @param[in] where Optional borrowed diagnostic context name.
     * @return The result, or zero for invalid operands or a result beyond
     * TIFF_TMSIZE_T_MAX; a valid zero result is indistinguishable from
     * rejection where zero is allowed.
     */
    extern tmsize_t _TIFFMultiplySSize(TIFF *tif, tmsize_t first,
                                       tmsize_t second, const char *where);

    /**
     * @brief Check and add two signed-size operands.
     * @ingroup libtiff_private_arithmetic
     *
     * Both operands must be nonnegative; zero is accepted. A rejection emits
     * an error only when both tif and where are non-NULL; otherwise it
     * returns zero silently. Uses bounds checks before the arithmetic
     * operation. Does not allocate or change handle fields; diagnostics can
     * have callback-defined effects.
     *
     * @param[in] tif Optional diagnostic handle.
     * @param[in] first First operand.
     * @param[in] second Second operand.
     * @param[in] where Optional borrowed diagnostic context name.
     * @return The result, or zero for invalid operands or a result beyond
     * TIFF_TMSIZE_T_MAX; a valid zero result is indistinguishable from
     * rejection where zero is allowed.
     */
    extern tmsize_t _TIFFAddSSize(TIFF *tif, tmsize_t first, tmsize_t second,
                                  const char *where);

    /**
     * @brief Convert an unsigned 64-bit value after checking the destination
     * range.
     * @ingroup libtiff_private_arithmetic
     *
     * Range rejection reports an error only when both tif and module are
     * non-NULL. Otherwise it fails silently with zero. Does not allocate,
     * retain pointers, or change handle state except for diagnostic callback
     * effects.
     *
     * @param[in] tif Optional diagnostic handle.
     * @param[in] val Unsigned value to convert.
     * @param[in] module Optional borrowed diagnostic module name.
     * @return The converted value, or zero if val exceeds TIFF_TMSIZE_T_MAX;
     * zero is also a valid converted value.
     */
    extern tmsize_t _TIFFCastUInt64ToSSize(TIFF *tif, uint64_t val,
                                           const char *module);

    /**
     * @brief Convert an unsigned 64-bit value after checking the destination
     * range.
     * @ingroup libtiff_private_arithmetic
     *
     * Range rejection reports an error only when both tif and module are
     * non-NULL. Otherwise it fails silently with zero. Does not allocate,
     * retain pointers, or change handle state except for diagnostic callback
     * effects.
     *
     * @param[in] tif Optional diagnostic handle.
     * @param[in] val Unsigned value to convert.
     * @param[in] module Optional borrowed diagnostic module name.
     * @return The converted value, or zero if val exceeds UINT32_MAX; zero
     * is also a valid converted value.
     */
    extern uint32_t _TIFFCastUInt64ToUInt32(TIFF *tif, uint64_t val,
                                            const char *module);

    /**
     * @brief Compute a byte-rounded row size from width and sample geometry.
     * @ingroup libtiff_private_arithmetic
     *
     * Rounds the complete row once, rather than each pixel/sample. Zero
     * geometry returns zero without a direct diagnostic. Multiplication
     * rejection uses _TIFFMultiply64() reporting rules. This is a generic
     * packed-row computation, not a complete codec/YCbCr layout rule. No
     * allocation, ownership transfer, or handle-field mutation occurs.
     *
     * @param[in] tif Optional diagnostic handle used by checked
     * multiplication.
     * @param[in] width Row width in pixels.
     * @param[in] spp Samples per pixel.
     * @param[in] bps Bits per sample.
     * @param[in] where Non-NULL diagnostic context if multiplication
     * overflow is possible.
     * @return ceil(width * spp * bps / 8), or zero if an intermediate
     * product is zero or a multiplication check rejects it.
     */
    extern uint64_t _TIFFComputeRowSize64(TIFF *tif, uint32_t width,
                                          uint16_t spp, uint16_t bps,
                                          const char *where);

    /**
     * @brief Compute a checked byte offset for a row in a memory raster.
     * @ingroup libtiff_private_arithmetic
     *
     * Delegates nonzero rows to _TIFFMultiplySSize(), including its
     * conditional diagnostics. On narrower hosts, the row conversion must
     * itself be representable. Does not allocate, validate an actual buffer
     * extent, or change handle fields.
     *
     * @param[in] tif Optional diagnostic handle.
     * @param[in] rowstride Positive bytes per row when row is nonzero.
     * @param[in] row Zero-based row; must be representable by tmsize_t when
     * nonzero.
     * @param[in] where Optional borrowed diagnostic context.
     * @return Zero for row zero without validating rowstride; otherwise the
     * checked positive product, or zero on rejection.
     */
    extern tmsize_t _TIFFComputeRowOffset(TIFF *tif, tmsize_t rowstride,
                                          uint32_t row, const char *where);

    /**
     * @brief Compute a checked packed-sample column offset in bits.
     * @ingroup libtiff_private_arithmetic
     *
     * Uses _TIFFMultiply64() for the two products and its diagnostic rules.
     * Does not round to a byte boundary or validate a buffer extent. Does
     * not allocate or change handle fields.
     *
     * @param[in] tif Optional diagnostic handle.
     * @param[in] col Zero-based pixel column.
     * @param[in] spp Samples per pixel.
     * @param[in] bps Bits per sample.
     * @param[in] where Non-NULL diagnostic context if multiplication
     * overflow is possible.
     * @return col * spp * bps, or zero for a zero product or checked
     * rejection; these cases are not distinguished.
     */
    extern uint64_t _TIFFComputeBitOffset(TIFF *tif, uint32_t col, uint16_t spp,
                                          uint16_t bps, const char *where);

    /**
     * @brief Allocate a checked array with an allocation-failure diagnostic.
     * @ingroup libtiff_private_memory
     *
     * Checks the product through _TIFFMultiplySSize() with its optional
     * arithmetic diagnostic disabled, then delegates to _TIFFreallocExt().
     * Zero/negative operands and overflowing products are rejected without
     * calling realloc. Any NULL result emits an allocation diagnostic using
     * tif->tif_name and what; limit rejection can therefore produce both a
     * limit and an allocation diagnostic.
     *
     * Equivalent to _TIFFCheckRealloc() with buffer = NULL. The returned
     * block is uninitialised and must be released with _TIFFfreeExt() and
     * the same handle.
     *
     * @param[in,out] tif Non-NULL live TIFF handle for allocation policy and
     * diagnostics.
     * @param[in] nmemb Positive element count.
     * @param[in] elem_size Positive element size in bytes.
     * @param[in] what Non-NULL borrowed allocation-purpose description for
     * diagnostics.
     * @return An owned block on success, or NULL on invalid/overflowing
     * size, a policy limit, or allocator failure.
     */
    extern void *_TIFFCheckMalloc(TIFF *tif, tmsize_t nmemb, tmsize_t elem_size,
                                  const char *what);

    /**
     * @brief Resize a checked array with an allocation-failure diagnostic.
     * @ingroup libtiff_private_memory
     *
     * Checks the product through _TIFFMultiplySSize() with its optional
     * arithmetic diagnostic disabled, then delegates to _TIFFreallocExt().
     * Zero/negative operands and overflowing products are rejected without
     * calling realloc. Any NULL result emits an allocation diagnostic using
     * tif->tif_name and what; limit rejection can therefore produce both a
     * limit and an allocation diagnostic.
     *
     * On any NULL result the old positive-size allocation remains owned and
     * valid, including a rejected zero request; unlike _TIFFreallocExt(s <=
     * 0), a zero count does not free it. Success invalidates the old pointer
     * and returns storage for _TIFFfreeExt() with the matching handle.
     * Preserve the old pointer until success.
     *
     * @param[in,out] tif Non-NULL live TIFF handle for allocation policy and
     * diagnostics.
     * @param[in] buffer Matching handle-aware allocation to resize, or NULL.
     * @param[in] nmemb Positive element count.
     * @param[in] elem_size Positive element size in bytes.
     * @param[in] what Non-NULL borrowed allocation-purpose description for
     * diagnostics.
     * @return An owned block on success, or NULL on invalid/overflowing
     * size, a policy limit, or allocator failure.
     */
    extern void *_TIFFCheckRealloc(TIFF *tif, void *buffer, tmsize_t nmemb,
                                   tmsize_t elem_size, const char *what);

    /**
     * @brief Clamp a double to the finite float range before conversion.
     * @ingroup libtiff_private_arithmetic
     *
     * Performs no allocation, handle mutation, ownership transfer, or
     * library diagnostics. Normal floating-point rounding/exception
     * behaviour applies; NaN payload preservation is not promised.
     *
     * @param[in] val Input double, including infinities or NaN.
     * @return FLT_MAX or -FLT_MAX outside that finite range, otherwise the
     * float conversion of val. Infinities clamp; NaN follows the floating
     * conversion rather than selecting a bound.
     */
    extern float _TIFFClampDoubleToFloat(double val);

    /**
     * @brief Clamp and truncate a double to an unsigned 32-bit value.
     * @ingroup libtiff_private_arithmetic
     *
     * Performs no allocation, ownership transfer, handle mutation, or
     * library diagnostics. Normal floating-point exception behaviour
     * applies; the NaN-to-maximum rule is intentional current behaviour.
     *
     * @param[in] val Input double, including infinities or NaN.
     * @return Zero for negative values; UINT32_MAX for NaN or values above
     * UINT32_MAX; otherwise the uint32_t conversion truncating toward zero.
     */
    extern uint32_t _TIFFClampDoubleToUInt32(double val);

    /**
     * @brief Release a handle's directory-offset/index lookup maps.
     * @ingroup libtiff_private_lifecycle
     *
     * Destroys each existing map and sets its member pointer to NULL.
     * Borrowed entries/views into the maps become invalid. Does not free the
     * TIFF handle or close client I/O and has no status result or direct
     * diagnostic.
     *
     * @param[in,out] tif Non-NULL TIFF handle whose maps may be NULL.
     */
    extern void _TIFFCleanupIFDOffsetAndNumberMaps(TIFF *tif);

    /**
     * @brief Compute row-aligned decoded storage for a strip or tile-height
     * request.
     * @ingroup libtiff_private_image_layout
     *
     * Strip mode maps UINT32_MAX rows to ImageLength. Tile mode rejects zero
     * tile width/length/depth silently but does not multiply by depth. Packed
     * contiguous non-upsampled YCbCr requires exactly three samples and
     * horizontal/vertical factors each in {1,2,4}; invokes the defaulted
     * subsampling getter, rounds width/height to sampling blocks and the entire
     * horizontal block-row bit total to bytes, then multiplies by vertical
     * block count. Other layouts multiply nrows by the scanline or tile-row
     * estimate. Invalid YCbCr sample count or subsampling factors report an
     * error and return zero. Checked products diagnose overflow, but
     * conservative ceiling guards can return zero silently. Allocates no output
     * and does not decode pixels; getters can mutate lookup/cache state or
     * invoke callbacks. No ownership transfer; caller must establish consistent
     * geometry.
     *
     * @param[in,out] tif Non-NULL live handle with consistent current geometry;
     * queries may use getters/callbacks. Serialise access externally.
     * @param[in] nrows Row count; UINT32_MAX is an all-image sentinel only in
     * strip mode.
     * @param[in] isStrip Nonzero selects strip width/scanline and sentinel
     * handling; zero selects tile geometry.
     * @return Byte estimate, or zero for empty/rejected/overflowed geometry;
     * not a general multi-depth or buffer-validity guarantee.
     */
    extern uint64_t _TIFFStrileSize64(TIFF *tif, uint32_t nrows, int isStrip);

    /**
     * @brief Decode after deferred allocation of caller-owned output storage.
     * @ingroup libtiff_private_image_transfers
     *
     * An existing *buf delegates to the normal decoder. A NULL slot first loads
     * raw data and applies selected compression-ratio/size sanity checks, then
     * allocates zeroed matching Ext storage. On success the caller owns it and
     * must use _TIFFfreeExt(tif,...); keep the matching handle/accounting
     * context alive. Later decode failure retains that allocation and partial
     * output in *buf for the caller to release. The separate allocation-size
     * and decode-size parameters are caller-established invariants, not an
     * automatic capacity check. Tile coordinate wrapper checks coordinates
     * before delegating. May allocate or replace handle-owned raw/strile
     * buffers, load deferred metadata, seek/read/write client I/O, and invoke
     * codec/tag/diagnostic callbacks. No handle or caller-buffer ownership
     * transfers. Failure is not transactional: buffers, codec progress,
     * indices, flags, metadata, file position, or file bytes can already have
     * changed; disregard partial decoded output unless a codec explicitly
     * documents otherwise. No errno, rollback, or durability guarantee. See
     * @ref image_transfers.
     *
     * @param[in,out] tif Non-NULL live handle with matching current geometry
     * and codec methods; serialise access externally.
     * @param[in] strip Valid strip index.
     * @param[in,out] buf Non-NULL owner slot, initially NULL or a matching
     * writable allocation; existing storage must already have enough capacity.
     * @param[in] bufsizetoalloc Positive allocation byte count at least the
     * effective decoded output extent; the helper does not verify that
     * relationship.
     * @param[in] size_to_read Minus one for full decoded extent, otherwise
     * nonnegative supported output limit.
     * @return Effective decoded byte request on success, minus one on detected
     * rejection/allocation/decoder failure; an allocation may survive failure.
     */
    extern tmsize_t _TIFFReadEncodedStripAndAllocBuffer(TIFF *tif,
                                                        uint32_t strip,
                                                        void **buf,
                                                        tmsize_t bufsizetoalloc,
                                                        tmsize_t size_to_read);
    /**
     * @brief Decode after deferred allocation of caller-owned output storage.
     * @ingroup libtiff_private_image_transfers
     *
     * An existing *buf delegates to the normal decoder. A NULL slot first loads
     * raw data and applies selected compression-ratio/size sanity checks, then
     * allocates zeroed matching Ext storage. On success the caller owns it and
     * must use _TIFFfreeExt(tif,...); keep the matching handle/accounting
     * context alive. Later decode failure retains that allocation and partial
     * output in *buf for the caller to release. The separate allocation-size
     * and decode-size parameters are caller-established invariants, not an
     * automatic capacity check. Tile coordinate wrapper checks coordinates
     * before delegating. May allocate or replace handle-owned raw/strile
     * buffers, load deferred metadata, seek/read/write client I/O, and invoke
     * codec/tag/diagnostic callbacks. No handle or caller-buffer ownership
     * transfers. Failure is not transactional: buffers, codec progress,
     * indices, flags, metadata, file position, or file bytes can already have
     * changed; disregard partial decoded output unless a codec explicitly
     * documents otherwise. No errno, rollback, or durability guarantee. See
     * @ref image_transfers.
     *
     * @param[in,out] tif Non-NULL live handle with matching current geometry
     * and codec methods; serialise access externally.
     * @param[in] tile Valid tile index.
     * @param[in,out] buf Non-NULL owner slot, initially NULL or a matching
     * writable allocation; existing storage must already have enough capacity.
     * @param[in] bufsizetoalloc Positive allocation byte count at least the
     * effective decoded output extent; the helper does not verify that
     * relationship.
     * @param[in] size_to_read Minus one for full decoded extent, otherwise
     * nonnegative supported output limit.
     * @return Effective decoded byte request on success, minus one on detected
     * rejection/allocation/decoder failure; an allocation may survive failure.
     */
    extern tmsize_t _TIFFReadEncodedTileAndAllocBuffer(TIFF *tif, uint32_t tile,
                                                       void **buf,
                                                       tmsize_t bufsizetoalloc,
                                                       tmsize_t size_to_read);
    /**
     * @brief Decode after deferred allocation of caller-owned output storage.
     * @ingroup libtiff_private_image_transfers
     *
     * An existing *buf delegates to the normal decoder. A NULL slot first loads
     * raw data and applies selected compression-ratio/size sanity checks, then
     * allocates zeroed matching Ext storage. On success the caller owns it and
     * must use _TIFFfreeExt(tif,...); keep the matching handle/accounting
     * context alive. Later decode failure retains that allocation and partial
     * output in *buf for the caller to release. The separate allocation-size
     * and decode-size parameters are caller-established invariants, not an
     * automatic capacity check. Tile coordinate wrapper checks coordinates
     * before delegating. May allocate or replace handle-owned raw/strile
     * buffers, load deferred metadata, seek/read/write client I/O, and invoke
     * codec/tag/diagnostic callbacks. No handle or caller-buffer ownership
     * transfers. Failure is not transactional: buffers, codec progress,
     * indices, flags, metadata, file position, or file bytes can already have
     * changed; disregard partial decoded output unless a codec explicitly
     * documents otherwise. No errno, rollback, or durability guarantee. See
     * @ref image_transfers.
     *
     * @param[in,out] tif Non-NULL live handle with matching current geometry
     * and codec methods; serialise access externally.
     * @param[in,out] buf Non-NULL owner slot, initially NULL or a matching
     * writable allocation; existing storage must already have enough capacity.
     * @param[in] bufsizetoalloc Positive allocation byte count at least the
     * effective decoded output extent; the helper does not verify that
     * relationship.
     * @param[in] x Image column.
     * @param[in] y Image row.
     * @param[in] z Depth slice.
     * @param[in] s Separate sample plane.
     * @return Effective decoded byte request on success, minus one on detected
     * rejection/allocation/decoder failure; an allocation may survive failure.
     */
    extern tmsize_t _TIFFReadTileAndAllocBuffer(TIFF *tif, void **buf,
                                                tmsize_t bufsizetoalloc,
                                                uint32_t x, uint32_t y,
                                                uint32_t z, uint16_t s);

    /**
     * @brief Test whether an allowed absolute client seek reaches its target.
     * @ingroup libtiff_private_arithmetic
     *
     * Offsets above the bound are rejected without seeking. Otherwise
     * invokes one SEEK_SET callback. May change client position/state and
     * inherits its failure conventions; a zero-returning native Windows seek
     * failure can be confused with a successful seek to zero. Adds no
     * diagnostics, allocation, or ownership transfer.
     *
     * @param[in,out] tif Non-NULL live TIFF handle with seek callback.
     * @param[in] off Requested absolute byte position.
     * @return Nonzero if off is at most UINT64_MAX / 2 and the client
     * reports that exact position; zero otherwise.
     */
    extern int _TIFFSeekOK(TIFF *tif, toff_t off);

    /**
     * @brief Install uncompressed transfer callbacks.
     * @ingroup libtiff_private_basic_codecs
     *
     * Requires prior default compression state and cleanup of any previous
     * codec. Assigns supported callbacks without allocating outer state or
     * touching payload. Installs direct-copy encoding/decoding and buffered
     * forward row seek. No ownership transfer; later transfer callbacks have
     * their own state and I/O effects.
     *
     * @param[in,out] tif Non-NULL live handle whose codec methods are
     * overwritten; geometry is not consulted. Serialise access.
     * @param[in] scheme Unused compatibility compression identifier.
     * @return One unconditionally.
     */
    extern int TIFFInitDumpMode(TIFF *tif, int scheme);
#ifdef PACKBITS_SUPPORT
    /**
     * @brief Install PackBits transfer callbacks.
     * @ingroup libtiff_private_basic_codecs
     *
     * Requires prior default compression state and cleanup of any previous
     * codec. Assigns supported callbacks without allocating outer state or
     * touching payload. PackBits encoding callbacks are omitted in
     * PACKBITS_READ_ONLY builds. No ownership transfer; later transfer
     * callbacks have their own state and I/O effects.
     *
     * @param[in,out] tif Non-NULL live handle whose codec methods are
     * overwritten; geometry is not consulted. Serialise access.
     * @param[in] scheme Unused compatibility compression identifier.
     * @return One unconditionally.
     */
    extern int TIFFInitPackBits(TIFF *tif, int scheme);
#endif
#ifdef CCITT_SUPPORT
    /**
     * @brief Install CCITTRLE fax callbacks and default mode.
     * @ingroup libtiff_private_fax
     *
     * Uses shared fax installation, merges applicable Group3/4 fields and/or
     * overrides transfer methods, then sets mode through TIFFSetField.
     * Suppresses RTC/EOL and byte-aligns encoded rows. Requires cleaned-up
     * prior codec state. Field/allocation/setter failure can leave partially
     * installed state for lifecycle cleanup; diagnostics can run, no pixel I/O
     * or caller ownership transfer.
     *
     * @param[in,out] tif Non-NULL handle whose previous codec state has been
     * cleaned up; receives the new fax codec state. Serialise operations.
     * @param[in] scheme Compatibility scheme argument; this wrapper selects its
     * own fax variant.
     * @return One after successful setup/mode assignment, zero on shared setup,
     * field merge or setter failure.
     */
    extern int TIFFInitCCITTRLE(TIFF *tif, int scheme);
    /**
     * @brief Install CCITTRLEW fax callbacks and default mode.
     * @ingroup libtiff_private_fax
     *
     * Uses shared fax installation, merges applicable Group3/4 fields and/or
     * overrides transfer methods, then sets mode through TIFFSetField.
     * Suppresses RTC/EOL and word-aligns encoded rows. Requires cleaned-up
     * prior codec state. Field/allocation/setter failure can leave partially
     * installed state for lifecycle cleanup; diagnostics can run, no pixel I/O
     * or caller ownership transfer.
     *
     * @param[in,out] tif Non-NULL handle whose previous codec state has been
     * cleaned up; receives the new fax codec state. Serialise operations.
     * @param[in] scheme Compatibility scheme argument; this wrapper selects its
     * own fax variant.
     * @return One after successful setup/mode assignment, zero on shared setup,
     * field merge or setter failure.
     */
    extern int TIFFInitCCITTRLEW(TIFF *tif, int scheme);
    /**
     * @brief Install CCITTFax3 fax callbacks and default mode.
     * @ingroup libtiff_private_fax
     *
     * Uses shared fax installation, merges applicable Group3/4 fields and/or
     * overrides transfer methods, then sets mode through TIFFSetField. Defaults
     * to Class/F without RTC. Requires cleaned-up prior codec state.
     * Field/allocation/setter failure can leave partially installed state for
     * lifecycle cleanup; diagnostics can run, no pixel I/O or caller ownership
     * transfer.
     *
     * @param[in,out] tif Non-NULL handle whose previous codec state has been
     * cleaned up; receives the new fax codec state. Serialise operations.
     * @param[in] scheme Compatibility scheme argument; this wrapper selects its
     * own fax variant.
     * @return One after successful setup/mode assignment, zero on shared setup,
     * field merge or setter failure.
     */
    extern int TIFFInitCCITTFax3(TIFF *tif, int scheme);
    /**
     * @brief Install CCITTFax4 fax callbacks and default mode.
     * @ingroup libtiff_private_fax
     *
     * Uses shared fax installation, merges applicable Group3/4 fields and/or
     * overrides transfer methods, then sets mode through TIFFSetField. Selects
     * Group4 transfer/EOFB and suppresses RTC. Requires cleaned-up prior codec
     * state. Field/allocation/setter failure can leave partially installed
     * state for lifecycle cleanup; diagnostics can run, no pixel I/O or caller
     * ownership transfer.
     *
     * @param[in,out] tif Non-NULL handle whose previous codec state has been
     * cleaned up; receives the new fax codec state. Serialise operations.
     * @param[in] scheme Compatibility scheme argument; this wrapper selects its
     * own fax variant.
     * @return One after successful setup/mode assignment, zero on shared setup,
     * field merge or setter failure.
     */
    extern int TIFFInitCCITTFax4(TIFF *tif, int scheme);
#endif
#ifdef THUNDER_SUPPORT
    /**
     * @brief Install ThunderScan transfer callbacks.
     * @ingroup libtiff_private_basic_codecs
     *
     * Requires prior default compression state and cleanup of any previous
     * codec. Assigns supported callbacks without allocating outer state or
     * touching payload. ThunderScan installs row/strip decoding only; no tile
     * encoder/decoder is installed. No ownership transfer; later transfer
     * callbacks have their own state and I/O effects.
     *
     * @param[in,out] tif Non-NULL live handle whose codec methods are
     * overwritten; geometry is not consulted. Serialise access.
     * @param[in] scheme Unused compatibility compression identifier.
     * @return One unconditionally.
     */
    extern int TIFFInitThunderScan(TIFF *tif, int scheme);
#endif
#ifdef NEXT_SUPPORT
    /**
     * @brief Install NeXT transfer callbacks.
     * @ingroup libtiff_private_basic_codecs
     *
     * Requires prior default compression state and cleanup of any previous
     * codec. Assigns supported callbacks without allocating outer state or
     * touching payload. NeXT installs decode-only row/strip/tile methods. No
     * ownership transfer; later transfer callbacks have their own state and I/O
     * effects.
     *
     * @param[in,out] tif Non-NULL live handle whose codec methods are
     * overwritten; geometry is not consulted. Serialise access.
     * @param[in] scheme Unused compatibility compression identifier.
     * @return One unconditionally.
     */
    extern int TIFFInitNeXT(TIFF *tif, int scheme);
#endif
#ifdef LZW_SUPPORT
    /**
     * @brief Allocate LZW codec state and install predictor-aware methods.
     * @ingroup libtiff_private_lzw
     *
     * Requires COMPRESSION_LZW and cleaned-up previous codec state. Allocates
     * handle-owned state, initialises table pointers, installs callbacks and
     * attempts predictor hook installation. PredictorInit status is ignored, so
     * success does not guarantee that field merging succeeded. Encoding methods
     * are omitted in LZW_READ_ONLY builds. Allocation failure diagnoses;
     * partial state/callback effects are not rolled back. No pixel I/O.
     *
     * @param[in,out] tif Non-NULL handle whose previous codec state has been
     * cleaned up; receives the new codec state. Serialise access.
     * @param[in] scheme COMPRESSION_LZW; asserted in debug builds.
     * @return One after state/method installation, zero on outer-state
     * allocation failure.
     */
    extern int TIFFInitLZW(TIFF *tif, int scheme);
#endif
#ifdef OJPEG_SUPPORT
    /**
     * @brief Install the decode-only old-style JPEG codec.
     * @ingroup libtiff_private_ojpeg
     *
     * Requires COMPRESSION_OJPEG and previous codec cleanup. Merges fields,
     * allocates zeroed Ext state, preserves parent tag/print hooks and installs
     * codec methods. Sets NOREADRAW because this decoder reads its own
     * interchange/strile stream. Defaults to baseline process and 2x2 sampling;
     * the 2x2 YCbCrSubsampling is set through the parent setter before the
     * codec tag methods are installed, so it is not marked as an explicit codec
     * tag, and its status is ignored. Allocation/field failures
     * diagnose; installed state remains handle-owned until cleanup.
     *
     * @param[in,out] tif Non-NULL handle whose previous codec state has been
     * cleaned up; receives the new codec state. Serialise access.
     * @param[in] scheme COMPRESSION_OJPEG, asserted in debug builds.
     * @return One after installation, zero on field/allocation failure.
     */
    extern int TIFFInitOJPEG(TIFF *tif, int scheme);
#endif
#ifdef JPEG_SUPPORT
    /**
     * @brief Allocate JPEG-in-TIFF codec state and install method chains.
     * @ingroup libtiff_private_jpeg
     *
     * Requires COMPRESSION_JPEG and cleaned-up previous state. Merges fields,
     * allocates zeroed Ext state, saves tag/print/sizing hooks and installs
     * common callbacks. For a not-yet-written directory allocates a zero-filled
     * 2000-byte JPEGTables placeholder without marking its field present; later
     * setup builds real tables. Failures diagnose and can retain installed
     * state for lifecycle cleanup. Dependency context is created lazily; no
     * pixel I/O or caller ownership transfer.
     *
     * @param[in,out] tif Non-NULL handle whose previous codec state has been
     * cleaned up; receives the new codec state. Serialise operations.
     * @param[in] scheme COMPRESSION_JPEG, asserted in debug builds.
     * @return One after installation/placeholder setup, zero on field or
     * allocation failure.
     */
    extern int TIFFInitJPEG(TIFF *tif, int scheme);
    /**
     * @brief Probe whether cached JPEG input needs whole-strile decoding.
     * @ingroup libtiff_private_jpeg
     *
     * Independently creates a temporary dependency decompressor over
     * rawdata/rawcc, reads a header, queries multiple scans and destroys
     * dependency state. Enabled twelve-bit input delegates to the twelve-bit
     * probe. Requires valid readable raw bytes and sample depth; does not
     * require usual codec setup. Some create/destroy statuses are ignored.
     * Diagnostics and synthetic-EOI recovery can occur; no caller storage
     * ownership transfer or client reads.
     *
     * @param[in,out] tif Non-NULL live JPEG handle with compatible current
     * directory/state; serialise operations.
     * @return One for multiple scans, zero for single-scan input or
     * trapped/header failure; zero does not distinguish failure.
     */
    extern int TIFFJPEGIsFullStripRequired(TIFF *tif);
#endif
#ifdef JBIG_SUPPORT
    /**
     * @brief Install single-strip JBIG callbacks and mutable raw-input policy.
     * @ingroup libtiff_private_jbig
     *
     * Requires cleaned-up previous codec state and COMPRESSION_JBIG. Sets
     * NOBITREV, clears MAPPED, and detaches a previously mapped raw view while
     * restoring owned-buffer policy. Does not free the mapping, allocate outer
     * state or perform pixel I/O. Installs strip decode/encode and setup
     * methods; no row or tile transfer support. No ownership transfer of caller
     * storage.
     *
     * @param[in,out] tif Non-NULL handle whose previous codec state has been
     * cleaned up; its flags, raw-buffer fields and methods are changed.
     * Serialise access.
     * @param[in] scheme COMPRESSION_JBIG, asserted in debug builds.
     * @return One unconditionally.
     */
    extern int TIFFInitJBIG(TIFF *tif, int scheme);
#endif
#ifdef ZIP_SUPPORT
    /**
     * @brief Install predictor-aware Deflate codec and allocator hooks.
     * @ingroup libtiff_private_deflate
     *
     * Requires a supported Deflate scheme and cleaned-up previous state. Merges
     * pseudo-tags, allocates zeroed codec state, installs handle-backed
     * dependency allocators, saves tag hooks, sets default quality/backend and
     * assigns callbacks. Attempts PredictorInit without propagating its status.
     * Merge/allocation failure diagnoses; metadata/state effects are not rolled
     * back. Dependency streams/optional backend objects are initialised lazily;
     * no pixel I/O.
     *
     * @param[in,out] tif Non-NULL handle whose previous codec state has been
     * cleaned up; receives the new codec state. Serialise operations.
     * @param[in] scheme COMPRESSION_DEFLATE or COMPRESSION_ADOBE_DEFLATE,
     * asserted in debug builds.
     * @return One after installation, zero on field merge or outer allocation
     * failure.
     */
    extern int TIFFInitZIP(TIFF *tif, int scheme);
#endif
#ifdef PIXARLOG_SUPPORT
    /**
     * @brief Install PixarLog codec state methods and companding tables.
     * @ingroup libtiff_private_pixarlog
     *
     * Requires COMPRESSION_PIXARLOG and cleaned-up previous state. Merges
     * pseudo-tags, allocates zeroed Ext state, chains get/set hooks, installs
     * transfer/close/cleanup methods and defaults unknown user format/default
     * zlib quality. Attempts PredictorInit without propagating status, then
     * builds tables and shared float coefficients. Serialise initialisation and
     * float encoding across handles because those coefficients are
     * process-global mutable storage. Failures can retain installed state for
     * cleanup; no pixel I/O or caller ownership transfer.
     *
     * @param[in,out] tif Non-NULL handle whose previous codec state has been
     * cleaned up; receives the new codec state. Serialise operations
     * and shared-table initialisation.
     * @param[in] scheme COMPRESSION_PIXARLOG, asserted in debug builds.
     * @return One after table construction, zero on field/state/table
     * allocation failure.
     */
    extern int TIFFInitPixarLog(TIFF *tif, int scheme);
#endif
#ifdef LOGLUV_SUPPORT
    /**
     * @brief Allocate SGILog codec state and install row dispatch and tag
     * hooks.
     * @ingroup libtiff_private_sgilog
     *
     * Requires SGILOG/SGILOG24 and cleaned-up previous codec state. Merges
     * pseudo-tags, allocates zeroed Ext state, saves tag methods and installs
     * strile/setup/close/cleanup dispatch. Row methods are selected at later
     * setup. Defaults no dither for SGILOG and random dither for SGILOG24,
     * unknown user format and no-op translation. Merge/allocation failures
     * diagnose without rollback; no pixel I/O or caller ownership transfer.
     *
     * @param[in,out] tif Non-NULL handle whose previous codec state has been
     * cleaned up; receives the new codec state. Serialise access.
     * @param[in] scheme COMPRESSION_SGILOG or COMPRESSION_SGILOG24, selecting
     * default dithering.
     * @return One after installation, zero on field merge or state allocation
     * failure.
     */
    extern int TIFFInitSGILog(TIFF *tif, int scheme);
#endif
#ifdef LERC_SUPPORT
    /**
     * @brief Allocate LERC staging state and install frame callbacks and
     * pseudo-tags.
     * @ingroup libtiff_private_lerc
     *
     * Requires cleaned-up previous codec state and COMPRESSION_LERC. Merges
     * field metadata, allocates zeroed Ext state, chains tag methods and
     * installs decode plus non-read-only encode callbacks. Sets
     * version/additional-compression defaults through TIFFSetField without
     * checking those statuses, then sets zero error tolerance, ZSTD level nine
     * and default Deflate quality. Reusable buffers/dependency objects are
     * created lazily. No predictor prefix or finite ratio heuristic is
     * installed. Failures diagnose without general rollback; no pixel I/O.
     *
     * @param[in,out] tif Non-NULL handle whose previous codec state has been
     * cleaned up; receives the new codec state. Serialise access.
     * @param[in] scheme COMPRESSION_LERC, asserted in debug builds.
     * @return One after installation/default assignments, zero on field merge
     * or outer allocation failure.
     */
    extern int TIFFInitLERC(TIFF *tif, int scheme);
#endif
#ifdef LZMA_SUPPORT
    /**
     * @brief Allocate predictor-aware LZMA2 codec state and install callbacks.
     * @ingroup libtiff_private_lzma
     *
     * Requires cleaned-up previous state and COMPRESSION_LZMA. Merges preset
     * metadata, allocates state and initialises dependency stream, saved tag
     * hooks, default preset and no integrity check. Configures a byte-delta
     * filter followed by LZMA2; delta distance is BitsPerSample/8 when
     * BitsPerSample is a multiple of eight, otherwise one, using the bit depth
     * current at initialisation. Installs methods and attempts PredictorInit
     * without propagating its status. Failures diagnose; merged metadata or
     * state changes are not rolled back. No pixel I/O.
     *
     * @param[in,out] tif Non-NULL handle whose previous codec state has been
     * cleaned up; receives the new codec state. Serialise operations.
     * @param[in] scheme COMPRESSION_LZMA, asserted in debug builds.
     * @return One after installation, zero on field merge or outer allocation
     * failure.
     */
    extern int TIFFInitLZMA(TIFF *tif, int scheme);
#endif
#ifdef ZSTD_SUPPORT
    /**
     * @brief Allocate ZSTD state and install predictor-aware streaming methods.
     * @ingroup libtiff_private_zstd
     *
     * Requires cleaned-up previous codec state and COMPRESSION_ZSTD. Merges
     * pseudo-tag metadata, allocates handle-owned state, saves tag hooks,
     * defaults level to nine, and installs stream/cleanup methods. Attempts
     * PredictorInit without propagating its status. Field/allocation failure
     * diagnoses; merged metadata or other changes are not rolled back.
     * Dependency streams are created lazily. No pixel I/O.
     *
     * @param[in,out] tif Non-NULL handle whose previous codec state has been
     * cleaned up; receives the new codec state. Serialise operations.
     * @param[in] scheme COMPRESSION_ZSTD, asserted in debug builds.
     * @return One after state/method installation, zero on field merge or outer
     * allocation failure.
     */
    extern int TIFFInitZSTD(TIFF *tif, int scheme);
#endif
#ifdef WEBP_SUPPORT
    /**
     * @brief Allocate WebP codec state and install buffered frame callbacks.
     * @ingroup libtiff_private_webp
     *
     * Requires cleaned-up previous codec state and COMPRESSION_WEBP. Merges
     * pseudo-tags, allocates outer Ext state, saves tag hooks and sets defaults
     * quality 75, lossy, exact enabled for later lossless use. Installs
     * row/strile transfer and cleanup callbacks; row encoding stages input
     * until postencode. Field/allocation failure diagnoses without general
     * rollback. Dependency objects and staging are created lazily. No pixel
     * I/O.
     *
     * @param[in,out] tif Non-NULL handle whose previous codec state has been
     * cleaned up; receives the new codec state. Geometry is checked
     * later, at setup. Serialise operations.
     * @param[in] scheme COMPRESSION_WEBP, asserted in debug builds.
     * @return One after installation, zero on field merge or outer allocation
     * failure.
     */
    extern int TIFFInitWebP(TIFF *tif, int scheme);
#endif
    /**
     * @brief Describe built-in compression names and initialisers.
     * @ingroup libtiff_private_codecs
     *
     * Immutable library-lifetime name-NULL terminated array; disabled entries
     * refer to a metadata-accepting stub. Borrowed storage must not be modified
     * or freed.
     */
    extern const TIFFCodec _TIFFBuiltinCODECS[];
    /**
     * @brief Convert 16-bit TIFF CIE L*a*b* samples to XYZ.
     * @ingroup libtiff_private_conversion
     *
     * Uses the stored reference white and the piecewise Lab inverse transform.
     * Chrominance is scaled by 256. Writes outputs without allocation, I/O,
     * diagnostics, or ownership transfer. Supply finite valid conversion state;
     * this void interface has no error status.
     *
     * @param[in] cielab Non-NULL caller-owned state initialised by
     * TIFFCIELabToRGBInit(); retained by caller.
     * @param[in] l Unsigned luminance in 0..65535.
     * @param[in] a Signed chrominance in -32768..32767, encoding 256*a*.
     * @param[in] b Signed chrominance with the same scale/range as a.
     * @param[out] X Non-NULL caller-owned output slot; must not alias the
     * conversion state.
     * @param[out] Y Non-NULL caller-owned output slot; must not alias the
     * conversion state.
     * @param[out] Z Non-NULL caller-owned output slot; must not alias the
     * conversion state.
     */
    extern void TIFFCIELab16ToXYZ(TIFFCIELabToRGB *cielab, uint32_t l,
                                  int32_t a, int32_t b, float *X, float *Y,
                                  float *Z);

    /**
     * @brief Allocate uninitialised bytes under a handle's allocation policy.
     * @ingroup libtiff_private_memory
     *
     * Use the same handle and accounting configuration for allocation,
     * reallocation, and release. A cumulatively tracked block contains a
     * private prefix and cannot be passed to _TIFFfree() or another handle's
     * allocator. Release all such blocks before releasing their handle. NULL
     * tif selects untracked allocation and is valid only with matching
     * untracked blocks.
     *
     * Positive configured limits are checked before allocation. Cumulative
     * accounting tracks payload bytes and updates only after successful
     * allocation; the prefix itself is not charged. Limit failures emit a
     * diagnostic. Nonpositive size and ordinary allocator exhaustion return
     * NULL without a direct diagnostic. Does not perform file I/O.
     *
     * @param[in,out] tif Live accounting handle, or NULL for untracked
     * allocation.
     * @param[in] s Requested payload byte count.
     * @return An owned block for _TIFFfreeExt(tif, ...), or NULL for
     * nonpositive size, a rejected limit/prefix check, or allocator failure.
     */
    extern void *_TIFFmallocExt(TIFF *tif, tmsize_t s);

    /**
     * @brief Allocate a zero-filled array under a handle's allocation policy.
     * @ingroup libtiff_private_memory
     *
     * Use the same handle and accounting configuration for allocation,
     * reallocation, and release. A cumulatively tracked block contains a
     * private prefix and cannot be passed to _TIFFfree() or another handle's
     * allocator. Release all such blocks before releasing their handle. NULL
     * tif selects untracked allocation and is valid only with matching
     * untracked blocks.
     *
     * Checks signed-size product representability before allocating. Limit
     * failures emit diagnostics; invalid/overflowing products and ordinary
     * allocator failure have no direct diagnostic. Successful cumulative
     * accounting charges only payload bytes. Does not retain inputs or
     * perform file I/O.
     *
     * @param[in,out] tif Live accounting handle, or NULL for untracked
     * allocation.
     * @param[in] nmemb Requested element count.
     * @param[in] siz Element size in bytes.
     * @return An owned zero-filled block for _TIFFfreeExt(), or NULL for
     * nonpositive operands, a product beyond TIFF_TMSIZE_T_MAX, limit/prefix
     * rejection, or allocator failure.
     */
    extern void *_TIFFcallocExt(TIFF *tif, tmsize_t nmemb, tmsize_t siz);

    /**
     * @brief Resize a handle-aware block or release it for a nonpositive size.
     * @ingroup libtiff_private_memory
     *
     * Use the same handle and accounting configuration for allocation,
     * reallocation, and release. A cumulatively tracked block contains a
     * private prefix and cannot be passed to _TIFFfree() or another handle's
     * allocator. Release all such blocks before releasing their handle. NULL
     * tif selects untracked allocation and is valid only with matching
     * untracked blocks.
     *
     * Preserves the initial min(old size, s) bytes on success; the old
     * pointer becomes invalid even if the address is unchanged. Grown bytes
     * are uninitialised. Check through a temporary result pointer.
     * Successful cumulative accounting replaces the old payload charge with
     * the new one. Limit failures emit diagnostics; ordinary exhaustion does
     * not directly report an error. Does not perform file I/O.
     *
     * @param[in,out] tif Matching live accounting handle, or NULL for an
     * untracked block.
     * @param[in] p Matching handle-aware allocation, or NULL.
     * @param[in] s New payload byte count; a nonpositive value releases p.
     * @return The owned replacement on positive-size success. NULL with
     * positive s preserves p and its accounting; NULL with s <= 0 means p
     * was released and is invalid.
     */
    extern void *_TIFFreallocExt(TIFF *tif, void *p, tmsize_t s);

    /**
     * @brief Release a handle-aware allocation and its payload charge.
     * @ingroup libtiff_private_memory
     *
     * Use the same handle and accounting configuration for allocation,
     * reallocation, and release. A cumulatively tracked block contains a
     * private prefix and cannot be passed to _TIFFfree() or another handle's
     * allocator. Release all such blocks before releasing their handle. NULL
     * tif selects untracked allocation and is valid only with matching
     * untracked blocks.
     *
     * NULL p is accepted. For a tracked block, reads its private prefix,
     * subtracts the recorded payload size, and frees the underlying
     * allocation. Invalidates all pointers into the block. There is no
     * status result or direct diagnostic; debug assertions check accounting
     * consistency. A wrong family/handle is not detected safely.
     *
     * @param[in,out] tif Matching live accounting handle, or NULL for an
     * untracked block.
     * @param[in] p Owned matching allocation, or NULL.
     */
    extern void _TIFFfreeExt(TIFF *tif, void *p);

#if defined(__cplusplus)
}
#endif
#endif /* _TIFFIOP_ */

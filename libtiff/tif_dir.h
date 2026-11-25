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

#ifndef _TIFFDIR_
#define _TIFFDIR_

#include "tiff.h"
#include "tiffio.h"

/**
 * @file tif_dir.h
 * @brief Define private directory storage, field schemas, and presence bits.
 */

/**
 * @brief Store one custom field value and its descriptor.
 * @ingroup libtiff_private_fields
 *
 * info is borrowed from the registered field table. The directory owns
 * value and frees it on replacement, unset, or directory cleanup. count is
 * an element count in the internal representation, not a file byte extent.
 * Partial setter failure can leave an entry with a NULL value.
 */
typedef struct
{
    /** @brief Borrowed live descriptor; must outlive this custom value. */
    const TIFFField *info;
    /**
     * @brief Number of internally stored elements; after a partial setter
     * failure it may be zero or a nonzero count with value NULL, so check value
     * before use.
     */
    int count;
    /** @brief Owned custom-value allocation, or NULL after clearing/failure. */
    void *value;
} TIFFTagValue;

/*
 * TIFF Image File Directories are comprised of a table of field
 * descriptors of the form shown below.  The table is sorted in
 * ascending order by tag.  The values associated with each entry are
 * disjoint and may appear anywhere in the file (so long as they are
 * placed on a word boundary).
 *
 * If the value is 4 bytes or less, in ClassicTIFF, or 8 bytes or less in
 * BigTIFF, then it is placed in the offset field to save space. If so,
 * it is left-justified in the offset field.
 */
/**
 * @brief Represent a parsed directory entry before value conversion.
 * @ingroup libtiff_private_fields
 *
 * Tag, type, and count are normalised for host access. The offset union
 * preserves raw inline bytes (at most four in classic TIFF, eight in
 * BigTIFF) or an external file offset; byte swapping depends on the
 * consumer. In particular deferred strile entries still need offset
 * swapping. This is an in-memory representation, not a structure to
 * serialise directly.
 */
typedef struct
{
    /** @brief Directory tag identifier. */
    uint16_t tdir_tag;
    /** @brief On-disk TIFFDataType code. */
    uint16_t tdir_type;

    /** @brief Number of file-type elements, normalised to host order. */
    uint64_t tdir_count;

    /** @brief Overlay raw directory inline bytes and offset-width views. */
    union
    {
        /** @brief Raw 16-bit view of inline bytes or offset storage. */
        uint16_t toff_short;
        /** @brief Raw 32-bit view of inline bytes or offset storage. */
        uint32_t toff_long;
        /** @brief Raw 64-bit view of inline bytes or offset storage. */
        uint64_t toff_long8;
    } tdir_offset;
    /**<
     * @brief Raw inline value bytes or external file offset; swap as required
     * by the consumer.
     */
    /**
     * @brief Nonzero to suppress this entry during parsing, e.g. duplicate
     * tags.
     */
    uint8_t tdir_ignore;
} TIFFDirEntry;

/**
 * @brief Describe an external directory-value byte range.
 * @ingroup libtiff_private_fields
 *
 * Used to estimate contiguous IFD data extent; this structure owns no file
 * data.
 */
typedef struct
{
    /** @brief External value offset in file bytes. */
    uint64_t offset;
    /** @brief External value length in bytes. */
    uint64_t length;
} TIFFEntryOffsetAndLength; /* auxiliary for evaluating size of IFD data */

/*
 * Internal format of a TIFF directory entry.
 */
/**
 * @brief Store the current directory values and image traversal state.
 * @ingroup libtiff_private_fields
 *
 * Embedded in a TIFF handle. Pointer arrays/strings and custom values are
 * owned by the matching handle allocator unless explicitly noted;
 * TIFFFreeDirectory() releases them. Field bits determine which values are
 * present. Defaults can exist with their presence bits clear. Use strile
 * accessors instead of directly dereferencing deferred offset/count arrays.
 * Resetting the structure without first freeing live allocations loses
 * ownership. See @ref directories for the current documentation boundary.
 */
typedef struct
{
/**
 * @brief Size of the presence bitmap in uint32_t words.
 * @ingroup libtiff_private_fields
 */
#define FIELDSET_ITEMS 4
    /* bit vector of fields that are set */
    /** @brief Presence bitmap indexed by FIELD_* bit numbers. */
    uint32_t td_fieldsset[FIELDSET_ITEMS];

    /** @brief Image width in pixels. */
    uint32_t td_imagewidth;
    /** @brief Image length in rows. */
    uint32_t td_imagelength;
    /** @brief Image depth in slices. */
    uint32_t td_imagedepth;
    /** @brief Tile width in pixels. */
    uint32_t td_tilewidth;
    /** @brief Tile length in rows. */
    uint32_t td_tilelength;
    /** @brief Tile depth in slices. */
    uint32_t td_tiledepth;
    /** @brief NewSubfileType flag mask. */
    uint32_t td_subfiletype;
    /** @brief Bits in each sample. */
    uint16_t td_bitspersample;
    /** @brief SAMPLEFORMAT_* interpretation of samples. */
    uint16_t td_sampleformat;
    /** @brief COMPRESSION_* codec identifier. */
    uint16_t td_compression;
    /** @brief PHOTOMETRIC_* interpretation. */
    uint16_t td_photometric;
    /** @brief THRESHHOLD_* bilevel rendering convention. */
    uint16_t td_threshholding;
    /** @brief FILLORDER_* bit order within file bytes. */
    uint16_t td_fillorder;
    /** @brief ORIENTATION_* storage orientation. */
    uint16_t td_orientation;
    /** @brief Samples per pixel, including extra samples. */
    uint16_t td_samplesperpixel;
    /** @brief Rows per strip; UINT32_MAX represents one whole-image strip. */
    uint32_t td_rowsperstrip;
    /** @brief Legacy unsigned minimum sample value. */
    uint16_t td_minsamplevalue;
    /** @brief Legacy unsigned maximum sample value. */
    uint16_t td_maxsamplevalue;
    /** @brief Owned SamplesPerPixel-length array of minimum values, or NULL. */
    double *td_sminsamplevalue;
    /** @brief Owned SamplesPerPixel-length array of maximum values, or NULL. */
    double *td_smaxsamplevalue;
    /** @brief Horizontal pixels per resolution unit. */
    float td_xresolution;
    /** @brief Vertical pixels per resolution unit. */
    float td_yresolution;
    /** @brief RESUNIT_* unit for resolution and position values. */
    uint16_t td_resolutionunit;
    /** @brief PLANARCONFIG_* sample-plane arrangement. */
    uint16_t td_planarconfig;
    /** @brief Horizontal image position in resolution units. */
    float td_xposition;
    /** @brief Vertical image position in resolution units. */
    float td_yposition;
    /** @brief Page index and total-page count as two uint16_t values. */
    uint16_t td_pagenumber[2];
    /**
     * @brief Three owned palette arrays with 2^BitsPerSample entries each, or
     * NULL slots.
     */
    uint16_t *td_colormap[3];
    /** @brief Highlight and shadow grayscale breakpoints. */
    uint16_t td_halftonehints[2];
    /** @brief Number of extra samples described by td_sampleinfo. */
    uint16_t td_extrasamples;
    /**
     * @brief Owned ExtraSamples interpretation array of td_extrasamples
     * entries, or NULL.
     */
    uint16_t *td_sampleinfo;
    /* strip support */
    /** @brief Current scanline index; UINT32_MAX before traversal starts. */
    uint32_t td_row;
    /** @brief Current strip index, or NOSTRIP. */
    uint32_t td_curstrip;

    /** @brief Cached decoded scanline size in bytes; zero when not computed. */
    tmsize_t td_scanlinesize;
/**
 * @brief Represent the absence of a current strip with UINT32_MAX.
 * @ingroup libtiff_private_fields
 */
#define NOSTRIP ((uint32_t)(-1)) /* undefined state */
    /* tiling support */
    /** @brief Current pixel column; UINT32_MAX before traversal starts. */
    uint32_t td_col;

    /** @brief Current tile index, or NOTILE. */
    uint32_t td_curtile;

    /** @brief Cached decoded tile size in bytes; minus one when invalidated. */
    tmsize_t td_tilesize;
/**
 * @brief Represent the absence of a current tile with UINT32_MAX.
 * @ingroup libtiff_private_fields
 */
#define NOTILE ((uint32_t)(-1)) /* undefined state */
    /* even though the name is misleading, td_stripsperimage is the number
     * of striles (=strips or tiles) per plane, and td_nstrips the total
     * number of striles */
    /** @brief Strips or tiles per sample plane, despite the historic name. */
    uint32_t td_stripsperimage;
    /** @brief Total strile count across all planes; logical array length. */
    uint32_t td_nstrips;
    /**
     * @brief Owned offset array/cache, possibly NULL or partial; use
     * TIFFGetStrileOffset().
     */
    uint64_t *td_stripoffset_p;
    /**
     * @brief Owned byte-count array/cache, possibly NULL or partial; use
     * TIFFGetStrileByteCount().
     */
    uint64_t *td_stripbytecount_p;
    /**
     * @brief Allocated element capacity of lazy strile offset/count caches, not
     * byte size.
     */
    uint32_t td_stripoffsetbyteallocsize;
#ifdef STRIPBYTECOUNTSORTED_UNUSED
    /**
     * @brief Nonzero when strile byte counts are ascending; optional unused
     * state.
     */
    int td_stripbytecountsorted;
#endif
    /* Be aware that the parameters of td_stripoffset_entry and
     * td_stripbytecount_entry are swapped but tdir_offset is not
     * and has to be swapped when used. */
    /**
     * @brief Deferred offset entry; tag/type/count are swapped but offset
     * storage is raw.
     */
    TIFFDirEntry td_stripoffset_entry;
    /**
     * @brief Deferred byte-count entry; offset storage still requires swapping.
     */
    TIFFDirEntry td_stripbytecount_entry;
    /** @brief Number of child-directory offsets in td_subifd. */
    uint16_t td_nsubifd;
    /** @brief Owned array of td_nsubifd child-directory offsets, or NULL. */
    uint64_t *td_subifd;
    /* YCbCr parameters */
    /** @brief Horizontal and vertical YCbCr subsampling factors. */
    uint16_t td_ycbcrsubsampling[2];
    /** @brief YCBCRPOSITION_* placement convention. */
    uint16_t td_ycbcrpositioning;
    /* Colorimetry parameters */
    /**
     * @brief Up to three owned gamma arrays with 2^BitsPerSample entries each.
     */
    uint16_t *td_transferfunction[3];
    /** @brief Owned six-float reference black/white array, or NULL. */
    float *td_refblackwhite;
    /* CMYK parameters */
    /**
     * @brief Byte length of the concatenated ink-name strings, including
     * terminators.
     */
    int td_inknameslen;
    /** @brief Owned concatenated null-terminated ink names, or NULL. */
    char *td_inknames;
    /**
     * @brief Number of inks, kept consistent with accepted InkNames where
     * possible.
     */
    uint16_t td_numberofinks;

    /**
     * @brief Number of live records in td_customValues, including partial
     * setter entries.
     */
    int td_customValueCount;
    /** @brief Owned array of custom-value records, each owning its value. */
    TIFFTagValue *td_customValues;

    /**
     * @brief Nonzero to reserve strile entries for later explicit array
     * writing.
     */
    unsigned char td_deferstrilearraywriting;

    /**
     * @brief Nonzero when the current directory has an on-file representation.
     */
    unsigned char td_iswrittentofile;

    /* LibTIFF writes all data that does not fit into the IFD entries directly
     * after the IFD tag entry part. When reading, only the IFD data directly
     * and continuously behind the IFD tags is taken into account for the IFD
     * data size.*/
    /**
     * @brief Accumulated IFD write size in bytes, including entry space after
     * counting.
     */
    uint64_t td_dirdatasize_write;

    /**
     * @brief Reusable IFD span including entry space and contiguous external
     * data; UINT64_MAX permits extension at EOF.
     */
    uint64_t td_dirdatasize_read;
    /** @brief Number of ranges collected in td_dirdatasize_offsets. */
    uint32_t td_dirdatasize_Noffsets;

    /** @brief Owned auxiliary array of external IFD-value ranges, or NULL. */
    TIFFEntryOffsetAndLength *td_dirdatasize_offsets;
} TIFFDirectory;

/**
 * @brief Zero directory storage and initialise traversal sentinels.
 * @ingroup libtiff_private_directory_navigation
 *
 * Does not free live owned pointers first; call only on fresh or already
 * released storage. Sets row/column and strip/tile indices to their
 * no-current sentinels, scanline size to zero, and tile size to minus one.
 * Does not install semantic tag defaults, perform I/O, allocate, or emit
 * diagnostics. No ownership is acquired.
 *
 * @param[in,out] td Non-NULL writable directory without live allocations.
 */
extern void _TIFFResetTifDirAndInitStrileCounters(TIFFDirectory *td);

/*
 * Field flags used to indicate fields that have been set in a directory, and
 * to reference fields when manipulating a directory.
 */

/*
 * FIELD_IGNORE is used to signify tags that are to be processed but otherwise
 * ignored.  This permits antiquated tags to be quietly read and discarded.
 * Note that a bit *is* allocated for ignored tags; this is understood by the
 * directory reading logic which uses this fact to avoid special-case handling
 */
/**
 * @brief Mark a processed-but-discarded tag; bit zero is reserved.
 * @ingroup libtiff_private_fields
 */
#define FIELD_IGNORE 0

/* multi-item fields */
/**
 * @brief Index the image dimensions presence bit; not an on-disk tag number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_IMAGEDIMENSIONS 1
/**
 * @brief Index the tile dimensions presence bit; not an on-disk tag number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_TILEDIMENSIONS 2
/**
 * @brief Index the resolution presence bit; not an on-disk tag number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_RESOLUTION 3
/**
 * @brief Index the position presence bit; not an on-disk tag number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_POSITION 4

/* single-item fields */
/**
 * @brief Index the subfile type presence bit; not an on-disk tag number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_SUBFILETYPE 5
/**
 * @brief Index the bits per sample presence bit; not an on-disk tag number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_BITSPERSAMPLE 6
/**
 * @brief Index the compression presence bit; not an on-disk tag number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_COMPRESSION 7
/**
 * @brief Index the photometric presence bit; not an on-disk tag number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_PHOTOMETRIC 8
/**
 * @brief Index the thresholding presence bit; not an on-disk tag number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_THRESHHOLDING 9
/**
 * @brief Index the fill order presence bit; not an on-disk tag number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_FILLORDER 10
/**
 * @brief Index the orientation presence bit; not an on-disk tag number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_ORIENTATION 15
/**
 * @brief Index the samples per pixel presence bit; not an on-disk tag number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_SAMPLESPERPIXEL 16
/**
 * @brief Index the rows per strip presence bit; not an on-disk tag number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_ROWSPERSTRIP 17
/**
 * @brief Index the minimum sample value presence bit; not an on-disk tag
 * number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_MINSAMPLEVALUE 18
/**
 * @brief Index the maximum sample value presence bit; not an on-disk tag
 * number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_MAXSAMPLEVALUE 19
/**
 * @brief Index the planar configuration presence bit; not an on-disk tag
 * number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_PLANARCONFIG 20
/**
 * @brief Index the resolution unit presence bit; not an on-disk tag number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_RESOLUTIONUNIT 22
/**
 * @brief Index the page number presence bit; not an on-disk tag number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_PAGENUMBER 23
/**
 * @brief Index the strile byte counts presence bit; not an on-disk tag number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_STRIPBYTECOUNTS 24
/**
 * @brief Index the strile offsets presence bit; not an on-disk tag number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_STRIPOFFSETS 25
/**
 * @brief Index the colormap presence bit; not an on-disk tag number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_COLORMAP 26
/**
 * @brief Index the extra samples presence bit; not an on-disk tag number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_EXTRASAMPLES 31
/**
 * @brief Index the sample format presence bit; not an on-disk tag number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_SAMPLEFORMAT 32
/**
 * @brief Index the per-sample minimum presence bit; not an on-disk tag number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_SMINSAMPLEVALUE 33
/**
 * @brief Index the per-sample maximum presence bit; not an on-disk tag number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_SMAXSAMPLEVALUE 34
/**
 * @brief Index the image depth presence bit; not an on-disk tag number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_IMAGEDEPTH 35
/**
 * @brief Index the tile depth presence bit; not an on-disk tag number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_TILEDEPTH 36
/**
 * @brief Index the halftone hints presence bit; not an on-disk tag number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_HALFTONEHINTS 37
/**
 * @brief Index the YCbCr subsampling presence bit; not an on-disk tag number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_YCBCRSUBSAMPLING 39
/**
 * @brief Index the YCbCr positioning presence bit; not an on-disk tag number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_YCBCRPOSITIONING 40
/**
 * @brief Index the reference black/white presence bit; not an on-disk tag
 * number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_REFBLACKWHITE 41
/**
 * @brief Index the transfer function presence bit; not an on-disk tag number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_TRANSFERFUNCTION 44
/**
 * @brief Index the ink names presence bit; not an on-disk tag number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_INKNAMES 46
/**
 * @brief Index the child IFD presence bit; not an on-disk tag number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_SUBIFD 49
/**
 * @brief Index the number of inks presence bit; not an on-disk tag number.
 * @ingroup libtiff_private_fields
 */
#define FIELD_NUMBEROFINKS 50
/*      FIELD_CUSTOM (see tiffio.h)    65 */
/* end of support for well-known tags; codec-private tags follow */
/**
 * @brief Start the range reserved for codec-private presence bits.
 * @ingroup libtiff_private_fields
 */
#define FIELD_CODEC 66 /* base of codec-private tags */

/*
 * Pseudo-tags don't normally need field bits since they are not written to an
 * output file (by definition). The library also has express logic to always
 * query a codec for a pseudo-tag so allocating a field bit for one is a
 * waste.   If codec wants to promote the notion of a pseudo-tag being ``set''
 * or ``unset'' then it can do using internal state flags without polluting
 * the field bit space defined for real tags.
 */
/**
 * @brief Share bit zero with FIELD_IGNORE for pseudo-tags.
 * @ingroup libtiff_private_fields
 *
 * Getters skip the presence test because of the pseudo-tag number range
 * (isPseudoTag()), not because of this value; bit zero may still be set by the
 * generic setter.
 */
#define FIELD_PSEUDO 0

/**
 * @brief Largest valid presence bit index in the bitmap.
 * @ingroup libtiff_private_fields
 */
#define FIELD_LAST (32 * FIELDSET_ITEMS - 1)

/**
 * @brief Construct a one-bit mask within a 32-bit presence word.
 * @ingroup libtiff_private_fields
 *
 * No allocation, ownership change, diagnostic, or locking occurs. Arguments
 * must identify valid bitmap storage.
 *
 * @param[in] n Nonnegative bit index; low five bits select the mask.
 * @return Unsigned nonzero bit mask; no state change.
 */
#define BITn(n) (1U << ((n) & 0x1f))
/**
 * @brief Select the lvalue word containing a presence bit.
 * @ingroup libtiff_private_fields
 *
 * No allocation, ownership change, diagnostic, or locking occurs. Arguments
 * must identify valid bitmap storage.
 *
 * @param[in,out] tif Non-NULL live handle; expression is evaluated once.
 * @param[in] n Index from zero through FIELD_LAST; evaluated once.
 * @return Lvalue uint32_t word; no range check or allocation.
 */
#define BITFIELDn(tif, n) ((tif)->tif_dir.td_fieldsset[(n) / 32])
/**
 * @brief Test a directory presence bit.
 * @ingroup libtiff_private_fields
 *
 * No allocation, ownership change, diagnostic, or locking occurs. Arguments
 * must identify valid bitmap storage.
 *
 * @param[in] tif Non-NULL handle expression, evaluated once.
 * @param[in] field Valid bit index; evaluated twice, so avoid side effects.
 * @return Nonzero mask if set, zero otherwise; does not modify state.
 */
#define TIFFFieldSet(tif, field) (BITFIELDn(tif, field) & BITn(field))
/**
 * @brief Set a directory presence bit.
 * @ingroup libtiff_private_fields
 *
 * No allocation, ownership change, diagnostic, or locking occurs. Arguments
 * must identify valid bitmap storage.
 *
 * @param[in,out] tif Non-NULL handle expression, evaluated once.
 * @param[in] field Valid bit index; evaluated twice, so avoid side effects.
 * @return Updated bitmap word; no validation/diagnostic and no dirty flag
 * change.
 */
#define TIFFSetFieldBit(tif, field) (BITFIELDn(tif, field) |= BITn(field))
/**
 * @brief Clear a directory presence bit.
 * @ingroup libtiff_private_fields
 *
 * No allocation, ownership change, diagnostic, or locking occurs. Arguments
 * must identify valid bitmap storage.
 *
 * @param[in,out] tif Non-NULL handle expression, evaluated once.
 * @param[in] field Valid bit index; evaluated twice, so avoid side effects.
 * @return Updated bitmap word; does not free/reset stored values or set
 * dirty state.
 */
#define TIFFClrFieldBit(tif, field) (BITFIELDn(tif, field) &= ~BITn(field))

/**
 * @brief Test a bit in a supplied presence bitmap.
 * @ingroup libtiff_private_fields
 *
 * No allocation, ownership change, diagnostic, or locking occurs. Arguments
 * must identify valid bitmap storage.
 *
 * @param[in] fields Readable subscriptable bitmap expression with sufficient
 * words; evaluated once. Parenthesise expressions that need it before
 * subscripting.
 * @param[in] f Valid nonnegative bit index; evaluated twice, so avoid side
 * effects.
 * @return Nonzero mask if set, zero otherwise; no bounds check or state
 * change.
 */
#define FieldSet(fields, f) (fields[(f) / 32] & BITn(f))
/**
 * @brief Clear a bit in a supplied presence bitmap.
 * @ingroup libtiff_private_fields
 *
 * No allocation, ownership change, diagnostic, or locking occurs. Arguments
 * must identify valid bitmap storage.
 *
 * @param[in,out] fields Writable bitmap with sufficient words; evaluated
 * once.
 * @param[in] f Valid nonnegative bit index; evaluated twice, so avoid side
 * effects.
 * @return Updated bitmap word; no validation or resource cleanup.
 */
#define ResetFieldBit(fields, f) (fields[(f) / 32] &= ~BITn(f))

/**
 * @brief Encode the internal value and variadic count representation.
 * @ingroup libtiff_private_fields
 *
 * The suffix selects element storage, not an on-disk TIFFDataType. C0 uses
 * an array with implicit/fixed count, C16 passes a 16-bit count, and C32
 * passes a 32-bit count. Unprefixed numeric types normally select scalar
 * values. Getters receive output pointers; setters apply C default argument
 * promotions to scalar values. Built-in exceptions still require tag-
 * specific dispatch; see @ref tag_arguments. These encodings alone cannot
 * describe every calling convention.
 */
typedef enum
{
    /** @brief Unsupported or unspecified argument representation. */
    TIFF_SETGET_UNDEFINED = 0,
    /** @brief Null-terminated string representation. */
    TIFF_SETGET_ASCII = 1,
    /** @brief Unsigned 8-bit scalar storage. */
    TIFF_SETGET_UINT8 = 2,
    /** @brief Signed 8-bit scalar storage. */
    TIFF_SETGET_SINT8 = 3,
    /** @brief Unsigned 16-bit scalar storage. */
    TIFF_SETGET_UINT16 = 4,
    /** @brief Signed 16-bit scalar storage. */
    TIFF_SETGET_SINT16 = 5,
    /** @brief Unsigned 32-bit scalar storage. */
    TIFF_SETGET_UINT32 = 6,
    /** @brief Signed 32-bit scalar storage. */
    TIFF_SETGET_SINT32 = 7,
    /** @brief Unsigned 64-bit scalar storage. */
    TIFF_SETGET_UINT64 = 8,
    /** @brief Signed 64-bit scalar storage. */
    TIFF_SETGET_SINT64 = 9,
    /** @brief Float scalar storage. */
    TIFF_SETGET_FLOAT = 10,
    /** @brief Double scalar storage. */
    TIFF_SETGET_DOUBLE = 11,
    /** @brief 64-bit directory-offset scalar storage. */
    TIFF_SETGET_IFD8 = 12,
    /** @brief Promoted int scalar representation. */
    TIFF_SETGET_INT = 13,
    /** @brief Two separate promoted uint16_t values / uint16_t output slots. */
    TIFF_SETGET_UINT16_PAIR = 14,
    /** @brief Character array with implicit count. */
    TIFF_SETGET_C0_ASCII = 15,
    /** @brief Unsigned 8-bit array with implicit count. */
    TIFF_SETGET_C0_UINT8 = 16,
    /** @brief Signed 8-bit array with implicit count. */
    TIFF_SETGET_C0_SINT8 = 17,
    /** @brief Unsigned 16-bit array with implicit count. */
    TIFF_SETGET_C0_UINT16 = 18,
    /** @brief Signed 16-bit array with implicit count. */
    TIFF_SETGET_C0_SINT16 = 19,
    /** @brief Unsigned 32-bit array with implicit count. */
    TIFF_SETGET_C0_UINT32 = 20,
    /** @brief Signed 32-bit array with implicit count. */
    TIFF_SETGET_C0_SINT32 = 21,
    /** @brief Unsigned 64-bit array with implicit count. */
    TIFF_SETGET_C0_UINT64 = 22,
    /** @brief Signed 64-bit array with implicit count. */
    TIFF_SETGET_C0_SINT64 = 23,
    /** @brief Float array with implicit count. */
    TIFF_SETGET_C0_FLOAT = 24,
    /** @brief Double array with implicit count. */
    TIFF_SETGET_C0_DOUBLE = 25,
    /** @brief 64-bit directory-offset array with implicit count. */
    TIFF_SETGET_C0_IFD8 = 26,
    /** @brief Character array with 16-bit explicit count. */
    TIFF_SETGET_C16_ASCII = 27,
    /** @brief Unsigned 8-bit array with 16-bit explicit count. */
    TIFF_SETGET_C16_UINT8 = 28,
    /** @brief Signed 8-bit array with 16-bit explicit count. */
    TIFF_SETGET_C16_SINT8 = 29,
    /** @brief Unsigned 16-bit array with 16-bit explicit count. */
    TIFF_SETGET_C16_UINT16 = 30,
    /** @brief Signed 16-bit array with 16-bit explicit count. */
    TIFF_SETGET_C16_SINT16 = 31,
    /** @brief Unsigned 32-bit array with 16-bit explicit count. */
    TIFF_SETGET_C16_UINT32 = 32,
    /** @brief Signed 32-bit array with 16-bit explicit count. */
    TIFF_SETGET_C16_SINT32 = 33,
    /** @brief Unsigned 64-bit array with 16-bit explicit count. */
    TIFF_SETGET_C16_UINT64 = 34,
    /** @brief Signed 64-bit array with 16-bit explicit count. */
    TIFF_SETGET_C16_SINT64 = 35,
    /** @brief Float array with 16-bit explicit count. */
    TIFF_SETGET_C16_FLOAT = 36,
    /** @brief Double array with 16-bit explicit count. */
    TIFF_SETGET_C16_DOUBLE = 37,
    /** @brief 64-bit directory-offset array with 16-bit explicit count. */
    TIFF_SETGET_C16_IFD8 = 38,
    /** @brief Character array with 32-bit explicit count. */
    TIFF_SETGET_C32_ASCII = 39,
    /** @brief Unsigned 8-bit array with 32-bit explicit count. */
    TIFF_SETGET_C32_UINT8 = 40,
    /** @brief Signed 8-bit array with 32-bit explicit count. */
    TIFF_SETGET_C32_SINT8 = 41,
    /** @brief Unsigned 16-bit array with 32-bit explicit count. */
    TIFF_SETGET_C32_UINT16 = 42,
    /** @brief Signed 16-bit array with 32-bit explicit count. */
    TIFF_SETGET_C32_SINT16 = 43,
    /** @brief Unsigned 32-bit array with 32-bit explicit count. */
    TIFF_SETGET_C32_UINT32 = 44,
    /** @brief Signed 32-bit array with 32-bit explicit count. */
    TIFF_SETGET_C32_SINT32 = 45,
    /** @brief Unsigned 64-bit array with 32-bit explicit count. */
    TIFF_SETGET_C32_UINT64 = 46,
    /** @brief Signed 64-bit array with 32-bit explicit count. */
    TIFF_SETGET_C32_SINT64 = 47,
    /** @brief Float array with 32-bit explicit count. */
    TIFF_SETGET_C32_FLOAT = 48,
    /** @brief Double array with 32-bit explicit count. */
    TIFF_SETGET_C32_DOUBLE = 49,
    /** @brief 64-bit directory-offset array with 32-bit explicit count. */
    TIFF_SETGET_C32_IFD8 = 50,
    /** @brief Tag-specific calling convention outside the regular encodings. */
    TIFF_SETGET_OTHER = 51
} TIFFSetGetFieldType;

#if defined(__cplusplus)
extern "C"
{
#endif

    /**
     * @brief Return the built-in image field schema.
     * @ingroup libtiff_private_fields
     *
     * Returns static borrowed storage for the library lifetime; do not
     * modify/free the array or its descriptors. No allocation, diagnostics, or
     * state changes occur.
     *
     * @return Non-NULL pointer to the static schema.
     */
    extern const TIFFFieldArray *_TIFFGetFields(void);

    /**
     * @brief Return the built-in EXIF field schema.
     * @ingroup libtiff_private_fields
     *
     * Returns static borrowed storage for the library lifetime; do not
     * modify/free the array or its descriptors. No allocation, diagnostics, or
     * state changes occur.
     *
     * @return Non-NULL pointer to the static schema.
     */
    extern const TIFFFieldArray *_TIFFGetExifFields(void);

    /**
     * @brief Return the built-in GPS field schema.
     * @ingroup libtiff_private_fields
     *
     * Returns static borrowed storage for the library lifetime; do not
     * modify/free the array or its descriptors. No allocation, diagnostics, or
     * state changes occur.
     *
     * @return Non-NULL pointer to the static schema.
     */
    extern const TIFFFieldArray *_TIFFGetGpsFields(void);

    /**
     * @brief Replace the registered field-pointer table with a schema.
     * @ingroup libtiff_private_fields
     *
     * Disposes old anonymous descriptors/names and the old pointer array when
     * the table is non-empty, then merges the supplied borrowed descriptors. A
     * table left with tif_nfields zero by an earlier _TIFFMergeFields()
     * allocation failure is not released, and the next merge allocates a new
     * array over it, so the old array and its anonymous descriptors leak.
     * Ordinary field descriptors and names remain externally owned. Allocation
     * failure is diagnosed without restoring the old table; this void interface
     * cannot report status. Caller must dispose any directory values that still
     * refer to removed descriptors before replacement.
     *
     * @param[in,out] tif Non-NULL live TIFF handle; accessed without internal
     * locking.
     * @param[in] fieldarray Non-NULL schema whose descriptors and names remain
     * live while registered.
     */
    extern void _TIFFSetupFields(TIFF *tif, const TIFFFieldArray *fieldarray);

    /**
     * @brief Print the registered field metadata to a stream.
     * @ingroup libtiff_private_fields
     *
     * Writes diagnostic text without modifying the field table or taking
     * ownership of the stream. Does not check/report stdio failures and returns
     * no status.
     *
     * @param[in] tif Non-NULL live handle with valid field names.
     * @param[in,out] fd Non-NULL writable borrowed stdio stream.
     */
    extern void _TIFFPrintFieldInfo(TIFF *tif, FILE *fd);

    /**
     * @brief Materialise deferred strile arrays when required.
     * @ingroup libtiff_private_strile_metadata
     *
     * Delegates to _TIFFFillStrilesInternal() with byte-count loading enabled.
     * Lazy mode discards partial caches on first full load; replaces/allocates
     * handle-owned arrays and may do I/O or report diagnostics. Clears deferred
     * entries after the fetch attempt, including failure, so retry is not
     * equivalent to the first call. Already loaded/chopped/non-deferred cases
     * return one without validating all array contents. Borrowed array pointers
     * may be invalidated. No ownership transfers.
     *
     * @param[in,out] tif Non-NULL live handle with matching schema/state;
     * serialise access externally.
     * @return One for a completed load or no work, zero on detected load
     * failure; partial arrays/state may remain.
     */
    extern int _TIFFFillStriles(TIFF *tif);

    /**
     * @brief Identify the namespace described by a field array.
     * @ingroup libtiff_private_fields
     */
    typedef enum
    {
        /** @brief Main image-directory schema. */
        tfiatImage,
        /** @brief EXIF metadata-directory schema. */
        tfiatExif,
        /** @brief EXIF GPS metadata-directory schema. */
        tfiatGps,

        /** @brief Other application/compatibility schema. */
        tfiatOther
    } TIFFFieldArrayType;

    /**
     * @brief Describe a borrowed schema or owned compatibility batch.
     * @ingroup libtiff_private_fields
     *
     * Built-in arrays have static fields and zero allocated_size. Compatibility
     * batches own their descriptor storage but borrow names;
     * TIFFDefaultDirectory() (on each directory setup) and handle cleanup
     * release the batch allocations, so legacy descriptors must be
     * re-registered, typically from a tag extender, for each directory.
     * Registration itself stores pointers into fields, so descriptor storage
     * must outlive every lookup/value referring to it.
     */
    struct _TIFFFieldArray
    {
        /** @brief Namespace/category of the schema. */
        TIFFFieldArrayType type;

        /**
         * @brief Descriptor capacity when owned; zero identifies a static
         * schema.
         */
        uint32_t allocated_size;
        /** @brief Number of valid descriptors in fields. */
        uint32_t count;

        /**
         * @brief Descriptor array; static/borrowed or owned according to
         * allocated_size and creator.
         */
        TIFFField *fields;
    };

    /**
     * @brief Describe a field's file type and internal argument contract.
     * @ingroup libtiff_private_fields
     *
     * Built-in descriptors/names are static. Ordinary merged descriptors/names
     * remain borrowed; anonymous descriptors own generated names and are freed
     * by field-table cleanup. Legacy compatibility descriptors are handle-owned
     * and freed at each TIFFDefaultDirectory() call; their names remain
     * borrowed. Do not mutate a registered descriptor:
     * sorted lookup and stored values rely on it.
     */
    struct _TIFFField
    {
        /** @brief Tag identifier in this schema namespace. */
        uint32_t field_tag;

        /**
         * @brief Fixed getter count or TIFF_VARIABLE/TIFF_VARIABLE2/TIFF_SPP
         * rule.
         */
        short field_readcount;
        /** @brief Fixed setter count or variable/sample-count sentinel. */
        short field_writecount;

        /**
         * @brief On-disk TIFFDataType, distinct from the argument
         * representation.
         */
        TIFFDataType field_type;
        /**
         * @brief Nonzero for an automatically created unknown-tag descriptor.
         */
        uint32_t field_anonymous;
        /**
         * @brief Internal storage/count representation for tag access and
         * conversion.
         */
        TIFFSetGetFieldType set_get_field_type;
        /**
         * @brief FIELD_* presence index; FIELD_CUSTOM shares storage through
         * the custom list.
         */
        unsigned short field_bit;
        /**
         * @brief Nonzero to permit changes after writing starts; ImageLength is
         * separately exempt.
         */
        unsigned char field_oktochange;
        /**
         * @brief Nonzero for an explicit count argument; tag-specific
         * exceptions still apply.
         */
        unsigned char field_passcount;
        /**
         * @brief Borrowed or anonymous-owned null-terminated name; must remain
         * live while used.
         */
        const char *field_name;
        /**
         * @brief Borrowed child-directory schema, or NULL; not recursively
         * freed here.
         */
        TIFFFieldArray *field_subfields;
    };

    /**
     * @brief Merge borrowed field definitions into the lookup table.
     * @ingroup libtiff_private_fields
     *
     * Clears the lookup cache and grows/sorts the owned pointer table. Stores
     * descriptor pointers, not copies; names and records must remain live and
     * stable while registered. Existing tag numbers win regardless of type.
     * Allocation failure is diagnosed and sets tif_nfields to zero even when
     * old pointer storage survives; that storage and the anonymous descriptors
     * it lists are then never freed (see _TIFFSetupFields()). No rollback is
     * promised. No directory
     * values are created.
     *
     * @param[in,out] tif Non-NULL live TIFF handle; accessed without internal
     * locking.
     * @param[in] info Readable array of n long-lived descriptors.
     * @param[in] n Positive descriptor count; allocation product and int result
     * must be representable.
     * @return Requested n cast to int on success, not the number actually
     * added; zero on allocation failure (also the ambiguous zero-count result).
     */
    extern int _TIFFMergeFields(TIFF *tif, const TIFFField info[], uint32_t n);

    /**
     * @brief Find a typed field or register an anonymous replacement.
     * @ingroup libtiff_private_fields
     *
     * Existing matches remain borrowed. If absent, allocates an anonymous
     * descriptor/name and merges it into the handle table, which owns it on
     * success. Merge failure follows _TIFFMergeFields() state changes and does
     * not free the newly allocated descriptor/name in this implementation. No
     * new ownership is returned on failure; this path can leak. Diagnostics
     * follow the allocation/merge helpers. Existing tag numbers prevent a
     * different type from actually being added, so use only for genuinely
     * unknown tags or matching registered types (see @return).
     *
     * @param[in,out] tif Non-NULL live TIFF handle; accessed without internal
     * locking.
     * @param[in] tag Tag number to find/register.
     * @param[in] dt Required on-disk type.
     * @return Borrowed descriptor on success, or NULL on allocation/merge
     * failure; valid until relevant table/handle cleanup. If the tag is
     * already registered with a different type, the returned descriptor is a
     * new anonymous descriptor that was not added to the table; it is not
     * released by field-table or handle cleanup (it leaks), and later lookups
     * return the existing descriptor.
     */
    extern const TIFFField *_TIFFFindOrRegisterField(TIFF *tif, uint32_t tag,
                                                     TIFFDataType dt);

    /**
     * @brief Allocate a counted anonymous field descriptor.
     * @ingroup libtiff_private_fields
     *
     * Allocates a descriptor and a 32-byte generated name with the matching Ext
     * allocator. Uses FIELD_CUSTOM, TIFF_VARIABLE2, a C32 representation, and
     * changeable/pass-count flags. It does not register the descriptor. Before
     * registration the caller owns both allocations and must release name then
     * descriptor with _TIFFfreeExt(tif,...); registered anonymous storage is
     * released by field-table/handle cleanup. Name-allocation failure frees the
     * descriptor. Allocation-policy diagnostics may occur; raw exhaustion can
     * be silent.
     *
     * @param[in,out] tif Non-NULL live TIFF handle; accessed without internal
     * locking.
     * @param[in] tag Unknown tag identifier.
     * @param[in] field_type On-disk data type; unsupported values receive an
     * undefined representation.
     * @return Owned descriptor, or NULL on allocation failure.
     */
    extern TIFFField *_TIFFCreateAnonField(TIFF *tif, uint32_t tag,
                                           TIFFDataType field_type);

    /**
     * @brief Filter recognised codec-specific tags against current compression.
     * @ingroup libtiff_private_fields
     *
     * Checks the explicit predictor/JPEG/OJPEG/CCITT/LERC tag list and whether
     * its associated codec is configured. All unlisted tags pass without schema
     * validation. Does not allocate, mutate state, or emit diagnostics.
     *
     * @param[in] tif Non-NULL live handle with current compression.
     * @param[in] tag Tag identifier to filter.
     * @return One for an unlisted tag or a compatible configured codec; zero
     * for a recognised incompatible/unconfigured codec tag.
     */
    extern int _TIFFCheckFieldIsValidForCodec(TIFF *tif, ttag_t tag);
    /**
     * @brief Register or validate a chain index/offset pair for loop detection.
     * @ingroup libtiff_private_directory_navigation
     *
     * A known offset at a different index warns and fails; the same pair
     * succeeds. A known index can be rebound to a new offset, removing the old
     * shared record first. Enforces TIFF_MAX_DIR_COUNT when adding a new pair.
     * Borrows the handle; no values or entry pointers are transferred to the
     * caller. The offset map owns shared malloc-allocated records and the
     * number map borrows them. Allocations use libc/hash storage rather than
     * per-handle allocation limits. Failures are not transactional: maps can be
     * only partly created/updated. Hash removal results are ignored, so
     * allocation failures during removal cannot be inferred from this result
     * and map consistency is not guaranteed on those paths. Creating either
     * hash map unsuccessfully reports an error but returns one, allowing
     * traversal without complete loop tracking. Other allocation/insertion
     * failures return zero, sometimes without a direct diagnostic. Callers
     * requiring recovery must discard/rebuild maps rather than assuming
     * unchanged state.
     *
     * @param[in,out] tif Non-NULL live handle; caller must serialise access.
     * @param[in] dirn Chain-relative zero-based index.
     * @param[in] diroff Nonzero IFD offset in bytes; zero means chain end.
     * @return One for an accepted pair or map-creation exhaustion; zero for
     * offset zero, loop/limit detection, or other detected allocation/insertion
     * failure.
     */
    extern int _TIFFCheckDirNumberAndOffset(TIFF *tif, tdir_t dirn,
                                            uint64_t diroff);
    /**
     * @brief Resolve a directory offset using the chain maps.
     * @ingroup libtiff_private_directory_navigation
     *
     * Returns immediately for offset zero or a missing offset map. If a map
     * exists but lacks the offset, calls TIFFNumberOfDirectories() to scan the
     * main chain and tries again; that can allocate, change maps/counts, move
     * I/O position, and dispatch diagnostics. Does not load tag values. Output
     * is assigned only on success; no ownership transfers.
     *
     * @param[in,out] tif Non-NULL live handle; caller must serialise access.
     * @param[in] diroff Directory offset in bytes.
     * @param[out] dirn Non-NULL index output; unchanged when no match is found.
     * @return One with a matching cached/scanned index, zero if absent; a zero
     * result does not distinguish absence from scan failure.
     */
    extern int _TIFFGetDirNumberFromOffset(TIFF *tif, uint64_t diroff,
                                           tdir_t *dirn);
    /**
     * @brief Resolve an index from the existing number-to-offset map.
     * @ingroup libtiff_private_directory_navigation
     *
     * Does not populate a missing map, scan the file, allocate, emit
     * diagnostics, or transfer ownership. A match is scoped to the currently
     * cached chain and is not independent validation of file contents.
     *
     * @param[in] tif Non-NULL live handle; caller must serialise access.
     * @param[in] dirn Chain-relative zero-based index.
     * @param[out] diroff Non-NULL byte-offset output; unchanged on absence.
     * @return One on a cached match, zero if map/key is absent.
     */
    extern int _TIFFGetOffsetFromDirNumber(TIFF *tif, tdir_t dirn,
                                           uint64_t *diroff);
    /**
     * @brief Attempt to remove a shared directory-map entry by offset.
     * @ingroup libtiff_private_directory_navigation
     *
     * Treats absence as success. With both matching entries, attempts removal
     * from the borrowing number map before removing/freeing the owning
     * offset-map entry. A missing number map when the offset entry exists
     * reports an error. Individual hash removal results are ignored; shrink
     * allocation can therefore fail while this function still returns one, and
     * failed first removal can leave a dangling borrowed entry if the owning
     * removal succeeds. If the number map exists without the expected key, the
     * offset entry is left untouched and one is returned. Do not use success as
     * proof of removal; recovery may require disposing/rebuilding both maps.
     * Does not change directory values/file contents or transfer ownership.
     *
     * @param[in,out] tif Non-NULL live handle; caller must serialise access.
     * @param[in] diroff Byte-offset key to remove; need not already be present.
     * @return Zero only for an existing offset entry with a missing number map;
     * one otherwise, including ignored removal failures/inconsistent keys.
     */
    extern int _TIFFRemoveEntryFromDirectoryListByOffset(TIFF *tif,
                                                         uint64_t diroff);

#if defined(__cplusplus)
}
#endif
#endif /* _TIFFDIR_ */

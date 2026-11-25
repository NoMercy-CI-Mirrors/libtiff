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
 * @file tiff.h
 * @brief Define TIFF file-format types, tag identifiers, and constant values.
 *
 * A defined tag or compression identifier does not imply that this library
 * implements that extension or codec. Tag-specific API contracts are described
 * with the directory interfaces and in the Sphinx manual.
 */

#ifndef _TIFF_
#define _TIFF_

#include "tiffconf.h"

/*
 * Tag Image File Format (TIFF)
 *
 * Based on Rev 6.0 from:
 *    Developer's Desk
 *    Aldus Corporation
 *    411 First Ave. South
 *    Suite 200
 *    Seattle, WA  98104
 *    206-622-5500
 *
 *    (http://partners.adobe.com/asn/developer/PDFS/TN/TIFF6.pdf)
 *
 * For BigTIFF design notes see the following links
 *    http://www.remotesensing.org/libtiff/bigtiffdesign.html
 *    http://www.awaresystems.be/imaging/tiff/bigtiff.html
 */

/** @addtogroup libtiff_public_types
 * @{ */

/** @brief Identify the classic TIFF file format in a file header. */
#define TIFF_VERSION_CLASSIC 42
/** @brief Identify the BigTIFF file format in a file header. */
#define TIFF_VERSION_BIG 43

/** @brief Identify big-endian TIFF byte order (the bytes MM). */
#define TIFF_BIGENDIAN 0x4d4d
/** @brief Identify little-endian TIFF byte order (the bytes II). */
#define TIFF_LITTLEENDIAN 0x4949
/**
 * @brief Microsoft Document Imaging signature bytes (EP) read on a
 * little-endian host.
 */
#define MDI_LITTLEENDIAN 0x5045
/**
 * @brief Microsoft Document Imaging signature bytes (EP) read on a big-endian
 * host.
 *
 * Both MDI constants describe the same on-disk bytes. When MDI_SUPPORT is
 * enabled, the reader compares the constant matching the host byte order. MDI
 * data is always little-endian.
 */
#define MDI_BIGENDIAN 0x4550

/*
 * Intrinsic data types required by the file format:
 *
 * 8-bit quantities     int8_t/uint_8_t
 * 16-bit quantities    int16_t/uint_16_t
 * 32-bit quantities    int32_t/uint_32_t
 * 64-bit quantities    int64_t/uint_64_t
 * strings              unsigned char*
 */
/** @brief Mark legacy integer typedefs as deprecated on GNU-compatible
 * compilers.
 *
 * Expands to an attribute when supported and to nothing otherwise.
 */
#ifdef __GNUC__
#define TIFF_GCC_DEPRECATED __attribute__((deprecated))
#else
#define TIFF_GCC_DEPRECATED
#endif
/** @brief Mark legacy integer typedefs as deprecated with Microsoft compilers.
 *
 * Expands to a declaration specifier when supported and to nothing otherwise.
 */
#ifdef _MSC_VER
#define TIFF_MSC_DEPRECATED                                                    \
    __declspec(deprecated("libtiff type deprecated; please use corresponding " \
                          "C99 stdint.h type"))
#else
#define TIFF_MSC_DEPRECATED
#endif

#ifndef TIFF_DISABLE_DEPRECATED
/** @brief Provide the legacy signed 8-bit integer type.
 * @deprecated Use int8_t instead. */
typedef TIFF_MSC_DEPRECATED int8_t int8 TIFF_GCC_DEPRECATED;
/** @brief Provide the legacy unsigned 8-bit integer type.
 * @deprecated Use uint8_t instead. */
typedef TIFF_MSC_DEPRECATED uint8_t uint8 TIFF_GCC_DEPRECATED;

/** @brief Provide the legacy signed 16-bit integer type.
 * @deprecated Use int16_t instead. */
typedef TIFF_MSC_DEPRECATED int16_t int16 TIFF_GCC_DEPRECATED;
/** @brief Provide the legacy unsigned 16-bit integer type.
 * @deprecated Use uint16_t instead. */
typedef TIFF_MSC_DEPRECATED uint16_t uint16 TIFF_GCC_DEPRECATED;

/** @brief Provide the legacy signed 32-bit integer type.
 * @deprecated Use int32_t instead. */
typedef TIFF_MSC_DEPRECATED int32_t int32 TIFF_GCC_DEPRECATED;
/** @brief Provide the legacy unsigned 32-bit integer type.
 * @deprecated Use uint32_t instead. */
typedef TIFF_MSC_DEPRECATED uint32_t uint32 TIFF_GCC_DEPRECATED;

/** @brief Provide the legacy signed 64-bit integer type.
 * @deprecated Use int64_t instead. */
typedef TIFF_MSC_DEPRECATED int64_t int64 TIFF_GCC_DEPRECATED;
/** @brief Provide the legacy unsigned 64-bit integer type.
 * @deprecated Use uint64_t instead. */
typedef TIFF_MSC_DEPRECATED uint64_t uint64 TIFF_GCC_DEPRECATED;
#endif /* TIFF_DISABLE_DEPRECATED */

/*
 * Some types as promoted in a variable argument list
 * We use uint16_vap rather then directly using int, because this way
 * we document the type we actually want to pass through, conceptually,
 * rather then confusing the issue by merely stating the type it gets
 * promoted to
 */

/**
 * @brief Represent a uint16_t value after default variadic argument promotion.
 *
 * The typedef names the conceptual unsigned 16-bit value; the argument is
 * passed as int in a variadic call on the supported platforms.
 */
typedef int uint16_vap;

/*
 * TIFF header.
 */
/** @brief Represent the common prefix of classic TIFF and BigTIFF headers.
 *
 * Integer members are interpreted in host byte order after header decoding.
 */
typedef struct
{
    /** @brief File byte-order signature. */
    uint16_t tiff_magic;
    /** @brief TIFF_VERSION_CLASSIC or TIFF_VERSION_BIG. */
    uint16_t tiff_version;
} TIFFHeaderCommon;
/** @brief Represent a classic TIFF header with a 32-bit directory offset.
 *
 * Integer members are interpreted in host byte order after header decoding.
 * The first-directory offset is measured in bytes from the start of the file.
 */
typedef struct
{
    /** @brief File byte-order signature. */
    uint16_t tiff_magic;
    /** @brief TIFF_VERSION_CLASSIC. */
    uint16_t tiff_version;
    /** @brief First IFD byte offset; zero if none is linked. */
    uint32_t tiff_diroff;
} TIFFHeaderClassic;
/** @brief Represent a BigTIFF header with a 64-bit directory offset.
 *
 * Integer members are interpreted in host byte order after header decoding.
 * The first-directory offset is measured in bytes from the start of the file.
 */
typedef struct
{
    /** @brief File byte-order signature. */
    uint16_t tiff_magic;
    /** @brief TIFF_VERSION_BIG. */
    uint16_t tiff_version;
    /** @brief Offset width in bytes; must be 8. */
    uint16_t tiff_offsetsize;
    /** @brief Reserved field; must be zero. */
    uint16_t tiff_unused;
    /** @brief First IFD byte offset; zero if none is linked. */
    uint64_t tiff_diroff;
} TIFFHeaderBig;

/*
 * Tag and value descriptions below note when an item was obsoleted by TIFF
 * revision 5.0 or 6.0, introduced in or after revision 6.0, or introduced by
 * the Adobe DNG specification.
 */

/**
 * @brief Identify the on-disk representation of a TIFF directory field.
 *
 * RATIONAL and SRATIONAL store a pair of 32-bit integers, not an IEEE double.
 * API argument types need not equal these storage types; use the contracts of
 * the field accessors for variadic argument types and counts.
 *
 * TIFF_IFD8 in an internal field definition permits format-dependent directory
 * offset writing: the writer uses TIFF_IFD and 32-bit offsets for classic TIFF,
 * and TIFF_IFD8 and 64-bit offsets for BigTIFF.
 *
 * TIFF_SBYTE through TIFF_DOUBLE were introduced in TIFF 6.0, TIFF_IFD after
 * TIFF 6.0, and TIFF_LONG8, TIFF_SLONG8 and TIFF_IFD8 by BigTIFF.
 */
typedef enum
{
    /** @brief Placeholder for an unspecified field type. */
    TIFF_NOTYPE = 0,
    /** @brief Unsigned 8-bit integer. */
    TIFF_BYTE = 1,
    /** @brief Sequence of 8-bit bytes ending in a null byte. */
    TIFF_ASCII = 2,
    /** @brief Unsigned 16-bit integer. */
    TIFF_SHORT = 3,
    /** @brief Unsigned 32-bit integer. */
    TIFF_LONG = 4,
    /** @brief Unsigned 32-bit numerator and denominator. */
    TIFF_RATIONAL = 5,
    /** @brief Signed 8-bit integer. */
    TIFF_SBYTE = 6,
    /** @brief Untyped 8-bit data. */
    TIFF_UNDEFINED = 7,
    /** @brief Signed 16-bit integer. */
    TIFF_SSHORT = 8,
    /** @brief Signed 32-bit integer. */
    TIFF_SLONG = 9,
    /** @brief Signed 32-bit numerator and denominator. */
    TIFF_SRATIONAL = 10,
    /** @brief 32-bit IEEE floating-point value. */
    TIFF_FLOAT = 11,
    /** @brief 64-bit IEEE floating-point value. */
    TIFF_DOUBLE = 12,
    /** @brief Unsigned 32-bit IFD byte offset. */
    TIFF_IFD = 13,
    /** @brief BigTIFF unsigned 64-bit integer. */
    TIFF_LONG8 = 16,
    /** @brief BigTIFF signed 64-bit integer. */
    TIFF_SLONG8 = 17,
    /** @brief BigTIFF unsigned 64-bit IFD byte offset. */
    TIFF_IFD8 = 18
} TIFFDataType;

/** @} */

/*
 * TIFF Tag Definitions.
 */
/* clang-format off */   /* for better readability of tag comments */
/** @addtogroup libtiff_public_tiff_tags
 * @{ */
/** @brief TIFF tag: Subfile data descriptor. */
#define TIFFTAG_SUBFILETYPE 254
/** @brief Reduced resolution version. */
#define FILETYPE_REDUCEDIMAGE 0x1
/** @brief One page of many. */
#define FILETYPE_PAGE 0x2
/** @brief Transparency mask. */
#define FILETYPE_MASK 0x4
/**
 * @brief TIFF tag: Old subfile classification; superseded by
 * TIFFTAG_SUBFILETYPE (obsoleted by TIFF 5.0).
 */
#define TIFFTAG_OSUBFILETYPE 255
/** @brief Full resolution image data. */
#define OFILETYPE_IMAGE 1
/** @brief Reduced size image data. */
#define OFILETYPE_REDUCEDIMAGE 2
/** @brief One page of many. */
#define OFILETYPE_PAGE 3
/** @brief TIFF tag: Image width in pixels. */
#define TIFFTAG_IMAGEWIDTH 256
/** @brief TIFF tag: Image height in pixels. */
#define TIFFTAG_IMAGELENGTH 257
/** @brief TIFF tag: Bits per channel (sample). */
#define TIFFTAG_BITSPERSAMPLE 258
/** @brief TIFF tag: Data compression technique. */
#define TIFFTAG_COMPRESSION 259
/** @brief Dump mode. */
#define COMPRESSION_NONE 1
/** @brief CCITT modified Huffman RLE. */
#define COMPRESSION_CCITTRLE 2
/** @brief CCITT Group 3 fax encoding. */
#define COMPRESSION_CCITTFAX3 3
/** @brief CCITT T.4 (TIFF 6 name). */
#define COMPRESSION_CCITT_T4 3
/** @brief CCITT Group 4 fax encoding. */
#define COMPRESSION_CCITTFAX4 4
/** @brief CCITT T.6 (TIFF 6 name). */
#define COMPRESSION_CCITT_T6 4
/** @brief Lempel-Ziv  & Welch. */
#define COMPRESSION_LZW 5
/** @brief 6.0 JPEG (introduced in TIFF 6.0). */
#define COMPRESSION_OJPEG 6
/** @brief JPEG DCT compression (introduced after TIFF 6.0). */
#define COMPRESSION_JPEG 7
/** @brief TIFF/FX T.85 JBIG compression (introduced in TIFF 6.0). */
#define COMPRESSION_T85 9
/**
 * @brief TIFF/FX T.43 colour by layered JBIG compression (introduced in TIFF
 * 6.0).
 */
#define COMPRESSION_T43 10
/** @brief NeXT 2-bit RLE. */
#define COMPRESSION_NEXT 32766
/** @brief CCITT modified Huffman RLE with word alignment. */
#define COMPRESSION_CCITTRLEW 32771
/** @brief Macintosh RLE. */
#define COMPRESSION_PACKBITS 32773
/** @brief ThunderScan RLE. */
#define COMPRESSION_THUNDERSCAN 32809
/* codes 32895-32898 are reserved for ANSI IT8 TIFF/IT <dkelly@apago.com) */
/** @brief IT8 CT w/padding. */
#define COMPRESSION_IT8CTPAD 32895
/** @brief IT8 Linework RLE. */
#define COMPRESSION_IT8LW 32896
/** @brief IT8 Monochrome picture. */
#define COMPRESSION_IT8MP 32897
/** @brief IT8 Binary line art. */
#define COMPRESSION_IT8BL 32898
/* compression codes 32908-32911 are reserved for Pixar */
/** @brief Pixar companded 10bit LZW. */
#define COMPRESSION_PIXARFILM 32908
/** @brief Pixar companded 11bit ZIP. */
#define COMPRESSION_PIXARLOG 32909
/** @brief Deflate compression, legacy tag. */
#define COMPRESSION_DEFLATE 32946
/** @brief Deflate compression, as recognized by Adobe. */
#define COMPRESSION_ADOBE_DEFLATE 8
/* compression code 32947 is reserved for Oceana Matrix <dev@oceana.com> */
/** @brief Kodak DCS encoding. */
#define COMPRESSION_DCS 32947
/** @brief ISO JBIG. */
#define COMPRESSION_JBIG 34661
/** @brief SGI Log Luminance RLE. */
#define COMPRESSION_SGILOG 34676
/** @brief SGI Log 24-bit packed. */
#define COMPRESSION_SGILOG24 34677
/** @brief Leadtools JPEG2000. */
#define COMPRESSION_JP2000 34712
/** @brief ESRI LERC compression. */
#define COMPRESSION_LERC 34887
/* compression codes 34887-34889 are reserved for ESRI */
/** @brief LZMA2. */
#define COMPRESSION_LZMA 34925
/**
 * @brief Zstandard compression; not registered in the Adobe-maintained
 * registry.
 */
#define COMPRESSION_ZSTD 50000
/** @brief WebP compression; not registered in the Adobe-maintained registry. */
#define COMPRESSION_WEBP 50001
/**
 * @brief JPEG XL compression; not registered in the Adobe-maintained registry.
 */
#define COMPRESSION_JXL 50002
/** @brief JPEGXL from DNG 1.7 specification. */
#define COMPRESSION_JXL_DNG_1_7 52546
/** @brief TIFF tag: Photometric interpretation. */
#define TIFFTAG_PHOTOMETRIC 262
/** @brief Min value is white. */
#define PHOTOMETRIC_MINISWHITE 0
/** @brief Min value is black. */
#define PHOTOMETRIC_MINISBLACK 1
/** @brief RGB color model. */
#define PHOTOMETRIC_RGB 2
/** @brief Color map indexed. */
#define PHOTOMETRIC_PALETTE 3
/** @brief Transparency mask (obsoleted by TIFF 6.0). */
#define PHOTOMETRIC_MASK 4
/** @brief Color separations (introduced in TIFF 6.0). */
#define PHOTOMETRIC_SEPARATED 5
/** @brief CCIR 601 (introduced in TIFF 6.0). */
#define PHOTOMETRIC_YCBCR 6
/** @brief 1976 CIE L*a*b* (introduced in TIFF 6.0). */
#define PHOTOMETRIC_CIELAB 8
/** @brief ICC L*a*b* [Adobe TIFF Technote 4]. */
#define PHOTOMETRIC_ICCLAB 9
/** @brief ITU L*a*b*. */
#define PHOTOMETRIC_ITULAB 10
/** @brief Color filter array. */
#define PHOTOMETRIC_CFA 32803
/** @brief CIE Log2(L). */
#define PHOTOMETRIC_LOGL 32844
/** @brief CIE Log2(L) (u',v'). */
#define PHOTOMETRIC_LOGLUV 32845
/** @brief TIFF tag: Thresholding used on data (obsoleted by TIFF 5.0). */
#define TIFFTAG_THRESHHOLDING 263
/** @brief B&w art scan. */
#define THRESHHOLD_BILEVEL 1
/** @brief Ordered dither or halftone scan. */
#define THRESHHOLD_HALFTONE 2
/** @brief Usually floyd-steinberg. */
#define THRESHHOLD_ERRORDIFFUSE 3
/** @brief TIFF tag: Dithering matrix width (obsoleted by TIFF 5.0). */
#define TIFFTAG_CELLWIDTH 264
/** @brief TIFF tag: Dithering matrix height (obsoleted by TIFF 5.0). */
#define TIFFTAG_CELLLENGTH 265
/** @brief TIFF tag: Data order within a byte. */
#define TIFFTAG_FILLORDER 266
/** @brief Most significant -> least. */
#define FILLORDER_MSB2LSB 1
/** @brief Least significant -> most. */
#define FILLORDER_LSB2MSB 2
/** @brief TIFF tag: Name of the source document. */
#define TIFFTAG_DOCUMENTNAME 269
/** @brief TIFF tag: Info about image. */
#define TIFFTAG_IMAGEDESCRIPTION 270
/** @brief TIFF tag: Scanner manufacturer name. */
#define TIFFTAG_MAKE 271
/** @brief TIFF tag: Scanner model name/number. */
#define TIFFTAG_MODEL 272
/** @brief TIFF tag: Offsets to data strips. */
#define TIFFTAG_STRIPOFFSETS 273
/** @brief TIFF tag: Image orientation (obsoleted by TIFF 5.0). */
#define TIFFTAG_ORIENTATION 274
/** @brief Row 0 top, col 0 lhs. */
#define ORIENTATION_TOPLEFT 1
/** @brief Row 0 top, col 0 rhs. */
#define ORIENTATION_TOPRIGHT 2
/** @brief Row 0 bottom, col 0 rhs. */
#define ORIENTATION_BOTRIGHT 3
/** @brief Row 0 bottom, col 0 lhs. */
#define ORIENTATION_BOTLEFT 4
/** @brief Row 0 lhs, col 0 top. */
#define ORIENTATION_LEFTTOP 5
/** @brief Row 0 rhs, col 0 top. */
#define ORIENTATION_RIGHTTOP 6
/** @brief Row 0 rhs, col 0 bottom. */
#define ORIENTATION_RIGHTBOT 7
/** @brief Row 0 lhs, col 0 bottom. */
#define ORIENTATION_LEFTBOT 8
/** @brief TIFF tag: Samples per pixel. */
#define TIFFTAG_SAMPLESPERPIXEL 277
/** @brief TIFF tag: Rows per strip of data. */
#define TIFFTAG_ROWSPERSTRIP 278
/** @brief TIFF tag: Bytes counts for strips. */
#define TIFFTAG_STRIPBYTECOUNTS 279
/** @brief TIFF tag: Minimum sample value (obsoleted by TIFF 5.0). */
#define TIFFTAG_MINSAMPLEVALUE 280
/** @brief TIFF tag: Maximum sample value (obsoleted by TIFF 5.0). */
#define TIFFTAG_MAXSAMPLEVALUE 281
/** @brief TIFF tag: Pixels/resolution in x. */
#define TIFFTAG_XRESOLUTION 282
/** @brief TIFF tag: Pixels/resolution in y. */
#define TIFFTAG_YRESOLUTION 283
/** @brief TIFF tag: Storage organization. */
#define TIFFTAG_PLANARCONFIG 284
/** @brief Single image plane. */
#define PLANARCONFIG_CONTIG 1
/** @brief Separate planes of data. */
#define PLANARCONFIG_SEPARATE 2
/** @brief TIFF tag: Page name image is from. */
#define TIFFTAG_PAGENAME 285
/** @brief TIFF tag: X page offset of image lhs. */
#define TIFFTAG_XPOSITION 286
/** @brief TIFF tag: Y page offset of image lhs. */
#define TIFFTAG_YPOSITION 287
/** @brief TIFF tag: Byte offset to free block (obsoleted by TIFF 5.0). */
#define TIFFTAG_FREEOFFSETS 288
/** @brief TIFF tag: Sizes of free blocks (obsoleted by TIFF 5.0). */
#define TIFFTAG_FREEBYTECOUNTS 289
/** @brief TIFF tag: Gray scale curve accuracy (obsoleted by TIFF 6.0). */
#define TIFFTAG_GRAYRESPONSEUNIT 290
/** @brief Tenths of a unit. */
#define GRAYRESPONSEUNIT_10S 1
/** @brief Hundredths of a unit. */
#define GRAYRESPONSEUNIT_100S 2
/** @brief Thousandths of a unit. */
#define GRAYRESPONSEUNIT_1000S 3
/** @brief Ten-thousandths of a unit. */
#define GRAYRESPONSEUNIT_10000S 4
/** @brief Hundred-thousandths. */
#define GRAYRESPONSEUNIT_100000S 5
/** @brief TIFF tag: Gray scale response curve (obsoleted by TIFF 6.0). */
#define TIFFTAG_GRAYRESPONSECURVE 291
/** @brief TIFF tag: 32 flag bits. */
#define TIFFTAG_GROUP3OPTIONS 292
/** @brief TIFF tag: TIFF 6.0 proper name alias. */
#define TIFFTAG_T4OPTIONS 292
/** @brief 2-dimensional coding. */
#define GROUP3OPT_2DENCODING 0x1
/** @brief Data not compressed. */
#define GROUP3OPT_UNCOMPRESSED 0x2
/** @brief Fill to byte boundary. */
#define GROUP3OPT_FILLBITS 0x4
/** @brief TIFF tag: 32 flag bits. */
#define TIFFTAG_GROUP4OPTIONS 293
/** @brief TIFF tag: TIFF 6.0 proper name. */
#define TIFFTAG_T6OPTIONS 293
/** @brief Data not compressed. */
#define GROUP4OPT_UNCOMPRESSED 0x2
/** @brief TIFF tag: Units of resolutions. */
#define TIFFTAG_RESOLUTIONUNIT 296
/** @brief No meaningful units. */
#define RESUNIT_NONE 1
/** @brief Resolution measured in pixels per inch. */
#define RESUNIT_INCH 2
/** @brief Resolution measured in pixels per centimetre. */
#define RESUNIT_CENTIMETER 3
/** @brief TIFF tag: Page numbers of multi-page. */
#define TIFFTAG_PAGENUMBER 297
/** @brief TIFF tag: Color curve accuracy (obsoleted by TIFF 6.0). */
#define TIFFTAG_COLORRESPONSEUNIT 300
/** @brief Tenths of a unit. */
#define COLORRESPONSEUNIT_10S 1
/** @brief Hundredths of a unit. */
#define COLORRESPONSEUNIT_100S 2
/** @brief Thousandths of a unit. */
#define COLORRESPONSEUNIT_1000S 3
/** @brief Ten-thousandths of a unit. */
#define COLORRESPONSEUNIT_10000S 4
/** @brief Hundred-thousandths. */
#define COLORRESPONSEUNIT_100000S 5
/** @brief TIFF tag: Colorimetry info (introduced in TIFF 6.0). */
#define TIFFTAG_TRANSFERFUNCTION 301
/** @brief TIFF tag: Name & release. */
#define TIFFTAG_SOFTWARE 305
/** @brief TIFF tag: Creation date and time. */
#define TIFFTAG_DATETIME 306
/** @brief TIFF tag: Creator of image. */
#define TIFFTAG_ARTIST 315
/** @brief TIFF tag: Machine where created. */
#define TIFFTAG_HOSTCOMPUTER 316
/** @brief TIFF tag: Prediction scheme applied before compression. */
#define TIFFTAG_PREDICTOR 317
/** @brief No prediction scheme used. */
#define PREDICTOR_NONE 1
/** @brief Horizontal differencing. */
#define PREDICTOR_HORIZONTAL 2
/** @brief Floating point predictor. */
#define PREDICTOR_FLOATINGPOINT 3
/** @brief TIFF tag: Image white point. */
#define TIFFTAG_WHITEPOINT 318
/** @brief TIFF tag: Primary chromaticities (introduced in TIFF 6.0). */
#define TIFFTAG_PRIMARYCHROMATICITIES 319
/** @brief TIFF tag: RGB map for palette image. */
#define TIFFTAG_COLORMAP 320
/** @brief TIFF tag: Highlight+shadow info (introduced in TIFF 6.0). */
#define TIFFTAG_HALFTONEHINTS 321
/** @brief TIFF tag: Tile width in pixels (introduced in TIFF 6.0). */
#define TIFFTAG_TILEWIDTH 322
/** @brief TIFF tag: Tile height in pixels (introduced in TIFF 6.0). */
#define TIFFTAG_TILELENGTH 323
/** @brief TIFF tag: Offsets to data tiles (introduced in TIFF 6.0). */
#define TIFFTAG_TILEOFFSETS 324
/** @brief TIFF tag: Byte counts for tiles (introduced in TIFF 6.0). */
#define TIFFTAG_TILEBYTECOUNTS 325
/** @brief TIFF tag: Lines w/ wrong pixel count. */
#define TIFFTAG_BADFAXLINES 326
/** @brief TIFF tag: Regenerated line info. */
#define TIFFTAG_CLEANFAXDATA 327
/** @brief No errors detected. */
#define CLEANFAXDATA_CLEAN 0
/** @brief Receiver regenerated lines. */
#define CLEANFAXDATA_REGENERATED 1
/** @brief Uncorrected errors exist. */
#define CLEANFAXDATA_UNCLEAN 2
/** @brief TIFF tag: Max consecutive bad lines. */
#define TIFFTAG_CONSECUTIVEBADFAXLINES 328
/** @brief TIFF tag: Subimage descriptors. */
#define TIFFTAG_SUBIFD 330
/** @brief TIFF tag: Inks in separated image (introduced in TIFF 6.0). */
#define TIFFTAG_INKSET 332
/** @brief Cyan-magenta-yellow-black color (introduced in TIFF 6.0). */
#define INKSET_CMYK 1
/** @brief Multi-ink or hi-fi color (introduced in TIFF 6.0). */
#define INKSET_MULTIINK 2
/** @brief TIFF tag: Ascii names of inks (introduced in TIFF 6.0). */
#define TIFFTAG_INKNAMES 333
/** @brief TIFF tag: Number of inks (introduced in TIFF 6.0). */
#define TIFFTAG_NUMBEROFINKS 334
/** @brief TIFF tag: 0% and 100% dot codes (introduced in TIFF 6.0). */
#define TIFFTAG_DOTRANGE 336
/** @brief TIFF tag: Separation target (introduced in TIFF 6.0). */
#define TIFFTAG_TARGETPRINTER 337
/** @brief TIFF tag: Info about extra samples (introduced in TIFF 6.0). */
#define TIFFTAG_EXTRASAMPLES 338
/** @brief Unspecified data (introduced in TIFF 6.0). */
#define EXTRASAMPLE_UNSPECIFIED 0
/** @brief Associated alpha data (introduced in TIFF 6.0). */
#define EXTRASAMPLE_ASSOCALPHA 1
/** @brief Unassociated alpha data (introduced in TIFF 6.0). */
#define EXTRASAMPLE_UNASSALPHA 2
/** @brief TIFF tag: Data sample format (introduced in TIFF 6.0). */
#define TIFFTAG_SAMPLEFORMAT 339
/** @brief Unsigned integer data (introduced in TIFF 6.0). */
#define SAMPLEFORMAT_UINT 1
/** @brief Signed integer data (introduced in TIFF 6.0). */
#define SAMPLEFORMAT_INT 2
/** @brief IEEE floating point data (introduced in TIFF 6.0). */
#define SAMPLEFORMAT_IEEEFP 3
/** @brief Untyped data (introduced in TIFF 6.0). */
#define SAMPLEFORMAT_VOID 4
/** @brief Complex signed int (introduced in TIFF 6.0). */
#define SAMPLEFORMAT_COMPLEXINT 5
/** @brief Complex ieee floating (introduced in TIFF 6.0). */
#define SAMPLEFORMAT_COMPLEXIEEEFP 6
/** @brief TIFF tag: Variable MinSampleValue (introduced in TIFF 6.0). */
#define TIFFTAG_SMINSAMPLEVALUE 340
/** @brief TIFF tag: Variable MaxSampleValue (introduced in TIFF 6.0). */
#define TIFFTAG_SMAXSAMPLEVALUE 341
/**
 * @brief TIFF tag: ClipPath [Adobe TIFF technote 2] (introduced after TIFF
 * 6.0).
 */
#define TIFFTAG_CLIPPATH 343
/**
 * @brief TIFF tag: XClipPathUnits [Adobe TIFF technote 2] (introduced after
 * TIFF 6.0).
 */
#define TIFFTAG_XCLIPPATHUNITS 344
/**
 * @brief TIFF tag: YClipPathUnits [Adobe TIFF technote 2] (introduced after
 * TIFF 6.0).
 */
#define TIFFTAG_YCLIPPATHUNITS 345
/**
 * @brief TIFF tag: Indexed [Adobe TIFF Technote 3] (introduced after TIFF 6.0).
 */
#define TIFFTAG_INDEXED 346
/** @brief TIFF tag: JPEG table stream (introduced after TIFF 6.0). */
#define TIFFTAG_JPEGTABLES 347
/**
 * @brief TIFF tag: OPI Proxy [Adobe TIFF technote] (introduced after TIFF 6.0).
 */
#define TIFFTAG_OPIPROXY 351
/* Tags 400-435 are from the TIFF/FX spec */
/**
 * @brief TIFF tag: TIFF/FX global parameters directory offset (introduced in
 * TIFF 6.0).
 */
#define TIFFTAG_GLOBALPARAMETERSIFD 400
/** @brief TIFF tag: TIFF/FX profile type (introduced in TIFF 6.0). */
#define TIFFTAG_PROFILETYPE 401
/** @brief Unspecified TIFF/FX profile type (introduced in TIFF 6.0). */
#define PROFILETYPE_UNSPECIFIED 0
/** @brief TIFF/FX Group 3 fax profile type (introduced in TIFF 6.0). */
#define PROFILETYPE_G3_FAX 1
/** @brief TIFF tag: TIFF/FX fax profile (introduced in TIFF 6.0). */
#define TIFFTAG_FAXPROFILE 402
/** @brief TIFF/FX FAX profile S (introduced in TIFF 6.0). */
#define FAXPROFILE_S 1
/** @brief TIFF/FX FAX profile F (introduced in TIFF 6.0). */
#define FAXPROFILE_F 2
/** @brief TIFF/FX FAX profile J (introduced in TIFF 6.0). */
#define FAXPROFILE_J 3
/** @brief TIFF/FX FAX profile C (introduced in TIFF 6.0). */
#define FAXPROFILE_C 4
/** @brief TIFF/FX FAX profile L (introduced in TIFF 6.0). */
#define FAXPROFILE_L 5
/** @brief TIFF/FX fax profile M (introduced in TIFF 6.0). */
#define FAXPROFILE_M 6
/** @brief TIFF tag: TIFF/FX coding methods (introduced in TIFF 6.0). */
#define TIFFTAG_CODINGMETHODS 403
/** @brief T.4 1D. */
#define CODINGMETHODS_T4_1D (1 << 1)
/** @brief T.4 2D. */
#define CODINGMETHODS_T4_2D (1 << 2)
/** @brief T.6. */
#define CODINGMETHODS_T6 (1 << 3)
/** @brief T.85 JBIG. */
#define CODINGMETHODS_T85 (1 << 4)
/** @brief T.42 JPEG. */
#define CODINGMETHODS_T42 (1 << 5)
/** @brief T.43 colour by layered JBIG. */
#define CODINGMETHODS_T43 (1 << 6)
/** @brief TIFF tag: TIFF/FX version year (introduced in TIFF 6.0). */
#define TIFFTAG_VERSIONYEAR 404
/** @brief TIFF tag: TIFF/FX mode number (introduced in TIFF 6.0). */
#define TIFFTAG_MODENUMBER 405
/** @brief TIFF tag: TIFF/FX decode (introduced in TIFF 6.0). */
#define TIFFTAG_DECODE 433
/** @brief TIFF tag: TIFF/FX image base colour (introduced in TIFF 6.0). */
#define TIFFTAG_IMAGEBASECOLOR 434
/** @brief TIFF tag: TIFF/FX T.82 options (introduced in TIFF 6.0). */
#define TIFFTAG_T82OPTIONS 435
/*
 * Tags 512-521 are obsoleted by Technical Note #2 which specifies a
 * revised JPEG-in-TIFF scheme.
 */
/** @brief TIFF tag: JPEG processing algorithm (introduced in TIFF 6.0). */
#define TIFFTAG_JPEGPROC 512
/** @brief Baseline sequential (introduced in TIFF 6.0). */
#define JPEGPROC_BASELINE 1
/** @brief Huffman coded lossless (introduced in TIFF 6.0). */
#define JPEGPROC_LOSSLESS 14
/** @brief TIFF tag: Pointer to SOI marker (introduced in TIFF 6.0). */
#define TIFFTAG_JPEGIFOFFSET 513
/** @brief TIFF tag: JFIF stream length (introduced in TIFF 6.0). */
#define TIFFTAG_JPEGIFBYTECOUNT 514
/** @brief TIFF tag: Restart interval length (introduced in TIFF 6.0). */
#define TIFFTAG_JPEGRESTARTINTERVAL 515
/** @brief TIFF tag: Lossless proc predictor (introduced in TIFF 6.0). */
#define TIFFTAG_JPEGLOSSLESSPREDICTORS 517
/** @brief TIFF tag: Lossless point transform (introduced in TIFF 6.0). */
#define TIFFTAG_JPEGPOINTTRANSFORM 518
/** @brief TIFF tag: Q matrix offsets (introduced in TIFF 6.0). */
#define TIFFTAG_JPEGQTABLES 519
/** @brief TIFF tag: DCT table offsets (introduced in TIFF 6.0). */
#define TIFFTAG_JPEGDCTABLES 520
/** @brief TIFF tag: AC coefficient offsets (introduced in TIFF 6.0). */
#define TIFFTAG_JPEGACTABLES 521
/** @brief TIFF tag: RGB -> YCbCr transform (introduced in TIFF 6.0). */
#define TIFFTAG_YCBCRCOEFFICIENTS 529
/** @brief TIFF tag: YCbCr subsampling factors (introduced in TIFF 6.0). */
#define TIFFTAG_YCBCRSUBSAMPLING 530
/** @brief TIFF tag: Subsample positioning (introduced in TIFF 6.0). */
#define TIFFTAG_YCBCRPOSITIONING 531
/** @brief As in PostScript Level 2 (introduced in TIFF 6.0). */
#define YCBCRPOSITION_CENTERED 1
/** @brief As in CCIR 601-1 (introduced in TIFF 6.0). */
#define YCBCRPOSITION_COSITED 2
/** @brief TIFF tag: Colorimetry info (introduced in TIFF 6.0). */
#define TIFFTAG_REFERENCEBLACKWHITE 532
/** @brief TIFF tag: TIFF/FX strip row counts (introduced in TIFF 6.0). */
#define TIFFTAG_STRIPROWCOUNTS 559
/** @brief TIFF tag: XMP XML packet (introduced after TIFF 6.0). */
#define TIFFTAG_XMLPACKET 700
/**
 * @brief TIFF tag: OPI ImageID [Adobe TIFF technote] (introduced after TIFF
 * 6.0).
 */
#define TIFFTAG_OPIIMAGEID 32781
/* For eiStream Annotation Specification, Version 1.00.06 see
 * http://web.archive.org/web/20050309141348/http://www.kofile.com/support%20pro/faqs/annospec.htm */
/** @brief TIFF tag: EiStream TIFF annotation data. */
#define TIFFTAG_TIFFANNOTATIONDATA 32932
/* tags 32952-32956 are private tags registered to Island Graphics */
/** @brief TIFF tag: Image reference points. */
#define TIFFTAG_REFPTS 32953
/** @brief TIFF tag: Region-xform tack point. */
#define TIFFTAG_REGIONTACKPOINT 32954
/** @brief TIFF tag: Warp quadrilateral. */
#define TIFFTAG_REGIONWARPCORNERS 32955
/** @brief TIFF tag: Affine transformation mat. */
#define TIFFTAG_REGIONAFFINE 32956
/* tags 32995-32999 are private tags registered to SGI */
/**
 * @brief TIFF tag: Legacy alpha indicator; superseded by TIFFTAG_EXTRASAMPLES
 * (obsoleted by TIFF 6.0).
 */
#define TIFFTAG_MATTEING 32995
/**
 * @brief TIFF tag: Legacy sample-type indicator; superseded by
 * TIFFTAG_SAMPLEFORMAT (obsoleted by TIFF 6.0).
 */
#define TIFFTAG_DATATYPE 32996
/** @brief TIFF tag: Z depth of image. */
#define TIFFTAG_IMAGEDEPTH 32997
/** @brief TIFF tag: Z depth/data tile. */
#define TIFFTAG_TILEDEPTH 32998
/* tags 33300-33309 are private tags registered to Pixar */
/*
 * TIFFTAG_PIXAR_IMAGEFULLWIDTH and TIFFTAG_PIXAR_IMAGEFULLLENGTH
 * are set when an image has been cropped out of a larger image.
 * They reflect the size of the original uncropped image.
 * The TIFFTAG_XPOSITION and TIFFTAG_YPOSITION can be used
 * to determine the position of the smaller image in the larger one.
 */
/** @brief TIFF tag: Original uncropped image width in pixels. */
#define TIFFTAG_PIXAR_IMAGEFULLWIDTH 33300
/** @brief TIFF tag: Original uncropped image height in pixels. */
#define TIFFTAG_PIXAR_IMAGEFULLLENGTH 33301
/* Tags 33302-33306 are used to identify special image modes and data
 * used by Pixar's texture formats.
 */
/** @brief TIFF tag: Texture map format. */
#define TIFFTAG_PIXAR_TEXTUREFORMAT 33302
/** @brief TIFF tag: S & t wrap modes. */
#define TIFFTAG_PIXAR_WRAPMODES 33303
/** @brief TIFF tag: Cotangent of the field of view for environment maps. */
#define TIFFTAG_PIXAR_FOVCOT 33304
/** @brief TIFF tag: Pixar world-to-screen transformation matrix. */
#define TIFFTAG_PIXAR_MATRIX_WORLDTOSCREEN 33305
/** @brief TIFF tag: Pixar world-to-camera transformation matrix. */
#define TIFFTAG_PIXAR_MATRIX_WORLDTOCAMERA 33306
/* tag 33405 is a private tag registered to Eastman Kodak */
/** @brief TIFF tag: Device serial number. */
#define TIFFTAG_WRITERSERIALNUMBER 33405
/** @brief TIFF tag: (alias for TIFFTAG_EP_CFAREPEATPATTERNDIM). */
#define TIFFTAG_CFAREPEATPATTERNDIM 33421
/** @brief TIFF tag: (alias for TIFFTAG_EP_CFAPATTERN). */
#define TIFFTAG_CFAPATTERN 33422
/** @brief TIFF tag: (alias for TIFFTAG_EP_BATTERYLEVEL). */
#define TIFFTAG_BATTERYLEVEL 33423
/* tag 33432 is listed in the 6.0 spec w/ unknown ownership */
/** @brief TIFF tag: Copyright string. */
#define TIFFTAG_COPYRIGHT 33432
/* Tags 33445-33452 are used for Molecular Dynamics GEL fileformat,
 * see http://research.stowers-institute.org/mcm/efg/ScientificSoftware/Utility/TiffTags/GEL-FileFormat.pdf
 * (2023: the above web site is unavailable but tags are explained briefly at
 * https://www.awaresystems.be/imaging/tiff/tifftags/docs/gel.html
 */
/**
 * @brief TIFF tag: Specifies the pixel data format encoding in the GEL file
 * format.
 */
#define TIFFTAG_MD_FILETAG 33445
/** @brief TIFF tag: Scale factor. */
#define TIFFTAG_MD_SCALEPIXEL 33446
/** @brief TIFF tag: Conversion from 16bit to 8bit. */
#define TIFFTAG_MD_COLORTABLE 33447
/** @brief TIFF tag: Name of the lab that scanned this file. */
#define TIFFTAG_MD_LABNAME 33448
/** @brief TIFF tag: Information about the scanned GEL sample. */
#define TIFFTAG_MD_SAMPLEINFO 33449
/**
 * @brief TIFF tag: Information about the date the sample was prepared YY/MM/DD.
 */
#define TIFFTAG_MD_PREPDATE 33450
/**
 * @brief TIFF tag: Information about the time the sample was prepared HH:MM.
 */
#define TIFFTAG_MD_PREPTIME 33451
/**
 * @brief TIFF tag: Units for data in this file, as used in the GEL file format.
 */
#define TIFFTAG_MD_FILEUNITS 33452
/* IPTC TAG from RichTIFF specifications */
/** @brief TIFF tag: RichTIFF IPTC metadata. */
#define TIFFTAG_RICHTIFFIPTC 33723
/** @brief TIFF tag: Intergraph Application specific storage. */
#define TIFFTAG_INGR_PACKET_DATA_TAG 33918
/** @brief TIFF tag: Intergraph Application specific flags. */
#define TIFFTAG_INGR_FLAG_REGISTERS 33919
/**
 * @brief TIFF tag: Originally part of Intergraph's GeoTIFF tags, but likely
 * understood by IrasB only.
 */
#define TIFFTAG_IRASB_TRANSORMATION_MATRIX 33920
/** @brief TIFF tag: GeoTIFF. */
#define TIFFTAG_MODELTIEPOINTTAG 33922
/* 34016-34029 are reserved for ANSI IT8 TIFF/IT <dkelly@apago.com) */
/** @brief TIFF tag: Site name. */
#define TIFFTAG_IT8SITE 34016
/** @brief TIFF tag: IT8 colour sequence, such as RGB or CMYK. */
#define TIFFTAG_IT8COLORSEQUENCE 34017
/** @brief TIFF tag: DDES Header. */
#define TIFFTAG_IT8HEADER 34018
/** @brief TIFF tag: Raster scanline padding. */
#define TIFFTAG_IT8RASTERPADDING 34019
/** @brief TIFF tag: # of bits in short run. */
#define TIFFTAG_IT8BITSPERRUNLENGTH 34020
/** @brief TIFF tag: # of bits in long run. */
#define TIFFTAG_IT8BITSPEREXTENDEDRUNLENGTH 34021
/** @brief TIFF tag: LW colortable. */
#define TIFFTAG_IT8COLORTABLE 34022
/** @brief TIFF tag: BP/BL image color switch. */
#define TIFFTAG_IT8IMAGECOLORINDICATOR 34023
/** @brief TIFF tag: BP/BL bg color switch. */
#define TIFFTAG_IT8BKGCOLORINDICATOR 34024
/** @brief TIFF tag: BP/BL image color value. */
#define TIFFTAG_IT8IMAGECOLORVALUE 34025
/** @brief TIFF tag: BP/BL bg color value. */
#define TIFFTAG_IT8BKGCOLORVALUE 34026
/** @brief TIFF tag: MP pixel intensity value. */
#define TIFFTAG_IT8PIXELINTENSITYRANGE 34027
/** @brief TIFF tag: HC transparency switch. */
#define TIFFTAG_IT8TRANSPARENCYINDICATOR 34028
/** @brief TIFF tag: IT8 colour-characterisation table. */
#define TIFFTAG_IT8COLORCHARACTERIZATION 34029
/** @brief TIFF tag: HC usage indicator. */
#define TIFFTAG_IT8HCUSAGE 34030
/** @brief TIFF tag: Trapping indicator (untrapped=0, trapped=1). */
#define TIFFTAG_IT8TRAPINDICATOR 34031
/** @brief TIFF tag: CMYK color equivalents. */
#define TIFFTAG_IT8CMYKEQUIVALENT 34032
/* tags 34232-34236 are private tags registered to Texas Instruments */
/** @brief TIFF tag: Sequence Frame Count. */
#define TIFFTAG_FRAMECOUNT 34232
/** @brief TIFF tag: Used in interchangeable GeoTIFF files. */
#define TIFFTAG_MODELTRANSFORMATIONTAG 34264
/* tag 34377 is private tag registered to Adobe for PhotoShop */
/** @brief TIFF tag: Photoshop image resource data. */
#define TIFFTAG_PHOTOSHOP 34377
/* tags 34665, 34853 and 40965 are documented in EXIF specification */
/**
 * The offset identifies an EXIF directory, whose tag namespace is separate
 * from the image directory.
 */
/** @brief TIFF tag: Pointer to EXIF private directory. */
#define TIFFTAG_EXIFIFD 34665
/* tag 34750 is a private tag registered to Adobe? */
/** @brief TIFF tag: ICC profile data. */
#define TIFFTAG_ICCPROFILE 34675
/**
 * @brief TIFF tag: TIFF/FX image layer information (introduced in TIFF 6.0).
 */
#define TIFFTAG_IMAGELAYER 34732
/* tag 34750 is a private tag registered to Pixel Magic */
/** @brief TIFF tag: JBIG options. */
#define TIFFTAG_JBIGOPTIONS 34750
/**
 * The offset identifies a GPS directory; its small tag numbers are interpreted
 * in that directory rather than as image-directory tags.
 */
/** @brief TIFF tag: Pointer to EXIF GPS private directory. */
#define TIFFTAG_GPSIFD 34853
/* tags 34908-34914 are private tags registered to SGI */
/** @brief TIFF tag: Encoded Class 2 fax-session parameters. */
#define TIFFTAG_FAXRECVPARAMS 34908
/** @brief TIFF tag: Received SubAddr string. */
#define TIFFTAG_FAXSUBADDRESS 34909
/** @brief TIFF tag: Receive time (secs). */
#define TIFFTAG_FAXRECVTIME 34910
/** @brief TIFF tag: Encoded fax-session parameters from Table 2/T.30. */
#define TIFFTAG_FAXDCS 34911
/* tags 37439-37443 are registered to SGI <gregl@sgi.com> */
/** @brief TIFF tag: Sample value to Nits. */
#define TIFFTAG_STONITS 37439
/* tag 34929 is a private tag registered to FedEx */
/**
 * @brief TIFF tag: FedEx EDR private tag; payload semantics are not specified
 * here.
 */
#define TIFFTAG_FEDEX_EDR 34929
/** @brief TIFF tag: Photoshop image source data. */
#define TIFFTAG_IMAGESOURCEDATA 37724
/** @brief TIFF tag: Pointer to EXIF Interoperability private directory. */
#define TIFFTAG_INTEROPERABILITYIFD 40965
/** @brief TIFF tag: Used by the GDAL library. */
#define TIFFTAG_GDAL_METADATA 42112
/** @brief TIFF tag: Used by the GDAL library. */
#define TIFFTAG_GDAL_NODATA 42113
/** @brief TIFF tag: Used in the Oce scanning process. */
#define TIFFTAG_OCE_SCANJOB_DESCRIPTION 50215
/** @brief TIFF tag: Used in the Oce scanning process. */
#define TIFFTAG_OCE_APPLICATION_SELECTOR 50216
/** @brief TIFF tag: Oce scanning identification number. */
#define TIFFTAG_OCE_IDENTIFICATION_NUMBER 50217
/** @brief TIFF tag: Oce scanning image-logic characteristics. */
#define TIFFTAG_OCE_IMAGELOGIC_CHARACTERISTICS 50218
/* tags 50674 to 50677 are reserved for ESRI */
/** @brief TIFF tag: Stores LERC version and additional compression method. */
#define TIFFTAG_LERC_PARAMETERS 50674

/* Adobe Digital Negative (DNG) format tags */
/** @} */

/** @addtogroup libtiff_public_dng_tags
 * @{ */
/** @brief TIFF tag: DNG version number (introduced by Adobe DNG). */
#define TIFFTAG_DNGVERSION 50706
/** @brief TIFF tag: DNG compatibility version (introduced by Adobe DNG). */
#define TIFFTAG_DNGBACKWARDVERSION 50707
/** @brief TIFF tag: Name for the camera model (introduced by Adobe DNG). */
#define TIFFTAG_UNIQUECAMERAMODEL 50708
/**
 * @brief TIFF tag: Localized camera model name (UTF-8) (introduced by Adobe
 * DNG).
 */
#define TIFFTAG_LOCALIZEDCAMERAMODEL 50709
/**
 * @brief TIFF tag: CFAPattern->LinearRaw space mapping (introduced by Adobe
 * DNG).
 */
#define TIFFTAG_CFAPLANECOLOR 50710
/** @brief TIFF tag: Spatial layout of the CFA (introduced by Adobe DNG). */
#define TIFFTAG_CFALAYOUT 50711
/** @brief TIFF tag: Lookup table description (introduced by Adobe DNG). */
#define TIFFTAG_LINEARIZATIONTABLE 50712
/**
 * @brief TIFF tag: Repeat pattern size for the BlackLevel tag (introduced by
 * Adobe DNG).
 */
#define TIFFTAG_BLACKLEVELREPEATDIM 50713
/** @brief TIFF tag: Zero light encoding level (introduced by Adobe DNG). */
#define TIFFTAG_BLACKLEVEL 50714
/**
 * @brief TIFF tag: Zero light encoding level differences (columns) (introduced
 * by Adobe DNG).
 */
#define TIFFTAG_BLACKLEVELDELTAH 50715
/**
 * @brief TIFF tag: Zero light encoding level differences (rows) (introduced by
 * Adobe DNG).
 */
#define TIFFTAG_BLACKLEVELDELTAV 50716
/**
 * @brief TIFF tag: Fully saturated encoding level (introduced by Adobe DNG).
 */
#define TIFFTAG_WHITELEVEL 50717
/** @brief TIFF tag: Default scale factors (introduced by Adobe DNG). */
#define TIFFTAG_DEFAULTSCALE 50718
/**
 * @brief TIFF tag: Origin of the final image area (introduced by Adobe DNG).
 */
#define TIFFTAG_DEFAULTCROPORIGIN 50719
/** @brief TIFF tag: Size of the final image area (introduced by Adobe DNG). */
#define TIFFTAG_DEFAULTCROPSIZE 50720
/**
 * @brief TIFF tag: XYZ->reference color space transformation matrix 1
 * (introduced by Adobe DNG).
 */
#define TIFFTAG_COLORMATRIX1 50721
/**
 * @brief TIFF tag: XYZ->reference color space transformation matrix 2
 * (introduced by Adobe DNG).
 */
#define TIFFTAG_COLORMATRIX2 50722
/** @brief TIFF tag: Calibration matrix 1 (introduced by Adobe DNG). */
#define TIFFTAG_CAMERACALIBRATION1 50723
/** @brief TIFF tag: Calibration matrix 2 (introduced by Adobe DNG). */
#define TIFFTAG_CAMERACALIBRATION2 50724
/**
 * @brief TIFF tag: Dimensionality reduction matrix 1 (introduced by Adobe DNG).
 */
#define TIFFTAG_REDUCTIONMATRIX1 50725
/**
 * @brief TIFF tag: Dimensionality reduction matrix 2 (introduced by Adobe DNG).
 */
#define TIFFTAG_REDUCTIONMATRIX2 50726
/**
 * @brief TIFF tag: Gain applied the stored raw values (introduced by Adobe
 * DNG).
 */
#define TIFFTAG_ANALOGBALANCE 50727
/**
 * @brief TIFF tag: Selected white balance in linear reference space (introduced
 * by Adobe DNG).
 */
#define TIFFTAG_ASSHOTNEUTRAL 50728
/**
 * @brief TIFF tag: Selected white balance in x-y chromaticity coordinates
 * (introduced by Adobe DNG).
 */
#define TIFFTAG_ASSHOTWHITEXY 50729
/**
 * @brief TIFF tag: How much to move the zero point (introduced by Adobe DNG).
 */
#define TIFFTAG_BASELINEEXPOSURE 50730
/** @brief TIFF tag: Relative noise level (introduced by Adobe DNG). */
#define TIFFTAG_BASELINENOISE 50731
/** @brief TIFF tag: Relative amount of sharpening (introduced by Adobe DNG). */
#define TIFFTAG_BASELINESHARPNESS 50732
/* TIFFTAG_BAYERGREENSPLIT: &how closely the values of the green pixels in the blue/green rows
 * track the values of the green pixels in the red/green rows */
/**
 * @brief TIFF tag: DNG difference between green responses in blue/green and
 * red/green rows.
 */
#define TIFFTAG_BAYERGREENSPLIT 50733
/** @brief TIFF tag: Non-linear encoding range (introduced by Adobe DNG). */
#define TIFFTAG_LINEARRESPONSELIMIT 50734
/** @brief TIFF tag: Camera's serial number (introduced by Adobe DNG). */
#define TIFFTAG_CAMERASERIALNUMBER 50735
/** @brief TIFF tag: Info about the lens. */
#define TIFFTAG_LENSINFO 50736
/** @brief TIFF tag: Chroma blur radius (introduced by Adobe DNG). */
#define TIFFTAG_CHROMABLURRADIUS 50737
/**
 * @brief TIFF tag: Relative strength of the camera's anti-alias filter
 * (introduced by Adobe DNG).
 */
#define TIFFTAG_ANTIALIASSTRENGTH 50738
/** @brief TIFF tag: Used by Adobe Camera Raw (introduced by Adobe DNG). */
#define TIFFTAG_SHADOWSCALE 50739
/** @brief TIFF tag: Manufacturer's private data (introduced by Adobe DNG). */
#define TIFFTAG_DNGPRIVATEDATA 50740
/**
 * @brief TIFF tag: Whether the EXIF MakerNote tag is safe to preserve along
 * with the rest of the EXIF data (introduced by Adobe DNG).
 */
#define TIFFTAG_MAKERNOTESAFETY 50741
/** @brief TIFF tag: Illuminant 1 (introduced by Adobe DNG). */
#define TIFFTAG_CALIBRATIONILLUMINANT1 50778
/** @brief TIFF tag: Illuminant 2 (introduced by Adobe DNG). */
#define TIFFTAG_CALIBRATIONILLUMINANT2 50779
/** @brief TIFF tag: Best quality multiplier (introduced by Adobe DNG). */
#define TIFFTAG_BESTQUALITYSCALE 50780
/**
 * @brief TIFF tag: Unique identifier for the raw image data (introduced by
 * Adobe DNG).
 */
#define TIFFTAG_RAWDATAUNIQUEID 50781
/**
 * @brief TIFF tag: File name of the original raw file (UTF-8) (introduced by
 * Adobe DNG).
 */
#define TIFFTAG_ORIGINALRAWFILENAME 50827
/**
 * @brief TIFF tag: Contents of the original raw file (introduced by Adobe DNG).
 */
#define TIFFTAG_ORIGINALRAWFILEDATA 50828
/**
 * @brief TIFF tag: Active (non-masked) pixels of the sensor (introduced by
 * Adobe DNG).
 */
#define TIFFTAG_ACTIVEAREA 50829
/**
 * @brief TIFF tag: List of coordinates of fully masked pixels (introduced by
 * Adobe DNG).
 */
#define TIFFTAG_MASKEDAREAS 50830
/**
 * @brief TIFF tag: DNG as-shot camera ICC profile (introduced by Adobe DNG).
 */
#define TIFFTAG_ASSHOTICCPROFILE 50831
/**
 * @brief TIFF tag: DNG camera-to-as-shot-profile input transformation matrix.
 */
#define TIFFTAG_ASSHOTPREPROFILEMATRIX 50832
/**
 * @brief TIFF tag: DNG current camera ICC profile (introduced by Adobe DNG).
 */
#define TIFFTAG_CURRENTICCPROFILE 50833
/**
 * @brief TIFF tag: DNG camera-to-current-profile input transformation matrix
 * (introduced by Adobe DNG).
 */
#define TIFFTAG_CURRENTPREPROFILEMATRIX 50834

/* DNG 1.2.0.0 */
/** @brief TIFF tag: Colorimetric reference (introduced by Adobe DNG). */
#define TIFFTAG_COLORIMETRICREFERENCE 50879
/**
 * @brief TIFF tag: Camera calibration signature (UTF-8) (introduced by Adobe
 * DNG).
 */
#define TIFFTAG_CAMERACALIBRATIONSIGNATURE 50931
/**
 * @brief TIFF tag: Profile calibration signature (UTF-8) (introduced by Adobe
 * DNG).
 */
#define TIFFTAG_PROFILECALIBRATIONSIGNATURE 50932
/* TIFFTAG_EXTRACAMERAPROFILES 50933 &extra camera profiles : is already defined for GeoTIFF DGIWG */
/** @brief TIFF tag: As shot profile name (UTF-8) (introduced by Adobe DNG). */
#define TIFFTAG_ASSHOTPROFILENAME 50934
/**
 * @brief TIFF tag: Amount of applied noise reduction (introduced by Adobe DNG).
 */
#define TIFFTAG_NOISEREDUCTIONAPPLIED 50935
/** @brief TIFF tag: Camera profile name (UTF-8) (introduced by Adobe DNG). */
#define TIFFTAG_PROFILENAME 50936
/** @brief TIFF tag: Dimensions of HSV mapping (introduced by Adobe DNG). */
#define TIFFTAG_PROFILEHUESATMAPDIMS 50937
/** @brief TIFF tag: First HSV mapping table (introduced by Adobe DNG). */
#define TIFFTAG_PROFILEHUESATMAPDATA1 50938
/** @brief TIFF tag: Second HSV mapping table (introduced by Adobe DNG). */
#define TIFFTAG_PROFILEHUESATMAPDATA2 50939
/** @brief TIFF tag: Default tone curve (introduced by Adobe DNG). */
#define TIFFTAG_PROFILETONECURVE 50940
/** @brief TIFF tag: Profile embedding policy (introduced by Adobe DNG). */
#define TIFFTAG_PROFILEEMBEDPOLICY 50941
/**
 * @brief TIFF tag: Profile copyright information (UTF-8) (introduced by Adobe
 * DNG).
 */
#define TIFFTAG_PROFILECOPYRIGHT 50942
/**
 * @brief TIFF tag: Matrix for mapping white balanced camera colors to XYZ D50
 * (introduced by Adobe DNG).
 */
#define TIFFTAG_FORWARDMATRIX1 50964
/**
 * @brief TIFF tag: Matrix for mapping white balanced camera colors to XYZ D50
 * (introduced by Adobe DNG).
 */
#define TIFFTAG_FORWARDMATRIX2 50965
/**
 * @brief TIFF tag: Name of application that created preview (UTF-8) (introduced
 * by Adobe DNG).
 */
#define TIFFTAG_PREVIEWAPPLICATIONNAME 50966
/**
 * @brief TIFF tag: Version of application that created preview (UTF-8)
 * (introduced by Adobe DNG).
 */
#define TIFFTAG_PREVIEWAPPLICATIONVERSION 50967
/**
 * @brief TIFF tag: Name of conversion settings (UTF-8) (introduced by Adobe
 * DNG).
 */
#define TIFFTAG_PREVIEWSETTINGSNAME 50968
/**
 * @brief TIFF tag: Unique id of conversion settings (introduced by Adobe DNG).
 */
#define TIFFTAG_PREVIEWSETTINGSDIGEST 50969
/** @brief TIFF tag: Preview color space (introduced by Adobe DNG). */
#define TIFFTAG_PREVIEWCOLORSPACE 50970
/**
 * @brief TIFF tag: Date/time preview was rendered (introduced by Adobe DNG).
 */
#define TIFFTAG_PREVIEWDATETIME 50971
/** @brief TIFF tag: Md5 of raw image data (introduced by Adobe DNG). */
#define TIFFTAG_RAWIMAGEDIGEST 50972
/**
 * @brief TIFF tag: Md5 of the data stored in the OriginalRawFileData tag
 * (introduced by Adobe DNG).
 */
#define TIFFTAG_ORIGINALRAWFILEDIGEST 50973
/** @brief TIFF tag: Subtile block size (introduced by Adobe DNG). */
#define TIFFTAG_SUBTILEBLOCKSIZE 50974
/** @brief TIFF tag: Number of interleaved fields (introduced by Adobe DNG). */
#define TIFFTAG_ROWINTERLEAVEFACTOR 50975
/**
 * @brief TIFF tag: Num of input samples in each dim of default "look" table
 * (introduced by Adobe DNG).
 */
#define TIFFTAG_PROFILELOOKTABLEDIMS 50981
/**
 * @brief TIFF tag: Default "look" table for use as starting point (introduced
 * by Adobe DNG).
 */
#define TIFFTAG_PROFILELOOKTABLEDATA 50982

/* DNG 1.3.0.0 */
/**
 * @brief TIFF tag: Opcodes that should be applied to raw image after reading
 * (introduced by Adobe DNG).
 */
#define TIFFTAG_OPCODELIST1 51008
/**
 * @brief TIFF tag: Opcodes that should be applied after mapping to linear
 * reference (introduced by Adobe DNG).
 */
#define TIFFTAG_OPCODELIST2 51009
/**
 * @brief TIFF tag: Opcodes that should be applied after demosaicing (introduced
 * by Adobe DNG).
 */
#define TIFFTAG_OPCODELIST3 51022
/** @brief TIFF tag: Noise profile (introduced by Adobe DNG). */
#define TIFFTAG_NOISEPROFILE 51041

/* DNG 1.4.0.0 */
/**
 * @brief TIFF tag: Default user crop rectangle in relative coords (introduced
 * by Adobe DNG).
 */
#define TIFFTAG_DEFAULTUSERCROP 51125
/** @brief TIFF tag: Black rendering hint (introduced by Adobe DNG). */
#define TIFFTAG_DEFAULTBLACKRENDER 51110
/** @brief TIFF tag: Baseline exposure offset (introduced by Adobe DNG). */
#define TIFFTAG_BASELINEEXPOSUREOFFSET 51109
/**
 * @brief TIFF tag: 3D LookTable indexing conversion (introduced by Adobe DNG).
 */
#define TIFFTAG_PROFILELOOKTABLEENCODING 51108
/**
 * @brief TIFF tag: 3D HueSatMap indexing conversion (introduced by Adobe DNG).
 */
#define TIFFTAG_PROFILEHUESATMAPENCODING 51107
/**
 * @brief TIFF tag: Default final size of larger original file for this proxy
 * (introduced by Adobe DNG).
 */
#define TIFFTAG_ORIGINALDEFAULTFINALSIZE 51089
/**
 * @brief TIFF tag: Best quality final size of larger original file for this
 * proxy (introduced by Adobe DNG).
 */
#define TIFFTAG_ORIGINALBESTQUALITYFINALSIZE 51090
/**
 * @brief TIFF tag: The default crop size of larger original file for this proxy
 * (introduced by Adobe DNG).
 */
#define TIFFTAG_ORIGINALDEFAULTCROPSIZE 51091
/**
 * @brief TIFF tag: Modified MD5 digest of the raw image data (introduced by
 * Adobe DNG).
 */
#define TIFFTAG_NEWRAWIMAGEDIGEST 51111
/**
 * @brief TIFF tag: The gain between the main raw FD and the preview IFD
 * containing this tag (introduced by Adobe DNG).
 */
#define TIFFTAG_RAWTOPREVIEWGAIN 51112

/* DNG 1.5.0.0 */
/**
 * @brief TIFF tag: Encoding of the depth data in the file (introduced by Adobe
 * DNG).
 */
#define TIFFTAG_DEPTHFORMAT 51177
/**
 * @brief TIFF tag: Distance from the camera represented by value 0 in the depth
 * map (introduced by Adobe DNG).
 */
#define TIFFTAG_DEPTHNEAR 51178
/**
 * @brief TIFF tag: Distance from the camera represented by the maximum value in
 * the depth map (introduced by Adobe DNG).
 */
#define TIFFTAG_DEPTHFAR 51179
/**
 * @brief TIFF tag: Measurement units for DepthNear and DepthFar (introduced by
 * Adobe DNG).
 */
#define TIFFTAG_DEPTHUNITS 51180
/**
 * @brief TIFF tag: Measurement geometry for the depth map (introduced by Adobe
 * DNG).
 */
#define TIFFTAG_DEPTHMEASURETYPE 51181
/**
 * @brief TIFF tag: A string that documents how the enhanced image data was
 * processed (introduced by Adobe DNG).
 */
#define TIFFTAG_ENHANCEPARAMS 51182

/* DNG 1.6.0.0 */
/**
 * @brief TIFF tag: Spatially varying gain tables that can be applied as
 * starting point (introduced by Adobe DNG).
 */
#define TIFFTAG_PROFILEGAINTABLEMAP 52525
/**
 * @brief TIFF tag: A string that identifies the semantic mask (introduced by
 * Adobe DNG).
 */
#define TIFFTAG_SEMANTICNAME 52526
/**
 * @brief TIFF tag: A string that identifies a specific instance in a semantic
 * mask (introduced by Adobe DNG).
 */
#define TIFFTAG_SEMANTICINSTANCEID 52528
/**
 * @brief TIFF tag: The crop rectangle of this IFD's mask, relative to the main
 * image (introduced by Adobe DNG).
 */
#define TIFFTAG_MASKSUBAREA 52536
/**
 * @brief TIFF tag: Color transforms to apply to masked image regions
 * (introduced by Adobe DNG).
 */
#define TIFFTAG_RGBTABLES 52543
/**
 * @brief TIFF tag: The illuminant used for the third set of color calibration
 * tags (introduced by Adobe DNG).
 */
#define TIFFTAG_CALIBRATIONILLUMINANT3 52529
/**
 * @brief TIFF tag: Matrix to convert XYZ values to reference camera native
 * color space under CalibrationIlluminant3 (introduced by Adobe DNG).
 */
#define TIFFTAG_COLORMATRIX3 52531
/**
 * @brief TIFF tag: Matrix to transform reference camera native space values to
 * individual camera native space values under CalibrationIlluminant3
 * (introduced by Adobe DNG).
 */
#define TIFFTAG_CAMERACALIBRATION3 52530
/**
 * @brief TIFF tag: Dimensionality reduction matrix for use in color conversion
 * to XYZ under CalibrationIlluminant3 (introduced by Adobe DNG).
 */
#define TIFFTAG_REDUCTIONMATRIX3 52538
/**
 * @brief TIFF tag: The data for the third HSV table (introduced by Adobe DNG).
 */
#define TIFFTAG_PROFILEHUESATMAPDATA3 52537
/**
 * @brief TIFF tag: Matrix to map white balanced camera colors to XYZ D50
 * (introduced by Adobe DNG).
 */
#define TIFFTAG_FORWARDMATRIX3 52532
/**
 * @brief TIFF tag: Data for the first calibration illuminant (introduced by
 * Adobe DNG).
 */
#define TIFFTAG_ILLUMINANTDATA1 52533
/**
 * @brief TIFF tag: Data for the second calibration illuminant (introduced by
 * Adobe DNG).
 */
#define TIFFTAG_ILLUMINANTDATA2 52534
/**
 * @brief TIFF tag: Data for the third calibration illuminant (introduced by
 * Adobe DNG).
 */
#define TIFFTAG_ILLUMINANTDATA3 52535

/* TIFF/EP */
/** @} */

/** @addtogroup libtiff_public_ep_tags
 * @{ */
/** @brief TIFF tag: Dimensions of CFA pattern. */
#define TIFFTAG_EP_CFAREPEATPATTERNDIM 33421
/** @brief TIFF tag: Color filter array pattern. */
#define TIFFTAG_EP_CFAPATTERN 33422
/** @brief TIFF tag: Battery level (rational or ASCII). */
#define TIFFTAG_EP_BATTERYLEVEL 33423
/** @brief TIFF tag: Number of multi-field images. */
#define TIFFTAG_EP_INTERLACE 34857
/* TIFFTAG_EP_IPTC_NAA and TIFFTAG_RICHTIFFIPTC share the same tag number (33723)
 *   LibTIFF type is UNDEFINED or BYTE, but often times incorrectly specified as LONG,
 *   because TIFF/EP (ISO/DIS 12234-2) specifies type LONG or ASCII. */
/** @brief TIFF tag: Alias IPTC/NAA Newspaper Association RichTIFF. */
#define TIFFTAG_EP_IPTC_NAA 33723
/** @brief TIFF tag: Time zone offset relative to UTC. */
#define TIFFTAG_EP_TIMEZONEOFFSET 34858
/** @brief TIFF tag: Number of seconds capture was delayed from button press. */
#define TIFFTAG_EP_SELFTIMERMODE 34859
/** @brief TIFF tag: Flash energy, or range if there is uncertainty. */
#define TIFFTAG_EP_FLASHENERGY 37387
/** @brief TIFF tag: Spatial frequency response. */
#define TIFFTAG_EP_SPATIALFREQUENCYRESPONSE 37388
/** @brief TIFF tag: Camera noise measurement values. */
#define TIFFTAG_EP_NOISE 37389
/** @brief TIFF tag: Focal plane X resolution. */
#define TIFFTAG_EP_FOCALPLANEXRESOLUTION 37390
/** @brief TIFF tag: Focal plane Y resolution. */
#define TIFFTAG_EP_FOCALPLANEYRESOLUTION 37391
/** @brief TIFF tag: Focal plane resolution unit. */
#define TIFFTAG_EP_FOCALPLANERESOLUTIONUNIT 37392
/**
 * @brief TIFF tag: Number of image when several of burst shot stored in same
 * TIFF/EP.
 */
#define TIFFTAG_EP_IMAGENUMBER 37393
/** @brief TIFF tag: Security classification. */
#define TIFFTAG_EP_SECURITYCLASSIFICATION 37394
/** @brief TIFF tag: Record of what has been done to the image. */
#define TIFFTAG_EP_IMAGEHISTORY 37395
/** @brief TIFF tag: Exposure index. */
#define TIFFTAG_EP_EXPOSUREINDEX 37397
/** @brief TIFF tag: TIFF/EP standard version, n.n.n.n. */
#define TIFFTAG_EP_STANDARDID 37398
/** @brief TIFF tag: Type of image sensor. */
#define TIFFTAG_EP_SENSINGMETHOD 37399
/*
 * TIFF/EP tags equivalent to EXIF tags
 *     Note that TIFF-EP and EXIF use nearly the same metadata tag set, but TIFF-EP stores the tags in IFD 0,
 *     while EXIF store the tags in a separate IFD. Either location is allowed by DNG, but the EXIF location is preferred.
 */
/** @brief TIFF tag: Exposure time. */
#define TIFFTAG_EP_EXPOSURETIME 33434
/** @brief TIFF tag: F number. */
#define TIFFTAG_EP_FNUMBER 33437
/** @brief TIFF tag: Exposure program. */
#define TIFFTAG_EP_EXPOSUREPROGRAM 34850
/** @brief TIFF tag: Spectral sensitivity. */
#define TIFFTAG_EP_SPECTRALSENSITIVITY 34852
/** @brief TIFF tag: ISO speed rating. */
#define TIFFTAG_EP_ISOSPEEDRATINGS 34855
/** @brief TIFF tag: Optoelectric conversion factor. */
#define TIFFTAG_EP_OECF 34856
/** @brief TIFF tag: Date and time of original data generation. */
#define TIFFTAG_EP_DATETIMEORIGINAL 36867
/** @brief TIFF tag: Image compression mode. */
#define TIFFTAG_EP_COMPRESSEDBITSPERPIXEL 37122
/** @brief TIFF tag: Shutter speed. */
#define TIFFTAG_EP_SHUTTERSPEEDVALUE 37377
/** @brief TIFF tag: Aperture. */
#define TIFFTAG_EP_APERTUREVALUE 37378
/** @brief TIFF tag: Brightness. */
#define TIFFTAG_EP_BRIGHTNESSVALUE 37379
/** @brief TIFF tag: Exposure bias. */
#define TIFFTAG_EP_EXPOSUREBIASVALUE 37380
/** @brief TIFF tag: Maximum lens aperture. */
#define TIFFTAG_EP_MAXAPERTUREVALUE 37381
/** @brief TIFF tag: Subject distance. */
#define TIFFTAG_EP_SUBJECTDISTANCE 37382
/** @brief TIFF tag: Metering mode. */
#define TIFFTAG_EP_METERINGMODE 37383
/** @brief TIFF tag: Light source. */
#define TIFFTAG_EP_LIGHTSOURCE 37384
/** @brief TIFF tag: Flash. */
#define TIFFTAG_EP_FLASH 37385
/** @brief TIFF tag: Lens focal length. */
#define TIFFTAG_EP_FOCALLENGTH 37386
/** @brief TIFF tag: Subject location (area). */
#define TIFFTAG_EP_SUBJECTLOCATION 37396

/** @} */

/** @addtogroup libtiff_public_tiff_tags
 * @{ */
/**
 * @brief TIFF tag: Rational polynomial coefficients for geospatial
 * georeferencing.
 */
#define TIFFTAG_RPCCOEFFICIENT 50844
/** @brief TIFF tag: Alias Sketchbook Pro layer usage description. */
#define TIFFTAG_ALIAS_LAYER_METADATA 50784

/* GeoTIFF DGIWG */
/** @brief TIFF tag: TIFF raster identifier metadata. */
#define TIFFTAG_TIFF_RSID 50908
/** @brief TIFF tag: Geospatial metadata. */
#define TIFFTAG_GEO_METADATA 50909
/** @brief TIFF tag: Offsets to additional DNG camera profiles. */
#define TIFFTAG_EXTRACAMERAPROFILES 50933

/* tag 65535 is an undefined tag used by Eastman Kodak */
/** @brief TIFF tag: Hue shift correction data. */
#define TIFFTAG_DCSHUESHIFTVALUES 65535

/*
 * The following are ``pseudo tags'' that can be used to control
 * codec-specific functionality.  These tags are not written to file.
 * Note that these values start at 0xffff+1 so that they'll never
 * collide with Aldus-assigned tags.
 *
 * If you want your private pseudo tags ``registered'' (i.e. added to
 * this file), please post a bug report via the tracking system at
 * http://www.remotesensing.org/libtiff/bugs.html with the appropriate
 * C definitions to add.
 */
/** @} */

/** @addtogroup libtiff_public_pseudotags
 * @{ */
/** @brief Pseudo-tag: Group 3/4 format control. */
#define TIFFTAG_FAXMODE 65536
/** @brief Default, include RTC. */
#define FAXMODE_CLASSIC 0x0000
/** @brief No RTC at end of data. */
#define FAXMODE_NORTC 0x0001
/** @brief No EOL code at end of row. */
#define FAXMODE_NOEOL 0x0002
/** @brief Byte align row. */
#define FAXMODE_BYTEALIGN 0x0004
/** @brief Word align row. */
#define FAXMODE_WORDALIGN 0x0008
/** @brief TIFF Class F. */
#define FAXMODE_CLASSF FAXMODE_NORTC
/** @brief Pseudo-tag: Compression quality level. */
#define TIFFTAG_JPEGQUALITY 65537
/* Note: quality level is on the IJG 0-100 scale.  Default value is 75 */
/** @brief Pseudo-tag: Auto RGB<=>YCbCr convert? */
#define TIFFTAG_JPEGCOLORMODE 65538
/** @brief No conversion (default). */
#define JPEGCOLORMODE_RAW 0x0000
/** @brief Do auto conversion. */
#define JPEGCOLORMODE_RGB 0x0001
/** @brief Pseudo-tag: What to put in JPEGTables. */
#define TIFFTAG_JPEGTABLESMODE 65539
/** @brief Include quantization tbls. */
#define JPEGTABLESMODE_QUANT 0x0001
/** @brief Include Huffman tbls. */
#define JPEGTABLESMODE_HUFF 0x0002
/* Note: default is JPEGTABLESMODE_QUANT | JPEGTABLESMODE_HUFF */
/** @brief Pseudo-tag: G3/G4 fill function. */
#define TIFFTAG_FAXFILLFUNC 65540
/** @brief Pseudo-tag: PixarLogCodec I/O data sz. */
#define TIFFTAG_PIXARLOGDATAFMT 65549
/** @brief Regular u_char samples. */
#define PIXARLOGDATAFMT_8BIT 0
/** @brief ABGR-order u_chars. */
#define PIXARLOGDATAFMT_8BITABGR 1
/** @brief 11-bit log-encoded (raw). */
#define PIXARLOGDATAFMT_11BITLOG 2
/** @brief As per PICIO (1.0==2048). */
#define PIXARLOGDATAFMT_12BITPICIO 3
/** @brief Signed short samples. */
#define PIXARLOGDATAFMT_16BIT 4
/** @brief IEEE float samples. */
#define PIXARLOGDATAFMT_FLOAT 5
/* 65550-65556 are allocated to Oceana Matrix <dev@oceana.com> */
/** @brief Pseudo-tag: Imager model & filter. */
#define TIFFTAG_DCSIMAGERTYPE 65550
/** @brief M3 chip (1280 x 1024). */
#define DCSIMAGERMODEL_M3 0
/** @brief M5 chip (1536 x 1024). */
#define DCSIMAGERMODEL_M5 1
/** @brief M6 chip (3072 x 2048). */
#define DCSIMAGERMODEL_M6 2
/** @brief Infrared filter. */
#define DCSIMAGERFILTER_IR 0
/** @brief Monochrome filter. */
#define DCSIMAGERFILTER_MONO 1
/** @brief Color filter array. */
#define DCSIMAGERFILTER_CFA 2
/** @brief Other filter. */
#define DCSIMAGERFILTER_OTHER 3
/** @brief Pseudo-tag: Interpolation mode. */
#define TIFFTAG_DCSINTERPMODE 65551
/** @brief Whole image, default. */
#define DCSINTERPMODE_NORMAL 0x0
/** @brief Preview of image (384x256). */
#define DCSINTERPMODE_PREVIEW 0x1
/** @brief Pseudo-tag: Color balance values. */
#define TIFFTAG_DCSBALANCEARRAY 65552
/** @brief Pseudo-tag: Color correction values. */
#define TIFFTAG_DCSCORRECTMATRIX 65553
/** @brief Pseudo-tag: Gamma value. */
#define TIFFTAG_DCSGAMMA 65554
/** @brief Pseudo-tag: Toe & shoulder points. */
#define TIFFTAG_DCSTOESHOULDERPTS 65555
/** @brief Pseudo-tag: Calibration file desc. */
#define TIFFTAG_DCSCALIBRATIONFD 65556
/* Note: quality level is on the ZLIB 1-9 scale. Default value is -1 */
/** @brief Pseudo-tag: Deflate compression level control. */
#define TIFFTAG_ZIPQUALITY 65557
/** @brief Pseudo-tag: PixarLog compression level control. */
#define TIFFTAG_PIXARLOGQUALITY 65558
/* 65559 is allocated to Oceana Matrix <dev@oceana.com> */
/** @brief Pseudo-tag: Area of image to acquire. */
#define TIFFTAG_DCSCLIPRECTANGLE 65559
/** @brief Pseudo-tag: SGILog user data format. */
#define TIFFTAG_SGILOGDATAFMT 65560
/** @brief IEEE float samples. */
#define SGILOGDATAFMT_FLOAT 0
/** @brief 16-bit samples. */
#define SGILOGDATAFMT_16BIT 1
/** @brief Uninterpreted data. */
#define SGILOGDATAFMT_RAW 2
/** @brief 8-bit RGB monitor values. */
#define SGILOGDATAFMT_8BIT 3
/** @brief Pseudo-tag: SGILog data encoding control. */
#define TIFFTAG_SGILOGENCODE 65561
/** @brief Do not dither encoded values. */
#define SGILOGENCODE_NODITHER 0
/** @brief Randomly dither encoded values. */
#define SGILOGENCODE_RANDITHER 1
/** @brief Pseudo-tag: LZMA2 preset (compression level). */
#define TIFFTAG_LZMAPRESET 65562
/** @brief Pseudo-tag: Interface for per sample tags. */
#define TIFFTAG_PERSAMPLE 65563
/** @brief Present as a single value. */
#define PERSAMPLE_MERGED 0
/** @brief Present as multiple values. */
#define PERSAMPLE_MULTI 1
/** @brief Pseudo-tag: ZSTD compression level. */
#define TIFFTAG_ZSTD_LEVEL 65564
/** @brief Pseudo-tag: LERC version. */
#define TIFFTAG_LERC_VERSION 65565
/** @brief Select the LERC 2.4-compatible bitstream format. */
#define LERC_VERSION_2_4 4
/** @brief Pseudo-tag: LERC additional compression. */
#define TIFFTAG_LERC_ADD_COMPRESSION 65566
/** @brief No additional compression around the LERC bitstream. */
#define LERC_ADD_COMPRESSION_NONE 0
/** @brief Deflate compression around the LERC bitstream. */
#define LERC_ADD_COMPRESSION_DEFLATE 1
/** @brief Zstandard compression around the LERC bitstream. */
#define LERC_ADD_COMPRESSION_ZSTD 2
/** @brief Pseudo-tag: LERC maximum error. */
#define TIFFTAG_LERC_MAXZERROR 65567
/** @brief Pseudo-tag: WebP quality setting. */
#define TIFFTAG_WEBP_LEVEL 65568
/** @brief Pseudo-tag: WebP lossless/lossy. */
#define TIFFTAG_WEBP_LOSSLESS 65569
/**
 * The WebP codec defaults to preserving these RGB values. Setting the control
 * to zero may improve compression by allowing them to change. It is a codec
 * pseudo-tag that can be set and queried but is not written to the file.
 */
/**
 * @brief Pseudo-tag: Preserve RGB values under fully transparent pixels in
 * lossless WebP.
 */
#define TIFFTAG_WEBP_LOSSLESS_EXACT 65571
/**
 * Defaults to libdeflate when that implementation is available. This control
 * selects an implementation of Deflate, not a different on-disk compression
 * identifier.
 */
/** @brief Pseudo-tag: Select the zlib or libdeflate Deflate implementation. */
#define TIFFTAG_DEFLATE_SUBCODEC 65570
/** @brief Select the zlib Deflate implementation. */
#define DEFLATE_SUBCODEC_ZLIB 0
/** @brief Select the libdeflate Deflate implementation. */
#define DEFLATE_SUBCODEC_LIBDEFLATE 1

/*
 * EXIF tags
 */
/** @} */

/** @addtogroup libtiff_public_exif_tags
 * @{ */
/** @brief EXIF tag: Exposure time. */
#define EXIFTAG_EXPOSURETIME 33434
/** @brief EXIF tag: F number. */
#define EXIFTAG_FNUMBER 33437
/** @brief EXIF tag: Exposure program. */
#define EXIFTAG_EXPOSUREPROGRAM 34850
/** @brief EXIF tag: Spectral sensitivity. */
#define EXIFTAG_SPECTRALSENSITIVITY 34852
/* After EXIF 2.2.1 ISOSpeedRatings is named PhotographicSensitivity.
   In addition, while "Count=Any", only 1 count should be used. */
/** @brief EXIF tag: ISO speed rating. */
#define EXIFTAG_ISOSPEEDRATINGS 34855
/** @brief EXIF tag: Photographic Sensitivity (new name for tag 34855). */
#define EXIFTAG_PHOTOGRAPHICSENSITIVITY 34855
/** @brief EXIF tag: Optoelectric conversion factor. */
#define EXIFTAG_OECF 34856
/** @brief EXIF tag: Exif version. */
#define EXIFTAG_EXIFVERSION 36864
/** @brief EXIF tag: Date and time of original data generation. */
#define EXIFTAG_DATETIMEORIGINAL 36867
/** @brief EXIF tag: Date and time of digital data generation. */
#define EXIFTAG_DATETIMEDIGITIZED 36868
/** @brief EXIF tag: Meaning of each component. */
#define EXIFTAG_COMPONENTSCONFIGURATION 37121
/** @brief EXIF tag: Image compression mode. */
#define EXIFTAG_COMPRESSEDBITSPERPIXEL 37122
/** @brief EXIF tag: Shutter speed. */
#define EXIFTAG_SHUTTERSPEEDVALUE 37377
/** @brief EXIF tag: Aperture. */
#define EXIFTAG_APERTUREVALUE 37378
/** @brief EXIF tag: Brightness. */
#define EXIFTAG_BRIGHTNESSVALUE 37379
/** @brief EXIF tag: Exposure bias. */
#define EXIFTAG_EXPOSUREBIASVALUE 37380
/** @brief EXIF tag: Maximum lens aperture. */
#define EXIFTAG_MAXAPERTUREVALUE 37381
/** @brief EXIF tag: Subject distance. */
#define EXIFTAG_SUBJECTDISTANCE 37382
/** @brief EXIF tag: Metering mode. */
#define EXIFTAG_METERINGMODE 37383
/** @brief EXIF tag: Light source. */
#define EXIFTAG_LIGHTSOURCE 37384
/** @brief EXIF tag: Flash. */
#define EXIFTAG_FLASH 37385
/** @brief EXIF tag: Lens focal length. */
#define EXIFTAG_FOCALLENGTH 37386
/** @brief EXIF tag: Subject area. */
#define EXIFTAG_SUBJECTAREA 37396
/** @brief EXIF tag: Manufacturer notes. */
#define EXIFTAG_MAKERNOTE 37500
/** @brief EXIF tag: User comments. */
#define EXIFTAG_USERCOMMENT 37510
/** @brief EXIF tag: DateTime subseconds. */
#define EXIFTAG_SUBSECTIME 37520
/** @brief EXIF tag: DateTimeOriginal subseconds. */
#define EXIFTAG_SUBSECTIMEORIGINAL 37521
/** @brief EXIF tag: DateTimeDigitized subseconds. */
#define EXIFTAG_SUBSECTIMEDIGITIZED 37522
/** @brief EXIF tag: Supported Flashpix version. */
#define EXIFTAG_FLASHPIXVERSION 40960
/** @brief EXIF tag: Color space information. */
#define EXIFTAG_COLORSPACE 40961
/** @brief EXIF tag: Valid image width. */
#define EXIFTAG_PIXELXDIMENSION 40962
/** @brief EXIF tag: Valid image height. */
#define EXIFTAG_PIXELYDIMENSION 40963
/** @brief EXIF tag: Related audio file. */
#define EXIFTAG_RELATEDSOUNDFILE 40964
/** @brief EXIF tag: Flash energy. */
#define EXIFTAG_FLASHENERGY 41483
/** @brief EXIF tag: Spatial frequency response. */
#define EXIFTAG_SPATIALFREQUENCYRESPONSE 41484
/** @brief EXIF tag: Focal plane X resolution. */
#define EXIFTAG_FOCALPLANEXRESOLUTION 41486
/** @brief EXIF tag: Focal plane Y resolution. */
#define EXIFTAG_FOCALPLANEYRESOLUTION 41487
/** @brief EXIF tag: Focal plane resolution unit. */
#define EXIFTAG_FOCALPLANERESOLUTIONUNIT 41488
/** @brief EXIF tag: Subject location. */
#define EXIFTAG_SUBJECTLOCATION 41492
/** @brief EXIF tag: Exposure index. */
#define EXIFTAG_EXPOSUREINDEX 41493
/** @brief EXIF tag: Sensing method. */
#define EXIFTAG_SENSINGMETHOD 41495
/** @brief EXIF tag: File source. */
#define EXIFTAG_FILESOURCE 41728
/** @brief EXIF tag: Scene type. */
#define EXIFTAG_SCENETYPE 41729
/** @brief EXIF tag: CFA pattern. */
#define EXIFTAG_CFAPATTERN 41730
/** @brief EXIF tag: Custom image processing. */
#define EXIFTAG_CUSTOMRENDERED 41985
/** @brief EXIF tag: Exposure mode. */
#define EXIFTAG_EXPOSUREMODE 41986
/** @brief EXIF tag: White balance. */
#define EXIFTAG_WHITEBALANCE 41987
/** @brief EXIF tag: Digital zoom ratio. */
#define EXIFTAG_DIGITALZOOMRATIO 41988
/** @brief EXIF tag: Focal length in 35 mm film. */
#define EXIFTAG_FOCALLENGTHIN35MMFILM 41989
/** @brief EXIF tag: Scene capture type. */
#define EXIFTAG_SCENECAPTURETYPE 41990
/** @brief EXIF tag: Gain control. */
#define EXIFTAG_GAINCONTROL 41991
/** @brief EXIF tag: Contrast. */
#define EXIFTAG_CONTRAST 41992
/** @brief EXIF tag: Saturation. */
#define EXIFTAG_SATURATION 41993
/** @brief EXIF tag: Sharpness. */
#define EXIFTAG_SHARPNESS 41994
/** @brief EXIF tag: Device settings description. */
#define EXIFTAG_DEVICESETTINGDESCRIPTION 41995
/** @brief EXIF tag: Subject distance range. */
#define EXIFTAG_SUBJECTDISTANCERANGE 41996
/** @brief EXIF tag: Unique image ID. */
#define EXIFTAG_IMAGEUNIQUEID 42016

/*--: New for EXIF-Version 2.32, May 2019 ... */
/**
 * @brief EXIF tag: The SensitivityType tag indicates which one of the
 * parameters of ISO12232 is the PhotographicSensitivity tag.
 */
#define EXIFTAG_SENSITIVITYTYPE 34864
/**
 * @brief EXIF tag: This tag indicates the standard output sensitivity value of
 * a camera or input device defined in ISO 12232.
 */
#define EXIFTAG_STANDARDOUTPUTSENSITIVITY 34865
/** @brief EXIF tag: Recommended exposure index. */
#define EXIFTAG_RECOMMENDEDEXPOSUREINDEX 34866
/** @brief EXIF tag: ISO speed value. */
#define EXIFTAG_ISOSPEED 34867
/** @brief EXIF tag: ISO speed latitude yyy. */
#define EXIFTAG_ISOSPEEDLATITUDEYYY 34868
/** @brief EXIF tag: ISO speed latitude zzz. */
#define EXIFTAG_ISOSPEEDLATITUDEZZZ 34869
/** @brief EXIF tag: Offset from UTC of the time of DateTime tag. */
#define EXIFTAG_OFFSETTIME 36880
/** @brief EXIF tag: Offset from UTC of the time of DateTimeOriginal tag. */
#define EXIFTAG_OFFSETTIMEORIGINAL 36881
/** @brief EXIF tag: Offset from UTC of the time of DateTimeDigitized tag. */
#define EXIFTAG_OFFSETTIMEDIGITIZED 36882
/** @brief EXIF tag: Ambient temperature at capture, in degrees Celsius. */
#define EXIFTAG_TEMPERATURE 37888
/**
 * @brief EXIF tag: Humidity as the ambient situation at the shot in percent.
 */
#define EXIFTAG_HUMIDITY 37889
/**
 * @brief EXIF tag: Pressure as the ambient situation at the shot hecto-Pascal
 * (hPa).
 */
#define EXIFTAG_PRESSURE 37890
/**
 * @brief EXIF tag: WaterDepth as the ambient situation at the shot in meter
 * (m).
 */
#define EXIFTAG_WATERDEPTH 37891
/** @brief EXIF tag: Scalar acceleration at capture, in mGal (10^-5 m/s^2). */
#define EXIFTAG_ACCELERATION 37892
/* EXIFTAG_CAMERAELEVATIONANGLE: Elevation/depression. angle of the orientation of the  camera(imaging optical axis)
 *                               as the ambient situation at the shot in degree from -180deg to +180deg. */
/**
 * @brief EXIF tag: Camera elevation/depression angle in degrees, from -180 to
 * +180.
 */
#define EXIFTAG_CAMERAELEVATIONANGLE 37893
/** @brief EXIF tag: Owner of a camera. */
#define EXIFTAG_CAMERAOWNERNAME 42032
/** @brief EXIF tag: Serial number of the body of the camera. */
#define EXIFTAG_BODYSERIALNUMBER 42033
/* EXIFTAG_LENSSPECIFICATION: minimum focal length (in mm), maximum focal length (in mm),minimum F number in the minimum focal length,
 *                            and minimum F number in the maximum focal length, */
/**
 * @brief EXIF tag: Minimum/maximum lens focal lengths in millimetres and
 * corresponding minimum F numbers.
 */
#define EXIFTAG_LENSSPECIFICATION 42034
/** @brief EXIF tag: The lens manufacturer. */
#define EXIFTAG_LENSMAKE 42035
/** @brief EXIF tag: The lens model name and model number. */
#define EXIFTAG_LENSMODEL 42036
/** @brief EXIF tag: The serial number of the interchangeable lens. */
#define EXIFTAG_LENSSERIALNUMBER 42037
/** @brief EXIF tag: Value of coefficient gamma. */
#define EXIFTAG_GAMMA 42240
/** @brief EXIF tag: Composite image. */
#define EXIFTAG_COMPOSITEIMAGE 42080
/** @brief EXIF tag: Source image number of composite image. */
#define EXIFTAG_SOURCEIMAGENUMBEROFCOMPOSITEIMAGE 42081
/** @brief EXIF tag: Source exposure times of composite image. */
#define EXIFTAG_SOURCEEXPOSURETIMESOFCOMPOSITEIMAGE 42082

/*
 * EXIF-GPS tags  (Version 2.31, July 2016)
 */
/** @} */

/** @addtogroup libtiff_public_gps_tags
 * @{ */
/** @brief GPS tag: Indicates the version of GPSInfoIFD. */
#define GPSTAG_VERSIONID 0
/**
 * @brief GPS tag: Indicates whether the latitude is north or south latitude.
 */
#define GPSTAG_LATITUDEREF 1
/** @brief GPS tag: Indicates the latitude. */
#define GPSTAG_LATITUDE 2
/**
 * @brief GPS tag: Indicates whether the longitude is east or west longitude.
 */
#define GPSTAG_LONGITUDEREF 3
/** @brief GPS tag: Indicates the longitude. */
#define GPSTAG_LONGITUDE 4
/** @brief GPS tag: Indicates the altitude used as the reference altitude. */
#define GPSTAG_ALTITUDEREF 5
/**
 * @brief GPS tag: Indicates the altitude based on the reference in
 * GPSAltitudeRef.
 */
#define GPSTAG_ALTITUDE 6
/** @brief GPS tag: Indicates the time as UTC (Coordinated Universal Time). */
#define GPSTAG_TIMESTAMP 7
/** @brief GPS tag: Indicates the GPS satellites used for measurements. */
#define GPSTAG_SATELLITES 8
/**
 * @brief GPS tag: Indicates the status of the GPS receiver when the image is
 * recorded.
 */
#define GPSTAG_STATUS 9
/** @brief GPS tag: Indicates the GPS measurement mode. */
#define GPSTAG_MEASUREMODE 10
/** @brief GPS tag: Indicates the GPS DOP (data degree of precision). */
#define GPSTAG_DOP 11
/**
 * @brief GPS tag: Indicates the unit used to express the GPS receiver speed of
 * movement.
 */
#define GPSTAG_SPEEDREF 12
/** @brief GPS tag: Indicates the speed of GPS receiver movement. */
#define GPSTAG_SPEED 13
/**
 * @brief GPS tag: Indicates the reference for giving the direction of GPS
 * receiver movement.
 */
#define GPSTAG_TRACKREF 14
/** @brief GPS tag: Indicates the direction of GPS receiver movement. */
#define GPSTAG_TRACK 15
/**
 * @brief GPS tag: Indicates the reference for giving the direction of the image
 * when it is captured.
 */
#define GPSTAG_IMGDIRECTIONREF 16
/**
 * @brief GPS tag: Indicates the direction of the image when it was captured.
 */
#define GPSTAG_IMGDIRECTION 17
/**
 * @brief GPS tag: Indicates the geodetic survey data used by the GPS receiver.
 * (e.g. WGS-84).
 */
#define GPSTAG_MAPDATUM 18
/**
 * @brief GPS tag: Indicates whether the latitude of the destination point is
 * north or south latitude.
 */
#define GPSTAG_DESTLATITUDEREF 19
/** @brief GPS tag: Indicates the latitude of the destination point. */
#define GPSTAG_DESTLATITUDE 20
/**
 * @brief GPS tag: Indicates whether the longitude of the destination point is
 * east or west longitude.
 */
#define GPSTAG_DESTLONGITUDEREF 21
/** @brief GPS tag: Indicates the longitude of the destination point. */
#define GPSTAG_DESTLONGITUDE 22
/**
 * @brief GPS tag: Indicates the reference used for giving the bearing to the
 * destination point.
 */
#define GPSTAG_DESTBEARINGREF 23
/** @brief GPS tag: Indicates the bearing to the destination point. */
#define GPSTAG_DESTBEARING 24
/**
 * @brief GPS tag: Indicates the unit used to express the distance to the
 * destination point.
 */
#define GPSTAG_DESTDISTANCEREF 25
/** @brief GPS tag: Indicates the distance to the destination point. */
#define GPSTAG_DESTDISTANCE 26
/**
 * @brief GPS tag: A character string recording the name of the method used for
 * location finding.
 */
#define GPSTAG_PROCESSINGMETHOD 27
/** @brief GPS tag: A character string recording the name of the GPS area. */
#define GPSTAG_AREAINFORMATION 28
/**
 * @brief GPS tag: A character string recording date and time information
 * relative to UTC (Coordinated Universal Time).
 */
#define GPSTAG_DATESTAMP 29
/**
 * @brief GPS tag: Indicates whether differential correction is applied to the
 * GPS receiver.
 */
#define GPSTAG_DIFFERENTIAL 30
/** @brief GPS tag: Indicates horizontal positioning errors in meters. */
#define GPSTAG_GPSHPOSITIONINGERROR 31

/** @} */

#endif /* _TIFF_ */

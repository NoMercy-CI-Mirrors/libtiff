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
 * @file tiffio.h
 * @brief Declare the public C library interface and its supporting types.
 */

#ifndef _TIFFIO_
#define _TIFFIO_

/*
 * TIFF I/O Library Definitions.
 */
#include "tiff.h"
#include "tiffvers.h"

/*
 * TIFF is defined as an incomplete type to hide the
 * library's internal data structures from clients.
 */
/** @addtogroup libtiff_public_types
 * @{ */

/**
 * @brief Refer to an opaque open TIFF library instance.
 *
 * Obtain handles through the open or client-open interfaces. Handle ownership
 * and the lifetime of client I/O objects are defined by those interfaces.
 * TIFFClose() or TIFFCleanup() invalidates the handle and pointers to storage
 * owned by it. Applications must not access the private struct tiff layout.
 */
typedef struct tiff TIFF;

/*
 * The following typedefs define the intrinsic size of
 * data types used in the *exported* interfaces.  These
 * definitions depend on the proper definition of types
 * in tiff.h.  Note also that the varargs interface used
 * to pass tag types and values uses the types defined in
 * tiff.h directly.
 *
 * NB: ttag_t -> deprecated and replaced by uint32_t
 *     is unsigned int and not unsigned short because
 *     ANSI C requires that the type before the ellipsis be a
 *     promoted type (i.e. one of int, unsigned int, pointer,
 *     or double) and because we defined pseudo-tags that are
 *     outside the range of legal Aldus-assigned tags.
 * NB: tsize_t -> deprecated and replaced by tmsize_t
 *     is signed and not unsigned because some functions
 *     return -1.
 * NB: toff_t is not off_t for many reasons; TIFFs max out at
 *     32-bit file offsets, and BigTIFF maxes out at 64-bit
 *     offsets being the most important, and to ensure use of
 *     a consistently unsigned type across architectures.
 *     Prior to libtiff 4.0, this was an unsigned 32 bit type.
 */
/*
 * this is the machine addressing size type, only it's signed, so make it
 * int32_t on 32bit machines, int64_t on 64bit machines
 */
/**
 * @brief Represent a signed memory or I/O size in bytes.
 *
 * Has the addressing width of the build platform. Signedness permits negative
 * error sentinels; the meaning of a negative value depends on the interface.
 */
typedef TIFF_SSIZE_T tmsize_t;
/** @brief Define the largest positive size representable by tmsize_t. */
#define TIFF_TMSIZE_T_MAX (tmsize_t)(SIZE_MAX >> 1)

/** @brief Represent an unsigned file byte offset independently of off_t. */
typedef uint64_t toff_t;
/** @brief Represent a zero-based directory index, rather than a file offset. */
typedef uint32_t tdir_t;

/* the following are deprecated and should be replaced by their defining
   counterparts */
/** @brief Represent a legacy directory tag identifier.
 * @deprecated Use uint32_t instead. */
typedef uint32_t ttag_t;
/** @brief Represent a legacy sample index.
 * @deprecated Use uint16_t instead. */
typedef uint16_t tsample_t;
/** @brief Represent a legacy strip or tile index.
 * @deprecated Use uint32_t instead. */
typedef uint32_t tstrile_t;
/** @brief Represent a legacy strip index.
 * @deprecated Use uint32_t instead. */
typedef tstrile_t tstrip_t;
/** @brief Represent a legacy tile index.
 * @deprecated Use uint32_t instead. */
typedef tstrile_t ttile_t;
/** @brief Represent a legacy signed I/O byte count.
 * @deprecated Use tmsize_t instead. */
typedef tmsize_t tsize_t;
/** @brief Represent a legacy untyped image-data pointer.
 * @deprecated Use void * instead. */
typedef void *tdata_t;

/*
 * On windows you should define USE_WIN32_FILEIO if you are using tif_win32.c
 * or AVOID_WIN32_FILEIO if you are using something else (like tif_unix.c).
 *
 * By default tif_unix.c is assumed.
 */

#if defined(_WIN32)
#if !defined(__CYGWIN) && !defined(AVOID_WIN32_FILEIO) &&                      \
    !defined(USE_WIN32_FILEIO)
/**
 * @brief Select the non-native-Windows I/O convention in public declarations.
 * @ingroup libtiff_public_data
 */
#define AVOID_WIN32_FILEIO
#endif
#endif

#if defined(USE_WIN32_FILEIO)
/**
 * @brief Limit Windows header declarations requested by this header.
 * @ingroup libtiff_public_data
 */
#define VC_EXTRALEAN
#include <windows.h>
#ifdef _WIN32
/** @brief Represent a native Windows file handle for client I/O callbacks. */
DECLARE_HANDLE(thandle_t); /* Win32 file handle */
#else
/** @brief Represent a legacy Windows file handle for client I/O callbacks. */
typedef HFILE thandle_t; /* client data handle */
#endif /* _WIN32 */
#else
/**
 * @brief Carry an opaque client I/O context through library callbacks.
 *
 * The client-open interface defines ownership and lifetime; this typedef does
 * not allocate, retain, or release the referenced object.
 */
typedef void *thandle_t; /* client data handle */
#endif /* USE_WIN32_FILEIO */

/** @} */

/*
 * Flags to pass to TIFFPrintDirectory to control
 * printing of data structures that are potentially
 * very large.   Bit-or these flags to enable printing
 * multiple items.
 */
/**
 * @brief Request no optional large-data expansions in TIFFPrintDirectory().
 * @ingroup libtiff_public_data
 *
 * Zero disables optional core table expansions, not custom arrays, raw XML,
 * or callback output; it is not a total output-size limit. See @ref
 * directory_printing.
 */
#define TIFFPRINT_NONE 0x0 /* no extra info */
/**
 * @brief Include strip/tile offsets and byte counts in directory printing.
 * @ingroup libtiff_public_data
 *
 * Includes every logical strile when offsets are marked present. Accessors
 * may load deferred metadata, allocate, and change I/O/cache state; failed
 * queries can print zero without separate status. See @ref
 * directory_printing.
 */
#define TIFFPRINT_STRIPS 0x1 /* strips/tiles info */
/**
 * @brief Include transfer-function curves in directory printing.
 * @ingroup libtiff_public_data
 *
 * Expands present transfer functions to 2^BitsPerSample rows with up to
 * three colour columns. Requires consistent array extents; it is not a
 * validation flag. See @ref directory_printing.
 */
#define TIFFPRINT_CURVES 0x2 /* color/gray response curves */
/**
 * @brief Include palette entries in directory printing.
 * @ingroup libtiff_public_data
 *
 * Expands a present three-plane palette to 2^BitsPerSample rows. Requires
 * consistent plane extents; very large output remains possible. See @ref
 * directory_printing.
 */
#define TIFFPRINT_COLORMAP 0x4 /* colormap */
/**
 * @brief Retain the legacy JPEG quantisation-table print flag.
 * @ingroup libtiff_public_data
 *
 * The default directory printer does not inspect this legacy bit.
 */
#define TIFFPRINT_JPEGQTABLES 0x100 /* JPEG Q matrices */
/**
 * @brief Retain the legacy JPEG AC-table print flag.
 * @ingroup libtiff_public_data
 *
 * Shares its value with TIFFPRINT_JPEGDCTABLES. The default directory
 * printer does not inspect this legacy bit.
 */
#define TIFFPRINT_JPEGACTABLES 0x200 /* JPEG AC tables */
/**
 * @brief Retain the legacy JPEG DC-table print flag.
 * @ingroup libtiff_public_data
 *
 * Shares its value with TIFFPRINT_JPEGACTABLES. The default directory
 * printer does not inspect this legacy bit.
 */
#define TIFFPRINT_JPEGDCTABLES 0x200 /* JPEG DC tables */

/*
 * Colour conversion stuff
 */

/* reference white */
/**
 * @brief Define the D65 reference-white X coordinate, scaled to Y = 100.
 * @ingroup libtiff_public_data
 */
#define D65_X0 (95.0470f)
/**
 * @brief Define the D65 reference-white Y coordinate (100).
 * @ingroup libtiff_public_data
 */
#define D65_Y0 (100.0f)
/**
 * @brief Define the D65 reference-white Z coordinate, scaled to Y = 100.
 * @ingroup libtiff_public_data
 */
#define D65_Z0 (108.8827f)

/**
 * @brief Define the D50 reference-white X coordinate, scaled to Y = 100.
 * @ingroup libtiff_public_data
 */
#define D50_X0 (96.4250f)
/**
 * @brief Define the D50 reference-white Y coordinate (100).
 * @ingroup libtiff_public_data
 */
#define D50_Y0 (100.0f)
/**
 * @brief Define the D50 reference-white Z coordinate, scaled to Y = 100.
 * @ingroup libtiff_public_data
 */
#define D50_Z0 (82.4680f)

/* Structure for holding information about a display device. */

/** @brief Represent an 8-bit RGB channel value.
 * @ingroup libtiff_public_data
 */
typedef unsigned char TIFFRGBValue;

/**
 * @brief Describe the matrix, luminance, and gamma of an RGB display.
 * @ingroup libtiff_public_data
 *
 * All values are stored inline; the structure owns no external allocations.
 * TIFFCIELabToRGBInit() copies the display description into its conversion
 * state.
 */
typedef struct
{
    /** @brief 3 by 3 matrix mapping XYZ to red, green, and blue luminance. */
    float d_mat[3][3];
    /** @brief Red-channel reference-white luminance. */
    float d_YCR;
    /** @brief Green-channel reference-white luminance. */
    float d_YCG;
    /** @brief Blue-channel reference-white luminance. */
    float d_YCB;
    /** @brief Red-channel code value at reference white. */
    uint32_t d_Vrwr;
    /** @brief Green-channel code value at reference white. */
    uint32_t d_Vrwg;
    /** @brief Blue-channel code value at reference white. */
    uint32_t d_Vrwb;
    /** @brief Red-channel residual luminance at black. */
    float d_Y0R;
    /** @brief Green-channel residual luminance at black. */
    float d_Y0G;
    /** @brief Blue-channel residual luminance at black. */
    float d_Y0B;
    /** @brief Red-channel gamma. */
    float d_gammaR;
    /** @brief Green-channel gamma. */
    float d_gammaG;
    /** @brief Blue-channel gamma. */
    float d_gammaB;
} TIFFDisplay;

/**
 * @brief Hold lookup-table pointers for 8-bit YCbCr-to-RGB conversion.
 * @ingroup libtiff_public_data
 *
 * TIFFYCbCrToRGBInit() lays out the tables in caller-provided storage after
 * this structure. Allocating sizeof(TIFFYCbCrToRGB) alone is insufficient. The
 * required byte capacity is:
 * @code
 * TIFFroundup_32(sizeof(TIFFYCbCrToRGB), sizeof(long)) +
 *     4 * 256 * sizeof(TIFFRGBValue) + 2 * 256 * sizeof(int) +
 *     3 * 256 * sizeof(int32_t)
 * @endcode
 * The rounding helper is private; external callers must provide equivalent
 * alignment and capacity. Table pointers borrow storage within the same block;
 * they must not be freed separately. Keep the block valid for every conversion
 * and release it through its matching allocator after the last use.
 */
typedef struct
{ /* YCbCr->RGB support */
    /**
     * @brief Biased table pointer for clamping channel results to 0 through
     * 255.
     */
    TIFFRGBValue *clamptab;
    /** @brief 256-entry table of Cr contributions to red. */
    int *Cr_r_tab;
    /** @brief 256-entry table of Cb contributions to blue. */
    int *Cb_b_tab;
    /** @brief 256-entry fixed-point table of Cr contributions to green. */
    int32_t *Cr_g_tab;
    /** @brief 256-entry fixed-point table of Cb contributions to green. */
    int32_t *Cb_g_tab;
    /** @brief 256-entry table of scaled luminance contributions. */
    int32_t *Y_tab;
} TIFFYCbCrToRGB;

/**
 * @brief Hold CIE L*a*b*-to-RGB conversion parameters and tables.
 * @ingroup libtiff_public_data
 *
 * Initialise with TIFFCIELabToRGBInit() before conversion. The display and
 * lookup tables are inline values; this structure has no separately owned
 * allocations.
 */
typedef struct
{ /* CIE Lab 1976->RGB support */
    /** @brief Largest table index; initialised to CIELABTORGB_TABLE_RANGE. */
    int range;
/**
 * @brief Define the largest CIE L*a*b*-to-RGB lookup-table index.
 * @ingroup libtiff_public_data
 *
 * Each conversion array holds this many intervals plus one endpoint.
 */
#define CIELABTORGB_TABLE_RANGE 1500
    /** @brief Red luminance increment per lookup-table interval. */
    float rstep;
    /**
     * @brief Green luminance increment per lookup-table interval. Currently
     * initialised from the red-channel d_YCR/d_Y0R values by
     * TIFFCIELabToRGBInit().
     */
    float gstep;
    /**
     * @brief Blue luminance increment per lookup-table interval. Currently
     * initialised from the red-channel d_YCR/d_Y0R values by
     * TIFFCIELabToRGBInit().
     */
    float bstep;
    /** @brief Reference-white X coordinate. */
    float X0;
    /** @brief Reference-white Y coordinate. */
    float Y0;
    /** @brief Reference-white Z coordinate. */
    float Z0;
    /** @brief Inline copy of the display description. */
    TIFFDisplay display;
    /** @brief Red luminance-to-code table, including both endpoints. */
    float Yr2r[CIELABTORGB_TABLE_RANGE + 1];
    /** @brief Green luminance-to-code table, including both endpoints. */
    float Yg2g[CIELABTORGB_TABLE_RANGE + 1];
    /** @brief Blue luminance-to-code table, including both endpoints. */
    float Yb2b[CIELABTORGB_TABLE_RANGE + 1];
} TIFFCIELabToRGB;

/*
 * RGBA-style image support.
 */
/** @brief Name the RGBA reader state type.
 * @ingroup libtiff_public_data
 */
typedef struct _TIFFRGBAImage TIFFRGBAImage;
/*
 * The image reading and conversion routines invoke
 * ``put routines'' to copy/image/whatever tiles of
 * raw image data.  A default set of routines are
 * provided to convert/copy raw image data to 8-bit
 * packed ABGR format rasters.  Applications can supply
 * alternate routines that unpack the data into a
 * different format or, for example, unpack the data
 * and draw the unpacked raster on the display.
 */
/**
 * @brief Convert a decoded contiguous region through a caller-selectable
 * packer.
 * @ingroup libtiff_public_rgba
 *
 * Invoked synchronously by RGBA getters. Default callbacks produce
 * numeric 0xAABBGGRR pixels, normally with associated alpha; see
 * TIFFRGBAImageGet() for the grayscale exception. Custom callbacks define their
 * output interpretation while respecting supplied capacities and row steps.
 * No pointers or ownership transfer. Input is temporary decoded storage and
 * must not be retained; state/handle and callback code must remain live
 * during dispatch. There is no status return; report errors through an
 * application-defined mechanism without closing or recursively decoding the
 * active handle.
 *
 * @param[in,out] img Borrowed reader state; default packers read conversion
 * maps without changing them.
 * @param[out] cp Borrowed destination region; valid for all requested rows
 * and signed row steps.
 * @param[in] x Horizontal coordinate within the copied image rectangle,
 * excluding the rectangle's placement offset in the destination raster
 * (cp already includes it); zero for strip getters.
 * @param[in] y Destination row, within the copied image rectangle, of the
 * first decoded row, with any vertical flip applied but excluding the
 * placement offset. The getter's horizontal flip runs after the callbacks,
 * so x and y are not final raster positions.
 * @param[in] w Visible row width in pixels.
 * @param[in] h Visible row count.
 * @param[in] fromskew Source-row skip in pixels; callback scales it for its
 * format.
 * @param[in] toskew Signed output-row adjustment after w pixels, in
 * uint32_t elements.
 * @param[in] pp Borrowed contiguous decoded samples with selected-format
 * padding/alignment.
 */
typedef void (*tileContigRoutine)(TIFFRGBAImage *img, uint32_t *cp, uint32_t x,
                                  uint32_t y, uint32_t w, uint32_t h,
                                  int32_t fromskew, int32_t toskew,
                                  unsigned char *pp);
/**
 * @brief Convert a decoded separate-plane region through a caller-selectable
 * packer.
 * @ingroup libtiff_public_rgba
 *
 * Invoked synchronously by RGBA getters. Default callbacks produce
 * associated numeric 0xAABBGGRR pixels; custom callbacks define their
 * output interpretation while respecting supplied capacities and row steps.
 * No pointers or ownership transfer. Input is temporary decoded storage and
 * must not be retained; state/handle and callback code must remain live
 * during dispatch. There is no status return; report errors through an
 * application-defined mechanism without closing or recursively decoding the
 * active handle.
 *
 * @param[in,out] img Borrowed reader state; default packers read conversion
 * maps without changing them.
 * @param[out] cp Borrowed destination region; valid for all requested rows
 * and signed row steps.
 * @param[in] x Horizontal coordinate within the copied image rectangle,
 * excluding the rectangle's placement offset in the destination raster
 * (cp already includes it); zero for strip getters.
 * @param[in] y Destination row, within the copied image rectangle, of the
 * first decoded row, with any vertical flip applied but excluding the
 * placement offset. The getter's horizontal flip runs after the callbacks,
 * so x and y are not final raster positions.
 * @param[in] w Visible row width in pixels.
 * @param[in] h Visible row count.
 * @param[in] fromskew Source-row skip in pixels; callback scales it for its
 * format.
 * @param[in] toskew Signed output-row adjustment after w pixels, in
 * uint32_t elements.
 * @param[in] r Borrowed non-NULL first decoded plane.
 * @param[in] g Borrowed non-NULL second decoded plane; for one-channel images
 * it aliases r.
 * @param[in] b Borrowed non-NULL third decoded plane; for one-channel images
 * it aliases r.
 * @param[in] a Borrowed fourth decoded plane (alpha, or black for 8-bit CMYK)
 * when present, otherwise NULL.
 */
typedef void (*tileSeparateRoutine)(TIFFRGBAImage *img, uint32_t *cp,
                                    uint32_t x, uint32_t y, uint32_t w,
                                    uint32_t h, int32_t fromskew,
                                    int32_t toskew, unsigned char *r,
                                    unsigned char *g, unsigned char *b,
                                    unsigned char *a);
/*
 * RGBA-reader state.
 */
/**
 * @brief Hold the state and selected callbacks for an RGBA image reader.
 * @ingroup libtiff_public_data
 *
 * For state successfully initialised by TIFFRGBAImageBegin(), release owned
 * conversion tables with TIFFRGBAImageEnd(). It does not close the borrowed
 * TIFF handle, free the caller's state structure, or free the output raster.
 * Do not release the member pointers individually. They are invalid after End.
 * Callback calling conventions and supported conversions belong to the RGBA
 * module; this description records the data layout and ownership.
 */
struct _TIFFRGBAImage
{
    /** @brief Borrowed TIFF handle; must outlive this reader state. */
    TIFF *tif;
    /** @brief Nonzero to stop reading when an error is encountered. */
    int stoponerr;
    /**
     * @brief Nonzero for contiguous samples; zero for separate sample planes.
     */
    int isContig;
    /**
     * @brief ExtraSamples alpha interpretation, or zero when none is selected.
     */
    int alpha;
    /** @brief Source image width in pixels. */
    uint32_t width;
    /** @brief Source image height in pixels. */
    uint32_t height;
    /** @brief Source bits per sample. */
    uint16_t bitspersample;
    /** @brief Source samples per pixel. */
    uint16_t samplesperpixel;
    /** @brief Source Orientation tag value. */
    uint16_t orientation;
    /** @brief Requested output raster orientation. */
    uint16_t req_orientation;
    /** @brief Source PhotometricInterpretation tag value. */
    uint16_t photometric;
    /** @brief Reader-owned red palette copy, or NULL. */
    uint16_t *redcmap;
    /** @brief Reader-owned green palette copy, or NULL. */
    uint16_t *greencmap;
    /** @brief Reader-owned blue palette copy, or NULL. */
    uint16_t *bluecmap;
    /* get image data routine */
    /**
     * @brief Read and pack a raster through the selected RGBA state.
     *
     * Called synchronously by TIFFRGBAImageGet(). Custom replacements must
     * respect image offsets, caller capacity and their chosen output format.
     * Default getters use the format/orientation/error-policy contracts in
     * TIFFRGBAImageGet() and can allocate, perform I/O and invoke diagnostics.
     * State and callback code must remain live; do not retain raster, close the
     * handle or recursively decode it. Caller retains all argument ownership;
     * failure may leave partial output and changed reader/handle state.
     *
     * @param[in,out] img Borrowed successfully initialised RGBA reader state.
     * @param[out] raster Borrowed writable rwidth*rheight-pixel destination;
     * allocation product must be representable and disjoint from reader data.
     * @param[in] rwidth Positive destination row stride in pixels.
     * @param[in] rheight Positive destination height in rows.
     * @return One on completion, zero on failure; the default error policy can
     * tolerate selected corrupt input and still report completion.
     */
    int (*get)(TIFFRGBAImage *img, uint32_t *raster, uint32_t rwidth,
               uint32_t rheight);
    /* put decoded strip/tile */
    /** @brief Select the RGBA packer signature matching the image organisation.
     */
    union
    {
        /**
         * @brief Reserve a generic storage view of the selected packer.
         *
         * This signature is not callable: dispatch through contig or separate
         * according to isContig and keep the selected callback code live.
         * No invocation, error, side-effect or ownership contract exists for
         * this storage-only view.
         *
         * @param[in,out] img Nominal reader argument; no valid call is allowed.
         */
        void (*any)(TIFFRGBAImage *img);
        /** @brief Put callback for contiguous source samples. */
        tileContigRoutine contig;
        /** @brief Put callback for separate source planes. */
        tileSeparateRoutine separate;
    } put;
    /**< @brief Selected put callback; use the member matching isContig. */
    /** @brief Reader-owned sample mapping table, or NULL. */
    TIFFRGBValue *Map;
    /** @brief Reader-owned black/white expansion tables, or NULL. */
    uint32_t **BWmap;
    /** @brief Reader-owned palette expansion tables, or NULL. */
    uint32_t **PALmap;
    /** @brief Reader-owned YCbCr conversion block, or NULL. */
    TIFFYCbCrToRGB *ycbcr;
    /** @brief Reader-owned CIE L*a*b* conversion state, or NULL. */
    TIFFCIELabToRGB *cielab;

    /**
     * @brief Reader-owned unassociated-to-associated alpha lookup table, or
     * NULL.
     */
    uint8_t *UaToAa;
    /** @brief Reader-owned 16-bit-to-8-bit sample lookup table, or NULL. */
    uint8_t *Bitdepth16To8;

    /** @brief Nonnegative starting source row, measured in pixels. */
    int row_offset;
    /** @brief Nonnegative starting source column, measured in pixels. */
    int col_offset;
};

/*
 * Macros for extracting components from the
 * packed ABGR form returned by TIFFReadRGBAImage.
 */
/**
 * @brief Extract the red byte from a packed ABGR pixel value.
 * @ingroup libtiff_public_data
 *
 * Operates on the numeric pixel value, independently of its byte layout in
 * memory. The argument is evaluated once; the macro performs no allocation,
 * I/O, or library-state changes and has no failure result.
 *
 * @param[in] abgr Unsigned 32-bit packed pixel, as returned by the RGBA reader.
 * @return The red channel in the range 0 through 255.
 */
#define TIFFGetR(abgr) ((abgr) & 0xff)
/**
 * @brief Extract the green byte from a packed ABGR pixel value.
 * @ingroup libtiff_public_data
 *
 * Operates on the numeric pixel value, independently of its byte layout in
 * memory. The argument is evaluated once; the macro performs no allocation,
 * I/O, or library-state changes and has no failure result.
 *
 * @param[in] abgr Unsigned 32-bit packed pixel, as returned by the RGBA reader.
 * @return The green channel in the range 0 through 255.
 */
#define TIFFGetG(abgr) (((abgr) >> 8) & 0xff)
/**
 * @brief Extract the blue byte from a packed ABGR pixel value.
 * @ingroup libtiff_public_data
 *
 * Operates on the numeric pixel value, independently of its byte layout in
 * memory. The argument is evaluated once; the macro performs no allocation,
 * I/O, or library-state changes and has no failure result.
 *
 * @param[in] abgr Unsigned 32-bit packed pixel, as returned by the RGBA reader.
 * @return The blue channel in the range 0 through 255.
 */
#define TIFFGetB(abgr) (((abgr) >> 16) & 0xff)
/**
 * @brief Extract the alpha byte from a packed ABGR pixel value.
 * @ingroup libtiff_public_data
 *
 * Operates on the numeric pixel value, independently of its byte layout in
 * memory. The argument is evaluated once; the macro performs no allocation,
 * I/O, or library-state changes and has no failure result.
 *
 * @param[in] abgr Unsigned 32-bit packed pixel, as returned by the RGBA reader.
 * @return The alpha channel in the range 0 through 255.
 */
#define TIFFGetA(abgr) (((abgr) >> 24) & 0xff)

/*
 * A CODEC is a software package that implements decoding,
 * encoding, or decoding+encoding of a compression algorithm.
 * The library provides a collection of builtin codecs.
 * More codecs may be registered through calls to the library
 * and/or the builtin implementations may be overridden.
 */
/**
 * @brief Install compression methods and private state on a handle.
 * @ingroup libtiff_public_codecs
 *
 * Invoked synchronously after default methods have been installed. The
 * initialiser may allocate handle-owned state and chain tag methods; keep
 * code live for the lifetime of all installed callbacks. Cleanup must
 * release that state and restore method chains. Failure can leave partial
 * state for the handle lifecycle; no caller-buffer ownership transfers. Do
 * not close or recursively reconfigure the active handle.
 *
 * @param[in,out] tif Non-NULL live handle whose codec state can be changed;
 * caller serialises handle and registry access.
 * @param[in] scheme Requested compression identifier; related schemes can
 * share an initialiser.
 * @return Normally one for accepted installation and zero on failure; a
 * disabled/unknown scheme can be accepted with nonworking methods.
 */
typedef int (*TIFFInitMethod)(TIFF *tif, int scheme);
/**
 * @brief Describe a named compression scheme and its initialiser.
 * @ingroup libtiff_public_data
 *
 * Descriptor allocation and ownership depend on the codec registration/query
 * operation. Do not free the name or descriptor independently of that contract.
 */
typedef struct
{
    /**
     * @brief Codec name; storage lifetime follows the API supplying this
     * descriptor.
     */
    const char *name;
    /** @brief Compression identifier, such as COMPRESSION_LZW. */
    uint16_t scheme;
    /** @brief Callback that installs the codec on a TIFF handle. */
    TIFFInitMethod init;
} TIFFCodec;

/**
 * @brief Represent an unsigned TIFF rational as numerator and denominator.
 * @ingroup libtiff_public_data
 *
 * Contains inline integer values, not a floating-point approximation. The type
 * does not allocate storage or validate division by zero.
 */
typedef struct
{
    /** @brief Unsigned 32-bit numerator. */
    uint32_t uNum;
    /**
     * @brief Unsigned 32-bit denominator; the representation itself performs no
     * validation.
     */
    uint32_t uDenom;
} TIFFRational_t;

#include <stdarg.h>
#include <stdio.h>

/* share internal LogLuv conversion routines? */
#ifndef LOGLUV_PUBLIC
/**
 * @brief Control declaration of shared LogLuv conversion functions.
 * @ingroup libtiff_public_data
 *
 * Defaults to 1 unless overridden before inclusion. This controls header
 * visibility; it is not a runtime codec-capability query.
 */
#define LOGLUV_PUBLIC 1
#endif

/**
 * @brief Expand a compiler attribute in a public declaration when supported.
 * @ingroup libtiff_public_data
 *
 * This is declaration markup, not a runtime operation. It neither evaluates
 * expressions nor allocates or transfers storage.
 *
 * @param[in] x GNU attribute argument tokens, including their parentheses.
 * @return The compiler attribute syntax, or no tokens on other compilers.
 */
#if defined(__GNUC__) || defined(__clang__) || defined(__attribute__)
#define TIFF_ATTRIBUTE(x) __attribute__(x)
#else
#define TIFF_ATTRIBUTE(x) /*nothing*/
#endif

#if defined(c_plusplus) || defined(__cplusplus)
extern "C"
{
#endif

    /**
     * @brief Handle a library-wide error or warning diagnostic.
     * @ingroup libtiff_public_diagnostics
     *
     * Used for both error and warning callbacks. Invocation is synchronous
     * and returns no status or suppression signal. Its return does not decide
     * operation success. Do not retain borrowed
     * strings/va_list or destroy the invoking handle from a callback.
     * Callback code and any external context must remain valid until
     * replaced and no invocation is in flight. Global handler replacement
     * and concurrent use need external coordination; the library provides no
     * registration lock. Exceptions from callbacks are not caught.
     *
     * @param[in] module Borrowed module name, possibly NULL.
     * @param[in] fmt Borrowed printf-style format string.
     * @param[in,out] ap Borrowed arguments valid during invocation; use
     * va_copy() for a separate traversal.
     */
    typedef void (*TIFFErrorHandler)(const char *module, const char *fmt,
                                     va_list ap);

    /**
     * @brief Handle a library-wide error or warning diagnostic.
     * @ingroup libtiff_public_diagnostics
     *
     * Used for both error and warning callbacks. Invocation is synchronous
     * and returns no status or suppression signal. Its return does not decide
     * operation success. Do not retain borrowed
     * strings/va_list or destroy the invoking handle from a callback.
     * Callback code and any external context must remain valid until
     * replaced and no invocation is in flight. Global handler replacement
     * and concurrent use need external coordination; the library provides no
     * registration lock. Exceptions from callbacks are not caught.
     *
     * @param[in] fd Opaque client context, possibly zero; borrowed for this
     * call.
     * @param[in] module Borrowed module name, possibly NULL.
     * @param[in] fmt Borrowed printf-style format string.
     * @param[in,out] ap Borrowed arguments valid during invocation; use
     * va_copy() for a separate traversal.
     */
    typedef void (*TIFFErrorHandlerExt)(thandle_t fd, const char *module,
                                        const char *fmt, va_list ap);
    /**
     * @brief Handle a diagnostic using a per-handle borrowed context.
     * @ingroup libtiff_public_open_options
     *
     * The same signature is used for per-handle errors and warnings.
     * Invocation is synchronous; do not destroy the invoking handle or
     * recursively mutate its I/O state. The callback and context must remain
     * valid through opening, normal use, and close. The library does not own
     * the context or retain module/fmt/ap beyond the call. Early errors may
     * supply tif = NULL. Callback exceptions are not caught by the C
     * library.
     *
     * @param[in] tif Invoking handle, or NULL for an error before a handle
     * exists.
     * @param[in,out] user_data Borrowed context supplied through open
     * options; possibly NULL.
     * @param[in] module Borrowed diagnostic module name, possibly NULL.
     * @param[in] fmt Borrowed printf-style format string.
     * @param[in,out] ap Borrowed argument list valid during this invocation;
     * use va_copy() for an independent traversal.
     * @return Nonzero to suppress the legacy global handlers for this
     * message; zero to let them run. This does not abort, retry, or change
     * the operation's status.
     */
    typedef int (*TIFFErrorHandlerExtR)(TIFF *tif, void *user_data,
                                        const char *module, const char *fmt,
                                        va_list ap);
    /**
     * @brief Transfer bytes at the current position of a client I/O resource.
     * @ingroup libtiff_public_io_callbacks
     *
     * Used for both readproc and writeproc. Reads fill buf, writes consume
     * buf; either may advance the client position. Library callers normally
     * require an exact transfer and do not provide general retry/rollback. A
     * failure may follow partial I/O. Buffer storage remains caller-owned
     * and can be reused immediately after return; do not retain it. Zero-
     * length behaviour and errno/GetLastError are backend-specific.
     * Callbacks must remain valid for the handle lifetime and must not
     * destroy or recursively perform I/O on the invoking handle.
     *
     * @param[in,out] fd Opaque client context; the callback may update its
     * I/O state.
     * @param[in,out] buf For reads, writable destination; for writes,
     * readable source that must not be modified. Capacity is at least size
     * bytes.
     * @param[in] size Requested nonnegative byte count representable by
     * tmsize_t.
     * @return The transferred byte count. Short counts or a negative result
     * indicate an incomplete transfer; zero may denote end-of-input or a
     * backend error. The native Windows backend returns zero on API failure.
     */
    typedef tmsize_t (*TIFFReadWriteProc)(thandle_t fd, void *buf,
                                          tmsize_t size);
    /**
     * @brief Change the current position of a client I/O resource.
     * @ingroup libtiff_public_io_callbacks
     *
     * Offsets and returned positions use TIFF-file coordinates. A callback
     * must support the seeks needed by the chosen operations, including
     * absolute seeks from the beginning of the TIFF data. A failed seek need
     * not preserve position. No storage ownership transfers. Backend
     * diagnostics are not implied; the library may separately diagnose an
     * unexpected position. Do not destroy or recursively perform I/O on the
     * invoking handle.
     *
     * @param[in,out] fd Opaque client context whose position may change.
     * @param[in] off Byte offset interpreted according to whence; the
     * interface uses an unsigned type.
     * @param[in] whence SEEK_SET, SEEK_CUR, or SEEK_END.
     * @return The resulting absolute byte position. The conventional failure
     * sentinel is (toff_t)-1, but the current native Windows adapter returns
     * zero on failure; see @ref lifecycle_limits.
     */
    typedef toff_t (*TIFFSeekProc)(thandle_t fd, toff_t off, int whence);
    /**
     * @brief Release a client I/O resource or its adapter context.
     * @ingroup libtiff_public_io_callbacks
     *
     * TIFFClose() calls this once after TIFFCleanup() has invalidated the
     * handle. TIFFCleanup() and a NULL-returning client open do not call it.
     * The callback defines whether it closes a resource, frees a wrapper, or
     * both; it must not access the freed TIFF handle. A C++ stream callback
     * deletes the adapter but leaves the borrowed stream open. Failure does
     * not restore the TIFF handle, and no automatic library diagnostic for
     * close failure is guaranteed.
     *
     * @param[in,out] fd Client context passed to close after the TIFF handle
     * is freed.
     * @return Conventionally zero on success and -1 on failure; TIFFClose()
     * discards this result.
     */
    typedef int (*TIFFCloseProc)(thandle_t fd);
    /**
     * @brief Query the byte length of a client I/O resource.
     * @ingroup libtiff_public_io_callbacks
     *
     * If implemented by seeking, restore the prior position where possible.
     * The library performs no independent position rollback and does not
     * acquire storage ownership. Failure reporting and errno/GetLastError
     * follow the callback. Keep the callback and context valid for the
     * handle lifetime.
     *
     * @param[in,out] fd Opaque client context; a seek-based implementation
     * may temporarily change position.
     * @return The resource length in TIFF-file byte coordinates. The built-
     * in backends use zero for size-query failure, so there is no uniform
     * distinct error sentinel.
     */
    typedef toff_t (*TIFFSizeProc)(thandle_t fd);
    /**
     * @brief Provide a readable whole-file view of a client resource.
     * @ingroup libtiff_public_io_callbacks
     *
     * The view starts at TIFF-file offset zero and must remain readable and
     * valid until its matching TIFFUnmapFileProc call. Library mapping paths
     * are read-only. The library borrows the view and releases it through
     * the supplied unmapper, not _TIFFfree(). On failure the outputs are not
     * guaranteed valid and must not be used. A failed mapping normally
     * permits fallback to read/seek I/O without a library error. NULL
     * mapproc is replaced by an always-failing dummy.
     *
     * @param[in,out] fd Opaque client context used to obtain a mapping.
     * @param[out] base Non-NULL output pointer for the view base on success.
     * @param[out] size Non-NULL output pointer for the view length in bytes
     * on success.
     * @return Nonzero if a valid readable view is supplied; zero if mapping
     * is unavailable or fails. Do not use the mmap() pointer/error
     * convention as this status.
     */
    typedef int (*TIFFMapFileProc)(thandle_t fd, void **base, toff_t *size);
    /**
     * @brief Release a whole-file view supplied by a mapping callback.
     * @ingroup libtiff_public_io_callbacks
     *
     * Invalidates the mapped view according to the client mapping contract.
     * Returns no status; the library cannot detect or report an unmap
     * failure through this interface. It does not replace the client close
     * callback. A NULL unmapproc installs a no-op, so supply a real unmapper
     * whenever a successful mapping requires explicit release.
     * The current early read-open failure cleanup can also invoke this callback
     * with base = NULL and size = 0 before any mapping call. Tolerate that
     * no-view case without assuming that a mapping was acquired.
     *
     * @param[in,out] fd Matching client context, still valid for view
     * release.
     * @param[in] base Mapped-view base, or NULL in early no-view cleanup.
     * @param[in] size Mapped length in bytes, or zero in early no-view cleanup.
     */
    typedef void (*TIFFUnmapFileProc)(thandle_t fd, void *base, toff_t size);
    /**
     * @brief Extend the image-directory field table during default
     * initialisation.
     * @ingroup libtiff_public_fields
     *
     * Called synchronously by TIFFDefaultDirectory(), including repeated
     * directory setup on an existing handle. Register application fields and
     * optionally chain the previously installed extender. It has no status
     * channel; registration errors must be checked/reported within the
     * callback. Custom/EXIF/GPS setup subsequently replaces the field table, so
     * this hook does not extend their schemas. Keep callback code and borrowed
     * field names live for their use, and synchronise global registration
     * externally.
     *
     * @param[in,out] tif Non-NULL live TIFF handle; accessed without internal
     * locking.
     */
    typedef void (*TIFFExtendProc)(TIFF *tif);

    /**
     * @brief Return the version and copyright banner of the loaded library.
     * @ingroup libtiff_public_version
     *
     * The banner identifies the runtime library, which can differ from the
     * headers used to compile an application. It contains embedded newlines.
     * No I/O, allocation, state changes, or diagnostic callbacks occur.
     *
     * @return A non-NULL pointer to immutable, null-terminated static storage.
     * The library retains ownership; do not modify or free the string. It
     * remains valid while the library is loaded. There is no failure result.
     * @see TIFFLIB_VERSION_STR
     */
    extern const char *TIFFGetVersion(void);

    /**
     * @brief Find the highest-priority descriptor for a compression scheme.
     * @ingroup libtiff_public_codecs
     *
     * Searches registered entries newest first, then built-ins; a descriptor
     * can represent a codec disabled at build time. No allocation, I/O or
     * diagnostics. Returned registered storage is invalidated by unregister;
     * built-in storage remains live while the library is loaded. Do not
     * modify/free it. The process-global registry is unsynchronised; serialise
     * queries and modifications externally. No registry operation manages
     * active codec state or unloads callback code.
     *
     * @param[in] scheme Compression identifier to look up.
     * @return Borrowed descriptor, or NULL if no matching scheme is known.
     */
    extern const TIFFCodec *TIFFFindCODEC(uint16_t scheme);
    /**
     * @brief Register a codec descriptor ahead of built-in and older entries.
     * @ingroup libtiff_public_codecs
     *
     * Copies name into a combined allocation and stores the init pointer
     * without invoking it. Registration can shadow existing schemes and need
     * not supply both encoding and decoding. Keep callback code/context live
     * while registered and while any installed handles still use it. Release
     * this registration only with TIFFUnRegisterCODEC(); do not free the
     * returned descriptor or its name separately. Allocation failure uses
     * global diagnostics. The process-global registry is unsynchronised;
     * serialise queries and modifications externally. No registry operation
     * manages active codec state or unloads callback code.
     *
     * @param[in] scheme Compression identifier to register.
     * @param[in] name Non-NULL borrowed terminated name, copied synchronously;
     * combined allocation size must be representable.
     * @param[in] init Initialiser callback; a usable registration requires a
     * callable non-NULL function.
     * @return Registration-owned descriptor token, or NULL on allocation
     * failure.
     */
    extern TIFFCodec *TIFFRegisterCODEC(uint16_t scheme, const char *name,
                                        TIFFInitMethod init);
    /**
     * @brief Remove and free an exact registered codec descriptor.
     * @ingroup libtiff_public_codecs
     *
     * Requires a live descriptor token from TIFFRegisterCODEC(). Removes only
     * that pointer, exposing any older match. Frees the combined allocation and
     * invalidates its descriptor/name plus borrowed copies of that name. Does
     * not clean up installed handles; callback code must still outlive them. An
     * unmatched live descriptor is diagnosed through global handlers;
     * dangling/NULL input is not safe. The process-global registry is
     * unsynchronised; serialise queries and modifications externally. No
     * registry operation manages active codec state or unloads callback code.
     *
     * @param[in] c Live registration token, consumed on successful removal; not
     * a configured-list copy or built-in descriptor.
     */
    extern void TIFFUnRegisterCODEC(TIFFCodec *c);
    /**
     * @brief Check whether the selected descriptor has a configured
     * initialiser.
     * @ingroup libtiff_public_codecs
     *
     * Tests lookup, non-NULL init and inequality with the build-disabled stub.
     * Does not call init, validate a custom callback, test format support,
     * allocate, do I/O, or diagnose. The process-global registry is
     * unsynchronised; serialise queries and modifications externally. No
     * registry operation manages active codec state or unloads callback code.
     *
     * @param[in] scheme Compression identifier.
     * @return One for a descriptor with a non-stub non-NULL initialiser, zero
     * otherwise; not a runtime success guarantee.
     */
    extern int TIFFIsCODECConfigured(uint16_t scheme);
    /**
     * @brief Allocate a terminated snapshot of registered and configured codec
     * descriptors.
     * @ingroup libtiff_public_codecs
     *
     * Lists registrations newest first followed by built-in records whose
     * scheme currently resolves as configured. Duplicates/shadowed entries can
     * remain, and registered entries are copied even if their callback is NULL.
     * The sentinel record has all fields zero. Caller frees the array with
     * _TIFFfree(); names and callback pointers are shallow borrowed copies.
     * Registration removal invalidates names in earlier snapshots. Reallocation
     * failure frees partial output and returns NULL; no explicit diagnostic is
     * emitted here. The process-global registry is unsynchronised; serialise
     * queries and modifications externally. No registry operation manages
     * active codec state or unloads callback code.
     *
     * @return Owned array terminated by a name-NULL record, or NULL on
     * allocation failure.
     */
    extern TIFFCodec *TIFFGetConfiguredCODECs(void);

    /*
     * Auxiliary functions.
     */
#ifndef TIFF_DO_NOT_USE_NON_EXT_ALLOC_FUNCTIONS

    /**
     * @brief Allocate an uninitialised block through the public allocator.
     * @ingroup libtiff_public_memory
     *
     * Calls the platform malloc() for nonzero requests after conversion to
     * size_t. Negative values are not validated and must not be supplied.
     * Does not apply TIFFOpenOptions limits or emit library diagnostics;
     * errno follows the allocator. Storage remains valid until freed or
     * successfully reallocated.
     *
     * @param[in] s Nonnegative requested byte count.
     * @return An owned block, or NULL for zero size or allocation failure;
     * release with _TIFFfree().
     */
    extern void *_TIFFmalloc(tmsize_t s);

    /**
     * @brief Allocate a zero-filled array through the public allocator.
     * @ingroup libtiff_public_memory
     *
     * Converts nonzero arguments to size_t and delegates allocation/product
     * handling to the platform calloc(). Negative inputs are not checked and
     * must not be supplied. Does not apply handle allocation limits, change
     * handle state, or emit library diagnostics. errno follows the
     * allocator.
     *
     * @param[in] nmemb Nonnegative element count.
     * @param[in] siz Nonnegative element size in bytes.
     * @return An owned zero-filled block, or NULL when either argument is
     * zero or allocation fails; release with _TIFFfree().
     */
    extern void *_TIFFcalloc(tmsize_t nmemb, tmsize_t siz);

    /**
     * @brief Resize a public-allocator block using platform realloc semantics.
     * @ingroup libtiff_public_memory
     *
     * For positive size, preserves the initial min(old size, s) bytes and
     * invalidates the old pointer on success, even if the address is
     * unchanged. New bytes are uninitialised. Pass the result through a
     * temporary pointer until success is known. Negative sizes are not
     * checked and must not be supplied. Does not apply handle limits or emit
     * library diagnostics. Never pass an accounting-prefixed
     * _TIFFmallocExt() block to this family.
     *
     * @param[in] p Public-allocator block to resize, or NULL.
     * @param[in] s Nonnegative new byte count.
     * @return The replacement owned block on success, or NULL. For positive
     * size, NULL leaves the old block owned and valid; zero-size behaviour
     * follows the platform realloc(), including possible release of p.
     */
    extern void *_TIFFrealloc(void *p, tmsize_t s);

    /**
     * @brief Release a block allocated by the public allocation family.
     * @ingroup libtiff_public_memory
     *
     * Calls platform free(); NULL is accepted. Invalidates the block and all
     * views into it. Does not release caller resources or emit diagnostics.
     * Blocks using handle-aware cumulative accounting must instead be
     * released with _TIFFfreeExt() and the matching handle.
     *
     * @param[in] p Owned public-allocator block, or NULL.
     */
    extern void _TIFFfree(void *p);
#endif

    /**
     * @brief Fill a byte range with one repeated byte.
     * @ingroup libtiff_public_memory
     *
     * Calls memset() after converting c to size_t; follows its pointer-
     * validity requirements even for a zero count. Does not allocate, change
     * ownership, validate bounds/counts, or emit diagnostics.
     *
     * @param[out] p Destination valid for c bytes.
     * @param[in] v Byte value after conversion to unsigned char.
     * @param[in] c Nonnegative byte count representable by size_t.
     */
    extern void _TIFFmemset(void *p, int v, tmsize_t c);

    /**
     * @brief Copy a non-overlapping byte range.
     * @ingroup libtiff_public_memory
     *
     * Calls memcpy() after converting c to size_t; source and destination
     * must meet its validity/non-overlap requirements. Does not allocate,
     * retain pointers, validate bounds, or report errors. Use an appropriate
     * memmove() operation if overlap is possible.
     *
     * @param[out] d Destination valid for c writable bytes.
     * @param[in] s Source valid for c readable bytes, not overlapping d.
     * @param[in] c Nonnegative byte count representable by size_t.
     */
    extern void _TIFFmemcpy(void *d, const void *s, tmsize_t c);

    /**
     * @brief Compare two byte ranges lexicographically.
     * @ingroup libtiff_public_memory
     *
     * Compares bytes as unsigned char values. Returns only an ordering sign,
     * not a promised magnitude. Does not allocate, mutate the ranges,
     * validate bounds, transfer ownership, or emit diagnostics.
     *
     * @param[in] p1 First range valid for c readable bytes.
     * @param[in] p2 Second range valid for c readable bytes.
     * @param[in] c Nonnegative byte count representable by size_t.
     * @return A negative, zero, or positive value according to memcmp(); no
     * distinct failure result.
     */
    extern int _TIFFmemcmp(const void *p1, const void *p2, tmsize_t c);

    /*
    ** Stuff, related to tag handling and creating custom tags.
    */
    /**
     * @brief Count values stored in the current custom-tag list.
     * @ingroup libtiff_public_fields
     *
     * Counts FIELD_CUSTOM values, not all present baseline or codec tags. No
     * allocation, diagnostic, or ownership change occurs.
     *
     * @param[in] tif Non-NULL live handle.
     * @return Number of entries, including any partially installed custom
     * value.
     */
    extern int TIFFGetTagListCount(TIFF *tif);
    /**
     * @brief Look up a tag number by its custom-value list index.
     * @ingroup libtiff_public_fields
     *
     * The unsorted list changes after custom setters, unsetting, and directory
     * replacement. No diagnostic is emitted for an invalid index.
     *
     * @param[in] tif Non-NULL live handle.
     * @param[in] tag_index Zero-based index below TIFFGetTagListCount().
     * @return Tag number, or UINT32_MAX for an invalid index.
     */
    extern uint32_t TIFFGetTagListEntry(TIFF *tif, int tag_index);

/**
 * @brief Match any data type when searching for a field descriptor.
 * @ingroup libtiff_public_data
 */
#define TIFF_ANY TIFF_NOTYPE /* for field descriptor searching */
/**
 * @brief Mark a variable-length field with a 16-bit count convention.
 * @ingroup libtiff_public_data
 *
 * For pass-count fields, the variadic API uses a 16-bit count. The field
 * access contract determines whether a count is passed at all.
 */
#define TIFF_VARIABLE -1 /* marker for variable length tags */
/**
 * @brief Mark a field whose count follows SamplesPerPixel.
 * @ingroup libtiff_public_data
 */
#define TIFF_SPP -2 /* marker for SamplesPerPixel tags */
/**
 * @brief Mark a variable-length field with a 32-bit count convention.
 * @ingroup libtiff_public_data
 *
 * For pass-count fields, the variadic API uses a 32-bit count. The field
 * access contract determines whether a count is passed at all.
 */
#define TIFF_VARIABLE2 -3 /* marker for uint32_t var-length tags */

/**
 * @brief Identify a field stored through the custom-metadata mechanism.
 * @ingroup libtiff_public_data
 */
#define FIELD_CUSTOM 65

    /**
     * @brief Refer to an opaque registered field descriptor.
     * @ingroup libtiff_public_data
     *
     * Descriptor ownership and validity follow the field lookup/registration
     * API.
     */
    typedef struct _TIFFField TIFFField;
    /**
     * @brief Refer to an opaque collection of field descriptors.
     * @ingroup libtiff_public_data
     *
     * The directory module owns the definition and specifies registration
     * lifetime.
     */
    typedef struct _TIFFFieldArray TIFFFieldArray;

    /**
     * @brief Find a registered field by tag number and file data type.
     * @ingroup libtiff_public_fields
     *
     * The result is borrowed; do not free or modify it. Directory field-table
     * replacement or handle cleanup can invalidate it, particularly for
     * anonymous and legacy descriptors. Lookup updates the handle's field cache
     * but does not register a missing tag. No diagnostic is emitted on absence.
     *
     * @param[in,out] tif Non-NULL live TIFF handle; accessed without internal
     * locking.
     * @param[in] tag Tag number in the current directory namespace.
     * @param[in] dt Required file type, or TIFF_ANY to accept any registered
     * type.
     * @return Borrowed matching descriptor, or NULL if none is registered.
     */
    extern const TIFFField *TIFFFindField(TIFF *tif, uint32_t tag,
                                          TIFFDataType dt);
    /**
     * @brief Find a registered field by tag number.
     * @ingroup libtiff_public_fields
     *
     * The result is borrowed; do not free or modify it. Directory field-table
     * replacement or handle cleanup can invalidate it, particularly for
     * anonymous and legacy descriptors. Lookup updates the handle's field cache
     * but does not register a missing tag. Absence dispatches a warning only
     * when unknown-tag warnings are enabled for this handle.
     *
     * @param[in,out] tif Non-NULL live TIFF handle; accessed without internal
     * locking.
     * @param[in] tag Tag number in the current namespace.
     * @return Borrowed descriptor, or NULL if absent.
     */
    extern const TIFFField *TIFFFieldWithTag(TIFF *tif, uint32_t tag);
    /**
     * @brief Find a registered field by name.
     * @ingroup libtiff_public_fields
     *
     * The result is borrowed; do not free or modify it. Directory field-table
     * replacement or handle cleanup can invalidate it, particularly for
     * anonymous and legacy descriptors. Lookup updates the handle's field cache
     * but does not register a missing tag. Absence dispatches a warning only
     * when unknown-tag warnings are enabled for this handle.
     *
     * @param[in,out] tif Non-NULL live TIFF handle; accessed without internal
     * locking.
     * @param[in] field_name Non-NULL null-terminated, case-sensitive field
     * name.
     * @return Borrowed descriptor, or NULL if absent.
     */
    extern const TIFFField *TIFFFieldWithName(TIFF *tif,
                                              const char *field_name);

    /**
     * @brief Read a field descriptor's tag number.
     * @ingroup libtiff_public_fields
     *
     * Reads descriptor metadata without changing it or transferring ownership.
     * No allocation or diagnostic occurs.
     *
     * @param[in] fip Non-NULL live borrowed field descriptor.
     * @return Tag identifier; no allocation or diagnostics.
     */
    extern uint32_t TIFFFieldTag(const TIFFField *fip);
    /**
     * @brief Read a field descriptor's name.
     * @ingroup libtiff_public_fields
     *
     * Reads descriptor metadata without changing it or transferring ownership.
     * No allocation or diagnostic occurs.
     *
     * @param[in] fip Non-NULL live borrowed field descriptor.
     * @return Borrowed name string; valid only while its descriptor/name
     * storage remains live. Do not free or modify it.
     */
    extern const char *TIFFFieldName(const TIFFField *fip);
    /**
     * @brief Read a field descriptor's on-disk data type.
     * @ingroup libtiff_public_fields
     *
     * Reads descriptor metadata without changing it or transferring ownership.
     * No allocation or diagnostic occurs.
     *
     * @param[in] fip Non-NULL live borrowed field descriptor.
     * @return TIFFDataType value; not the complete variadic argument
     * representation.
     */
    extern TIFFDataType TIFFFieldDataType(const TIFFField *fip);
    /**
     * @brief Test whether a field passes an explicit count argument.
     * @ingroup libtiff_public_fields
     *
     * Reads descriptor metadata without changing it or transferring ownership.
     * No allocation or diagnostic occurs.
     *
     * @param[in] fip Non-NULL live borrowed field descriptor.
     * @return Nonzero for an explicit count, zero otherwise; see @ref
     * tag_arguments for argument promotions and exceptions.
     */
    extern int TIFFFieldPassCount(const TIFFField *fip);
    /**
     * @brief Read a field descriptor's getter count rule.
     * @ingroup libtiff_public_fields
     *
     * Reads descriptor metadata without changing it or transferring ownership.
     * No allocation or diagnostic occurs.
     *
     * @param[in] fip Non-NULL live borrowed field descriptor.
     * @return Fixed element count, or TIFF_VARIABLE, TIFF_VARIABLE2, or
     * TIFF_SPP; see @ref tag_arguments. No diagnostic is emitted.
     */
    extern int TIFFFieldReadCount(const TIFFField *fip);
    /**
     * @brief Read a field descriptor's setter count rule.
     * @ingroup libtiff_public_fields
     *
     * Reads descriptor metadata without changing it or transferring ownership.
     * No allocation or diagnostic occurs.
     *
     * @param[in] fip Non-NULL live borrowed field descriptor.
     * @return Fixed element count, or TIFF_VARIABLE, TIFF_VARIABLE2, or
     * TIFF_SPP; see @ref tag_arguments. No diagnostic is emitted.
     */
    extern int TIFFFieldWriteCount(const TIFFField *fip);
    /**
     * @brief Report the internal element storage width of a field.
     * @ingroup libtiff_public_fields
     *
     * Returns the width selected by the set/get representation, including float
     * versus double rationals. UINT16_PAIR returns four. UNDEFINED, ASCII, and
     * OTHER representations return one. This is neither the file data width nor
     * a complete variadic signature: promotions, count arguments, and pointer
     * indirection still apply. No allocation or diagnostic occurs.
     *
     * @param[in] fip Borrowed descriptor, or NULL.
     * @return Element width in bytes (1, 2, 4, or 8), or zero for NULL or an
     * unrecognised representation.
     */
    extern int TIFFFieldSetGetSize(const TIFFField *fip);
    /**
     * @brief Report the count width encoded by a field's set/get
     * representation.
     * @ingroup libtiff_public_fields
     *
     * Inspects the C16/C32 representation; it does not validate special built-
     * in calling conventions or the consistency of legacy metadata. No
     * allocation or diagnostic occurs.
     *
     * @param[in] fip Borrowed descriptor, or NULL.
     * @return Two for C16, four for C32, or zero for all other representations
     * or NULL.
     */
    extern int TIFFFieldSetGetCountSize(const TIFFField *fip);
    /**
     * @brief Test whether a field was registered as an unknown tag.
     * @ingroup libtiff_public_fields
     *
     * Reads descriptor metadata without changing it or transferring ownership.
     * No allocation or diagnostic occurs.
     *
     * @param[in] fip Non-NULL live borrowed field descriptor.
     * @return Nonzero for an anonymous descriptor, zero for a named definition;
     * no diagnostic is emitted.
     */
    extern int TIFFFieldIsAnonymous(const TIFFField *fip);

    /**
     * @brief Implement a handle's variadic tag setter.
     * @ingroup libtiff_public_tag_access
     *
     * Invoked synchronously after TIFFVSetField() validates registration and
     * mutability. Apply the tag-specific @ref tag_arguments contract, copy
     * borrowed values when storing them, and manage dirty/field bits and codec
     * state. Save/delegate to the parent method for unhandled tags; calling
     * TIFFSetField() on the same tag would recurse. The callback owns no input
     * list or handle. Diagnostics and partial failure state follow the
     * implementation.
     *
     * @param[in,out] tif Non-NULL live TIFF handle; accessed without internal
     * locking.
     * @param[in] tag Registered tag number.
     * @param[in,out] ap Borrowed list of promoted values/array pointers; may be
     * traversed. Use va_copy() for an independent traversal.
     * @return One on success, zero on rejection/failure; no rollback is
     * implied.
     */
    typedef int (*TIFFVSetMethod)(TIFF *tif, uint32_t tag, va_list ap);
    /**
     * @brief Implement a handle's variadic tag getter.
     * @ingroup libtiff_public_tag_access
     *
     * Invoked synchronously for registered, set tags or pseudo-tags. Use @ref
     * tag_arguments to fill caller-owned outputs; returned arrays remain
     * borrowed. Delegate unhandled tags to the saved parent method without
     * recursing through TIFFGetField(). Queries may allocate/cache data or do
     * I/O. On failure outputs need not be unchanged; the caller must not use
     * them. No handle/list ownership transfers.
     *
     * @param[in,out] tif Non-NULL live TIFF handle; accessed without internal
     * locking.
     * @param[in] tag Tag number in the current namespace.
     * @param[in,out] ap Borrowed output-argument list; traversal and pointed-to
     * outputs may change. Use va_copy() for independent traversal.
     * @return One when values were supplied, zero if unavailable or failed.
     */
    typedef int (*TIFFVGetMethod)(TIFF *tif, uint32_t tag, va_list ap);
    /**
     * @brief Append extension-specific directory information to a stream.
     * @ingroup libtiff_public_tag_access
     *
     * Invoked synchronously by TIFFPrintDirectory() after core and custom
     * values, and before optional strip/tile rows.
     * Borrowed arguments are valid only for this call; do not close/free them.
     * Follow TIFFPRINT_* controls, delegate saved parent printers where needed,
     * and avoid recursive directory printing. The original flags are forwarded
     * unchanged, including bits unused by the core formatter. Callback code
     * must remain live while installed or in flight. The caller performs no
     * exception handling or aggregate output-status check. Stream errors cannot
     * be returned by this interface; report failures through diagnostics if
     * required.
     *
     * @param[in,out] tif Non-NULL live TIFF handle; accessed without internal
     * locking.
     * @param[in,out] fd Non-NULL writable caller-owned stdio stream.
     * @param[in] flags TIFFPRINT_* bit mask.
     */
    typedef void (*TIFFPrintMethod)(TIFF *tif, FILE *fd, long flags);

    /**
     * @brief Collect a handle's tag-access and directory-print callbacks.
     * @ingroup libtiff_public_data
     *
     * TIFFAccessTagMethods() exposes the table embedded in a TIFF handle. Its
     * lifetime ends with that handle; the table is not a separately owned
     * allocation. Extension callbacks must obey the directory module's calling
     * conventions.
     */
    typedef struct
    {
        /** @brief Variadic tag-setting implementation for the handle. */
        TIFFVSetMethod vsetfield;
        /** @brief Variadic tag-query implementation for the handle. */
        TIFFVGetMethod vgetfield;
        /** @brief Optional codec/extension directory-print callback. */
        TIFFPrintMethod printdir;
    } TIFFTagMethods;

    /**
     * @brief Expose a handle's mutable tag-method table.
     * @ingroup libtiff_public_fields
     *
     * Returns inline borrowed storage valid until handle cleanup. Changes take
     * effect immediately and may be overwritten by codec/directory setup. Save
     * and delegate to the previous methods for unhandled tags. Keep installed
     * callback code live until removed and all calls finish. No allocation,
     * validation, or locking occurs; do not free the table.
     *
     * @param[in] tif Non-NULL live TIFF handle; not modified here, but the
     * returned pointer gives write access to its method table. Accessed
     * without internal locking.
     * @return Non-NULL pointer to the embedded method table.
     */
    extern TIFFTagMethods *TIFFAccessTagMethods(TIFF *tif);

    /**
     * @brief Look up a borrowed payload by its handle-local name.
     * @ingroup libtiff_public_client_info
     *
     * Uses case-sensitive strcmp() matching without allocation, I/O,
     * mutation, or diagnostics. Does not transfer ownership or extend
     * payload lifetime. A later replacement changes the association, and
     * handle cleanup releases only names/nodes, not payloads. This list is
     * independent of TIFFClientdata().
     *
     * @param[in] tif Non-NULL live TIFF handle.
     * @param[in] name Non-NULL null-terminated lookup name.
     * @return The stored pointer, or NULL if absent or if the stored payload
     * is NULL; these cases are not distinguished.
     */
    extern void *TIFFGetClientInfo(TIFF *tif, const char *name);

    /**
     * @brief Add or replace a handle-local named borrowed payload.
     * @ingroup libtiff_public_client_info
     *
     * For an existing case-sensitive name, stores data without allocation or
     * freeing the old payload. A new entry allocates a node and copied name
     * through checked handle allocators. If either allocation fails, emits
     * allocation diagnostics, frees any partially allocated node, and leaves
     * the list unchanged; the void API supplies no status.
     *
     * The caller retains ownership of both old and new payloads and must
     * keep the active one alive while it can be used. Passing NULL stores a
     * NULL association rather than removing it. TIFFCleanup()/TIFFClose()
     * release the copied name and node but not the payload. No file I/O or
     * retention of the input name pointer occurs.
     *
     * @param[in,out] tif Non-NULL live TIFF handle.
     * @param[in] data Borrowed payload pointer, possibly NULL.
     * @param[in] name Non-NULL null-terminated association name; copied for
     * a new entry.
     */
    extern void TIFFSetClientInfo(TIFF *tif, void *data, const char *name);

    /**
     * @brief Release library state without invoking the client close callback.
     * @ingroup libtiff_public_lifecycle
     *
     * For a writable handle, attempts TIFFFlush() and discards its result.
     * Releases codec/directory state, offset maps, client-info names/nodes,
     * owned buffers and field tables; releases mapped views through the
     * unmapping callback, then frees the handle. Borrowed client-info payloads
     * and diagnostic contexts are not freed.
     *
     * The client I/O resource and any adapter context normally freed by
     * closeproc remain the caller's responsibility. Save TIFFGetCloseProc() and
     * TIFFClientdata() before cleanup if they are needed afterward. This is
     * especially important for the C++ stream adapter. The handle and all
     * borrowed views into its storage become invalid, even when flushing fails.
     *
     * There is no status result. Error/warning callbacks may run, including
     * during flushing and cleanup. Call TIFFFlush() first when its status is
     * needed; cleanup is not an all-or-nothing write transaction.
     *
     * @param[in,out] tif Non-NULL live TIFF handle; invalid after the call.
     */
    extern void TIFFCleanup(TIFF *tif);

    /**
     * @brief Release a TIFF handle and invoke its client close callback.
     * @ingroup libtiff_public_lifecycle
     *
     * Saves the close callback and client context, calls TIFFCleanup(), then
     * invokes the saved close callback. The handle is already invalid when
     * closeproc runs. Borrowed diagnostic and client-info contexts are not
     * independently freed by the library. A stream adapter releases its own
     * wrapper but leaves the caller's stream open.
     *
     * There is no status result: flush failures and the close callback's return
     * value are discarded. Error/warning callbacks may run during cleanup, but
     * a close failure need not produce a library diagnostic. Call TIFFFlush()
     * before closing to inspect buffered-write status; that does not certify OS
     * durability or successful resource close. NULL is accepted.
     *
     * @param[in,out] tif Live TIFF handle to release, or NULL for a no-op.
     */
    extern void TIFFClose(TIFF *tif);

    /**
     * @brief Flush pending image data and dirty directory information.
     * @ingroup libtiff_public_lifecycle
     *
     * Runs codec postencoding and buffered writes as needed, then updates dirty
     * directory or strip/tile-array information. May change file position,
     * contents, and directory/buffer state and invoke I/O or diagnostic
     * callbacks. A failure can leave partial file writes and changed state; no
     * rollback is provided. Does not close the handle, call fsync(), or flush a
     * caller-owned C++ stream to its final device. Ownership and handle
     * lifetime are unchanged.
     *
     * @param[in,out] tif Non-NULL live TIFF handle.
     * @return 1 on success, including a read-only/no-work case; 0 if flushing
     * image data or rewriting directory information fails. In update mode, a
     * failed strile-array-only rewrite falls back to a full directory rewrite;
     * its diagnostics can occur even when 1 is returned.
     */
    extern int TIFFFlush(TIFF *tif);

    /**
     * @brief Flush pending encoded image data without committing directory
     * changes.
     * @ingroup libtiff_public_lifecycle
     *
     * Finalises pending codec output and writes buffered image data. It does
     * not by itself commit dirty directory metadata; use TIFFFlush() for that.
     * May invoke codec, I/O, and diagnostic callbacks and alter file position,
     * contents, and buffer flags. In particular the pending-postencode flag is
     * cleared before the postencode callback, even if that callback fails, so
     * failure is not a rollback/retry guarantee. Does not close or release the
     * handle and does not ensure device durability.
     *
     * @param[in,out] tif Non-NULL live TIFF handle.
     * @return 1 when no writing is pending or the data flush succeeds; 0 on
     * postencode or write failure.
     */
    extern int TIFFFlushData(TIFF *tif);
    /**
     * @brief Query a present tag in the current directory.
     * @ingroup libtiff_public_tag_access
     *
     * See @ref tag_arguments for exact pointer types, counts, exceptions, and
     * lifetimes. Values are read from the current in-memory directory, not
     * reread wholesale from disk. Registered pseudo-tags bypass the presence-
     * bit test and may have codec effects. Array/string results are borrowed
     * and must not be freed or modified; replacement, unsetting, directory
     * change, codec cleanup, or handle cleanup can invalidate them. Deferred
     * strip/tile arrays may allocate and perform I/O. Missing fields normally
     * return zero silently; codec, I/O, and allocation failures can dispatch
     * diagnostics. Outputs are usable only on success, and failure need not
     * preserve them. No errno contract is provided.
     *
     * @param[in,out] tif Non-NULL live TIFF handle; accessed without internal
     * locking.
     * @param[in] tag Tag number in the current directory namespace.
     * @param[out] ... Tag-specific non-NULL output pointers; see @ref
     * tag_arguments. Caller owns the output slots.
     * @return One if supplied, zero if absent, unsupported, or failed; outputs
     * are valid only on success.
     */
    extern int TIFFGetField(TIFF *tif, uint32_t tag, ...);
    /**
     * @brief Query a present tag in the current directory.
     * @ingroup libtiff_public_tag_access
     *
     * See @ref tag_arguments for exact pointer types, counts, exceptions, and
     * lifetimes. Values are read from the current in-memory directory, not
     * reread wholesale from disk. Registered pseudo-tags bypass the presence-
     * bit test and may have codec effects. Array/string results are borrowed
     * and must not be freed or modified; replacement, unsetting, directory
     * change, codec cleanup, or handle cleanup can invalidate them. Deferred
     * strip/tile arrays may allocate and perform I/O. Missing fields normally
     * return zero silently; codec, I/O, and allocation failures can dispatch
     * diagnostics. Outputs are usable only on success, and failure need not
     * preserve them. No errno contract is provided.
     *
     * @param[in,out] tif Non-NULL live TIFF handle; accessed without internal
     * locking.
     * @param[in] tag Tag number in the current directory namespace.
     * @param[in,out] ap Borrowed tag-specific output list; pointed-to outputs
     * and traversal can change. Do not reuse without va_copy().
     * @return One if supplied, zero if absent, unsupported, or failed; outputs
     * are valid only on success.
     */
    extern int TIFFVGetField(TIFF *tif, uint32_t tag, va_list ap);
    /**
     * @brief Query a present tag or a supported default.
     * @ingroup libtiff_public_tag_access
     *
     * See @ref tag_arguments for exact pointer types, counts, exceptions, and
     * lifetimes. Values are read from the current in-memory directory, not
     * reread wholesale from disk. Registered pseudo-tags bypass the presence-
     * bit test and may have codec effects. Array/string results are borrowed
     * and must not be freed or modified; replacement, unsetting, directory
     * change, codec cleanup, or handle cleanup can invalidate them. Deferred
     * strip/tile arrays may allocate and perform I/O. Missing fields normally
     * return zero silently; codec, I/O, and allocation failures can dispatch
     * diagnostics. Outputs are usable only on success, and failure need not
     * preserve them. No errno contract is provided. If ordinary lookup fails,
     * selected core defaults are supplied. TransferFunction and
     * ReferenceBlackWhite may be allocated/cached without setting tag presence;
     * unsupported defaults still return zero. Static default arrays also remain
     * borrowed. Failed custom getters must not consume ap before returning zero
     * if this fallback is to reuse it safely.
     *
     * @param[in,out] tif Non-NULL live TIFF handle; accessed without internal
     * locking.
     * @param[in] tag Tag number in the current directory namespace.
     * @param[out] ... Tag-specific non-NULL output pointers; see @ref
     * tag_arguments. Caller owns the output slots.
     * @return One if supplied, zero if absent, unsupported, or failed; outputs
     * are valid only on success.
     */
    extern int TIFFGetFieldDefaulted(TIFF *tif, uint32_t tag, ...);
    /**
     * @brief Query a present tag or a supported default.
     * @ingroup libtiff_public_tag_access
     *
     * See @ref tag_arguments for exact pointer types, counts, exceptions, and
     * lifetimes. Values are read from the current in-memory directory, not
     * reread wholesale from disk. Registered pseudo-tags bypass the presence-
     * bit test and may have codec effects. Array/string results are borrowed
     * and must not be freed or modified; replacement, unsetting, directory
     * change, codec cleanup, or handle cleanup can invalidate them. Deferred
     * strip/tile arrays may allocate and perform I/O. Missing fields normally
     * return zero silently; codec, I/O, and allocation failures can dispatch
     * diagnostics. Outputs are usable only on success, and failure need not
     * preserve them. No errno contract is provided. If ordinary lookup fails,
     * selected core defaults are supplied. TransferFunction and
     * ReferenceBlackWhite may be allocated/cached without setting tag presence;
     * unsupported defaults still return zero. Static default arrays also remain
     * borrowed. Failed custom getters must not consume ap before returning zero
     * if this fallback is to reuse it safely.
     *
     * @param[in,out] tif Non-NULL live TIFF handle; accessed without internal
     * locking.
     * @param[in] tag Tag number in the current directory namespace.
     * @param[in,out] ap Borrowed tag-specific output list; pointed-to outputs
     * and traversal can change. Do not reuse without va_copy().
     * @return One if supplied, zero if absent, unsupported, or failed; outputs
     * are valid only on success.
     */
    extern int TIFFVGetFieldDefaulted(TIFF *tif, uint32_t tag, va_list ap);
    /**
     * @brief Read and install the next image directory.
     * @ingroup libtiff_public_ifd_reading
     *
     * Reads entries, validates/normalises tags, configures codecs, and computes
     * image geometry. May register unknown fields, estimate missing byte
     * counts, and chop eligible uncompressed strips into logical smaller
     * strips. Does not decode image pixels. Allocates directory-owned values
     * and bookkeeping; seeks/reads or uses mapped bytes and invokes
     * tag/codec/extender/diagnostic callbacks. Replaces old values and
     * codec/schema state without saving dirty contents. Borrowed values and
     * replaceable descriptors can become invalid. Failure has no rollback:
     * offsets/index/flags/schema and some values can already have changed, with
     * the index advanced before later semantic checks. Some optional tag errors
     * are recoverable; success is not proof that every stored tag was retained.
     * There is no general errno contract. With next offset zero, sets the
     * current offset to zero and returns zero without resetting old values or
     * emitting an end-of-chain diagnostic. See @ref ifd_io for recovery and
     * ownership limits.
     *
     * @param[in,out] tif Non-NULL live handle with matching schema/state;
     * serialise access externally.
     * @return One for an installed image directory, zero at chain end or on
     * detected loop, count, validation, allocation, or I/O failure; zero does
     * not preserve old state.
     */
    extern int TIFFReadDirectory(TIFF *tif);
    /**
     * @brief Read metadata using an explicit custom field schema.
     * @ingroup libtiff_public_ifd_reading
     *
     * Fetches an IFD at the supplied byte offset, then frees old values and
     * installs defaults followed by the supplied schema. The schema record is
     * only read during setup; descriptors/names remain borrowed while
     * registered. Allocates/copies owned values, can register anonymous fields,
     * and may seek/read or access mapped data. Duplicate, unknown-type, and
     * wrong-count handling can skip/trim tags with diagnostics. Individual
     * tag-fetch/set failures are ignored by the top-level loop, so success can
     * coexist with errors and omitted values. The reader does not traverse a
     * custom NextIFD chain or link this IFD into a parent. It does not save
     * dirty old values. Even an early fetch failure can change the current
     * offset; later failure can leave a partly replaced schema/values. No
     * rollback or general errno guarantee. Borrowed old values/descriptors can
     * be invalidated. Successful completion forces subsequent indexed selection
     * to restart from the main header; index/next-link caches are not
     * comprehensively reset. See @ref ifd_io.
     *
     * @param[in,out] tif Non-NULL live handle with matching schema/state;
     * serialise access externally.
     * @param[in] diroff Nonzero custom-IFD offset in file bytes.
     * @param[in] infoarray Non-NULL valid private schema; field
     * descriptors/names must remain live while used.
     * @return One after the custom traversal, even if individual tags failed;
     * zero on structural fetch/bookkeeping failure, with possibly changed
     * state.
     */
    extern int TIFFReadCustomDirectory(TIFF *tif, toff_t diroff,
                                       const TIFFFieldArray *infoarray);
    /**
     * @brief Read a EXIF metadata directory.
     * @ingroup libtiff_public_ifd_reading
     *
     * Delegates to TIFFReadCustomDirectory() with the built-in EXIF schema,
     * including its allocations, callbacks, borrowed-result invalidation,
     * partial-failure rules, and ignored per-tag failures. Obtain the offset
     * from the matching parent pointer tag. Does not decode image data, follow
     * a metadata chain, or preserve the previous directory context.
     *
     * @param[in,out] tif Non-NULL live handle with matching schema/state;
     * serialise access externally.
     * @param[in] diroff Nonzero metadata-IFD offset in file bytes.
     * @return One on completed custom traversal (not proof that all tags
     * loaded), zero on detected structural failure; no rollback.
     */
    extern int TIFFReadEXIFDirectory(TIFF *tif, toff_t diroff);
    /**
     * @brief Read a EXIF GPS metadata directory.
     * @ingroup libtiff_public_ifd_reading
     *
     * Delegates to TIFFReadCustomDirectory() with the built-in EXIF GPS schema,
     * including its allocations, callbacks, borrowed-result invalidation,
     * partial-failure rules, and ignored per-tag failures. Obtain the offset
     * from the matching parent pointer tag. Does not decode image data, follow
     * a metadata chain, or preserve the previous directory context.
     *
     * @param[in,out] tif Non-NULL live handle with matching schema/state;
     * serialise access externally.
     * @param[in] diroff Nonzero metadata-IFD offset in file bytes.
     * @return One on completed custom traversal (not proof that all tags
     * loaded), zero on detected structural failure; no rollback.
     */
    extern int TIFFReadGPSDirectory(TIFF *tif, toff_t diroff);
    /**
     * @brief Compute the current scanline I/O byte estimate.
     * @ingroup libtiff_public_image_layout
     *
     * Contiguous data normally rounds width*SamplesPerPixel*BitsPerSample once
     * to bytes; separate planes use one sample plane. Packed contiguous
     * three-sample YCbCr without upsampling instead computes a padded
     * sampling-row byte count and integer-divides by vertical subsampling; this
     * can truncate and must not be used to reconstruct a complete sampling
     * group. Rejects zero computed size and invalid packed subsampling values
     * with diagnostics, but does not uniformly validate every malformed
     * photometric/sample combination. Current ordinary field widths bound the
     * row bit product below the generic byte-rounding addition limit. Returns a
     * scalar without acquiring input/output storage or allocating an output
     * buffer. Does not read/decode pixels, validate actual buffer capacity, or
     * persist tag values. Arithmetic/geometry failures may dispatch
     * diagnostics; zero can mean an empty result or rejection. Not every
     * rounding/count rejection is separately diagnosed; see @ref image_layout.
     * There is no general errno/status channel.
     *
     * @param[in,out] tif Non-NULL live handle with consistent current geometry;
     * queries may use getters/callbacks. Serialise access externally.
     * @return Byte count as uint64_t, or zero for empty/invalid/overflowed
     * calculation; no allocation/ownership is returned.
     */
    extern uint64_t TIFFScanlineSize64(TIFF *tif);
    /**
     * @brief Compute the current scanline I/O byte estimate.
     * @ingroup libtiff_public_image_layout
     *
     * Delegates to TIFFScanlineSize64() and rejects results above
     * TIFF_TMSIZE_T_MAX through _TIFFCastUInt64ToSSize(), returning zero with a
     * diagnostic. Contiguous data normally rounds
     * width*SamplesPerPixel*BitsPerSample once to bytes; separate planes use
     * one sample plane. Packed contiguous three-sample YCbCr without upsampling
     * instead computes a padded sampling-row byte count and integer-divides by
     * vertical subsampling; this can truncate and must not be used to
     * reconstruct a complete sampling group. Rejects zero computed size and
     * invalid packed subsampling values with diagnostics, but does not
     * uniformly validate every malformed photometric/sample combination.
     * Current ordinary field widths bound the row bit product below the generic
     * byte-rounding addition limit. Returns a scalar without acquiring
     * input/output storage or allocating an output buffer. Does not read/decode
     * pixels, validate actual buffer capacity, or persist tag values.
     * Arithmetic/geometry failures may dispatch diagnostics; zero can mean an
     * empty result or rejection. Not every rounding/count rejection is
     * separately diagnosed; see @ref image_layout. There is no general
     * errno/status channel.
     *
     * @param[in,out] tif Non-NULL live handle with consistent current geometry;
     * queries may use getters/callbacks. Serialise access externally.
     * @return Byte count as tmsize_t, or zero for empty/invalid/overflowed
     * calculation; no allocation/ownership is returned.
     */
    extern tmsize_t TIFFScanlineSize(TIFF *tif);
    /**
     * @brief Compute the packed full-raster row byte count.
     * @ingroup libtiff_public_image_layout
     *
     * Includes all SamplesPerPixel channels. Contiguous storage rounds the
     * complete row once; separate storage rounds each plane first, then
     * multiplies by the sample count. Does not model packed YCbCr sampling
     * groups, codec upsampling, or a tile row. Zero products can return zero
     * silently. Returns a scalar without acquiring input/output storage or
     * allocating an output buffer. Does not read/decode pixels, validate actual
     * buffer capacity, or persist tag values. Arithmetic/geometry failures may
     * dispatch diagnostics; zero can mean an empty result or rejection. Not
     * every rounding/count rejection is separately diagnosed; see @ref
     * image_layout. There is no general errno/status channel.
     *
     * @param[in] tif Non-NULL live handle with consistent current geometry; no
     * ownership transfer.
     * @return Byte count as uint64_t, or zero for empty/invalid/overflowed
     * calculation; no allocation/ownership is returned.
     */
    extern uint64_t TIFFRasterScanlineSize64(TIFF *tif);
    /**
     * @brief Compute the packed full-raster row byte count.
     * @ingroup libtiff_public_image_layout
     *
     * Delegates to TIFFRasterScanlineSize64() and rejects results above
     * TIFF_TMSIZE_T_MAX through _TIFFCastUInt64ToSSize(), returning zero with a
     * diagnostic. Includes all SamplesPerPixel channels. Contiguous storage
     * rounds the complete row once; separate storage rounds each plane first,
     * then multiplies by the sample count. Does not model packed YCbCr sampling
     * groups, codec upsampling, or a tile row. Zero products can return zero
     * silently. Returns a scalar without acquiring input/output storage or
     * allocating an output buffer. Does not read/decode pixels, validate actual
     * buffer capacity, or persist tag values. Arithmetic/geometry failures may
     * dispatch diagnostics; zero can mean an empty result or rejection. Not
     * every rounding/count rejection is separately diagnosed; see @ref
     * image_layout. There is no general errno/status channel.
     *
     * @param[in] tif Non-NULL live handle with consistent current geometry; no
     * ownership transfer.
     * @return Byte count as tmsize_t, or zero for empty/invalid/overflowed
     * calculation; no allocation/ownership is returned.
     */
    extern tmsize_t TIFFRasterScanlineSize(TIFF *tif);
    /**
     * @brief Compute the decoded-size estimate for a nominal strip.
     * @ingroup libtiff_public_image_layout
     *
     * Uses min(RowsPerStrip, ImageLength), then TIFFVStripSize64(). Last
     * individual strips may be shorter; this is not an encoded byte-count
     * query. Packed YCbCr rowblock and diagnostic rules follow
     * _TIFFStrileSize64(). Returns a scalar without acquiring input/output
     * storage or allocating an output buffer. Does not read/decode pixels,
     * validate actual buffer capacity, or persist tag values.
     * Arithmetic/geometry failures may dispatch diagnostics; zero can mean an
     * empty result or rejection. Not every rounding/count rejection is
     * separately diagnosed; see @ref image_layout. There is no general
     * errno/status channel.
     *
     * @param[in,out] tif Non-NULL live handle with consistent current geometry;
     * queries may use getters/callbacks. Serialise access externally.
     * @return Byte count as uint64_t, or zero for empty/invalid/overflowed
     * calculation; no allocation/ownership is returned.
     */
    extern uint64_t TIFFStripSize64(TIFF *tif);
    /**
     * @brief Compute the decoded-size estimate for a nominal strip.
     * @ingroup libtiff_public_image_layout
     *
     * Delegates to TIFFStripSize64() and rejects results above
     * TIFF_TMSIZE_T_MAX through _TIFFCastUInt64ToSSize(), returning zero with a
     * diagnostic. Uses min(RowsPerStrip, ImageLength), then TIFFVStripSize64().
     * Last individual strips may be shorter; this is not an encoded byte-count
     * query. Packed YCbCr rowblock and diagnostic rules follow
     * _TIFFStrileSize64(). Returns a scalar without acquiring input/output
     * storage or allocating an output buffer. Does not read/decode pixels,
     * validate actual buffer capacity, or persist tag values.
     * Arithmetic/geometry failures may dispatch diagnostics; zero can mean an
     * empty result or rejection. Not every rounding/count rejection is
     * separately diagnosed; see @ref image_layout. There is no general
     * errno/status channel.
     *
     * @param[in,out] tif Non-NULL live handle with consistent current geometry;
     * queries may use getters/callbacks. Serialise access externally.
     * @return Byte count as tmsize_t, or zero for empty/invalid/overflowed
     * calculation; no allocation/ownership is returned.
     */
    extern tmsize_t TIFFStripSize(TIFF *tif);
    /**
     * @brief Query the stored byte count for one raw strip.
     * @ingroup libtiff_public_image_layout
     *
     * Queries TIFFGetStrileByteCount() rather than computing decoded geometry.
     * May load/grow deferred metadata caches, allocate and change client I/O
     * position, invalidating old borrowed array views. Does not decode pixels
     * or validate that the range exists in the file. A zero metadata count,
     * including out-of-range/load failures returning zero, is diagnosed and
     * mapped to the raw-size sentinel. Other getter errors are not tracked with
     * a separate WithErr output. No ownership transfers.
     *
     * @param[in,out] tif Non-NULL live handle with consistent current geometry;
     * queries may use getters/callbacks. Serialise access externally.
     * @param[in] strip Zero-based strip/strile index in the current metadata
     * arrays; intended for strip-organised data.
     * @return Nonzero stored byte count, or UINT64_MAX after a zero
     * count/error; UINT64_MAX metadata is indistinguishable from that sentinel.
     */
    extern uint64_t TIFFRawStripSize64(TIFF *tif, uint32_t strip);
    /**
     * @brief Convert one raw-strip byte count to the legacy signed-size result.
     * @ingroup libtiff_public_image_layout
     *
     * Queries TIFFGetStrileByteCount() rather than computing decoded geometry.
     * May load/grow deferred metadata caches, allocate and change client I/O
     * position, invalidating old borrowed array views. Does not decode pixels
     * or validate that the range exists in the file. A zero metadata count,
     * including out-of-range/load failures returning zero, is diagnosed and
     * mapped to the raw-size sentinel. Other getter errors are not tracked with
     * a separate WithErr output. No ownership transfers. Maps the uint64_t
     * sentinel to minus one; otherwise uses a cast/unsigned round-trip
     * comparison, not the ordinary checked signed-size helper. A failed round
     * trip reports integer overflow and returns zero. Very large values that
     * round-trip as negative can escape this check, including on 64-bit
     * signed-size hosts; do not treat every nonzero result as a valid
     * allocation length. See @ref image_layout.
     *
     * @param[in,out] tif Non-NULL live handle with consistent current geometry;
     * queries may use getters/callbacks. Serialise access externally.
     * @param[in] strip Zero-based strip/strile metadata index.
     * @return Signed byte count, minus one for the raw-size sentinel, or zero
     * for detected cast overflow; other negative results are possible for huge
     * metadata values.
     */
    extern tmsize_t TIFFRawStripSize(TIFF *tif, uint32_t strip);
    /**
     * @brief Compute the decoded-size estimate for a requested strip height.
     * @ingroup libtiff_public_image_layout
     *
     * Uses the requested row count, with UINT32_MAX meaning current
     * ImageLength. Does not clamp other row counts to ImageLength. Contiguous
     * non-upsampled YCbCr counts complete sampling blocks and rounds the
     * complete horizontal sampling row once; with SamplesPerPixel other than
     * three it is rejected with a diagnostic and zero (unlike
     * TIFFScanlineSize64(), which treats it as ordinary data). Ordinary data
     * multiplies the scanline estimate by row count. Does not include an
     * ImageDepth multiplier. Returns a scalar without acquiring input/output
     * storage or allocating an output buffer. Does not read/decode pixels,
     * validate actual buffer capacity, or persist tag values.
     * Arithmetic/geometry failures may dispatch diagnostics; zero can mean an
     * empty result or rejection. Not every rounding/count overflow path is
     * separately diagnosed; see @ref image_layout. There is no general
     * errno/status channel.
     *
     * @param[in,out] tif Non-NULL live handle with consistent current geometry;
     * queries may use getters/callbacks. Serialise access externally.
     * @param[in] nrows Requested row count; UINT32_MAX selects ImageLength.
     * @return Byte count as uint64_t, or zero for empty/invalid/overflowed
     * calculation; no allocation/ownership is returned.
     */
    extern uint64_t TIFFVStripSize64(TIFF *tif, uint32_t nrows);
    /**
     * @brief Compute the decoded-size estimate for a requested strip height.
     * @ingroup libtiff_public_image_layout
     *
     * Delegates to TIFFVStripSize64() and rejects results above
     * TIFF_TMSIZE_T_MAX through _TIFFCastUInt64ToSSize(), returning zero with a
     * diagnostic. Uses the requested row count, with UINT32_MAX meaning current
     * ImageLength. Does not clamp other row counts to ImageLength. Contiguous
     * non-upsampled YCbCr counts complete sampling blocks and rounds the
     * complete horizontal sampling row once; with SamplesPerPixel other than
     * three it is rejected with a diagnostic and zero (unlike
     * TIFFScanlineSize64(), which treats it as ordinary data). Ordinary data
     * multiplies the scanline estimate by row count. Does not include an
     * ImageDepth multiplier. Returns a scalar without acquiring input/output
     * storage or allocating an output buffer. Does not read/decode pixels,
     * validate actual buffer capacity, or persist tag values.
     * Arithmetic/geometry failures may dispatch diagnostics; zero can mean an
     * empty result or rejection. Not every rounding/count overflow path is
     * separately diagnosed; see @ref image_layout. There is no general
     * errno/status channel.
     *
     * @param[in,out] tif Non-NULL live handle with consistent current geometry;
     * queries may use getters/callbacks. Serialise access externally.
     * @param[in] nrows Requested row count; UINT32_MAX selects ImageLength.
     * @return Byte count as tmsize_t, or zero for empty/invalid/overflowed
     * calculation; no allocation/ownership is returned.
     */
    extern tmsize_t TIFFVStripSize(TIFF *tif, uint32_t nrows);
    /**
     * @brief Compute the ordinary packed byte count of one tile row.
     * @ingroup libtiff_public_image_layout
     *
     * Uses TileWidth*BitsPerSample and, for contiguous storage,
     * SamplesPerPixel; rounds once to bytes. Requires nonzero
     * TileWidth/TileLength and nonzero contiguous sample count, diagnosing
     * invalid/zero results. Does not model packed YCbCr sampling groups or
     * multiply by TileDepth. Returns a scalar without acquiring input/output
     * storage or allocating an output buffer. Does not read/decode pixels,
     * validate actual buffer capacity, or persist tag values.
     * Arithmetic/geometry failures may dispatch diagnostics; zero can mean an
     * empty result or rejection. Not every rounding/count rejection is
     * separately diagnosed; see @ref image_layout. There is no general
     * errno/status channel.
     *
     * @param[in] tif Non-NULL live handle with consistent current geometry; no
     * ownership transfer.
     * @return Byte count as uint64_t, or zero for empty/invalid/overflowed
     * calculation; no allocation/ownership is returned.
     */
    extern uint64_t TIFFTileRowSize64(TIFF *tif);
    /**
     * @brief Compute the ordinary packed byte count of one tile row.
     * @ingroup libtiff_public_image_layout
     *
     * Delegates to TIFFTileRowSize64() and rejects results above
     * TIFF_TMSIZE_T_MAX through _TIFFCastUInt64ToSSize(), returning zero with a
     * diagnostic. Uses TileWidth*BitsPerSample and, for contiguous storage,
     * SamplesPerPixel; rounds once to bytes. Requires nonzero
     * TileWidth/TileLength and nonzero contiguous sample count, diagnosing
     * invalid/zero results. Does not model packed YCbCr sampling groups or
     * multiply by TileDepth. Returns a scalar without acquiring input/output
     * storage or allocating an output buffer. Does not read/decode pixels,
     * validate actual buffer capacity, or persist tag values.
     * Arithmetic/geometry failures may dispatch diagnostics; zero can mean an
     * empty result or rejection. Not every rounding/count rejection is
     * separately diagnosed; see @ref image_layout. There is no general
     * errno/status channel.
     *
     * @param[in] tif Non-NULL live handle with consistent current geometry; no
     * ownership transfer.
     * @return Byte count as tmsize_t, or zero for empty/invalid/overflowed
     * calculation; no allocation/ownership is returned.
     */
    extern tmsize_t TIFFTileRowSize(TIFF *tif);
    /**
     * @brief Compute the decoded-size estimate for the configured tile height.
     * @ingroup libtiff_public_image_layout
     *
     * Calls TIFFVTileSize64() with TileLength, so its YCbCr rules apply,
     * including rejection of contiguous non-upsampled YCbCr with
     * SamplesPerPixel other than three. Requires nonzero tile
     * width/length/depth. TileDepth is checked but not multiplied into the
     * result; this implementation computes row-count times row storage or YCbCr
     * sampling groups. Do not infer a multi-depth volume allocation from this
     * result alone. Returns a scalar without acquiring input/output storage or
     * allocating an output buffer. Does not read/decode pixels, validate actual
     * buffer capacity, or persist tag values. Arithmetic/geometry failures may
     * dispatch diagnostics; zero can mean an empty result or rejection. Not
     * every rounding/count rejection is separately diagnosed; see @ref
     * image_layout. There is no general errno/status channel.
     *
     * @param[in,out] tif Non-NULL live handle with consistent current geometry;
     * queries may use getters/callbacks. Serialise access externally.
     * @return Byte count as uint64_t, or zero for empty/invalid/overflowed
     * calculation; no allocation/ownership is returned.
     */
    extern uint64_t TIFFTileSize64(TIFF *tif);
    /**
     * @brief Compute the decoded-size estimate for the configured tile height.
     * @ingroup libtiff_public_image_layout
     *
     * Delegates to TIFFTileSize64() and rejects results above TIFF_TMSIZE_T_MAX
     * through _TIFFCastUInt64ToSSize(), returning zero with a diagnostic. Calls
     * TIFFVTileSize64() with TileLength, so its YCbCr rules apply, including
     * rejection of contiguous non-upsampled YCbCr with SamplesPerPixel other
     * than three. Requires nonzero tile width/length/depth. TileDepth is
     * checked but not multiplied into the result; this implementation computes
     * row-count times row storage or YCbCr sampling groups. Do not infer a
     * multi-depth volume allocation from this result alone. Returns a scalar
     * without acquiring input/output storage or allocating an output buffer.
     * Does not read/decode pixels, validate actual buffer capacity, or persist
     * tag values. Arithmetic/geometry failures may dispatch diagnostics; zero
     * can mean an empty result or rejection. Not every rounding/count rejection
     * is separately diagnosed; see @ref image_layout. There is no general
     * errno/status channel.
     *
     * @param[in,out] tif Non-NULL live handle with consistent current geometry;
     * queries may use getters/callbacks. Serialise access externally.
     * @return Byte count as tmsize_t, or zero for empty/invalid/overflowed
     * calculation; no allocation/ownership is returned.
     */
    extern tmsize_t TIFFTileSize(TIFF *tif);
    /**
     * @brief Compute the decoded-size estimate for a requested tile height.
     * @ingroup libtiff_public_image_layout
     *
     * Requires nonzero TileWidth/TileLength/TileDepth and uses the requested
     * nrows without clipping to TileLength. UINT32_MAX is an ordinary row count
     * here, not the strip all-image sentinel. TileDepth is checked but not
     * multiplied. Contiguous non-upsampled YCbCr uses complete sampling blocks
     * and, with SamplesPerPixel other than three, is rejected with a diagnostic
     * and zero; ordinary data uses TIFFTileRowSize64(). Returns a scalar
     * without acquiring input/output storage or allocating an output buffer.
     * Does not read/decode pixels, validate actual buffer capacity, or persist
     * tag values. Arithmetic/geometry failures may dispatch diagnostics; zero
     * can mean an empty result or rejection. Not every rounding/count rejection
     * is separately diagnosed; see @ref image_layout. There is no general
     * errno/status channel.
     *
     * @param[in,out] tif Non-NULL live handle with consistent current geometry;
     * queries may use getters/callbacks. Serialise access externally.
     * @param[in] nrows Requested row count, not clipped to TileLength;
     * UINT32_MAX is an ordinary count here.
     * @return Byte count as uint64_t, or zero for empty/invalid/overflowed
     * calculation; no allocation/ownership is returned.
     */
    extern uint64_t TIFFVTileSize64(TIFF *tif, uint32_t nrows);
    /**
     * @brief Compute the decoded-size estimate for a requested tile height.
     * @ingroup libtiff_public_image_layout
     *
     * Delegates to TIFFVTileSize64() and rejects results above
     * TIFF_TMSIZE_T_MAX through _TIFFCastUInt64ToSSize(), returning zero with a
     * diagnostic. Requires nonzero TileWidth/TileLength/TileDepth and uses the
     * requested nrows without clipping to TileLength. UINT32_MAX is an ordinary
     * row count here, not the strip all-image sentinel. TileDepth is checked
     * but not multiplied. Contiguous non-upsampled YCbCr uses complete sampling
     * blocks and, with SamplesPerPixel other than three, is rejected with a
     * diagnostic and zero; ordinary data uses TIFFTileRowSize64(). Returns a
     * scalar without acquiring input/output storage or allocating an output
     * buffer. Does not read/decode pixels, validate actual buffer capacity, or
     * persist tag values. Arithmetic/geometry failures may dispatch
     * diagnostics; zero can mean an empty result or rejection. Not every
     * rounding/count rejection is separately diagnosed; see @ref image_layout.
     * There is no general errno/status channel.
     *
     * @param[in,out] tif Non-NULL live handle with consistent current geometry;
     * queries may use getters/callbacks. Serialise access externally.
     * @param[in] nrows Requested row count, not clipped to TileLength;
     * UINT32_MAX is an ordinary count here.
     * @return Byte count as tmsize_t, or zero for empty/invalid/overflowed
     * calculation; no allocation/ownership is returned.
     */
    extern tmsize_t TIFFVTileSize(TIFF *tif, uint32_t nrows);
    /**
     * @brief Ask the installed sizing method for a RowsPerStrip recommendation.
     * @ingroup libtiff_public_image_layout
     *
     * Invokes the current tif_defstripsize callback synchronously; it may apply
     * codec-specific constraints or have additional allocation/state/diagnostic
     * effects. Does not itself set RowsPerStrip, allocate a result, validate
     * the callback recommendation, or report a separate status. Default
     * heuristic semantics, including signed interpretation of high-bit
     * requests, are documented in _TIFFDefaultStripSize(). Callback
     * code/context must remain live during the call.
     *
     * @param[in,out] tif Non-NULL live handle with consistent current geometry;
     * queries may use getters/callbacks. Serialise access externally.
     * @param[in] request Suggested rows per strip, zero for a heuristic
     * request; codec methods may adjust it.
     * @return Recommended row count from the installed callback; no universal
     * error sentinel or success guarantee is imposed by dispatch.
     */
    extern uint32_t TIFFDefaultStripSize(TIFF *tif, uint32_t request);
    /**
     * @brief Ask the installed sizing method to adjust tile width and height.
     * @ingroup libtiff_public_image_layout
     *
     * Invokes tif_deftilesize synchronously on caller-owned width/height slots.
     * Does not set TileWidth/TileLength tags, allocate returned storage, or
     * validate recommendations. The normal default supplies 256 for nonpositive
     * signed requests and rounds to multiples of 16; codec overrides can impose
     * additional rules and effects. Callback code/context must remain live
     * during the call. This void interface has no independent error status.
     *
     * @param[in] tif Non-NULL live handle with consistent current geometry;
     * the built-in methods only read it. Serialise access externally.
     * @param[in,out] tw Non-NULL writable width request/result in pixels.
     * @param[in,out] th Non-NULL writable height request/result in rows.
     */
    extern void TIFFDefaultTileSize(TIFF *tif, uint32_t *tw, uint32_t *th);

    /**
     * @brief Return the handle's file-descriptor metadata.
     * @ingroup libtiff_public_lifecycle
     *
     * Reads stored state without performing I/O, validating the underlying
     * resource, allocating, or emitting diagnostics. Requires a live handle and
     * has no distinct failure result. Client I/O uses TIFFClientdata(), not
     * this metadata. A generic TIFFClientOpen() handle starts with descriptor
     * metadata zero unless changed.
     *
     * @param[in] tif Non-NULL live TIFF handle.
     * @return The stored descriptor metadata, not a separately acquired
     * resource.
     */
    extern int TIFFFileno(TIFF *tif);

    /**
     * @brief Replace descriptor metadata without changing client I/O routing.
     * @ingroup libtiff_public_lifecycle
     *
     * Stores fd without opening, closing, duplicating, or validating any
     * resource. Does not change TIFFClientdata() or callbacks; I/O continues
     * through the existing client context. No diagnostics or distinct failure
     * result occur.
     *
     * @param[in,out] tif Non-NULL live TIFF handle.
     * @param[in] fd Replacement descriptor metadata.
     * @return The previous descriptor metadata.
     */
    extern int TIFFSetFileno(TIFF *tif, int fd);

    /**
     * @brief Return the context passed to client I/O callbacks.
     * @ingroup libtiff_public_lifecycle
     *
     * Reads stored state without performing I/O, validating the underlying
     * resource, allocating, or emitting diagnostics. Requires a live handle and
     * has no distinct failure result. The context lifetime follows the client-
     * open/close contract; querying it does not extend that lifetime.
     *
     * @param[in] tif Non-NULL live TIFF handle.
     * @return The stored opaque context, without ownership transfer.
     */
    extern thandle_t TIFFClientdata(TIFF *tif);

    /**
     * @brief Replace the context passed to all client I/O callbacks.
     * @ingroup libtiff_public_lifecycle
     *
     * Stores the new context without retaining, validating, or releasing either
     * value. Subsequent callbacks, including unmapping and closing, receive the
     * replacement. The caller must preserve compatibility with installed
     * callbacks and existing buffers/mappings; replacing the context does not
     * reset those. Descriptor metadata is unchanged. No diagnostic or distinct
     * failure result occurs.
     *
     * @param[in,out] tif Non-NULL live TIFF handle.
     * @param[in] newvalue Replacement context compatible with all installed
     * callbacks.
     * @return The previous context, without ownership transfer.
     */
    extern thandle_t TIFFSetClientdata(TIFF *tif, thandle_t newvalue);

    /**
     * @brief Return the stored library access mode.
     * @ingroup libtiff_public_lifecycle
     *
     * Reads stored state without performing I/O, validating the underlying
     * resource, allocating, or emitting diagnostics. Requires a live handle and
     * has no distinct failure result. Changing OS access permissions or stream
     * state outside the library is not reflected by this query.
     *
     * @param[in] tif Non-NULL live TIFF handle.
     * @return The stored O_RDONLY/O_RDWR-style mode value, not the original
     * mode string.
     */
    extern int TIFFGetMode(TIFF *tif);

    /**
     * @brief Replace the stored library access mode without reopening the
     * resource.
     * @ingroup libtiff_public_lifecycle
     *
     * Stores the integer without validation or changing OS permissions. Can
     * alter library read/write/flush decisions; the caller must ensure
     * compatibility with the actual resource and handle state. Does not close,
     * reopen, or otherwise take ownership of a resource. No diagnostic or
     * distinct failure result occurs.
     *
     * @param[in,out] tif Non-NULL live TIFF handle.
     * @param[in] mode Replacement O_RDONLY/O_RDWR-style mode value.
     * @return The previous stored access mode.
     */
    extern int TIFFSetMode(TIFF *tif, int mode);

    /**
     * @brief Test whether the current image uses tiles.
     * @ingroup libtiff_public_lifecycle
     *
     * Reads stored header/flag state without I/O, mutation, allocation, or
     * diagnostic callbacks. There is no distinct error result. Directory- and
     * codec-dependent flags describe the current handle state rather than every
     * image in a multipage file.
     *
     * @param[in] tif Non-NULL live TIFF handle.
     * @return Nonzero for tiled storage; zero for strip storage.
     */
    extern int TIFFIsTiled(TIFF *tif);

    /**
     * @brief Test whether file values require byte swapping for this host.
     * @ingroup libtiff_public_lifecycle
     *
     * Reads stored header/flag state without I/O, mutation, allocation, or
     * diagnostic callbacks. There is no distinct error result. Directory- and
     * codec-dependent flags describe the current handle state rather than every
     * image in a multipage file.
     *
     * @param[in] tif Non-NULL live TIFF handle.
     * @return Nonzero if the byte-swapping flag is set; zero otherwise.
     */
    extern int TIFFIsByteSwapped(TIFF *tif);

    /**
     * @brief Test whether the active codec supplies upsampled data.
     * @ingroup libtiff_public_lifecycle
     *
     * Reads stored header/flag state without I/O, mutation, allocation, or
     * diagnostic callbacks. There is no distinct error result. Directory- and
     * codec-dependent flags describe the current handle state rather than every
     * image in a multipage file.
     *
     * @param[in] tif Non-NULL live TIFF handle.
     * @return Nonzero if the upsampled-output flag is set; zero otherwise.
     */
    extern int TIFFIsUpSampled(TIFF *tif);

    /**
     * @brief Test the selected data bit-fill-order flag.
     * @ingroup libtiff_public_lifecycle
     *
     * Reads stored header/flag state without I/O, mutation, allocation, or
     * diagnostic callbacks. There is no distinct error result. Directory- and
     * codec-dependent flags describe the current handle state rather than every
     * image in a multipage file.
     *
     * @param[in] tif Non-NULL live TIFF handle.
     * @return Nonzero for MSB-to-LSB data order; zero otherwise. This is not a
     * CPU byte-order test.
     */
    extern int TIFFIsMSB2LSB(TIFF *tif);

    /**
     * @brief Test whether the stored file signature is TIFF_BIGENDIAN.
     * @ingroup libtiff_public_lifecycle
     *
     * Reads stored header/flag state without I/O, mutation, allocation, or
     * diagnostic callbacks. There is no distinct error result. Directory- and
     * codec-dependent flags describe the current handle state rather than every
     * image in a multipage file.
     *
     * @param[in] tif Non-NULL live TIFF handle.
     * @return Nonzero for TIFF_BIGENDIAN; zero otherwise. The test does not
     * classify MDI signatures as TIFF_BIGENDIAN.
     */
    extern int TIFFIsBigEndian(TIFF *tif);

    /**
     * @brief Test whether the handle uses the BigTIFF format.
     * @ingroup libtiff_public_lifecycle
     *
     * Reads stored header/flag state without I/O, mutation, allocation, or
     * diagnostic callbacks. There is no distinct error result. Directory- and
     * codec-dependent flags describe the current handle state rather than every
     * image in a multipage file.
     *
     * @param[in] tif Non-NULL live TIFF handle.
     * @return Nonzero for BigTIFF; zero for classic TIFF.
     */
    extern int TIFFIsBigTIFF(TIFF *tif);

    /**
     * @brief Return the installed client read callback.
     * @ingroup libtiff_public_lifecycle
     *
     * Reads the callback pointer without I/O, allocation, mutation, or
     * diagnostics. The pointer alone does not retain the client context or
     * backing code. Invoking it manually requires the matching live
     * TIFFClientdata() context and its callback contract; closing a resource
     * manually while the handle still uses it can invalidate the handle.
     *
     * @param[in] tif Non-NULL live TIFF handle.
     * @return The installed function pointer; no ownership is transferred. For
     * a successfully opened handle it is the non-NULL callback supplied at
     * open.
     */
    extern TIFFReadWriteProc TIFFGetReadProc(TIFF *tif);

    /**
     * @brief Return the installed client write callback.
     * @ingroup libtiff_public_lifecycle
     *
     * Reads the callback pointer without I/O, allocation, mutation, or
     * diagnostics. The pointer alone does not retain the client context or
     * backing code. Invoking it manually requires the matching live
     * TIFFClientdata() context and its callback contract; closing a resource
     * manually while the handle still uses it can invalidate the handle.
     *
     * @param[in] tif Non-NULL live TIFF handle.
     * @return The installed function pointer; no ownership is transferred. For
     * a successfully opened handle it is the non-NULL callback supplied at
     * open.
     */
    extern TIFFReadWriteProc TIFFGetWriteProc(TIFF *tif);

    /**
     * @brief Return the installed client seek callback.
     * @ingroup libtiff_public_lifecycle
     *
     * Reads the callback pointer without I/O, allocation, mutation, or
     * diagnostics. The pointer alone does not retain the client context or
     * backing code. Invoking it manually requires the matching live
     * TIFFClientdata() context and its callback contract; closing a resource
     * manually while the handle still uses it can invalidate the handle.
     *
     * @param[in] tif Non-NULL live TIFF handle.
     * @return The installed function pointer; no ownership is transferred. For
     * a successfully opened handle it is the non-NULL callback supplied at
     * open.
     */
    extern TIFFSeekProc TIFFGetSeekProc(TIFF *tif);

    /**
     * @brief Return the installed client close callback.
     * @ingroup libtiff_public_lifecycle
     *
     * Reads the callback pointer without I/O, allocation, mutation, or
     * diagnostics. The pointer alone does not retain the client context or
     * backing code. Invoking it manually requires the matching live
     * TIFFClientdata() context and its callback contract; closing a resource
     * manually while the handle still uses it can invalidate the handle.
     *
     * @param[in] tif Non-NULL live TIFF handle.
     * @return The installed function pointer; no ownership is transferred. For
     * a successfully opened handle it is the non-NULL callback supplied at
     * open.
     */
    extern TIFFCloseProc TIFFGetCloseProc(TIFF *tif);

    /**
     * @brief Return the installed client file-size callback.
     * @ingroup libtiff_public_lifecycle
     *
     * Reads the callback pointer without I/O, allocation, mutation, or
     * diagnostics. The pointer alone does not retain the client context or
     * backing code. Invoking it manually requires the matching live
     * TIFFClientdata() context and its callback contract; closing a resource
     * manually while the handle still uses it can invalidate the handle.
     *
     * @param[in] tif Non-NULL live TIFF handle.
     * @return The installed function pointer; no ownership is transferred. For
     * a successfully opened handle it is the non-NULL callback supplied at
     * open.
     */
    extern TIFFSizeProc TIFFGetSizeProc(TIFF *tif);

    /**
     * @brief Return the installed client mapping callback.
     * @ingroup libtiff_public_lifecycle
     *
     * Reads the callback pointer without I/O, allocation, mutation, or
     * diagnostics. The pointer alone does not retain the client context or
     * backing code. Invoking it manually requires the matching live
     * TIFFClientdata() context and its callback contract; closing a resource
     * manually while the handle still uses it can invalidate the handle.
     *
     * @param[in] tif Non-NULL live TIFF handle.
     * @return The installed function pointer; no ownership is transferred. For
     * a successfully opened handle it is non-NULL: the client's callback, or
     * a dummy that always reports failure when none was supplied at open.
     */
    extern TIFFMapFileProc TIFFGetMapFileProc(TIFF *tif);

    /**
     * @brief Return the installed client unmapping callback.
     * @ingroup libtiff_public_lifecycle
     *
     * Reads the callback pointer without I/O, allocation, mutation, or
     * diagnostics. The pointer alone does not retain the client context or
     * backing code. Invoking it manually requires the matching live
     * TIFFClientdata() context and its callback contract; closing a resource
     * manually while the handle still uses it can invalidate the handle.
     *
     * @param[in] tif Non-NULL live TIFF handle.
     * @return The installed function pointer; no ownership is transferred. For
     * a successfully opened handle it is non-NULL: the client's callback, or
     * a dummy that does nothing when none was supplied at open.
     */
    extern TIFFUnmapFileProc TIFFGetUnmapFileProc(TIFF *tif);
    /**
     * @brief Read the cached current scanline index.
     * @ingroup libtiff_public_image_layout
     *
     * Direct state read with no allocation, I/O, diagnostics, mutation, or
     * ownership transfer. The value reflects traversal/codec progress, which
     * can change even on failure, and does not prove successful pixel access or
     * predict every next operation. Directory reset installs UINT32_MAX as the
     * no-current sentinel. See @ref image_layout for scope.
     *
     * @param[in] tif Non-NULL live handle with consistent current geometry; no
     * ownership transfer.
     * @return Cached zero-based scanline index, or UINT32_MAX for the
     * reset/no-current state.
     *
     * Scanline read/write paths can record row + 1 after processing, including
     * some decoder failures; the result can equal ImageLength after the last
     * row.
     */
    extern uint32_t TIFFCurrentRow(TIFF *tif);
    /**
     * @brief Read the cached index of the current directory.
     * @ingroup libtiff_public_directory_navigation
     *
     * Reads state without I/O, allocation, diagnostics, or ownership transfer.
     * A main-chain read uses zero-based numbering; a newly entered unrecognised
     * SubIFD chain starts at zero. After writing an image directory the index
     * can describe the last written IFD while the in-memory directory is fresh.
     * Creation of an ordinary directory preserves the old index. A failed read
     * can leave an index without a fully valid directory. See @ref
     * directory_navigation for these distinctions.
     *
     * @param[in] tif Non-NULL live handle; caller must serialise access.
     * @return Cached index, or (tdir_t)UINT_MAX for an absent/unknown index;
     * not an independent validity test.
     */
    extern tdir_t TIFFCurrentDirectory(TIFF *tif);
    /**
     * @brief Count reachable main-chain directories from the cached file
     * header.
     * @ingroup libtiff_public_directory_navigation
     *
     * Walks directory links without loading their tag values, starting from the
     * cached header rather than the current SubIFD. Does not count unwritten
     * directories or traverse SubIFD arrays/custom metadata. Updates
     * index/offset maps and the cached directory count; leaves current tag
     * storage/index unchanged but can move client I/O position and allocate map
     * storage. Read errors dispatch diagnostics and return the prefix counted
     * so far. A detected next-link loop warns and is treated as the chain end.
     * Map-creation exhaustion can be diagnosed while traversal continues
     * without full loop tracking. A zero or partial count is not a separate
     * error status. No ownership transfers.
     *
     * @param[in,out] tif Non-NULL live handle; caller must serialise access.
     * @return Number successfully traversed, possibly zero/partial on error;
     * also stored in the cached count.
     */
    extern tdir_t TIFFNumberOfDirectories(TIFF *tif);
    /**
     * @brief Read the cached file offset of the current directory.
     * @ingroup libtiff_public_directory_navigation
     *
     * Reads state without I/O, allocation, diagnostics, or ownership transfer.
     * A nonzero result can identify a directory whose read later failed; zero
     * can mean a fresh directory or stepping beyond the chain. It is not proof
     * that current tag values came from that offset. See @ref
     * directory_navigation.
     *
     * @param[in] tif Non-NULL live handle; caller must serialise access.
     * @return Directory offset in file bytes, or zero when no current file
     * offset is recorded.
     */
    extern uint64_t TIFFCurrentDirOffset(TIFF *tif);
    /**
     * @brief Read the cached current strip index.
     * @ingroup libtiff_public_image_layout
     *
     * Direct state read with no allocation, I/O, diagnostics, mutation, or
     * ownership transfer. The value reflects traversal/codec progress, which
     * can change even on failure, and does not prove successful pixel access or
     * predict every next operation. Directory reset installs UINT32_MAX as the
     * no-current sentinel. See @ref image_layout for scope.
     *
     * @param[in] tif Non-NULL live handle with consistent current geometry; no
     * ownership transfer.
     * @return Cached zero-based strip index, or UINT32_MAX for the
     * reset/no-current state.
     */
    extern uint32_t TIFFCurrentStrip(TIFF *tif);
    /**
     * @brief Read the cached current tile index.
     * @ingroup libtiff_public_image_layout
     *
     * Direct state read with no allocation, I/O, diagnostics, mutation, or
     * ownership transfer. The value reflects traversal/codec progress, which
     * can change even on failure, and does not prove successful pixel access or
     * predict every next operation. Directory reset installs UINT32_MAX as the
     * no-current sentinel. See @ref image_layout for scope.
     *
     * @param[in] tif Non-NULL live handle with consistent current geometry; no
     * ownership transfer.
     * @return Cached zero-based tile index, or UINT32_MAX for the
     * reset/no-current state.
     */
    extern uint32_t TIFFCurrentTile(TIFF *tif);
    /**
     * @brief Replace the handle's raw reading buffer.
     * @ingroup libtiff_public_image_buffers
     *
     * Detaches old raw storage, freeing it only when TIFF_MYBUFFER marks
     * ownership. A non-NULL bp is borrowed: keep it live, writable, and large
     * enough until detached/handle cleanup; the library does not free it. NULL
     * requests matching Ext allocation owned/released by the handle. Never pass
     * the old owned allocation back as bp: it is freed first. Replacement is
     * destructive, with no rollback on allocation failure. Requires a
     * raw-capable codec; owned capacity is rounded to 1024 bytes and
     * zero-filled. Explicit borrowed capacity is stored as supplied, so the
     * caller must provide a valid positive size. Does not fully restart codec
     * state or initialise every raw cursor/count field. Allocation/policy
     * failures dispatch diagnostics. No caller allocation ownership transfers
     * merely by attaching bp.
     *
     * @param[in,out] tif Non-NULL live handle with matching current geometry
     * and codec methods; serialise access externally.
     * @param[in] bp Borrowed writable buffer that later raw reads fill, or
     * NULL to allocate owned storage; not dereferenced here and must not alias
     * an allocation being released.
     * @param[in] size Positive buffer capacity in bytes; with bp NULL it is
     * rounded up to a multiple of 1024, and a size that rounds to zero is
     * rejected.
     * @return One on attachment/allocation success, zero on unsupported raw
     * reading or detected size/allocation failure; old storage may already be
     * gone.
     */
    extern int TIFFReadBufferSetup(TIFF *tif, void *bp, tmsize_t size);
    /**
     * @brief Replace the handle's encoding buffer.
     * @ingroup libtiff_public_image_buffers
     *
     * Detaches old raw storage, freeing it only when TIFF_MYBUFFER marks
     * ownership. A non-NULL bp is borrowed: keep it live, writable, and large
     * enough until detached/handle cleanup; the library does not free it. NULL
     * requests matching Ext allocation owned/released by the handle. Never pass
     * the old owned allocation back as bp: it is freed first. Replacement is
     * destructive, with no rollback on allocation failure. Minus one forces
     * owned allocation, ignoring bp, using a geometry estimate plus margin and
     * at least 8192 bytes. Initialises raw cursor/count and buffer-setup flag
     * on success; does not flush pending encoded bytes before replacement.
     * Allocation failure dispatches a diagnostic. No caller allocation
     * ownership transfers merely by attaching bp.
     *
     * @param[in,out] tif Non-NULL live handle with matching current geometry
     * and codec methods; serialise access externally.
     * @param[in] bp Borrowed writable buffer that later encoding fills, or NULL
     * to allocate owned storage; not dereferenced here and must not alias an
     * allocation being released.
     * @param[in] size Buffer capacity in bytes, or minus one to allocate owned
     * storage sized from the current geometry (bp is then ignored).
     * @return One when the buffer is attached or allocated; zero if allocating
     * owned storage fails (including a nonpositive explicit size with bp
     * NULL). Any previously owned buffer has already been freed.
     */
    extern int TIFFWriteBufferSetup(TIFF *tif, void *bp, tmsize_t size);
    /**
     * @brief Replace strile offset/count arrays from current geometry.
     * @ingroup libtiff_public_image_buffers
     *
     * Computes counts, enforces directory array-size limits, frees old arrays
     * and allocates zeroed replacements with the handle allocator. Requires
     * consistent nonzero sample/row/tile geometry, particularly separate-plane
     * divisor. Updates cached counts before allocations; failure can leave one
     * array allocated and the other NULL, with old values lost. Sets
     * offset/count presence bits only on success. Does not write payloads or
     * the IFD. Diagnostics follow geometry/size/allocation helpers; borrowed
     * array views are invalidated.
     *
     * @param[in,out] tif Non-NULL live handle with matching current geometry
     * and codec methods; serialise access externally.
     * @return One after both arrays are initialised, zero on
     * count/size/allocation failure; not transactional.
     */
    extern int TIFFSetupStrips(TIFF *tif);
    /**
     * @brief Validate and prepare geometry for image writing.
     * @ingroup libtiff_public_image_buffers
     *
     * Checks writable mode and organisation, attempts deferred-array fill
     * without propagating its result, validates image-width presence, allocates
     * missing strile arrays, computes cached tile/scanline sizes, and sets
     * TIFF_BEENWRITING to freeze most format tags. Can force deferred
     * strile-array writing without propagating that status. Does not validate
     * every tag, input buffer, or codec operation. Reads deferred strile arrays
     * when present and, in update mode with placeholder strile entries,
     * rewrites them in the file through TIFFForceStrileArrayWriting(). Invokes
     * tag/diagnostic callbacks but no codec callbacks, and encodes no image
     * data. Failure can leave new arrays/counts/caches and flags; no rollback.
     * No ownership transfers. No errno or durability guarantee. See
     * @ref image_transfers.
     *
     * @param[in,out] tif Non-NULL live handle with matching current geometry
     * and codec methods; serialise access externally.
     * @param[in] tiles Exactly one for tiled operations, zero for
     * strips/scanlines.
     * @param[in] module Borrowed diagnostic context string, or NULL.
     * @return One after required preparation, zero on detected
     * mode/organisation/required geometry/allocation failure; nested errors can
     * coexist with one.
     */
    extern int TIFFWriteCheck(TIFF *tif, int tiles, const char *module);
    /**
     * @brief Release the current directory's owned values and codec state.
     * @ingroup libtiff_public_directory_navigation
     *
     * Invokes codec cleanup, clears presence bits, and releases directory-owned
     * arrays, strings, custom-value storage, deferred strile entries, and
     * IFD-size bookkeeping. Borrowed pointers into this storage become invalid.
     * Does not free/close the handle or its client I/O, persist dirty values,
     * dispose the field schema, or fully reset scalar values, offsets, and
     * indices. Initialise a new directory before using it for normal tag/image
     * operations. Core cleanup returns no status; codec callbacks may have
     * their own diagnostics/side effects.
     *
     * @param[in,out] tif Non-NULL live handle; caller must serialise access.
     */
    extern void TIFFFreeDirectory(TIFF *tif);
    /**
     * @brief Replace current values with a fresh in-memory image directory.
     * @ingroup libtiff_public_directory_navigation
     *
     * Discards current directory values and codec state through
     * TIFFFreeDirectory(), then installs defaults and the image schema through
     * TIFFDefaultDirectory(). Invalidates borrowed values and replaceable
     * descriptors. Does not save dirty values or write an IFD; use this only
     * when discarding them is intended. No new TIFF handle is allocated. Setup
     * may allocate and invoke the process-wide tag extender and codec setup,
     * with diagnostics but no reliable status propagation. Clears current/next
     * directory and image-data offsets and marks the directory unwritten. The
     * cached directory index, directory maps, and other write state are not
     * comprehensively reset; this is not equivalent to reopening a handle.
     *
     * @param[in,out] tif Non-NULL live handle; caller must serialise access.
     * @return Zero unconditionally, including when internal setup reports a
     * failure; not a Boolean success indication.
     */
    extern int TIFFCreateDirectory(TIFF *tif);
    /**
     * @brief Replace current values with a fresh custom metadata directory.
     * @ingroup libtiff_public_directory_navigation
     *
     * Discards current directory values and codec state through
     * TIFFFreeDirectory(), then installs defaults and the image schema through
     * TIFFDefaultDirectory(). Invalidates borrowed values and replaceable
     * descriptors. Does not save dirty values or write an IFD; use this only
     * when discarding them is intended. No new TIFF handle is allocated. Setup
     * may allocate and invoke the process-wide tag extender and codec setup,
     * with diagnostics but no reliable status propagation. Subsequently
     * replaces the image field table with the supplied custom schema; the
     * extender does not extend the final custom table. Clears current/next/data
     * offsets, sets the index to (tdir_t)UINT_MAX, destroys directory maps, and
     * forces the next indexed selection to start from the main-chain header.
     * Descriptors/names remain borrowed and must outlive their use. The schema
     * record itself is only read during setup. The new metadata must later be
     * written with TIFFWriteCustomDirectory() and linked by the appropriate
     * parent tag; creation does not link it.
     *
     * @param[in,out] tif Non-NULL live handle; caller must serialise access.
     * @param[in] infoarray Non-NULL valid private schema with long-lived
     * descriptors and names; TIFFFieldArray is opaque in the public header.
     * @return Zero unconditionally, even after reported setup failure; inspect
     * diagnostics rather than interpreting zero as failure.
     */
    extern int TIFFCreateCustomDirectory(TIFF *tif,
                                         const TIFFFieldArray *infoarray);
    /**
     * @brief Create a fresh in-memory EXIF metadata directory.
     * @ingroup libtiff_public_directory_navigation
     *
     * Delegates to TIFFCreateCustomDirectory() with the built-in EXIF schema
     * and all its discard, pointer-invalidation, index/map reset, callback, and
     * failure rules. The caller needs no private schema definition. Does not
     * persist or link the metadata directory; write/link it separately.
     *
     * @param[in,out] tif Non-NULL live handle; caller must serialise access.
     * @return Zero unconditionally; internal setup failures can report
     * diagnostics without changing this return value.
     */
    extern int TIFFCreateEXIFDirectory(TIFF *tif);
    /**
     * @brief Create a fresh in-memory EXIF GPS metadata directory.
     * @ingroup libtiff_public_directory_navigation
     *
     * Delegates to TIFFCreateCustomDirectory() with the built-in EXIF GPS
     * schema and all its discard, pointer-invalidation, index/map reset,
     * callback, and failure rules. The caller needs no private schema
     * definition. Does not persist or link the metadata directory; write/link
     * it separately.
     *
     * @param[in,out] tif Non-NULL live handle; caller must serialise access.
     * @return Zero unconditionally; internal setup failures can report
     * diagnostics without changing this return value.
     */
    extern int TIFFCreateGPSDirectory(TIFF *tif);
    /**
     * @brief Test whether the cached next-directory link is zero.
     * @ingroup libtiff_public_directory_navigation
     *
     * Does not scan or validate the file. A fresh unwritten directory also
     * satisfies this test, and a loop-truncated chain can appear finished. No
     * allocation, I/O, mutation, diagnostic, or ownership transfer occurs.
     *
     * @param[in] tif Non-NULL live handle; caller must serialise access.
     * @return Nonzero if the cached next offset is zero; zero otherwise.
     */
    extern int TIFFLastDirectory(TIFF *tif);
    /**
     * @brief Select and read an image directory by its main-chain index.
     * @ingroup libtiff_public_directory_navigation
     *
     * Uses a cached offset, relative stepping, or traversal from the cached
     * header as appropriate. After leaving a custom/SubIFD context it clears
     * those maps and starts from the main-chain header. It does not select a
     * SubIFD by array position. Does not save dirty values first. Successful
     * reading replaces directory/codec state and invalidates borrowed values
     * and replaceable descriptors. Once reading starts, failure can leave
     * changed offsets, index, next-link, schema, flags, and partially loaded
     * values; there is no rollback to the old directory. Unreachable indices
     * can fail earlier with old values still present but altered maps,
     * traversal flags, or I/O position. Diagnostics follow traversal/reader
     * failures; absence can return zero silently. No ownership transfers and no
     * errno guarantee is provided.
     *
     * @param[in,out] tif Non-NULL live handle; caller must serialise access.
     * @param[in] dirn Zero-based main-chain directory index.
     * @return One when TIFFReadDirectory() succeeds, zero if unreachable,
     * looping, or a traversal/read failure occurs; zero does not preserve the
     * old state.
     */
    extern int TIFFSetDirectory(TIFF *tif, tdir_t dirn);
    /**
     * @brief Select and read an image directory at a file offset.
     * @ingroup libtiff_public_directory_navigation
     *
     * Use this for image SubIFDs; EXIF/GPS/custom metadata requires the
     * matching custom reader/schema. An offset known to the maps retains its
     * mapped index. An unrecognised successful offset starts a new chain
     * indexed at zero, clears/reseeds maps, and forces a later
     * TIFFSetDirectory() to restart at the main-chain header. The lookup may
     * first scan main-chain links. Failures may still change traversal state,
     * and a final map-registration failure can follow a successful directory
     * read. A zero offset is a special state operation: clears current/next
     * offsets, sets the index unknown and unwritten flag, keeps old tag values,
     * performs no directory read, and returns zero. Does not save dirty values
     * first. Successful reading replaces directory/codec state and invalidates
     * borrowed values and replaceable descriptors. Once reading starts, failure
     * can leave changed offsets, index, next-link, schema, flags, and partially
     * loaded values; there is no rollback to the old directory. An unreadable
     * or loop-detected offset can fail before old values are replaced, but
     * after the map lookup has scanned the main chain and changed maps, flags,
     * or I/O position. Diagnostics follow traversal/reader failures. No
     * ownership transfers and no errno guarantee is provided.
     *
     * @param[in,out] tif Non-NULL live handle; caller must serialise access.
     * @param[in] diroff Image-IFD offset in file bytes, or zero for the special
     * no-current-offset operation.
     * @return One on read/map-registration success, zero for zero offset,
     * read/traversal failure, or detected map-registration failure; no rollback
     * is promised.
     */
    extern int TIFFSetSubDirectory(TIFF *tif, uint64_t diroff);
    /**
     * @brief Remove an image directory from the on-file main chain.
     * @ingroup libtiff_public_directory_navigation
     *
     * Requires a writable handle and a reachable main-chain entry. Patches the
     * preceding link (or header) to the following directory; does not reclaim
     * the removed IFD or image-data bytes, truncate the file, or update
     * references such as SubIFD arrays. Does not flush current dirty
     * values/data first. After a successful link write, releases owned
     * raw-buffer/directory storage and codec state, installs defaults, clears
     * write/current/next/last offsets and maps, and leaves a fresh directory
     * intended for appending. Borrowed storage is invalidated and cached
     * counts/index change. Seek/write/chain failures dispatch diagnostics but
     * can leave changed maps, position, or file bytes; no rollback is
     * attempted. The patch seek result is not independently checked, and
     * post-write default-setup errors do not change the success result. No
     * handle ownership transfers.
     *
     * @param[in,out] tif Non-NULL live handle; caller must serialise access.
     * @param[in] dirn One-based main-chain position: the first directory is 1,
     * unlike TIFFSetDirectory(). Zero is rejected.
     * @return One after a successful link write/reset attempt; zero for
     * read-only mode, zero index, a nonexistent directory (diagnosed either as
     * nonexistent or as a possible IFD loop), or a detected traversal/write
     * failure.
     */
    extern int TIFFUnlinkDirectory(TIFF *tif, tdir_t dirn);
    /**
     * @brief Set a tag value in the current in-memory directory.
     * @ingroup libtiff_public_tag_access
     *
     * See @ref tag_arguments for exact promoted values, counts, array lengths,
     * and exceptions. Changes update memory and dirty/field bits; file-
     * directory persistence requires a later write/flush. Unknown tags or
     * disallowed changes after writing begins are diagnosed and rejected
     * (ImageLength is a special exception). Codec selection may clean up and
     * reinitialise codec state. Core setters copy arrays/strings without taking
     * ownership, but ExtraSamples may repair a caller element in place. Input
     * storage must not alias replaced owned storage. Failure can discard old
     * values, alter codec/related tags, or leave a partial custom entry; there
     * is no rollback. Some void array-copy helpers lose data on allocation
     * failure without changing the setter's success result. Diagnostics and
     * errno alone do not establish success.
     *
     * @param[in,out] tif Non-NULL live TIFF handle; accessed without internal
     * locking.
     * @param[in] tag Registered tag or pseudo-tag number.
     * @param[in,out] ... Promoted tag-specific values and borrowed buffers;
     * ExtraSamples may be repaired in place. See @ref tag_arguments.
     * @return One when accepted, zero on rejection or detected failure;
     * acceptance is not proof that every array allocation succeeded.
     */
    extern int TIFFSetField(TIFF *tif, uint32_t tag, ...);
    /**
     * @brief Set a tag value in the current in-memory directory.
     * @ingroup libtiff_public_tag_access
     *
     * See @ref tag_arguments for exact promoted values, counts, array lengths,
     * and exceptions. Changes update memory and dirty/field bits; file-
     * directory persistence requires a later write/flush. Unknown tags or
     * disallowed changes after writing begins are diagnosed and rejected
     * (ImageLength is a special exception). Codec selection may clean up and
     * reinitialise codec state. Core setters copy arrays/strings without taking
     * ownership, but ExtraSamples may repair a caller element in place. Input
     * storage must not alias replaced owned storage. Failure can discard old
     * values, alter codec/related tags, or leave a partial custom entry; there
     * is no rollback. Some void array-copy helpers lose data on allocation
     * failure without changing the setter's success result. Diagnostics and
     * errno alone do not establish success.
     *
     * @param[in,out] tif Non-NULL live TIFF handle; accessed without internal
     * locking.
     * @param[in] tag Registered tag or pseudo-tag number.
     * @param[in,out] ap Borrowed tag-specific value list; traversal and
     * permitted mutable input data can change. Use va_copy() before reusing.
     * @return One when accepted, zero on rejection or detected failure;
     * acceptance is not proof that every array allocation succeeded.
     */
    extern int TIFFVSetField(TIFF *tif, uint32_t tag, va_list ap);
    /**
     * @brief Clear tag presence or remove a stored custom value.
     * @ingroup libtiff_public_tag_access
     *
     * A non-custom tag only has its field bit cleared: stored scalar/array
     * values and codec configuration are not reset, and a shared bit can hide
     * multiple tags. A custom entry frees its owned value and compacts the
     * value list without unregistering its descriptor. Marks the directory
     * dirty even when a known custom value was absent. Does not enforce setter
     * mutability rules or persist changes. Unknown-tag warnings follow
     * TIFFFieldWithTag(). No rollback or general resource-reset operation is
     * provided.
     *
     * @param[in,out] tif Non-NULL live TIFF handle; accessed without internal
     * locking.
     * @param[in] tag Tag number whose descriptor is registered.
     * @return One for a known descriptor even if no value was present; zero for
     * an unknown tag.
     */
    extern int TIFFUnsetField(TIFF *tif, uint32_t tag);
    /**
     * @brief Finish the current image directory and prepare a fresh one.
     * @ingroup libtiff_public_ifd_writing
     *
     * Serialises current values, allocates temporary entry/conversion buffers,
     * may load strile arrays, and invokes client I/O and tag/codec/diagnostic
     * callbacks. May overwrite a fitting IFD or relocate it when its recorded
     * reusable span is too small. Tag data or links can be written before final
     * IFD completion. Failure can leave partially changed file bytes,
     * offsets/maps/flags, buffers, and values; there is no rollback or
     * durability/fsync guarantee. Some array emitters swap borrowed directory
     * storage in place and do not restore it, including on failure. Internal
     * strile-fill, map-check, and fresh-default errors are not uniformly
     * propagated; success does not mean every nested operation was error-free.
     * See @ref ifd_io. Finalises postencoding and encoder close, flushes
     * applicable raw data, releases owned raw buffers, and on success frees old
     * directory/codec values and installs fresh defaults. Borrowed
     * values/descriptors can be invalidated; the TIFF handle and client
     * resource remain owned by the caller. New IFDs are linked into the main
     * chain or pending SubIFD slots. Read-only mode returns one without doing
     * this work.
     *
     * @param[in,out] tif Non-NULL live handle with matching schema/state;
     * serialise access externally.
     * @return One on completed write/reset attempt or a read-only no-op, zero
     * on a detected encoding, size, allocation, or I/O failure; success is not
     * a durability guarantee.
     */
    extern int TIFFWriteDirectory(TIFF *tif);
    /**
     * @brief Write current custom values and return their IFD offset.
     * @ingroup libtiff_public_ifd_writing
     *
     * Serialises current values, allocates temporary entry/conversion buffers,
     * may load strile arrays, and invokes client I/O and tag/codec/diagnostic
     * callbacks. May overwrite a fitting IFD or relocate it when its recorded
     * reusable span is too small. Tag data or links can be written before final
     * IFD completion. Failure can leave partially changed file bytes,
     * offsets/maps/flags, buffers, and values; there is no rollback or
     * durability/fsync guarantee. Some array emitters swap borrowed directory
     * storage in place and do not restore it, including on failure. Internal
     * strile-fill, map-check, and fresh-default errors are not uniformly
     * propagated; success does not mean every nested operation was error-free.
     * See @ref ifd_io. Uses the custom-value list rather than core image field
     * bits. Retains in-memory values and does not link the IFD into an
     * image/metadata parent; the caller must store the offset in the
     * appropriate parent tag. Can reuse a recorded fitting custom span or
     * append at EOF. Does not finalise encoder/raw-buffer state. Read-only mode
     * returns one before assigning the output.
     *
     * @param[in,out] tif Non-NULL live handle with matching schema/state;
     * serialise access externally.
     * @param[out] pdiroff Non-NULL offset slot. May be assigned before a later
     * failure; unchanged by the read-only no-op. Use only after an actual
     * successful write.
     * @return One after writing, or for the read-only no-op; zero on detected
     * failure. One alone does not prove pdiroff was assigned.
     */
    extern int TIFFWriteCustomDirectory(TIFF *tif, uint64_t *pdiroff);
    /**
     * @brief Write the current image directory while retaining its values.
     * @ingroup libtiff_public_ifd_writing
     *
     * Serialises current values, allocates temporary entry/conversion buffers,
     * may load strile arrays, and invokes client I/O and tag/codec/diagnostic
     * callbacks. May overwrite a fitting IFD or relocate it when its recorded
     * reusable span is too small. Tag data or links can be written before final
     * IFD completion. Failure can leave partially changed file bytes,
     * offsets/maps/flags, buffers, and values; there is no rollback or
     * durability/fsync guarantee. Some array emitters swap borrowed directory
     * storage in place and do not restore it, including on failure. Internal
     * strile-fill, map-check, and fresh-default errors are not uniformly
     * propagated; success does not mean every nested operation was error-free.
     * See @ref ifd_io. Does not perform the final
     * postencode/encoder-close/raw-buffer teardown of TIFFWriteDirectory().
     * Retains directory arrays/codec context for continued writing, although
     * in-place swapping can change array contents. Attempts strip setup without
     * checking its result first. Always seeks to EOF and resets the image write
     * offset afterwards, even when the IFD write failed; that seek result is
     * not checked. A checkpoint is not a guarantee that buffered pixel data is
     * complete or decoder-ready. Read-only handling in the shared writer is a
     * success no-op, but surrounding setup/seek can still run.
     *
     * @param[in,out] tif Non-NULL live handle with matching schema/state;
     * serialise access externally.
     * @return Shared writer result: one on completion/read-only no-op, zero on
     * detected writer failure; EOF reposition/setup failures are not
     * independently reflected.
     */
    extern int TIFFCheckpointDirectory(TIFF *tif);
    /**
     * @brief Relocate the current image IFD and repair its main-chain link.
     * @ingroup libtiff_public_ifd_writing
     *
     * Serialises current values, allocates temporary entry/conversion buffers,
     * may load strile arrays, and invokes client I/O and tag/codec/diagnostic
     * callbacks. May overwrite a fitting IFD or relocate it when its recorded
     * reusable span is too small. Tag data or links can be written before final
     * IFD completion. Failure can leave partially changed file bytes,
     * offsets/maps/flags, buffers, and values; there is no rollback or
     * durability/fsync guarantee. Some array emitters swap borrowed directory
     * storage in place and do not restore it, including on failure. Internal
     * strile-fill, map-check, and fresh-default errors are not uniformly
     * propagated; success does not mean every nested operation was error-free.
     * See @ref ifd_io. If no on-file offset exists, delegates to
     * TIFFWriteDirectory(). Otherwise detaches the existing IFD from its
     * preceding main-chain/header link before writing the replacement at EOF;
     * old bytes are not reclaimed. Requires an IFD reachable through that
     * chain; this is not a general SubIFD relocation operation. A failure after
     * detachment can leave the chain broken. Successful completion resets
     * values like TIFFWriteDirectory(). The relocation wrapper has no initial
     * read-only guard: do not call it on a read-only handle with an existing
     * offset.
     *
     * @param[in,out] tif Non-NULL live handle with matching schema/state;
     * serialise access externally.
     * @return One when the replacement/write completes, zero on detected chain,
     * encoding, allocation, or I/O failure; neither old file links nor handle
     * state are preserved on failure.
     */
    extern int TIFFRewriteDirectory(TIFF *tif);
    /**
     * @brief Reserve strile-array entries for deferred payload writing.
     * @ingroup libtiff_public_ifd_writing
     *
     * Requires a writable current directory whose current IFD offset is zero,
     * before its first directory write. Sets a directory-local flag so
     * offset/byte-count entries are emitted as zero type/count/offset
     * placeholders. Does not allocate, flush, emit arrays, or transfer
     * ownership. Use the reviewed sequence in @ref ifd_io with
     * TIFFWriteCheck(), directory writing, reselecting that IFD, and
     * TIFFForceStrileArrayWriting(). The flag is reset with directory state.
     * Read-only/already-written rejection dispatches an error and leaves the
     * flag unchanged.
     *
     * @param[in,out] tif Non-NULL live handle with matching schema/state;
     * serialise access externally.
     * @return One after setting the flag, zero for read-only mode or an already
     * written directory.
     */
    extern int TIFFDeferStrileArrayWriting(TIFF *tif);
    /**
     * @brief Patch on-file strile offset and byte-count payloads.
     * @ingroup libtiff_public_ifd_writing
     *
     * Requires a writable, already-written, non-mapped IFD with no dirty
     * changes beyond strile arrays. Accepts dirty striles or both recognised
     * deferred placeholders. May allocate strip storage and conversion buffers,
     * seek/read entries, and write/append payloads. Calls _TIFFRewriteField()
     * for offsets then byte counts; failure can leave only the first patched,
     * and no rollback is attempted. Does not finish codec/raw data or rewrite
     * arbitrary metadata. On both successful patches clears dirty-strile and
     * been-writing flags. Input arrays remain handle-owned and are copied for
     * conversion by the patch helper; no ownership transfers. Errors dispatch
     * diagnostics; use TIFFRewriteDirectory() for other dirty metadata.
     *
     * @param[in,out] tif Non-NULL live handle with matching schema/state;
     * serialise access externally.
     * @return One after both field patches, zero on rejected state, allocation,
     * entry-conversion, or I/O failure; file/state may already be changed.
     */
    extern int TIFFForceStrileArrayWriting(TIFF *tif);

#if defined(c_plusplus) || defined(__cplusplus)
    extern void TIFFPrintDirectory(TIFF *tif, FILE *fd, long flags = 0);
    extern int TIFFReadScanline(TIFF *tif, void *buf, uint32_t row,
                                uint16_t sample = 0);
    extern int TIFFWriteScanline(TIFF *tif, void *buf, uint32_t row,
                                 uint16_t sample = 0);
    extern int TIFFReadRGBAImage(TIFF *, uint32_t, uint32_t, uint32_t *,
                                 int = 0);
    extern int TIFFReadRGBAImageOriented(TIFF *, uint32_t, uint32_t, uint32_t *,
                                         int = ORIENTATION_BOTLEFT, int = 0);
#else
/**
 * @brief Print a human-readable description of the current directory.
 * @ingroup libtiff_public_directory_printing
 *
 * Requires consistent initialised directory/codec state, including valid
 * present-tag pointers and array lengths. Prints core fields, stored custom
 * values, the installed print callback, then optional strile rows. Flags
 * control selected large core tables, not all custom output; even
 * TIFFPRINT_NONE can emit full custom arrays or raw XML. C++ permits
 * omitting flags (default zero); C requires all three arguments. See @ref
 * directory_printing for formatting and representation limits.
 *
 * Borrows the handle/stream and all values without transferring ownership.
 * Can allocate temporary scalar getter buffers, invoke tag/codec/print
 * callbacks, change field-lookup/cache state, and load deferred strile
 * arrays with I/O; borrowed array pointers can be invalidated by loading.
 * Does not change directory selection or decode pixels in the core path,
 * and does not save dirty metadata. Callbacks may have additional effects.
 * Serialise handle access and output externally if ordering matters.
 *
 * Returns no status, does not check stdio failures, and does not flush or
 * close the stream. Output can be partial; use ferror() and
 * caller-controlled flushing for stream status. Getter/metadata/allocation
 * failures can dispatch diagnostics or omit fields, and strile errors can
 * appear as zero values without an aggregate error indicator. Scalar
 * temporary allocation is not checked before passing its result to
 * TIFFGetField(), so exhaustion is not uniformly handled safely. This is a
 * diagnostic formatter, not validation or a complete serialisation API; no
 * errno guarantee or rollback.
 *
 * @param[in,out] tif Non-NULL live handle with consistent current values
 * and methods; may mutate caches and invoke callbacks.
 * @param[in,out] fd Non-NULL writable caller-owned stdio stream; not closed
 * or flushed by this helper.
 * @param[in] flags Bitwise OR of TIFFPRINT_* values; zero for ordinary
 * detail. Unrecognised/legacy bits are forwarded unchanged to the callback.
 */
extern void TIFFPrintDirectory(TIFF *tif, FILE *fd, long flags);
/**
 * @brief Decode one image scanline into caller storage.
 * @ingroup libtiff_public_image_reading
 *
 * Requires readable strip organisation; validates row and separate-plane
 * sample through TIFFSeek(). A whole decoded scanline must fit in buf.
 * Invokes decoder and postdecode; after decoder dispatch caches row+1 even
 * on failure. A failed seek can zero the output, while initial
 * mode/organisation rejection or codec failure need not. NULL output is
 * only supported by a specifically configured callback-based codec path,
 * not generally. C++ defaults sample to zero. May allocate or replace
 * handle-owned raw/strile buffers, load deferred metadata, seek and read
 * client I/O, and invoke codec/tag/diagnostic callbacks; reading never
 * writes to the file. No handle or caller-buffer ownership transfers.
 * Failure is not transactional: buffers, codec progress, indices, flags,
 * metadata, or file position can already have changed; disregard partial
 * decoded output unless a codec explicitly documents otherwise. No errno or
 * rollback guarantee. See @ref image_transfers.
 *
 * @param[in,out] tif Non-NULL live handle with matching current geometry
 * and codec methods; serialise access externally.
 * @param[out] buf Caller-owned writable output of at least
 * TIFFScanlineSize() bytes.
 * @param[in] row Zero-based row below ImageLength.
 * @param[in] sample Separate sample plane; ordinary contiguous access uses
 * zero.
 * @return One after positive decoder success, minus one for mode, geometry,
 * seek, or decode failure.
 */
extern int TIFFReadScanline(TIFF *tif, void *buf, uint32_t row,
                            uint16_t sample);
/**
 * @brief Encode and buffer one image scanline.
 * @ingroup libtiff_public_image_writing
 *
 * Requires strip organisation and a writable handle. Can extend ImageLength
 * for contiguous storage, not separate planes; skipped/backward rows depend
 * on codec seek support. Performs in-place postdecode byte swapping on the
 * input before encoding and does not restore it. Does not by itself persist
 * the IFD or guarantee all compressed data has reached the file. Row
 * advances to row+1 after encoder dispatch even when it fails. C++ defaults
 * sample to zero. May allocate or replace handle-owned raw/strile buffers,
 * load deferred metadata, seek and write client I/O, and invoke
 * codec/tag/diagnostic callbacks. No handle or caller-buffer ownership
 * transfers. Failure is not transactional: buffers, codec progress,
 * indices, flags, metadata, file position, or file bytes can already have
 * changed, and data already written is not removed. No errno, rollback, or
 * durability guarantee. See @ref image_transfers.
 *
 * @param[in,out] tif Non-NULL live handle with matching current geometry
 * and codec methods; serialise access externally.
 * @param[in,out] buf Caller-owned scanline of at least TIFFScanlineSize()
 * bytes; may be byte-swapped in place before encoding.
 * @param[in] row Zero-based row; for contiguous storage a row at or beyond
 * ImageLength extends it to row+1.
 * @param[in] sample Separate sample plane; ordinary contiguous access uses
 * zero.
 * @return Encoder callback status (normally one/zero), or minus one for
 * setup/seek rejection; zero must also be treated as failure.
 */
extern int TIFFWriteScanline(TIFF *tif, void *buf, uint32_t row,
                             uint16_t sample);
/**
 * @brief Read the current image into a packed RGBA raster.
 * @ingroup libtiff_public_rgba
 *
 * Runs Begin/Get/End, reporting Begin reason text through TIFFErrorExtR().
 * Requests bottom-left raster origin. Clipping, untouched excess raster
 * area, alpha interpretation and failure-state semantics follow
 * TIFFRGBAImageGet(). Borrows the handle and raster; does not close the
 * handle or acquire raster ownership. May allocate conversion/raw buffers,
 * invoke tag/codec/diagnostic callbacks and perform client reads/seeks.
 * Failure can leave partial raster and changed handle/codec state; no
 * rollback or errno contract. See @ref rgba.
 *
 * @param[in,out] tif Non-NULL readable live handle; keep its current
 * directory and codec live throughout conversion and serialise access.
 * @param[in] rwidth Positive raster row stride in pixels.
 * @param[in] rheight Positive raster height; allocation product must be
 * representable.
 * @param[out] raster Caller-owned writable array of at least rwidth*rheight
 * packed pixels.
 * @param[in] stop Nonzero stops on detected decode errors; zero permits
 * selected recovery paths; C++ default is zero.
 * @return One for default-getter completion, zero on setup/fatal read
 * failure; tolerated corrupt data can still yield one.
 */
extern int TIFFReadRGBAImage(TIFF *tif, uint32_t rwidth, uint32_t rheight,
                             uint32_t *raster, int stop);
/**
 * @brief Read the current image into a packed RGBA raster.
 * @ingroup libtiff_public_rgba
 *
 * Runs Begin/Get/End, reporting Begin reason text through TIFFErrorExtR().
 * Uses the requested orientation; C++ defaults to bottom-left. Clipping,
 * untouched excess raster area, alpha interpretation and failure-state
 * semantics follow TIFFRGBAImageGet(). Borrows the handle and raster; does
 * not close the handle or acquire raster ownership. May allocate
 * conversion/raw buffers, invoke tag/codec/diagnostic callbacks and perform
 * client reads/seeks. Failure can leave partial raster and changed
 * handle/codec state; no rollback or errno contract. See @ref rgba.
 *
 * @param[in,out] tif Non-NULL readable live handle; keep its current
 * directory and codec live throughout conversion and serialise access.
 * @param[in] rwidth Positive raster row stride in pixels.
 * @param[in] rheight Positive raster height; allocation product must be
 * representable.
 * @param[out] raster Caller-owned writable array of at least rwidth*rheight
 * packed pixels.
 * @param[in] orientation Requested TIFF orientation 1..8; default
 * implementation flips without transposition, invalid values fall back to
 * bottom-left with a warning.
 * @param[in] stop Nonzero stops on detected decode errors; zero permits
 * selected recovery paths; C++ default is zero.
 * @return One for default-getter completion, zero on setup/fatal read
 * failure; tolerated corrupt data can still yield one.
 */
extern int TIFFReadRGBAImageOriented(TIFF *tif, uint32_t rwidth,
                                     uint32_t rheight, uint32_t *raster,
                                     int orientation, int stop);
#endif

    /**
     * @brief Read and convert one whole strip to packed RGBA.
     * @ingroup libtiff_public_rgba
     *
     * Requires matching organisation and boundary-aligned coordinates.
     * Initialises/ends a temporary RGBA reader. Output is bottom-to-top with
     * alpha interpretation as in TIFFRGBAImageGet(). The last strip returns
     * only rows inside ImageLength. Delegates to the Ext interface with
     * stop_on_error zero. Borrows the handle and raster; does not close the
     * handle or acquire raster ownership. May allocate conversion/raw buffers,
     * invoke tag/codec/diagnostic callbacks and perform client reads/seeks.
     * Failure can leave partial raster and changed handle/codec state; no
     * rollback or errno contract. See @ref rgba.
     *
     * @param[in,out] tif Non-NULL readable live handle; keep its current
     * directory and codec live throughout conversion and serialise access.
     * @param[in] row Zero-based row at a strip boundary; inside the image and
     * representable by int.
     * @param[out] raster Writable caller-owned ImageWidth times effective strip
     * rows packed pixels; allocation product must be representable.
     * @return One on default-getter completion, zero on detected
     * setup/coordinate/fatal read failure; tolerated decode errors can still
     * yield one.
     */
    extern int TIFFReadRGBAStrip(TIFF *tif, uint32_t row, uint32_t *raster);
    /**
     * @brief Read and convert one whole tile to packed RGBA.
     * @ingroup libtiff_public_rgba
     *
     * Requires matching organisation and boundary-aligned coordinates.
     * Initialises/ends a temporary RGBA reader. Output is bottom-to-top with
     * alpha interpretation as in TIFFRGBAImageGet(). Edge tiles shift visible
     * pixels into a full tile and zero padding, even after a failed Get; this
     * does not validate other failed output. Delegates to the Ext interface
     * with stop_on_error zero. Borrows the handle and raster; does not close
     * the handle or acquire raster ownership. May allocate conversion/raw
     * buffers, invoke tag/codec/diagnostic callbacks and perform client
     * reads/seeks. Failure can leave partial raster and changed handle/codec
     * state; no rollback or errno contract. See @ref rgba.
     *
     * @param[in,out] tif Non-NULL readable live handle; keep its current
     * directory and codec live throughout conversion and serialise access.
     * @param[in] col Zero-based pixel column at a tile boundary; inside the
     * image and representable by int.
     * @param[in] row Zero-based row at a tile boundary; inside the image and
     * representable by int.
     * @param[out] raster Writable caller-owned TileWidth times TileLength
     * packed pixels; allocation product must be representable.
     * @return One on default-getter completion, zero on detected
     * setup/coordinate/fatal read failure; tolerated decode errors can still
     * yield one.
     */
    extern int TIFFReadRGBATile(TIFF *tif, uint32_t col, uint32_t row,
                                uint32_t *raster);
    /**
     * @brief Read and convert one whole strip to packed RGBA.
     * @ingroup libtiff_public_rgba
     *
     * Requires matching organisation and boundary-aligned coordinates.
     * Initialises/ends a temporary RGBA reader. Output is bottom-to-top with
     * alpha interpretation as in TIFFRGBAImageGet(). The last strip returns
     * only rows inside ImageLength. Uses the specified error policy. Borrows
     * the handle and raster; does not close the handle or acquire raster
     * ownership. May allocate conversion/raw buffers, invoke
     * tag/codec/diagnostic callbacks and perform client reads/seeks. Failure
     * can leave partial raster and changed handle/codec state; no rollback or
     * errno contract. See @ref rgba.
     *
     * @param[in,out] tif Non-NULL readable live handle; keep its current
     * directory and codec live throughout conversion and serialise access.
     * @param[in] row Zero-based row at a strip boundary; inside the image and
     * representable by int.
     * @param[out] raster Writable caller-owned ImageWidth times effective strip
     * rows packed pixels; allocation product must be representable.
     * @param[in] stop_on_error Nonzero stops on detected decode errors; zero
     * permits selected recovery.
     * @return One on default-getter completion, zero on detected
     * setup/coordinate/fatal read failure; tolerated decode errors can still
     * yield one.
     */
    extern int TIFFReadRGBAStripExt(TIFF *tif, uint32_t row, uint32_t *raster,
                                    int stop_on_error);
    /**
     * @brief Read and convert one whole tile to packed RGBA.
     * @ingroup libtiff_public_rgba
     *
     * Requires matching organisation and boundary-aligned coordinates.
     * Initialises/ends a temporary RGBA reader. Output is bottom-to-top with
     * alpha interpretation as in TIFFRGBAImageGet(). Edge tiles shift visible
     * pixels into a full tile and zero padding, even after a failed Get; this
     * does not validate other failed output. Uses the specified error policy.
     * Borrows the handle and raster; does not close the handle or acquire
     * raster ownership. May allocate conversion/raw buffers, invoke
     * tag/codec/diagnostic callbacks and perform client reads/seeks. Failure
     * can leave partial raster and changed handle/codec state; no rollback or
     * errno contract. See @ref rgba.
     *
     * @param[in,out] tif Non-NULL readable live handle; keep its current
     * directory and codec live throughout conversion and serialise access.
     * @param[in] col Zero-based pixel column at a tile boundary; inside the
     * image and representable by int.
     * @param[in] row Zero-based row at a tile boundary; inside the image and
     * representable by int.
     * @param[out] raster Writable caller-owned TileWidth times TileLength
     * packed pixels; allocation product must be representable.
     * @param[in] stop_on_error Nonzero stops on detected decode errors; zero
     * permits selected recovery.
     * @return One on default-getter completion, zero on detected
     * setup/coordinate/fatal read failure; tolerated decode errors can still
     * yield one.
     */
    extern int TIFFReadRGBATileExt(TIFF *tif, uint32_t col, uint32_t row,
                                   uint32_t *raster, int stop_on_error);
    /**
     * @brief Check preliminary RGBA compatibility of the current directory.
     * @ingroup libtiff_public_rgba
     *
     * Examines compression availability, bit/sample formats, photometric
     * interpretation, and selected channel/layout constraints. A positive
     * result does not guarantee that Begin/Get will support the complete
     * conversion or allocate successfully. No pixel reads, allocation, or
     * conversion state. Borrows the handle without ownership transfer; tag
     * getters may invoke codec callbacks (OJPEG getters can read the file).
     * Writes emsg only on rejection; no rollback or errno contract. See
     * @ref rgba.
     *
     * @param[in,out] tif Non-NULL live handle whose current directory tags
     * are queried; tag lookup can update field caches. Serialise access.
     * @param[out] emsg Non-NULL caller-owned buffer of at least 1024 bytes;
     * reason text on rejection, contents not defined on success.
     * @return One for preliminary compatibility, zero with an explanation
     * otherwise.
     */
    extern int TIFFRGBAImageOK(TIFF *tif, char emsg[1024]);
    /**
     * @brief Initialise an RGBA reader and select conversion methods.
     * @ingroup libtiff_public_rgba
     *
     * First performs TIFFRGBAImageOK(); that early rejection leaves img
     * untouched. Thereafter initialises owned pointers, copies palettes, builds
     * tables and selects callbacks. Requires fresh state: reusing an active
     * state leaks its old allocations. Successful state must be released with
     * TIFFRGBAImageEnd() before closing tif. Later setup failures normally
     * release conversion allocations. JPEG colour mode or SGILog data-format
     * pseudo-tags can be changed and are not restored by End. The caller may
     * customise callbacks and offsets only under their documented
     * preconditions. Borrows tif, stored in img, without closing it or taking
     * ownership. Allocates conversion tables owned by img and invokes
     * tag/codec/diagnostic callbacks; codec tag getters (OJPEG) can read the
     * file, but no pixel data is read. No errno contract. See @ref rgba.
     *
     * @param[in,out] img Non-NULL caller-owned RGBA state; its borrowed handle
     * must remain live.
     * @param[in,out] tif Non-NULL readable live handle; keep its current
     * directory and codec live throughout conversion and serialise access.
     * @param[in] stop Nonzero to stop on detected decode errors; zero allows
     * some failed striles to be converted from retained/partial data.
     * @param[out] emsg Non-NULL caller-owned buffer of at least 1024 bytes;
     * reason text on rejection, contents not defined on success.
     * @return One after setup, zero on unsupported input or detected
     * setup/allocation failure; no general handle-state rollback.
     */
    extern int TIFFRGBAImageBegin(TIFFRGBAImage *img, TIFF *tif, int stop,
                                  char emsg[1024]);
    /**
     * @brief Read and convert a selected image area through installed
     * callbacks.
     * @ingroup libtiff_public_rgba
     *
     * Requires successful Begin and positive raster dimensions with a
     * representable rwidth*rheight allocation. Checks get/put availability then
     * invokes get synchronously. Default getters clip to available image
     * dimensions after nonnegative in-range offsets, reject sub-byte column
     * offsets not aligned to bytes, and place pixels at the requested raster
     * corner. Raster areas outside the copied rectangle remain unchanged.
     * Default orientation handling flips but does not transpose; orientations
     * 5..8 can warn about unavailable rotation. Output words contain associated
     * RGB in low bytes and alpha in the high byte, except that the default
     * eight-bit two-sample grayscale packer copies gray and alpha without
     * premultiplying an unassociated gray sample. Such input retains
     * unassociated colour values; see @ref rgba. Borrows the handle and
     * raster; does not close the handle or acquire raster ownership. May
     * allocate conversion/raw buffers, invoke tag/codec/diagnostic callbacks
     * and perform client reads/seeks. Failure can leave partial raster and
     * changed handle/codec state; no rollback or errno contract. See @ref rgba.
     *
     * @param[in,out] img Non-NULL caller-owned RGBA state; its borrowed handle
     * must remain live.
     * @param[out] raster Borrowed writable array of at least rwidth*rheight
     * uint32_t pixels; disjoint from state and decode buffers.
     * @param[in] rwidth Positive output row stride in pixels.
     * @param[in] rheight Positive output row count.
     * @return Installed get callback status; default getters return one on
     * completion and zero on detected fatal error, but tolerated decode errors
     * can still return one.
     */
    extern int TIFFRGBAImageGet(TIFFRGBAImage *img, uint32_t *raster,
                                uint32_t rwidth, uint32_t rheight);
    /**
     * @brief Release RGBA conversion tables and copied colormaps.
     * @ingroup libtiff_public_rgba
     *
     * Requires successfully begun state, or a state whose owned pointers and
     * tif have been initialised equivalently. Releases Ext allocations and
     * clears their pointers; does not free img, close tif, restore changed
     * codec pseudo-tags, clear callbacks, or free raster storage. Do not use
     * the released tables or decode again without a new Begin. No pixel I/O or
     * separate error status.
     *
     * @param[in,out] img Non-NULL caller-owned RGBA state; its borrowed handle
     * must remain live.
     */
    extern void TIFFRGBAImageEnd(TIFFRGBAImage *img);

    /**
     * @brief Return the handle's diagnostic filename.
     * @ingroup libtiff_public_lifecycle
     *
     * Reads stored state without performing I/O, validating the underlying
     * resource, allocating, or emitting diagnostics. Requires a live handle and
     * has no distinct failure result. Do not free or modify the returned
     * string. The initial name survives until handle cleanup; a
     * TIFFSetFileName() replacement has the caller-managed lifetime described
     * there.
     *
     * @param[in] tif Non-NULL live TIFF handle.
     * @return A borrowed filename pointer; initially it refers to the handle's
     * copied name.
     */
    extern const char *TIFFFileName(TIFF *tif);

    /**
     * @brief Replace the diagnostic filename pointer without copying it.
     * @ingroup libtiff_public_lifecycle
     *
     * Stores the supplied pointer without allocation, validation, or I/O. The
     * new null-terminated name must remain valid until replaced again or the
     * handle is released. Does not rename the OS resource. An original inline
     * name remains valid until handle cleanup; the lifetime of a previous
     * caller-supplied name remains the caller's responsibility. No diagnostics
     * or distinct failure result occur.
     *
     * @param[in,out] tif Non-NULL live TIFF handle.
     * @param[in] name Non-NULL borrowed null-terminated diagnostic name.
     * @return The previous borrowed filename pointer; it is not freed by this
     * call.
     */
    extern const char *TIFFSetFileName(TIFF *tif, const char *name);

    /**
     * @brief Dispatch a error diagnostic to the configured handlers.
     * @ingroup libtiff_public_diagnostics
     *
     * Invokes the ordinary global handler first and the extended global
     * handler second, if installed. The extended handler receives zero,
     * since this entry point has no client context. It does not dispatch a
     * per-handle callback.
     *
     * Returns no status, does not itself abort or change an operation's
     * result, and owns none of the arguments. Default handlers write to
     * stderr; user callbacks may have other effects, including changing
     * errno. Each invoked handler receives a freshly started argument list.
     * Formatting validity and callback lifetime are caller responsibilities;
     * exceptions are not caught.
     *
     * @param[in] module Borrowed module name, possibly NULL.
     * @param[in] fmt Non-NULL printf-style format string.
     * @param[in] ... Arguments matching fmt and the default variadic
     * promotions.
     */
    extern void TIFFError(const char *module, const char *fmt, ...)
        TIFF_ATTRIBUTE((__format__(__printf__, 2, 3)));

    /**
     * @brief Dispatch a error diagnostic to the configured handlers.
     * @ingroup libtiff_public_diagnostics
     *
     * Invokes the ordinary global handler first and the extended global
     * handler second, if installed. The extended handler receives fd. It
     * does not dispatch a per-handle callback.
     *
     * Returns no status, does not itself abort or change an operation's
     * result, and owns none of the arguments. Default handlers write to
     * stderr; user callbacks may have other effects, including changing
     * errno. Each invoked handler receives a freshly started argument list.
     * Formatting validity and callback lifetime are caller responsibilities;
     * exceptions are not caught.
     *
     * @param[in] fd Opaque client context passed to the extended global
     * handler.
     * @param[in] module Borrowed module name, possibly NULL.
     * @param[in] fmt Non-NULL printf-style format string.
     * @param[in] ... Arguments matching fmt and the default variadic
     * promotions.
     */
    extern void TIFFErrorExt(thandle_t fd, const char *module, const char *fmt,
                             ...)
        TIFF_ATTRIBUTE((__format__(__printf__, 3, 4)));

    /**
     * @brief Dispatch a warning diagnostic to the configured handlers.
     * @ingroup libtiff_public_diagnostics
     *
     * Invokes the ordinary global handler first and the extended global
     * handler second, if installed. The extended handler receives zero,
     * since this entry point has no client context. It does not dispatch a
     * per-handle callback.
     *
     * Returns no status, does not itself abort or change an operation's
     * result, and owns none of the arguments. Default handlers write to
     * stderr; user callbacks may have other effects, including changing
     * errno. Each invoked handler receives a freshly started argument list.
     * Formatting validity and callback lifetime are caller responsibilities;
     * exceptions are not caught.
     *
     * @param[in] module Borrowed module name, possibly NULL.
     * @param[in] fmt Non-NULL printf-style format string.
     * @param[in] ... Arguments matching fmt and the default variadic
     * promotions.
     */
    extern void TIFFWarning(const char *module, const char *fmt, ...)
        TIFF_ATTRIBUTE((__format__(__printf__, 2, 3)));

    /**
     * @brief Dispatch a warning diagnostic to the configured handlers.
     * @ingroup libtiff_public_diagnostics
     *
     * Invokes the ordinary global handler first and the extended global
     * handler second, if installed. The extended handler receives fd. It
     * does not dispatch a per-handle callback.
     *
     * Returns no status, does not itself abort or change an operation's
     * result, and owns none of the arguments. Default handlers write to
     * stderr; user callbacks may have other effects, including changing
     * errno. Each invoked handler receives a freshly started argument list.
     * Formatting validity and callback lifetime are caller responsibilities;
     * exceptions are not caught.
     *
     * @param[in] fd Opaque client context passed to the extended global
     * handler.
     * @param[in] module Borrowed module name, possibly NULL.
     * @param[in] fmt Non-NULL printf-style format string.
     * @param[in] ... Arguments matching fmt and the default variadic
     * promotions.
     */
    extern void TIFFWarningExt(thandle_t fd, const char *module,
                               const char *fmt, ...)
        TIFF_ATTRIBUTE((__format__(__printf__, 3, 4)));

    /**
     * @brief Replace the library-wide error handler.
     * @ingroup libtiff_public_diagnostics
     *
     * Changes one global callback pointer without allocating, retaining
     * code/context, or emitting a diagnostic. Does not change the other
     * global slot or per-handle callbacks. NULL suppresses only this slot,
     * not every diagnostic path. Coordinate concurrent registration and in-
     * flight calls externally; no locking or lifetime management is
     * provided.
     *
     * @param[in] handler Replacement function pointer, or NULL to disable
     * this handler slot.
     * @return The previous function pointer, possibly NULL, without
     * ownership transfer.
     */
    extern TIFFErrorHandler TIFFSetErrorHandler(TIFFErrorHandler handler);

    /**
     * @brief Replace the library-wide error extended handler.
     * @ingroup libtiff_public_diagnostics
     *
     * Changes one global callback pointer without allocating, retaining
     * code/context, or emitting a diagnostic. Does not change the other
     * global slot or per-handle callbacks. NULL suppresses only this slot,
     * not every diagnostic path. Coordinate concurrent registration and in-
     * flight calls externally; no locking or lifetime management is
     * provided.
     *
     * @param[in] handler Replacement function pointer, or NULL to disable
     * this handler slot.
     * @return The previous function pointer, possibly NULL, without
     * ownership transfer.
     */
    extern TIFFErrorHandlerExt
    TIFFSetErrorHandlerExt(TIFFErrorHandlerExt handler);

    /**
     * @brief Replace the library-wide warning handler.
     * @ingroup libtiff_public_diagnostics
     *
     * Changes one global callback pointer without allocating, retaining
     * code/context, or emitting a diagnostic. Does not change the other
     * global slot or per-handle callbacks. NULL suppresses only this slot,
     * not every diagnostic path. Coordinate concurrent registration and in-
     * flight calls externally; no locking or lifetime management is
     * provided.
     *
     * @param[in] handler Replacement function pointer, or NULL to disable
     * this handler slot.
     * @return The previous function pointer, possibly NULL, without
     * ownership transfer.
     */
    extern TIFFErrorHandler TIFFSetWarningHandler(TIFFErrorHandler handler);

    /**
     * @brief Replace the library-wide warning extended handler.
     * @ingroup libtiff_public_diagnostics
     *
     * Changes one global callback pointer without allocating, retaining
     * code/context, or emitting a diagnostic. Does not change the other
     * global slot or per-handle callbacks. NULL suppresses only this slot,
     * not every diagnostic path. Coordinate concurrent registration and in-
     * flight calls externally; no locking or lifetime management is
     * provided.
     *
     * @param[in] handler Replacement function pointer, or NULL to disable
     * this handler slot.
     * @return The previous function pointer, possibly NULL, without
     * ownership transfer.
     */
    extern TIFFErrorHandlerExt
    TIFFSetWarningHandlerExt(TIFFErrorHandlerExt handler);

    /**
     * @brief Dispatch a warning diagnostic to the configured handlers.
     * @ingroup libtiff_public_diagnostics
     *
     * First invokes the per-handle warning callback when tif is non-NULL and
     * that slot is installed. A nonzero result suppresses both global
     * handlers for this message; otherwise the ordinary global handler and
     * then the extended global handler run if installed. The latter receives
     * tif's client context, or zero for a NULL tif.
     *
     * Returns no status, does not itself abort or change an operation's
     * result, and owns none of the arguments. Default handlers write to
     * stderr; user callbacks may have other effects, including changing
     * errno. Each invoked handler receives a freshly started argument list.
     * Formatting validity and callback lifetime are caller responsibilities;
     * exceptions are not caught.
     *
     * @param[in] tif Invoking live handle, or NULL to use only global
     * handlers.
     * @param[in] module Borrowed module name, possibly NULL.
     * @param[in] fmt Non-NULL printf-style format string.
     * @param[in] ... Arguments matching fmt and the default variadic
     * promotions.
     */
    extern void TIFFWarningExtR(TIFF *tif, const char *module, const char *fmt,
                                ...)
        TIFF_ATTRIBUTE((__format__(__printf__, 3, 4)));

    /**
     * @brief Dispatch a error diagnostic to the configured handlers.
     * @ingroup libtiff_public_diagnostics
     *
     * First invokes the per-handle error callback when tif is non-NULL and
     * that slot is installed. A nonzero result suppresses both global
     * handlers for this message; otherwise the ordinary global handler and
     * then the extended global handler run if installed. The latter receives
     * tif's client context, or zero for a NULL tif.
     *
     * Returns no status, does not itself abort or change an operation's
     * result, and owns none of the arguments. Default handlers write to
     * stderr; user callbacks may have other effects, including changing
     * errno. Each invoked handler receives a freshly started argument list.
     * Formatting validity and callback lifetime are caller responsibilities;
     * exceptions are not caught.
     *
     * @param[in] tif Invoking live handle, or NULL to use only global
     * handlers.
     * @param[in] module Borrowed module name, possibly NULL.
     * @param[in] fmt Non-NULL printf-style format string.
     * @param[in] ... Arguments matching fmt and the default variadic
     * promotions.
     */
    extern void TIFFErrorExtR(TIFF *tif, const char *module, const char *fmt,
                              ...)
        TIFF_ATTRIBUTE((__format__(__printf__, 3, 4)));

    /**
     * @brief Refer to opaque settings used when opening a TIFF handle.
     * @ingroup libtiff_public_data
     *
     * Allocate and release through the open-options API; callback contexts
     * remain client-owned and must outlive their use by the resulting handle.
     */
    typedef struct TIFFOpenOptions TIFFOpenOptions;

    /**
     * @brief Allocate default options for opening a TIFF handle.
     * @ingroup libtiff_public_open_options
     *
     * The object starts with no per-handle diagnostic callbacks, no allocation
     * limits, and unknown-tag warnings disabled. Allocation failure is reported
     * only by NULL; this function does not emit a library diagnostic. The
     * object has no owned callback-context storage.
     *
     * @return An owned options object, or NULL if allocation fails. Release it
     * with TIFFOpenOptionsFree().
     */
    extern TIFFOpenOptions *TIFFOpenOptionsAlloc(void);

    /**
     * @brief Release an open-options object.
     * @ingroup libtiff_public_open_options
     *
     * Invalidates opts without freeing callback contexts or affecting settings
     * already copied into an open handle. NULL is accepted. There is no status
     * result or library diagnostic. Any callback contexts copied into a handle
     * must remain alive until their last invocation, including during close.
     *
     * @param[in] opts Options returned by TIFFOpenOptionsAlloc(), or NULL.
     */
    extern void TIFFOpenOptionsFree(TIFFOpenOptions *opts);

    /**
     * @brief Set the single checked allocation limit for subsequent opens.
     * @ingroup libtiff_public_open_options
     *
     * Stores the value for subsequent opens; already-open handles are
     * unchanged. Zero is the default and means unlimited. Negative values are
     * stored without validation and do not activate the positive-limit checks.
     * No allocation or diagnostic occurs and no status is returned.
     *
     * The positive limit is applied to the initial handle/name allocation and
     * requests through the handle-aware allocation helpers. It does not limit
     * direct calls to _TIFFmalloc() or _TIFFrealloc(), caller storage, or
     * allocations outside those checked paths.
     *
     * @param[in,out] opts Non-NULL open-options object.
     * @param[in] max_single_mem_alloc Byte limit; positive enables checking,
     * zero leaves it unlimited.
     */
    extern void
    TIFFOpenOptionsSetMaxSingleMemAlloc(TIFFOpenOptions *opts,
                                        tmsize_t max_single_mem_alloc);

    /**
     * @brief Set the cumulative checked allocation limit for subsequent opens.
     * @ingroup libtiff_public_open_options
     *
     * Stores the value for subsequent opens; already-open handles are
     * unchanged. Zero is the default and means unlimited. Negative values are
     * stored without validation and do not activate the positive-limit checks.
     * No allocation or diagnostic occurs and no status is returned.
     *
     * The positive limit applies to the total tracked by the handle-aware
     * allocation helpers, not total process memory. The initial handle/name
     * allocation is checked against this limit separately, but is not added to
     * that tracked total. Direct _TIFFmalloc()/_TIFFrealloc() calls, mappings,
     * caller storage, and other untracked allocations are not covered.
     *
     * @param[in,out] opts Non-NULL open-options object.
     * @param[in] max_cumulated_mem_alloc Byte limit; positive enables checking,
     * zero leaves it unlimited.
     */
    extern void
    TIFFOpenOptionsSetMaxCumulatedMemAlloc(TIFFOpenOptions *opts,
                                           tmsize_t max_cumulated_mem_alloc);

    /**
     * @brief Select whether subsequent opens warn about unknown tags.
     * @ingroup libtiff_public_open_options
     *
     * Stores the value without allocating, emitting diagnostics, or changing
     * existing handles. There is no status result. The default is zero since
     * 4.7.1; this setting controls unknown-tag warnings rather than making
     * unknown tags valid or invalid.
     *
     * @param[in,out] opts Non-NULL open-options object.
     * @param[in] warn_about_unknown_tags Zero disables warnings; nonzero
     * enables them.
     */
    extern void
    TIFFOpenOptionsSetWarnAboutUnknownTags(TIFFOpenOptions *opts,
                                           int warn_about_unknown_tags);

    /**
     * @brief Set the per-handle error callback for subsequent opens.
     * @ingroup libtiff_public_open_options
     *
     * Stores the callback and borrowed context for subsequent opens; existing
     * handles are unchanged. NULL removes the per-handle callback from these
     * options. There is no allocation, context copy, diagnostic, or status
     * result.
     *
     * Opening copies the function and context pointers, so the options may then
     * be freed while the context remains valid through the last callback on the
     * resulting handle. The callback is synchronous and may run during opening
     * and closing. Nonzero callback return suppresses the legacy global
     * handlers for that message, not the underlying operation. Early opening
     * errors can invoke the handler with a NULL TIFF pointer; no callback may
     * assume that a fully initialised handle exists.
     *
     * @param[in,out] opts Non-NULL open-options object.
     * @param[in] handler Callback to store, or NULL for the normal global-
     * handler path.
     * @param[in] errorhandler_user_data Borrowed context, possibly NULL, passed
     * to the callback.
     */
    extern void
    TIFFOpenOptionsSetErrorHandlerExtR(TIFFOpenOptions *opts,
                                       TIFFErrorHandlerExtR handler,
                                       void *errorhandler_user_data);

    /**
     * @brief Set the per-handle warning callback for subsequent opens.
     * @ingroup libtiff_public_open_options
     *
     * Stores the callback and borrowed context for subsequent opens; existing
     * handles are unchanged. NULL removes the per-handle callback from these
     * options. There is no allocation, context copy, diagnostic, or status
     * result.
     *
     * Opening copies the function and context pointers, so the options may then
     * be freed while the context remains valid through the last callback on the
     * resulting handle. The callback is synchronous and may run during opening
     * and closing. Nonzero callback return suppresses the legacy global
     * handlers for that message, not the underlying operation. The callback
     * follows the TIFFErrorHandlerExtR signature; its return value does not
     * change operation success.
     *
     * @param[in,out] opts Non-NULL open-options object.
     * @param[in] handler Callback to store, or NULL for the normal global-
     * handler path.
     * @param[in] warnhandler_user_data Borrowed context, possibly NULL, passed
     * to the callback.
     */
    extern void
    TIFFOpenOptionsSetWarningHandlerExtR(TIFFOpenOptions *opts,
                                         TIFFErrorHandlerExtR handler,
                                         void *warnhandler_user_data);

    /**
     * @brief Open a TIFF file by pathname.
     * @ingroup libtiff_public_lifecycle
     *
     * Opens the OS resource, then initialises a TIFF handle. Pathname mode w
     * truncates an existing file, a may create one, and r/r+ require an
     * existing resource. Opening may read a directory, map the file, or write a
     * header; failures do not roll back file changes.
     *
     * Equivalent to TIFFOpenExt(name, mode, NULL). Reports ordinary
     * opening/initialisation failures through the configured error handlers.
     * errno/GetLastError may reflect a platform failure but are not a complete
     * library-error channel. See @ref lifecycle for mode and lifetime details.
     *
     * @param[in] name Non-NULL, null-terminated pathname interpreted by the
     * selected platform backend.
     * @param[in] mode Non-NULL, null-terminated open mode; see @ref
     * lifecycle_modes.
     * @return An owned TIFF handle to release with TIFFClose(), or NULL on
     * failure. On normal failure paths the internally opened resource is
     * closed.
     */
    extern TIFF *TIFFOpen(const char *name, const char *mode);

    /**
     * @brief Open a TIFF file by pathname.
     * @ingroup libtiff_public_lifecycle
     *
     * Opens the OS resource, then initialises a TIFF handle. Pathname mode w
     * truncates an existing file, a may create one, and r/r+ require an
     * existing resource. Opening may read a directory, map the file, or write a
     * header; failures do not roll back file changes.
     *
     * Uses the supplied options. Diagnostic callbacks may run before a handle
     * exists; per-handle callback contexts remain borrowed through close.
     * Reports ordinary opening/initialisation failures through the configured
     * error handlers. errno/GetLastError may reflect a platform failure but are
     * not a complete library-error channel. See @ref lifecycle for mode and
     * lifetime details.
     *
     * @param[in] name Non-NULL, null-terminated pathname interpreted by the
     * selected platform backend.
     * @param[in] mode Non-NULL, null-terminated open mode; see @ref
     * lifecycle_modes.
     * @param[in] opts Borrowed open options, or NULL for defaults; copied
     * before return.
     * @return An owned TIFF handle to release with TIFFClose(), or NULL on
     * failure. On normal failure paths the internally opened resource is
     * closed.
     */
    extern TIFF *TIFFOpenExt(const char *name, const char *mode,
                             TIFFOpenOptions *opts);
#ifdef _WIN32
    /*
     * TIFFOpenW() and TIFFOpenWExt() are documented with their definitions in
     * tif_win32.c: the reference is not generated for Windows, so these
     * Windows-only declarations are not visible to Doxygen.
     */
    extern TIFF *TIFFOpenW(const wchar_t *name, const char *mode);

    extern TIFF *TIFFOpenWExt(const wchar_t *name, const char *mode,
                              TIFFOpenOptions *opts);
#endif /* _WIN32 */

    /**
     * @brief Attach a TIFF handle to an already-open platform resource.
     * @ingroup libtiff_public_lifecycle
     *
     * Wraps an existing resource without opening it or physically truncating
     * it. The resource must support seek/read and, for writable modes, write.
     * Initialise its position at the beginning of the TIFF data. Mode w writes
     * a fresh header; truncate the resource yourself if physical truncation is
     * required.
     *
     * Equivalent to TIFFFdOpenExt(fd, name, mode, NULL). On success TIFFClose()
     * invokes the backend close callback. On a NULL return the resource stays
     * the caller's responsibility; its position and content may already have
     * changed. Diagnostics follow the configured handlers. The POSIX/CRT
     * backend expects a file descriptor; the native Windows backend expects its
     * integer representation of a Windows HANDLE, not a CRT descriptor.
     *
     * @param[in] fd Already-open resource in the backend's descriptor
     * convention; see @ref lifecycle_backends.
     * @param[in] name Non-NULL diagnostic name; copied into the new handle.
     * @param[in] mode Non-NULL open mode consistent with the resource access
     * permissions.
     * @return An owned TIFF handle, or NULL on failure. Close a successful
     * handle with TIFFClose(); on failure close the resource yourself as
     * appropriate.
     */
    extern TIFF *TIFFFdOpen(int fd, const char *name, const char *mode);

    /**
     * @brief Attach a TIFF handle to an already-open platform resource.
     * @ingroup libtiff_public_lifecycle
     *
     * Wraps an existing resource without opening it or physically truncating
     * it. The resource must support seek/read and, for writable modes, write.
     * Initialise its position at the beginning of the TIFF data. Mode w writes
     * a fresh header; truncate the resource yourself if physical truncation is
     * required.
     *
     * Uses the supplied options. On success TIFFClose() invokes the backend
     * close callback. On a NULL return the resource stays the caller's
     * responsibility; its position and content may already have changed.
     * Diagnostics follow the configured handlers. The POSIX/CRT backend expects
     * a file descriptor; the native Windows backend expects its integer
     * representation of a Windows HANDLE, not a CRT descriptor.
     *
     * @param[in] fd Already-open resource in the backend's descriptor
     * convention; see @ref lifecycle_backends.
     * @param[in] name Non-NULL diagnostic name; copied into the new handle.
     * @param[in] mode Non-NULL open mode consistent with the resource access
     * permissions.
     * @param[in] opts Borrowed options copied into the handle, or NULL for
     * defaults.
     * @return An owned TIFF handle, or NULL on failure. Close a successful
     * handle with TIFFClose(); on failure close the resource yourself as
     * appropriate.
     */
    extern TIFF *TIFFFdOpenExt(int fd, const char *name, const char *mode,
                               TIFFOpenOptions *opts);

    /**
     * @brief Initialise a TIFF handle using client-supplied I/O callbacks.
     * @ingroup libtiff_public_lifecycle
     *
     * Initialises a handle over caller-supplied I/O without opening or
     * truncating the underlying resource. Start with the I/O position at TIFF
     * offset zero. Opening can invoke read/seek/size/map callbacks, read the
     * initial directory, and write a new header for writable modes. A short
     * initial header read in a writable mode can initialise a fresh header; do
     * not expect file-content or position rollback on failure.
     *
     * Equivalent to TIFFClientOpenExt() with opts = NULL. All five non-mapping
     * callbacks must be supplied. Mapping/unmapping are independently optional,
     * but a successful mapping must have a matching unmapper if its resources
     * require release. The callbacks and their context must remain valid
     * through the last call, including TIFFClose(). The name and mode strings
     * need not remain alive after return.
     *
     * A successful TIFFClose() path invokes closeproc after releasing library
     * state. TIFFCleanup() alone never invokes closeproc. A NULL return does
     * not invoke closeproc: the caller retains responsibility for the client
     * resource/context, though a mapped view may already have been unmapped
     * during failure cleanup. Error handlers can run before or during
     * initialisation; early per-handle errors can receive a NULL TIFF pointer.
     *
     * @param[in] name Non-NULL diagnostic name; copied into the handle.
     * @param[in] mode Non-NULL open mode; see @ref lifecycle_modes.
     * @param[in,out] clientdata Opaque client I/O context; passed unchanged to
     * the callbacks.
     * @param[in] readproc Non-NULL read callback, even for a write-only
     * application.
     * @param[in] writeproc Non-NULL write callback, even for read-only mode.
     * @param[in] seekproc Non-NULL seek callback.
     * @param[in] closeproc Non-NULL resource/adapter release callback.
     * @param[in] sizeproc Non-NULL file-size callback.
     * @param[in] mapproc Optional mapping callback; NULL installs a failing no-
     * op mapper.
     * @param[in] unmapproc Optional unmapping callback; NULL installs a no-op
     * unmapper.
     * @return An owned TIFF handle, or NULL on failure. Use TIFFClose() for
     * complete normal cleanup; client resource cleanup after NULL remains the
     * caller's responsibility.
     */
    extern TIFF *TIFFClientOpen(const char *name, const char *mode,
                                thandle_t clientdata,
                                TIFFReadWriteProc readproc,
                                TIFFReadWriteProc writeproc,
                                TIFFSeekProc seekproc, TIFFCloseProc closeproc,
                                TIFFSizeProc sizeproc, TIFFMapFileProc mapproc,
                                TIFFUnmapFileProc unmapproc);

    /**
     * @brief Initialise a TIFF handle using client-supplied I/O callbacks.
     * @ingroup libtiff_public_lifecycle
     *
     * Initialises a handle over caller-supplied I/O without opening or
     * truncating the underlying resource. Start with the I/O position at TIFF
     * offset zero. Opening can invoke read/seek/size/map callbacks, read the
     * initial directory, and write a new header for writable modes. A short
     * initial header read in a writable mode can initialise a fresh header; do
     * not expect file-content or position rollback on failure.
     *
     * Copies options but borrows callback context pointers. All five
     * non-mapping callbacks must be supplied. Mapping/unmapping are
     * independently optional, but a successful mapping must have a matching
     * unmapper if its resources require release. The callbacks and their
     * context must remain valid through the last call, including TIFFClose().
     * The name and mode strings need not remain alive after return.
     *
     * A successful TIFFClose() path invokes closeproc after releasing library
     * state. TIFFCleanup() alone never invokes closeproc. A NULL return does
     * not invoke closeproc: the caller retains responsibility for the client
     * resource/context, though a mapped view may already have been unmapped
     * during failure cleanup. Error handlers can run before or during
     * initialisation; early per-handle errors can receive a NULL TIFF pointer.
     *
     * @param[in] name Non-NULL diagnostic name; copied into the handle.
     * @param[in] mode Non-NULL open mode; see @ref lifecycle_modes.
     * @param[in,out] clientdata Opaque client I/O context; passed unchanged to
     * the callbacks.
     * @param[in] readproc Non-NULL read callback, even for a write-only
     * application.
     * @param[in] writeproc Non-NULL write callback, even for read-only mode.
     * @param[in] seekproc Non-NULL seek callback.
     * @param[in] closeproc Non-NULL resource/adapter release callback.
     * @param[in] sizeproc Non-NULL file-size callback.
     * @param[in] mapproc Optional mapping callback; NULL installs a failing no-
     * op mapper.
     * @param[in] unmapproc Optional unmapping callback; NULL installs a no-op
     * unmapper.
     * @param[in] opts Borrowed options copied before return, or NULL for
     * defaults.
     * @return An owned TIFF handle, or NULL on failure. Use TIFFClose() for
     * complete normal cleanup; client resource cleanup after NULL remains the
     * caller's responsibility.
     */
    extern TIFF *
    TIFFClientOpenExt(const char *name, const char *mode, thandle_t clientdata,
                      TIFFReadWriteProc readproc, TIFFReadWriteProc writeproc,
                      TIFFSeekProc seekproc, TIFFCloseProc closeproc,
                      TIFFSizeProc sizeproc, TIFFMapFileProc mapproc,
                      TIFFUnmapFileProc unmapproc, TIFFOpenOptions *opts);
    /**
     * @brief Replace the process-wide image-directory extension callback.
     * @ingroup libtiff_public_fields
     *
     * Affects future default-directory initialisations, including those on
     * existing handles, rather than invoking the hook immediately. NULL
     * disables it. To coexist with other extenders, save and chain the returned
     * callback. Callback code is borrowed; no allocation, validation,
     * diagnostic, or locking occurs. Coordinate replacement with all users and
     * in-flight initialisations.
     *
     * @param[in] extender New callback, or NULL; must remain callable while
     * installed/in flight.
     * @return Previous callback, possibly NULL; no ownership transfer.
     */
    extern TIFFExtendProc TIFFSetTagExtender(TIFFExtendProc extender);
    /**
     * @brief Compute a tile index from pixel and sample coordinates.
     * @ingroup libtiff_public_image_layout
     *
     * Substitutes corresponding image extents for UINT32_MAX tile dimensions,
     * then computes x-major, y, z, and separate-plane offsets with checked
     * products/additions. Does not validate or clamp x/y/z to the image; forces
     * z to zero when ImageDepth is one. Checks s only for separate storage.
     * Zero effective tile dimensions return the initial value one silently;
     * arithmetic/sample rejection returns zero, which is also the first valid
     * tile. Conservative ceiling guards can reject large extents. Does not
     * allocate, perform I/O, acquire ownership, or validate tile arrays. Use
     * TIFFCheckTile() for coordinate bounds and establish valid tile geometry
     * separately.
     *
     * @param[in] tif Non-NULL live handle with consistent current geometry; no
     * ownership transfer.
     * @param[in] x Pixel column; not checked against ImageWidth.
     * @param[in] y Pixel row; not checked against ImageLength.
     * @param[in] z Depth slice; forced to zero when ImageDepth is one.
     * @param[in] s Separate sample plane; ignored for contiguous storage.
     * @return Computed zero-based tile index, zero on detected
     * arithmetic/sample rejection, or one for zero effective tile dimensions;
     * not proof of a valid tile.
     */
    extern uint32_t TIFFComputeTile(TIFF *tif, uint32_t x, uint32_t y,
                                    uint32_t z, uint16_t s);
    /**
     * @brief Check pixel and separate-sample coordinates against image bounds.
     * @ingroup libtiff_public_image_layout
     *
     * Checks x<ImageWidth, y<ImageLength, z<ImageDepth, and s<SamplesPerPixel
     * only for separate planes. Unlike TIFFComputeTile(), it does not discard z
     * when ImageDepth is one. On first invalid coordinate dispatches an error
     * and returns zero. Does not validate TileWidth/Length/Depth, organisation,
     * count arithmetic, array capacity, or codec support. Does not allocate,
     * perform I/O, or transfer ownership.
     *
     * @param[in] tif Non-NULL live handle with consistent current geometry; no
     * ownership transfer.
     * @param[in] x Pixel column to check against ImageWidth.
     * @param[in] y Pixel row to check against ImageLength.
     * @param[in] z Depth slice to check against ImageDepth; not ignored when
     * ImageDepth is one.
     * @param[in] s Sample plane checked against SamplesPerPixel only for
     * separate planar configuration.
     * @return One if all relevant coordinates are in image bounds, zero
     * otherwise; one alone does not establish valid tile geometry.
     */
    extern int TIFFCheckTile(TIFF *tif, uint32_t x, uint32_t y, uint32_t z,
                             uint16_t s);
    /**
     * @brief Compute the tile count across image depth and sample planes.
     * @ingroup libtiff_public_image_layout
     *
     * Substitutes image extents for UINT32_MAX tile dimensions, takes ceiling
     * counts in x/y/z, then multiplies, including SamplesPerPixel for separate
     * planes. Zero effective tile dimensions return zero. Products use checked
     * 32-bit arithmetic with diagnostics; conservative ceiling guards can
     * return zero silently near UINT32_MAX. No organisation/array-capacity
     * validation, allocation, I/O, or ownership transfer occurs.
     *
     * @param[in] tif Non-NULL live handle with consistent current geometry; no
     * ownership transfer.
     * @return Geometry-derived total including depth and separate planes, or
     * zero for empty/invalid/rejected arithmetic; not the cached array
     * capacity.
     */
    extern uint32_t TIFFNumberOfTiles(TIFF *tif);
    /**
     * @brief Decode the tile containing image coordinates.
     * @ingroup libtiff_public_image_reading
     *
     * Checks coordinate bounds, computes the tile index, then delegates to
     * TIFFReadEncodedTile() with the full-tile sentinel. The buffer must cover
     * the full decoded tile including edge padding, not just visible pixels.
     * Output is valid only on success. May allocate or replace handle-owned
     * raw/strile buffers, load deferred metadata, seek and read client I/O, and
     * invoke codec/tag/diagnostic callbacks; reading never writes to the file.
     * No handle or caller-buffer ownership transfers. Failure is not
     * transactional: buffers, codec progress, indices, flags, metadata, or file
     * position can already have changed; disregard partial decoded output
     * unless a codec explicitly documents otherwise. No errno or rollback
     * guarantee. See @ref image_transfers.
     *
     * Codec decode callbacks are tested for nonzero here; negative callback
     * errors can therefore be accepted as completion (including fax errors).
     * A positive public status can accompany error diagnostics.
     *
     * @param[in,out] tif Non-NULL live handle with matching current geometry
     * and codec methods; serialise access externally.
     * @param[out] buf Caller-owned writable output of at least TIFFTileSize()
     * bytes.
     * @param[in] x Pixel column inside the image.
     * @param[in] y Pixel row inside the image.
     * @param[in] z Depth slice inside the image.
     * @param[in] s Separate sample plane, zero for ordinary contiguous use.
     * @return Full effective decoded/input tile byte count on success, minus
     * one on detected coordinate/setup/codec/I/O failure.
     */
    extern tmsize_t TIFFReadTile(TIFF *tif, void *buf, uint32_t x, uint32_t y,
                                 uint32_t z, uint16_t s);
    /**
     * @brief Encode the tile containing image coordinates.
     * @ingroup libtiff_public_image_writing
     *
     * Checks coordinate bounds, computes the tile index, then delegates to
     * TIFFWriteEncodedTile() with the full-tile sentinel. The buffer must cover
     * the full decoded tile including edge padding, not just visible pixels.
     * Input can be changed by swapping/bit reversal. May allocate or replace
     * handle-owned raw/strile buffers, load deferred metadata, seek and write
     * client I/O, and invoke codec/tag/diagnostic callbacks. No handle or
     * caller-buffer ownership transfers. Failure is not transactional: buffers,
     * codec progress, indices, flags, metadata, file position, or file bytes
     * can already have changed, and data already written is not removed. No
     * errno, rollback, or durability guarantee. See @ref image_transfers.
     *
     * The delegated TIFFWriteEncodedTile() tests encode callbacks for
     * nonzero; negative callback errors, such as the fallback encoder used for
     * unknown schemes, can therefore be accepted as success. A positive public
     * status can accompany error diagnostics with no data stored.
     *
     * @param[in,out] tif Non-NULL live handle with matching current geometry
     * and codec methods; serialise access externally.
     * @param[in,out] buf Caller-owned full decoded tile of TIFFTileSize()
     * bytes; may be byte-swapped or bit-reversed in place before encoding.
     * @param[in] x Pixel column inside the image.
     * @param[in] y Pixel row inside the image.
     * @param[in] z Depth slice inside the image.
     * @param[in] s Separate sample plane, zero for ordinary contiguous use.
     * @return Full effective decoded/input tile byte count on success, minus
     * one on detected coordinate/setup/codec/I/O failure.
     */
    extern tmsize_t TIFFWriteTile(TIFF *tif, void *buf, uint32_t x, uint32_t y,
                                  uint32_t z, uint16_t s);
    /**
     * @brief Compute a strip index from a row and optional sample plane.
     * @ingroup libtiff_public_image_layout
     *
     * Uses row/RowsPerStrip and, for separate planes, the cached
     * td_stripsperimage times sample. Requires nonzero RowsPerStrip and a
     * consistent cached strips-per-plane count. Does not validate or clamp row
     * to ImageLength, or verify strip arrays/organisation. sample is checked
     * only for separate planes and ignored otherwise. Invalid separate sample,
     * zero RowsPerStrip, or checked arithmetic/narrowing failures report
     * diagnostics and return zero; a valid first strip is also zero. No
     * allocation, I/O, or ownership effects.
     *
     * @param[in] tif Non-NULL live handle with consistent current geometry; no
     * ownership transfer.
     * @param[in] row Row coordinate; caller must ensure it is inside the image.
     * @param[in] sample Sample plane for separate storage; ignored for
     * contiguous storage.
     * @return Computed zero-based index, or zero on rejection; not a
     * coordinate-validation result.
     */
    extern uint32_t TIFFComputeStrip(TIFF *tif, uint32_t row, uint16_t sample);
    /**
     * @brief Compute the strip count from current image geometry.
     * @ingroup libtiff_public_image_layout
     *
     * Uses ceil(ImageLength/RowsPerStrip), multiplying by SamplesPerPixel for
     * separate planes. RowsPerStrip UINT32_MAX produces one strip per plane
     * even for zero ImageLength. RowsPerStrip zero warns and returns zero. The
     * conservative 32-bit ceiling guard can return zero near UINT32_MAX without
     * a direct diagnostic; multiplication overflow is diagnosed. Does not
     * include ImageDepth, check strip-versus-tile organisation, allocate
     * arrays, or return their cached capacity. No I/O or ownership effects.
     *
     * @param[in] tif Non-NULL live handle with consistent current geometry; no
     * ownership transfer.
     * @return Geometry-derived total including separate planes, or zero for
     * empty/invalid/rejected arithmetic; not proof of allocated strile-array
     * length.
     */
    extern uint32_t TIFFNumberOfStrips(TIFF *tif);
    /**
     * @brief Read decoded samples from one strip.
     * @ingroup libtiff_public_image_reading
     *
     * Decodes host-order samples, including postdecode conversion and
     * configured fill order. Minus one requests the complete decoded strile; a
     * nonnegative size limits the requested output, clipped to the decoded
     * extent (the last strip can be shorter). Zero requests are not uniform
     * success paths; prefer a positive count. Negative values other than minus
     * one violate the buffer-extent contract. An uncompressed direct-read
     * shortcut bypasses decoder state transitions. Fill failure may zero the
     * requested output; other failure paths need not. May allocate or replace
     * handle-owned raw/strile buffers, load deferred metadata, seek and read
     * client I/O, and invoke codec/tag/diagnostic callbacks; reading never
     * writes to the file. No handle or caller-buffer ownership transfers.
     * Failure is not transactional: buffers, codec progress, indices, flags,
     * metadata, or file position can already have changed; disregard partial
     * decoded output unless a codec explicitly documents otherwise. No errno or
     * rollback guarantee. See @ref image_transfers.
     *
     * @param[in,out] tif Non-NULL live handle with matching current geometry
     * and codec methods; serialise access externally.
     * @param[in] strip Zero-based strile index including separate sample
     * planes.
     * @param[out] buf Caller-owned writable output with capacity for the
     * effective byte count; do not alias mutable handle raw storage.
     * @param[in] size Minus one for full extent, otherwise a nonnegative
     * decoded limit.
     * @return Effective requested byte count on success, minus one on detected
     * failure; not an ownership result.
     */
    extern tmsize_t TIFFReadEncodedStrip(TIFF *tif, uint32_t strip, void *buf,
                                         tmsize_t size);
    /**
     * @brief Read stored bytes from one strip.
     * @ingroup libtiff_public_image_reading
     *
     * Copies stored bytes without decoding, postdecode sample swapping, or
     * fill-order reversal. Minus one requests the whole stored byte count; a
     * positive size limits it to the available metadata count. Zero effective
     * size fails. Requires raw access supported by the codec, valid index,
     * matching organisation, and readable mode. Payload range is read/mapped
     * and exact short reads fail; partial destination contents may remain.
     * Loads deferred strile metadata when needed, then seeks and reads client
     * I/O or copies from the file mapping; invokes no codec callbacks and
     * never writes to the file. Allocates no intermediate pixel-data buffer,
     * but deferred strile metadata queries can allocate or replace
     * handle-owned offset/count arrays, with the allocation-failure and
     * borrowed-view effects described for TIFFGetStrileOffset(). On failure
     * the file position may have moved. No errno or rollback guarantee. See
     * @ref image_transfers.
     *
     * @param[in,out] tif Non-NULL live handle with matching current geometry
     * and codec methods; serialise access externally.
     * @param[in] strip Zero-based strile index including separate sample
     * planes.
     * @param[out] buf Caller-owned writable output with capacity for the
     * effective byte count; do not alias mutable handle raw storage.
     * @param[in] size Minus one for all stored bytes, otherwise a positive raw
     * byte limit.
     * @return Effective requested byte count on success, minus one on detected
     * failure; not an ownership result.
     */
    extern tmsize_t TIFFReadRawStrip(TIFF *tif, uint32_t strip, void *buf,
                                     tmsize_t size);
    /**
     * @brief Read decoded samples from one tile.
     * @ingroup libtiff_public_image_reading
     *
     * Decodes host-order samples, including postdecode conversion and
     * configured fill order. Minus one requests the complete decoded tile; a
     * nonnegative size limits the requested output, clipped to the full
     * decoded tile size (edge tiles include padding and are not shortened).
     * Zero requests are not uniform success paths; prefer a positive count.
     * Negative values other than minus one violate the buffer-extent contract.
     * An uncompressed direct-read shortcut bypasses decoder state transitions.
     * Fill failure may zero the requested output; other failure paths need
     * not. May allocate or replace handle-owned raw/strile buffers, load
     * deferred metadata, seek and read client I/O, and invoke
     * codec/tag/diagnostic callbacks; reading never writes to the file. No
     * handle or caller-buffer ownership transfers. Failure is not
     * transactional: buffers, codec progress, indices, flags, metadata, or file
     * position can already have changed; disregard partial decoded output
     * unless a codec explicitly documents otherwise. No errno or rollback
     * guarantee. See @ref image_transfers.
     *
     * Codec decode callbacks are tested for nonzero here; negative callback
     * errors can therefore be accepted as completion (including fax errors).
     * A positive public status can accompany error diagnostics.
     *
     * @param[in,out] tif Non-NULL live handle with matching current geometry
     * and codec methods; serialise access externally.
     * @param[in] tile Zero-based strile index including separate sample planes.
     * @param[out] buf Caller-owned writable output with capacity for the
     * effective byte count; do not alias mutable handle raw storage.
     * @param[in] size Minus one for full extent, otherwise a nonnegative
     * decoded limit.
     * @return Effective requested byte count on success, minus one on detected
     * failure; not an ownership result.
     */
    extern tmsize_t TIFFReadEncodedTile(TIFF *tif, uint32_t tile, void *buf,
                                        tmsize_t size);
    /**
     * @brief Read stored bytes from one tile.
     * @ingroup libtiff_public_image_reading
     *
     * Copies stored bytes without decoding, postdecode sample swapping, or
     * fill-order reversal. Minus one requests the whole stored byte count; a
     * positive size limits it to the available metadata count. Zero effective
     * size fails. Requires raw access supported by the codec, valid index,
     * matching organisation, and readable mode. Payload range is read/mapped
     * and exact short reads fail; partial destination contents may remain.
     * Loads deferred strile metadata when needed, then seeks and reads client
     * I/O or copies from the file mapping; invokes no codec callbacks and
     * never writes to the file. Allocates no intermediate pixel-data buffer,
     * but deferred strile metadata queries can allocate or replace
     * handle-owned offset/count arrays, with the allocation-failure and
     * borrowed-view effects described for TIFFGetStrileOffset(). On failure
     * the file position may have moved. No errno or rollback guarantee. See
     * @ref image_transfers.
     *
     * @param[in,out] tif Non-NULL live handle with matching current geometry
     * and codec methods; serialise access externally.
     * @param[in] tile Zero-based strile index including separate sample planes.
     * @param[out] buf Caller-owned writable output with capacity for the
     * effective byte count; do not alias mutable handle raw storage.
     * @param[in] size Minus one for all stored bytes, otherwise a positive raw
     * byte limit.
     * @return Effective requested byte count on success, minus one on detected
     * failure; not an ownership result.
     */
    extern tmsize_t TIFFReadRawTile(TIFF *tif, uint32_t tile, void *buf,
                                    tmsize_t size);
    /**
     * @brief Decode one strile from caller-provided stored bytes.
     * @ingroup libtiff_public_image_reading
     *
     * Borrows a writable input and caller output without reading the strile
     * payload from the file. Intended for handles with readable data (the mode
     * check only rejects O_WRONLY, which libtiff handles never use). Requires a
     * raw-capable codec, valid strile and consistent nonzero per-plane
     * geometry; it does not perform the ordinary public strile-index
     * validation. Temporarily attaches inbuf, may
     * reverse its bits before decode and reverses again afterwards under the
     * then-current flags. Restores old raw-data pointer/capacity and
     * ownership/mapping bits, but not complete codec/raw cursor/progress state:
     * rawcp/rawcc may still refer into input after return. Keep input live
     * until decoder state is restarted/replaced; do not assume safe
     * interleaving with normal reads. No allocations are transferred. Startup
     * failure can zero output; later failure can leave partial output. Other
     * flags and codec effects survive. Codec setup and decode callbacks run and
     * can allocate codec state; some codecs (OJPEG) also seek and read the file
     * there. Never writes to the file. Failure is not transactional; disregard
     * partial decoded output unless a codec explicitly documents otherwise. No
     * errno or rollback guarantee. See @ref image_transfers.
     *
     * Codec decode callbacks are tested for nonzero here; negative callback
     * errors can therefore be accepted as completion (including fax errors).
     * A positive public status can accompany error diagnostics.
     *
     * @param[in,out] tif Non-NULL live handle with matching current geometry
     * and codec methods; serialise access externally.
     * @param[in] strile Valid zero-based strip/tile index under current
     * organisation.
     * @param[in,out] inbuf Borrowed mutable stored-byte buffer of insize bytes;
     * separate from output and handle-owned storage.
     * @param[in] insize Nonnegative valid raw input extent.
     * @param[out] outbuf Writable decoded destination of outsize bytes; NULL
     * only with an explicitly compatible callback codec.
     * @param[in] outsize Nonnegative valid decoded extent accepted by the
     * codec.
     * @return One when startup/decoder paths accepted input, zero on detected
     * rejection/failure; no complete state rollback.
     */
    extern int TIFFReadFromUserBuffer(TIFF *tif, uint32_t strile, void *inbuf,
                                      tmsize_t insize, void *outbuf,
                                      tmsize_t outsize);
    /**
     * @brief Write encoded samples to one strip.
     * @ingroup libtiff_public_image_writing
     *
     * Encodes supplied host-order samples. The source is writable because
     * postdecode swapping runs in place before encoding; uncompressed paths can
     * additionally reverse its bits. Mutation is not undone on failure.
     * Compressed output may be buffered/appended through codec hooks. cc must
     * be a nonnegative representable input extent; encoded strips do not
     * interpret minus one as a full-strip sentinel. Strip writers can grow
     * contiguous strile arrays only at the immediately following index;
     * separate-plane arrays cannot grow this way. Establish ImageLength before
     * strile writes; array growth is not a general image-height update. May
     * allocate or replace handle-owned raw/strile buffers, load deferred
     * metadata, seek and write client I/O, and invoke codec/tag/diagnostic
     * callbacks. No handle or caller-buffer ownership transfers. Failure is not
     * transactional: buffers, codec progress, indices, flags, metadata, file
     * position, or file bytes can already have changed, and data already
     * written is not removed. No errno, rollback, or durability guarantee. See
     * @ref image_transfers.
     *
     * Codec encode callbacks are tested for nonzero here; negative callback
     * errors, such as the fallback encoder used for unknown schemes, can
     * therefore be accepted as success. A positive public status can
     * accompany error diagnostics with no data stored.
     *
     * @param[in,out] tif Non-NULL live handle with matching current geometry
     * and codec methods; serialise access externally.
     * @param[in] strip Zero-based strip index including separate sample
     * planes.
     * @param[in,out] data Caller-owned input of cc bytes; may be byte-swapped
     * or bit-reversed in place before encoding.
     * @param[in] cc Nonnegative input byte count.
     * @return cc on success (the input byte count, not the compressed byte
     * count), or minus one on detected failure.
     */
    extern tmsize_t TIFFWriteEncodedStrip(TIFF *tif, uint32_t strip, void *data,
                                          tmsize_t cc);
    /**
     * @brief Write raw stored bytes to one strip.
     * @ingroup libtiff_public_image_writing
     *
     * Appends already encoded/file-order bytes without sample conversion or
     * compression; supply the representation expected by the selected
     * codec/directory. Does not inspect payload correctness or generally
     * prevent overlapping file regions. cc must be a nonnegative representable
     * input extent; minus one is not a full-strip sentinel. Strip writers can
     * grow contiguous strile arrays only at the immediately following index;
     * separate-plane arrays cannot grow this way. Establish ImageLength before
     * strile writes; array growth is not a general image-height update. May
     * allocate handle-owned strile arrays, load deferred metadata, seek and
     * write client I/O, and invoke tag/diagnostic callbacks; no codec callbacks
     * run. No handle or caller-buffer ownership transfers. Failure is not
     * transactional: strile arrays, flags, file position, or file bytes can
     * already have changed, and data already written is not removed. No errno,
     * rollback, or durability guarantee. See
     * @ref image_transfers.
     *
     * @param[in,out] tif Non-NULL live handle with matching current geometry
     * and codec methods; serialise access externally.
     * @param[in] strip Zero-based strip index including separate sample
     * planes.
     * @param[in] data Readable caller-owned stored bytes of cc bytes; not
     * modified by this call.
     * @param[in] cc Nonnegative count of stored bytes to append.
     * @return cc on success, or minus one on detected failure.
     */
    extern tmsize_t TIFFWriteRawStrip(TIFF *tif, uint32_t strip, void *data,
                                      tmsize_t cc);
    /**
     * @brief Write encoded samples to one tile.
     * @ingroup libtiff_public_image_writing
     *
     * Encodes supplied host-order samples. The source is writable because
     * postdecode swapping runs in place before encoding; uncompressed paths can
     * additionally reverse its bits. Mutation is not undone on failure.
     * Compressed output may be buffered/appended through codec hooks. Encoded
     * tiles clamp cc<1 or cc above the cached decoded tile size to a full tile,
     * including minus one. The caller must therefore supply the full extent
     * even for a zero/negative request. Requires an existing in-range tile and
     * established image/tile geometry; cannot grow the image. May allocate or
     * replace handle-owned raw/strile buffers, load deferred metadata, seek and
     * write client I/O, and invoke codec/tag/diagnostic callbacks. No handle or
     * caller-buffer ownership transfers. Failure is not transactional: buffers,
     * codec progress, indices, flags, metadata, file position, or file bytes
     * can already have changed, and data already written is not removed. No
     * errno, rollback, or durability guarantee. See @ref image_transfers.
     *
     * Codec encode callbacks are tested for nonzero here; negative callback
     * errors, such as the fallback encoder used for unknown schemes, can
     * therefore be accepted as success. A positive public status can
     * accompany error diagnostics with no data stored.
     *
     * @param[in,out] tif Non-NULL live handle with matching current geometry
     * and codec methods; serialise access externally.
     * @param[in] tile Zero-based tile index including separate sample planes.
     * @param[in,out] data Caller-owned input of the effective cc bytes; may be
     * byte-swapped or bit-reversed in place before encoding.
     * @param[in] cc Input byte count; values below one or above the decoded
     * tile size select the full tile.
     * @return Effective input byte count after clamping (not the compressed
     * byte count), or minus one on detected failure.
     */
    extern tmsize_t TIFFWriteEncodedTile(TIFF *tif, uint32_t tile, void *data,
                                         tmsize_t cc);
    /**
     * @brief Write raw stored bytes to one tile.
     * @ingroup libtiff_public_image_writing
     *
     * Appends already encoded/file-order bytes without sample conversion or
     * compression; supply the representation expected by the selected
     * codec/directory. Does not inspect payload correctness or generally
     * prevent overlapping file regions. cc must be a nonnegative representable
     * input extent; unlike TIFFWriteEncodedTile(), minus one is not a full-tile
     * sentinel. Requires an existing in-range tile and established image/tile
     * geometry; cannot grow the image. May allocate handle-owned strile arrays,
     * load deferred metadata, seek and write client I/O, and invoke
     * tag/diagnostic callbacks; no codec callbacks run. No handle or
     * caller-buffer ownership transfers. Failure is not transactional: strile
     * arrays, flags, file position, or file bytes can already have changed, and
     * data already written is not removed. No errno, rollback, or durability
     * guarantee. See @ref image_transfers.
     *
     * @param[in,out] tif Non-NULL live handle with matching current geometry
     * and codec methods; serialise access externally.
     * @param[in] tile Zero-based tile index including separate sample planes.
     * @param[in] data Readable caller-owned stored bytes of cc bytes; not
     * modified by this call.
     * @param[in] cc Nonnegative count of stored bytes to append.
     * @return cc on success, or minus one on detected failure.
     */
    extern tmsize_t TIFFWriteRawTile(TIFF *tif, uint32_t tile, void *data,
                                     tmsize_t cc);
    /**
     * @brief Query the codec's allocation-recovery compression-ratio heuristic.
     * @ingroup libtiff_public_image_buffers
     *
     * Invokes the installed optional ratio callback; absent callback returns
     * zero. Used by selected large-allocation sanity checks, not a
     * mathematically guaranteed compression bound or output-buffer capacity.
     * Callback effects/diagnostics depend on the codec; no ownership transfers
     * and dispatch imposes no other validation.
     *
     * @param[in] tif Non-NULL live handle with matching current geometry and
     * codec methods; the built-in callbacks only read it. Serialise access
     * externally.
     * @return Codec heuristic ratio, or zero when absent/disabled; not an error
     * indicator.
     */
    extern uint64_t TIFFGetMaxCompressionRatio(TIFF *tif);
    /**
     * @brief Report the on-disk byte width of a TIFF data type.
     * @ingroup libtiff_public_fields
     *
     * RATIONAL and SRATIONAL occupy eight bytes even when represented
     * internally by float. TIFF_NOTYPE (zero) returns one for historical
     * compatibility. No diagnostic or allocation occurs.
     *
     * @param[in] type File data type; no state is accessed.
     * @return Width of one file element (1, 2, 4, or 8), or zero for an unknown
     * type.
     */
    extern int TIFFDataWidth(
        TIFFDataType type); /* table of tag datatype widths within TIFF file. */
    /**
     * @brief Change the cached next payload write offset.
     * @ingroup libtiff_public_image_writing
     *
     * Assigns the cached continuation offset (tif_curoff) and clears the saved
     * reusable-extent boundary without seeking, validating file layout,
     * writing, allocating, or diagnosing. The offset only matters when further
     * data is appended to a strile that already has an offset: it is then
     * taken as the current write position for size and boundary bookkeeping,
     * but no seek to it is performed. It does not choose the location of a new
     * strile: for a strile with no offset, or after the offset is reset to
     * zero, the next append reuses the strile's existing on-disk slot when the
     * new data fits and otherwise appends at EOF, ignoring any nonzero value.
     * TIFFWriteEncodedStrip() and TIFFWriteEncodedTile() reset the offset to
     * zero on entry, and TIFFWriteScanline() and TIFFWriteRawStrip() reset it
     * when the strip changes. Does not set IFD metadata offsets, prevent
     * overlap, flush buffers, or transfer ownership.
     *
     * @param[in,out] tif Non-NULL live handle; serialise access externally.
     * @param[in] off Continuation offset in file bytes for the strile being
     * written; zero lets the next append choose the strile location.
     */
    extern void TIFFSetWriteOffset(TIFF *tif, toff_t off);
    /**
     * @brief Reverse the byte order of one value.
     * @ingroup libtiff_public_conversion
     *
     * Swaps bytes in place without interpreting numeric values. No allocation,
     * I/O, diagnostics, bounds checks, or ownership transfer occurs. Caller
     * retains storage; overlapping concurrent access requires external
     * synchronisation. Zero array counts perform no accesses.
     *
     * @param[in,out] wp Borrowed writable single value with the declared type
     * alignment; each element spans 2 bytes.
     */
    extern void TIFFSwabShort(uint16_t *wp);
    /**
     * @brief Reverse the byte order of one value.
     * @ingroup libtiff_public_conversion
     *
     * Swaps bytes in place without interpreting numeric values. No allocation,
     * I/O, diagnostics, bounds checks, or ownership transfer occurs. Caller
     * retains storage; overlapping concurrent access requires external
     * synchronisation. Zero array counts perform no accesses.
     *
     * @param[in,out] lp Borrowed writable single value with the declared type
     * alignment; each element spans 4 bytes.
     */
    extern void TIFFSwabLong(uint32_t *lp);
    /**
     * @brief Reverse the byte order of one value.
     * @ingroup libtiff_public_conversion
     *
     * Swaps bytes in place without interpreting numeric values. No allocation,
     * I/O, diagnostics, bounds checks, or ownership transfer occurs. Caller
     * retains storage; overlapping concurrent access requires external
     * synchronisation. Zero array counts perform no accesses.
     *
     * @param[in,out] lp Borrowed writable single value with the declared type
     * alignment; each element spans 8 bytes.
     */
    extern void TIFFSwabLong8(uint64_t *lp);
    /**
     * @brief Reverse the byte order of one value.
     * @ingroup libtiff_public_conversion
     *
     * Swaps bytes in place without interpreting numeric values. No allocation,
     * I/O, diagnostics, bounds checks, or ownership transfer occurs. Caller
     * retains storage; overlapping concurrent access requires external
     * synchronisation. Zero array counts perform no accesses.
     *
     * @param[in,out] fp Borrowed writable single value with the declared type
     * alignment; each element spans 4 bytes.
     */
    extern void TIFFSwabFloat(float *fp);
    /**
     * @brief Reverse the byte order of one value.
     * @ingroup libtiff_public_conversion
     *
     * Swaps bytes in place without interpreting numeric values. No allocation,
     * I/O, diagnostics, bounds checks, or ownership transfer occurs. Caller
     * retains storage; overlapping concurrent access requires external
     * synchronisation. Zero array counts perform no accesses.
     *
     * @param[in,out] dp Borrowed writable single value with the declared type
     * alignment; each element spans 8 bytes.
     */
    extern void TIFFSwabDouble(double *dp);
    /**
     * @brief Reverse the byte order of each array element.
     * @ingroup libtiff_public_conversion
     *
     * Swaps bytes in place without interpreting numeric values. No allocation,
     * I/O, diagnostics, bounds checks, or ownership transfer occurs. Caller
     * retains storage; overlapping concurrent access requires external
     * synchronisation. Zero array counts perform no accesses.
     *
     * @param[in,out] wp Borrowed writable array of n elements with the declared
     * type alignment; each element spans 2 bytes.
     * @param[in] n Nonnegative element count, not a byte count; storage must
     * cover every element.
     */
    extern void TIFFSwabArrayOfShort(uint16_t *wp, tmsize_t n);
    /**
     * @brief Reverse the byte order of each array element.
     * @ingroup libtiff_public_conversion
     *
     * Swaps bytes in place without interpreting numeric values. For triples,
     * exchanges the first and third bytes of each three-byte group. No
     * allocation, I/O, diagnostics, bounds checks, or ownership transfer
     * occurs. Caller retains storage; overlapping concurrent access requires
     * external synchronisation. Zero array counts perform no accesses.
     *
     * @param[in,out] tp Borrowed writable array of n elements with the declared
     * type alignment; each element spans 3 bytes.
     * @param[in] n Nonnegative element count, not a byte count; storage must
     * cover every element.
     */
    extern void TIFFSwabArrayOfTriples(uint8_t *tp, tmsize_t n);
    /**
     * @brief Reverse the byte order of each array element.
     * @ingroup libtiff_public_conversion
     *
     * Swaps bytes in place without interpreting numeric values. No allocation,
     * I/O, diagnostics, bounds checks, or ownership transfer occurs. Caller
     * retains storage; overlapping concurrent access requires external
     * synchronisation. Zero array counts perform no accesses.
     *
     * @param[in,out] lp Borrowed writable array of n elements with the declared
     * type alignment; each element spans 4 bytes.
     * @param[in] n Nonnegative element count, not a byte count; storage must
     * cover every element.
     */
    extern void TIFFSwabArrayOfLong(uint32_t *lp, tmsize_t n);
    /**
     * @brief Reverse the byte order of each array element.
     * @ingroup libtiff_public_conversion
     *
     * Swaps bytes in place without interpreting numeric values. No allocation,
     * I/O, diagnostics, bounds checks, or ownership transfer occurs. Caller
     * retains storage; overlapping concurrent access requires external
     * synchronisation. Zero array counts perform no accesses.
     *
     * @param[in,out] lp Borrowed writable array of n elements with the declared
     * type alignment; each element spans 8 bytes.
     * @param[in] n Nonnegative element count, not a byte count; storage must
     * cover every element.
     */
    extern void TIFFSwabArrayOfLong8(uint64_t *lp, tmsize_t n);
    /**
     * @brief Reverse the byte order of each array element.
     * @ingroup libtiff_public_conversion
     *
     * Swaps bytes in place without interpreting numeric values. No allocation,
     * I/O, diagnostics, bounds checks, or ownership transfer occurs. Caller
     * retains storage; overlapping concurrent access requires external
     * synchronisation. Zero array counts perform no accesses.
     *
     * @param[in,out] fp Borrowed writable array of n elements with the declared
     * type alignment; each element spans 4 bytes.
     * @param[in] n Nonnegative element count, not a byte count; storage must
     * cover every element.
     */
    extern void TIFFSwabArrayOfFloat(float *fp, tmsize_t n);
    /**
     * @brief Reverse the byte order of each array element.
     * @ingroup libtiff_public_conversion
     *
     * Swaps bytes in place without interpreting numeric values. No allocation,
     * I/O, diagnostics, bounds checks, or ownership transfer occurs. Caller
     * retains storage; overlapping concurrent access requires external
     * synchronisation. Zero array counts perform no accesses.
     *
     * @param[in,out] dp Borrowed writable array of n elements with the declared
     * type alignment; each element spans 8 bytes.
     * @param[in] n Nonnegative element count, not a byte count; storage must
     * cover every element.
     */
    extern void TIFFSwabArrayOfDouble(double *dp, tmsize_t n);
    /**
     * @brief Reverse the bit order within each byte of a buffer.
     * @ingroup libtiff_public_conversion
     *
     * Byte order is unchanged. Applies the immutable reversal table in place.
     * No allocation, I/O, diagnostics, bounds checking, or ownership transfer;
     * zero length performs no accesses.
     *
     * @param[in,out] cp Borrowed writable storage for n bytes.
     * @param[in] n Nonnegative byte count.
     */
    extern void TIFFReverseBits(uint8_t *cp, tmsize_t n);
    /**
     * @brief Select the immutable byte reversal or identity lookup table.
     * @ingroup libtiff_public_conversion
     *
     * Returns static library storage with 256 byte-indexed entries. Keep it
     * read-only and do not free it; valid while the library is loaded. No
     * allocation, mutation, I/O, diagnostics, or failure path.
     *
     * @param[in] reversed Nonzero selects reversed bits; zero selects identity.
     * @return Borrowed non-NULL pointer to the selected 256-entry table.
     */
    extern const unsigned char *TIFFGetBitRevTable(int reversed);

    /**
     * @brief Query one logical strile file offset.
     * @ingroup libtiff_public_strile_metadata
     *
     * Reads cached metadata, possibly allocating/growing both offset/count
     * caches and seeking/reading pages or full arrays. Loading can invalidate
     * previously borrowed array pointers. Invalid indices fail without a
     * diagnostic; malformed entries, I/O, and allocation may dispatch
     * diagnostics. Cache failures can be destructive and can store a zero
     * value; a later call can observe the cached zero without repeating the
     * original error. Logical strip chopping may make these values differ from
     * the stored IFD arrays. Does not decode pixels or validate that the
     * returned range is readable. No ownership transfers. Zero is ambiguous;
     * use the WithErr variant for errors detected by the current call.
     *
     * @param[in,out] tif Non-NULL live handle with matching schema/state;
     * serialise access externally.
     * @param[in] strile Zero-based strip or tile index below the current
     * logical strile count, including sample planes.
     * @return Value in file bytes, or zero on detected failure; zero can also
     * be legitimate metadata. A fill error can accompany a nonzero cached value
     * in the WithErr variant.
     */
    extern uint64_t TIFFGetStrileOffset(TIFF *tif, uint32_t strile);
    /**
     * @brief Query one logical strile byte count.
     * @ingroup libtiff_public_strile_metadata
     *
     * Reads cached metadata, possibly allocating/growing both offset/count
     * caches and seeking/reading pages or full arrays. Loading can invalidate
     * previously borrowed array pointers. Invalid indices fail without a
     * diagnostic; malformed entries, I/O, and allocation may dispatch
     * diagnostics. Cache failures can be destructive and can store a zero
     * value; a later call can observe the cached zero without repeating the
     * original error. Logical strip chopping may make these values differ from
     * the stored IFD arrays. Does not decode pixels or validate that the
     * returned range is readable. No ownership transfers. Zero is ambiguous;
     * use the WithErr variant for errors detected by the current call.
     *
     * @param[in,out] tif Non-NULL live handle with matching schema/state;
     * serialise access externally.
     * @param[in] strile Zero-based strip or tile index below the current
     * logical strile count, including sample planes.
     * @return Value in file bytes, or zero on detected failure; zero can also
     * be legitimate metadata. A fill error can accompany a nonzero cached value
     * in the WithErr variant.
     */
    extern uint64_t TIFFGetStrileByteCount(TIFF *tif, uint32_t strile);
    /**
     * @brief Query one logical strile file offset.
     * @ingroup libtiff_public_strile_metadata
     *
     * Reads cached metadata, possibly allocating/growing both offset/count
     * caches and seeking/reading pages or full arrays. Loading can invalidate
     * previously borrowed array pointers. Invalid indices fail without a
     * diagnostic; malformed entries, I/O, and allocation may dispatch
     * diagnostics. Cache failures can be destructive and can store a zero
     * value; a later call can observe the cached zero without repeating the
     * original error. Logical strip chopping may make these values differ from
     * the stored IFD arrays. Does not decode pixels or validate that the
     * returned range is readable. No ownership transfers. Check pbErr for this
     * call, but it is not a persistent record of earlier cache-load errors.
     *
     * @param[in,out] tif Non-NULL live handle with matching schema/state;
     * serialise access externally.
     * @param[in] strile Zero-based strip or tile index below the current
     * logical strile count, including sample planes.
     * @param[out] pbErr Optional error slot: initially zero, set to one for
     * errors detected by this call; NULL discards status.
     * @return Value in file bytes, or zero on detected failure; zero can also
     * be legitimate metadata. A fill error can accompany a nonzero cached value
     * in the WithErr variant.
     */
    extern uint64_t TIFFGetStrileOffsetWithErr(TIFF *tif, uint32_t strile,
                                               int *pbErr);
    /**
     * @brief Query one logical strile byte count.
     * @ingroup libtiff_public_strile_metadata
     *
     * Reads cached metadata, possibly allocating/growing both offset/count
     * caches and seeking/reading pages or full arrays. Loading can invalidate
     * previously borrowed array pointers. Invalid indices fail without a
     * diagnostic; malformed entries, I/O, and allocation may dispatch
     * diagnostics. Cache failures can be destructive and can store a zero
     * value; a later call can observe the cached zero without repeating the
     * original error. Logical strip chopping may make these values differ from
     * the stored IFD arrays. Does not decode pixels or validate that the
     * returned range is readable. No ownership transfers. Check pbErr for this
     * call, but it is not a persistent record of earlier cache-load errors.
     *
     * @param[in,out] tif Non-NULL live handle with matching schema/state;
     * serialise access externally.
     * @param[in] strile Zero-based strip or tile index below the current
     * logical strile count, including sample planes.
     * @param[out] pbErr Optional error slot: initially zero, set to one for
     * errors detected by this call; NULL discards status.
     * @return Value in file bytes, or zero on detected failure; zero can also
     * be legitimate metadata. A fill error can accompany a nonzero cached value
     * in the WithErr variant.
     */
    extern uint64_t TIFFGetStrileByteCountWithErr(TIFF *tif, uint32_t strile,
                                                  int *pbErr);

#if LOGLUV_PUBLIC
/**
 * @brief Define the neutral CIE u-prime chromaticity used by LogLuv.
 * @ingroup libtiff_public_data
 */
#define U_NEU 0.210526316
/**
 * @brief Define the neutral CIE v-prime chromaticity used by LogLuv.
 * @ingroup libtiff_public_data
 */
#define V_NEU 0.473684211
/**
 * @brief Define the chromaticity scale factor for 32-bit LogLuv encoding.
 * @ingroup libtiff_public_data
 */
#define UVSCALE 410.
    /**
     * @brief Decode 16-bit logarithmic luminance to a linear value.
     * @ingroup libtiff_public_logluv
     *
     * Uses the low fifteen magnitude bits and bit fifteen as sign; zero
     * magnitude returns zero, otherwise returns the bin-centre exponential with
     * its encoded sign. No allocation, TIFF handle access, client I/O,
     * diagnostics, retained pointers, or ownership transfer occurs. There is no
     * separate error status unless stated.
     *
     * @param[in] p16 Encoded signed LogL16 luminance; only the low sixteen
     * bits are used, with bit fifteen as the sign.
     * @return Decoded linear luminance; negative when the sign bit is set.
     */
    extern double LogL16toY(int p16);
    /**
     * @brief Decode 10-bit logarithmic luminance to a linear value.
     * @ingroup libtiff_public_logluv
     *
     * Requires a code in 0..1023; zero returns zero, otherwise returns the
     * bin-centre exponential. Does not mask or range-check arbitrary int input.
     * No allocation, TIFF handle access, client I/O, diagnostics, retained
     * pointers, or ownership transfer occurs. There is no separate error status
     * unless stated.
     *
     * @param[in] p10 Encoded 10-bit luminance code in 0..1023.
     * @return Nonnegative decoded linear luminance.
     */
    extern double LogL10toY(int p10);
    /**
     * @brief Convert a linear XYZ triplet to display RGB bytes.
     * @ingroup libtiff_public_logluv
     *
     * Uses fixed CCIR-709 primary coefficients, clips each linear channel to
     * 0..1 and applies an approximate gamma of two via square root. Requires
     * finite XYZ inputs and disjoint input/output storage. This display
     * transform loses precision and clips out-of-range colours. No allocation,
     * TIFF handle access, client I/O, diagnostics, retained pointers, or
     * ownership transfer occurs. There is no separate error status unless
     * stated.
     *
     * @param[in] xyz Borrowed readable three-float XYZ triplet.
     * @param[out] rgb Borrowed writable three-byte RGB triplet.
     */
    extern void XYZtoRGB24(float *xyz, uint8_t *rgb);
    /**
     * @brief Decode a quantised chromaticity index to a bin-centre pair.
     * @ingroup libtiff_public_logluv
     *
     * Rejects c outside 0..UV_NDIVS-1 before writing outputs; binary-searches
     * the immutable uv_row table and returns u-prime/v-prime bin centres.
     * Output slots must be separate and non-NULL. No allocation, TIFF handle
     * access, client I/O, diagnostics, retained pointers, or ownership transfer
     * occurs. There is no separate error status unless stated.
     *
     * @param[out] up Caller-owned u-prime result; unchanged on invalid code.
     * @param[out] vp Caller-owned v-prime result; unchanged on invalid code.
     * @param[in] c Quantised chromaticity code, valid range 0..16288.
     * @return Zero on decoded coordinates, minus one for invalid code.
     */
    extern int uv_decode(double *up, double *vp, int c);
    /**
     * @brief Decode one packed 24-bit LogLuv pixel to linear XYZ.
     * @ingroup libtiff_public_logluv
     *
     * Extracts ten-bit luminance and fourteen-bit table chromaticity from the
     * low twenty-four bits; invalid chromaticity codes use the neutral pair.
     * Nonpositive luminance produces an all-zero triplet; positive luminance
     * converts bin-centre chromaticity to XYZ. This is quantised representation
     * conversion, not exact round-trip recovery. No allocation, TIFF handle
     * access, client I/O, diagnostics, retained pointers, or ownership transfer
     * occurs. There is no separate error status unless stated.
     *
     * @param[in] p Packed 24-bit LogLuv code in the low twenty-four bits;
     * upper eight bits ignored.
     * @param[out] XYZ Non-NULL caller-owned writable three-float result.
     */
    extern void LogLuv24toXYZ(uint32_t p, float *XYZ);
    /**
     * @brief Decode one packed 32-bit LogLuv pixel to linear XYZ.
     * @ingroup libtiff_public_logluv
     *
     * Extracts signed sixteen-bit luminance and two eight-bit chromaticity
     * components scaled by UVSCALE. Nonpositive luminance produces an all-zero
     * triplet; positive luminance converts bin-centre chromaticity to XYZ. This
     * is quantised representation conversion, not exact round-trip recovery. No
     * allocation, TIFF handle access, client I/O, diagnostics, retained
     * pointers, or ownership transfer occurs. There is no separate error status
     * unless stated.
     *
     * @param[in] p Packed 32-bit LogLuv code: signed 16-bit LogL in the high
     * word, 8-bit u and v codes in the low word.
     * @param[out] XYZ Non-NULL caller-owned writable three-float result.
     */
    extern void LogLuv32toXYZ(uint32_t p, float *XYZ);
#if defined(c_plusplus) || defined(__cplusplus)
    extern int LogL16fromY(double, int = SGILOGENCODE_NODITHER);
    extern int LogL10fromY(double, int = SGILOGENCODE_NODITHER);
    extern int uv_encode(double, double, int = SGILOGENCODE_NODITHER);
    extern uint32_t LogLuv24fromXYZ(float *, int = SGILOGENCODE_NODITHER);
    extern uint32_t LogLuv32fromXYZ(float *, int = SGILOGENCODE_NODITHER);
#else
    /**
     * @brief Encode linear luminance as a 16-bit logarithmic value.
     * @ingroup libtiff_public_logluv
     *
     * Saturates magnitudes at approximately 1.8371976e19, maps magnitudes below
     * approximately 5.4136769e-20 to zero, and quantises log2 magnitude with
     * its sign. Negative encodings can be returned as sign-extended int values;
     * inspect the low sixteen bits rather than interpreting a negative result
     * as an error. SGILOGENCODE_NODITHER truncates deterministically; other
     * method values use the shared pseudo-random dither state. Serialise
     * dithering across threads; state is seeded from time on first use and is
     * not caller-owned. C++ defaults em to SGILOGENCODE_NODITHER. No
     * allocation, TIFF handle access, client I/O, diagnostics, retained
     * pointers, or ownership transfer occurs. There is no separate error status
     * unless stated.
     *
     * @param[in] Y Finite linear luminance; LogL16 supports signed values,
     * LogL10 clips low/nonpositive values to zero.
     * @param[in] em Encoding method, normally SGILOGENCODE_NODITHER or
     * SGILOGENCODE_RANDITHER.
     * @return Encoded code pattern; not a Boolean status or ownership result.
     */
    extern int LogL16fromY(double Y, int em);
    /**
     * @brief Encode linear luminance as a 10-bit logarithmic value.
     * @ingroup libtiff_public_logluv
     *
     * Saturates Y>=15.742 to 1023 and Y<=0.00024283 to zero; quantises the
     * intermediate log2 range. Requires finite Y; NaN can reach an invalid
     * integer conversion. SGILOGENCODE_NODITHER truncates deterministically;
     * other method values use the shared pseudo-random dither state. Serialise
     * dithering across threads; state is seeded from time on first use and is
     * not caller-owned. C++ defaults em to SGILOGENCODE_NODITHER. No
     * allocation, TIFF handle access, client I/O, diagnostics, retained
     * pointers, or ownership transfer occurs. There is no separate error status
     * unless stated.
     *
     * @param[in] Y Finite linear luminance; LogL16 supports signed values,
     * LogL10 clips low/nonpositive values to zero.
     * @param[in] em Encoding method, normally SGILOGENCODE_NODITHER or
     * SGILOGENCODE_RANDITHER.
     * @return Encoded code pattern; not a Boolean status or ownership result.
     */
    extern int LogL10fromY(double Y, int em);
    /**
     * @brief Encode chromaticity using the irregular LogLuv quantisation table.
     * @ingroup libtiff_public_logluv
     *
     * Maps NaN coordinates to neutral chromaticity, then selects a table bin.
     * Out-of-gamut values map to a hue-indexed perimeter bin through a lazily
     * initialised shared table. Require finite coordinates after NaN handling
     * and representable quantisation arithmetic; infinities are not generally
     * safe. Serialise first-use perimeter-table initialisation and dithering
     * across threads. SGILOGENCODE_NODITHER truncates deterministically; other
     * method values use the shared pseudo-random dither state. Serialise
     * dithering across threads; state is seeded from time on first use and is
     * not caller-owned. C++ defaults em to SGILOGENCODE_NODITHER. No
     * allocation, TIFF handle access, client I/O, diagnostics, retained
     * pointers, or ownership transfer occurs. There is no separate error status
     * unless stated.
     *
     * @param[in] u Finite CIE u-prime coordinate.
     * @param[in] v Finite CIE v-prime coordinate.
     * @param[in] em Quantisation/dithering method; C++ default is no dither.
     * @return Quantised chromaticity code; no independent success/failure
     * channel.
     */
    extern int uv_encode(double u, double v, int em);
    /**
     * @brief Encode linear XYZ as one packed 24-bit LogLuv pixel.
     * @ingroup libtiff_public_logluv
     *
     * Encodes luminance and derives u-prime/v-prime from X+15Y+3Z. Zero encoded
     * luminance or nonpositive sum uses neutral chromaticity. Uses the
     * irregular chromaticity table and hue-preserving perimeter fallback;
     * serialise its first-use initialisation. SGILOGENCODE_NODITHER truncates
     * deterministically; other method values use the shared pseudo-random
     * dither state. Serialise dithering across threads; state is seeded from
     * time on first use and is not caller-owned. C++ defaults em to
     * SGILOGENCODE_NODITHER. No allocation, TIFF handle access, client I/O,
     * diagnostics, retained pointers, or ownership transfer occurs. There is no
     * separate error status unless stated.
     *
     * @param[in] XYZ Borrowed readable three-float finite XYZ triplet;
     * arithmetic/quantisation must remain representable.
     * @param[in] em Quantisation/dithering method; C++ defaults to no dither.
     * @return Quantised packed code; upper eight bits zero for LogLuv24, no
     * independent error status.
     */
    extern uint32_t LogLuv24fromXYZ(float *XYZ, int em);
    /**
     * @brief Encode linear XYZ as one packed 32-bit LogLuv pixel.
     * @ingroup libtiff_public_logluv
     *
     * Encodes luminance and derives u-prime/v-prime from X+15Y+3Z. Zero encoded
     * luminance or nonpositive sum uses neutral chromaticity. Quantises scaled
     * chromaticity components and clips codes to 0..255; signed luminance codes
     * are retained in the high word. SGILOGENCODE_NODITHER truncates
     * deterministically; other method values use the shared pseudo-random
     * dither state. Serialise dithering across threads; state is seeded from
     * time on first use and is not caller-owned. C++ defaults em to
     * SGILOGENCODE_NODITHER. No allocation, TIFF handle access, client I/O,
     * diagnostics, retained pointers, or ownership transfer occurs. There is no
     * separate error status unless stated.
     *
     * @param[in] XYZ Borrowed readable three-float finite XYZ triplet;
     * arithmetic/quantisation must remain representable.
     * @param[in] em Quantisation/dithering method; C++ defaults to no dither.
     * @return Quantised packed code; upper eight bits zero for LogLuv24, no
     * independent error status.
     */
    extern uint32_t LogLuv32fromXYZ(float *XYZ, int em);
#endif
#endif /* LOGLUV_PUBLIC */

    /**
     * @brief Initialise caller-owned CIE Lab conversion tables and reference
     * white.
     * @ingroup libtiff_public_conversion
     *
     * Copies display and white-point values into the supplied complete
     * structure and fills embedded gamma tables. Allocates no storage and
     * retains no input pointers. Requires finite, physically usable
     * coefficients, positive gammas and nonzero usable table steps; inputs are
     * not validated. Current implementation computes all three table steps from
     * the red-channel luminance endpoints. No I/O or diagnostics; caller owns
     * and releases the state.
     *
     * @param[out] cielab Non-NULL writable full conversion structure, disjoint
     * from inputs.
     * @param[in] display Non-NULL borrowed display description copied during
     * the call.
     * @param[in] refWhite Borrowed three-float XYZ reference-white array;
     * positive Y.
     * @return Zero unconditionally; not a validation status.
     */
    extern int TIFFCIELabToRGBInit(TIFFCIELabToRGB *cielab,
                                   const TIFFDisplay *display, float *refWhite);
    /**
     * @brief Convert 8-bit TIFF CIE L*a*b* samples to XYZ.
     * @ingroup libtiff_public_conversion
     *
     * Uses the stored reference white and the piecewise Lab inverse transform.
     * Delegates to TIFFCIELab16ToXYZ() after unsigned luminance scaling by 257
     * and signed chrominance scaling by 256; out-of-range inputs are not
     * checked and can overflow signed arithmetic. Writes outputs without
     * allocation, I/O, diagnostics, or ownership transfer. Supply finite valid
     * conversion state; this void interface has no error status.
     *
     * @param[in] cielab Non-NULL caller-owned state initialised by
     * TIFFCIELabToRGBInit(); retained by caller.
     * @param[in] l Unsigned luminance in 0..255.
     * @param[in] a Signed chrominance in -128..127.
     * @param[in] b Signed chrominance with the same scale/range as a.
     * @param[out] X Non-NULL caller-owned output slot; must not alias the
     * conversion state.
     * @param[out] Y Non-NULL caller-owned output slot; must not alias the
     * conversion state.
     * @param[out] Z Non-NULL caller-owned output slot; must not alias the
     * conversion state.
     */
    extern void TIFFCIELabToXYZ(TIFFCIELabToRGB *cielab, uint32_t l, int32_t a,
                                int32_t b, float *X, float *Y, float *Z);
    /**
     * @brief Convert XYZ tristimulus values to display RGB channels.
     * @ingroup libtiff_public_conversion
     *
     * Multiplies the display matrix, clips luminosities, indexes gamma tables,
     * rounds, and caps channels at configured display white values. Requires
     * finite inputs and valid positive table steps; malformed state/NaNs are
     * not generally rejected. No allocation, I/O, diagnostics, or ownership
     * transfer; output slots remain caller-owned and must not alias state.
     *
     * @param[in] cielab Non-NULL caller-owned state initialised by
     * TIFFCIELabToRGBInit(); retained by caller.
     * @param[in] X Finite tristimulus component in the configured
     * reference-white scale.
     * @param[in] Y Finite tristimulus component in the configured
     * reference-white scale.
     * @param[in] Z Finite tristimulus component in the configured
     * reference-white scale.
     * @param[out] r Non-NULL writable display-channel result.
     * @param[out] g Non-NULL writable display-channel result.
     * @param[out] b Non-NULL writable display-channel result.
     */
    extern void TIFFXYZToRGB(TIFFCIELabToRGB *cielab, float X, float Y, float Z,
                             uint32_t *r, uint32_t *g, uint32_t *b);

    /**
     * @brief Initialise caller-allocated 8-bit YCbCr conversion tables.
     * @ingroup libtiff_public_conversion
     *
     * Lays out biased clamp and contribution tables after the structure in the
     * same allocation. See TIFFYCbCrToRGB for the required extra capacity and
     * alignment; sizeof the structure alone is insufficient. Copies
     * coefficients into computed tables without retaining input pointers.
     * Requires finite usable coefficients/reference endpoints; clamps
     * contribution ranges but does not provide comprehensive validation. No
     * allocation, I/O, diagnostics, or ownership transfer. Do not free
     * individual table pointers.
     *
     * @param[out] ycbcr Non-NULL suitably aligned writable structure plus all
     * trailing table storage, disjoint from inputs.
     * @param[in] luma Borrowed three-float red/green/blue luma weights; green
     * must support finite division.
     * @param[in] refBlackWhite Borrowed six-float Y/Cb/Cr black-white endpoint
     * pairs.
     * @return Zero unconditionally; not a validation status.
     */
    extern int TIFFYCbCrToRGBInit(TIFFYCbCrToRGB *ycbcr, float *luma,
                                  float *refBlackWhite);
    /**
     * @brief Convert an 8-bit YCbCr sample triplet to clipped RGB.
     * @ingroup libtiff_public_conversion
     *
     * Clamps inputs to 0..255, combines initialised fixed-point contribution
     * tables, and clips all outputs to 0..255. No allocation, I/O, diagnostics,
     * state mutation, or ownership transfer. Output slots must not alias the
     * conversion state; state and tables must remain live throughout.
     *
     * @param[in] ycbcr Non-NULL caller-owned state and trailing tables
     * initialised by TIFFYCbCrToRGBInit().
     * @param[in] Y Unsigned luminance, capped at 255.
     * @param[in] Cb Blue chroma code, clamped to 0..255.
     * @param[in] Cr Red chroma code, clamped to 0..255.
     * @param[out] r Non-NULL caller-owned channel result in 0..255.
     * @param[out] g Non-NULL caller-owned channel result in 0..255.
     * @param[out] b Non-NULL caller-owned channel result in 0..255.
     */
    extern void TIFFYCbCrtoRGB(TIFFYCbCrToRGB *ycbcr, uint32_t Y, int32_t Cb,
                               int32_t Cr, uint32_t *r, uint32_t *g,
                               uint32_t *b);

    /****************************************************************************
     *               O B S O L E T E D    I N T E R F A C E S
     *
     * Don't use this stuff in your applications, it may be removed in the
     *future libtiff versions.
     ****************************************************************************/
    /**
     * @brief Describe a tag using the legacy field-registration interface.
     * @ingroup libtiff_public_data
     *
     * TIFFMergeFieldInfo() converts these records to private field descriptors
     * but retains non-NULL name pointers. Do not release name storage while the
     * descriptors can still be used. This legacy interface is marked obsolete
     * in the public header; field-access argument types and counts are
     * documented with directories.
     */
    typedef struct
    {
        /** @brief Numeric tag identifier. */
        uint32_t field_tag;
        /**
         * @brief Expected read count, or a TIFF_VARIABLE, TIFF_VARIABLE2, or
         * TIFF_SPP marker.
         */
        short field_readcount;
        /**
         * @brief Expected write count, or a variable/per-sample count marker.
         */
        short field_writecount;
        /** @brief On-disk TIFFDataType. */
        TIFFDataType field_type;
        /** @brief Field-presence bit; use FIELD_CUSTOM for custom fields. */
        unsigned short field_bit;
        /** @brief Nonzero if the field may change after writing has begun. */
        unsigned char field_oktochange;
        /**
         * @brief Nonzero if a count argument accompanies the value in field
         * access calls.
         */
        unsigned char field_passcount;
        /**
         * @brief Borrowed null-terminated tag name; keep valid while
         * registered.
         */
        const char *field_name;
    } TIFFFieldInfo;

    /**
     * @brief Register legacy application field definitions on a handle.
     * @ingroup libtiff_public_fields
     *
     * Converts descriptors into handle-owned compatibility storage, but borrows
     * each non-NULL field_name until field-table/handle cleanup; keep names
     * live and unchanged. Input records may be released after the call.
     * Existing tag definitions win; this does not replace a definition or
     * install tag values. Count validation/allocation/merge failures dispatch
     * diagnostics and may leave partial compatibility storage or altered field-
     * table state; there is no rollback. Zero-length input is not a successful
     * no-op. Call during image-directory extension before values are read. This
     * legacy interface cannot express every private set/get representation; see
     * @ref tag_arguments.
     *
     * @param[in,out] tif Non-NULL live TIFF handle; accessed without internal
     * locking.
     * @param[in] info Readable array of n definitions, with valid count rules
     * and long-lived names.
     * @param[in] n Positive number of records, with allocation sizes
     * representable by tmsize_t.
     * @return Zero on success, minus one on detected failure (opposite to
     * ordinary tag setters).
     */
    extern int TIFFMergeFieldInfo(TIFF *tif, const TIFFFieldInfo info[],
                                  uint32_t n);

#if defined(c_plusplus) || defined(__cplusplus)
}
#endif

#endif /* _TIFFIO_ */

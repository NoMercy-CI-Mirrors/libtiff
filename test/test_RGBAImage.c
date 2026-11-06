/*
 * TIFF Library
 *
 * Copyright (c) 2026, Su Laus  @Su_Laus
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that (i) the above copyright notices and this permission notice appear in
 * all copies of the software and related documentation, and (ii) the names of
 * the author may not be used in any advertising or
 * publicity relating to the software without the specific, prior written
 * permission of the author.
 *
 * THE SOFTWARE IS PROVIDED "AS-IS" AND WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS, IMPLIED OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, INCIDENTAL, INDIRECT OR CONSEQUENTIAL DAMAGES OF ANY KIND,
 * OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER OR NOT ADVISED OF THE POSSIBILITY OF DAMAGE, AND ON ANY THEORY OF
 * LIABILITY, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

/* ===========  Purpose ===============================================
 *
 * Test file for the TFFRGBAImage functions.
 * see e.g. https://libtiff.gitlab.io/libtiff/functions/TIFFRGBAImage.html
 * and      https://libtiff.gitlab.io/libtiff/functions/TIFFReadRGBAImage.html
 *
 * Test points are:
 * - Tests are performed using RGB test-images.
 * - Pixel orientation within "raster" of TIFFRGBAImageGet() and
 *   TIFFRGBAImage().
 * - Image data are always located at lower, left-hand part of the raster
 * matrix.
 * - Test for buffer overflows.
 *
 * Tests for following improvements:
 * - Raster width can now be larger than image width.
 * - Only image data are copied to raster buffer if tiles are padded.
 * - Avoid buffer overflow if col_offset > 0.
 * - If row_offset > 0 do not try to read after last row - avoiding warnings.
 * - Feature "col_offset" and "row_offset" now works as expected.
 * - Test update of requested orientation puts image origin now to respective
 *   raster corner.
 *
 */

#include "tif_config.h"

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "tiffio.h"

/*-- Special settings for debugging. --*/
/* Output raster memory as .ppm images. */
// #define PRINTTO_PPM

/* For visual debugging (e.g. .ppm-images) the corner pixels are assigned colors
 * that are noticeably different.
 * But then, checkRasterContents() returns errors and should be disables
 * defining NO_checkRasterContents. */
// #define PATCH_CORNER_PIXELS
#ifdef PATCH_CORNER_PIXELS
#define NO_checkRasterContents
#endif

/* Fileames are assigned uniquely, corresponding to tiled/stripes and
 * continuous/separate. In all cases, the data in the raster must be the same.
 * To make it easier to compare this during debugging, the filenames are
 * generated without this distinction, making them ambiguous. However, in that
 * case, only the PPM files for each combination should be created and saved
 * manually so they can be compared later. */
// #define SHORT_AMBIGUOUS_FILENAMES

/* For debugging do not diasble warnings and error messages for certain cases.
 */
// #define DO_NOT_DISABLE_WARNINGS_AND_ERRORS

// #define DEBUG_TESTING
#ifdef DEBUG_TESTING
#define GOTOFAILURE                                                            \
    {                                                                          \
    }
#else
/*  Only for automake and CMake infrastructure the test should:
    a.) delete any written testfiles when test passed
        (otherwise autotest will fail)
    b.) goto failure, if any failure is detected, which is not
        necessary when test is initiated manually for debugging.
*/
#define GOTOFAILURE goto failure;
#endif

/* Activate test cases with raster dimensions wider than image / tile
 * dimensions. Higher raster than image is allowd but till libtiff v4.7.0 the
 * raster must not be wider than ther image. From v4.7.1 on the raster can be
 * wider than the image. */
#define WITH_WIDER_RASTER

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

/* Uint32_t pattern to prefill raster memory. */
#define RASTER_BACKGROUND_UINT32VAL 0x18254621

/* Global parameter for writing to a logfile and/or to screen. */
FILE *stdXOut = NULL;
bool const blnStdOutToLogFile = false;
bool blnQuiet = false;

#ifdef DEBUG_TESTING
char const *logFilename = "test_RGBAImage_log.txt";
#else
char const *logFilename = NULL;
#endif
bool const blnMultipleLogFiles = false;
char const *arrLogFilenames[] = {
    "test_RGBAImage_log_1.txt", "test_RGBAImage_log_2.txt",
    "test_RGBAImage_log_3.txt", "test_RGBAImage_log_4.txt"};
FILE *fpLog = NULL;
bool const blnPrintRasterToScreen = false;

/* Global values for write_data_to_current_directory()
 * which are set only on purpose to different values. */
uint16_t photometric = PHOTOMETRIC_RGB; // PHOTOMETRIC_MINISBLACK
uint16_t planarconfig = PLANARCONFIG_CONTIG;
uint32_t rows_per_strip = 16;
uint32_t tile_width_length = 16;

#ifndef TIFFmin
#define TIFFmax(A, B) ((A) > (B) ? (A) : (B))
#define TIFFmin(A, B) ((A) < (B) ? (A) : (B))
#endif

/* Global settings -not save to change- */
#define SPP 3 /* samples per pixel */
#define BPS 8 /* bits per sample */

static char const *const modeStrings[] = {"wl", "wb", "w8l", "w8b"};
static char const *const orientationStrings[] = {
    "not valid", /*0 for error printout */
    "TOPLEFT",   /*1 row 0 top, col 0 lhs */
    "TOPRIGHT",  /*2 row 0 top, col 0 rhs */
    "BOTRIGHT",  /*3 row 0 bottom, col 0 rhs */
    "BOTLEFT",   /*4 row 0 bottom, col 0 lhs */
    "LEFTTOP",   /*5 row 0 lhs, col 0 top */
    "RIGHTTOP",  /*6 row 0 rhs, col 0 top */
    "RIGHTBOT",  /*7 row 0 rhs, col 0 bottom */
    "LEFTBOT"    /*8 row 0 lhs, col 0 bottom */
};
static uint16_t checkedOrientation(uint16_t orientation)
{
    if (orientation < 1 || orientation > 8)
        return 0;
    return orientation;
}

/* Defines to have error checking but also clean and readable source code. */

#define TIFFSetField_M(tif, tag, value, filename, line)                        \
    if (!TIFFSetField(tif, tag, value))                                        \
    {                                                                          \
        fprintf(stdXOut, "Can't set tag %d (%s) for %s at line %d\n", tag,     \
                filename, TIFFFieldName(TIFFFieldWithTag(tif, tag)), line);    \
        goto failure;                                                          \
    }

#define TIFFGetField_M(tif, tag, value, filename, line)                        \
    if (!TIFFGetField(tif, tag, value))                                        \
    {                                                                          \
        fprintf(stdXOut, "Can't get tag %d (%s) for %s at line %d\n", tag,     \
                filename, TIFFFieldName(TIFFFieldWithTag(tif, tag)), line);    \
        goto failure;                                                          \
    }

#define TIFFOpen_M(tif, filename, modeString, line)                            \
    tif = TIFFOpen(filename, modeString);                                      \
    if (!tif)                                                                  \
    {                                                                          \
        fprintf(stdXOut, "Can't create %s. Testline %d\n", filename,           \
                __LINE__);                                                     \
        return 1;                                                              \
    }

#define TIFFWriteDirectory_M(tif, filename, line)                              \
    if (!TIFFWriteDirectory(tif))                                              \
    {                                                                          \
        fprintf(stdXOut, "Can't write directory to %s at line %d\n", filename, \
                line);                                                         \
        goto failure;                                                          \
    }

#define TIFFWriteScanline_M(tif, buf, row, sample, filename, line)             \
    if (TIFFWriteScanline(tif, buf, row, sample) == -1)                        \
    {                                                                          \
        fprintf(                                                               \
            stdXOut,                                                           \
            "Can't write image data scanline %u sample %u of %s at line %d\n", \
            (unsigned)(row), (unsigned)(sample), filename, line);              \
        goto failure;                                                          \
    }

#ifdef PRINTTO_PPM
/* Print "raster" as PPM to file */
static int printRasterToPPMFile(char const *fileName, TIFFRGBAImage *img,
                                uint32_t *raster, uint32_t rSizeWidth,
                                uint32_t rSizeHeight)
{
    char ppmFileName[8096];
    sprintf(ppmFileName, "%s_r%02u_%02u_o%02d_%02d.ppm", fileName, rSizeWidth,
            rSizeHeight, img->col_offset, img->row_offset);
    FILE *f = fopen(ppmFileName, "w");
    if (!f)
    {
        printf("couldn't open %s\n", ppmFileName);
        return 1;
    }
    fprintf(f, "P3\n%u %u\n255\n", rSizeWidth, rSizeHeight);
    for (uint32_t y = 0; y < rSizeHeight; y++)
    {
        for (uint32_t x = 0; x < rSizeWidth; x++)
        {
            uint32_t pix = raster[y * rSizeWidth + x];
            fprintf(f, "%u %u %u\n", TIFFGetR(pix), TIFFGetG(pix),
                    TIFFGetB(pix));
        }
    }
    fclose(f);
    return 0;
} /*-- printRasterToPPMFile() --*/
#endif

/* Writes some pixel data as strips or tiles to file.
 */
static int write_image_data(TIFF *tif, uint32_t width, uint32_t length,
                            bool blnTiled, unsigned int pixval,
                            unsigned char *plastlinedata,
                            unsigned int lastlinebytesmax)
{
    size_t bufLen;
    unsigned char *pbufLine = NULL;
    unsigned int bpsmod = (1 << BPS);
    uint32_t tlwidth = 0;
    uint32_t tllength = 0;
    tmsize_t tlsize;

    uint16_t l_planarconfig, samples_per_pixel;
    uint32_t local_rows_per_strip = 0;
    tmsize_t strip_size = 0;
    tmsize_t scanline_size = 0;
    uint32_t num_strips;

    (void)pixval;

    char const *const filename = TIFFFileName(tif);

    if (width == 0 || length == 0)
    {
        fprintf(stdXOut,
                "Image width or length of image data to write is zero."
                "Testline %d\n",
                __LINE__);
        goto failure;
    }

    TIFFGetField_M(tif, TIFFTAG_PLANARCONFIG, &l_planarconfig, filename,
                   __LINE__);
    TIFFGetField_M(tif, TIFFTAG_SAMPLESPERPIXEL, &samples_per_pixel, filename,
                   __LINE__);

    if (blnTiled)
    {
        TIFFGetField_M(tif, TIFFTAG_TILEWIDTH, &tlwidth, filename, __LINE__);
        TIFFGetField_M(tif, TIFFTAG_TILELENGTH, &tllength, filename, __LINE__);
        /* For tiled mode get size of a tile in bytes */
        tlsize = TIFFTileSize(tif);
        bufLen = (size_t)tlsize;
    }
    else
    {
        TIFFGetField_M(tif, TIFFTAG_ROWSPERSTRIP, &local_rows_per_strip,
                       filename, __LINE__);
        /* For strip mode get some strip parameters.
         * With SEPARATE, num_strips is SamplesPerPixel times larger
         * than with CONTIG - and strip_size is smaller. */
        strip_size = TIFFStripSize(tif);
        scanline_size = TIFFScanlineSize(tif);
        num_strips = TIFFNumberOfStrips(tif); // debugging
        (void)num_strips; // debugging avoid compiler warning unused parameter

        /* For strip mode get size of a row in bytes */
        bufLen = (((size_t)width * SPP * BPS) + 7) / 8;
        if (planarconfig == PLANARCONFIG_SEPARATE)
        {
            bufLen = (size_t)strip_size;
        }
    }

    pbufLine = (unsigned char *)_TIFFmalloc((tmsize_t)bufLen);
    if (pbufLine == NULL)
        return 1;

    /* Write dummy pixel data. */
    if (blnTiled)
    {
        /* With SEPARATE, the complete image of each color is written one after
         * the other. Tiles are counted continuously R+G+B. */
        uint32_t numtiles = TIFFNumberOfTiles(tif);
        uint32_t this_spp =
            l_planarconfig == PLANARCONFIG_CONTIG ? 0 : (SPP - 1);
        /* tiles_per_row = tiles across */
        uint32_t tiles_per_row = (width + tlwidth - 1) / tlwidth;
        /* tiles_per_col = tiles down */
        uint32_t tiles_per_col = (length + tllength - 1) / tllength;

        uint32_t last_width = width % tlwidth;
        uint32_t last_length = length % tllength;
        /* Correct values for following processing at special case. */
        if (last_width == 0)
            last_width = tlwidth;
        if (last_length == 0)
            last_length = tllength;

        uint32_t i, j, k, s, row, this_tlwidth, this_tllength;
        uint32_t coltile, rowtile;
        /* CONTIG:   s=0 because all components are written into one tile
         * continuously. SEPARATE: For each component s=0..2, all tiles of a
         * component are written.
         */
        for (s = 0; s <= this_spp; s++)
        {
            for (coltile = 0; coltile < tiles_per_col; coltile++)
            {
                if (coltile < (tiles_per_col - 1))
                    this_tllength = tllength;
                else
                    this_tllength = last_length;
                for (rowtile = 0; rowtile < tiles_per_row; rowtile++)
                {
                    memset(pbufLine, 0x1e, bufLen);
                    if (rowtile < (tiles_per_row - 1))
                        this_tlwidth = tlwidth;
                    else
                        this_tlwidth = last_width;
                    j = 0;
                    for (row = 0; row < this_tllength; row++)
                    {
                        /* Fill tile buffer. */
                        for (k = 0; (k < this_tlwidth); k++)
                        {
                            /* CONTIG:   All three components are written
                             * continouosly. SEPARATE: Only one component is
                             * written, according to s=0..2 */
                            if (l_planarconfig == PLANARCONFIG_CONTIG || s == 0)
                                pbufLine[j++] =
                                    (unsigned char)((row + coltile * tllength) %
                                                    bpsmod); /* row */
                            if (l_planarconfig == PLANARCONFIG_CONTIG || s == 1)
                                pbufLine[j++] =
                                    (unsigned char)((k + rowtile * tlwidth) %
                                                    bpsmod); /* column */
                            if (l_planarconfig == PLANARCONFIG_CONTIG || s == 2)
                                pbufLine[j++] = (unsigned char)((254) % bpsmod);
                        }
                        /* Fill rest of row in last tile with zeros. */
                        for (; (k < tlwidth); k++)
                        {
                            if (l_planarconfig == PLANARCONFIG_CONTIG || s == 0)
                                pbufLine[j++] = 0; /* row */
                            if (l_planarconfig == PLANARCONFIG_CONTIG || s == 1)
                                pbufLine[j++] = 0; /* column */
                            if (l_planarconfig == PLANARCONFIG_CONTIG || s == 2)
                                pbufLine[j++] =
                                    //(unsigned char)((0xcc) % bpsmod);
                                    (unsigned char)((0x2c) % bpsmod);
                        }
                    } /* row */
                    /* Calculate tile-number for TIFFWriteEncodedTile().
                     * CONTIG:  s=0; SEPARATE: s=0..2 writing SPP times the
                     * tiles. */
                    i = s * tiles_per_row * tiles_per_col +
                        coltile * tiles_per_row + rowtile;

#ifdef PATCH_CORNER_PIXELS
                    /* Patch some pixels towards  red, green or yellwo for
                     * .ppm images. */
                    uint32_t i_firstTlInLastTileRow, i_lastTlInLastTileRow;
                    /* Get tile number of last tiles */
                    if (l_planarconfig == PLANARCONFIG_CONTIG)
                    {
                        i_firstTlInLastTileRow =
                            tiles_per_row * (tiles_per_col - 1);
                        i_lastTlInLastTileRow =
                            tiles_per_row * tiles_per_col - 1;
                    }
                    else
                    {
                        /* s==1 for green component for SEPARATE*/
                        i_firstTlInLastTileRow =
                            1 * tiles_per_row * tiles_per_col +
                            tiles_per_row * (tiles_per_col - 1);
                        i_lastTlInLastTileRow =
                            1 * tiles_per_row * tiles_per_col +
                            tiles_per_row * tiles_per_col - 1;
                    }
                    if (i == 0)
                    {
                        /* Red for first pixel */
                        pbufLine[0] = 250;
                    }
                    else if (i == i_firstTlInLastTileRow)
                    {
                        /* first pixel of first tile in last row towards green
                         */
                        if (planarconfig == PLANARCONFIG_SEPARATE)
                            pbufLine[0] = 190; /* green tile for SEPARATE */
                        else
                        {
                            pbufLine[1] = 190; /* green component for CONTIG*/
                        }
                    }
                    else if (i == i_lastTlInLastTileRow)
                    {
                        /* last pixel of last tile in last line towards yellow
                         * Be aware of padding of each row.
                         */
                        if (planarconfig == PLANARCONFIG_SEPARATE)
                        {
                            /* towards yellow component for SEPARATE,
                             * for loop s=0..2 */
                            uint32_t offset_lastpixel =
                                (this_tllength - 1) * tlwidth +
                                (this_tlwidth - 1);
                            pbufLine[offset_lastpixel] = 90;
                        }
                        else
                        {
                            /* towards yellow; all components for CONTIG at
                             * once*/
                            uint32_t offset_lastpixel =
                                SPP * ((this_tllength - 1) * tlwidth +
                                       (this_tlwidth - 1));
                            pbufLine[offset_lastpixel + 0] = 90;
                            pbufLine[offset_lastpixel + 1] = 90;
                            pbufLine[offset_lastpixel + 2] = 90;
                        }
                    }
#endif

                    if (TIFFWriteEncodedTile(tif, i, pbufLine, 0) == -1)
                    {
                        fprintf(stdXOut,
                                "Can't write image data tile. Testline %d\n",
                                __LINE__);
                        goto failure;
                    }
                    if ((plastlinedata != NULL) && (i == (numtiles - 1)))
                    {
                        memcpy(plastlinedata, pbufLine,
                               TIFFmin(bufLen, (size_t)lastlinebytesmax));
                    }
                } /* rowtile */
            } /* coltile */
        } /* s sample in pixel */
    }
    else
    {
        /*== STRIP ==*/
        uint32_t i = 0;
        /* Fill image buffer. */
        uint32_t k, j, m;
        uint16_t s;
        if (planarconfig == PLANARCONFIG_CONTIG)
        {
            for (i = 0; i < length; i++)
            {
                for (k = 0, j = 0; k < width && j <= (bufLen - SPP);
                     k++, j += SPP)
                {
                    pbufLine[j] = (unsigned char)((i) % bpsmod); /* row */
                    pbufLine[j + 1] =
                        (unsigned char)((k) % bpsmod); /* column */
                    pbufLine[j + 2] = (unsigned char)((254) % bpsmod);
                }
#ifdef PATCH_CORNER_PIXELS
                /* Patch first pixel in image towards red,
                 * and first pixel of last row towards green. */
                if (i == 0)
                {
                    pbufLine[0] = 250;
                }
                else if (i == length - 1)
                {
                    pbufLine[0] = 190;
                    pbufLine[width - 1] = 90;
                }
#endif
                TIFFWriteScanline_M(tif, pbufLine, i, 0, filename, __LINE__);
            }
        }
        else
        {
            /* SEPARATE - RRRR GGGGG BBBBB, each all lines per strip */
            uint32_t this_strip = 0;
            uint32_t last_length = length % local_rows_per_strip;
            uint32_t i_last_strip_start =
                (length / local_rows_per_strip) * local_rows_per_strip;
            /* Correct values for following processing at special case. */
            if (last_length == 0)
                last_length = local_rows_per_strip;
            for (s = 0; s < samples_per_pixel; s++)
            {
                tmsize_t this_strip_size = strip_size;
                uint32_t this_rows_per_strip = local_rows_per_strip;
                for (i = 0; i < length; i += local_rows_per_strip)
                {
                    if (i >= i_last_strip_start)
                    {
                        this_rows_per_strip = last_length;
                        this_strip_size = scanline_size * last_length;
                    }
                    m = 0;
                    for (j = 0; j < this_rows_per_strip; j++)
                    {
                        for (k = 0; k < width; k++)
                        {
                            if (s == 0)
                                pbufLine[m++] =
                                    (unsigned char)((i + j) % bpsmod); /* row */
                            else if (s == 1)
                                pbufLine[m++] =
                                    (unsigned char)((k) % bpsmod); /* column */
                            else if (s == 2)
                                pbufLine[m++] = (unsigned char)((254) % bpsmod);
                        }
#ifdef PATCH_CORNER_PIXELS
                        /* Patch first pixel in image towards red,
                         * and first pixel of last row towards light red. */
                        if (i == 0 && s == 0 && j == 0)
                        {
                            pbufLine[0] = 250;
                        }
                        else if ((i + j) == length - 1 && s == 0)
                        {
                            pbufLine[m - width] =
                                190;              /* first pixel of last row */
                            pbufLine[m - 1] = 90; /* last pixel of last row */
                        }
#endif
                    } /* for (j to this_rows_per_strip) */
                    tmsize_t ret = TIFFWriteEncodedStrip(
                        tif, this_strip, pbufLine, this_strip_size);
                    if (ret == -1)
                    {
                        fprintf(stdXOut,
                                "Can't write image data separate strip. "
                                "Testline %d\n",
                                __LINE__);
                        goto failure;
                    }
                    this_strip++;
                } /* for (s to samples_per_pixel)   */
            }
        }
        if ((plastlinedata != NULL) && (i == (length - 1)))
        {
            memcpy(plastlinedata, pbufLine,
                   TIFFmin(bufLen, (size_t)lastlinebytesmax));
        }
    } /* -- strip --*/
    _TIFFfree(pbufLine);
    return 0;
failure:
    if (pbufLine != NULL)
        _TIFFfree(pbufLine);
    return 1;
} /*-- write_image_data() --*/

/* Fills the active IFD with some default values and writes
 * an image with given number of lines as strips or tiles to
 * file.
 */
static int write_data_to_current_directory(
    TIFF *tif, uint32_t width, uint32_t length, uint32_t rows_per_strile,
    bool blnTiled, uint16_t orientation, bool blnWriteData,
    unsigned char *plastlinedata, unsigned int lastlinebytesmax)
{
    if (!tif)
    {
        fprintf(stdXOut, "Invalid TIFF handle. Line %d\n", __LINE__);
        return 1;
    }
    char const *const filename = TIFFFileName(tif);
    TIFFSetField_M(tif, TIFFTAG_IMAGEWIDTH, width, filename, __LINE__);
    TIFFSetField_M(tif, TIFFTAG_IMAGELENGTH, length, filename, __LINE__);
    TIFFSetField_M(tif, TIFFTAG_BITSPERSAMPLE, BPS, filename, __LINE__);
    TIFFSetField_M(tif, TIFFTAG_SAMPLESPERPIXEL, SPP, filename, __LINE__);

    if (blnTiled)
    {
        /* Tilesizes must be multiple of 16. */
        TIFFSetField_M(tif, TIFFTAG_TILEWIDTH, rows_per_strile, filename,
                       __LINE__);
        TIFFSetField_M(tif, TIFFTAG_TILELENGTH, rows_per_strile, filename,
                       __LINE__);
    }
    else
    {
        TIFFSetField_M(tif, TIFFTAG_ROWSPERSTRIP, rows_per_strile, filename,
                       __LINE__);
    }

    TIFFSetField_M(tif, TIFFTAG_PLANARCONFIG, planarconfig, filename, __LINE__);
    TIFFSetField_M(tif, TIFFTAG_PHOTOMETRIC, photometric, filename, __LINE__);
    /* Write Orientation tag. */
    TIFFSetField_M(tif, TIFFTAG_ORIENTATION, orientation, filename, __LINE__);

    /* Write pixel data. */
    if (blnWriteData)
    {
        if (write_image_data(tif, width, length, blnTiled, 99, plastlinedata,
                             lastlinebytesmax))
        {
            fprintf(stdXOut, "Can't write image data. Testline %d\n", __LINE__);
            return 1;
        }
    }
    return 0;

failure:
    return 1;
} /*-- write_data_to_current_directory() --*/

/* Some defines for TIFFRGBAImage raster data handling. */
#define A1 (((uint32_t)0xffL) << 24)
#define PACK(r, g, b)                                                          \
    ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16) | A1)

/* Check some contents of the raster buffer. Ensure they are correctly filled.
 */
static int checkRasterContents(char const *txt, TIFFRGBAImage *img,
                               uint32_t *raster, uint32_t rw, uint32_t rh,
                               uint16_t orientation, bool blnReadRGBAStrip)
{
    /* For this test, the image pixel samples are set for R= row, G= column, B=
     * 0xfe and the last raster component is 0xff. The raster is preset with
     * RASTER_BACKGROUND_UINT32VAL.
     * In general, according to documentation:
     * - If the raster height is greater than that of the image, then the image
     *   data are placed in the lower part of the raster.
     * - If the raster width is greater than that of the image, then the image
     *   data are placed in the left part of the raster.
     * With the "requested orientation" the image data are not only flipped but
     * also placed into the requested corner of the raster (i.e. raster origin).
     */
    typedef union
    {
        uint8_t u8[4];
        uint32_t u32;
    } RasterPixel;
    typedef struct
    {
        uint32_t x;
        uint32_t y;
    } Coordinate;

    (void)txt;

    /* Define all parameters here because of CXX and goto compliance
     * (-Wjump-misses-init). */
    uint32_t roff[8];
    RasterPixel a;
    int iStart;
    int iEnd;

#ifdef NO_checkRasterContents
    return 0;
#endif

    /* Exit for special test case */
    if (rh == 0 || rw == 0)
        return 0;

    /* Calculate raster size in bytes for offset checking below. */
    uint64_t rasterSize = sizeof(uint32_t) * rw * rh;

    /* When image is cropped, or raster is larger than image, determine crop
     * lengths for image within raster. */
    uint32_t rwmin = TIFFmin(rw, img->width - (uint32_t)img->col_offset);
    uint32_t rhmin = TIFFmin(rh, img->height - (uint32_t)img->row_offset);
    /* When there is a col_offset or row_offset, the image pixels of the corners
     * are differently. */
    uint32_t xcol_start = (uint32_t)img->col_offset;
    uint32_t xrow_start = (uint32_t)img->row_offset;

    /* Pixel values of image corner pixels according to image dimensions written
     * in write_image_data(). Pixel values are set to (rowIdx, colIdx, 0xfe).
     * The first pixel stored in the file is P0 with I0=(0,0,0xfe);
     * the last pixel of the first data row is P1 with I1=(0,imgWidth-1,0xfe);
     * the first pixel of the last row is P2 with I2=(imgLength-1,0,0xfe) and
     * the last pixel of the last row is P3 with
     * I3=(imgLength-1,imgWidth-1,0xfe). P/I are arranged in Z-order: P0---P1
     *    P2---P3
     * I01 is value of row-0 and col-1 or in image coordinates x=1, y=0.
     */
    RasterPixel I[8];
    /* Without offsets: */
    // I[0].u32 = PACK(0, 0, 0xfe);
    // I[1].u32 = PACK(0, rwmin - 1, 0xfe);
    // I[2].u32 = PACK(rhmin - 1, 0, 0xfe);
    // I[3].u32 = PACK(rhmin - 1, rwmin - 1, 0xfe);
    /* With offsets: */
    I[0].u32 = PACK(xrow_start, xcol_start, 0xfe);
    I[1].u32 = PACK(xrow_start, xcol_start + rwmin - 1, 0xfe);
    I[2].u32 = PACK(xrow_start + rhmin - 1, xcol_start, 0xfe);
    I[3].u32 = PACK(xrow_start + rhmin - 1, xcol_start + rwmin - 1, 0xfe);
    /* The value of the next pixels in the row (or behind) to the pixels P0 th
     * P3 is: P0, P4 --- P1, P5 P2, P6 --- P3, P7
     */
    I[4].u32 = PACK(I[0].u8[0], I[0].u8[1] + 1, 0xfe);
    I[5].u32 = RASTER_BACKGROUND_UINT32VAL;
    I[6].u32 = PACK(I[2].u8[2], I[2].u8[1] + 1, 0xfe);
    I[7].u32 = RASTER_BACKGROUND_UINT32VAL;

    /* Depending on orientation (and requested orientation) the input data are
     * fliped horizontally and / or vertically. Without rotation, this leads to
     * four possible orientations (locations of the first pixel from the image
     * file). See tif_getimage.c setorientation(). If the raster matrix is
     * larger than the image, the image data is stored in:
     *  a) Code till LibTIFF v4.7.2: the lower-left part of the raster matrix.
     *  b) Code from LibTIFF v4.7.3: the corner defined by the requested
     *     orientation of the raster matrix (raster origin).
     * "firstPixelAt" gives the orientation of the data from file, stored in the
     * raster matrix; i.e. the location of the first pixel from the file.
     */

    int firstPixelAt =
        ORIENTATION_TOPLEFT; /* if data from file is not flipped */
    switch (orientation)
    {
        case ORIENTATION_TOPLEFT:
        case ORIENTATION_LEFTTOP:
            if (img->req_orientation == ORIENTATION_TOPRIGHT ||
                img->req_orientation == ORIENTATION_RIGHTTOP)
                firstPixelAt = ORIENTATION_TOPRIGHT; // FLIP_HORIZONTALLY
            else if (img->req_orientation == ORIENTATION_BOTRIGHT ||
                     img->req_orientation == ORIENTATION_RIGHTBOT)
                firstPixelAt = ORIENTATION_BOTRIGHT; // FLIP_HORIZONTALLY |
                                                     // FLIP_VERTICALLY;
            else if (img->req_orientation == ORIENTATION_BOTLEFT ||
                     img->req_orientation == ORIENTATION_LEFTBOT)
                firstPixelAt = ORIENTATION_BOTLEFT; // FLIP_VERTICALLY;
            break;
        case ORIENTATION_TOPRIGHT:
        case ORIENTATION_RIGHTTOP:
            if (img->req_orientation == ORIENTATION_TOPLEFT ||
                img->req_orientation == ORIENTATION_LEFTTOP)
                firstPixelAt = ORIENTATION_TOPRIGHT; // FLIP_HORIZONTALLY;
            else if (img->req_orientation == ORIENTATION_BOTRIGHT ||
                     img->req_orientation == ORIENTATION_RIGHTBOT)
                firstPixelAt = ORIENTATION_BOTLEFT; // FLIP_VERTICALLY;
            else if (img->req_orientation == ORIENTATION_BOTLEFT ||
                     img->req_orientation == ORIENTATION_LEFTBOT)
                firstPixelAt = ORIENTATION_BOTRIGHT; // FLIP_HORIZONTALLY |
                                                     // FLIP_VERTICALLY;
            break;
        case ORIENTATION_BOTRIGHT:
        case ORIENTATION_RIGHTBOT:
            if (img->req_orientation == ORIENTATION_TOPLEFT ||
                img->req_orientation == ORIENTATION_LEFTTOP)
                firstPixelAt = ORIENTATION_BOTRIGHT; // FLIP_HORIZONTALLY |
                                                     // FLIP_VERTICALLY;
            else if (img->req_orientation == ORIENTATION_TOPRIGHT ||
                     img->req_orientation == ORIENTATION_RIGHTTOP)
                firstPixelAt = ORIENTATION_BOTLEFT; // FLIP_VERTICALLY;
            else if (img->req_orientation == ORIENTATION_BOTLEFT ||
                     img->req_orientation == ORIENTATION_LEFTBOT)
                firstPixelAt = ORIENTATION_TOPRIGHT; // FLIP_HORIZONTALLY;
            break;
        case ORIENTATION_BOTLEFT:
        case ORIENTATION_LEFTBOT:
            if (img->req_orientation == ORIENTATION_TOPLEFT ||
                img->req_orientation == ORIENTATION_LEFTTOP)
                firstPixelAt = ORIENTATION_BOTLEFT; // FLIP_VERTICALLY;
            else if (img->req_orientation == ORIENTATION_TOPRIGHT ||
                     img->req_orientation == ORIENTATION_RIGHTTOP)
                firstPixelAt = ORIENTATION_BOTRIGHT; // FLIP_HORIZONTALLY |
                                                     // FLIP_VERTICALLY;
            else if (img->req_orientation == ORIENTATION_BOTRIGHT ||
                     img->req_orientation == ORIENTATION_RIGHTBOT)
                firstPixelAt = ORIENTATION_TOPRIGHT; // FLIP_HORIZONTALLY;
            break;
        default: /* NOTREACHED */
            break;
    }

    /* Determine position in raster-matrix (0..rw-1 / 0..rh-1)
     * If the raster matrix is larger than the image, the image data is stored:
     * a) Code till LibTIFF v4.7.2: in the lower-left part of the raster matrix.
     * b) Code from LibTIFF v4.7.3: in the corner defined by the requested
     *    orientation of the raster matrix (raster origin).
     * P0 to P3 are image corner in Z-order sequence within raster matrix:
     *    P0---P1
     *    P2---P3
     */
    Coordinate P[8];
    Coordinate Pbase[8];
#define SETCoordinate(P, idx, xv, yv)                                          \
    P[idx].x = xv;                                                             \
    P[idx].y = yv;

    /* req_orientation defines raster origin
     * (corner coordinates in which image is located)
     * An exception is ReadRGBAStrip(), which places the image data always at
     * the beginning of the raster buffer (i.e. position TOPLEFT),
     * but does flip the image itself.
     */
    if (blnReadRGBAStrip)
    {
        /* always TOPLEFT corner */
        SETCoordinate(Pbase, 0, 0, 0);
        SETCoordinate(Pbase, 1, rwmin - 1, 0);
        SETCoordinate(Pbase, 2, 0, rhmin - 1);
        SETCoordinate(Pbase, 3, rwmin - 1, rhmin - 1);
    }
    else
    {
        switch (img->req_orientation)
        {
            case ORIENTATION_TOPLEFT:
            case ORIENTATION_LEFTTOP:
                SETCoordinate(Pbase, 0, 0, 0);
                SETCoordinate(Pbase, 1, rwmin - 1, 0);
                SETCoordinate(Pbase, 2, 0, rhmin - 1);
                SETCoordinate(Pbase, 3, rwmin - 1, rhmin - 1);
                break;
            case ORIENTATION_TOPRIGHT:
            case ORIENTATION_RIGHTTOP:
                SETCoordinate(
                    Pbase, 0,
                    (uint32_t)TIFFmax(0, (tmsize_t)rw - (tmsize_t)rwmin), 0);
                SETCoordinate(Pbase, 1, rw - 1, 0);
                SETCoordinate(
                    Pbase, 2,
                    (uint32_t)TIFFmax(0, (tmsize_t)rw - (tmsize_t)rwmin),
                    rhmin - 1);
                SETCoordinate(Pbase, 3, rw - 1, rhmin - 1);
                break;
            case ORIENTATION_BOTRIGHT:
            case ORIENTATION_RIGHTBOT:
                SETCoordinate(
                    Pbase, 0,
                    (uint32_t)TIFFmax(0, (tmsize_t)rw - (tmsize_t)rwmin),
                    (uint32_t)TIFFmax(0, (tmsize_t)rh - (tmsize_t)rhmin));
                SETCoordinate(
                    Pbase, 1, rw - 1,
                    (uint32_t)TIFFmax(0, (tmsize_t)rh - (tmsize_t)rhmin));
                SETCoordinate(
                    Pbase, 2,
                    (uint32_t)TIFFmax(0, (tmsize_t)rw - (tmsize_t)rwmin),
                    rh - 1);
                SETCoordinate(Pbase, 3, rw - 1, rh - 1);
                break;
            case ORIENTATION_BOTLEFT:
            case ORIENTATION_LEFTBOT:
                SETCoordinate(
                    Pbase, 0, 0,
                    (uint32_t)TIFFmax(0, (tmsize_t)rh - (tmsize_t)rhmin));
                SETCoordinate(
                    Pbase, 1, rwmin - 1,
                    (uint32_t)TIFFmax(0, (tmsize_t)rh - (tmsize_t)rhmin));
                SETCoordinate(Pbase, 2, 0, rh - 1);
                SETCoordinate(Pbase, 3, rwmin - 1, rh - 1);
                break;
            default: /* NOTREACHED */
                fprintf(stdXOut,
                        "Error in SWITCH statement requested-orientation at "
                        "line %d",
                        __LINE__);
                goto failure;
                break;
        }
    }

    /* firstPixelAt defines pixel Z-order sequence
     * and thus location in raster from raster origin. */
    switch (firstPixelAt)
    {
        case ORIENTATION_TOPLEFT:
            P[0] = Pbase[0];
            P[1] = Pbase[1];
            P[2] = Pbase[2];
            P[3] = Pbase[3];
            break;
        case ORIENTATION_TOPRIGHT:
            P[0] = Pbase[1];
            P[1] = Pbase[0];
            P[2] = Pbase[3];
            P[3] = Pbase[2];
            break;
        case ORIENTATION_BOTRIGHT:
            P[0] = Pbase[3];
            P[1] = Pbase[2];
            P[2] = Pbase[1];
            P[3] = Pbase[0];
            break;
        case ORIENTATION_BOTLEFT:
            P[0] = Pbase[2];
            P[1] = Pbase[3];
            P[2] = Pbase[0];
            P[3] = Pbase[1];
            break;
        default: /* NOTREACHED */
            fprintf(stdXOut,
                    "Error in SWITCH statement img-orientation at line %d",
                    __LINE__);
            goto failure;
            break;
    }

        /* Determine offset of P[i] into the raster of uint32_t into roff[8]: */
        /* Check offset to be inside raster. */
#define NUMP 4
    for (int i = 0; i < NUMP; i++)
    {
        roff[i] = P[i].y * rw + P[i].x;
        if (roff[i] >= rasterSize)
        {
            fprintf(stdXOut,
                    "Error: Offset into raster buffer exceeds raster size "
                    "- at line %d",
                    __LINE__);
            goto failure;
        }
    }

    /* Check value of corner pixels at expected location in raster. */
    a.u32 = 0;
    iStart = 0;
    iEnd = NUMP;
    /* If first and last pixel is patched, then skip comparison. */
#ifdef PATCH_CORNER_PIXELS
    iStart = 1;
    iEnd = 2;
#endif
    for (int i = iStart; i < iEnd; i++)
    {
        a.u32 = raster[roff[i]];
        if (a.u32 != I[i].u32)
        {
            fprintf(stdXOut,
                    "\nPixel value of P%d = (%d, %d, %d)/(%02x, %02x, %02x) in "
                    "raster at offset %u does not match expected value "
                    "(%d, "
                    "%d, %d)/(%02x, %02x, %02x)",
                    i, a.u8[0], a.u8[1], a.u8[2], a.u8[0], a.u8[1], a.u8[2],
                    roff[i], I[i].u8[0], I[i].u8[1], I[i].u8[2], I[i].u8[0],
                    I[i].u8[1], I[i].u8[2]);
            GOTOFAILURE
        }
    }
    return 0;

failure:
    return 1;
} /*-- checkRasterContents() --*/

/* Prints the raster buffer 2D matrix as hex to display and/or to file.
 * Global blnPrintRasterToScreen defines output to display and
 * the global logFilename == NULL suppresses output to file. */
static void printRaster(char const *txt, TIFFRGBAImage *img, uint32_t *raster,
                        uint32_t rw, uint32_t rh, uint16_t orientation,
                        bool blnTiled)
{
    uint32_t h, w;
    FILE *fp = NULL;
    union
    {
        uint8_t u8[4];
        uint32_t u32;
    } u;
    char straux[2048];

    /* If fpLog is already open, don't open and close that file. */
    if (fpLog != NULL)
        fp = fpLog;
    else if (logFilename != NULL)
        fp = fopen(logFilename, "a");

    if (img->col_offset != 0 || img->row_offset != 0)
    {
        sprintf(straux,
                "\n--- (%3u /%3u) Orientation = %d (%s) %s, %s readWidth = %u, "
                "readLength = %u, col_off = %d, row_off = %d, "
                "req_orientation=%d (%s) using %s---\n",
                img->width, img->height, orientation,
                orientationStrings[checkedOrientation(orientation)],
                planarconfig == 1 ? "CONTIG" : "SEPARATE",
                blnTiled ? "TILED" : "STRIP", rw, rh, img->col_offset,
                img->row_offset, img->req_orientation,
                orientationStrings[checkedOrientation(img->req_orientation)],
                txt);
    }
    else
    {
        sprintf(straux,
                "\n--- (%3u /%3u) Orientation = %d (%s) %s, %s readWidth = %u, "
                "readLength = %u, req_orientation=%d (%s) using "
                "%s---\n",
                img->width, img->height, orientation,
                orientationStrings[checkedOrientation(orientation)],
                planarconfig == 1 ? "CONTIG" : "SEPARATE",
                blnTiled ? "TILED" : "STRIP", rw, rh, img->req_orientation,
                orientationStrings[checkedOrientation(img->req_orientation)],
                txt);
    }
    if (fp != NULL)
        fprintf(fp, "%s", straux);
    if (blnPrintRasterToScreen)
        fprintf(stdout, "%s", straux);
    for (h = 0; h < rh; h++)
    {
        for (w = 0; w < rw; w++)
        {
            u.u32 = raster[h * rw + w];
            sprintf(straux, "%02x %02x %02x %02x ", u.u8[0], u.u8[1], u.u8[2],
                    u.u8[3]);
            if (fp != NULL)
                fprintf(fp, "%s", straux);
            if (blnPrintRasterToScreen)
                fprintf(stdout, "%s", straux);
        }
        sprintf(straux, "\n");
        if (fp != NULL)
            fprintf(fp, "%s", straux);
        if (blnPrintRasterToScreen)
            fprintf(stdout, "%s", straux);
    }
    sprintf(straux, "--------------\n");
    if (fp != NULL)
    {
        fprintf(fp, "%s", straux);
        if (fpLog == NULL)
            fclose(fp);
    }
    if (blnPrintRasterToScreen)
        fprintf(stdout, "%s", straux);
    return;
} /*-- printRaster() --*/

/* Calls TIFFRGBAImageGet() with col_offset and row_offset and with a given
 * raster and reads the data from file into that raster. The raster content is
 * printed and checked.
 */
static int testRGBAImageReadWithOffsets(TIFF *tif, uint32_t imgWidth,
                                        uint32_t imgLength, int w_offset,
                                        int l_offset, uint32_t rWidth,
                                        uint32_t rHeight, uint16_t orientation,
                                        uint16_t req_orientation,
                                        uint32_t cLine)
{
    int ret;
    uint32_t *raster1 = NULL;
    char emsg[1024] = "";
    TIFFRGBAImage img;
    int ok;
    uint32_t rSizeWidth = rWidth;
    uint32_t rSizeHeight = rHeight;

    /* Just for debugging output in printRaster() */
    bool blnTiledLocal = TIFFIsTiled(tif);

    tmsize_t rasterSize =
        (tmsize_t)(sizeof(uint32_t) * (size_t)rWidth * (size_t)rHeight);
    if (rasterSize == 0)
    {
        rasterSize =
            (tmsize_t)(sizeof(uint32_t) * (size_t)imgWidth * (size_t)imgLength);
        rSizeWidth = imgWidth;
        rSizeHeight = imgLength;
    }
    raster1 = (uint32_t *)_TIFFmalloc(rasterSize);
    if (raster1 == NULL)
    {
        fprintf(stdXOut,
                "Can't allocate 'raster'-buffer. Testline %d called from %u\n",
                __LINE__, cLine);
        goto failure;
    }
    for (uint32_t i = 0; i < rSizeWidth * rSizeHeight; i++)
    {
        /* Initialize dark green background */
        raster1[i] = RASTER_BACKGROUND_UINT32VAL;
    }

    if (TIFFRGBAImageBegin(&img, tif, 0, emsg))
    {
        img.req_orientation = req_orientation;
        img.col_offset = w_offset;
        img.row_offset = l_offset;
        ok = TIFFRGBAImageGet(&img, raster1, rSizeWidth, rSizeHeight);
        TIFFRGBAImageEnd(&img);
        if (ok != 1)
        {
            if (!blnQuiet)
                fprintf(stdXOut,
                        "TIFFRGBAImageGet() returned failure in test with "
                        "offsets. Testline %d called from %u\n",
                        __LINE__, cLine);
            GOTOFAILURE
        }
        else
        {
            if ((ret = checkRasterContents("TIFFRGBAImageGet()", &img, raster1,
                                           rSizeWidth, rSizeHeight, orientation,
                                           false)))
            {
                GOTOFAILURE
            }
        }
        printRaster("TIFFRGBAImageGet()", &img, raster1, rSizeWidth,
                    rSizeHeight, orientation, blnTiledLocal);
#ifdef PRINTTO_PPM
        if ((ret = printRasterToPPMFile(TIFFFileName(tif), &img, raster1,
                                        rSizeWidth, rSizeHeight)))
        {
            GOTOFAILURE
        }
#endif
    }
    _TIFFfree(raster1);
    return 0;

failure:
    if (raster1)
        _TIFFfree(raster1);
    return 1;
} /*-- testRGBAImageReadWithOffsets() --*/

/* Tests TIFFReadRGBAImage functions with different raster sizes, col_offset,
 * row_offset and requested orientations in the raster.
 */
static int test_ReadRGBAImage(int ntest, unsigned int openMode,
                              uint16_t orientation, uint32_t width,
                              uint32_t length, bool blnTiled,
                              uint16_t req_orientation)
{

    char filename[128] = {0};
    int ret;
    TIFFErrorHandler errHandler = NULL;
    TIFFErrorHandler warnHandler = NULL;

    assert(openMode < (sizeof(modeStrings) / sizeof(modeStrings[0])));
    assert(orientation <
           (sizeof(orientationStrings) / sizeof(orientationStrings[0])));
    blnQuiet = FALSE;

#ifdef DEBUG_TESTING
    fprintf(stdXOut,
            "\n==== test_ReadRGBAImage() - sequence --- Orientation = %u (%s) "
            "to %u (%s) %s, %s ====\n",
            orientation, orientationStrings[checkedOrientation(orientation)],
            req_orientation,
            orientationStrings[checkedOrientation(req_orientation)],
            planarconfig == 1 ? "CONTIG" : "SEPARATE",
            blnTiled ? "TILED" : "STRIP");
#else
    fprintf(stdXOut, ".");
#endif
    /*-- Create a file with appropriate filename --*/
#ifndef SHORT_AMBIGUOUS_FILENAMES
    sprintf(filename, "test_RGBAImage_%02d_%s_%s_%s_%s-%s_i%02u_%02u.tif",
            ntest, modeStrings[openMode], (blnTiled ? "TL" : "ST"),
            planarconfig == 1 ? "CONTIG" : "SEPARATE",
            orientationStrings[checkedOrientation(orientation)],
            orientationStrings[checkedOrientation(req_orientation)], width,
            length);
#else
    sprintf(filename, "test_RGBAImage__%s-%s_i%02u_%02u.tif",
            orientationStrings[checkedOrientation(orientation)],
            orientationStrings[checkedOrientation(req_orientation)], width,
            length);
#endif

    TIFF *tif = TIFFOpen(filename, modeStrings[openMode]);
    if (!tif)
    {
        fprintf(stdXOut, "\nCan't create %s. Testline %d\n", filename,
                __LINE__);
        return 1;
    }

    /* Writing baseline images (IFDs) to file. */
    if (write_data_to_current_directory(tif, width, length, rows_per_strip,
                                        blnTiled, orientation, true, NULL, 0))
    {
        fprintf(stdXOut,
                "\nCan't write data to current directory in %s. Testline %d\n",
                filename, __LINE__);
        goto failure;
    }
    /* Write IFD tags to file (and setup new empty IFD). */
    TIFFWriteDirectory_M(tif, filename, __LINE__);
    TIFFClose(tif);
    TIFFOpen_M(tif, filename, "r", __LINE__);

/*--- Disable Warning Handler for "cannot rotate" warnings ---*/
#ifndef DO_NOT_DISABLE_WARNINGS_AND_ERRORS
    if (((req_orientation <= ORIENTATION_BOTLEFT) &&
         (orientation >= ORIENTATION_LEFTTOP)) ||
        ((req_orientation >= ORIENTATION_LEFTTOP) &&
         (orientation <= ORIENTATION_BOTLEFT)))
    {
        warnHandler = TIFFSetWarningHandler(NULL);
    }
#endif

    /*=== Test basic function TIFFRGBAImageGet() ===*/
    /* Full image */
    if ((ret = testRGBAImageReadWithOffsets(tif, width, length, 0, 0, width,
                                            length, orientation,
                                            req_orientation, __LINE__)))
    {
        GOTOFAILURE
    }

    /* More lines - result will be different for NewCode - */
    if ((ret = testRGBAImageReadWithOffsets(tif, width, length, 0, 0, width,
                                            length + 2, orientation,
                                            req_orientation, __LINE__)))
    {
        GOTOFAILURE
    }

    /* Less lines */
    if ((ret = testRGBAImageReadWithOffsets(tif, width, length, 0, 0, width,
                                            length - 1, orientation,
                                            req_orientation, __LINE__)))
    {
        GOTOFAILURE
    }

    /* Less columns */
    if ((ret = testRGBAImageReadWithOffsets(tif, width, length, 0, 0, width - 3,
                                            length, orientation,
                                            req_orientation, __LINE__)))
    {
        GOTOFAILURE
    }

    /* Less lines and less columns */
    if ((ret = testRGBAImageReadWithOffsets(tif, width, length, 0, 0, width - 5,
                                            length - 1, orientation,
                                            req_orientation, __LINE__)))
    {
        GOTOFAILURE
    }

#ifdef WITH_WIDER_RASTER
    /* More columns. */
    if ((ret = testRGBAImageReadWithOffsets(tif, width, length, 0, 0, width + 2,
                                            length, orientation,
                                            req_orientation, __LINE__)))
    {
        GOTOFAILURE
    }

    /* More rows and columns. */
    if ((ret = testRGBAImageReadWithOffsets(tif, width, length, 0, 0, width + 2,
                                            length + 2, orientation,
                                            req_orientation, __LINE__)))
    {
        GOTOFAILURE
    }
    // goto leave_function;
#endif

    /*-- Test an invalid raster size --*/
    if ((ret = testRGBAImageReadWithOffsets(tif, width, length, 0, 0, width, 0,
                                            orientation, req_orientation,
                                            __LINE__)))
    {
        GOTOFAILURE
    }
    if ((ret = testRGBAImageReadWithOffsets(tif, width, length, 0, 0, 0, length,
                                            orientation, req_orientation,
                                            __LINE__)))
    {
        GOTOFAILURE
    }
    if ((ret = testRGBAImageReadWithOffsets(tif, width, length, 0, 0, 0, 0,
                                            orientation, req_orientation,
                                            __LINE__)))
    {
        GOTOFAILURE
    }

    /*=== Testing reading with OFFSETs in the image file; raster can then also
     * be smaller ===*/
    /*-- row_offset --*/
    if ((ret = testRGBAImageReadWithOffsets(
             tif, width, length, 0, (int)(length - 1), width, length,
             orientation, req_orientation, __LINE__)))
    {
        GOTOFAILURE
    }
    if ((ret = testRGBAImageReadWithOffsets(
             tif, width, length, 0, (int)(length - 4), width, 4, orientation,
             req_orientation, __LINE__)))
    {
        GOTOFAILURE
    }
    if ((ret = testRGBAImageReadWithOffsets(
             tif, width, length, 0, (int)(length - 4), width, 2, orientation,
             req_orientation, __LINE__)))
    {
        GOTOFAILURE
    }

    /*-- col_offset --*/
    if ((ret = testRGBAImageReadWithOffsets(
             tif, width, length, (int)(width - 2), 0, width, length,
             orientation, req_orientation, __LINE__)))
    {
        GOTOFAILURE
    }
    if ((ret = testRGBAImageReadWithOffsets(
             tif, width, length, (int)(width - 2), 0, 2, length, orientation,
             req_orientation, __LINE__)))
    {
        GOTOFAILURE
    }
    if ((ret = testRGBAImageReadWithOffsets(
             tif, width, length, (int)(width - 3), 0, 2, length, orientation,
             req_orientation, __LINE__)))
    {
        GOTOFAILURE
    }

    /*-- row_offset and col_offset --*/
    if ((ret = testRGBAImageReadWithOffsets(
             tif, width, length, (int)(width - 4), (int)(length - 2), width,
             length, orientation, req_orientation, __LINE__)))
    {
        GOTOFAILURE
    }

    /*-- For tiled images read last tile into a raster of full tile size (padded
     * lines and cols).
     * See https://gitlab.com/libtiff/libtiff/-/issues/791
     * --*/
    if (blnTiled)
    {
        uint32_t tile_width, tile_length;
        if (!TIFFGetField(tif, TIFFTAG_TILELENGTH, &tile_length) ||
            !TIFFGetField(tif, TIFFTAG_TILEWIDTH, &tile_width))
        {
            fprintf(stdXOut,
                    "\nError: cannot get tile width and tile length in %s."
                    "Testline %d\n ",
                    filename, __LINE__);
            goto failure;
        }
        if (width > tile_width && length > tile_length)
        {
            /* calculate offset to last tile bottom-left */
            uint32_t w_offset, l_offset;
            w_offset = 0;
            l_offset =
                (length - ((length % tile_length) ? (length % tile_length)
                                                  : tile_length));
            if (w_offset > INT_MAX || l_offset > INT_MAX)
            {
                fprintf(stdXOut,
                        "\nError: Value of w_offset or l_offset exceeds (int) "
                        "size in %s. Testline %d\n ",
                        filename, __LINE__);
                goto failure;
            }

            if ((ret = testRGBAImageReadWithOffsets(
                     tif, width, length, (int)w_offset, (int)l_offset,
                     tile_width, tile_length, orientation, req_orientation,
                     __LINE__)))
            {
                GOTOFAILURE
            }

            /* calculate offset to last tile bottom-right */
            w_offset = width - ((width % tile_width) ? (width % tile_width)
                                                     : tile_width);
            /* raster = tile-size */
            if (w_offset > INT_MAX || l_offset > INT_MAX)
            {
                fprintf(stdXOut,
                        "\nError: Value of w_offset or l_offset exceeds (int) "
                        "size in %s. Testline %d\n ",
                        filename, __LINE__);
                goto failure;
            }
            if ((ret = testRGBAImageReadWithOffsets(
                     tif, width, length, (int)w_offset, (int)l_offset,
                     tile_width, tile_length, orientation, req_orientation,
                     __LINE__)))
            {
                GOTOFAILURE
            }
#ifdef WITH_WIDER_RASTER
            /* raster bigger than tile-size
             * - reading of lines should be limited to those of the
             *   padding lines. */
            if ((ret = testRGBAImageReadWithOffsets(
                     tif, width, length, (int)w_offset, (int)l_offset,
                     tile_width + 5, tile_length + 5, orientation,
                     req_orientation, __LINE__)))
            {
                GOTOFAILURE
            }
#endif
        }
    }

    /* Here are some tests which are expected to fail. Suppress warning messages
     * for them. */
    blnQuiet = TRUE;
#ifndef DO_NOT_DISABLE_WARNINGS_AND_ERRORS
    errHandler = TIFFSetErrorHandler(NULL);
    if (warnHandler == NULL) /* was not disabled before */
        warnHandler = TIFFSetWarningHandler(NULL);
#endif

    /*-- row_offset --*/
    if (!(ret = testRGBAImageReadWithOffsets(tif, width, length, 0, (int)length,
                                             width, length, orientation,
                                             req_orientation, __LINE__)))
    {
        GOTOFAILURE
    }
    if (!(ret = testRGBAImageReadWithOffsets(
              tif, width, length, 0, (int)(length + 5), width, length,
              orientation, req_orientation, __LINE__)))
    {
        GOTOFAILURE
    }
    if (!(ret = testRGBAImageReadWithOffsets(tif, width, length, 0, -10, width,
                                             length, orientation,
                                             req_orientation, __LINE__)))
    {
        GOTOFAILURE
    }

    /*-- col_offset --*/
    if (!(ret = testRGBAImageReadWithOffsets(tif, width, length, (int)width, 0,
                                             width, length, orientation,
                                             req_orientation, __LINE__)))
    {
        GOTOFAILURE
    }
    if (!(ret = testRGBAImageReadWithOffsets(
              tif, width, length, (int)(width + 5), 0, width, length,
              orientation, req_orientation, __LINE__)))
    {
        GOTOFAILURE
    }
    if (!(ret = testRGBAImageReadWithOffsets(tif, width, length, -15, 0, width,
                                             length, orientation,
                                             req_orientation, __LINE__)))
    {
        GOTOFAILURE
    }

    /*-- row_offset and col_offset --*/
    if (!(ret = testRGBAImageReadWithOffsets(tif, width, length, -20, -30,
                                             width, length, orientation,
                                             req_orientation, __LINE__)))
    {
        GOTOFAILURE
    }

    /*-- Leaving function --*/
    if (errHandler != NULL)
        TIFFSetErrorHandler(errHandler);
    if (warnHandler != NULL)
        TIFFSetWarningHandler(warnHandler);
    TIFFClose(tif);
#ifndef DEBUG_TESTING
    unlink(filename);
#endif
    return 0;

failure:
    if (errHandler != NULL)
        TIFFSetErrorHandler(errHandler);
    if (warnHandler != NULL)
        TIFFSetWarningHandler(warnHandler);
    if (tif)
        TIFFClose(tif);
    return 1;
} /*-- test_ReadRGBAImage() --*/

/* Tests TIFFReadRGBAStrip() function with different raster sizes, row_offset,
 * and row_per_strip values.
 */
static int test_ReadRGBAStrip(int ntest, unsigned int openMode,
                              uint16_t orientation, uint32_t imgWidth,
                              uint32_t imgLength, uint32_t rows_p_strip,
                              bool blnReadLastStrip, uint32_t cLine)
{

    char filename[128] = {0};
    int ret;
    TIFFErrorHandler errHandler = NULL;
    TIFFErrorHandler warnHandler = NULL;
    /* Define all parameters here because of CXX and goto compliance
     * (-Wjump-misses-init). */
    uint32_t rows_p_strip_file;
    uint64_t stripSize;
    uint32_t *raster1 = NULL;
    uint32_t rSizeWidth;
    uint32_t rSizeHeight;
    tmsize_t rasterSize;
    TIFFRGBAImage img;
    uint32_t row_start;

    assert(openMode < (sizeof(modeStrings) / sizeof(modeStrings[0])));
    assert(orientation <
           (sizeof(orientationStrings) / sizeof(orientationStrings[0])));
    blnQuiet = FALSE;

#ifdef DEBUG_TESTING
    fprintf(stdXOut,
            "\n==== test_ReadRGBAStrip() - sequence --- Orientation = %d (%s) "
            "%s ====\n",
            orientation, orientationStrings[checkedOrientation(orientation)],
            planarconfig == 1 ? "CONTIG" : "SEPARATE");
#else
    fprintf(stdXOut, ".");
#endif

    /*-- Create a file with appropriate filename --*/
#ifndef SHORT_AMBIGUOUS_FILENAMES
    sprintf(filename, "test_ReadRGBAStrip_%02d_%s_%s_%s_%s_i%02u_%02u.tif",
            ntest, modeStrings[openMode],
            planarconfig == 1 ? "CONTIG" : "SEPARATE",
            orientationStrings[checkedOrientation(orientation)],
            blnReadLastStrip ? "LastST" : "FirstST", imgWidth, imgLength);
#else
    sprintf(filename, "test_ReadRGBAStrip_%s_%s_i%02u_%02u.tif",
            orientationStrings[checkedOrientation(orientation)],
            blnReadLastStrip ? "LastST" : "FirstST", imgWidth, imgLength);
#endif

    /*-- Create a file and write numIFDs IFDs directly to it --*/
    TIFF *tif = TIFFOpen(filename, modeStrings[openMode]);
    if (!tif)
    {
        fprintf(stdXOut, "\nCan't create %s. Testline %d called from %u\n",
                filename, __LINE__, cLine);
        return 1;
    }

    /* Writing baseline images (IFDs) to file. */
    if (write_data_to_current_directory(tif, imgWidth, imgLength, rows_p_strip,
                                        FALSE, orientation, true, NULL, 0))
    {
        fprintf(stdXOut,
                "\nCan't write data to current directory in %s. Testline %d  "
                "called from %u\n",
                filename, __LINE__, cLine);
        goto failure;
    }

    /* Write IFD tags to file (and setup new empty IFD). */
    TIFFWriteDirectory_M(tif, filename, __LINE__);
    TIFFClose(tif);

    /*=== Test basic function TIFFReadRGBAStrip() ===*/
    TIFFOpen_M(tif, filename, "r", __LINE__);
    /*-------
    TIFFReadRGBAStrip() reads a single strip of a strip-based image into
    memory, storing the result in the user supplied RGBA raster. The raster is
    assumed to be an array of ImageWidth * RowsPerStrip 32-bit entries, where
    ImageWidth is the width of the image (TIFFTAG_IMAGEWIDTH) and RowsPerStrip
    is the maximum lines in a strip (TIFFTAG_ROWSPERSTRIP).

    TIFFReadRGBAStripExt() provides the parameter stop_on_error. Its behaviour
    is described at TIFFReadRGBAImage.

    The row value should be the row of the first row in the strip (strip x
    RowsPerStrip, zero based).

    Note that the raster is assume to be organized such that the pixel at
    location (x, y) is raster [ y * width + x ]; with the raster origin in the
    lower-left hand corner of the strip. That is bottom to top organization.
    When reading a partial last strip in the file the last line of the image
    will begin at the beginning of the buffer.
    --------*/

    TIFFGetField_M(tif, TIFFTAG_ROWSPERSTRIP, &rows_p_strip_file, filename,
                   __LINE__);
    if (rows_p_strip_file != rows_p_strip)
    {
        fprintf(stdXOut,
                "Rows per strip in file %u differs from parameter rows_p_strip "
                "%u . Testline %d called from %u\n",
                rows_p_strip_file, rows_p_strip, __LINE__, cLine);
        goto failure;
    }
    /* For strip mode get size of a strip in bytes. */
    stripSize = TIFFStripSize64(tif); // debugging
    (void)stripSize; // debugging avoid compiler warning unused parameter

    raster1 = NULL;
    rSizeWidth = imgWidth;
    rSizeHeight = rows_p_strip; // max(imgLength, rows_p_strip);

    rasterSize =
        (tmsize_t)(sizeof(uint32_t) * (size_t)rSizeWidth * (size_t)rSizeHeight);
    raster1 = (uint32_t *)_TIFFmalloc(rasterSize);
    if (raster1 == NULL)
    {
        fprintf(stdXOut,
                "Can't allocate 'raster'-buffer. Testline %d called from %u\n",
                __LINE__, cLine);
        goto failure;
    }
    for (uint32_t i = 0; i < rSizeWidth * rSizeHeight; i++)
    {
        /* Initialize dark green background */
        raster1[i] = RASTER_BACKGROUND_UINT32VAL;
    }

    /*--- Disable Warning Handler for "cannot rotate" warnings ---*/
#ifndef DO_NOT_DISABLE_WARNINGS_AND_ERRORS
    if (orientation >= ORIENTATION_LEFTTOP)
    {
        warnHandler = TIFFSetWarningHandler(NULL);
    }
#endif

    /* Read first or last strip. */
    row_start = 0;
    if (blnReadLastStrip)
    {
        row_start = (imgLength / rows_p_strip) * rows_p_strip;
    }
    /* --- Set parameter for printRaster() and checkRasterContents(), which use
     * img-struct --*/
    img.width = imgWidth;
    img.height = imgLength;
    img.col_offset = 0;
    img.row_offset = (int)row_start;
    /* Set default req_orientation as it used by TIFFReadRGBAStrip(). */
    img.req_orientation = ORIENTATION_BOTLEFT;

    ret = TIFFReadRGBAStrip(tif, row_start, raster1);
    if (!ret)
    {
        if (!blnQuiet)
            fprintf(stdXOut,
                    "TIFFReadRGBAStrip() returned failure. Testline %d called "
                    "from %u\n",
                    __LINE__, cLine);
        GOTOFAILURE
    }
    else
    {
        if ((ret = checkRasterContents("TIFFReadRGBAStrip()", &img, raster1,
                                       rSizeWidth, rSizeHeight, orientation,
                                       true)))
        {
            GOTOFAILURE
        }
    }

    printRaster("TIFFReadRGBAStrip()", &img, raster1, rSizeWidth, rSizeHeight,
                orientation, TRUE);
#ifdef PRINTTO_PPM
    if ((ret = printRasterToPPMFile(TIFFFileName(tif), &img, raster1,
                                    rSizeWidth, rSizeHeight)))
    {
        GOTOFAILURE
    }
#endif

    /*-- Leaving function --*/
    if (errHandler != NULL)
        TIFFSetErrorHandler(errHandler);
    if (warnHandler != NULL)
        TIFFSetWarningHandler(warnHandler);
    TIFFClose(tif);
#ifndef DEBUG_TESTING
    unlink(filename);
#endif
    return 0;

failure:
    if (errHandler != NULL)
        TIFFSetErrorHandler(errHandler);
    if (warnHandler != NULL)
        TIFFSetWarningHandler(warnHandler);
    if (tif)
        TIFFClose(tif);
    return 1;
} /*-- test_ReadRGBAStrip() --*/

/* Tests TIFFReadRGBATile() function.
 */
static int test_ReadRGBATile(int ntest, unsigned int openMode,
                             uint16_t orientation, uint32_t imgWidth,
                             uint32_t imgLength, bool blnReadLastTile,
                             uint32_t cLine)
{

    char filename[128] = {0};
    int ret;
    TIFFErrorHandler errHandler = NULL;
    TIFFErrorHandler warnHandler = NULL;
    /* Define all parameters here because of CXX and goto compliance
     * (-Wjump-misses-init). */
    uint32_t tlwidth = 0;
    uint32_t tllength = 0;
    tmsize_t tlsize;
    uint32_t *raster1 = NULL;
    uint32_t rSizeWidth;
    uint32_t rSizeHeight;
    tmsize_t rasterSize;
    TIFFRGBAImage img;
    uint32_t aux1, aux2;

    assert(openMode < (sizeof(modeStrings) / sizeof(modeStrings[0])));
    assert(orientation <
           (sizeof(orientationStrings) / sizeof(orientationStrings[0])));
    blnQuiet = FALSE;

#ifdef DEBUG_TESTING
    fprintf(stdXOut,
            "\n==== test_ReadRGBATile() - sequence --- Orientation = %d (%s) "
            "%s ====\n",
            orientation, orientationStrings[checkedOrientation(orientation)],
            planarconfig == 1 ? "CONTIG" : "SEPARATE");
#else
    fprintf(stdXOut, ".");
#endif

    /*-- Create a file with appropriate filename --*/
#ifndef SHORT_AMBIGUOUS_FILENAMES
    sprintf(filename, "test_ReadRGBATile_%02d_%s_%s_%s_%s_i%02u_%02u.tif",
            ntest, modeStrings[openMode],
            planarconfig == 1 ? "CONTIG" : "SEPARATE",
            orientationStrings[checkedOrientation(orientation)],
            blnReadLastTile ? "LastTL" : "FirstTL", imgWidth, imgLength);
#else
    sprintf(filename, "test_ReadRGBATile_%s_%s_i%02u_%02u.tif",
            orientationStrings[checkedOrientation(orientation)],
            blnReadLastTile ? "LastTL" : "FirstTL", imgWidth, imgLength);
#endif

    /*-- Create a file and write numIFDs IFDs directly to it --*/
    TIFF *tif = TIFFOpen(filename, modeStrings[openMode]);
    if (!tif)
    {
        fprintf(stdXOut, "\nCan't create %s. Testline %d called from %u\n",
                filename, __LINE__, cLine);
        return 1;
    }

    /* Writing baseline images (IFDs) to file. */
    if (write_data_to_current_directory(tif, imgWidth, imgLength,
                                        tile_width_length, true, orientation,
                                        true, NULL, 0))
    {
        fprintf(stdXOut,
                "\nCan't write data to current directory in %s. Testline %d "
                "called from %u\n",
                filename, __LINE__, cLine);
        goto failure;
    }

    /* Write IFD tags to file (and setup new empty IFD). */
    TIFFWriteDirectory_M(tif, filename, __LINE__);
    TIFFClose(tif);

    /*=== Test basic function TIFFReadRGBATile() ===*/
    TIFFOpen_M(tif, filename, "r", __LINE__);
    /*------
     TIFFReadRGBATile() reads a single tile of a tile-based image into memory,
     storing the result in the user supplied RGBA raster. The raster is
     assumed to be an array of TileWidth * TileLength 32-bit entries, where
     TileWidth is the width of a tile (TIFFTAG_TILEWIDTH) and TileLength is the
     height of a tile (TIFFTAG_TILELENGTH).

        TIFFReadRGBATileExt() provides the parameter stop_on_error. Its
     behaviour is described at TIFFReadRGBAImage.

        The x and y values are the offsets from the top left corner to the top
     left corner of the tile to be read. They must be an exact multiple of the
     tile width and length.

        Note that the raster is assume to be organized such that the pixel at
     location (x, y) is raster [ y * width + x ]; with the raster origin in the
     lower-left hand corner of the tile. That is bottom to top organization.
     Edge tiles which partly fall off the image will be filled out with
     appropriate zeroed areas.
     ------*/
    tlwidth = 0;
    tllength = 0;
    TIFFGetField_M(tif, TIFFTAG_TILEWIDTH, &tlwidth, filename, __LINE__);
    TIFFGetField_M(tif, TIFFTAG_TILELENGTH, &tllength, filename, __LINE__);
    /* For tiled mode get size of a tile in bytes */
    tlsize = TIFFTileSize(tif); // debugging
    (void)tlsize; // debugging avoid compiler warning unused parameter

    raster1 = NULL;
    rSizeWidth = tlwidth;
    rSizeHeight = tllength;

    rasterSize =
        (tmsize_t)(sizeof(uint32_t) * (size_t)rSizeWidth * (size_t)rSizeHeight);
    raster1 = (uint32_t *)_TIFFmalloc(rasterSize);
    if (raster1 == NULL)
    {
        fprintf(stdXOut,
                "Can't allocate 'raster'-buffer. Testline %d called from %u\n",
                __LINE__, cLine);
        goto failure;
    }
    for (uint32_t i = 0; i < rSizeWidth * rSizeHeight; i++)
    {
        /* Initialize dark green background */
        raster1[i] = RASTER_BACKGROUND_UINT32VAL;
    }

    /*--- Disable Warning Handler for "cannot rotate" warnings ---*/
#ifndef DO_NOT_DISABLE_WARNINGS_AND_ERRORS
    if (orientation >= ORIENTATION_LEFTTOP)
    {
        warnHandler = TIFFSetWarningHandler(NULL);
    }
#endif

    /* Read first or last tile in image */
    /* --- Set parameter for printRaster() in img-struct later, used also as
     * parameter for TIFFReadRGBATile() --*/
    img.width = imgWidth;
    img.height = imgLength;
    aux1 = aux2 = 0;
    if (blnReadLastTile)
    {
        aux1 = (imgWidth / tlwidth) * tlwidth;
        aux2 = (imgLength / tllength) * tllength;
        if (aux1 > INT_MAX || aux2 > INT_MAX)
        {
            fprintf(stdXOut, "Integer overflow. Testline %d called from %u\n",
                    __LINE__, cLine);
            goto failure;
        }
    }
    img.col_offset = (int)aux1;
    img.row_offset = (int)aux2;
    /* Set default req_orientation as it used by TIFFReadRGBATile(). */
    img.req_orientation = ORIENTATION_BOTLEFT;

    ret = TIFFReadRGBATile(tif, (uint32_t)img.col_offset,
                           (uint32_t)img.row_offset, raster1);
    if (!ret)
    {
        if (!blnQuiet)
            fprintf(stdXOut,
                    "TIFFReadRGBATile() returned failure. Testline %d called "
                    "from %u\n",
                    __LINE__, cLine);
        GOTOFAILURE
    }
    else
    {
        if ((ret = checkRasterContents("TIFFReadRGBATile()", &img, raster1,
                                       rSizeWidth, rSizeHeight, orientation,
                                       false)))
        {
            GOTOFAILURE
        }
    }

    printRaster("TIFFReadRGBATile()", &img, raster1, rSizeWidth, rSizeHeight,
                orientation, TRUE);
#ifdef PRINTTO_PPM
    if ((ret = printRasterToPPMFile(TIFFFileName(tif), &img, raster1,
                                    rSizeWidth, rSizeHeight)))
    {
        GOTOFAILURE
    }
#endif

    /*-- Leaving function --*/
    if (errHandler != NULL)
        TIFFSetErrorHandler(errHandler);
    if (warnHandler != NULL)
        TIFFSetWarningHandler(warnHandler);
    TIFFClose(tif);
#ifndef DEBUG_TESTING
    unlink(filename);
#endif
    return 0;

failure:
    if (errHandler != NULL)
        TIFFSetErrorHandler(errHandler);
    if (warnHandler != NULL)
        TIFFSetWarningHandler(warnHandler);
    if (tif)
        TIFFClose(tif);
    return 1;
} /*-- test_ReadRGBATile() --*/

/* Dependent on the given output- and logging- flags, the logFile is opende or a
 * new one is re-opened, and the stdXOut output is redirected from stderr to
 * file.
 */
static void checkOpenLogFile(int blnReOpen)
{
    /* First open logfile, if it does not exist, otherwise leave it as is except
     * blnReOpen is set. */
    if (blnReOpen && fpLog != NULL)
    {
        fclose(fpLog);
        fpLog = NULL;
    }

    if (fpLog == NULL && logFilename != NULL)
    {
        fpLog = fopen(logFilename, "a");
        if (fpLog == NULL)
        {
            fprintf(stderr, "\nError: Could not open logfile %s.\n",
                    logFilename);
        }
    }

    /* Switch stdXOut to logFile, if ...*/
    if (blnStdOutToLogFile)
    {
        if (fpLog != NULL)
        {
            stdXOut = fpLog;
        }
    }
    if (stdXOut == NULL)
    {
        fprintf(stderr, "\nError: stdXOut is NULL - exiting.\n");
        exit(10);
    }
    return;
}

/* ============  MAIN =============== */
int main(void)
{
    int retval = 0;
    int retvalLast = 0;
    int ntest = 0;

    if (logFilename != NULL)
    {
        unlink(logFilename);
        if (blnMultipleLogFiles)
        {
            for (unsigned int i = 0;
                 i < (sizeof(arrLogFilenames) / sizeof(arrLogFilenames[0]));
                 i++)
            {
                unlink(arrLogFilenames[i]);
            }
        }
    }

    /* Set default output to stderr. */
    stdXOut = stderr;

    /* If fprintf shall be redirected from stderr to logFile. */
    checkOpenLogFile(FALSE);

    unsigned int openMode = 0;

    {
        for (planarconfig = PLANARCONFIG_CONTIG;
             planarconfig <= PLANARCONFIG_SEPARATE; planarconfig++)
        {
            fprintf(
                stdXOut,
                "\n-----------------------------------------------------------"
                "\n==== Testing ReadRGBAStrip and Tile: %s with openMode = "
                "%s ===="
                "\n-----------------------------------------------------------"
                "\n",
                planarconfig == 1 ? "CONTIG" : "SEPARATE",
                modeStrings[openMode]);
            if (stdXOut != stderr)
            {
                fprintf(
                    stderr,
                    "\n--------------------------------------------------------"
                    "---"
                    "\n==== Testing ReadRGBAStrip and Tile: %s with openMode = "
                    "%s ===="
                    "\n--------------------------------------------------------"
                    "---"
                    "\n",
                    planarconfig == 1 ? "CONTIG" : "SEPARATE",
                    modeStrings[openMode]);
            }

            for (uint16_t orientation = 1; orientation < 9; orientation++)
            {

                /* clang-format off */
                /* STRIP */
                retval += test_ReadRGBAStrip(ntest, openMode, orientation, 28, 24, rows_per_strip, false, __LINE__); ntest++;
                if (retval != retvalLast) { fprintf(stdXOut, "    >>>> Test %d test_ReadRGBAStrip FAILED  (openMode %s, %s img_orient: %s). <<<<\n\n", ntest, modeStrings[openMode], (planarconfig == 1 ? "CONTIG" : "SEPARATE"), orientationStrings[checkedOrientation(orientation)]); retvalLast = retval; }
                retval += test_ReadRGBAStrip(ntest, openMode, orientation, 28, 24, rows_per_strip, true, __LINE__); ntest++;
                if (retval != retvalLast) { fprintf(stdXOut, "    >>>> Test %d test_ReadRGBAStrip FAILED  (openMode %s, %s img_orient: %s). <<<<\n\n", ntest, modeStrings[openMode], (planarconfig == 1 ? "CONTIG" : "SEPARATE"), orientationStrings[checkedOrientation(orientation)]); retvalLast = retval; }

                /*  Tiled */
                retval += test_ReadRGBATile(ntest, openMode, orientation, 28, 24, false, __LINE__); ntest++;
                if (retval != retvalLast) { fprintf(stdXOut, "    >>>> Test %d ReadRGBATile FAILED  (openMode %s, %s img_orient: %s). <<<<\n\n", ntest, modeStrings[openMode], (planarconfig == 1 ? "CONTIG" : "SEPARATE"), orientationStrings[checkedOrientation(orientation)]); retvalLast = retval; }
                retval += test_ReadRGBATile(ntest, openMode, orientation, 28, 24, true, __LINE__); ntest++;
                if (retval != retvalLast) { fprintf(stdXOut, "    >>>> Test %d ReadRGBATile FAILED  (openMode %s, %s img_orient: %s). <<<<\n\n", ntest, modeStrings[openMode], (planarconfig == 1 ? "CONTIG" : "SEPARATE"), orientationStrings[checkedOrientation(orientation)]); retvalLast = retval; }
                /* clang-format on */
            }
        }
    }

    fprintf(stdXOut, "\n\n==== Testing RGBAImageGet... ====\n");
    if (stdXOut != stderr)
        fprintf(stderr, "\n\n==== Testing RGBAImageGet... ====\n");

    for (int tiled = 0; tiled <= 1; tiled++) // original
    {
        for (planarconfig = PLANARCONFIG_CONTIG;
             planarconfig <= PLANARCONFIG_SEPARATE; planarconfig++)
        {
            fprintf(stdXOut,
                    "\n---------------------------------------------"
                    "\n==== Testing %s %s with openMode = %s ===="
                    "\n---------------------------------------------\n",
                    tiled ? "TILED" : "STRIP",
                    planarconfig == 1 ? "CONTIG" : "SEPARATE",
                    modeStrings[openMode]);
            if (blnMultipleLogFiles)
            {
                unsigned int n =
                    (unsigned int)tiled * 2 + (unsigned int)(planarconfig - 1);
                assert(n <
                       (sizeof(arrLogFilenames) / sizeof(arrLogFilenames[0])));
                logFilename = arrLogFilenames[n];
                checkOpenLogFile(TRUE);
            }
            for (uint16_t orientation = ORIENTATION_TOPLEFT;
                 orientation <= ORIENTATION_LEFTBOT; orientation++)
            {
                for (uint16_t req_orientation = ORIENTATION_TOPLEFT;
                     req_orientation <= ORIENTATION_LEFTBOT; req_orientation++)
                {
                    /* clang-format off */
                /*-- Only some testcases are necessary for CI regression testing. The others are commented but not deleted for easier debugging - when necessary. */
                //retval += test_ReadRGBAImage(ntest, openMode, (uint16_t)orientation,8, 4, tiled, req_orientation);  ntest++;
                //if (retval != retvalLast) { fprintf(stdXOut, "    >>>> Test %d FAILED  (openMode %s; tiled=%d). <<<<\n\n", ntest, modeStrings[openMode], tiled); retvalLast = retval; }
                //retval += test_ReadRGBAImage(ntest, openMode, (uint16_t)orientation,15, 15, tiled, req_orientation);  ntest++;
                //if (retval != retvalLast) { fprintf(stdXOut, "    >>>> Test %d FAILED  (openMode %s; tiled=%d). <<<<\n\n", ntest, modeStrings[openMode], tiled); retvalLast = retval; }
                retval += test_ReadRGBAImage(ntest, openMode, orientation,16, 16, tiled, req_orientation);  ntest++;
                if (retval != retvalLast) { fprintf(stdXOut, "    >>>> Test %d FAILED  (openMode %s; tiled=%d %s, img_orient: %s raster_orient: %s). <<<<\n\n", ntest, modeStrings[openMode], tiled, (planarconfig == 1 ? "CONTIG" : "SEPARATE"), orientationStrings[checkedOrientation(orientation)], orientationStrings[checkedOrientation(req_orientation)]); retvalLast = retval; }
                //retval += test_ReadRGBAImage(ntest, openMode, (uint16_t)orientation,31, 18, tiled, req_orientation);  ntest++;
                //if (retval != retvalLast) { fprintf(stdXOut, "    >>>> Test %d FAILED  (openMode %s; tiled=%d). <<<<\n\n", ntest, modeStrings[openMode], tiled); retvalLast = retval; }
                retval += test_ReadRGBAImage(ntest, openMode, (uint16_t)orientation,28, 24, tiled, req_orientation);  ntest++;
                if (retval != retvalLast) { fprintf(stdXOut, "    >>>> Test %d FAILED  (openMode %s; tiled=%d %s, img_orient: %s raster_orient: %s). <<<<\n\n", ntest, modeStrings[openMode], tiled, (planarconfig == 1 ? "CONTIG" : "SEPARATE"), orientationStrings[checkedOrientation(orientation)], orientationStrings[checkedOrientation(req_orientation)]); retvalLast = retval; }
                //retval += test_ReadRGBAImage(ntest, openMode, (uint16_t)orientation,32, 32, tiled, req_orientation);  ntest++;
                //if (retval != retvalLast) { fprintf(stdXOut, "    >>>> Test %d FAILED  (openMode %s; tiled=%d). <<<<\n\n", ntest, modeStrings[openMode], tiled); retvalLast = retval; }
                    /* clang-format on */
                }
            }
        }
    }

    if (!retval)
    {
        fprintf(stdXOut, "\n==== Testing RGBAImage finished OK. ====\n");
        if (stdXOut != stderr)
            fprintf(stderr, "\n==== Testing RGBAImage finished OK. ====\n");
    }
    else
    {
        fprintf(stdXOut,
                "\n==== Testing RGBAImage finished with ERROR. ====\n");
        if (stdXOut != stderr)
            fprintf(stderr,
                    "\n==== Testing RGBAImage finished with ERROR. ====\n");
    }

    return retval;
} /*-- main() -- */

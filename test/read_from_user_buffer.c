#include "tiffio.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int create_test_file(const char *filename)
{
    static const uint8_t pixels[8] = {
        0x11, 0x12, 0x13, 0x14,
        0x21, 0x22, 0x23, 0x24
    };

    TIFF *tif = TIFFOpen(filename, "w");
    if (tif == NULL)
        return 0;

    if (!TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, 4) ||
        !TIFFSetField(tif, TIFFTAG_IMAGELENGTH, 2) ||
        !TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8) ||
        !TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1) ||
        !TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK) ||
        !TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG) ||
        !TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_NONE) ||
        !TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, 2))
    {
        TIFFClose(tif);
        return 0;
    }

    if (TIFFWriteEncodedStrip(tif, 0, (void *)pixels,
                              (tmsize_t)sizeof(pixels)) !=
        (tmsize_t)sizeof(pixels))
    {
        TIFFClose(tif);
        return 0;
    }

    TIFFClose(tif);
    return 1;
}

int main(void)
{
    static const char filename[] = "read_from_user_buffer.tif";

    uint8_t inbuf[8] = {
        0xA1, 0xA2, 0xA3, 0xA4,
        0xB1, 0xB2, 0xB3, 0xB4
    };

    const uint8_t expected_partial[4] = {
        0xA1, 0xA2, 0xA3, 0xA4
    };

    const uint8_t expected_file_row[4] = {
        0x11, 0x12, 0x13, 0x14
    };

    uint8_t partial[4] = {0};
    uint8_t row[4] = {0};

    if (!create_test_file(filename))
    {
        fprintf(stderr, "Cannot create test TIFF\n");
        return 1;
    }

    TIFF *tif = TIFFOpen(filename, "r");
    if (tif == NULL)
    {
        remove(filename);
        return 1;
    }

    /*
     * Decode only the first half of caller-owned input.
     * The decoder will leave its raw cursor at inbuf + 4.
     */
    if (!TIFFReadFromUserBuffer(tif, 0, inbuf, sizeof(inbuf),
                                partial, sizeof(partial)))
    {
        fprintf(stderr, "TIFFReadFromUserBuffer() failed\n");
        TIFFClose(tif);
        remove(filename);
        return 1;
    }

    if (memcmp(partial, expected_partial, sizeof(partial)) != 0)
    {
        fprintf(stderr, "Unexpected TIFFReadFromUserBuffer() output\n");
        TIFFClose(tif);
        remove(filename);
        return 1;
    }

    /*
     * Overwrite the remaining caller-owned input.
     *
     * TIFFReadFromUserBuffer() must not leave subsequent normal reads
     * dependent on this temporary buffer.
     */
    memset(inbuf + 4, 0xCC, 4);

    if (TIFFReadScanline(tif, row, 0, 0) != 1)
    {
        fprintf(stderr, "TIFFReadScanline() failed\n");
        TIFFClose(tif);
        remove(filename);
        return 1;
    }

    /*
     * The row must come from the TIFF file (11 12 13 14), not from the
     * temporary input buffer (which now contains CC CC CC CC).
     */
    if (memcmp(row, expected_file_row, sizeof(row)) != 0)
    {
        fprintf(stderr,
                "Subsequent read reused TIFFReadFromUserBuffer() state: "
                "%02x %02x %02x %02x\n",
                row[0], row[1], row[2], row[3]);

        TIFFClose(tif);
        remove(filename);
        return 1;
    }

    TIFFClose(tif);
    remove(filename);
    return 0;
}

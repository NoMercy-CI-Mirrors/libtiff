/*
 * Regression test for a failed SEEK_END in TIFFLinkDirectory() and when
 * appending a custom directory in TIFFWriteDirectorySec().
 *
 * The second TIFFWriteDirectory() must report the seek error without changing
 * the in-memory file. A retry after the transient error must then succeed.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tiffio.h"

#define BUFFER_SIZE 4096

typedef struct
{
    uint8_t bytes[BUFFER_SIZE];
    toff_t offset;
    toff_t size;
    int fail_next_seek_to_end;
    int observed_seek_failure;
} MemoryFile;

static tmsize_t read_proc(thandle_t handle, void *buffer, tmsize_t size)
{
    MemoryFile *file = (MemoryFile *)handle;
    if (size <= 0 || file->offset >= file->size)
        return 0;

    toff_t available = file->size - file->offset;
    if ((toff_t)size > available)
        size = (tmsize_t)available;
    memcpy(buffer, file->bytes + file->offset, (size_t)size);
    file->offset += (toff_t)size;
    return size;
}

static tmsize_t write_proc(thandle_t handle, void *buffer, tmsize_t size)
{
    MemoryFile *file = (MemoryFile *)handle;
    if (size < 0 || file->offset > BUFFER_SIZE ||
        (toff_t)size > BUFFER_SIZE - file->offset)
        return 0;

    memcpy(file->bytes + file->offset, buffer, (size_t)size);
    file->offset += (toff_t)size;
    if (file->offset > file->size)
        file->size = file->offset;
    return size;
}

static toff_t seek_proc(thandle_t handle, toff_t offset, int whence)
{
    MemoryFile *file = (MemoryFile *)handle;
    toff_t base;

    if (whence == SEEK_END && file->fail_next_seek_to_end)
    {
        file->fail_next_seek_to_end = 0;
        file->observed_seek_failure = 1;
        return (toff_t)-1;
    }

    if (whence == SEEK_SET)
        base = 0;
    else if (whence == SEEK_CUR)
        base = file->offset;
    else if (whence == SEEK_END)
        base = file->size;
    else
        return (toff_t)-1;

    if (offset > BUFFER_SIZE || base > BUFFER_SIZE - offset)
        return (toff_t)-1;
    file->offset = base + offset;
    return file->offset;
}

static int close_proc(thandle_t handle)
{
    (void)handle;
    return 0;
}

static toff_t size_proc(thandle_t handle)
{
    return ((MemoryFile *)handle)->size;
}

static int map_proc(thandle_t handle, void **base, toff_t *size)
{
    (void)handle;
    (void)base;
    (void)size;
    return 0;
}

static void unmap_proc(thandle_t handle, void *base, toff_t size)
{
    (void)handle;
    (void)base;
    (void)size;
}

static int set_fields(TIFF *tif)
{
    return TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, 1U) &&
           TIFFSetField(tif, TIFFTAG_IMAGELENGTH, 1U) &&
           TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8) &&
           TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1) &&
           TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, 1U) &&
           TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG) &&
           TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK) &&
           TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
}

static int test_mode(const char *mode)
{
    MemoryFile file = {{0}, 0, 0, 0, 0};
    TIFF *tif = TIFFClientOpen("seek-failure", mode, (thandle_t)&file,
                               read_proc, write_proc, seek_proc, close_proc,
                               size_proc, map_proc, unmap_proc);
    if (tif == NULL)
        return 1;

    if (!set_fields(tif) || !TIFFWriteDirectory(tif) || !set_fields(tif))
        return 1;

    const toff_t size_before_failure = file.size;
    file.fail_next_seek_to_end = 1;
    if (TIFFWriteDirectory(tif) != 0)
    {
        fprintf(stderr, "TIFFWriteDirectory() ignored a failed seek\n");
        return 1;
    }
    if (!file.observed_seek_failure || file.size != size_before_failure)
    {
        fprintf(stderr, "failed seek unexpectedly changed the output\n");
        return 1;
    }

    if (!TIFFWriteDirectory(tif))
    {
        fprintf(stderr, "TIFFWriteDirectory() retry failed\n");
        return 1;
    }

    TIFFClose(tif);
    return 0;
}

static int test_custom_directory(const char *mode)
{
    MemoryFile file = {{0}, 0, 0, 0, 0};
    TIFF *tif = TIFFClientOpen("seek-failure-custom", mode, (thandle_t)&file,
                               read_proc, write_proc, seek_proc, close_proc,
                               size_proc, map_proc, unmap_proc);
    if (tif == NULL)
        return 1;

    if (!set_fields(tif) || !TIFFWriteDirectory(tif))
        return 1;

    if (TIFFCreateEXIFDirectory(tif) != 0 ||
        !TIFFSetField(tif, EXIFTAG_EXPOSURETIME, 0.5))
        return 1;

    const toff_t size_before_failure = file.size;
    uint64_t diroff = 0;
    file.fail_next_seek_to_end = 1;
    if (TIFFWriteCustomDirectory(tif, &diroff) != 0)
    {
        fprintf(stderr,
                "TIFFWriteCustomDirectory() ignored a failed seek\n");
        return 1;
    }
    if (!file.observed_seek_failure || file.size != size_before_failure)
    {
        fprintf(stderr, "failed seek unexpectedly changed the output\n");
        return 1;
    }

    if (!TIFFWriteCustomDirectory(tif, &diroff) || diroff == 0)
    {
        fprintf(stderr, "TIFFWriteCustomDirectory() retry failed\n");
        return 1;
    }

    TIFFClose(tif);
    return 0;
}

int main(void)
{
    int ret = test_mode("w");
    ret |= test_mode("w8");
    ret |= test_custom_directory("w");
    ret |= test_custom_directory("w8");
    return ret;
}

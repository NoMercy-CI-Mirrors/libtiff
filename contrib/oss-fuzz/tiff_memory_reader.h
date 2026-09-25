/* Copyright (c) 2026 The libtiff contributors
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that (i) the above copyright notices and this permission notice appear in
 * all copies of the software and related documentation, and (ii) the names of
 * the copyright holders may not be used in any advertising or publicity
 * relating to the software without their specific, prior written permission.
 *
 * THE SOFTWARE IS PROVIDED "AS-IS" AND WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS, IMPLIED OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY WARRANTY
 * OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef TIFF_MEMORY_READER_H
#define TIFF_MEMORY_READER_H

#include <cstdint>
#include <cstring>

#include <tiffio.h>

#define TIFF_FUZZ_MAX_ALLOC (64 * 1024 * 1024)

struct TIFFMemoryReader
{
    const uint8_t *data;
    toff_t size;
    toff_t offset;
};

static tmsize_t TIFFMemoryRead(thandle_t handle, void *buffer, tmsize_t size)
{
    TIFFMemoryReader *reader = static_cast<TIFFMemoryReader *>(handle);
    if (size < 0 || reader->offset >= reader->size)
    {
        return 0;
    }

    toff_t available = reader->size - reader->offset;
    if (static_cast<toff_t>(size) > available)
    {
        size = static_cast<tmsize_t>(available);
    }

    memcpy(buffer, reader->data + reader->offset, static_cast<size_t>(size));
    reader->offset += static_cast<toff_t>(size);
    return size;
}

static tmsize_t TIFFMemoryWrite(thandle_t, void *, tmsize_t) { return 0; }

static toff_t TIFFMemorySeek(thandle_t handle, toff_t offset, int whence)
{
    TIFFMemoryReader *reader = static_cast<TIFFMemoryReader *>(handle);
    toff_t base;

    switch (whence)
    {
        case SEEK_SET:
            base = 0;
            break;
        case SEEK_CUR:
            base = reader->offset;
            break;
        case SEEK_END:
            base = reader->size;
            break;
        default:
            return static_cast<toff_t>(-1);
    }

    if (offset > static_cast<toff_t>(-1) - base)
    {
        return static_cast<toff_t>(-1);
    }

    reader->offset = base + offset;
    return reader->offset;
}

static int TIFFMemoryClose(thandle_t) { return 0; }

static toff_t TIFFMemorySize(thandle_t handle)
{
    return static_cast<TIFFMemoryReader *>(handle)->size;
}

static TIFF *TIFFOpenMemory(const char *name, const uint8_t *data, size_t size,
                            TIFFMemoryReader *reader)
{
    TIFFOpenOptions *options = TIFFOpenOptionsAlloc();
    if (options == nullptr)
    {
        return nullptr;
    }

    reader->data = data;
    reader->size = static_cast<toff_t>(size);
    reader->offset = 0;

    TIFFOpenOptionsSetMaxSingleMemAlloc(options, TIFF_FUZZ_MAX_ALLOC);
    TIFFOpenOptionsSetMaxCumulatedMemAlloc(options, TIFF_FUZZ_MAX_ALLOC);
    TIFF *tif = TIFFClientOpenExt(
        name, "r", static_cast<thandle_t>(reader), TIFFMemoryRead,
        TIFFMemoryWrite, TIFFMemorySeek, TIFFMemoryClose, TIFFMemorySize,
        nullptr, nullptr, options);
    TIFFOpenOptionsFree(options);
    return tif;
}

#endif

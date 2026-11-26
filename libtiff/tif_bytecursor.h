/*
 * Copyright (c) 2026, Guillermo Hernandez
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * @file tif_bytecursor.h
 * @brief Define small bounds-checked byte cursors for codec decoders.
 *
 * A cursor is a pointer plus the number of bytes remaining after it.  The
 * helpers check that count at the point of access and either succeed fully
 * (advancing the pointer and decreasing the count) or do nothing and return
 * 0, so a decoder logic error cannot turn into an out-of-bounds access.
 *
 * Copies and fills go through _TIFFmemcpy()/_TIFFmemset() rather than
 * memcpy()/memset(): with the small, bounded sizes seen in decoders, GCC
 * otherwise expands them inline into slow "rep movs"/"rep stos" sequences.
 */

#ifndef TIF_BYTECURSOR_H
#define TIF_BYTECURSOR_H

#include "tiffiop.h"

/**
 * @brief Track a borrowed byte range as a pointer and a remaining count.
 * @ingroup libtiff_private_basic_codecs
 *
 * The cursor does not own its storage; the caller keeps the range live and
 * valid for at least left bytes from p while the cursor is used. The helpers
 * below only advance p and decrease left together, by the same amount.
 */
typedef struct
{
    /** @brief Next byte to read or write. */
    uint8_t *p;
    /** @brief Number of bytes remaining from p. */
    tmsize_t left;
} TIFFByteCursor;

/**
 * @brief Read one byte from a cursor.
 * @ingroup libtiff_private_basic_codecs
 *
 * On success the cursor advances by one byte.
 *
 * @param[in,out] c Non-NULL cursor to read from.
 * @param[out] out Non-NULL destination for the byte read; left untouched if
 * no byte is left.
 * @return 1 after reading one byte, 0 (and leaves *out untouched) if none is
 * left.
 */
static inline int _TIFFByteCursorGetU8(TIFFByteCursor *c, uint8_t *out)
{
    if (c->left <= 0)
        return 0;
    *out = *c->p++;
    c->left--;
    return 1;
}

/**
 * @brief Write n copies of value to a cursor.
 * @ingroup libtiff_private_basic_codecs
 *
 * On success the cursor advances by n bytes.
 *
 * @param[in,out] dst Non-NULL cursor over writable storage.
 * @param[in] value Byte value to write.
 * @param[in] n Number of bytes to write.
 * @return 1 after writing n bytes, 0 and writes nothing if n is negative or
 * does not fit.
 */
static inline int _TIFFByteCursorFill(TIFFByteCursor *dst, uint8_t value,
                                      tmsize_t n)
{
    if (n < 0 || n > dst->left)
        return 0;
    _TIFFmemset(dst->p, value, n);
    dst->p += n;
    dst->left -= n;
    return 1;
}

/**
 * @brief Move n bytes from src to dst.
 * @ingroup libtiff_private_basic_codecs
 *
 * On success both cursors advance by n bytes. The two ranges must not
 * overlap.
 *
 * @param[in,out] dst Non-NULL cursor over writable storage.
 * @param[in,out] src Non-NULL cursor over readable storage.
 * @param[in] n Number of bytes to move.
 * @return 1 after moving n bytes, 0 and changes nothing if n is negative or
 * exceeds the bytes left in either cursor.
 */
static inline int _TIFFByteCursorCopy(TIFFByteCursor *dst, TIFFByteCursor *src,
                                      tmsize_t n)
{
    if (n < 0 || n > dst->left || n > src->left)
        return 0;
    _TIFFmemcpy(dst->p, src->p, n);
    dst->p += n;
    dst->left -= n;
    src->p += n;
    src->left -= n;
    return 1;
}

#endif /* TIF_BYTECURSOR_H */

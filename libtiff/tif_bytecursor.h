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

#ifndef TIF_BYTECURSOR_H
#define TIF_BYTECURSOR_H

/*
 * Small bounds-checked byte cursors for codec decoders.
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

#include "tiffiop.h"

typedef struct
{
    uint8_t *p;
    tmsize_t left;
} TIFFByteCursor;

/* Read one byte. Returns 0 (and leaves *out untouched) if none is left. */
static inline int _TIFFByteCursorGetU8(TIFFByteCursor *c, uint8_t *out)
{
    if (c->left <= 0)
        return 0;
    *out = *c->p++;
    c->left--;
    return 1;
}

/* Write n copies of value. Returns 0 and writes nothing if n does not fit. */
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

/*
 * Move n bytes from src to dst. Returns 0 and changes nothing if n exceeds
 * the bytes left in either cursor.
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

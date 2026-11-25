TIFFmemory
==========

Synopsis
--------

.. highlight:: c

::

    #include <tiffio.h>

.. c:function:: void *_TIFFmalloc(tmsize_t size)

.. c:function:: void *_TIFFcalloc(tmsize_t nmemb, tmsize_t size)

.. c:function:: void *_TIFFrealloc(void *buffer, tmsize_t size)

.. c:function:: void _TIFFfree(void *buffer)

.. c:function:: void _TIFFmemset(void *s, int c, tmsize_t n)

.. c:function:: void _TIFFmemcpy(void *dest, const void *src, tmsize_t n)

.. c:function:: int _TIFFmemcmp(const void *s1, const void *s2, tmsize_t n)

.. c:function:: void* _TIFFCheckMalloc(TIFF* tif, tmsize_t nmemb, tmsize_t elem_size, const char* what)

.. c:function:: void* _TIFFCheckRealloc(TIFF* tif, void* buffer, tmsize_t nmemb, tmsize_t elem_size, const char* what)

Description
-----------

These routines are provided for writing portable software that uses
:program:`libtiff`; they hide any memory-management related issues, such as
dealing with segmented architectures found on 16-bit machines.

:c:func:`_TIFFmalloc`, :c:func:`_TIFFcalloc` and :c:func:`_TIFFrealloc` are used to dynamically
allocate and reallocate memory used by :program:`libtiff`; such as memory
passed into the I/O routines. Memory allocated through these interfaces is
released back to the system using the :c:func:`_TIFFfree` routine.
These public wrappers do not apply TIFFOpenOptions allocation limits. Supply
nonnegative sizes; negative values are not validated before conversion to size_t.
Zero-size malloc/calloc returns NULL. For positive-size realloc failure, the old
allocation remains valid; zero-size realloc follows the platform allocator's
semantics and may release the original block. Use a temporary result pointer.

Memory allocated through one of the above interfaces can be set to a known
value using :c:func:`_TIFFmemset`, copied to another memory location using
:c:func:`_TIFFmemcpy`, or compared for equality using :c:func:`_TIFFmemcmp`.
These routines conform to the equivalent C routines:
:c:func:`memset`, :c:func:`memcpy`, :c:func:`memcmp`, respectively.

:c:func:`_TIFFCheckMalloc` and :c:func:`_TIFFCheckRealloc` are private helpers
declared in tiffiop.h. They require a non-NULL TIFF handle, reject nonpositive or
overflowing element products, then use the handle-aware allocator family.
Release their output with _TIFFfreeExt() and the matching live handle, not the
public free wrapper. A rejected zero-count CheckRealloc preserves its old block.
The cumulative-accounting family can use a private prefix, so mixing allocation
families or accounting handles is unsafe.

Diagnostics
-----------

The public memory wrappers emit no library diagnostics. The private checking
helpers report a diagnostic for any failed request; a policy rejection can
also emit a limit diagnostic. Memory copy/set/compare operations do not validate
counts, pointer extents, or overlap beyond the requirements of their C equivalents.

See also
--------

malloc (3),
memory (3),
:doc:`libtiff` (3tiff)

TIFFClose
==========

Synopsis
--------

.. highlight:: c

::

    #include <tiffio.h>

.. c:function:: void TIFFClose(TIFF* tif)

.. c:function:: void TIFFCleanup(TIFF* tif)

Description
-----------

:c:func:`TIFFClose` closes a file that was previously opened with
:c:func:`TIFFOpen`. Buffered data are flushed on a best-effort basis, including
the contents of the current directory (if modified); and all resources
are reclaimed. :c:func:`TIFFClose` calls :c:func:`TIFFCleanup` and then
the associated function to close the file handle.

:c:func:`TIFFCleanup` is an auxiliary function to free the TIFF structure.
The given structure will be completely freed, so you should save opened file
handle and pointer to the close procedure in external variables before
calling :c:func:`TIFFCleanup`, if you will need these ones to close the file.
:c:func:`TIFFCleanup` calls :c:func:`TIFFFlush` for writable handles and discards
its result. It releases library state even when flushing fails, without
invoking the client close callback. Client-info payloads and diagnostic contexts
remain client-owned. The TIFF handle and pointers into its released storage
are invalid afterward.

:c:func:`TIFFClose` accepts NULL as a no-op; :c:func:`TIFFCleanup` requires a
non-NULL live handle. The client close callback runs after the TIFF handle
has been freed and must not use it.

.. note::
    Both functions have no return value. 
    If you want to check whether the data has been successfully written
    to the file, you can call :c:func:`TIFFFlush`, which has a return value,
    before :c:func:`TIFFClose`.
    The close callback's return value is also discarded. A successful
    :c:func:`TIFFFlush` does not certify successful resource close or durable
    storage. C++ stream adapters leave the stream caller-owned and do not
    explicitly call its ``flush()`` or ``close()`` operation.
    

Diagnostics
-----------

All error messages are directed to the :c:func:`TIFFErrorExtR` routine.
Likewise, warning messages are directed to the :c:func:`TIFFWarningExtR` routine.

See also
--------

:doc:`libtiff` (3tiff),
:doc:`TIFFOpen`  (3tiff),
:doc:`TIFFError` (3tiff)

TIFFError
=========

Synopsis
--------

.. highlight:: c

::

    #include <tiffio.h>
    #include <stdarg.h>

.. c:function:: void TIFFError(const char * module, const char * fmt, ...)

.. c:function:: void TIFFErrorExt(thandle_t fd, const char* module, const char* fmt, ...)

.. c:function:: void TIFFErrorExtR(TIFF *tif, const char* module, const char* fmt, ...)

.. c:type:: void (*TIFFErrorHandler)(const char * module, const char* fmt, va_list ap)

.. c:type:: void (*TIFFErrorHandlerExt)(thandle_t fd, const char * module, const char* fmt, va_list ap)

.. c:type:: int (*TIFFErrorHandlerExtR)(TIFF* tif, void* user_data, const char* module, const char* fmt, va_list ap)

.. c:function:: TIFFErrorHandler TIFFSetErrorHandler(TIFFErrorHandler handler)

.. c:function:: TIFFErrorHandlerExt TIFFSetErrorHandlerExt(TIFFErrorHandlerExt handler)

Description
-----------

:c:func:`TIFFError` invokes the library-wide error handler function
to (normally) write an error message to ``stderr``.
The *fmt* parameter is a :c:func:`printf` format string, and any number
arguments can be supplied. The *module* parameter is interpreted as a
string that, if non-zero, should be printed before the message; it
typically is used to identify the software module in which an error is
detected.

Applications that desire to capture control in the event of an error
should use :c:func:`TIFFSetErrorHandler` to override the default
error handler. A :c:macro:`NULL` (0) error handler function may be
installed to suppress that global handler slot. Other global or per-handle
handlers can still run. Replacement is unsynchronised and does not manage
callback lifetime; coordinate concurrent registration and in-flight calls
externally.

Two more application-specific error handler callbacks are available,
each with different call parameters and passing parameters to the handler.
Each handler is also linked with an error message function, i.e.
:c:func:`TIFFErrorExt` and :c:func:`TIFFErrorExtR` if the application
intends to call the handler with those extended parameters.

:c:func:`TIFFErrorExt`  provides a file handle as parameter.
Within ``libtiff`` :c:func:`TIFFErrorExt` is called passing ``tif->tif_clientdata``
as *fd*, which represents the TIFF file handle (file descriptor).
The application-specific and library-wide handler for :c:func:`TIFFErrorExt`
is setup with :c:func:`TIFFSetErrorHandlerExt`.

:c:func:`TIFFErrorExtR` (introduced with libtiff 4.5) is called with its
TIFF handle and thus provides access to a per-TIFF handle (re-entrant)
error handler. That means for different TIFF handles, different error
handlers can be setup. This application-specific handler
can be setup when a TIFF file is opened with one of the following functions:
:c:func:`TIFFOpenExt`, :c:func:`TIFFOpenWExt`, :c:func:`TIFFFdOpenExt`
or :c:func:`TIFFClientOpenExt`.
Furthermore, a **custom defined data structure** *user_data* for the
error handler can be given along.

Please refer to :doc:`/functions/TIFFOpenOptions` for how to setup the
application-specific handler introduced with ``libtiff`` 4.5.

Note
----

Both functions :c:func:`TIFFError` and :c:func:`TIFFErrorExt`
each attempt to call both handler functions if they are defined.
First :c:type:`TIFFErrorHandler` is called and then :c:type:`TIFFErrorHandlerExt`.
However, :c:func:`TIFFError` passes a "0" as a file handle to
:c:type:`TIFFErrorHandlerExt`.

:c:func:`TIFFErrorExtR` tries first to call the per-TIFF handle defined
error handler. If :c:type:`TIFFErrorHandlerExtR` is not defined or
returns 0, :c:type:`TIFFErrorHandler` and then :c:type:`TIFFErrorHandlerExt`
are called. From libtiff 4.5 onwards :c:func:`TIFFErrorExtR` is used
within the ``libtiff`` library where the TIFF handle is available.
Otherwise, :c:func:`TIFFErrorExt` is used!

Dispatch is synchronous and does not itself abort an operation or change its
status. Callback strings and argument lists are borrowed for the call; callbacks
may change external state or errno, and exceptions are not caught.

Return values
-------------

:c:func:`TIFFSetErrorHandler` and :c:func:`TIFFSetErrorHandlerExt`
returns a reference to the previous error handler function.

:c:type:`TIFFErrorHandlerExtR` returns an integer as "stop" to control the call
of further error handler functions within :c:func:`TIFFErrorExtR`:

  - 0: both functions :c:type:`TIFFErrorHandler` and :c:type:`TIFFErrorHandlerExt` are called.
  - non-zero: no further error message function is called.

See also
--------

:doc:`TIFFWarning` (3tiff),
:doc:`TIFFOpen` (3tiff),
:doc:`/functions/TIFFOpenOptions`,
:doc:`libtiff` (3tiff),
printf (3)

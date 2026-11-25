TIFFWarning
===========

Synopsis
--------

.. highlight:: c

::

    #include <tiffio.h>
    #include <stdarg.h>

.. c:function:: void TIFFWarning(const char* module, const char* fmt, ...)

.. c:function:: void TIFFWarningExt(thandle_t fd, const char* module, const char* fmt, ...)

.. c:function:: void TIFFWarningExtR(TIFF *tif, const char* module, const char* fmt, ...)

.. c:function:: TIFFErrorHandler TIFFSetWarningHandler(TIFFErrorHandler handler)

.. c:function:: TIFFErrorHandlerExt TIFFSetWarningHandlerExt(TIFFErrorHandlerExt handler)

Description
-----------

Warning callbacks use the TIFFErrorHandler, TIFFErrorHandlerExt, and
TIFFErrorHandlerExtR typedefs declared in tiffio.h; there are no separate
warning-handler typedefs.

:c:func:`TIFFWarning` invokes the library-wide warning handler function
to (normally) write a warning message to the ``stderr``.
The *fmt* parameter is a :c:func:`printf` format string, and any number
arguments can be supplied. The *module* parameter is interpreted as a
string that, if non-zero, should be printed before the message; it
typically is used to identify the software module in which a warning is
detected.

Applications that desire to capture control in the event of a warning
should use :c:func:`TIFFSetWarningHandler` to override the default
warning handler. A :c:macro:`NULL` (0) warning handler function may be
installed to suppress that global handler slot. Other global or per-handle
handlers can still run. Replacement is unsynchronised and does not manage
callback lifetime; coordinate concurrent registration and in-flight calls
externally.

Two more application-specific warning handler callbacks are available,
each with different call parameters and passing parameters to the handler.
Each handler is also linked with a warning message function, i.e.
:c:func:`TIFFWarningExt` and :c:func:`TIFFWarningExtR` if the application
intends to call the handler with those extended parameters.

:c:func:`TIFFWarningExt` provides a file handle as parameter.
Within ``libtiff`` :c:func:`TIFFWarningExt` is called passing ``tif->tif_clientdata``
as *fd*, which represents the TIFF file handle (file descriptor).
The application-specific and library-wide handler for :c:func:`TIFFWarningExt`
is setup with :c:func:`TIFFSetWarningHandlerExt`.

:c:func:`TIFFWarningExtR` (introduced with libtiff 4.5) is called with its
TIFF handle and thus provides access to a per-TIFF handle (re-entrant)
warning handler. That means for different TIFF handles, different warning
handlers can be setup. This application-specific handler
can be setup when a TIFF file is opened with one of the following functions:
:c:func:`TIFFOpenExt`, :c:func:`TIFFOpenWExt`, :c:func:`TIFFFdOpenExt`
or :c:func:`TIFFClientOpenExt`.
Furthermore, a **custom defined data structure** *user_data* for the
warning handler can be given along.

Please refer to :doc:`/functions/TIFFOpenOptions` for how to setup the
application-specific handler introduced with ``libtiff`` 4.5.

Note
----

Both functions :c:func:`TIFFWarning` and :c:func:`TIFFWarningExt`
each attempt to call both handler functions if they are defined.
First :c:type:`TIFFErrorHandler` is called and then :c:type:`TIFFErrorHandlerExt`.
However, :c:func:`TIFFWarning` passes a "0" as a file handle to
:c:type:`TIFFErrorHandlerExt`.

:c:func:`TIFFWarningExtR` tries first to call the per-TIFF handle defined
warning handler. If :c:type:`TIFFErrorHandlerExtR` is not defined or
returns 0, :c:type:`TIFFErrorHandler` and then :c:type:`TIFFErrorHandlerExt`
are called. From libtiff 4.5 onwards :c:func:`TIFFWarningExtR` is used
within the ``libtiff`` library.

Dispatch is synchronous and does not itself abort an operation or change its
status. Callback strings and argument lists are borrowed for the call; callbacks
may change external state or errno, and exceptions are not caught.

Return values
-------------

:c:func:`TIFFSetWarningHandler` and :c:func:`TIFFSetWarningHandlerExt`
returns a reference to the previous warning handler function.

:c:type:`TIFFErrorHandlerExtR` returns an integer as "stop" to control the call
of further warning handler functions within :c:func:`TIFFWarningExtR`:

  - 0: both functions :c:type:`TIFFErrorHandler` and :c:type:`TIFFErrorHandlerExt` are called.
  - non-zero: no further warning message function is called.

See also
--------

:doc:`TIFFError` (3tiff),
:doc:`TIFFOpen` (3tiff),
:doc:`/functions/TIFFOpenOptions`,
:doc:`libtiff` (3tiff),
printf (3)

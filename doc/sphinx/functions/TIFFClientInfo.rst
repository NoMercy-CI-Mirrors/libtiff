TIFFClientInfo
==============

Synopsis
--------

.. highlight:: c

::

    #include <tiffio.h>

.. c:function:: void *TIFFGetClientInfo(TIFF* tif, const char *name)

.. c:function:: void TIFFSetClientInfo(TIFF* tif, void *data, const char *name)

Description
-----------

The *clientinfo* linked list provides a method to hand over user defined
data from one routine to another via the internal ``tif`` storage of the
``libtiff`` library.

:c:func:`TIFFGetClientInfo` returns a pointer to the data of the named entry
in the clientinfo-list. If the *name* is not found ``NULL`` is returned.
Names are compared case-sensitively. A stored NULL payload is indistinguishable
from an absent name through this query. No allocation or ownership transfer occurs.

:c:func:`TIFFSetClientInfo` adds or replaces an entry in the clientinfo-list
with the *name* and the pointer to the *data* provided by the caller.
New entries copy the name, but payload storage remains caller-owned.
Replacement neither allocates nor frees the previous payload. Passing NULL stores
a NULL association rather than deleting it. Handle cleanup frees names/nodes,
not payloads. This list is independent of the client I/O context.

Diagnostics
-----------

New-entry allocation failure emits checked-allocation diagnostics and leaves
the existing list unchanged. The setter returns no status; lookup and successful
existing-name replacement emit no direct diagnostic.

See also
--------

:doc:`libtiff` (3tiff),

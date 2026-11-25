@page jpeg-support JPEG dependency interface

Bridge dependency error handling and borrowed JPEG buffers.

The @ref libtiff_private_jpeg group begins with the dependency interface: error
frames, typed calls, data/table managers, packed twelve-bit access and raw
component buffer allocation. The @ref jpeg guide completes setup, marker inspection, row conversion,
tag hooks and codec installation.

JPEGState begins with the dependency ABI context union so callbacks can cast
from it. Dependency image-pool buffers are borrowed views that expire on pool
release; Ext-owned tables and outer state have separate lifetimes. Every
CALLJPEG wrapper creates a live synchronous setjmp frame. Fatal errors route
diagnostics, abort dependency image state and longjmp to that frame. Partial
buffer, dependency and file effects are not rolled back.

Void-style wrappers report one when the dependency call returns without a
fatal jump and discard any dependency Boolean result. Row wrappers preserve
counts and use minus one for trapped errors; header/finish queries have their
own status conventions. Progress monitoring uses a default scan limit of 100
and an unvalidated atoi environment override.

Raw managers borrow handle buffers. Input exhaustion can warn and substitute
a synthetic EOI rather than reading more client bytes. Table output replaces
and grows codec-owned storage, invalidating older views. Packed twelve-bit
writes assign the shared byte on even samples, so pairs must be written even
then odd when retaining both values. All capacities must match the chosen
eight/twelve-bit dependency ABI.

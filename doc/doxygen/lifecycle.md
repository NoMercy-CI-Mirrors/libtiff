# Lifecycle {#lifecycle}

The @ref libtiff_public_lifecycle module covers opening a TIFF handle, selecting
client I/O, configuring open options, querying/replacing handle metadata,
flushing, and releasing the handle. The C++ stream interface is an adapter over
the same client-I/O mechanism.

## Resource and context ownership

| Entry point | On a normal successful open | On a NULL return |
| --- | --- | --- |
| TIFFOpen()/TIFFOpenExt() | Own the returned TIFF handle; TIFFClose() releases it and invokes the backend close operation | The internally opened resource is closed on ordinary failure paths; prior file changes are not rolled back |
| TIFFOpenW()/TIFFOpenWExt() | Same ownership, using a Windows wide pathname | Same normal failure cleanup, with the conversion-allocation leak described under @ref lifecycle_limits |
| TIFFFdOpen()/TIFFFdOpenExt() | TIFFClose() closes the supplied backend resource | The supplied resource remains the caller's responsibility; its position/content may have changed |
| TIFFClientOpen()/TIFFClientOpenExt() | TIFFClose() invokes the supplied closeproc after freeing library state | closeproc is not invoked; release the client resource/context yourself |
| TIFFStreamOpen() | TIFFClose() deletes the adapter while the stream remains caller-owned | Ordinary NULL-return cleanup deletes the adapter; stream changes are not rolled back, and exceptions have separate limitations |

The client-open name is copied. The mode string is consumed during opening.
Callback pointers and client/diagnostic contexts are borrowed and must remain
usable through their last invocation, including opening and closing. A callback
must not destroy the invoking handle or recursively mutate its I/O state.
Per-handle diagnostics do not make simultaneous access to the same handle safe.

TIFFClose(NULL) does nothing. TIFFCleanup() instead requires a non-NULL live
handle and deliberately omits the client close callback. When using cleanup
alone, save TIFFGetCloseProc() and TIFFClientdata() before the handle is freed
if they are needed to release the remaining resource or adapter. Member pointers
and the handle itself are invalid after cleanup, regardless of flush success.

## Open options and diagnostic lifetime

@ref libtiff_public_open_options provides an independently allocated settings
object. Allocate it with TIFFOpenOptionsAlloc(), modify it, pass it to an Ext
opener, then release it with TIFFOpenOptionsFree(). A NULL options pointer uses
defaults. Opening copies settings rather than retaining the options object.
Changing or freeing the options does not change an existing handle.

The object defaults to unlimited checked allocations, no per-handle error or
warning callback, and unknown-tag warnings disabled. Allocation of the options
object itself can return NULL without a library diagnostic. Setters do not
validate their inputs or report status. Positive limits enable checking; zero
is the documented unlimited value, and negative stored values do not activate
the positive-limit checks.

These are limits on checked allocation paths, not process-memory caps. The
initial handle/name allocation is checked separately; it is not included in
the later cumulative tracked-allocation total. Direct allocator calls, mappings,
caller storage, and allocations outside the handle-aware helpers are not covered.

Diagnostic context pointers remain borrowed after settings are copied. They
must survive the resulting handle, even if the options object is freed earlier.
Early error reporting can call TIFFErrorHandlerExtR with a NULL TIFF pointer;
other opening diagnostics may see a partially initialised handle. Returning
nonzero from the callback suppresses the legacy global handlers for that message;
it does not turn a failure into success or abort the operation by itself.

## Open modes {#lifecycle_modes}

Mode strings must be non-NULL and null-terminated. The first character selects
the basic operation. Only a `+` immediately after `r` enables update access.

| Prefix | Behaviour |
| --- | --- |
| `r` | Read an existing TIFF, normally loading its first directory |
| `r+` | Read/update an existing resource with read/write access |
| `w` | Initialise a new header and default directory; pathname openers create/truncate the OS file |
| `a` | Initialise a default directory for appending; pathname openers may create the file |

Descriptor/client openers do not themselves perform OS open or truncation.
Their mode must match resource permissions, and the resource starts at TIFF
offset zero. For physical truncation, prepare the resource before wrapping it.
A short initial header read in a writable mode can write a fresh header, even
outside pathname `w` opening. Failure is not a transaction: reads, mapping,
partial writes, and position changes can already have occurred.

| Modifier | Implementation behaviour |
| --- | --- |
| `b`, `l` | Select big-/little-endian header creation; do not use these to reinterpret an existing file |
| `B`, `L` | Select MSB-to-LSB or LSB-to-MSB data bit order; `B` is the default |
| `H` | Deprecated alias of `B`, emitting a warning; it does not select CPU order |
| `M`, `m` | Enable/disable mapping for read-only access; a failed mapping normally falls back to client read/seek I/O |
| `C`, `c` | Enable/disable strip chopping for read-only access; compile-time defaults can also enable chopping in update mode |
| `h` | For a reading prefix, initialise default field methods without reading the first IFD |
| `8` | Select BigTIFF when creating a header |
| `4` | Classic TIFF is the creation default; this character is ignored by the current modifier switch and does not undo an earlier `8` |
| `D` | Defer strip/tile offset and byte-count array loading |
| `O` | Request on-demand array-value loading for read-only access; implies `D` |
| `u` | Native Windows backend mapping suppression; use portable `m` when appropriate |

Other modifier characters are ignored by the current switch. This is not a
promise that arbitrary combinations are useful. The Sphinx TIFFOpen manual
provides complementary usage information; the table records the current parser.

## Client I/O and platform conventions {#lifecycle_backends}

@ref libtiff_public_io_callbacks defines the six callback types: one shared
read/write signature, seek, close, size, map, and unmap. Even read-only client
opening requires all five non-mapping callback pointers to be non-NULL. Mapping
and unmapping can be omitted independently, but a real mapping that needs
release requires a real matching unmapper.

Read/write callbacks may transfer only part of a request. Failure does not imply
that the buffer, file bytes, or position are unchanged. The library has no
general retry/rollback layer around these callbacks. A mapper returns a boolean
status and supplies the view address and length through outputs. An unmapper
returns void; it cannot report status through its signature.

In the current early read-open failure path, cleanup can call the unmapper with
`base = NULL` and `size = 0` before calling the mapper at all. Custom unmapping
callbacks must tolerate this no-view cleanup case. This differs from a mapping
callback that returns failure after it was called, for which the mapping flag
is cleared and normal fallback I/O can proceed.

The POSIX/CRT backend uses an integer file descriptor carried through an opaque
handle union. The native Windows backend instead uses an integer representation
of a Windows HANDLE. A CRT descriptor is not interchangeable with that native
HANDLE convention. The latter backend also recognises `u` for mapping suppression.
Windows pathname variants use the native/CRT wide-character opening operation;
the converted narrow name is for diagnostics.

TIFFFileno()/TIFFSetFileno() manipulate descriptor metadata. Actual I/O uses
TIFFClientdata() and the installed callbacks. Replacing one does not update the
other. TIFFSetClientdata() affects later reads, writes, unmapping, and closing;
it does not reset existing directory/buffer/mapping state. TIFFSetMode() stores
an integer but does not change OS permissions. TIFFSetFileName() borrows its
replacement pointer without copying or renaming the OS resource.

## Flushing and close status

TIFFFlush() flushes image data and dirty directory information. TIFFFlushData()
flushes image data without committing directory changes. Both return 1 for
success/no work and 0 on relevant failure. Flushing can invoke codecs, I/O,
and diagnostics and can leave partial writes or changed state on failure.

TIFFClose() discards both the cleanup flush result and the client close result.
To inspect the library flush result, use a sequence such as:

```c
int written = TIFFFlush(tif);
TIFFClose(tif);
/* written records the explicit flush result, not close status or durability. */
```

Neither function is an fsync-style durability guarantee. The C++ adapter writes
to the stream but does not explicitly call its flush() or close it. The caller
remains responsible for its final stream/device flush and close handling.

## C++ stream adapters

@ref libtiff_public_streams supplies an input `rm` overload and an output `wm`
overload. Pass a non-NULL seekable binary stream positioned at zero, and keep
it alive until the handle/adapter is released. Input cannot write; output cannot
read back data even if the underlying object supports both directions. Mapping
is disabled. The output wrapper may write a dummy null byte to make an initially
empty string stream seekable before initialising the TIFF header.

The adapters preserve caller ownership of the stream. Ordinary NULL-return
opening failure deletes the wrapper but does not restore stream position, flags,
or bytes. C++ allocation and stream exceptions are not caught, and there is no
exception cleanup/rollback guarantee. Use streams with exceptions disabled for
status-based handling. See @ref libtiff_private_streams for the actual adapter
state and callback behaviour.

## Current implementation limits {#lifecycle_limits}

These observations describe source behaviour; this documentation pass does not
change it:

- The native Windows seek callback returns zero on API failure, which can be
  confused with a successful seek to offset zero. Its read/write callbacks also
  return zero on API failure, even after earlier chunks transferred data.
- The POSIX/CRT read/write callbacks return -1 for a zero-length request because
  their initial status value reaches the error branch without an OS call. A
  later-chunk error also returns -1 after earlier successful transfers.
- Early read-open failure can invoke unmapping with a NULL, zero-length view
  even though no mapper has run.
- Both Windows wide-path implementations open an OS resource before allocating
  the diagnostic-name conversion buffer. That allocation's failure returns
  NULL without closing the newly opened resource.
- Stream output seek returns absolute tellp() rather than subtracting the
  recorded TIFF origin. Both stream-size callbacks report absolute end position.
  Embedded TIFF data at a nonzero stream origin is therefore not reliably
  supported by these adapters.
- A failed output-stream tellp() is not independently checked when deriving a
  write count. A failed postencoding callback has already had its pending flag
  cleared. Neither path provides a general retry guarantee.

## Coverage

This module covers the public C lifecycle functions, the two TIFFStreamOpen()
overloads, the I/O callback typedefs, and the TIFFErrorHandlerExtR diagnostic
signature used by open options. Canonical contracts are in `tiffio.h` and
`tiffio.hxx`. Private-header contracts cover mode parsing, dummy
mapping/unmapping, early diagnostics, and directory-map teardown. Source-local
documentation covers both platform callback sets, Windows handle conversions,
the descriptor union, the stream helpers, and both stream-state records.

The allocator implementations sharing `tif_open.c` and the platform files,
diagnostic APIs, and default handlers are documented in @ref shared_services.
Directory/strile cursor queries and codec/directory operations remain with
their owning modules. Windows behaviour is documented from source review; a
build on a non-Windows host does not validate Windows execution.

| Public function family | Functions |
| --- | --- |
| Options | Alloc, Free, SetMaxSingleMemAlloc, SetMaxCumulatedMemAlloc, SetWarnAboutUnknownTags, SetErrorHandlerExtR, SetWarningHandlerExtR, all with the TIFFOpenOptions prefix |
| Openers | TIFFOpen, TIFFOpenExt, TIFFOpenW, TIFFOpenWExt, TIFFFdOpen, TIFFFdOpenExt, TIFFClientOpen, TIFFClientOpenExt |
| Release/flush | TIFFCleanup, TIFFClose, TIFFFlush, TIFFFlushData |
| Identity/context/access | TIFFFileName, TIFFSetFileName, TIFFFileno, TIFFSetFileno, TIFFClientdata, TIFFSetClientdata, TIFFGetMode, TIFFSetMode |
| Stored format/flags | TIFFIsTiled, TIFFIsByteSwapped, TIFFIsUpSampled, TIFFIsMSB2LSB, TIFFIsBigEndian, TIFFIsBigTIFF |
| Callback queries | TIFFGetReadProc, TIFFGetWriteProc, TIFFGetSeekProc, TIFFGetCloseProc, TIFFGetSizeProc, TIFFGetMapFileProc, TIFFGetUnmapFileProc |
| Streams | TIFFStreamOpen input and output overloads |

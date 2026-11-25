@page codecs Compression registry and codec installation

The @ref libtiff_public_codecs interfaces describe the process-global codec
registry. Built-ins remain known even when their compression dependency was
not enabled. TIFFFindCODEC() can therefore return a descriptor for unavailable
support. TIFFIsCODECConfigured() tests the selected callback against the build
stub; it does not execute that callback or prove that a particular image works.

Registrations take precedence newest first and can shadow built-ins. Registry
access is unsynchronised: serialise lookup, enumeration, registration, and
removal. A registration owns its descriptor and copied name; unregister consumes
its exact token. It does not clean up handles that already installed the codec.
Keep callback code and its context live for those handles as well as for the
registration itself.

TIFFGetConfiguredCODECs() returns an owned name-NULL-terminated array, freed
with _TIFFfree(). Descriptor names and callback pointers are shallow copies.
Unregistering can invalidate names in an earlier snapshot. Enumeration can
include duplicates, shadowed built-ins, and registered records with a NULL
initialiser. Do not treat the result as a unique, validated capability list.

The @ref libtiff_private_codecs group describes default/fallback methods,
installation, built-in descriptors, and registration nodes. Setting a scheme
resets methods and invokes its initialiser; the caller must clean up previous
codec state first. Unknown or build-disabled schemes can be accepted so callers
can inspect metadata. Build-disabled codecs report an error and fail when their
setup callback is invoked, so their pixel transfers fail. Unknown schemes keep
the default transfer methods: decoding fallbacks return zero and their reads
fail, but encoding fallbacks return minus one. Encoded strip and tile writes
test encoder callbacks only for nonzero, so those negative errors are accepted
and the writes can return a success count with no data stored; see
TIFFWriteEncodedStrip().
Method installation and initialiser failure do not provide rollback.

Individual codec and predictor groups describe state, tag-method chaining,
buffer ownership, and their specific transfer contracts. Optional dependencies
and build settings determine executable support; the reference includes source
contracts for all implementations regardless of local availability.

See @ref predictor for the shared predictor prefix, transforms and teardown.

See @ref basic_codecs for uncompressed, PackBits, ThunderScan and NeXT callbacks.

See @ref lzw for standard and compatibility LZW dictionaries and transfers.

See @ref zstd for ZSTD streaming codec state and transfer contracts.

See @ref lzma for LZMA2 streaming codec state and transfer contracts.

See @ref deflate for Deflate codec state and transfer contracts.

See @ref webp for WebP frame codec state and transfer contracts.

See @ref jbig for JBIG plane codec state and transfer contracts.

See @ref lerc for LERC frame codec state and transfer contracts.

See @ref fax for CCITT fax codecs state and transfer contracts.

See @ref pixarlog for PixarLog codec state and transfer contracts.

See @ref sgilog for SGILog codec state and transfer contracts.

See @ref jpeg-support for JPEG dependency interface state and transfer contracts.

See @ref jpeg for JPEG-in-TIFF setup, sampled transfer and twelve-bit dispatch.

See @ref ojpeg for Old-style JPEG reconstruction state and transfer contracts.

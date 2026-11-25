@page pixarlog PixarLog codec

Compand linear samples into predicted eleven-bit zlib streams.

The @ref libtiff_private_pixarlog group covers every conversion kernel, native
unaligned load/store helper, companding table, state member and codec callback.
Eight-bit input is preserved by companding; wider/floating representations
can lose precision. Integer output clips highlights; float encoding requires
finite inputs and caps high values. Table setup writes global coefficients,
so serialise table initialisation and float encoding across handles.

The codec performs its own prediction and byte-order handling. Output format
controls change directory depth/sample format and recompute cached sizes,
without propagating every nested setter status. On close, initialised state
changes advertised metadata to unsigned eight-bit samples. Input and on-disk
representation are therefore distinct.

Decode supports several output conversions; encode implements only float,
unsigned sixteen-bit and byte conversion. Encoder sizing/row traversal uses
strip geometry and requires complete input rows. The current three-channel
ABGR comparison rejects ordinary positive expanded output requests despite
the available packing helper. Decode can warn/truncate a partial final row
and zero its remaining output while returning success.

Failures can retain allocations and dependency/file progress. Cleanup restores
methods and releases tables/staging/stream state without finalising or flushing
payload. Installation ignores PredictorInit status. Read-write mode handling
and all format variants require source-based preconditions beyond a successful
format flag assignment.

@page lerc LERC frame codec

Stage scalar samples validity masks and secondary compression.

The @ref libtiff_private_lerc group covers complete frame preparation, staging,
scalar-type mapping, dependency transfers, variadic tags and state ownership.
Buffers use Ext allocation; optional libdeflate objects use dependency frees.
Supported input includes signed/unsigned 8/16/32-bit integers and IEEE
32/64-bit floating samples. Geometry products, mask products and dependency
size conversions must remain representable; a final allocation check alone
does not establish every intermediate arithmetic precondition.

Predecode decompresses the full strile into owned staging before byte-copy
callbacks serve the caller. Masks reconstruct binary alpha or invalid floating
samples; per-band NaN masks depend on LERC version. Encoding stages all input,
checks the exact count at postencode, builds masks and optionally wraps the
LERC blob in Deflate or ZSTD. Staging can be compacted or overwritten.

Flush temporarily substitutes codec storage for tif_rawdata and restores that
pointer, but rawcp can still refer into staging. Failure is not transactional.
Scalar controls and custom LERC_PARAMETERS have different variadic contracts,
with some attempted values retained after rejection and some default setter
statuses ignored. Cleanup frees staging/dependency objects without encoding
or flushing pending input. No finite compression-ratio heuristic is installed.

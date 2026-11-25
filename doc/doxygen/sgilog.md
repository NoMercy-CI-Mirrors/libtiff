@page sgilog SGILog codec

Compress logarithmic luminance and packed LogLuv pixel rows.

The @ref libtiff_private_sgilog group covers row/strile callbacks, format
inference, native unaligned access, translation kernels, state and codec
lifecycle. Public representation formulas are documented separately in
@ref logluv_conversion. Translation storage is Ext-owned and counted in pixels.
Caller buffers remain borrowed; their native wide values can be unaligned.

LogL16 and LogLuv32 encode byte-plane runs; LogLuv24 stores three bytes per
pixel. Low-level row callbacks require complete pixels and sufficient raw
capacity, while strile wrappers require complete decoded rows. Short input
can change raw/staging state without clearing caller output. Earlier decoded
rows remain on a later failure. Encoding flushes can leave partial file data.

Format controls modify directory depth/sample format and, for raw words,
sample count. Nested setter statuses are ignored. Setup can allocate or
replace translation state; repeated setup without release can leak old
storage. Close normalises encoded directory metadata to signed sixteen-bit
LogL or Luv triples. Cleanup restores methods and frees storage without
normalising metadata or flushing pending payload.

Random dithering uses an unsynchronised global counter seeded from time.
Out-of-gamut conversion lazily initialises a shared hue table. Serialise
those operations across handles/threads; finite representable conversion
inputs remain preconditions. The no-op converter and raw-copy paths retain
their explicit direction and lifetime contracts.

The historical uvcode.h table has no generator in this checkout. Its maintained
snapshot is annotated without changing initializer values; chromaticity row
units, bin counts and cumulative indices are documented in the header.

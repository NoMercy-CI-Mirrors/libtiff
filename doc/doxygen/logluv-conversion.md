@page logluv_conversion LogL and LogLuv representations

The @ref libtiff_public_logluv functions convert encoded luminance/chromaticity,
linear XYZ, and display RGB without an open TIFF handle. Availability of compiled
helpers follows SGILog support. They return scalar codes or fill caller storage;
there are no allocated result objects or diagnostic callbacks.

LogL16 stores signed logarithmic luminance in a sixteen-bit pattern. Some
negative luminances produce a sign-extended negative int, which is a code
rather than an error. LogL10 stores a narrower positive range. Decoding chooses
bin centres, so encode/decode is quantised. LogLuv24 combines ten-bit luminance
with table chromaticity; LogLuv32 combines signed LogL16 with two scaled bytes.
Nonpositive decoded luminance produces a zero XYZ triplet.

uv_decode() is the explicit status-returning table query: zero succeeds and
minus one rejects an out-of-range index without changing its outputs.
uv_encode() substitutes neutral coordinates for NaN input, maps out-of-gamut
colours through a hue-indexed perimeter table, and has no separate error code.
Finite representable arithmetic remains a precondition; NaN substitution does
not establish safety for arbitrary infinities or extreme conversion inputs.

No-dither quantisation is deterministic and is the C++ default. Dithered
conversions mutate shared pseudo-random state seeded from time; first-use
out-of-gamut mapping also initialises shared storage without synchronisation.
Serialise those operations across threads. The representation helpers do not
own or expose that state.

XYZtoRGB24() uses fixed display primaries, gamma approximately two, and channel
clipping. It produces RGB bytes, without an alpha channel or lossless colour
recovery. Keep input and output allocations disjoint and large enough for the
specified triplets. Private codec conversions and staging have separate
contracts under the compression module.

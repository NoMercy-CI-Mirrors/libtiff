@page rgba RGBA readers and packing

The @ref libtiff_public_rgba API produces display-oriented eight-bit channels
from supported grayscale, palette, RGB, CMYK, YCbCr, Lab, and SGILog inputs.
Support depends on bit depth, planar layout, channel count, codec configuration,
and subsampling. TIFFRGBAImageOK() performs a preliminary check; successful
Begin/Get operations impose additional conditions.

## Pixels, orientation, and clipping

Default output is a numeric uint32_t word 0xAABBGGRR with associated
(premultiplied) colour and alpha. This is a word layout, not a portable byte
sequence. Readers without alpha supply opaque alpha. Unassociated RGB alpha
is premultiplied through a lookup table. The default eight-bit, two-sample
grayscale-with-alpha packer instead copies gray and alpha directly, including
when the input marks alpha as unassociated; that path retains unassociated
colour values. Do not assume all supported input layouts are premultiplied. CMYK conversion is approximate;
colour reduction is intended for display rather than lossless sample access.

Whole-image readers default to a bottom-left raster origin. The oriented
interface and reusable reader allow another requested corner. Default getters
flip horizontally/vertically; they cannot transpose for orientations 5..8 and
warn for incompatible orientation families. Positive raster dimensions and a
representable, sufficiently large allocation remain caller preconditions.

Default getters clip the visible rectangle to the image after row/column
offsets and place it at the requested raster corner. Excess raster area is
unchanged. Packed sub-byte column offsets must align to a byte boundary.
Strip wrappers require the first row of a strip and return only actual rows
for the last strip. Tile wrappers require the upper-left tile coordinate;
edge tiles occupy a complete tile allocation with zero padding. Padding fixup
can still run after failed conversion, so it does not make other output valid.

## State and error policy

Use Begin/Get/End with a fresh caller-owned TIFFRGBAImage. Keep its TIFF handle,
current directory, methods, and callback code live. End releases conversion
tables and copied palettes, leaving the handle and output raster with the
caller. Reinitialising active state loses its allocations. Begin's preliminary
compatibility rejection leaves state untouched, so calling End on arbitrary
uninitialised state after that rejection is unsafe.

Setup can change JPEG colour mode or SGILog output format pseudo-tags. End
does not restore them. Transfer operations also change codec progress, cached
indices, and file position. Failure is not transactional.

With stop-on-error disabled, selected decoder failures are tolerated when
storage exists: conversion can continue with partial or retained decoded bytes
and return one. A positive status under that policy is not proof of an intact
image. With stop-on-error enabled, fatal failures still leave partial raster
and handle state. Begin's reason buffer requires 1024 writable bytes; wrappers
report that reason through the handle's diagnostic routing.

## Private implementation

The @ref libtiff_private_rgba group includes clipping/orientation helpers,
contiguous and separate strip/tile getters, map builders, callback selection,
and all 35 concrete macro-declared packers. Doxygen expands only their two
signature macros so the concrete functions and call relationships appear in
the site and tagfile. Their contracts state the selected input representation,
padding, alignment, signed row steps, and borrowed lifetimes. Packers rely on
validated caller state and have no independent bounds check or error status.

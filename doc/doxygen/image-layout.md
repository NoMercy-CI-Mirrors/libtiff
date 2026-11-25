# Image layout and sizing {#image_layout}

This module documents size calculations, strip/tile indexing,
default sizing methods, and cached row/strip/tile queries. Browse
@ref libtiff_public_image_layout and @ref libtiff_private_image_layout.
These operations describe the current directory; they do not replace the
@ref ifd_io metadata readers/writers or perform pixel transfers themselves.

## Choosing a byte count

| Family | What is counted | Important limits |
| --- | --- | --- |
| TIFFScanlineSize64()/TIFFScanlineSize() | Current scanline I/O estimate; one plane for separate storage | Packed YCbCr has a special integer-divided sampling-row estimate; not a general full-raster or sampling-group allocation |
| TIFFRasterScanlineSize64()/TIFFRasterScanlineSize() | All sample channels of a packed raster row | Contiguous rows round once; separate planes round independently before multiplication; no packed YCbCr group model |
| TIFFVStripSize64()/TIFFVStripSize() | Requested strip row count | UINT32_MAX means ImageLength; other values are not clipped to it |
| TIFFStripSize64()/TIFFStripSize() | min(RowsPerStrip, ImageLength) rows | Nominal decoded-size estimate, not the exact shorter last strip or stored encoded size |
| TIFFTileRowSize64()/TIFFTileRowSize() | Ordinary packed tile-width row | Does not model packed YCbCr sampling groups or multiply by TileDepth |
| TIFFVTileSize64()/TIFFVTileSize() | Requested tile row count | Requires nonzero configured tile dimensions; UINT32_MAX is not an all-image sentinel |
| TIFFTileSize64()/TIFFTileSize() | Configured TileLength rows | TileDepth is checked but is not multiplied into this implementation's result |
| TIFFRawStripSize64()/TIFFRawStripSize() | Stored strip/strile byte-count metadata | May load deferred arrays; not a decoded output-buffer size or proof that the bytes exist |

The normal signed-size functions delegate to their uint64_t equivalent and
_TIFFCastUInt64ToSSize(). A value above TIFF_TMSIZE_T_MAX is rejected with a
diagnostic and zero. Zero from a size calculation can also mean empty/invalid
geometry or an earlier arithmetic rejection; there is no independent status
result or general errno contract.

The raw-strip functions have different rules. A zero byte count is diagnosed
and mapped to UINT64_MAX by TIFFRawStripSize64(); a genuine metadata value equal
to that maximum is indistinguishable from the sentinel. TIFFRawStripSize()
maps that sentinel to minus one, but otherwise uses a cast/unsigned round-trip
check rather than the ordinary signed-size helper. A failed round trip gives a
diagnostic and zero. Very large counts that round-trip as a negative signed
value escape that check, including on 64-bit signed-size hosts. Do not treat
any nonzero signed raw result as an allocatable byte count. The TIFFstrip man
page's eight-hex-digit spelling of the 64-bit sentinel is not its actual width.

Raw metadata queries can allocate/grow caches, seek/read client I/O, and
invalidate borrowed array pointers. Their wrapper does not request the strile
WithErr status, so not every underlying load error is independently reflected.
The other core calculations allocate no output objects and do not decode
pixels, validate caller buffer capacity, select a different IFD, or persist
changed geometry. Getters and installed codec sizing callbacks can have
additional cache, diagnostic, allocation, and state effects.

## Packing and rounding

For ordinary contiguous data, the scanline estimate rounds
`ImageWidth * SamplesPerPixel * BitsPerSample` once to bytes. Separate scanline
I/O uses one plane. Full-raster sizing instead multiplies the per-plane rounded
byte width by SamplesPerPixel when planes are separate. For width three,
one-bit samples, and three channels, this yields two contiguous bytes versus
three bytes across separately rounded planes. A raster size is not automatically
the size required by a one-plane scanline transfer.

Packed contiguous, non-upsampled YCbCr strile sizing requires exactly three
samples and horizontal/vertical factors each in {1, 2, 4}. It counts complete
sampling blocks: each block has `h*v + 2` samples, width rounds to a number of
horizontal blocks, the complete horizontal sampling-row bit product rounds
once to bytes, and the result multiplies by `ceil(nrows/v)`. Width and height
padding therefore affect the returned extent. This path is not the layout for
arbitrary extra channels or a codec that has selected upsampled output.

TIFFScanlineSize64() divides the padded sampling-row byte count by vertical
subsampling using integer division. At low bit depths this truncates: width
five, one-bit samples, and 2x2 subsampling gives a three-byte sampling row,
a one-byte scanline estimate, and a three-byte two-row strip. Multiplying the
scanline estimate by two does not reconstruct that group. Use the matching
strile/codec contract for allocation, not an assumed reversible row average.
The scanline function also falls back to ordinary sample multiplication for
YCbCr with a sample count other than three, whereas packed strile sizing
rejects that combination. These calculators are not uniform image validators.

Tile-depth behaviour also needs explicit interpretation. Tile indexing/counts
include ImageDepth/TileDepth, but the size path only validates nonzero TileDepth
and multiplies row storage or sampling groups by nrows. It does not multiply
by TileDepth. This reference records that implementation rather than promising
a full multi-depth tile volume allocation.

Checked products do not make every rounding step checked. The conservative
TIFFhowmany_32() addition guard can return zero near UINT32_MAX without a direct
diagnostic, even where a quotient could otherwise be represented. The generic
64-bit ceiling macro uses unchecked addition, but current ordinary row field
widths bound the bit product below that addition-overflow boundary. Other paths
use TIFFhowmany8_64(). Establish representable layout arithmetic and heed each
function's rejection rules; an empty result and arithmetic rejection can share
zero. Diagnostic assertions in the man pages saying "None" do not cover the
current invalid-geometry and overflow paths.

## Strip and tile indices

TIFFComputeStrip() uses `row / RowsPerStrip`, adding
`sample * cached_strips_per_image` for separate planes. The cache must already
match the current geometry. It checks separate-plane sample bounds and selected
arithmetic failures but does **not** check/clamp row to ImageLength or validate
organisation and strile arrays. A zero return can be the first strip or an
error. TIFFNumberOfStrips() recomputes from ImageLength/RowsPerStrip rather than
returning allocated array capacity. It includes separate planes but not
ImageDepth. RowsPerStrip UINT32_MAX yields one strip per plane even for zero
ImageLength; zero RowsPerStrip warns and returns zero.

TIFFComputeTile() substitutes image extents for UINT32_MAX tile dimensions and
computes x-major, then y, z, and separate-sample offsets. It does not clamp or
validate x/y/z. ImageDepth one forces z to zero, and contiguous storage ignores
the sample argument. Zero effective tile dimensions return the initial index
one silently; checked sample/arithmetic rejection returns zero, also the first
valid index. The TIFFstrip/TIFFtile man-page claim that these computation
functions always clamp to valid coordinates does not match this source.

TIFFCheckTile() checks x/y/z against image dimensions, including z for a
single-depth image, and checks the sample only for separate planes. It does
not validate tile dimensions, organisation, arithmetic, array extent, or codec
support. A successful coordinate check alone does not make zero tile geometry
usable. TIFFNumberOfTiles() counts ceil divisions over width/length/depth and
separate planes; it neither allocates arrays nor returns their cached capacity.

## Default sizing methods and traversal state

TIFFDefaultStripSize() and TIFFDefaultTileSize() synchronously dispatch the
installed TIFFStripMethod/TIFFTileMethod. Codec overrides may adjust a supplied
request and have their own state/allocation/diagnostic effects. The dispatcher
does not validate the recommendation, set directory tags, or provide an
independent success status. Keep installed method code/context live and
serialise handle access. Returned scalar/width/height values remain caller-owned;
no allocation ownership transfers.

The built-in strip heuristic interprets the uint32_t request as int32_t. Zero
and high-bit requests therefore select a heuristic, while positive signed-range
requests pass through. The heuristic divides STRIP_SIZE_DEFAULT (normally 8192)
by the scanline estimate, using one byte when that estimate returned zero, then
clamps to at least one row. A scanline diagnostic does not prevent a positive
recommendation. This estimates bytes; it does not guarantee a legal or optimal
codec strip geometry until the codec's method rules are applied.

The built-in tile method similarly interprets request bits as signed values,
uses 256 for nonpositive requests, and rounds each positive width/height to a
multiple of sixteen. Its tif parameter is unused. Codec overrides can impose
other rules; no storage, tags, or I/O resources are owned by the default method.

TIFFCurrentRow(), TIFFCurrentStrip(), and TIFFCurrentTile() directly return
cached traversal indices. Reset installs UINT32_MAX as no-current state. Reads,
writes, codec work, and failures can move these indices, so they are not proof
of a successfully transferred row/strile and do not uniformly predict the next
operation. Scanline transfer paths can record row + 1 after processing,
including some decoder failures, so TIFFCurrentRow() can equal ImageLength
after the last row. These queries perform no allocation, I/O, mutation, or
diagnostics.

## Coverage

The callable contracts cover public functions, private functions, and private
sizing-method typedefs. Every function in tif_strip.c and tif_tile.c has a
canonical header contract; the direct traversal queries in mixed-purpose
tif_open.c are included. Existing geometry members,
STRIP_SIZE_DEFAULT, and arithmetic macros retain their foundations/shared-service
contracts. Public contracts live in tiffio.h; private calculations and method
types are documented in tiffiop.h.

The @ref image_transfers module covers pixel transfers, codec preparation/
seeking, raw and encoded scanline/strip/tile access and buffer ownership in
tif_read.c and tif_write.c. Strile metadata loading, directory persistence and
lifecycle flush/close interfaces retain their respective module contracts.
See @ref documentation_completion for the module inventory.

@page image_transfers Image transfers and buffers

The @ref libtiff_public_image_reading and @ref libtiff_public_image_writing
interfaces operate on the current directory and installed codec. Establish
valid image geometry first; use the @ref image_layout queries for decoded
allocation estimates and the @ref ifd_io interfaces for stored strile metadata.

## Raw bytes and decoded samples

Raw strip/tile reads copy stored bytes without sample conversion, fill-order
reversal, or decompression. Encoded reads decode into caller storage and apply
postdecode conversion. Coordinate tile interfaces select a tile and transfer
its full decoded extent, including edge padding. Scanline access requires
strip organisation. Codec support determines whether random/backward scanline
access works.

The minus-one extent convention differs across interfaces. Reads use it for
the complete raw or decoded strile. TIFFWriteEncodedTile() treats every
nonpositive or oversized request as a full tile. TIFFWriteEncodedStrip()
requires a nonnegative byte extent and does not implement that sentinel.
Successful strile writers return accepted input bytes, not compressed size.
TIFFWriteScanline() returns codec status; zero and minus one are failures.

Encoded writes require writable input. Sample swapping can change it in place;
uncompressed paths can also reverse its bits. Neither success nor failure
restores the original samples. Raw writes expect an already encoded, file-order
representation and do not establish its validity.

## Buffer ownership

TIFFReadBufferSetup() and TIFFWriteBufferSetup() replace the previous raw
buffer. A supplied buffer is borrowed and must outlive its use by the handle.
A NULL pointer requests handle-owned allocation. Replacement can discard old
contents, invalidate old views, and fail after releasing old owned storage.
Flush pending output before replacing its buffer. The write size-minus-one
convention forces an owned default allocation even when a pointer was supplied.

Private allocate-and-decode helpers allocate only when the caller's output
pointer is NULL. They retain that allocation if a later decode fails; the
caller must release it with the corresponding Ext allocation family while the
handle is live. The supplied allocation size must cover the effective decoded
request; these helpers do not prove that relation.

TIFFReadFromUserBuffer() temporarily attaches writable caller input and can
reverse its bits. It restores the saved raw pointer/capacity and ownership bits,
but does not restore every cursor or codec state. In particular, a raw cursor
can still refer into the caller's input. Keep the input live until decoding is
restarted or the raw input is replaced. This API requires a valid strile index
and geometry established by its caller.

## Failure state and persistence

Transfers can load metadata, allocate, invoke callbacks, and move the client
file position. Failures can leave partial output, changed codec progress,
current indices, dirty arrays, or partially written file bytes. Discard failed
decoded output. There is no general rollback or errno contract. A successful
pixel transfer does not persist its directory or promise durable storage;
follow the @ref lifecycle flush/close contracts.

Private TIFFFlushData1() resets its pending-byte count even when append fails.
Retrying a flush does not recover the lost pending extent. Strile appends can
relocate an overwritten extent and change offsets/counts before later I/O
fails. TIFFSetWriteOffset() changes cached state without seeking or checking
that the proposed output region is safe.

## Coverage

This module documents every function definition in tif_read.c and tif_write.c,
the public compression-ratio dispatch query, the private codec method types,
and the local transfer macros. Public contracts live in tiffio.h and private
contracts in tiffiop.h where declarations already exist; file-local helpers
and the source-only fill interfaces retain their contracts in the source.
Codec implementations and image conversion have their own module contracts;
see @ref codecs, @ref conversion, @ref rgba and @ref documentation_completion.

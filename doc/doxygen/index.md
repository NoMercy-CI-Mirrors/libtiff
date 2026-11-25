# LibTIFF library reference

LibTIFF reads and writes Tag Image File Format images. This reference brings
the public library contracts and private implementation documentation together
in one searchable site. The Sphinx manual and man pages provide complementary
usage documentation.

- @ref libtiff_public "Public API": interfaces available to applications.
- @ref libtiff_private "Library internals": implementation details with no
  stability guarantee for external consumers.

## Foundations

The foundations module covers the types used to describe a TIFF file,
identify the library build, and represent supporting library state:

- @ref libtiff_public_types "Types and file headers": scalar types, classic
  TIFF and BigTIFF headers, and TIFFDataType values.
- @ref libtiff_public_version "Version information": TIFFGetVersion() and
  compile-time version macros, including TIFFLIB_AT_LEAST().
- @ref libtiff_public_configuration "Legacy build configuration": the configured
  public header and its compatibility definitions.
- @ref libtiff_public_tiff_tags "TIFF tags and values",
  @ref libtiff_public_dng_tags "DNG", @ref libtiff_public_ep_tags "TIFF/EP",
  @ref libtiff_public_exif_tags "EXIF", and @ref libtiff_public_gps_tags "GPS"
  catalogues, plus the @ref libtiff_public_pseudotags "pseudo-tag controls".
- @ref libtiff_public_data "Public data structures and constants": supporting
  layouts, ownership, print flags, colour constants, and packed-pixel accessors.
- @ref libtiff_private_foundations "Foundations internals": the header union,
  version storage, private handle state, utility macros, and build configuration.

See @ref foundations for context and @ref documentation_completion for the
completed module inventory, validation evidence and conditional coverage limits.
A symbol missing from a configured reference is not necessarily unavailable.

## Lifecycle

The @ref lifecycle "lifecycle guide" explains resource ownership, open modes,
callbacks, flush/close status, and platform/stream limitations. Browse:

- @ref libtiff_public_lifecycle "Opening, handle queries, flushing, and closing".
- @ref libtiff_public_open_options "Open options and diagnostic context".
- @ref libtiff_public_io_callbacks "Client I/O callback contracts".
- @ref libtiff_public_streams "C++ stream opening".
- @ref libtiff_private_lifecycle "Lifecycle implementation" and
  @ref libtiff_private_streams "stream adapter internals".

## Shared services

The @ref shared_services "shared-services guide" explains allocation families,
checked arithmetic and destructive array copies, diagnostic routing, client
payloads, and hash-set ownership. Browse @ref libtiff_public_memory,
@ref libtiff_public_diagnostics, @ref libtiff_public_client_info, or the
@ref libtiff_private_services "private shared services", including
@ref libtiff_private_hash "hash sets".

## Directories

The @ref directories "directory guide" introduces field definitions, borrowed
metadata, extension methods, and failure state. Browse
@ref libtiff_public_fields and @ref libtiff_public_tag_access, or their
@ref libtiff_private_directories "private counterparts". Read
@ref tag_arguments before using variadic getters and setters. The
@ref directory_navigation guide covers creation/freeing, queries, navigation,
unlinking, and private chain maps. The @ref ifd_io guide covers IFD reading,
writing, entry conversions, strile loading, and private serialisation helpers.
The @ref directory_printing guide covers flags, callback order, custom
formatting, and private ASCII helpers.

## Image I/O

The @ref image_layout guide covers size estimates, packing and rounding,
strip/tile indices, sizing callbacks, and traversal queries. Browse
@ref libtiff_public_image_layout and @ref libtiff_private_image_layout. The @ref image_transfers guide covers raw
and encoded transfers, buffer ownership, and error state. Browse @ref libtiff_public_image_reading,
@ref libtiff_public_image_writing, @ref libtiff_public_image_buffers, and
@ref libtiff_private_image_transfers.

## Image conversion

The @ref conversion guide covers sample byte/bit operations and Lab, XYZ, and
YCbCr colour conversion. Browse @ref libtiff_public_conversion and
@ref libtiff_private_conversion. The @ref rgba guide covers whole-image and
strile RGBA readers, state lifetime, and private packing callbacks. See
@ref logluv_conversion for LogL/LogLuv code patterns, XYZ conversions, chromaticity quantisation, and dithering.

## Compression

The @ref codecs guide explains registration, capability queries, descriptor
ownership, and fallback dispatch. Browse @ref libtiff_public_codecs and
@ref libtiff_private_codecs. The codec guide links every built-in codec, its
state, transfer methods, dependency interface and tag contracts.

## For contributors and downstream documentation

The @ref documentation_guide "documentation guide" describes comment contracts,
the module sequence, build/install commands, coverage review, and how to link
from a separate Doxygen project using `libtiff.tag`. The tagfile accompanies
this site's HTML and includes both public and private entries. Tools and tests
are not processed as part of this library reference.

See @ref documentation_completion for the module inventory, the reproducible
XML structural checks and known limitations.

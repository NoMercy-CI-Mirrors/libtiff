# Foundations {#foundations}

## File representation and API types

@ref libtiff_public_types distinguishes file representation from API argument
representation. TIFFHeaderClassic has a 32-bit first-directory offset;
TIFFHeaderBig has a 64-bit offset and additional header fields. Both begin with
TIFFHeaderCommon's byte-order and version fields. LibTIFF decodes numeric header
fields to host byte order when opening a file; the magic value continues to
identify the file's byte order.

TIFFDataType identifies directory-field storage. For example, TIFF_RATIONAL
stores an unsigned 32-bit numerator and denominator. This does not specify the
argument type required by a variadic field accessor: follow that accessor's
contract and the field's type/count rules. Tag and compression constants name
format values; their existence is not a promise that a codec or extension is
implemented in a particular library build.

Use tmsize_t for the signed memory/I/O sizes expected by the API, toff_t for
unsigned file byte offsets, and tdir_t for directory indices. They are distinct
concepts even when integer conversions compile. The TIFF handle is opaque to
applications; private fields visible elsewhere in this site are not part of
its application contract.

Internally, TIFFHeaderUnion holds the decoded classic TIFF or BigTIFF header.
Its members are inline values and have no independent allocation or cleanup.

## Tag namespaces and predefined values

The catalogues distinguish identifiers used in different directory namespaces:

- @ref libtiff_public_tiff_tags lists TIFF tags, compression identifiers, and
  predefined values and flag bits, including extension/vendor constants.
- @ref libtiff_public_dng_tags identifies Digital Negative fields.
- @ref libtiff_public_ep_tags identifies TIFF/EP fields in the image directory.
- @ref libtiff_public_exif_tags identifies EXIF-directory fields.
- @ref libtiff_public_gps_tags identifies GPS-directory fields.
- @ref libtiff_public_pseudotags lists in-memory controls and their predefined
  values; these are not written as TIFF directory fields.

Equal integers do not make tags interchangeable between directory namespaces.
Conversely, some named aliases deliberately share a value. The catalogue
documents the purpose of each constant, not a complete tag-access calling
convention or a promise that every listed extension is implemented. Consult
the directory and codec modules for variadic types/counts, valid ranges,
codec availability, and changes to handle state.

## Supporting structures and storage ownership

@ref libtiff_public_data documents public layouts and their member roles.
TIFFDisplay and TIFFCIELabToRGB contain inline parameters/tables.
TIFFYCbCrToRGB instead points into a caller-provided allocation that must hold
both the structure and the tables laid out after it. Its members must not be
freed as separate allocations.

For successfully initialised TIFFRGBAImage state, TIFFRGBAImageEnd() releases
reader-owned tables; it does not close the borrowed TIFF handle or free the
caller-provided state and output raster. Descriptor types have their own
lifetime rules: for example, legacy TIFFFieldInfo registration retains its
non-NULL name pointers rather than copying the strings.

@ref libtiff_private_state documents the private handle layout, open settings,
named client-data links, directory-index pairs, state flags, and utility macros.
Client-info names and nodes belong to the handle, but the associated payloads
remain client-owned. Opening copies the settings in TIFFOpenOptions, including
callback-context pointers; freeing the options does not extend those contexts'
lifetimes. Pointers into buffers and directory storage can be invalidated by
operations that replace that storage, well before the handle is closed.

Arithmetic helpers are not interchangeable. TIFFhowmany_32() has a conservative
addition guard that can return zero at the equality boundary;
TIFFhowmany_32_maxuint_compat() avoids that addition. TIFFhowmany_64() and
TIFFroundup_64() do not check overflow. TIFFSafeMultiply() requires an unsigned
multiplication type that is not promoted to a signed type. The floating-point
comparison macros preserve their current NaN behaviour; they are not general
IEEE equality predicates. These details describe the implementation and do not
change it.

## Header version versus runtime version

@ref libtiff_public_version separates two questions:

- The TIFFLIB_* macros describe the headers used when compiling the application.
- TIFFGetVersion() describes the loaded library and returns a borrowed immutable
  banner that remains valid while that library is loaded.

For a compile-time release check, use a side-effect-free expression such as:

```c
#if TIFFLIB_AT_LEAST(4, 5, 0)
/* Compile code requiring headers from version 4.5.0 or later. */
#endif
```

This check does not test the runtime library or the availability of a particular
codec. TIFFLIB_VERSION is the historical release-date integer; it is not a
packed major/minor/micro number. The private TIFFVersion array provides the
runtime banner without allocation or state changes.

## Configured compatibility definitions

@ref libtiff_public_configuration documents the header generated for the current
build. Conditional feature definitions may be absent, and legacy markers that
are always defined must not be used as capability tests. Use standard integer
types in new application code and TIFFIsCODECConfigured() to query codec
availability. HOST_FILLORDER is a legacy policy constant, not a CPU byte-order
test.

@ref libtiff_private_configuration covers the non-installed generated header:
platform probes, private codec switches, large-file controls, and printf format
fragments. CMake and Autotools have different platform-probe sets. Common
definitions carry matching descriptions, while build-system-specific entries
are documented only where they exist. Conditional features remain undefined
when disabled, so this build's rendered reference cannot show every platform
variant at once.

## Coverage

| Location | Reviewed documentation |
| --- | --- |
| `tiff.h` | Catalogue macros after TIFFDataType, format/version signatures, deprecation wrappers, legacy integer typedefs, uint16_vap, the header structures and their members, and TIFFDataType with its enumerators |
| `tiffio.h` | Non-callback types and public data layouts with every member, all non-guard macros (including TIFFGetR/G/B/A and TIFF_ATTRIBUTE), scalar/handle types, and TIFFGetVersion() |
| `tiffvers.h` templates | All version macros, including parameters and evaluation behaviour of TIFFLIB_AT_LEAST() |
| `tiffconf.h` templates | Configured integer types, compatibility policies, conditional features, and obsolete always-defined markers |
| `tif_config.h` templates | Every configuration/probe/format macro in each template, including conditional fallback definitions |
| `tiffiop.h` | TIFFHeaderUnion, the other state-record layouts and their members, legacy byte aliases, TIFFIOSize_t variants, every non-guard macro and its parameters/results |
| `tif_version.c` | TIFFVersion storage; the callable contract remains in tiffio.h |

The CMake and Autotools templates carry matching documentation. Generated output
reflects the current CMake configuration; unavailable conditional features do
not acquire definitions merely to make them visible in the reference.

Include-guard macros are intentionally excluded from the symbol contract
inventory. Callable contracts, directory-private types, predictor/codec state
and source-local helpers are documented in their own modules; client I/O
callbacks and open options are documented in @ref lifecycle. See
@ref documentation_completion for the complete module inventory. A member's
role description complements the applicable callable contract.

# Coverage and validation {#documentation_completion}

This reference covers public declarations in the installed headers and
private declarations in their existing headers, with source-local contracts
beside their definitions. This site and its tagfile include both
public and private groups. Private entries confer no ABI or API stability.
Sphinx HTML and man pages remain separate, complementary documentation.
Standalone tools, tests, `contrib`, `archive`, `spike` and tool-only `port`
code are outside this reference's input set.

## Module inventory

| Part | Contracts and implementation coverage |
| --- | --- |
| @ref foundations | File/API types, tag/value catalogues, structures and members, version/configuration templates, private state and utility macros |
| @ref lifecycle | Opening, options, client I/O, flushing/closing, Unix/Windows backends and C++ streams |
| @ref shared_services | Allocation/arithmetic, diagnostics, client information, callback chains and hash sets |
| @ref directories and @ref tag_arguments | Field schemas, tag access, variadic argument families, extension hooks and private directory state |
| @ref directory_navigation | Creation/freeing, directory queries, chain navigation/unlinking and maps |
| @ref ifd_io | IFD reading/writing, entry conversion, strile metadata and serialisation helpers |
| @ref directory_printing | Public flags, printer callbacks and source-local formatting |
| @ref image_layout and @ref image_transfers | Sizes, row/strip/tile traversal, raw/encoded transfers and buffer ownership |
| @ref conversion and @ref rgba | Byte/bit operations, Lab/XYZ/YCbCr, RGBA state/getters/maps/packers and callbacks |
| @ref codecs and @ref predictor | Registry/fallback dispatch, codec callbacks and horizontal/floating-point prediction |
| @ref basic_codecs and @ref lzw | Uncompressed, PackBits, ThunderScan, NeXT and LZW |
| @ref deflate, @ref zstd and @ref lzma | Deflate/zlib/libdeflate, Zstandard and LZMA |
| @ref webp, @ref jbig and @ref lerc | Optional dependency codecs, staging/masks and codec-specific tags |
| @ref fax | CCITT RLE/RLEW, Group 3/4, run macros and generated Huffman/state tables |
| @ref pixarlog, @ref logluv_conversion and @ref sgilog | PixarLog tables/transfers, public LogL/LogLuv conversions and SGILog packing |
| @ref jpeg-support and @ref jpeg | JPEG dependency wrappers, managers, tables, setup, transfers and eight/twelve-bit dispatch |
| @ref ojpeg | Decode-only legacy JPEG, marker/table parsing, reconstruction chunks and dependency sessions |

Coverage extends to conditional helpers, anonymous aggregates, callback
members and generated tables. Include guards are not symbol contracts.
An unused declaration named TIFFSetDefaultCompressionState() has no definition
in this tree; its documentation explicitly forbids calling it and identifies
_TIFFSetDefaultCompressionState() as the implemented private helper.

The fax table generator emits the source-file documentation, so regeneration
preserves it. The checked-in chromaticity table in `uvcode.h` has no generator
in this tree; its maintained snapshot carries documentation without changes to
its numeric initialisers.

## Repeating structural checks

Doxygen preprocessing controls extraction. A quiet warnings log cannot establish
that inactive branches were inspected, nor that a contract is semantically
correct. Use source review alongside the XML checks, and inspect generated
pages and cross-references after changing comments or preprocessing.

`-Ddoxygen-checks=ON` enables XML output and runs these checks, together with
the warnings-log check, as the `tiff-doc-doxygen-check` target (see
@ref documentation_guide). To run them by hand, XML output uses the same
CMake-generated Doxyfile:

```sh
cmake -S . -B build-doxygen -Dtiff-docs=ON -Ddoxygen=ON \
  -DDOXYGEN_GENERATE_XML=YES
cmake --build build-doxygen --target tiff-doc-doxygen
python3 doc/doxygen/check-xml.py \
  --warnings-log build-doxygen/doc/doxygen/warnings.log \
  build-doxygen/doc/doxygen/xml
```

With `--warnings-log`, any line in the Doxygen warnings log other than a
Graphviz graph size limit is also reported. Doxygen warns about undocumented
declarations and invalid markup, which the XML checks cannot see because
undocumented declarations are not extracted.

`check-xml.py` checks unique extracted members, compound types and source files
for briefs, enumerators for descriptions, function and function-like macro
parameters for matching names and directions, and non-void functions for an
`@return` or `@retval` contract. Doxygen emits no parameter list for
function-pointer typedefs or callback members, so their parameter documentation
is not checked automatically and needs source review. Doxygen merges the two
source-local `PACK` macro arities; their documented prose argument contracts are
an explicit exception to automatic parameter matching. Zero-argument macros
need no artificial parameter documentation. These checks cannot verify error,
ownership, lifetime or side-effect claims; those require reading the source.

## Conditional views and known limitations

Doxygen extraction follows the configured preprocessing view. Debug/unused hash
helpers, optional sorted-strile helpers, Windows CE, external JPEG
encapsulation, twelve-bit/MK1 JPEG code and 32-bit size selection can be
checked by running the structural checks against separate documentation-only
preprocessing views. Such views do not compile or execute those
platform/dependency variants. The literal `#if 0` rational-conversion
experiment remains excluded from normal extraction and is documented as
disabled in its source. The normal site explicitly exposes JBIG source
contracts and the IJG 9d compatibility helper even when unavailable in the
host dependency configuration; see @ref documentation_guide.

The documentation work preserves executable tokens and declaration types;
parameter names are added to declarations where useful. Known runtime
discrepancies are recorded in the affected contracts rather than repaired by
documentation changes:

- The CCITT RLE/W bilevel round trip emits a premature end-of-line warning and
  returns mismatched pixels, while RLE, Group 3 and Group 4 round trips
  succeed; see @ref fax.
- Legacy JPEG decoding can prematurely destroy its session when called once
  per scanline; see @ref ojpeg.
- The default eight-bit grayscale-with-alpha RGBA packer copies unassociated
  alpha without premultiplying, unlike other RGBA paths; see @ref rgba.

Ordinary CTest success does not establish all optional codec paths or
malformed-input behaviour.

## Build and installation

The reference is disabled by default. Configurations with it disabled omit the
target, while enabling it without a suitable Doxygen fails. Documentation
installation includes the generated HTML and tagfile. The reference also
builds without Graphviz. Graphviz emits interactive linked SVG; very large
call/caller graphs can be omitted at Doxygen's default 50-node limit,
producing graph-size warnings rather than contract warnings.

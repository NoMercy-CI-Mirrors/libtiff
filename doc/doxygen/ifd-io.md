# Reading and writing IFDs {#ifd_io}

This module documents the image/custom IFD readers and writers, their entry
conversion and serialisation helpers, deferred strile metadata, and logical
strip subdivision. Start with @ref directories for schemas and value ownership,
@ref tag_arguments for tag calls, and @ref directory_navigation for chain
indices, offsets, and selection. Browse @ref libtiff_public_ifd_reading,
@ref libtiff_public_ifd_writing, and @ref libtiff_public_strile_metadata,
or the corresponding private groups.

## Reading and recovery

TIFFReadDirectory() follows the cached next image-IFD offset, fetches entries,
checks directory geometry and tag representations, configures the codec, and
installs owned in-memory values. It does not decode pixels. Unknown fields can
be registered anonymously. Duplicate entries are ignored after the first;
misordered tags can warn; some counts/types are recovered, trimmed, or ignored.
Missing/suspect byte counts can be estimated, and enabled strip chopping can
replace the logical strip geometry. Success does not mean every input entry
was preserved or that all pixel ranges/codec operations will succeed.

An end-of-chain call returns zero, sets the current offset to zero, and retains
old tag values. Other zero returns can occur after offsets, index, flags,
schema, codec state, or values have already changed. A valid raw fetch advances
the index before later tag/geometry validation finishes. No rollback preserves
the previous directory. The reader can allocate, access mapped bytes or client
seek/read callbacks, and invoke setters, extenders, codec hooks, and diagnostic
callbacks. It does not save dirty old values first. Borrowed values and
replaceable descriptors can be invalidated during replacement.

TIFFReadCustomDirectory() fetches entries at an explicit offset, installs
intermediate defaults followed by the requested schema, and reads custom
values. TIFFReadEXIFDirectory()/TIFFReadGPSDirectory() supply built-in schemas.
They do not read an image or follow a metadata NextIFD chain. The schema wrapper
is only needed during setup; descriptor/name storage remains borrowed while
registered. The final schema replaces additions made by the image extender.

Custom reader success is particularly permissive: its main loop ignores
individual normal-tag and SubjectDistance fetch/set results. Type/count errors,
unknown registration failures, and optional-value errors may coexist with a
one return and missing values. Structural fetch/bookkeeping failure returns
zero, possibly after state replacement. Successful completion forces the next
indexed selection to restart at the main header, but does not comprehensively
reset cached index/next-link state. Explicitly reselect the required parent
image directory before using image APIs or linking new metadata.

## Entry decoding layers

| Layer | Input and result | Ownership and checks |
| --- | --- | --- |
| TIFFFetchDirectory() | Raw IFD bytes to a parsed TIFFDirEntry array | New Ext allocation; tag/type/count normalised, offset/inline bytes remain raw; counts above 4096 rejected |
| TIFFReadDirEntryData()/DataAndRealloc() | External byte ranges | Fixed borrowed destination or growing owned slot; no element conversion; short reads can leave partial data |
| TIFFReadDirEntryArray()/ArrayWithLimit() | Raw inline/external array bytes | New Ext allocation; source/destination byte ceilings; no element swapping/conversion |
| Typed scalar readers | One entry to a scalar | Count exactly one, supported source-type selection, integer range checks; no owned output |
| Typed array readers | Entry data to host-order typed elements | New Ext allocation, conversion/range checks, temporary cleanup; NULL can be a successful empty result |
| Checked scalar readers | Prevalidated representation to scalar | Usually rely on caller-validated type/count/placement; “Checked” is not an arbitrary-input safety guarantee |
| TIFFFetchNormalTag() | Representation-specific value to TIFFSetField() | Temporary conversions released after setter dispatch; recover changes error severity, not conversion failure status |

Release successful temporary reader arrays with _TIFFfreeExt() using the same
live handle/accounting context. Supply empty output owner slots: these readers
do not uniformly free prior caller allocations. Output initialisation differs
between paths; use only documented success outputs. In particular the raw array
helper can return Ok with NULL for an empty or unknown-width value **without
assigning its count output**. Some later failures assign count but not value.
Growing external reads retain their latest partially filled allocation on
failure, so their caller must still release it.

Scalar and array readers list accepted file types in their canonical contracts.
Integer narrowing/sign changes use explicit range helpers. Floating conversion
can lose precision. Scalar double-to-float uses a C cast, while the float-array
DOUBLE path clamps values beyond the finite float range first. RATIONAL zero
denominators produce zero in ordinary conversion; SRATIONAL readers interpret
a signed numerator with an unsigned denominator. EXIF SubjectDistance instead
maps a zero numerator to zero and UINT32_MAX numerator/zero denominator to
minus one. The existing man-page zero-denominator diagnostic is not a universal
promise for these conversion paths.

Per-sample short reduction warns for missing/excess counts, assumes missing
nonzero entries equal the first, and rejects differing inspected samples.
It writes the first output before detecting later differences. Raw fetch
accepts a missing/truncated trailing next-link as zero; this is not the same
as validating the entire file. TIFFFetchNormalTag() returns zero for conversion failures even when recover
selects warning severity. Its UNDEFINED-representation case instead diagnoses
and skips the value while returning one. Parent readers decide which statuses
to ignore. Many low-level failures are returned as
TIFFReadDirEntryErr and diagnosed by the caller; allocation-policy and selected
large-size rejections can already have emitted diagnostics.

The experimental TIFFReadDirEntryCheckedRationalDirect() remains under literal
`#if 0`: its source contract is documented, but it is neither compiled nor
part of the normal generated symbol reference. No production feature is enabled
solely for documentation. Native/IEEE no-op macros have explicit contracts;
non-IEEE alternatives remain external port-supplied hooks without implementations
in these files.

## Deferred and lazy strile metadata

A strile is a strip or tile, including separate sample planes. The four
TIFFGetStrileOffset/ByteCount APIs return logical byte offsets/counts, possibly
from chopped arrays rather than the original on-disk entries. They do not
decode pixels or validate that the payload range is readable. Zero can be a
valid value or a failure. WithErr outputs report errors detected by the
**current call**, not an enduring error history.

Deferred loading can materialise full arrays; lazy loading grows paired caches
and reads neighbouring metadata using 4096-byte pages. Allocation/I/O can occur
in a getter and invalidate borrowed array pointers. Failure behaviour matters:

- Failure of either paired-cache realloc frees both current arrays and loses
  the cached values.
- A failed lazy value fetch stores zero in its slot. A later call can find
  that zero cached and return it without repeating the earlier error.
- A failed full fill can set pbErr yet still return an available cached value.
- Full fill clears deferred entries after attempts, including failures. It
  does not guarantee equivalent retries or revalidate every already-loaded array.

Short strile arrays can be padded with zero up to the expected logical count.
The default expansion limit is one million elements and can be changed by
`LIBTIFF_STRILE_ARRAY_MAX_RESIZE_COUNT`, parsed through atoi; this is an
implementation recovery control, not a comprehensive resource quota. Additional
size/file-extent checks apply. The lazy page helper accepts nonconforming
SLONG8 storage by casting signed values to uint64_t, without the nonnegative
range checks used by the full typed reader. Its page index must fit int. Excess values are read only up to the required
logical count. Successful arrays belong to the directory.

Eligible single uncompressed strips can be subdivided toward
STRIP_SIZE_DEFAULT; verified huge contiguous uncompressed strips can be
subdivided toward 512 MiB. Chopping respects rowblock/YCbCr geometry, replaces
owned offset/count arrays, adjusts RowsPerStrip, and marks logical chopped
arrays. It does not rewrite the file. Unsupported geometry, suspected file
extent, or allocation failure can leave the old layout. Some metadata can
already have been loaded before chopping declines to proceed.

## Writer modes and state

| Operation | Completion and retention | File placement and output |
| --- | --- | --- |
| TIFFWriteDirectory() | Finishes postencode/encoder close, flushes applicable raw bytes, releases old values and prepares defaults | Links/writes an image or pending SubIFD; can overwrite a fitting IFD or relocate when needed |
| TIFFCheckpointDirectory() | Keeps values/codec state and does not perform final encoder/raw teardown | Writes image metadata then seeks to EOF/resets the image write offset even on failure |
| TIFFWriteCustomDirectory() | Keeps custom values; no encoder finalisation | Reuses a fitting custom span or appends; caller must link its returned offset through a parent tag |
| TIFFRewriteDirectory() | Finishes/resets like WriteDirectory | Detaches the old main-chain/header link before relocation; old bytes are not reclaimed |

The shared writer has a read-only success no-op. For a custom write, that path
returns one **without assigning the offset output**. Checkpoint's surrounding
strip-setup/EOF-seek work can still run. Rewrite's relocation wrapper has no
initial read-only guard, so its existing-offset case must only be used with a
writable handle. Rewrite requires a main-chain-reachable IFD; it is not a general
SubIFD relocation API. With current offset zero its private wrapper delegates
to TIFFWriteDirectory(), ignoring requested custom/retention modes.

A checkpoint is not a guarantee that pending codec/pixel data is complete or
readable. It ignores the initial strip-setup result, and its EOF seek has no
separate status. Writer internals do not uniformly propagate strile-fill,
final map-check, and fresh-default failures. Error diagnostics can therefore
coexist with a one return. No writer promises fsync or crash durability.

All write operations can alter file bytes, links, offsets/maps/flags, buffers,
and values before a detected failure. Rewrite clears the predecessor/header
link before replacement succeeds, so a failure can leave the chain detached.
External payload writes can precede the final IFD write. There is no rollback.
The caller decides how to handle pending dirty values and recover a failed
client resource. No general errno contract replaces the documented return and
diagnostic handling.

## Serialisation helpers and array mutation

The tag writers run a sizing pass with dir NULL and an emitting pass with a
writable entry array. Checked emitters generally account for entry/external
payload space in the first pass; wrappers can still inspect values, convert,
and allocate while sizing. Counts/byte products must satisfy each representation's
bounds. Some bounds return zero silently, while others use diagnostics or debug
assertions. The entry insertion helper requires unique tags and spare capacity;
it can shift/initialise entries before payload failure without incrementing the
completed-entry count.

Multi-byte checked integer, IFD, float, and double array writers can **swap the
original array in place**. Non-IEEE hooks can also convert floating storage in
place. These arrays remain mutated after emission or a later write failure.
Some wrapper paths create conversion copies, while direct BigTIFF paths can
pass original directory storage through. Retention by checkpoint/custom writers
therefore does not imply unchanged host-order array contents. This limitation
is documented without changing runtime behaviour.

Classic TIFF wrappers range-check narrowing of LONG8/SLONG8/IFD8 arrays into
32-bit representations; byte-count writers may choose SHORT/LONG/LONG8 using
strile-size and compression heuristics. The heuristics are not proof of actual
compressed size. The raw payload helper expects already encoded file-order
bytes, stores small payloads inline, and otherwise writes at an even-aligned
data offset with size/wrap checks. It does not perform arbitrary conversion.

The six scalar integer clamping helpers map signed NaN to the target minimum
and unsigned NaN to the maximum, then truncate finite in-range values. Rational
writers build bounded numerator/denominator pairs in temporary arrays:

- Unsigned conversion diagnoses negative/NaN through global TIFFErrorExt()
  and returns zero/zero; array emission can continue despite that diagnostic.
- Unsigned overflow produces UINT32_MAX/zero. Signed magnitude overflow,
  including negative infinity, produces positive INT32_MAX/zero and loses the sign.
- Signed rational conversion requires non-NaN input; the implementation has no
  safe NaN conversion path.
- Exact integers use denominator one; tiny magnitudes use a zero numerator
  and the maximum denominator. Other values compare two bounded GCD approximations.

These void converters do not return a success status; diagnostics or zero
denominators must not be confused with an aborted tag write. The scalar
RATIONAL writer separately rejects negative/NaN values before sizing/emission.
The local MAX_ITERATIONS bounds approximation-loop iterations, not all I/O or
setup work.

## Deferred writing and in-place field patching

TIFFDeferStrileArrayWriting() requires a writable directory with current offset
zero. It makes strile tags temporary zero-type/count/offset placeholders and
affects only that directory. The established sequence is to set tags, defer,
call TIFFWriteCheck(), write the directory, reselect that IFD after any other
work, then call TIFFForceStrileArrayWriting(). Actual pixel-writing order and
codec completion still need to follow their image-I/O contracts.

Force requires a writable already-written non-mapped directory without other
dirty metadata. It accepts dirty striles or the matching pair of placeholders,
possibly allocates strip arrays, patches offsets first and counts second, and
clears dirty-strile/been-writing flags only when both patches succeed. Failure
can leave just the first field patched; it does not finalise codec/raw data.

_TIFFRewriteField() operates on an existing on-file entry. It copies borrowed
input, narrows supported integer/IFD widths with range checks, swaps the copy,
and overwrites a compatible payload or appends/replaces it and patches the
entry. The original input stays unchanged. It can update deferred type/count
bookkeeping before write success and does not reconcile arbitrary in-memory
tag values. Some seeks are unchecked; the unsupported-conversion error path
leaks its temporary buffer. It is not a general rational/float conversion API
merely because it can copy/swap equal-width bytes. File/cache effects have no
rollback and no ownership is returned.

## Coverage

The callable inventory comprises public APIs, private declared interfaces and
source-local helpers, including the disabled raw-rational experiment. The
chain-map functions and callbacks keep their canonical contracts in
@ref directory_navigation. Together these inventories cover every function
definition in tif_dirread.c and tif_dirwrite.c, plus
TIFFForceStrileArrayWriting() in tif_flush.c.

The error enum and its values, the aligned union and its views, read
threshold/size sentinels, lazy-page and min/max macros, the rational iteration
bound, and the native/IEEE macro hooks and their conditional external
declarations
are documented at their source locations. Standalone tools/tests remain outside
the processed reference.

Directory printing and its private format helpers are covered by
@ref directory_printing. Image payload I/O, general size calculations, codecs
and colour/RGBA conversion have their own module contracts, listed in
@ref documentation_completion.

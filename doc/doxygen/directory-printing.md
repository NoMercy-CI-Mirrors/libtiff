# Directory printing {#directory_printing}

TIFFPrintDirectory() writes a human-readable description of the current
in-memory directory to a caller-owned FILE stream. It does not select/read
a different IFD, decode pixels, or save dirty values in the core path.
C++ callers may omit flags, which defaults to zero; C callers supply it.
Browse @ref libtiff_public_directory_printing and
@ref libtiff_private_directory_printing alongside @ref directories.

## Flags and output order

| Flag | Core effect | Qualification |
| --- | --- | --- |
| TIFFPRINT_NONE | Ordinary detail without optional core table expansion | Custom arrays, raw XML, and callback output can still be large |
| TIFFPRINT_COLORMAP | Print all present palette rows | Three planes, 2^BitsPerSample entries each; requires valid pointers/extents |
| TIFFPRINT_CURVES | Print present transfer-function rows | Up to three colour columns, 2^BitsPerSample entries; requires consistent channel counts/arrays |
| TIFFPRINT_STRIPS | Print all logical strip/tile offsets and byte counts | Can allocate/load deferred metadata and change client I/O/cache state |
| TIFFPRINT_JPEGQTABLES | No interpretation by the core formatter | Original bit mask is passed to the installed print callback |
| TIFFPRINT_JPEGACTABLES, TIFFPRINT_JPEGDCTABLES | No interpretation by the core formatter | Both have value 0x200; they cannot independently select AC versus DC tables |

Flags are combined with bitwise OR. They do not impose a total output-size
limit. For expanded colour tables, a BitsPerSample value of 64 or more prints
a safety message instead of shifting by that width. Smaller bit depths can
still imply enormous output; this guard does not establish valid array capacity.

The output sequence is core fields, custom values in stored-list order, the
optional TIFFPrintMethod callback, then optional strile rows. The callback
receives the original flags unchanged, including bits unused by the core.
Callbacks run synchronously and may have their own allocation, I/O, formatting,
and state effects. Save/delegate to parent printers as needed and avoid calling
TIFFPrintDirectory() recursively from its installed callback. Keep callback code
live while installed/in flight; the dispatcher does not catch exceptions.
Codec-specific print methods will be reviewed with their codec modules.

## Ownership, side effects, and errors

The caller retains the handle, output stream, field definitions, and directory
values. Printing does not close or flush the stream. Custom getter access can
allocate temporary scalar storage, invoke tag methods, and change field lookup
state. Strile rows use the offset/count getters without WithErr status and can
materialise/reallocate caches, invalidate borrowed array views, and seek/read
the client resource. Zero values in those rows are not proof of valid zero-size
striles; metadata load errors can also produce zero. No pixel-range validation
occurs. Serialise handle access and multi-call stream output externally when
ordering matters.

The function is void and ignores fprintf/fputs/fputc results. A failed stream
can leave partial text while later callbacks and metadata queries still run.
Inspect ferror() and perform any caller-controlled flush needed to detect
buffered-output failure. The printer supplies no aggregate error result,
rollback, or general errno guarantee. Getter and allocator failures can still
dispatch LibTIFF diagnostics, despite the existing man page saying "None".

This is not a safe validator for arbitrary partial/corrupt handle state.
Presence bits, pointer extents, descriptors, and getter signatures must be
consistent. Some colour-array NULL cases are skipped, but other present arrays
are dereferenced directly. Scalar custom output uses _TIFFCheckMalloc() and
passes its result to TIFFGetField() without first checking allocation success;
exhaustion is therefore not uniformly recovered safely. Printing must not be
used as proof that a failed load/set operation left a valid directory.

## Custom value representation

The custom loop obtains a descriptor and calls TIFFGetField() using the
read-count/pass-count pattern. C16/C32 fields receive their explicit count;
fixed, variable-without-passcount, and per-sample patterns follow the stored
schema. Generic scalar storage is temporary and freed after use. Array/string
results stay borrowed and are not freed by the printer. DotRange has its two
separate uint16_t outputs backed by local storage. Most failed lookups/getters
skip that value; the DotRange call does not inspect its status. The custom-list
count is narrowed to short, so a list beyond that type's representable range
is not guaranteed complete output.

_TIFFPrettyPrintField() handles a small set of named tags first; anonymous
fields bypass it. If it returns zero, _TIFFPrintField() emits generic values.
Neither helper returns stream success or takes ownership.

| Special case | Actual formatting rule |
| --- | --- |
| DotRange | Two SHORT values shown as a range |
| WhitePoint | Two RATIONAL values interpreted specifically as float elements; private double-rational schemas are not adapted |
| InkSet | Requires SHORT with count two but reads only the first value; the usual count-one representation falls back to generic output |
| XMLPacket | Exactly value_count raw bytes, including embedded NULs, between header/newline text |
| RichTIFFIPTC, Photoshop, ICCProfile | Presence/count summaries; the supplied element count is labelled bytes without a unit conversion |
| StoNits | One DOUBLE in exponential notation |

Generic numeric formatting follows field_type widths, with decimal integers,
hexadecimal IFD offsets, and float/double rational storage selected through
TIFFFieldSetGetSize(). It does not normalise every possible private file-type/
set-get representation mismatch. Anonymous GDAL metadata/nodata tags receive
friendly labels through the local tagnames table without registering a schema.

Generic ASCII with nonzero count uses unbounded `%s`: the data must contain a
readable terminator even when its descriptor has an explicit count. The count
is not a byte bound and the string is not escaped. This matters for counted
ASCII values whose setter copied bytes without adding a terminator. Numeric
arrays require enough matching elements. A zero count prints only the label
and newline; unsupported types print a placeholder.

## Private ASCII formatting

_TIFFprintAsciiBounded() examines at most max_chars source bytes and stops at
an earlier NUL. It does not scan with strlen() first. Zero bound emits nothing
and permits a NULL source. _TIFFprintAscii() instead calls strlen() and therefore
requires a readable terminated string. These routines add no quotes/newline;
_TIFFprintAsciiTag() wraps a raw label and quote-delimited value in a line.

Printable bytes are copied according to the current C locale, including
quotes and backslashes. The named escapes are tab, backspace, carriage return,
newline, and vertical tab. Other nonprintable bytes use a backslash plus three
octal digits. The bound counts source bytes, not emitted bytes: one byte can
expand to four characters. Form feed and bell use octal, not named escapes.
The code passes plain char to isprint(), so seven-bit ASCII is the portable
input domain. On signed-char hosts most high-bit bytes produce an invalid
classifier argument; the implementation does not cast through unsigned char.

The routines are diagnostic formatting, not a round-trip string encoding:
quotes/backslashes remain unchanged, labels are raw, and generic custom ASCII
and XML do not use these helpers at all. They retain no input, allocate no
storage, do not flush/close the stream, and ignore stdio errors.
TIFFPrintDirectory() uses the bounded helper for each InkNames component, but
its subsequent strchr()-based component walk still requires a valid terminated
InkNames sequence. The bound does not make that entire path a malformed-string
validator.

## Coverage

This module documents TIFFPrintDirectory(), the private
_TIFFprintAscii()/_TIFFprintAsciiTag() declarations, and the source-local
_TIFFPrintField(), _TIFFPrettyPrintField(), and _TIFFprintAsciiBounded()
helpers. It refines the TIFFPrintMethod callback timing and flag descriptions
without duplicating their canonical contracts. Print constants retain their
foundations group and link to this guide.

All functions in tif_print.c, the tagname record and its members, the static
label tables, and their count macros are covered. The _TIFFPrintFieldInfo()
registry dump keeps its field-schema contract in @ref directories. Navigation
and IFD I/O are documented in @ref directory_navigation and @ref ifd_io; codec
overrides, image payload I/O, size calculations and colour/RGBA operations
have their own modules. See @ref documentation_completion for the module
inventory.

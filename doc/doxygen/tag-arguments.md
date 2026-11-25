# Tag arguments {#tag_arguments}

TIFFGetField()/TIFFSetField() and their V/defaulted variants use the schema and
tag methods of the **current directory**. Tag numbers alone are insufficient:
EXIF/GPS/custom schemas can reinterpret an identifier. Match the exact count,
value widths, and pointer indirection before calling. The library cannot
validate a malformed C variadic argument list; mismatches can corrupt memory.
Use named TIFFTAG_*, EXIFTAG_*, and GPSTAG_* constants in the proper namespace.

## Promotions and outputs

Setter scalar uint8_t/int8_t/uint16_t/int16_t values undergo integer promotion
(normally int on supported platforms); float scalars promote to double.
Pass explicitly typed uint32_t/int32_t/uint64_t/int64_t values for those widths;
do not assume a literal, long, or size_t has the needed type. Array elements
are not promoted. Getters receive pointers to the exact output storage type;
a float result requires float*, not double*. Array results require an output
pointer slot (e.g. const uint16_t**), not a caller-preallocated element buffer.
The caller owns output slots and input buffers; returned storage is borrowed.

va_list arguments are borrowed traversals, not reusable value containers.
Use va_copy() for every independent traversal and perform the matching va_end().
Do not assume a V call preserves the traversal, including when it fails.
The core setter implementation itself calls va_end(ap) on its normal and
value-error paths. The defaulted getter first tries the ordinary getter using
the same list before applying its fallback, so an overriding getter returning
zero must not consume that list if the fallback is to work safely.

## Core image-directory arguments

This table covers the common dispatcher in tif_dir.c. The generic rules below
cover fields stored through FIELD_CUSTOM. Codec tag overrides have their own
contracts. In the table, `u16` means a promoted uint16_t setter value;
`u32` means an actual uint32_t value. Pointer element types are literal C types.
All input pointers refer to readable arrays of the specified length; getter
pointers identify writable non-NULL output slots. Returned arrays are read-only.

| Tags (TIFFTAG_ prefix omitted) | Setter arguments | Getter arguments | Extent or qualification |
| --- | --- | --- | --- |
| SUBFILETYPE, IMAGEWIDTH, IMAGELENGTH, ROWSPERSTRIP, TILEWIDTH, TILELENGTH, TILEDEPTH, IMAGEDEPTH | u32 | uint32_t* | Scalar; RowsPerStrip/TileDepth must be nonzero; write-mode tile width/length must be multiples of 16 |
| BITSPERSAMPLE, COMPRESSION, PHOTOMETRIC, THRESHHOLDING, FILLORDER, ORIENTATION, SAMPLESPERPIXEL, MINSAMPLEVALUE, MAXSAMPLEVALUE, PLANARCONFIG, RESOLUTIONUNIT, SAMPLEFORMAT, YCBCRPOSITIONING, NUMBEROFINKS | u16 | uint16_t* | Scalar; enum/range checks vary by tag; SamplesPerPixel must be nonzero |
| XRESOLUTION, YRESOLUTION, XPOSITION, YPOSITION | double | float* | Resolution rejects negative/NaN; conversion clamps finite range |
| PAGENUMBER, HALFTONEHINTS, YCBCRSUBSAMPLING | u16, u16 | uint16_t*, uint16_t* | Two independent scalar arguments, not one array |
| COLORMAP | uint16_t*, uint16_t*, uint16_t* | const uint16_t**, const uint16_t**, const uint16_t** | Three arrays, each 2^BitsPerSample entries; setter requires BitsPerSample < 32 |
| EXTRASAMPLES | u16 count, uint16_t* | uint16_t* count, const uint16_t** | Count no greater than SamplesPerPixel; nonempty setter array must be non-NULL; code 999 may be changed in caller storage |
| SUBIFD | u16 count, uint64_t* | uint16_t* count, const uint64_t** | Count offsets; nesting while already writing SubIFDs is rejected |
| TRANSFERFUNCTION | One or three uint16_t* | Three const uint16_t** | Arrays have 2^BitsPerSample entries; setter consumes three arrays when SamplesPerPixel minus ExtraSamples > 1, else one; see default exception below |
| REFERENCEBLACKWHITE | float* | const float** | Six floats |
| INKNAMES | u16 byte length, char* | const char** | Concatenated terminated names; length includes all terminators; getter has no length output; setter also adapts NumberOfInks |
| SMINSAMPLEVALUE, SMAXSAMPLEVALUE | double, or double* in PERSAMPLE_MULTI mode | double*, or double** in PERSAMPLE_MULTI mode | Array length SamplesPerPixel; default scalar mode repeats the setter value and returns the minimum/maximum over the stored array |
| STRIPOFFSETS/TILEOFFSETS, STRIPBYTECOUNTS/TILEBYTECOUNTS | Not a core setter | const uint64_t** | Entire logical strile array; getter may load/allocate it; use the per-strile accessors for lazy access |
| MATTEING (obsolete) | u16 boolean | uint16_t* | Compatibility view of a single associated-alpha extra sample |
| DATATYPE (obsolete) | u16 legacy code | uint16_t* | Compatibility conversion to/from SampleFormat; getter does not fill output for unsupported complex formats |
| PERSAMPLE (pseudo-tag) | u16 control | Not a core getter | PERSAMPLE_MULTI selects per-sample SMin/SMax arrays; any other value selects scalar mode |

The **ordinary** stored TransferFunction getter always consumes three output
slots. For one colour channel it sets the last two pointers to NULL.
The **default fallback** consumes only one output slot for one channel, or
three for multiple channels. Supplying three slots to the defaulted function
covers both paths, but initialise the last two if their values will be read:
the one-channel fallback leaves them untouched. Default generation rejects
BitsPerSample > 24 and can fail allocation. It caches arrays without setting
the tag bit. Do not pass only one output slot when a stored value may exist.

## Generic custom-field rules

TIFFFieldDataType() reports file representation. TIFFFieldSetGetSize() reports
internal element width; they differ for rationals and some offset fields.
Neither function alone describes the full argument list. Inspect read/write
count, TIFFFieldPassCount(), and the active tag method as well. In particular,
a two-byte scalar setter is still promoted, and count-size metadata does not
replace the special core conventions above.

| Definition pattern | Setter arguments | Getter arguments |
| --- | --- | --- |
| ASCII without passcount | const char* terminated string, copied including its terminator | Borrowed string output slot (const char**) |
| ASCII with passcount | uint32_t byte count, const char* readable bytes (writecount must be TIFF_VARIABLE2) | Count output then borrowed value output; count width follows readcount |
| Non-ASCII with passcount, TIFF_VARIABLE2 | uint32_t element count, pointer to elements | uint32_t* count, borrowed array output slot |
| Non-ASCII with passcount, TIFF_VARIABLE | Promoted 16-bit count, pointer to elements | uint16_t* count, borrowed array output slot |
| Fixed count > 1, without passcount | Pointer to exactly that many elements | Borrowed array output slot |
| TIFF_SPP without passcount | Pointer to SamplesPerPixel elements | Borrowed array output slot |
| TIFF_VARIABLE/TIFF_VARIABLE2 without passcount | Pointer to one element (the generic setter assumes count one) | Borrowed array output slot |
| Fixed count one without passcount | Promoted scalar of the internal element type | Pointer to scalar storage of that internal type |

Generic counted getter storage is accessed internally through const void**;
uncounted array storage through void**. The element interpretation follows the
field descriptor. Count values must fit the internal signed int count and
allocation extent; do not supply oversized counts merely because a uint32_t
argument can represent them. A zero count can warn yet still leave an accepted
partial entry. Counted ASCII stores exactly the supplied bytes and adds no
terminator; only treat the result as a C string if one was included.

Scalar RATIONAL/SRATIONAL arguments use a promoted double setter and a float*
or double* getter according to the field's internal representation. Arrays use
float or double elements accordingly. TIFFFieldSetGetSize() distinguishes four
and eight bytes. Legacy TIFFMergeFieldInfo() maps rationals to float storage;
private descriptors can select double. Generic ASCII and array values are
copied rather than retained, but copy failures are not uniformly propagated.

The named DotRange FIELD_CUSTOM definition is an explicit exception: two
promoted uint16_t setter arguments and two uint16_t* getter outputs, rather
than an array. Unknown fields auto-registered from a file use TIFF_VARIABLE2
and explicit 32-bit counts. Their IFD/IFD8 representation uses 64-bit offsets,
and rational values use float storage. Definitions whose read/write count
patterns disagree require care; the legacy registration routine performs only
limited validation and does not make arbitrary patterns safe.

The fixed ASCII case follows the generic string handling, not the numeric
fixed-count rule. File type IFD in a hand-written legacy descriptor also needs
care: the representation mapper chooses IFD8, while generic scalar/array
storage paths inspect the file type as well. Use the established built-in
schema or a reviewed descriptor rather than inferring a signature from width.

## Defaults and diagnostics

The core fallback supports SubfileType, BitsPerSample, Threshholding, FillOrder,
Orientation, SamplesPerPixel, RowsPerStrip, Min/MaxSampleValue, PlanarConfig,
ResolutionUnit, Predictor, DotRange, InkSet, NumberOfInks, ExtraSamples, Matteing,
TileDepth, DataType, SampleFormat, ImageDepth, YCbCrCoefficients,
YCbCrSubSampling/Positioning, WhitePoint, TransferFunction, and
ReferenceBlackWhite. Other absent tags return zero. Predictor needs configured
predictor state. YCbCrCoefficients/WhitePoint defaults return static borrowed
float arrays of three/two elements; no ownership transfers. DataType's fallback
uses SampleFormat minus one, which does not reproduce the ordinary legacy
mapping for all formats. These implementation differences are recorded for
review without changing behaviour in this documentation pass.

Return one is the normal criterion for using outputs, but the obsolete DATATYPE
complex-format case above illustrates why tag-specific qualifications matter.
Neither a successful query nor a default means the tag was physically stored
in the file. No generic call validates arbitrary pointer capacity or promises
rollback. Consult @ref directories and each canonical function contract for
failure, ownership, and side effects.

# Directories {#directories}

A TIFF handle has one current in-memory directory, a registered field schema,
and a tag-method table. The schema describes what a tag means and how its
arguments are represented. It does not establish that a value is present.
TIFFFindField() queries definitions; TIFFGetField() queries values.
TIFFGetTagListCount()/TIFFGetTagListEntry() enumerate only stored FIELD_CUSTOM
records, including a partially installed record after some failed setters.
They do not enumerate every baseline or codec tag.

Browse @ref libtiff_public_fields, @ref libtiff_public_tag_access,
@ref libtiff_private_fields, and @ref libtiff_private_tag_access.
The @ref tag_arguments guide describes the variadic calling conventions.

## Definitions, values, and ownership

Field lookup returns a borrowed TIFFField pointer. Do not modify or free it.
Static built-in definitions live for the library lifetime, but callers must not
assume that every descriptor is static. Automatically registered unknown fields
own generated names and are disposed by field-table/handle cleanup. Legacy
TIFFMergeFieldInfo() copies records into handle-owned compatibility batches but
borrows their names. Keep each name unchanged and live until that batch is no
longer used. Replacing a field table can invalidate anonymous descriptors and
values that still refer to them; dispose those values first.

Registration keeps the existing definition for an already registered tag number,
regardless of its file data type. It does not replace a definition, install a
value, or guarantee that a codec supports that tag. The private
_TIFFMergeFields() borrows whole descriptors; TIFFMergeFieldInfo() copies their
legacy form. Neither operation is transactional. The latter returns **zero on
success and minus one on failure**; ordinary tag get/set calls use one/zero.
Allocation and validation failures do dispatch diagnostics even though the
existing TIFFMergeFieldInfo man page currently says "None".

Array/string values returned by tag getters are borrowed from the directory,
codec, or static default storage. Use them only after success, treat them as
read-only, and copy them if needed across value replacement, unsetting, directory
changes, codec reinitialisation, or handle cleanup. Input arrays and strings
are normally copied by core setters; do not pass a pointer into the old owned
value being replaced, because copy helpers free the old allocation first.
ExtraSamples is a specific exception to input constness: value 999 is repaired
in the caller's array before copying it.

## Mutation and failure

Core setters change memory and field/dirty flags. They do not immediately
persist the IFD. Compression changes can dispose and replace codec state;
SamplesPerPixel and ExtraSamples changes can invalidate dependent arrays and
TransferFunction. Write-time mutability is checked against the descriptor,
with a special exception for ImageLength. This is not an atomic transaction.
A failed setter can discard an old value or leave partial custom storage.
Some core array setters use void copy helpers whose allocation failure still
allows a success result. Do not infer intact old values from a zero return or
complete storage from a one return in those paths.

TIFFUnsetField() clears a non-custom presence bit without freeing/resetting
its stored value or codec state. Some related tags share a bit. For a custom
field it frees the value and compacts the list, retaining the descriptor. It
returns one for a known field even when no value was present, and marks the
directory dirty. It does not apply the setter's write-time mutability check.
In particular, its custom-tag behaviour removes a value, not the registered
definition as the current man-page wording might suggest.

Getters can have side effects: deferred strile arrays may allocate and perform
I/O, while defaulted TransferFunction/ReferenceBlackWhite queries cache owned
arrays. Defaults do not mark the tags present. Some defaults are static arrays;
none transfers ownership. Failure can write some outputs; disregard every
output unless the operation succeeds. Missing fields often fail silently,
whereas validation, codec, allocation, and I/O failures can report diagnostics.
There is no general errno contract or per-handle internal locking.

## Extending the schema and methods

TIFFSetTagExtender() replaces a process-wide borrowed callback. Save and chain
the previous callback so independent extensions coexist. The callback runs on
every TIFFDefaultDirectory() initialisation, including repeated resets of an
existing handle. Install it before opening/reading image directories and keep
its code and borrowed names live. Synchronise changes with all users; this
registry has no locking. Custom/EXIF/GPS setup subsequently replaces the schema,
so this callback does not extend those final field tables.

TIFFAccessTagMethods() returns the handle's embedded mutable table, without
allocation or validation. Save parent methods and delegate unhandled tags to
them. Calling the outer getter/setter for an unhandled tag from its own method
can recurse. Codec/default-directory setup can replace the installed table;
callbacks must remain live while installed or in flight. A callback receives
borrowed arguments synchronously, owns no handle/list/stream, and must follow
the tag-specific output, copying, diagnostics, and dirty-state rules.

## Coverage

The field-schema and tag-access callable contracts are grouped as follows:

| Family | Scope |
| --- | --- |
| Public field/schema queries | Custom-list queries, field lookups, descriptor accessors, TIFFDataWidth |
| Public tag operations | AccessTagMethods, four getters, two setters, UnsetField |
| Public extension | SetTagExtender, MergeFieldInfo |
| Public callback types | ExtendProc, VSetMethod, VGetMethod, PrintMethod |
| Private schema interfaces | Three built-in schema getters, setup, merge, print, anonymous creation/registration, codec filter |
| Source-local schema helpers | Linear search, two comparators, name lookup, legacy representation mapping |
| Source-local tag/default helpers | ExtraSamples, InkNames count, mutability, core get/set, two default allocators |

The private directory header's structures, their members, the set/get and
schema enums, presence-bit/sentinel definitions, and bitmap macros
are documented. The built-in descriptor tables and schema wrappers,
legacy DataType codes, Corel repair code, and global extender slot are included.
The existing public opaque types, TIFFFieldInfo, TIFFTagMethods, and count
sentinels retain their foundations descriptions.

Directory navigation and creation/freeing are covered by
@ref directory_navigation. IFD reading/writing, strile loading, and their
conversion helpers are covered by @ref ifd_io. Printing and its helpers are
covered by @ref directory_printing. Codec-specific tag methods and pseudo-tags
are documented with their codecs; see @ref codecs.

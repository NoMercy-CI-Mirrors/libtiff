# Directory creation and navigation {#directory_navigation}

The directory index, current offset, next link, field schema, and in-memory
values describe different aspects of a TIFF handle. They need not all identify
a successfully loaded on-file IFD. Browse
@ref libtiff_public_directory_navigation and
@ref libtiff_private_directory_navigation alongside @ref directories.

## Creating and releasing in-memory directories

TIFFCreateDirectory() discards the current values and codec state, installs the
image schema/defaults, clears current/next/data offsets, and leaves a fresh
unwritten directory. It does **not** save dirty values first or allocate a new
TIFF handle. Its cached index and maps are retained; it is not a full handle
reset. The directory must later be persisted by the appropriate writer/flush
operation.

TIFFCreateCustomDirectory() additionally installs a caller-supplied schema,
clears the chain maps, sets an unknown index, and forces indexed navigation to
restart at the main-chain header. TIFFCreateEXIFDirectory() and
TIFFCreateGPSDirectory() select the built-in metadata schemas and need no
private schema definitions in the application. All discard previous values.
The global image-directory extender can run during intermediate default setup,
but its field additions do not survive replacement by the custom schema.
Custom schema descriptors and names remain borrowed while registered; the
TIFFFieldArray wrapper itself need only be live for setup.

**All four creation functions return zero unconditionally.** Their return
value is not a Boolean success result. Internal setup can report diagnostics
without changing that result. The private TIFFDefaultDirectory() likewise
returns one unconditionally despite ignored nested setup failures.

TIFFFreeDirectory() frees owned values and invokes codec cleanup. Borrowed
arrays/strings become invalid. It clears presence bits and deferred strile
entries, but it does not initialise a complete fresh directory, replace the
schema, close the client resource, free the handle, reset every scalar/index,
or flush dirty contents. Follow it with proper directory setup before normal
use. Its public return type is void. Directory creation keeps the TIFF handle
and replaces its directory state; it does not free the TIFF structure.

The private _TIFFResetTifDirAndInitStrileCounters() only zeros a structure and
sets traversal sentinels. It cannot free old allocations or install semantic
TIFF defaults; call it on fresh/already released storage. TIFFDefaultDirectory()
uses it after schema setup, replaces compatibility storage and tag methods,
invokes the extender, attempts uncompressed codec setup, and clears dirty/tiled
flags. Neither helper is a general checked recovery transaction.

## Querying state

| Query | What it reads or does | Limits |
| --- | --- | --- |
| TIFFCurrentDirectory() | Returns the cached chain index | Zero-based for main IFDs; unknown is `(tdir_t)UINT_MAX`; a new unrecognised SubIFD chain starts at zero |
| TIFFCurrentDirOffset() | Returns the cached IFD byte offset | Zero for fresh/no-current-offset state; nonzero does not prove a complete successful read |
| TIFFLastDirectory() | Tests whether the cached next offset is zero | Also true for an unwritten directory or a loop-truncated chain; it does not scan the file |
| TIFFNumberOfDirectories() | Traverses main-chain links from the cached header | Counts a successfully traversed prefix; excludes unwritten IFDs and SubIFD arrays; can allocate and move I/O position |

The first three are direct state reads with no allocation, diagnostics, or I/O.
The counting query updates directory maps and the cached count without replacing
current tag values/index. Read/loop/allocation diagnostics do not produce a
separate error status. A partial result, including zero, must not be treated
as an independently verified total. Initial hash-map allocation exhaustion is
diagnosed but can allow traversal to proceed without complete loop tracking.

After an image directory is written, the cached index can identify the last
written IFD while the active in-memory values describe the next fresh directory
with offset zero. Ordinary creation retains that old index. Custom creation
sets it unknown. A reader can advance the index before later tag validation
fails. The unknown index is `(tdir_t)UINT_MAX`. Query the
state appropriate to the operation, and check the operation's result first.

## Selecting a directory

TIFFSetDirectory() takes a **zero-based main-chain index**. It uses a cached
offset, relative traversal, or traversal from the cached header. After a custom
or unrecognised SubIFD context, it clears those maps and restarts from that
header. It does not interpret an index as an element of a SubIFD array.

TIFFSetSubDirectory() takes an **image-IFD byte offset**, commonly obtained from
a SubIFD tag. Read EXIF/GPS/custom metadata through the matching custom reader
instead, because it needs a different schema and validation. A known offset
keeps its mapped index. A successful unrecognised offset starts a new chain at
index zero, clears/reseeds maps, and forces the next TIFFSetDirectory() back to
the main-chain header. Finding whether an offset is known can itself scan main
IFDs. A nonzero next link in that SubIFD chain can be followed with
TIFFReadDirectory(); array-linked SubIFDs must be selected individually by offset.

A zero offset has a special implemented meaning in TIFFSetSubDirectory():
it returns zero without loading a directory, clears current/next offsets,
marks the index unknown and directory unwritten, but leaves old tag values
in place. It is not a newly initialised directory. This differs from ordinary
nonexistent-offset failure and is useful to understand when inspecting state.

Both selectors can discard the current values without saving dirty state.
Once TIFFReadDirectory() begins, success invalidates old borrowed values and
failure can leave partially replaced schema, flags, offsets, index, and values.
An unreachable index can fail earlier while keeping the old tag storage, yet
maps, traversal controls, and I/O position can already have changed. Some early
fetch failures set the index unknown; later semantic failures may retain the
new index. A final map-registration failure in TIFFSetSubDirectory() can follow
a successful read. There is no uniform rollback or general errno guarantee.

## Unlinking a main-chain IFD

TIFFUnlinkDirectory() takes a **one-based position**: the first directory is 1,
unlike indexed selection. It requires a writable file and rejects zero.
It overwrites the previous next-link field, or the file header for the first
IFD, to bypass the selected directory. It does not reclaim bytes, truncate the
file, or repair other references such as SubIFD arrays. Outstanding dirty
values and buffered data are not flushed first.

After a successful patch it releases owned raw-buffer/directory storage and
codec state, installs defaults, clears write/current/next/last offsets and maps,
and leaves a fresh context intended for appending. Borrowed storage is
invalidated. A failed traversal or write can leave changed maps, I/O position,
or file bytes. The patch seek is not independently checked, and failures in
post-patch default setup do not change the success return. This is an in-place
file modification without rollback; callers must choose when to discard pending
work and how to recover from their I/O implementation's failures.

## Private map ownership and failure paths

_TIFFCheckDirNumberAndOffset() tracks a chain-relative index/offset relation.
The offset-keyed hash map owns malloc-allocated records through free(); the
index-keyed map borrows those same records. These allocations bypass the
handle's Ext-allocation quotas. Keys must not change while stored. Both maps
are disposed by the existing _TIFFCleanupIFDOffsetAndNumberMaps() lifecycle
helper; entry pointers become invalid.

A repeated offset at a different index indicates a loop; the same pair is
accepted. An existing index may be rebound to a new offset when an IFD moves.
TIFF_MAX_DIR_COUNT limits new pairs. Registration is not transactional:

- Failure to create either hash map reports an error but returns one, so
  success does not prove that loop tracking was installed.
- Record/insertion failures generally return zero, with partial map updates
  possible; the replacement-record malloc failure has no direct diagnostic.
- Hash removal results are ignored during rebinding/removal. A shrink
  allocation can fail before removing a borrowing entry; subsequent successful
  removal of its owning entry can leave a dangling record in the other map.
  Do not assume the maps remain safe/consistent on these failure paths.

_TIFFGetOffsetFromDirNumber() only looks in the existing index map. It neither
allocates nor scans, and preserves its output on absence.
_TIFFGetDirNumberFromOffset() returns immediately for a zero offset or missing
offset map; an existing map with a missing key triggers TIFFNumberOfDirectories()
and a second lookup. It preserves its output on absence, but scan effects and
diagnostics still apply. Neither lookup proves that the on-file IFD is valid.

_TIFFRemoveEntryFromDirectoryListByOffset() treats absence as success and only
returns zero for an existing offset entry with a missing index map. It can
return one without removing anything if the index key is missing or a hash
removal failed. Its result is not proof that both entries were removed. Dispose
and rebuild maps for recovery rather than trusting an unchanged/consistent
relation after failure.

The source-local TIFFAdvanceDirectory() reads only counts/links and updates
these maps. It rejects an invalid starting pair, but a detected loop at the
next pair is truncated to a zero next link and still returns success. Other
failures need not preserve outputs or I/O/map state. The two hash and two
equality callbacks are documented beside their definitions and borrow records
without allocation or mutation.

## Coverage

This module documents the following callable contracts and one source-local
allocation-cleanup macro:

| Family | Symbols |
| --- | --- |
| Public directory state | CurrentDirectory, CurrentDirOffset, LastDirectory, NumberOfDirectories |
| Public directory storage | FreeDirectory, CreateDirectory, CreateCustomDirectory, CreateEXIFDirectory, CreateGPSDirectory |
| Public selection/modification | SetDirectory, SetSubDirectory, UnlinkDirectory |
| Private initialisation | DefaultDirectory, _TIFFResetTifDirAndInitStrileCounters |
| Private chain maps | _TIFFCheckDirNumberAndOffset, _TIFFGetDirNumberFromOffset, _TIFFGetOffsetFromDirNumber, _TIFFRemoveEntryFromDirectoryListByOffset |
| Source-local chain helpers | TIFFAdvanceDirectory, key hash callbacks, key equality callbacks |

All functions in tif_dir.c have canonical contracts, including the array
helpers and tag operations documented in @ref directories. IFD reading/writing,
custom operations, strile loading, and conversion helpers are documented in
@ref ifd_io, and printing in @ref directory_printing. In the mixed-purpose
tif_open.c and reader sources, this module covers only the functions listed
here.

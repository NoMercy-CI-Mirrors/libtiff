# Shared services {#shared_services}

The @ref libtiff_public_services module supplies memory operations, diagnostic
dispatch, and named client information. Its private counterparts provide
handle-aware allocation, checked arithmetic, owned-array replacement, and hash
sets. These services are used across directory, codec, and image-I/O modules.

## Allocation families

| Family | Allocation policy | Matching release | Failure/lifetime rules |
| --- | --- | --- | --- |
| _TIFFmalloc(), _TIFFcalloc(), _TIFFrealloc() | Platform allocator; no TIFFOpenOptions limits | _TIFFfree() | Positive-size realloc failure preserves the old block; zero-size realloc follows the platform |
| _TIFFmallocExt(), _TIFFcallocExt(), _TIFFreallocExt() | Matching handle's positive single/cumulative limits; private prefix when cumulatively tracked | _TIFFfreeExt() with the same handle/accounting configuration | Positive-size realloc failure preserves the old block and charge; nonpositive size frees it and returns NULL |
| _TIFFCheckMalloc(), _TIFFCheckRealloc() | Positive checked element product, then handle-aware allocation | _TIFFfreeExt() with the matching handle | Invalid/overflowing product or allocation failure reports a diagnostic; even a rejected zero-count realloc preserves the old block |
| Hash-set storage | Direct libc malloc/calloc, independent of handles and their limits | Container operations free buckets/nodes; optional disposer handles elements | A failed insertion calls the element disposer when configured |

Allocation transfers ownership only on success, except for the explicit
nonpositive-size release operation in _TIFFreallocExt() and the hash insertion
disposal policy. Use a temporary pointer for reallocation so a failed positive
request does not lose the original allocation. A successful reallocation
invalidates the old pointer even when its numeric address is unchanged.

Keep the matching handle alive until tracked blocks are released. Do not free
accounting-prefixed storage with _TIFFfree(), change its accounting convention,
or hand it to a different handle's allocator. Allocation quotas are not process
memory limits: initial handle allocation is checked separately, and direct
allocations, mappings, caller storage, and untracked third-party allocations are
not all charged to the cumulative total. See @ref lifecycle for open-option
setup and lifetime.

Ordinary raw allocator exhaustion has no direct library diagnostic. Ext helpers
diagnose policy rejection but return NULL silently for their invalid-size checks
or ordinary allocation exhaustion. Check helpers add an allocation diagnostic
for every NULL result, so policy rejection can produce two messages. The raw
wrappers do not validate negative signed sizes before conversion to size_t;
callers must supply nonnegative sizes.

_TIFFmemset(), _TIFFmemcpy(), and _TIFFmemcmp() follow their C-library pointer and
extent requirements. They do not validate buffer bounds. In particular, memcpy
requires non-overlapping ranges; none of these operations changes ownership.

## Checked arithmetic and owned-array copies

@ref libtiff_private_arithmetic distinguishes valid zero results from rejection:
most unsigned arithmetic/conversion helpers use zero for both. Signed-size
multiplication rejects nonpositive operands; signed-size addition accepts zero
but rejects negatives. Some helpers diagnose only when both a handle and a
context string are supplied, whereas unsigned multiplication dispatches a
diagnostic on overflow even with a NULL handle. Read each contract before
using a zero result as an error indicator.

_TIFFComputeRowSize64() rounds the complete packed row to bytes once. It is not
a full codec/YCbCr storage-layout rule. _TIFFComputeRowOffset() returns zero for
row zero without checking the stride; other rows use checked signed-size
multiplication. Narrow-host casts must also satisfy the documented range.
_TIFFClampDoubleToFloat() clamps infinities to finite bounds but converts NaN;
_TIFFClampDoubleToUInt32() maps NaN to UINT32_MAX.

The @ref libtiff_private_memory array-copy helpers are destructive replacement
operations. They free the old destination before checking or allocating the
replacement. A NULL source, zero/rejected byte count, or allocation failure
leaves the output slot NULL and loses the old contents. They return no status.
The source must not alias the old allocation. The repeated-double helper also
requires its unchecked byte product to be representable. These are different
contracts from failure-preserving positive-size realloc.

## Diagnostic routing

@ref libtiff_public_diagnostics uses two independent process-wide slots per
severity, plus the per-handle callback configured through open options.

| Entry point | Order for installed callbacks | Extended-global context |
| --- | --- | --- |
| TIFFError(), TIFFWarning() | Ordinary global, then extended global | Zero |
| TIFFErrorExt(), TIFFWarningExt() | Ordinary global, then extended global | Supplied fd |
| TIFFErrorExtR(), TIFFWarningExtR() | Per-handle first; if absent or returning zero, ordinary global then extended global | Handle's client context, or zero for a NULL TIFF pointer |

A nonzero per-handle callback result suppresses the two legacy slots for that
message; it does not change the calling operation's success or abort it. Setting
one global slot to NULL disables only that slot. Registrations do not own code
or context and provide no locking; coordinate replacement and in-flight calls
externally. Global slots are read as dispatch proceeds rather than captured in
an atomic snapshot; changing a slot inside a callback can affect later dispatch.
The R suffix permits per-handle routing, not unsynchronised mutation
of a single handle or callback registry.

Dispatch is synchronous, and each invoked legacy callback receives a freshly
started va_list. Strings and argument lists are borrowed only for the call;
use va_copy() for another traversal. Callbacks can change external state or
errno, and exceptions are not caught. Default formatters write module/message
text to stderr with a final period/newline and do not check stdio errors or
terminate the process. Warning callbacks use the same TIFFErrorHandler family
of typedefs; there are no separate TIFFWarningHandler typedefs in the header.

## Named client information

@ref libtiff_public_client_info is a handle-local, case-sensitive name-to-pointer
association list. It is separate from the client I/O context in TIFFClientdata().
A new association copies the name and allocates a node; the payload remains
borrowed. Replacement stores a new payload pointer without allocating or
disposing the previous payload. A NULL payload is a stored association, not a
delete request, and lookup cannot distinguish it from absence.

If either new-entry allocation fails, checked allocation emits diagnostics and
the existing list is unchanged. The setter has no status result. Cleanup frees
the copied names and nodes, not user payloads; the client controls their lifetime.

## Hash-set ownership and failure paths

@ref libtiff_private_hash stores unique elements according to hash/equality
callbacks. Equal keys must hash equally and remain stable while stored.
Callbacks must outlive the set and must not structurally mutate/destroy it.
The container owns buckets and nodes; element ownership follows the optional
disposer:

| Operation | With a consuming disposer | Without a disposer |
| --- | --- | --- |
| Equal-key insertion | Disposes the old element, stores the new pointer | Replaces the pointer; the caller still owns old storage |
| Failed insertion | Disposes the incoming element even though false is returned | Does not consume the incoming element |
| Removal/clear/destruction | Disposes affected stored elements | Does not free element storage |
| Lookup | Returns a borrowed pointer; later disposal can invalidate it | Returns a pointer whose storage lifetime remains caller-controlled |

Do not reinsert the same owned pointer as its own replacement when the disposer
frees it: disposal runs before the pointer is stored again. Lookup cannot
distinguish a stored NULL pointer from absence. Immediate removal can fail before
searching for a key if a shrink-table allocation fails; false therefore does not
prove absence. A failed insertion can follow a successful rehash, so membership
is preserved while bucket layout/order can already have changed. Payload objects
and existing nodes are not moved by rehash.

Removed nodes are recycled up to a 128-node cache; stale recycled payload fields
are not live objects. The fixed prime-capacity table has no explicit growth-end
guard, so the implementation does not provide an unbounded-capacity guarantee.
The optional Clear/Foreach/RemoveDeferRehash APIs and visitor typedef are behind
the `notused` condition and are not enabled in the normal build. Their source
contracts are documented without enabling them in production.

## Coverage

The callable inventory comprises the public memory, diagnostic and
client-information functions, the legacy diagnostic callback typedefs, private
allocation/arithmetic/copy/formatter functions, and the hash-set interface,
callback typedefs and implementation helpers. The optional hash functions and
their visitor typedef are inactive in default builds. The per-handle diagnostic
callback and early-error dispatcher retain their canonical lifecycle contracts.
Private global handler slots, prefix/capacity storage, and hash state members
are documented at their declarations or source-local definitions.

Directory-default/tag-access operations sharing tif_aux.c and tif_extension.c,
and the rest of tif_dir.c, belong to the directory module. Codec-specific
allocation and error semantics remain with the codec modules. Documenting these
shared helpers does not imply coverage of those mixed-purpose files.

| Callable family | Contracts |
| --- | --- |
| Public memory | _TIFFmalloc, _TIFFcalloc, _TIFFrealloc, _TIFFfree, _TIFFmemset, _TIFFmemcpy, _TIFFmemcmp |
| Public diagnostics | Error/Warning ordinary, Ext, and ExtR entry points, plus ordinary/extended global setter pairs |
| Public client information | TIFFGetClientInfo, TIFFSetClientInfo |
| Legacy diagnostic callback types | TIFFErrorHandler, TIFFErrorHandlerExt; the ExtR type retains its lifecycle contract |
| Private memory/arithmetic | Ext allocators, checking allocators, arithmetic/conversion/seek helpers, declared array-copy wrappers, source-local copy/fill helpers |
| Private diagnostic implementations | Allocation-limit emitters and backend default formatters |
| Hash-set interface | New, Destroy, Size, Insert, Lookup, Remove, and optional Clear, Foreach, RemoveDeferRehash |
| Hash callbacks | Hash, equality, disposal, and optional visitor |
| Hash implementation | Default pointer hash/equality, node acquisition/return, internal clearing, list destruction, rehash, slot lookup, internal removal |

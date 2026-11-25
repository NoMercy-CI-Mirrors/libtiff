@page fax CCITT fax codecs

Encode and decode Group3 Group4 and aligned bilevel runs.

The @ref libtiff_private_fax group covers all fax functions, method chains,
run/reference state, generated-transition declarations, Huffman entries and
state-machine macros. Setup validates bilevel single-sample/separate data and
allocates run/reference buffers. Row sizes must be positive, complete requests
are required, and signed decoder coordinate/count arithmetic must fit.

Decoders return minus one on errors, which must not be interpreted as Boolean
success. Recovery can warn about bad row lengths, missing EOL or exhausted
input and mutate/fill partial rows. Group4 can accept early termination after
at least one completed row. Recovery counters bound repeated corrupt-input
conditions. No general rollback or guarantee that every requested pixel is
valid follows from a recovery status.

Run-fill callbacks borrow row/run storage synchronously and can mutate runs.
Only a callback explicitly supporting it can accept NULL output. Default
filling emits MSB-first white-zero/black-one rows, clips runs and asserts the
final width. State-machine macros can update caller locals, branch to caller
labels, diagnose or return from the enclosing decoder; they are not ordinary
functions with independent error state.

Encoder helpers can flush mid-code. Some RTC/EOFB code-write statuses are
ignored, and the void close wrapper discards its completion status. Cleanup
restores methods and releases buffers/state without finishing or flushing
payload. Header declarations document generated transition storage; table
contents and their generator remain separate from the decoder contracts.

The public encoded-tile and user-buffer read paths currently test decoder
callbacks for nonzero, so a fax decoder's negative error can be accepted there
as completion. Their public contracts explicitly retain that status limitation.

A 64-by-9 bilevel scanline round trip with three-row strips succeeds for CCITT
RLE, Group 3 and Group 4. The RLE/W case emits a premature-EOL warning and
returns mismatched pixels on the third row of the first strip. This is a known
limitation of the current implementation: RLE/W round trips are not assured.

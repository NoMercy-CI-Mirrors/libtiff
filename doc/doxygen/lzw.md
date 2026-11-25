@page lzw LZW dictionaries and restart state

The @ref libtiff_private_lzw group covers standard TIFF early-change LZW,
historical reversed-code compatibility decoding, encoder hashing, code-bit
macros, and predictor-prefixed state. The dictionary and encoder hash table
are separately allocated and owned by the codec; entry pointers borrow that
storage and become invalid during cleanup.

Predecode resets widths, bit cache, interrupted-string restart, and the error
latch. It can switch to the compatibility decoder and reinstall predictor
wrappers. Decoding can satisfy a request from a partial dictionary string and
retain the remainder for a later call. Detected errors can clear the unfilled
output tail and latch failure for the rest of that strile. Subsequent calls
then clear output and fail until predecode resets the latch. Cursor progress
is not committed uniformly on all error paths.

Encoder setup allocates the hash table. Preencode assumes setup has succeeded;
its lazy setup call does not check status before clearing that table. Caller
buffering must leave the documented final-code headroom. Encoding may flush
partially produced bytes and reset the dictionary at ratio checkpoints.
Postencode appends the final string, EOI, and padding; it does not itself ensure
that all buffered bytes or directory metadata have reached the file.

Initialisation attempts Predictor hook installation without propagating its
status. Cleanup restores predictor hooks, frees both tables and outer state,
clears tif_data, and installs default compression callbacks. There is no payload
flush or general rollback during cleanup. Read-only builds omit encoder methods;
platform code-cache loads and compatibility paths remain separately documented.

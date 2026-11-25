@page basic_codecs Uncompressed and byte-run codecs

The @ref libtiff_private_basic_codecs group covers uncompressed transfer,
PackBits, ThunderScan and NeXT. Public raw/encoded interfaces retain their
@ref image_transfers contracts; these callbacks borrow handle buffers and caller
storage. Codec installation assumes default methods and prior codec cleanup.

Uncompressed transfer copies bytes and adjusts cached raw cursors. Its seek
method skips buffered scanlines without seeking the client stream. Encoding
flushes full raw output buffers and can leave partially written file state.

PackBits bounds individual packets to 128 bytes and divides strile encoding
at row boundaries. Its row encoder returns zero on flush failure, while its
chunk wrapper checks only for negative status and can therefore report success
after that failure. The preencode row-size allocation is released by postencode
without clearing tif_data; that pairing must not be repeated against a dangling
pointer. Decoder short input clears the unfilled output tail and returns zero;
clipped overlong packets emit warnings.

ThunderScan accepts four-bit single-plane rows (or separate samples). It
installs row/strip decoding, with no tile decoder or encoder. A failed row
clears that row and the remaining requested rows; earlier decoded rows and raw
progress remain. NeXT accepts two-bit single-plane/separate data and initialises
the requested output to white. Its decoder can exhaust input between rows and
return success with remaining white rows; malformed/truncated content within a
row is diagnosed. These details are documented behavior, without code changes.

@page lzma LZMA2 streaming codec

Implement filtered LZMA2 stream compression.

The @ref libtiff_private_lzma group covers dependency status descriptions,
stream setup/transfers, mode flags, filter options and tag-method chains.
The filter chain borrows embedded delta/LZMA2 options. The dependency stream
owns internal allocations until lzma_end; outer state uses the Ext family.

Decoder setup disables the dependency memory limit. Decode errors can latch
failure for the strile; later calls clear output and fail until predecode
resets the latch. Successful requested output does not prove complete stream
consumption or absence of a diagnosed dependency error.

Preset assignment updates options and can reinitialise an active encoder.
Some dependency statuses are ignored or diagnosed while returning success.
Choose options before writing a frame. Encoding/finalisation can leave partial
file bytes on failure. Cleanup ends and frees state without finishing/flushing
payload. Installation does not propagate PredictorInit status.

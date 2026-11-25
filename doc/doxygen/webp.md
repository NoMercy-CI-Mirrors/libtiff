@page webp WebP frame codec

Stage and transfer WebP RGB or RGBA frames.

The @ref libtiff_private_webp group covers every callback, state member and
pseudo-tag. Contiguous eight-bit unsigned RGB/RGBA is the intended input;
ABI support controls four-channel and exact-lossless features. The decoder
setup predicate is weaker than a full bit-depth/format validation, so those
preconditions remain explicit. Frame dimensions are limited to 16383 each.

Encoding stages a complete strile and compresses only at postencode. Supply
all frame bytes; postencode does not verify that the staging buffer was fully
filled. The dependency writer requires enough raw capacity for the complete
encoded frame and does not stream-flush fragments.

Whole-strile decoding can borrow caller output as dependency external memory.
Success deletes that decoder; an error can leave the pointer retained until
predecode or cleanup. Partial reads use codec-owned full-frame staging. Most
errors clear output and latch same-strile failure; fractional-row usage errors
permit recovery without that latch. Cleanup releases picture/decoder/staging
without finishing or flushing an outstanding frame.

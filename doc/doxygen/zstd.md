@page zstd ZSTD streaming codec

Implement predictor-aware ZSTD frame streaming.

The @ref libtiff_private_zstd group covers every stream callback, state member,
pseudo-tag method and mode flag. Streams belong to the codec and are released
with the dependency-specific free functions; output descriptors borrow raw
storage. Switching modes destroys the opposite stream.

Level assignment warns outside the preferred range but still stores the value.
Preencode applies it to a newly reset frame. Decode success means the requested
output filled, without proving complete frame consumption. Decode errors clear
the unfilled tail but need not commit local raw cursor progress; stream state
can already have changed. Encode/finalise failures can leave partial file data.
Cleanup frees streams and state without completing or flushing a frame.
Predictor initialisation status is not propagated by codec installation.

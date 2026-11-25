@page predictor Predictor method chains and transforms

The @ref libtiff_private_predictor layer is shared by codecs that support the
Predictor tag. Their codec-owned tif_data allocation must begin with a
TIFFPredictorState prefix. Initialisation saves parent tag, print, and setup
methods and installs wrappers. It does not allocate the outer codec state.

Setup validates the predictor/sample format, derives sample stride and decoded
row size, chooses accumulation/differencing, and wraps transfers. Parent setup
runs first and its effects remain if predictor validation fails. Integer
prediction works modulo sample width. Floating-point prediction rearranges
sample bytes and predicts byte planes without numeric floating-point operations.
Byte swapping is integrated where required, disabling ordinary postdecode
swapping to avoid a second conversion.

Row and strile encoding use temporary copies, so this layer preserves caller
input. Public write postdecode conversion can separately mutate it. Decoding
transforms output in place. Floating transforms allocate row temporaries;
integer transforms rely on valid strides, complete rows, and their documented
alignment. Accumulation accepts unaligned wide samples through memcpy/byte
helpers; wide differencing retains typed alignment requirements.

The shared strip/tile wrappers currently call the saved tile method. The encode
wrapper ignores each row transform's status before calling the parent encoder;
its result therefore does not independently prove that every transform worked.
Swapping accumulation wrappers can change sample byte order before an inner
extent rejection. No wrapper provides rollback of output, codec progress, or
client I/O.

Cleanup restores saved tag/print/setup hooks. It does not restore every wrapped
transfer method or postdecode method, remove merged field definitions, or free
the codec state. The outer codec must complete teardown before releasing the
prefix. Keep saved callback code and the handle live throughout that sequence.

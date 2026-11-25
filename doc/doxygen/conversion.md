@page conversion Byte, bit, and colour conversion

The @ref libtiff_public_conversion interfaces are independent of an open TIFF
handle. They operate on caller-owned values and conversion state. They do not
read tags, allocate result objects, perform file I/O, or dispatch diagnostics.

Byte swapping reverses each complete sample's bytes; bit reversal reverses
bits within each byte and leaves byte order alone. Array lengths count elements,
except TIFFReverseBits(), whose length counts bytes. Triple swapping exchanges
the first and third bytes in each group of three. Lookup-table pointers are
immutable borrowed library storage.

Initialise colour state before converting samples. TIFFCIELabToRGB holds its
own tables; TIFFYCbCrToRGB requires additional suitably aligned trailing table
storage in the same allocation. The required capacity is documented on that
structure. Neither initialiser allocates memory or retains coefficient pointers.
Both currently return zero unconditionally, so their return values do not
validate coefficient quality or storage capacity.

The CIE conversions require valid finite coefficients and correctly scaled Lab
inputs. The 8-bit wrapper retains historical signed chrominance scaling without
range checks. The XYZ conversion clips channel luminosities and output levels;
it does not make arbitrary NaN or malformed state safe. The implementation
currently derives all three gamma-table step sizes from the red-channel
luminance endpoints. This behavior is documented rather than altered here.
YCbCr conversion supports 8-bit codes and clips channel outputs to 0..255.

The pass covers all functions and constant tables in tif_swab.c and tif_color.c,
including the private 16-bit Lab conversion, coefficient clamp, and arithmetic
macros. RGBA orchestration and LogLuv representations have separate contracts.

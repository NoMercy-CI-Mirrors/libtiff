// tiff_core spike: domain — strip geometry with checked arithmetic.
// The single implementation of size arithmetic (plan §8.1); everything
// is unsigned and overflow-checked.
#pragma once

#include "core/result.hpp"

#include <cstdint>

namespace tiff::core {

Result<std::uint64_t> checked_mul(std::uint64_t a, std::uint64_t b);
Result<std::uint64_t> checked_add(std::uint64_t a, std::uint64_t b);

// Bytes in one packed scanline: ceil(width * spp * bps / 8).
// [impl CMP-1-01]
Result<std::uint64_t> scanline_size_bytes(std::uint32_t width,
                                          std::uint16_t samples_per_pixel,
                                          std::uint16_t bits_per_sample);

// StripsPerImage = ceil(ImageLength / RowsPerStrip). [impl DEP-001]
Result<std::uint32_t> strip_count(std::uint32_t image_length,
                                  std::uint32_t rows_per_strip);

// Rows covered by one strip (the last strip may be short).
// [impl TAG-278-02]
Result<std::uint32_t> rows_in_strip(std::uint32_t image_length,
                                    std::uint32_t rows_per_strip,
                                    std::uint32_t strip_index);

// Decoded size in bytes of one strip.
Result<std::uint64_t> strip_decoded_size(std::uint32_t width,
                                         std::uint32_t image_length,
                                         std::uint16_t samples_per_pixel,
                                         std::uint16_t bits_per_sample,
                                         std::uint32_t rows_per_strip,
                                         std::uint32_t strip_index);

} // namespace tiff::core

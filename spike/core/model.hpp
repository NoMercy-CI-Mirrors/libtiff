// tiff_core spike: domain — validated directory snapshot.
// Every array carries its own length (std::vector), so no length is ever
// derived from another mutable field (plan §8.2 invariant 2).
#pragma once

#include <cstdint>
#include <vector>

namespace tiff::core {

struct Directory {
    std::uint32_t width = 0;
    std::uint32_t length = 0;
    std::uint16_t samples_per_pixel = 1; // default [impl GEN-002]
    std::uint16_t bits_per_sample = 1;   // default [impl GEN-002]
    std::uint16_t compression = 1;       // default [impl GEN-002]
    std::uint16_t photometric = 0;       // required, no default
    // Default 2^32-1: effectively "one strip". [impl GEN-002 TAG-278-02]
    std::uint32_t rows_per_strip = 0xFFFFFFFFu;
    std::vector<std::uint64_t> strip_offsets;
    std::vector<std::uint64_t> strip_byte_counts;
    std::size_t skipped_unknown_tags = 0;
    std::uint64_t next_ifd_offset = 0;
};

} // namespace tiff::core

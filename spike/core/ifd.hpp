// tiff_core spike: domain — IFD decode. Pure: operates on spans the
// application layer has already read; no I/O.
#pragma once

#include "core/ports.hpp"
#include "core/result.hpp"
#include "core/span.hpp"
#include "core/types.hpp"

#include <cstdint>
#include <vector>

namespace tiff::core {

struct RawEntry {
    std::uint16_t tag = 0;
    std::uint16_t type = 0;
    std::uint64_t count = 0;
    std::size_t type_size = 0;    // bytes per value
    std::uint64_t value_size = 0; // type_size * count
    bool is_inline = false;
    std::uint64_t value_offset = 0; // valid when !is_inline
    Byte inline_bytes[8] = {};      // valid when is_inline
};

struct RawIfd {
    std::vector<RawEntry> entries;
    std::uint64_t next_ifd_offset = 0;
    std::size_t skipped_unknown_type = 0;
};

// Field sizes by container. [impl STR-005 BTF-005]
std::size_t ifd_count_field_size(Container container);  // 2 / 8
std::size_t ifd_entry_field_size(Container container);  // 12 / 20
std::size_t ifd_next_field_size(Container container);   // 4 / 8

// Decodes the entry-count field.
Result<std::uint64_t> parse_ifd_count(Span<const Byte> count_bytes,
                                      ByteOrder order, Container container);

// Decodes `count` entries plus the next-IFD offset from `body` (which the
// caller sized as count * entry_size + next_size).
Result<RawIfd> parse_ifd_body(Span<const Byte> body, std::uint64_t count,
                              ByteOrder order, Container container,
                              const ReadPolicy &policy, DiagnosticSink *sink);

} // namespace tiff::core

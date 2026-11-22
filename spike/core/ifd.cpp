#include "core/ifd.hpp"

#include "core/bytes.hpp"

#include <string>

namespace tiff::core {

namespace {
constexpr const char *MODULE = "ifd";
}

// [impl STR-010 STR-011 BTF-009]
std::size_t field_type_size(std::uint16_t type) {
    switch (static_cast<FieldType>(type)) {
    case FieldType::Byte:
    case FieldType::Ascii:
    case FieldType::SByte:
    case FieldType::Undefined:
        return 1;
    case FieldType::Short:
    case FieldType::SShort:
        return 2;
    case FieldType::Long:
    case FieldType::SLong:
    case FieldType::Float:
    case FieldType::Ifd:
        return 4;
    case FieldType::Rational:
    case FieldType::SRational:
    case FieldType::Double:
    case FieldType::Long8:
    case FieldType::SLong8:
    case FieldType::Ifd8:
        return 8;
    }
    return 0; // unknown type code: field will be skipped [impl GEN-017]
}

std::size_t ifd_count_field_size(Container container) {
    return container == Container::Classic ? 2 : 8;
}

// Classic: 12-byte entries; BigTIFF: 20-byte entries.
// [impl STR-005 STR-007 BTF-005 BTF-006]
std::size_t ifd_entry_field_size(Container container) {
    return container == Container::Classic ? 12 : 20;
}

std::size_t ifd_next_field_size(Container container) {
    return container == Container::Classic ? 4 : 8;
}

Result<std::uint64_t> parse_ifd_count(Span<const Byte> count_bytes,
                                      ByteOrder order, Container container) {
    if (count_bytes.size() < ifd_count_field_size(container))
        return Error{ErrorCode::TRUNCATED, MODULE, "truncated IFD count"};
    const std::uint64_t count =
        container == Container::Classic
            ? static_cast<std::uint64_t>(get_u16(count_bytes, 0, order))
            : get_u64(count_bytes, 0, order);
    // At least one entry per IFD. [impl STR-006]
    if (count == 0)
        return Error{ErrorCode::BAD_IFD, MODULE, "IFD has no entries"};
    return count;
}

Result<RawIfd> parse_ifd_body(Span<const Byte> body, std::uint64_t count,
                              ByteOrder order, Container container,
                              const ReadPolicy &policy, DiagnosticSink *sink) {
    const std::size_t entry_size = ifd_entry_field_size(container);
    const std::size_t next_size = ifd_next_field_size(container);
    const std::size_t inline_capacity =
        container == Container::Classic ? 4 : 8;

    if (body.size() < count * entry_size + next_size)
        return Error{ErrorCode::TRUNCATED, MODULE, "truncated IFD body"};

    RawIfd ifd;
    ifd.entries.reserve(static_cast<std::size_t>(count));

    std::uint32_t previous_tag = 0;
    bool first = true;
    for (std::uint64_t i = 0; i < count; ++i) {
        const std::size_t at = static_cast<std::size_t>(i) * entry_size;

        RawEntry e;
        // Entry layout: tag, type, count, value-or-offset.
        // [impl STR-007 BTF-006]
        e.tag = get_u16(body, at + 0, order);
        e.type = get_u16(body, at + 2, order);
        e.count = container == Container::Classic
                      ? static_cast<std::uint64_t>(get_u32(body, at + 4, order))
                      : get_u64(body, at + 4, order);
        const std::size_t value_field_at =
            at + (container == Container::Classic ? 8 : 12);

        // Entries must be sorted ascending by tag; tolerated as a vendor
        // deviation under tolerate.unsorted-ifd. [impl STR-009]
        if (!first && e.tag <= previous_tag) {
            if (!policy.tolerate_unsorted_ifd) {
                return Error{ErrorCode::UNSORTED_IFD, MODULE,
                             "IFD entries are not sorted by tag"};
            }
            if (sink != nullptr) {
                sink->warning(Error{ErrorCode::UNSORTED_IFD, MODULE,
                                    "IFD entries are not sorted by tag "
                                    "(tolerated by policy)"});
            }
        }
        previous_tag = e.tag;
        first = false;

        e.type_size = field_type_size(e.type);
        if (e.type_size == 0) {
            // Unknown field type: skip the field gracefully.
            // [impl GEN-017]
            ++ifd.skipped_unknown_type;
            continue;
        }
        e.value_size = e.count * e.type_size;

        // A value is inline iff it fits the value field (4 bytes classic,
        // 8 bytes BigTIFF), left-justified. [impl STR-008 BTF-007]
        if (e.value_size <= inline_capacity) {
            e.is_inline = true;
            for (std::size_t k = 0; k < static_cast<std::size_t>(e.value_size);
                 ++k)
                e.inline_bytes[k] = body[value_field_at + k];
        } else {
            e.is_inline = false;
            e.value_offset =
                container == Container::Classic
                    ? static_cast<std::uint64_t>(
                          get_u32(body, value_field_at, order))
                    : get_u64(body, value_field_at, order);
            // Out-of-line values are expected on a word boundary.
            // [impl STR-018]
            if ((e.value_offset & 1u) != 0 &&
                !policy.tolerate_unaligned_values) {
                return Error{ErrorCode::BAD_OFFSET, MODULE,
                             "value offset for tag " + std::to_string(e.tag) +
                                 " is odd"};
            }
        }
        ifd.entries.push_back(e);
    }

    const std::size_t next_at = static_cast<std::size_t>(count) * entry_size;
    ifd.next_ifd_offset =
        container == Container::Classic
            ? static_cast<std::uint64_t>(get_u32(body, next_at, order))
            : get_u64(body, next_at, order);
    return ifd;
}

} // namespace tiff::core

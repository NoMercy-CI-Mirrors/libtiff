#include "core/file.hpp"

#include "core/bytes.hpp"
#include "core/geometry.hpp"
#include "core/header.hpp"
#include "core/ifd.hpp"

#include <algorithm>
#include <string>

namespace tiff::core {

namespace {

constexpr const char *MODULE = "file";

// Implausible entry counts are refused before allocation.
constexpr std::uint64_t MAX_IFD_ENTRIES = 65535;

bool type_is(const RawEntry &e, FieldType t) {
    return e.type == static_cast<std::uint16_t>(t);
}

bool is_unsigned_int_type(const RawEntry &e, bool allow_long8) {
    // Unsigned-integer fields accept SHORT or LONG (BYTE for complete-
    // ness); LONG8 additionally in BigTIFF offset/count tags, and only
    // there: types 16-18 are invalid in a classic file.
    // [impl GEN-004 BTF-009 BTF-010]
    return type_is(e, FieldType::Byte) || type_is(e, FieldType::Short) ||
           type_is(e, FieldType::Long) ||
           (allow_long8 && type_is(e, FieldType::Long8));
}

struct Fetcher {
    IoProvider &io;
    std::uint64_t file_size;
    ByteOrder order;
    const ReadPolicy &policy;

    // Reads an entry's values and widens each element to uint64.
    Result<std::vector<std::uint64_t>> values(const RawEntry &e) const {
        if (e.value_size > policy.max_single_alloc)
            return Error{ErrorCode::ALLOC_LIMIT_EXCEEDED, MODULE,
                         "tag value size exceeds the allocation limit"};
        std::vector<Byte> raw(static_cast<std::size_t>(e.value_size));
        if (e.is_inline) {
            std::copy(e.inline_bytes, e.inline_bytes + raw.size(),
                      raw.begin());
        } else {
            // Every data offset must reference real data of the stated
            // size inside the file. [impl STR-016]
            if (e.value_offset > file_size ||
                file_size - e.value_offset < e.value_size)
                return Error{ErrorCode::BAD_OFFSET, MODULE,
                             "tag " + std::to_string(e.tag) +
                                 " value lies outside the file"};
            auto r = io.read_at(e.value_offset, Span<Byte>(raw));
            if (!r)
                return r.take_error();
        }
        std::vector<std::uint64_t> out(
            static_cast<std::size_t>(e.count));
        Span<const Byte> s(raw.data(), raw.size());
        for (std::size_t i = 0; i < out.size(); ++i) {
            switch (e.type_size) {
            case 1:
                out[i] = raw[i];
                break;
            case 2:
                out[i] = get_u16(s, i * 2, order);
                break;
            case 4:
                out[i] = get_u32(s, i * 4, order);
                break;
            case 8:
                out[i] = get_u64(s, i * 8, order);
                break;
            default:
                return Error{ErrorCode::TAG_BAD_TYPE, MODULE,
                             "unsupported element size"};
            }
        }
        return out;
    }

    // Single scalar, unsigned integer type required. [impl GEN-004]
    Result<std::uint64_t> scalar_u32(const RawEntry &e,
                                     const char *name) const {
        if (!is_unsigned_int_type(e, false))
            return Error{ErrorCode::TAG_BAD_TYPE, MODULE,
                         std::string(name) +
                             " has an unexpected field type"};
        if (e.count != 1)
            return Error{ErrorCode::TAG_BAD_COUNT, MODULE,
                         std::string(name) + " must have count 1"};
        auto v = values(e);
        if (!v)
            return v.take_error();
        return v.value()[0];
    }
};

} // namespace

Result<std::unique_ptr<File>> File::open_read(std::unique_ptr<IoProvider> io,
                                              ReadPolicy policy,
                                              DiagnosticSink *sink) {
    std::unique_ptr<File> f(new File(std::move(io), policy, sink));

    auto size = f->io_->size();
    if (!size)
        return size.take_error();
    f->file_size_ = size.value();

    // Header.
    const std::size_t prefix_len = static_cast<std::size_t>(
        std::min<std::uint64_t>(BIG_HEADER_SIZE, f->file_size_));
    std::vector<Byte> prefix(prefix_len);
    if (prefix_len > 0) {
        auto r = f->io_->read_at(0, Span<Byte>(prefix));
        if (!r)
            return r.take_error();
    }
    auto header = parse_header(Span<const Byte>(prefix.data(), prefix.size()),
                               policy);
    if (!header)
        return header.take_error();
    f->order_ = header.value().order;
    f->container_ = header.value().container;

    // First IFD: count field.
    const std::uint64_t ifd_at = header.value().first_ifd_offset;
    const std::size_t count_size = ifd_count_field_size(f->container_);
    if (ifd_at > f->file_size_ || f->file_size_ - ifd_at < count_size)
        return Error{ErrorCode::BAD_OFFSET, MODULE,
                     "first IFD offset lies outside the file"};
    std::vector<Byte> count_bytes(count_size);
    if (auto r = f->io_->read_at(ifd_at, Span<Byte>(count_bytes)); !r)
        return r.take_error();
    auto count = parse_ifd_count(
        Span<const Byte>(count_bytes.data(), count_bytes.size()), f->order_,
        f->container_);
    if (!count)
        return count.take_error();
    if (count.value() > MAX_IFD_ENTRIES)
        return Error{ErrorCode::BAD_IFD, MODULE,
                     "implausible IFD entry count"};

    // First IFD: body (entries + next-IFD offset).
    const std::uint64_t body_size =
        count.value() * ifd_entry_field_size(f->container_) +
        ifd_next_field_size(f->container_);
    const std::uint64_t body_at = ifd_at + count_size;
    if (body_at > f->file_size_ || f->file_size_ - body_at < body_size)
        return Error{ErrorCode::TRUNCATED, MODULE,
                     "IFD extends beyond the end of the file"};
    std::vector<Byte> body(static_cast<std::size_t>(body_size));
    if (auto r = f->io_->read_at(body_at, Span<Byte>(body)); !r)
        return r.take_error();
    auto ifd = parse_ifd_body(Span<const Byte>(body.data(), body.size()),
                              count.value(), f->order_, f->container_, policy,
                              sink);
    if (!ifd)
        return ifd.take_error();

    // Build the validated directory snapshot: the spike's scope is the
    // baseline grayscale image class, whose required-field set is
    // enforced piecewise below. [impl CLS-GRAY-6]
    Directory dir;
    dir.next_ifd_offset = ifd.value().next_ifd_offset;

    const Fetcher fetch{*f->io_, f->file_size_, f->order_, policy};
    auto find = [&](TagId tag) -> const RawEntry * {
        for (const RawEntry &e : ifd.value().entries)
            if (e.tag == tag)
                return &e;
        return nullptr;
    };

    // Unknown tags are skipped gracefully. [impl GEN-003]
    for (const RawEntry &e : ifd.value().entries) {
        switch (e.tag) {
        case tags::IMAGE_WIDTH:
        case tags::IMAGE_LENGTH:
        case tags::BITS_PER_SAMPLE:
        case tags::COMPRESSION:
        case tags::PHOTOMETRIC_INTERPRETATION:
        case tags::STRIP_OFFSETS:
        case tags::SAMPLES_PER_PIXEL:
        case tags::ROWS_PER_STRIP:
        case tags::STRIP_BYTE_COUNTS:
            break;
        default:
            ++dir.skipped_unknown_tags;
            break;
        }
    }
    dir.skipped_unknown_tags += ifd.value().skipped_unknown_type;

    // SamplesPerPixel (default 1). [impl GEN-002]
    if (const RawEntry *e = find(tags::SAMPLES_PER_PIXEL)) {
        auto v = fetch.scalar_u32(*e, "SamplesPerPixel");
        if (!v)
            return v.take_error();
        dir.samples_per_pixel = static_cast<std::uint16_t>(v.value());
    }

    // BitsPerSample (default 1); the writer emits all SamplesPerPixel
    // values. [impl GEN-002 TAG-258-01]
    if (const RawEntry *e = find(tags::BITS_PER_SAMPLE)) {
        if (!type_is(*e, FieldType::Short)) // [impl GEN-004]
            return Error{ErrorCode::TAG_BAD_TYPE, MODULE,
                         "BitsPerSample has an unexpected field type"};
        auto v = fetch.values(*e);
        if (!v)
            return v.take_error();
        if (v.value().empty())
            return Error{ErrorCode::TAG_BAD_COUNT, MODULE,
                         "BitsPerSample has no values"};
        if (v.value().size() != dir.samples_per_pixel && sink != nullptr)
            sink->warning(Error{ErrorCode::TAG_BAD_COUNT, MODULE,
                                "BitsPerSample count does not equal "
                                "SamplesPerPixel"});
        for (std::uint64_t b : v.value())
            if (b != v.value()[0])
                return Error{ErrorCode::TAG_BAD_VALUE, MODULE,
                             "differing per-sample BitsPerSample values are "
                             "not supported by the spike"};
        dir.bits_per_sample = static_cast<std::uint16_t>(v.value()[0]);
    }

    // Compression (default 1). [impl GEN-002]
    if (const RawEntry *e = find(tags::COMPRESSION)) {
        auto v = fetch.scalar_u32(*e, "Compression");
        if (!v)
            return v.take_error();
        dir.compression = static_cast<std::uint16_t>(v.value());
    }

    // PhotometricInterpretation is required and has no default.
    // [impl TAG-262-01]
    if (const RawEntry *e = find(tags::PHOTOMETRIC_INTERPRETATION)) {
        auto v = fetch.scalar_u32(*e, "PhotometricInterpretation");
        if (!v)
            return v.take_error();
        dir.photometric = static_cast<std::uint16_t>(v.value());
    } else {
        return Error{ErrorCode::TAG_MISSING, MODULE,
                     "PhotometricInterpretation is required and absent"};
    }

    // ImageWidth / ImageLength: SHORT or LONG. [impl GEN-004 TAG-256-01]
    const RawEntry *we = find(tags::IMAGE_WIDTH);
    const RawEntry *le = find(tags::IMAGE_LENGTH);
    if (we == nullptr || le == nullptr)
        return Error{ErrorCode::TAG_MISSING, MODULE,
                     "ImageWidth and ImageLength are required"};
    auto wv = fetch.scalar_u32(*we, "ImageWidth");
    if (!wv)
        return wv.take_error();
    auto lv = fetch.scalar_u32(*le, "ImageLength");
    if (!lv)
        return lv.take_error();
    dir.width = static_cast<std::uint32_t>(wv.value());
    dir.length = static_cast<std::uint32_t>(lv.value());

    // RowsPerStrip: any value 1..2^32-1, default 2^32-1 (single strip).
    // [impl GEN-002 TAG-278-02]
    if (const RawEntry *e = find(tags::ROWS_PER_STRIP)) {
        auto v = fetch.scalar_u32(*e, "RowsPerStrip");
        if (!v)
            return v.take_error();
        dir.rows_per_strip = static_cast<std::uint32_t>(v.value());
    }

    // Every image IFD needs a data-location mechanism; for the spike's
    // scope that is strips. [impl DEP-005]
    const RawEntry *so = find(tags::STRIP_OFFSETS);
    const RawEntry *sc = find(tags::STRIP_BYTE_COUNTS);
    if (so == nullptr || sc == nullptr)
        return Error{ErrorCode::TAG_MISSING, MODULE,
                     "StripOffsets and StripByteCounts are required"};
    const bool allow_long8 = f->container_ == Container::Big;
    if (!is_unsigned_int_type(*so, allow_long8) ||
        !is_unsigned_int_type(*sc, allow_long8))
        return Error{ErrorCode::TAG_BAD_TYPE, MODULE,
                     "strip offset/count tags have unexpected field types"};
    auto sov = fetch.values(*so);
    if (!sov)
        return sov.take_error();
    auto scv = fetch.values(*sc);
    if (!scv)
        return scv.take_error();
    dir.strip_offsets = std::move(sov.value());
    dir.strip_byte_counts = std::move(scv.value());

    // count(StripByteCounts) == count(StripOffsets). [impl DEP-003]
    if (dir.strip_offsets.size() != dir.strip_byte_counts.size())
        return Error{ErrorCode::TAG_BAD_COUNT, MODULE,
                     "StripOffsets and StripByteCounts have differing "
                     "counts"};

    // count == StripsPerImage = ceil(ImageLength / RowsPerStrip).
    // [impl DEP-001]
    auto strips = ::tiff::core::strip_count(dir.length, dir.rows_per_strip);
    if (!strips)
        return strips.take_error();
    if (dir.strip_offsets.size() != strips.value())
        return Error{ErrorCode::TAG_BAD_COUNT, MODULE,
                     "strip tag counts do not match StripsPerImage"};

    f->dir_ = std::move(dir);
    return Result<std::unique_ptr<File>>(std::move(f));
}

Result<std::uint64_t> File::strip_size(std::uint32_t strip_index) const {
    return strip_decoded_size(dir_.width, dir_.length, dir_.samples_per_pixel,
                              dir_.bits_per_sample, dir_.rows_per_strip,
                              strip_index);
}

Result<std::size_t> File::read_strip(std::uint32_t strip_index,
                                     Span<Byte> out) {
    if (strip_index >= strip_count())
        return Error{ErrorCode::BAD_PARAMETER, MODULE,
                     "strip index out of range"};

    auto expected = strip_size(strip_index);
    if (!expected)
        return expected.take_error();
    if (out.size() < expected.value())
        return Error{ErrorCode::BUFFER_TOO_SMALL, MODULE,
                     "output buffer is smaller than the decoded strip"};

    const std::uint64_t offset = dir_.strip_offsets[strip_index];
    const std::uint64_t coded_size = dir_.strip_byte_counts[strip_index];
    // Strip data must lie inside the file. [impl STR-016 TAG-273-01]
    if (offset > file_size_ || file_size_ - offset < coded_size)
        return Error{ErrorCode::BAD_OFFSET, MODULE,
                     "strip data lies outside the file"};
    if (coded_size > policy_.max_single_alloc)
        return Error{ErrorCode::ALLOC_LIMIT_EXCEEDED, MODULE,
                     "coded strip size exceeds the allocation limit"};

    std::vector<Byte> coded(static_cast<std::size_t>(coded_size));
    if (auto r = io_->read_at(offset, Span<Byte>(coded)); !r)
        return r.take_error();

    // Baseline readers must support the baseline schemes; others are
    // reported as unsupported. [impl TAG-259-01]
    std::unique_ptr<Codec> codec = make_codec(dir_.compression);
    if (codec == nullptr)
        return Error{ErrorCode::CODEC_UNSUPPORTED, MODULE,
                     "compression scheme " +
                         std::to_string(dir_.compression) +
                         " is not supported"};

    return codec->decode_strip(
        Span<const Byte>(coded.data(), coded.size()),
        out.first(static_cast<std::size_t>(expected.value())));
}

} // namespace tiff::core

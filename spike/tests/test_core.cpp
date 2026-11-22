// tiff_core spike: C++ unit tests for the core, driven through the
// domain and application layers directly (plan §8.1 driving side).
// Each test carries verify annotations for the trace scanner.
#include "core/file.hpp"
#include "core/geometry.hpp"
#include "core/header.hpp"
#include "core/ifd.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace tiff::core;

static int g_failures = 0;

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

#define CHECK_ERR(result, expected_code)                                      \
    do {                                                                      \
        CHECK(!(result));                                                     \
        if (!(result))                                                        \
            CHECK((result).error().code == (expected_code));                  \
    } while (0)

// ---- builders ------------------------------------------------------------

namespace {

constexpr std::uint64_t DATA = 0xDDDDDDDDull; // sentinel: patched to the
                                              // appended data offset

struct EntrySpec {
    std::uint16_t tag;
    std::uint16_t type;
    std::uint32_t count;
    std::uint64_t value;
};

void push16(std::vector<Byte> &v, std::uint16_t x) {
    v.push_back(static_cast<Byte>(x & 0xFF));
    v.push_back(static_cast<Byte>(x >> 8));
}
void push32(std::vector<Byte> &v, std::uint32_t x) {
    push16(v, static_cast<std::uint16_t>(x & 0xFFFF));
    push16(v, static_cast<std::uint16_t>(x >> 16));
}
void push64(std::vector<Byte> &v, std::uint64_t x) {
    push32(v, static_cast<std::uint32_t>(x & 0xFFFFFFFF));
    push32(v, static_cast<std::uint32_t>(x >> 32));
}

// Little-endian classic TIFF: header, one IFD, then the data blob.
std::vector<Byte> build_classic_le(const std::vector<EntrySpec> &entries,
                                   const std::vector<Byte> &data) {
    const std::uint32_t data_off =
        8 + 2 + 12 * static_cast<std::uint32_t>(entries.size()) + 4;
    std::vector<Byte> v;
    v.push_back('I');
    v.push_back('I');
    push16(v, 42);
    push32(v, 8);
    push16(v, static_cast<std::uint16_t>(entries.size()));
    for (const EntrySpec &e : entries) {
        push16(v, e.tag);
        push16(v, e.type);
        push32(v, e.count);
        push32(v, static_cast<std::uint32_t>(e.value == DATA ? data_off
                                                             : e.value));
    }
    push32(v, 0); // next IFD
    v.insert(v.end(), data.begin(), data.end());
    return v;
}

// Little-endian BigTIFF: 16-byte header, one 20-byte-entry IFD, data.
std::vector<Byte> build_big_le(const std::vector<EntrySpec> &entries,
                               const std::vector<Byte> &data) {
    const std::uint64_t data_off =
        16 + 8 + 20 * static_cast<std::uint64_t>(entries.size()) + 8;
    std::vector<Byte> v;
    v.push_back('I');
    v.push_back('I');
    push16(v, 43);
    push16(v, 8);
    push16(v, 0);
    push64(v, 16);
    push64(v, entries.size());
    for (const EntrySpec &e : entries) {
        push16(v, e.tag);
        push16(v, e.type);
        push64(v, e.count);
        push64(v, e.value == DATA ? data_off : e.value);
    }
    push64(v, 0); // next IFD
    v.insert(v.end(), data.begin(), data.end());
    return v;
}

// 4x4, 8-bit grayscale, PackBits, one strip.
const std::vector<Byte> PB_DATA = {0x03, 0x00, 0x01, 0x02, 0x03, 0xFD, 0x11,
                                   0x03, 0x22, 0x23, 0x24, 0x25, 0xFD, 0x33};
const std::vector<Byte> PB_PIXELS = {0x00, 0x01, 0x02, 0x03, 0x11, 0x11,
                                     0x11, 0x11, 0x22, 0x23, 0x24, 0x25,
                                     0x33, 0x33, 0x33, 0x33};

std::vector<EntrySpec> gray4x4_entries() {
    return {
        {256, 3, 1, 4},     {257, 3, 1, 4},  {258, 3, 1, 8},
        {259, 3, 1, 32773}, {262, 3, 1, 1},  {273, 4, 1, DATA},
        {277, 3, 1, 1},     {278, 3, 1, 4},  {279, 4, 1, 14},
    };
}

Result<std::unique_ptr<File>> open_mem(std::vector<Byte> bytes,
                                       ReadPolicy policy,
                                       DiagnosticSink *sink = nullptr) {
    return File::open_read(
        std::unique_ptr<IoProvider>(new MemoryIo(std::move(bytes))), policy,
        sink);
}

// Port test double: I/O that fails after a number of reads, proving the
// IoProvider port is substitutable (plan §8.1).
class FaultyIo final : public IoProvider {
  public:
    FaultyIo(std::vector<Byte> data, int allowed_reads)
        : inner_(std::move(data)), allowed_(allowed_reads) {}
    Result<void> read_at(std::uint64_t offset, Span<Byte> out) override {
        if (allowed_-- <= 0)
            return Error{ErrorCode::IO_READ, "FaultyIo", "injected fault"};
        return inner_.read_at(offset, out);
    }
    Result<std::uint64_t> size() override { return inner_.size(); }

  private:
    MemoryIo inner_;
    int allowed_;
};

} // namespace

// ---- header --------------------------------------------------------------

static void test_header() {
    const ReadPolicy strict = ReadPolicy::strict();

    // Classic little-endian. [verify STR-001 STR-002 STR-003 GEN-001]
    {
        const Byte h[] = {'I', 'I', 42, 0, 8, 0, 0, 0};
        auto r = parse_header(Span<const Byte>(h, sizeof h), strict);
        CHECK(r);
        CHECK(r.value().order == ByteOrder::LittleEndian);
        CHECK(r.value().container == Container::Classic);
        CHECK(r.value().first_ifd_offset == 8);
    }
    // Classic big-endian. [verify STR-001 GEN-001]
    {
        const Byte h[] = {'M', 'M', 0, 42, 0, 0, 0, 8};
        auto r = parse_header(Span<const Byte>(h, sizeof h), strict);
        CHECK(r);
        CHECK(r.value().order == ByteOrder::BigEndian);
    }
    // Invalid byte-order marker. [verify STR-001]
    {
        const Byte h[] = {'X', 'X', 42, 0, 8, 0, 0, 0};
        CHECK_ERR(parse_header(Span<const Byte>(h, sizeof h), strict),
                  ErrorCode::BAD_BYTE_ORDER);
    }
    // Wrong magic. [verify STR-002]
    {
        const Byte h[] = {'I', 'I', 41, 0, 8, 0, 0, 0};
        CHECK_ERR(parse_header(Span<const Byte>(h, sizeof h), strict),
                  ErrorCode::BAD_MAGIC);
    }
    // First IFD offset below 8. [verify STR-003]
    {
        const Byte h[] = {'I', 'I', 42, 0, 4, 0, 0, 0};
        CHECK_ERR(parse_header(Span<const Byte>(h, sizeof h), strict),
                  ErrorCode::BAD_HEADER);
    }
    // Odd first IFD offset: strict refuses, legacy tolerates
    // (tolerate.unaligned-values). [verify STR-003]
    {
        const Byte h[] = {'I', 'I', 42, 0, 9, 0, 0, 0};
        CHECK_ERR(parse_header(Span<const Byte>(h, sizeof h), strict),
                  ErrorCode::BAD_HEADER);
        auto r =
            parse_header(Span<const Byte>(h, sizeof h), ReadPolicy::legacy());
        CHECK(r);
        CHECK(r.value().first_ifd_offset == 9);
    }
    // BigTIFF, valid. [verify BTF-001 BTF-002 BTF-003 BTF-004]
    {
        const Byte h[] = {'I', 'I', 43, 0, 8, 0, 0, 0,
                          16,  0,   0,  0, 0, 0, 0, 0};
        auto r = parse_header(Span<const Byte>(h, sizeof h), strict);
        CHECK(r);
        CHECK(r.value().container == Container::Big);
        CHECK(r.value().first_ifd_offset == 16);
    }
    // BigTIFF offset size != 8 must be refused. [verify BTF-002]
    {
        const Byte h[] = {'I', 'I', 43, 0, 4, 0, 0, 0,
                          16,  0,   0,  0, 0, 0, 0, 0};
        CHECK_ERR(parse_header(Span<const Byte>(h, sizeof h), strict),
                  ErrorCode::BAD_OFFSET_SIZE);
    }
    // BigTIFF reserved bytes != 0 must be refused. [verify BTF-003]
    {
        const Byte h[] = {'I', 'I', 43, 0, 8, 0, 1, 0,
                          16,  0,   0,  0, 0, 0, 0, 0};
        CHECK_ERR(parse_header(Span<const Byte>(h, sizeof h), strict),
                  ErrorCode::BAD_RESERVED);
    }
    // BigTIFF first IFD inside the header. [verify BTF-004]
    {
        const Byte h[] = {'I', 'I', 43, 0, 8, 0, 0, 0,
                          8,   0,   0,  0, 0, 0, 0, 0};
        CHECK_ERR(parse_header(Span<const Byte>(h, sizeof h), strict),
                  ErrorCode::BAD_HEADER);
    }
    // Truncated header.
    {
        const Byte h[] = {'I', 'I', 42, 0};
        CHECK_ERR(parse_header(Span<const Byte>(h, sizeof h), strict),
                  ErrorCode::TRUNCATED);
    }
}

// ---- IFD -----------------------------------------------------------------

static std::vector<Byte> entry12(std::uint16_t tag, std::uint16_t type,
                                 std::uint32_t count, std::uint32_t value) {
    std::vector<Byte> v;
    push16(v, tag);
    push16(v, type);
    push32(v, count);
    push32(v, value);
    return v;
}

static void test_ifd() {
    const ReadPolicy strict = ReadPolicy::strict();
    const auto LE = ByteOrder::LittleEndian;

    // Zero-entry IFD is invalid. [verify STR-006]
    {
        const Byte c[] = {0, 0};
        CHECK_ERR(parse_ifd_count(Span<const Byte>(c, 2), LE,
                                  Container::Classic),
                  ErrorCode::BAD_IFD);
    }
    // Two sorted entries parse; 12-byte layout; next offset read.
    // [verify STR-005 STR-007]
    {
        std::vector<Byte> body = entry12(256, 3, 1, 4);
        auto e2 = entry12(257, 3, 1, 4);
        body.insert(body.end(), e2.begin(), e2.end());
        push32(body, 0);
        auto r = parse_ifd_body(Span<const Byte>(body.data(), body.size()), 2,
                                LE, Container::Classic, strict, nullptr);
        CHECK(r);
        CHECK(r.value().entries.size() == 2);
        CHECK(r.value().entries[0].tag == 256);
        CHECK(r.value().next_ifd_offset == 0);
    }
    // Unsorted: strict refuses; legacy warns and continues
    // (tolerate.unsorted-ifd). [verify STR-009]
    {
        std::vector<Byte> body = entry12(257, 3, 1, 4);
        auto e2 = entry12(256, 3, 1, 4);
        body.insert(body.end(), e2.begin(), e2.end());
        push32(body, 0);
        CHECK_ERR(parse_ifd_body(Span<const Byte>(body.data(), body.size()),
                                 2, LE, Container::Classic, strict, nullptr),
                  ErrorCode::UNSORTED_IFD);
        CollectingSink sink;
        auto r = parse_ifd_body(Span<const Byte>(body.data(), body.size()), 2,
                                LE, Container::Classic, ReadPolicy::legacy(),
                                &sink);
        CHECK(r);
        CHECK(sink.warnings.size() == 1);
    }
    // Inline threshold: 4 bytes classic. SHORT x1 inline; LONG x2 and
    // RATIONAL x1 out-of-line. [verify STR-008]
    {
        std::vector<Byte> body = entry12(256, 3, 1, 7);
        auto e2 = entry12(320, 4, 2, 100);
        auto e3 = entry12(321, 5, 1, 100);
        body.insert(body.end(), e2.begin(), e2.end());
        body.insert(body.end(), e3.begin(), e3.end());
        push32(body, 0);
        auto r = parse_ifd_body(Span<const Byte>(body.data(), body.size()), 3,
                                LE, Container::Classic, strict, nullptr);
        CHECK(r);
        CHECK(r.value().entries[0].is_inline);
        CHECK(r.value().entries[0].inline_bytes[0] == 7);
        CHECK(!r.value().entries[1].is_inline);
        CHECK(r.value().entries[1].value_offset == 100);
        CHECK(!r.value().entries[2].is_inline);
    }
    // Odd out-of-line value offset: strict refuses, legacy tolerates
    // (tolerate.unaligned-values). [verify STR-018]
    {
        std::vector<Byte> body = entry12(320, 4, 2, 101);
        push32(body, 0);
        CHECK_ERR(parse_ifd_body(Span<const Byte>(body.data(), body.size()),
                                 1, LE, Container::Classic, strict, nullptr),
                  ErrorCode::BAD_OFFSET);
        auto r = parse_ifd_body(Span<const Byte>(body.data(), body.size()), 1,
                                LE, Container::Classic, ReadPolicy::legacy(),
                                nullptr);
        CHECK(r);
    }
    // Baseline and 6.0 field-type sizes. [verify STR-010 STR-011]
    {
        CHECK(field_type_size(1) == 1);  // BYTE
        CHECK(field_type_size(2) == 1);  // ASCII
        CHECK(field_type_size(3) == 2);  // SHORT
        CHECK(field_type_size(4) == 4);  // LONG
        CHECK(field_type_size(5) == 8);  // RATIONAL
        CHECK(field_type_size(6) == 1);  // SBYTE
        CHECK(field_type_size(7) == 1);  // UNDEFINED
        CHECK(field_type_size(8) == 2);  // SSHORT
        CHECK(field_type_size(9) == 4);  // SLONG
        CHECK(field_type_size(10) == 8); // SRATIONAL
        CHECK(field_type_size(11) == 4); // FLOAT
        CHECK(field_type_size(12) == 8); // DOUBLE
        CHECK(field_type_size(99) == 0); // unknown
    }
    // Unknown field type is skipped gracefully. [verify GEN-017]
    {
        std::vector<Byte> body = entry12(256, 99, 1, 4);
        auto e2 = entry12(257, 3, 1, 4);
        body.insert(body.end(), e2.begin(), e2.end());
        push32(body, 0);
        auto r = parse_ifd_body(Span<const Byte>(body.data(), body.size()), 2,
                                LE, Container::Classic, strict, nullptr);
        CHECK(r);
        CHECK(r.value().entries.size() == 1);
        CHECK(r.value().skipped_unknown_type == 1);
    }
    // BigTIFF 20-byte entries; 8-byte inline threshold.
    // [verify BTF-006 BTF-007]
    {
        std::vector<Byte> body;
        push16(body, 273);
        push16(body, 16); // LONG8
        push64(body, 1);
        push64(body, 0x11223344);
        push16(body, 279);
        push16(body, 5); // RATIONAL x2 = 16 bytes: out of line
        push64(body, 2);
        push64(body, 200);
        push64(body, 0);
        auto r = parse_ifd_body(Span<const Byte>(body.data(), body.size()), 2,
                                LE, Container::Big, strict, nullptr);
        CHECK(r);
        CHECK(r.value().entries[0].is_inline);
        CHECK(r.value().entries[0].value_size == 8);
        CHECK(!r.value().entries[1].is_inline);
        CHECK(r.value().entries[1].value_offset == 200);
    }
}

// ---- geometry ------------------------------------------------------------

static void test_geometry() {
    // Packed rows: no unused bits except at the end of a row.
    // [verify CMP-1-01]
    {
        auto r = scanline_size_bytes(3, 1, 1);
        CHECK(r);
        CHECK(r.value() == 1);
        r = scanline_size_bytes(157, 1, 8);
        CHECK(r);
        CHECK(r.value() == 157);
    }
    // StripsPerImage = ceil(ImageLength / RowsPerStrip). [verify DEP-001]
    {
        auto r = strip_count(151, 52);
        CHECK(r);
        CHECK(r.value() == 3);
        // RowsPerStrip >= ImageLength: single strip. [verify TAG-278-02]
        r = strip_count(4, 0xFFFFFFFFu);
        CHECK(r);
        CHECK(r.value() == 1);
    }
    // Short last strip. [verify DEP-001]
    {
        auto r = rows_in_strip(151, 52, 2);
        CHECK(r);
        CHECK(r.value() == 47);
    }
    // Checked arithmetic refuses overflow.
    {
        CHECK_ERR(checked_mul(0x8000000000000000ull, 3),
                  ErrorCode::TAG_BAD_VALUE);
    }
}

// ---- codecs ----------------------------------------------------------------

static void test_packbits() {
    auto codec = make_codec(compression::PACKBITS);
    CHECK(codec != nullptr);

    // Worked example from the TIFF 6.0 PackBits section.
    // [verify CMP-32773-01]
    {
        const Byte src[] = {0xFE, 0xAA, 0x02, 0x80, 0x00, 0x2A, 0xFD, 0xAA,
                            0x03, 0x80, 0x00, 0x2A, 0x22, 0xF7, 0xAA};
        const Byte expect[] = {0xAA, 0xAA, 0xAA, 0x80, 0x00, 0x2A, 0xAA, 0xAA,
                               0xAA, 0xAA, 0x80, 0x00, 0x2A, 0x22, 0xAA, 0xAA,
                               0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
        Byte out[sizeof expect];
        auto r = codec->decode_strip(Span<const Byte>(src, sizeof src),
                                     Span<Byte>(out, sizeof out));
        CHECK(r);
        CHECK(r.value() == sizeof expect);
        bool same = true;
        for (std::size_t i = 0; i < sizeof expect; ++i)
            same = same && out[i] == expect[i];
        CHECK(same);
    }
    // -128 is a no-op. [verify CMP-32773-01]
    {
        const Byte src[] = {0x80, 0xFE, 0xAA};
        Byte out[3];
        auto r = codec->decode_strip(Span<const Byte>(src, sizeof src),
                                     Span<Byte>(out, sizeof out));
        CHECK(r);
        CHECK(out[0] == 0xAA && out[2] == 0xAA);
    }
    // Truncated input is an error, not an overrun. [verify CMP-32773-01]
    {
        const Byte src[] = {0x03, 0x00};
        Byte out[4];
        CHECK_ERR(codec->decode_strip(Span<const Byte>(src, sizeof src),
                                      Span<Byte>(out, sizeof out)),
                  ErrorCode::CODEC_DECODE);
    }
    // Output overrun is an error.
    {
        const Byte src[] = {0xFD, 0xAA};
        Byte out[2];
        CHECK_ERR(codec->decode_strip(Span<const Byte>(src, sizeof src),
                                      Span<Byte>(out, sizeof out)),
                  ErrorCode::CODEC_DECODE);
    }
}

// ---- end-to-end through the application layer ------------------------------

static void test_full_read() {
    const ReadPolicy strict = ReadPolicy::strict();

    // Complete baseline grayscale image, PackBits, one strip.
    // [verify CLS-GRAY-6 CMP-32773-01 TAG-273-01]
    {
        auto f = open_mem(build_classic_le(gray4x4_entries(), PB_DATA),
                          strict);
        CHECK(f);
        const Directory &d = f.value()->directory();
        CHECK(d.width == 4 && d.length == 4);
        CHECK(d.bits_per_sample == 8);
        CHECK(d.compression == 32773);
        CHECK(f.value()->strip_count() == 1);
        auto size = f.value()->strip_size(0);
        CHECK(size);
        CHECK(size.value() == 16);
        Byte out[16] = {};
        auto n = f.value()->read_strip(0, Span<Byte>(out, sizeof out));
        CHECK(n);
        CHECK(n.value() == 16);
        bool same = true;
        for (std::size_t i = 0; i < 16; ++i)
            same = same && out[i] == PB_PIXELS[i];
        CHECK(same);
    }
    // Defaults apply for absent fields: drop SamplesPerPixel and
    // RowsPerStrip. [verify GEN-002 TAG-278-02]
    {
        std::vector<EntrySpec> e = gray4x4_entries();
        std::vector<EntrySpec> reduced;
        for (const EntrySpec &s : e)
            if (s.tag != 277 && s.tag != 278)
                reduced.push_back(s);
        auto f = open_mem(build_classic_le(reduced, PB_DATA), strict);
        CHECK(f);
        CHECK(f.value()->directory().samples_per_pixel == 1);
        CHECK(f.value()->directory().rows_per_strip == 0xFFFFFFFFu);
        CHECK(f.value()->strip_count() == 1);
    }
    // Unknown (private) tags are skipped. [verify GEN-003]
    {
        std::vector<EntrySpec> e = gray4x4_entries();
        e.push_back({65000, 3, 1, 123});
        auto f = open_mem(build_classic_le(e, PB_DATA), strict);
        CHECK(f);
        CHECK(f.value()->directory().skipped_unknown_tags == 1);
    }
    // Width as LONG is as acceptable as SHORT.
    // [verify GEN-004 TAG-256-01]
    {
        std::vector<EntrySpec> e = gray4x4_entries();
        e[0] = {256, 4, 1, 4};
        auto f = open_mem(build_classic_le(e, PB_DATA), strict);
        CHECK(f);
        CHECK(f.value()->directory().width == 4);
    }
    // Width as RATIONAL is a type error. [verify GEN-004]
    {
        std::vector<EntrySpec> e = gray4x4_entries();
        e[0] = {256, 5, 1, DATA};
        auto f = open_mem(build_classic_le(e, PB_DATA), strict);
        CHECK_ERR(f, ErrorCode::TAG_BAD_TYPE);
    }
    // Missing PhotometricInterpretation is an error; it has no default.
    // [verify TAG-262-01]
    {
        std::vector<EntrySpec> e;
        for (const EntrySpec &s : gray4x4_entries())
            if (s.tag != 262)
                e.push_back(s);
        CHECK_ERR(open_mem(build_classic_le(e, PB_DATA), strict),
                  ErrorCode::TAG_MISSING);
    }
    // Missing strip location tags: no data-location mechanism.
    // [verify DEP-005]
    {
        std::vector<EntrySpec> e;
        for (const EntrySpec &s : gray4x4_entries())
            if (s.tag != 273)
                e.push_back(s);
        CHECK_ERR(open_mem(build_classic_le(e, PB_DATA), strict),
                  ErrorCode::TAG_MISSING);
    }
    // Mismatched StripOffsets/StripByteCounts counts. [verify DEP-003]
    {
        std::vector<EntrySpec> e = gray4x4_entries();
        for (EntrySpec &s : e)
            if (s.tag == 279)
                s = {279, 3, 2, 14}; // two SHORT counts, still inline
        CHECK_ERR(open_mem(build_classic_le(e, PB_DATA), strict),
                  ErrorCode::TAG_BAD_COUNT);
    }
    // Unsupported compression scheme reported as such (open succeeds,
    // decode refuses). [verify TAG-259-01]
    {
        std::vector<EntrySpec> e = gray4x4_entries();
        for (EntrySpec &s : e)
            if (s.tag == 259)
                s.value = 5; // LZW: outside the spike's codec set
        auto f = open_mem(build_classic_le(e, PB_DATA), strict);
        CHECK(f);
        Byte out[16];
        CHECK_ERR(f.value()->read_strip(0, Span<Byte>(out, sizeof out)),
                  ErrorCode::CODEC_UNSUPPORTED);
    }
    // Strip data outside the file is refused. [verify STR-016]
    {
        std::vector<EntrySpec> e = gray4x4_entries();
        for (EntrySpec &s : e)
            if (s.tag == 273)
                s.value = 100000;
        auto f = open_mem(build_classic_le(e, PB_DATA), strict);
        CHECK(f);
        Byte out[16];
        CHECK_ERR(f.value()->read_strip(0, Span<Byte>(out, sizeof out)),
                  ErrorCode::BAD_OFFSET);
    }
    // Whole-file unsorted IFD: strict refuses; legacy reads correctly
    // with a warning. [verify STR-009]
    {
        std::vector<EntrySpec> e = gray4x4_entries();
        std::swap(e[0], e[1]);
        CHECK_ERR(open_mem(build_classic_le(e, PB_DATA), strict),
                  ErrorCode::UNSORTED_IFD);
        CollectingSink sink;
        auto f = open_mem(build_classic_le(e, PB_DATA), ReadPolicy::legacy(),
                          &sink);
        CHECK(f);
        CHECK(sink.warnings.size() == 1);
        Byte out[16] = {};
        auto n = f.value()->read_strip(0, Span<Byte>(out, sizeof out));
        CHECK(n);
        CHECK(out[4] == 0x11);
    }
    // LONG8 strip offsets are invalid in a classic file.
    // [verify BTF-009]
    {
        std::vector<EntrySpec> e = gray4x4_entries();
        for (EntrySpec &s : e)
            if (s.tag == 273)
                s.type = 16; // LONG8 in a classic (42) container
        CHECK_ERR(open_mem(build_classic_le(e, PB_DATA), strict),
                  ErrorCode::TAG_BAD_TYPE);
    }
    // BitsPerSample count differing from SamplesPerPixel draws a
    // warning (short counts are a known writer defect).
    // [verify TAG-258-01]
    {
        std::vector<EntrySpec> e = gray4x4_entries();
        for (EntrySpec &s : e)
            if (s.tag == 258)
                s = {258, 3, 2, 8 | (8u << 16)}; // two SHORTs inline
        CollectingSink sink;
        auto f = open_mem(build_classic_le(e, PB_DATA), strict, &sink);
        CHECK(f);
        CHECK(sink.warnings.size() == 1);
        CHECK(f.value()->directory().bits_per_sample == 8);
    }
    // BigTIFF end-to-end: 2x2 8-bit gray, uncompressed, LONG8 offsets.
    // [verify BTF-001 BTF-005 BTF-006 BTF-007 BTF-010 CMP-1-01]
    {
        const std::vector<EntrySpec> e = {
            {256, 3, 1, 2}, {257, 3, 1, 2},    {258, 3, 1, 8},
            {259, 3, 1, 1}, {262, 3, 1, 1},    {273, 16, 1, DATA},
            {279, 16, 1, 4},
        };
        const std::vector<Byte> pixels = {0xAA, 0xBB, 0xCC, 0xDD};
        auto f = open_mem(build_big_le(e, pixels), strict);
        CHECK(f);
        CHECK(f.value()->container() == Container::Big);
        CHECK(f.value()->directory().width == 2);
        Byte out[4] = {};
        auto n = f.value()->read_strip(0, Span<Byte>(out, sizeof out));
        CHECK(n);
        CHECK(n.value() == 4);
        CHECK(out[0] == 0xAA && out[3] == 0xDD);
    }
    // Port substitution: injected I/O faults surface as clean errors
    // (IoProvider is a mockable port, plan §8.1).
    {
        auto bytes = build_classic_le(gray4x4_entries(), PB_DATA);
        auto f = File::open_read(
            std::unique_ptr<IoProvider>(new FaultyIo(std::move(bytes), 2)),
            strict, nullptr);
        CHECK(!f);
        CHECK(f.error().code == ErrorCode::IO_READ);
    }
}

int main() {
    test_header();
    test_ifd();
    test_geometry();
    test_packbits();
    test_full_read();
    if (g_failures == 0) {
        std::printf("test_core: all checks passed\n");
        return 0;
    }
    std::printf("test_core: %d check(s) FAILED\n", g_failures);
    return 1;
}

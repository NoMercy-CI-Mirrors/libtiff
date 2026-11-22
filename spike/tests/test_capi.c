/* tiff_core spike: pure C99 integration test of the safe C API.
 * Compiled with a C compiler and no C++ in the translation unit: proves
 * the installed header is C-clean and the ABI usable from plain C.
 * [verify ...] annotations feed the trace scanner. */
#include <tiff_core/tiff_core.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

/* ---- synthesised test files --------------------------------------------- */

static void put16(unsigned char *p, uint16_t v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)(v >> 8);
}
static void put32(unsigned char *p, uint32_t v) {
    put16(p, (uint16_t)(v & 0xFFFF));
    put16(p + 2, (uint16_t)(v >> 16));
}

static void put_entry(unsigned char *p, uint16_t tag, uint16_t type,
                      uint32_t count, uint32_t value) {
    put16(p, tag);
    put16(p + 2, type);
    put32(p + 4, count);
    put32(p + 8, value);
}

/* 4x4 8-bit grayscale, PackBits, one strip; little-endian classic.
 * Layout: header (8) + count (2) + 9 entries (108) + next (4) = 122,
 * then 14 bytes of compressed data. */
static const unsigned char PB_DATA[14] = {0x03, 0x00, 0x01, 0x02, 0x03,
                                          0xFD, 0x11, 0x03, 0x22, 0x23,
                                          0x24, 0x25, 0xFD, 0x33};
static const unsigned char PB_PIXELS[16] = {0x00, 0x01, 0x02, 0x03, 0x11,
                                            0x11, 0x11, 0x11, 0x22, 0x23,
                                            0x24, 0x25, 0x33, 0x33, 0x33,
                                            0x33};

static int write_gray4x4(const char *path, int unsorted) {
    unsigned char buf[136];
    unsigned char *e = buf + 10;
    FILE *f;

    buf[0] = 'I';
    buf[1] = 'I';
    put16(buf + 2, 42);
    put32(buf + 4, 8);
    put16(buf + 8, 9);
    /* Entries ascending by tag (or deliberately swapped for the
     * unsorted-IFD variant). */
    put_entry(e + 0 * 12, (uint16_t)(unsorted ? 257 : 256), 3, 1, 4);
    put_entry(e + 1 * 12, (uint16_t)(unsorted ? 256 : 257), 3, 1, 4);
    put_entry(e + 2 * 12, 258, 3, 1, 8);
    put_entry(e + 3 * 12, 259, 3, 1, 32773);
    put_entry(e + 4 * 12, 262, 3, 1, 1);
    put_entry(e + 5 * 12, 273, 4, 1, 122);
    put_entry(e + 6 * 12, 277, 3, 1, 1);
    put_entry(e + 7 * 12, 278, 3, 1, 4);
    put_entry(e + 8 * 12, 279, 4, 1, 14);
    put32(buf + 118, 0); /* next IFD */
    memcpy(buf + 122, PB_DATA, sizeof PB_DATA);

    f = fopen(path, "wb");
    if (f == NULL)
        return -1;
    if (fwrite(buf, 1, sizeof buf, f) != sizeof buf) {
        fclose(f);
        return -1;
    }
    return fclose(f);
}

/* ---- tests ---------------------------------------------------------------- */

static void test_packbits_roundtrip(void) {
    tiff_file_t *f = NULL;
    char errmsg[256] = {0};
    uint32_t v = 0, strips = 0;
    uint64_t size = 0;
    unsigned char out[16] = {0};
    size_t produced = 0;

    CHECK(write_gray4x4("spike_pb.tif", 0) == 0);

    /* Open under the strict policy; a well-formed baseline grayscale
     * file needs no tolerances. [verify CLS-GRAY-6] */
    CHECK(tiff_open_read("spike_pb.tif", tiff_policy_strict, &f, errmsg,
                         sizeof errmsg) == tiff_ok);
    CHECK(f != NULL);
    if (f == NULL)
        return;

    CHECK(tiff_tag_get_u32(f, tiff_tag_image_width, &v) == tiff_ok && v == 4);
    CHECK(tiff_tag_get_u32(f, tiff_tag_image_length, &v) == tiff_ok && v == 4);
    CHECK(tiff_tag_get_u32(f, tiff_tag_bits_per_sample, &v) == tiff_ok &&
          v == 8);
    CHECK(tiff_tag_get_u32(f, tiff_tag_compression, &v) == tiff_ok &&
          v == 32773);
    CHECK(tiff_tag_get_u32(f, tiff_tag_samples_per_pixel, &v) == tiff_ok &&
          v == 1);

    CHECK(tiff_strip_count(f, &strips) == tiff_ok && strips == 1);
    CHECK(tiff_strip_size(f, 0, &size) == tiff_ok && size == 16);

    /* Caller-provided buffer with explicit capacity; PackBits decode.
     * [verify CMP-32773-01] */
    CHECK(tiff_strip_read(f, 0, out, sizeof out, &produced) == tiff_ok);
    CHECK(produced == 16);
    CHECK(memcmp(out, PB_PIXELS, 16) == 0);

    /* Undersized buffer: parameter-band status, exact code and message
     * from the per-handle error state. */
    CHECK(tiff_strip_read(f, 0, out, 8, &produced) == tiff_err_param);
    CHECK(tiff_error_code(f) == 601);
    CHECK(strlen(tiff_error_message(f)) > 0);
    CHECK(strlen(tiff_error_module(f)) > 0);

    /* Out-of-range strip index. */
    CHECK(tiff_strip_read(f, 9, out, sizeof out, &produced) ==
          tiff_err_param);

    CHECK(tiff_close(f) == tiff_ok);
}

static void test_policy_unsorted(void) {
    tiff_file_t *f = NULL;
    char errmsg[256] = {0};
    unsigned char out[16] = {0};
    size_t produced = 0;

    CHECK(write_gray4x4("spike_pb_unsorted.tif", 1) == 0);

    /* Strict policy refuses an unsorted IFD; the legacy policy
     * (tolerate.unsorted-ifd) accepts it and decodes correctly.
     * [verify STR-009] */
    CHECK(tiff_open_read("spike_pb_unsorted.tif", tiff_policy_strict, &f,
                         errmsg, sizeof errmsg) == tiff_err_format);
    CHECK(f == NULL);
    CHECK(strlen(errmsg) > 0);

    CHECK(tiff_open_read("spike_pb_unsorted.tif", tiff_policy_legacy, &f,
                         errmsg, sizeof errmsg) == tiff_ok);
    CHECK(f != NULL);
    if (f == NULL)
        return;
    CHECK(tiff_strip_read(f, 0, out, sizeof out, &produced) == tiff_ok);
    CHECK(memcmp(out, PB_PIXELS, 16) == 0);
    CHECK(tiff_close(f) == tiff_ok);
}

/* Real corpus file written by libtiff: big-endian, 157x151 8-bit gray,
 * uncompressed, 3 strips of 52 rows. Exercises the big-endian decode
 * path and multi-strip geometry from a plain C client.
 * [verify GEN-001 STR-001 CMP-1-01 DEP-001] */
static void test_corpus(const char *path) {
    tiff_file_t *f = NULL;
    char errmsg[256] = {0};
    uint32_t v = 0, strips = 0;
    uint64_t size = 0;
    static unsigned char out[8164];
    size_t produced = 0;

    if (tiff_open_read(path, tiff_policy_strict, &f, errmsg,
                       sizeof errmsg) != tiff_ok) {
        printf("FAIL: corpus open: %s\n", errmsg);
        ++g_failures;
        return;
    }

    CHECK(tiff_tag_get_u32(f, tiff_tag_image_width, &v) == tiff_ok &&
          v == 157);
    CHECK(tiff_tag_get_u32(f, tiff_tag_image_length, &v) == tiff_ok &&
          v == 151);
    CHECK(tiff_tag_get_u32(f, tiff_tag_bits_per_sample, &v) == tiff_ok &&
          v == 8);
    CHECK(tiff_tag_get_u32(f, tiff_tag_compression, &v) == tiff_ok && v == 1);
    CHECK(tiff_tag_get_u32(f, tiff_tag_rows_per_strip, &v) == tiff_ok &&
          v == 52);

    CHECK(tiff_strip_count(f, &strips) == tiff_ok && strips == 3);
    CHECK(tiff_strip_size(f, 0, &size) == tiff_ok && size == 157 * 52);
    CHECK(tiff_strip_size(f, 2, &size) == tiff_ok && size == 157 * 47);

    CHECK(tiff_strip_read(f, 0, out, sizeof out, &produced) == tiff_ok);
    CHECK(produced == 157 * 52);
    CHECK(tiff_strip_read(f, 2, out, sizeof out, &produced) == tiff_ok);
    CHECK(produced == 157 * 47);

    CHECK(tiff_close(f) == tiff_ok);
}

int main(int argc, char **argv) {
    printf("safe C API: %s\n", tiff_version_string());
    test_packbits_roundtrip();
    test_policy_unsorted();
    if (argc > 1)
        test_corpus(argv[1]);
    if (g_failures == 0) {
        printf("test_capi: all checks passed\n");
        return 0;
    }
    printf("test_capi: %d check(s) FAILED\n", g_failures);
    return 1;
}

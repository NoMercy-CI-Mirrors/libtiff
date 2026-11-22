/* tiff_core spike: safe C API (C99). Throwaway prototype of the API
 * conventions in doc/plan/libtiff-layered-api-migration.rst §9: status returns,
 * out-parameters, unsigned sizes, caller-provided buffers, per-handle
 * errors, no varargs, no globals, no macros for constants.
 *
 * Spike deviations from the plan, noted in FINDINGS.md: open options are
 * reduced to a policy enum, and open failures report through a message
 * buffer rather than a tiff_error_t object.
 */
#ifndef TIFF_CORE_H
#define TIFF_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tiff_file tiff_file_t;

/* Status codes: 0 is success; failures use banded codes (memory 1-99,
 * I/O 100s, format 200s, tag 300s, codec 400s, state 500s, parameter
 * 600s). The enumerators are the band anchors; tiff_error_code() gives
 * the exact code within the band. */
typedef enum tiff_status {
    tiff_ok = 0,
    tiff_err_memory = 1,
    tiff_err_io = 100,
    tiff_err_format = 200,
    tiff_err_tag = 300,
    tiff_err_codec = 400,
    tiff_err_state = 500,
    tiff_err_param = 600
} tiff_status_t;

typedef enum tiff_policy {
    tiff_policy_strict = 0,
    tiff_policy_legacy = 1
} tiff_policy_t;

typedef enum tiff_tag_id {
    tiff_tag_image_width = 256,
    tiff_tag_image_length = 257,
    tiff_tag_bits_per_sample = 258,
    tiff_tag_compression = 259,
    tiff_tag_photometric_interpretation = 262,
    tiff_tag_samples_per_pixel = 277,
    tiff_tag_rows_per_strip = 278
} tiff_tag_id_t;

/* Lifecycle. On failure *out is NULL and, when errmsg is non-NULL, a
 * NUL-terminated diagnostic is placed in errmsg[0..errmsg_cap-1]. */
tiff_status_t tiff_open_read(const char *path, tiff_policy_t policy,
                             tiff_file_t **out, char *errmsg,
                             size_t errmsg_cap);
tiff_status_t tiff_close(tiff_file_t *file);
void tiff_close_abandon(tiff_file_t *file);

/* Typed tag access (no varargs). */
tiff_status_t tiff_tag_get_u32(tiff_file_t *file, tiff_tag_id_t tag,
                               uint32_t *value);

/* Strip geometry and I/O: unsigned sizes, caller-provided buffers with
 * explicit capacity. */
tiff_status_t tiff_strip_count(tiff_file_t *file, uint32_t *count);
tiff_status_t tiff_strip_size(tiff_file_t *file, uint32_t strip,
                              uint64_t *size);
tiff_status_t tiff_strip_read(tiff_file_t *file, uint32_t strip, void *buf,
                              size_t capacity, size_t *produced);

/* Per-handle last error; strings are owned by the handle and valid until
 * the next call on it. */
int tiff_error_code(const tiff_file_t *file);
const char *tiff_error_message(const tiff_file_t *file);
const char *tiff_error_module(const tiff_file_t *file);

const char *tiff_version_string(void);

#ifdef __cplusplus
}
#endif

#endif /* TIFF_CORE_H */

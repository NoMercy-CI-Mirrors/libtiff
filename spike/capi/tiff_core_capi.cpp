// tiff_core spike: safe C API implementation — the primary driving
// adapter (plan §8.1). Translates tiff_* calls onto the application
// layer; no exceptions escape; every failure is stored as the handle's
// last error and mapped onto the banded status codes.
#include "tiff_core/tiff_core.h"

#include "core/file.hpp"

#include <cstring>
#include <memory>
#include <string>

struct tiff_file {
    std::unique_ptr<tiff::core::File> file;
    tiff::core::Error last_error{tiff::core::ErrorCode::BAD_STATE, "", ""};
    bool has_error = false;
};

namespace {

using tiff::core::Error;
using tiff::core::ErrorCode;

tiff_status_t band_of(ErrorCode code) {
    const int c = static_cast<int>(code);
    if (c < 100)
        return tiff_err_memory;
    if (c < 200)
        return tiff_err_io;
    if (c < 300)
        return tiff_err_format;
    if (c < 400)
        return tiff_err_tag;
    if (c < 500)
        return tiff_err_codec;
    if (c < 600)
        return tiff_err_state;
    return tiff_err_param;
}

tiff_status_t fail(tiff_file_t *f, Error e) {
    const tiff_status_t status = band_of(e.code);
    f->last_error = std::move(e);
    f->has_error = true;
    return status;
}

tiff_status_t ok(tiff_file_t *f) {
    f->has_error = false;
    return tiff_ok;
}

tiff::core::ReadPolicy to_policy(tiff_policy_t p) {
    return p == tiff_policy_legacy ? tiff::core::ReadPolicy::legacy()
                                   : tiff::core::ReadPolicy::strict();
}

} // namespace

extern "C" {

tiff_status_t tiff_open_read(const char *path, tiff_policy_t policy,
                             tiff_file_t **out, char *errmsg,
                             size_t errmsg_cap) {
    if (out == nullptr || path == nullptr)
        return tiff_err_param;
    *out = nullptr;

    auto report = [&](const Error &e) {
        if (errmsg != nullptr && errmsg_cap > 0) {
            const std::string text = e.module + ": " + e.message;
            std::strncpy(errmsg, text.c_str(), errmsg_cap - 1);
            errmsg[errmsg_cap - 1] = '\0';
        }
        return band_of(e.code);
    };

    auto io = tiff::core::FileIo::open(path);
    if (!io)
        return report(io.error());

    auto file = tiff::core::File::open_read(std::move(io.value()),
                                            to_policy(policy));
    if (!file)
        return report(file.error());

    auto *handle = new tiff_file;
    handle->file = std::move(file.value());
    *out = handle;
    return tiff_ok;
}

tiff_status_t tiff_close(tiff_file_t *file) {
    if (file == nullptr)
        return tiff_err_param;
    // The spike is read-only, so close cannot fail; the real API flushes
    // here and reports (plan §7.2).
    delete file;
    return tiff_ok;
}

void tiff_close_abandon(tiff_file_t *file) { delete file; }

tiff_status_t tiff_tag_get_u32(tiff_file_t *file, tiff_tag_id_t tag,
                               uint32_t *value) {
    if (file == nullptr || value == nullptr)
        return tiff_err_param;
    const tiff::core::Directory &d = file->file->directory();
    switch (tag) {
    case tiff_tag_image_width:
        *value = d.width;
        break;
    case tiff_tag_image_length:
        *value = d.length;
        break;
    case tiff_tag_bits_per_sample:
        *value = d.bits_per_sample;
        break;
    case tiff_tag_compression:
        *value = d.compression;
        break;
    case tiff_tag_photometric_interpretation:
        *value = d.photometric;
        break;
    case tiff_tag_samples_per_pixel:
        *value = d.samples_per_pixel;
        break;
    case tiff_tag_rows_per_strip:
        *value = d.rows_per_strip;
        break;
    default:
        return fail(file, Error{ErrorCode::BAD_PARAMETER, "capi",
                                "unknown tag id"});
    }
    return ok(file);
}

tiff_status_t tiff_strip_count(tiff_file_t *file, uint32_t *count) {
    if (file == nullptr || count == nullptr)
        return tiff_err_param;
    *count = file->file->strip_count();
    return ok(file);
}

tiff_status_t tiff_strip_size(tiff_file_t *file, uint32_t strip,
                              uint64_t *size) {
    if (file == nullptr || size == nullptr)
        return tiff_err_param;
    auto r = file->file->strip_size(strip);
    if (!r)
        return fail(file, r.take_error());
    *size = r.value();
    return ok(file);
}

tiff_status_t tiff_strip_read(tiff_file_t *file, uint32_t strip, void *buf,
                              size_t capacity, size_t *produced) {
    if (file == nullptr || buf == nullptr || produced == nullptr)
        return tiff_err_param;
    *produced = 0;
    auto r = file->file->read_strip(
        strip, tiff::core::Span<tiff::core::Byte>(
                   static_cast<tiff::core::Byte *>(buf), capacity));
    if (!r)
        return fail(file, r.take_error());
    *produced = r.value();
    return ok(file);
}

int tiff_error_code(const tiff_file_t *file) {
    return (file != nullptr && file->has_error)
               ? static_cast<int>(file->last_error.code)
               : 0;
}

const char *tiff_error_message(const tiff_file_t *file) {
    return (file != nullptr && file->has_error)
               ? file->last_error.message.c_str()
               : "";
}

const char *tiff_error_module(const tiff_file_t *file) {
    return (file != nullptr && file->has_error)
               ? file->last_error.module.c_str()
               : "";
}

const char *tiff_version_string(void) { return "tiff_core-spike 0.0.1"; }

} // extern "C"

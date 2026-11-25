/*
 * Copyright (c) 1988-1997 Sam Leffler
 * Copyright (c) 1991-1997 Silicon Graphics, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that (i) the above copyright notices and this permission notice appear in
 * all copies of the software and related documentation, and (ii) the names of
 * Sam Leffler and Silicon Graphics may not be used in any advertising or
 * publicity relating to the software without the specific, prior written
 * permission of Sam Leffler and Silicon Graphics.
 *
 * THE SOFTWARE IS PROVIDED "AS-IS" AND WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS, IMPLIED OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * IN NO EVENT SHALL SAM LEFFLER OR SILICON GRAPHICS BE LIABLE FOR
 * ANY SPECIAL, INCIDENTAL, INDIRECT OR CONSEQUENTIAL DAMAGES OF ANY KIND,
 * OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER OR NOT ADVISED OF THE POSSIBILITY OF DAMAGE, AND ON ANY THEORY OF
 * LIABILITY, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

/*
 * TIFF Library UNIX-specific Routines. These are should also work with the
 * Windows Common RunTime Library.
 */

#ifdef TIFF_DO_NOT_USE_NON_EXT_ALLOC_FUNCTIONS
#undef TIFF_DO_NOT_USE_NON_EXT_ALLOC_FUNCTIONS
#endif

/**
 * @file tif_unix.c
 * @brief Implement descriptor-based resource callbacks and pathname opening.
 */

#include "tif_config.h"

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#include <errno.h>

#include <stdarg.h>
#include <stdlib.h>
#include <sys/stat.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef HAVE_IO_H
#include <io.h>
#endif

#include "tiffiop.h"

/** @brief Limit one descriptor read/write request to 2147483647 bytes.
 * @ingroup libtiff_private_lifecycle
 */
#define TIFF_IO_MAX 2147483647U

/**
 * @brief Carry descriptor bits through the opaque client-handle representation.
 * @ingroup libtiff_private_lifecycle
 *
 * Used only by the descriptor backend; the union does not own a resource.
 */
typedef union fd_as_handle_union
{
    /** @brief Descriptor view of the stored bits. */
    int fd;
    /** @brief Opaque callback-context view of the stored bits. */
    thandle_t h;
} fd_as_handle_union_t;

/**
 * @brief Read bytes from the descriptor with read().
 * @ingroup libtiff_private_lifecycle
 *
 * Loops over chunks limited by TIFF_IO_MAX until the request completes,
 * read() returns zero (end of file), or read() fails. Does not retry EINTR.
 * A -1 result can follow earlier successful chunks, so the file position and
 * the destination bytes may already have changed. errno follows read(); a
 * size that does not survive conversion to size_t sets EINVAL. Does not
 * issue a library diagnostic or take buffer ownership.
 *
 * @param[in,out] fd Live file descriptor carried by fd_as_handle_union_t.
 * @param[out] buf Writable destination of at least size bytes.
 * @param[in] size Nonnegative byte request representable by tmsize_t and the
 * backend.
 * @return The number of bytes read, which is less than size at end of file,
 * or -1 if a read() call fails. A zero size also returns -1.
 */
static tmsize_t _tiffReadProc(thandle_t fd, void *buf, tmsize_t size)
{
    fd_as_handle_union_t fdh;
    const size_t bytes_total = (size_t)size;
    size_t bytes_read;
    tmsize_t count = -1;
    if ((tmsize_t)bytes_total != size)
    {
        errno = EINVAL;
        return (tmsize_t)-1;
    }
    fdh.h = fd;
    for (bytes_read = 0; bytes_read < bytes_total; bytes_read += (size_t)count)
    {
        char *buf_offset = (char *)buf + bytes_read;
        size_t io_size = (size_t)(bytes_total - bytes_read);
        if (io_size > TIFF_IO_MAX)
            io_size = TIFF_IO_MAX;
        /* Below is an obvious false positive of Coverity Scan */
        /* coverity[overflow_sink] */
        count = read(fdh.fd, buf_offset, (TIFFIOSize_t)io_size);
        if (count <= 0)
            break;
    }
    if (count < 0)
        return (tmsize_t)-1;
    /* Silence Coverity Scan warning about unsigned to signed underflow. */
    /* coverity[return_overflow:SUPPRESS] */
    return (tmsize_t)bytes_read;
}

/**
 * @brief Write bytes to the descriptor with write().
 * @ingroup libtiff_private_lifecycle
 *
 * Loops over chunks limited by TIFF_IO_MAX until the request completes,
 * write() returns zero, or write() fails. Does not retry EINTR. A -1 result
 * can follow earlier successful chunks, so the file position and contents
 * may already have changed. errno follows write(); a size that does not
 * survive conversion to size_t sets EINVAL. Does not issue a library
 * diagnostic or take buffer ownership.
 *
 * @param[in,out] fd Live file descriptor carried by fd_as_handle_union_t.
 * @param[in] buf Readable source of at least size bytes.
 * @param[in] size Nonnegative byte request representable by tmsize_t and the
 * backend.
 * @return The number of bytes written, which is less than size if write()
 * returned zero, or -1 if a write() call fails. A zero size also returns -1.
 */
static tmsize_t _tiffWriteProc(thandle_t fd, void *buf, tmsize_t size)
{
    fd_as_handle_union_t fdh;
    const size_t bytes_total = (size_t)size;
    size_t bytes_written;
    tmsize_t count = -1;
    if ((tmsize_t)bytes_total != size)
    {
        errno = EINVAL;
        return (tmsize_t)-1;
    }
    fdh.h = fd;
    for (bytes_written = 0; bytes_written < bytes_total;
         bytes_written += (size_t)count)
    {
        const char *buf_offset = (char *)buf + bytes_written;
        size_t io_size = (size_t)(bytes_total - bytes_written);
        if (io_size > TIFF_IO_MAX)
            io_size = TIFF_IO_MAX;
        /* Below is an obvious false positive of Coverity Scan */
        /* coverity[overflow_sink] */
        count = write(fdh.fd, buf_offset, (TIFFIOSize_t)io_size);
        if (count <= 0)
            break;
    }
    if (count < 0)
        return (tmsize_t)-1;
    /* Silence Coverity Scan warning about unsigned to signed underflow. */
    /* coverity[return_overflow:SUPPRESS] */
    return (tmsize_t)bytes_written;
    /* return ((tmsize_t) write(fdh.fd, buf, bytes_total)); */
}

/**
 * @brief Seek the platform resource and return its reported position.
 * @ingroup libtiff_private_lifecycle
 *
 * Converts to _TIFF_off_t and checks the unsigned round trip, then calls
 * _TIFF_lseek_f. A failed conversion sets EINVAL without seeking. Other
 * errors follow the platform call and errno. Does not emit library
 * diagnostics or transfer ownership.
 *
 * @param[in,out] fd Live file descriptor carried by fd_as_handle_union_t.
 * @param[in] off Byte offset converted to the platform signed offset type.
 * @param[in] whence Seek origin requested by the library.
 * @return The resulting byte position, or UINT64_MAX for a -1 result or a
 * failed offset conversion.
 */
static uint64_t _tiffSeekProc(thandle_t fd, uint64_t off, int whence)
{
    fd_as_handle_union_t fdh;
    _TIFF_off_t off_io = (_TIFF_off_t)off;
    if ((uint64_t)off_io != off)
    {
        errno = EINVAL;
        return (uint64_t)-1; /* this is really gross */
    }
    fdh.h = fd;
    return ((uint64_t)_TIFF_lseek_f(fdh.fd, off_io, whence));
}

/**
 * @brief Release the backend OS resource.
 * @ingroup libtiff_private_lifecycle
 *
 * Calls close() and forwards its status/errno without retry or library
 * diagnostics. Resource state after a failed close follows the platform
 * close contract; a successful close invalidates the descriptor.
 *
 * @param[in,out] fd Live file descriptor carried by fd_as_handle_union_t.
 * @return Zero on success and -1 on failure.
 */
static int _tiffCloseProc(thandle_t fd)
{
    fd_as_handle_union_t fdh;
    fdh.h = fd;
    return (close(fdh.fd));
}

/**
 * @brief Query the backend resource length in bytes.
 * @ingroup libtiff_private_lifecycle
 *
 * Uses _TIFF_fstat_f() without seeking. errno follows OS failure. Does not
 * transfer ownership or emit library diagnostics.
 *
 * @param[in] fd Live file descriptor carried by fd_as_handle_union_t.
 * @return The byte length, or zero on query failure; an empty file also has
 * length zero.
 */
static uint64_t _tiffSizeProc(thandle_t fd)
{
    _TIFF_stat_s sb;
    fd_as_handle_union_t fdh;
    fdh.h = fd;
    if (_TIFF_fstat_f(fdh.fd, &sb) < 0)
        return (0);
    else
        return ((uint64_t)sb.st_size);
}

#ifdef HAVE_MMAP
#include <sys/mman.h>

/**
 * @brief Create a read-only view of the entire backend resource.
 * @ingroup libtiff_private_lifecycle
 *
 * Queries the length with _tiffSizeProc() and calls mmap(PROT_READ,
 * MAP_SHARED). If mmap() fails, *pbase is left holding MAP_FAILED; earlier
 * failures leave it unchanged. A successful view must be released through
 * _tiffUnmapProc(), not an allocator. errno follows attempted platform
 * operations; no library diagnostic is emitted.
 *
 * @param[in] fd Live file descriptor carried by fd_as_handle_union_t.
 * @param[out] pbase Output view base on success; do not rely on it after
 * failure.
 * @param[out] psize Output view length in bytes on success; unchanged on
 * failure.
 * @return 1 for a mapped view; 0 for an mmap() failure, an empty file or
 * failed size query, or a length not representable as tmsize_t.
 */
static int _tiffMapProc(thandle_t fd, void **pbase, toff_t *psize)
{
    uint64_t size64 = _tiffSizeProc(fd);
    tmsize_t sizem = (tmsize_t)size64;
    if (size64 && (uint64_t)sizem == size64)
    {
        fd_as_handle_union_t fdh;
        fdh.h = fd;
        *pbase =
            (void *)mmap(0, (size_t)sizem, PROT_READ, MAP_SHARED, fdh.fd, 0);
        if (*pbase != (void *)-1)
        {
            *psize = (toff_t)sizem;
            return (1);
        }
    }
    return (0);
}

/**
 * @brief Release a read-only backend mapping.
 * @ingroup libtiff_private_lifecycle
 *
 * Calls munmap() and discards its result. A released view is invalid.
 * Failure has no callback status or library diagnostic and does not close
 * the file descriptor.
 *
 * @param[in] fd Unused client context.
 * @param[in] base Mapped-view base, or NULL during early failed-open cleanup.
 * @param[in] size Mapped length in bytes.
 */
static void _tiffUnmapProc(thandle_t fd, void *base, toff_t size)
{
    (void)fd;
    (void)munmap(base, (size_t)size);
}
#else  /* !HAVE_MMAP */
/**
 * @brief Report that memory mapping is unavailable (no HAVE_MMAP).
 * @ingroup libtiff_private_lifecycle
 *
 * Built only without HAVE_MMAP. Always fails without touching its outputs,
 * so the library falls back to ordinary reads.
 *
 * @param[in] fd Unused client context.
 * @param[in] pbase Unused; *pbase is not written.
 * @param[in] psize Unused; *psize is not written.
 * @return Always 0.
 */
static int _tiffMapProc(thandle_t fd, void **pbase, toff_t *psize)
{
    (void)fd;
    (void)pbase;
    (void)psize;
    return (0);
}

/**
 * @brief Do nothing, because mapping is unavailable (no HAVE_MMAP).
 * @ingroup libtiff_private_lifecycle
 *
 * Built only without HAVE_MMAP, where _tiffMapProc() never creates a view.
 *
 * @param[in] fd Unused client context.
 * @param[in] base Unused.
 * @param[in] size Unused.
 */
static void _tiffUnmapProc(thandle_t fd, void *base, toff_t size)
{
    (void)fd;
    (void)base;
    (void)size;
}
#endif /* !HAVE_MMAP */

/*
 * Open a TIFF file descriptor for read/writing.
 */
TIFF *TIFFFdOpen(int fd, const char *name, const char *mode)
{
    return TIFFFdOpenExt(fd, name, mode, NULL);
}

TIFF *TIFFFdOpenExt(int fd, const char *name, const char *mode,
                    TIFFOpenOptions *opts)
{
    TIFF *tif;

    fd_as_handle_union_t fdh;
    fdh.fd = fd;
    tif = TIFFClientOpenExt(name, mode, fdh.h, _tiffReadProc, _tiffWriteProc,
                            _tiffSeekProc, _tiffCloseProc, _tiffSizeProc,
                            _tiffMapProc, _tiffUnmapProc, opts);
    if (tif)
        tif->tif_fd = fd;
    return (tif);
}

/*
 * Open a TIFF file for read/writing.
 */
TIFF *TIFFOpen(const char *name, const char *mode)
{
    return TIFFOpenExt(name, mode, NULL);
}

TIFF *TIFFOpenExt(const char *name, const char *mode, TIFFOpenOptions *opts)
{
    static const char module[] = "TIFFOpen";
    int m, fd;
    TIFF *tif;

    m = _TIFFgetMode(opts, NULL, mode, module);
    if (m == -1)
        return ((TIFF *)0);

/* for cygwin and mingw */
#ifdef O_BINARY
    m |= O_BINARY;
#endif

    fd = open(name, m, 0666);
    if (fd < 0)
    {
        if (errno > 0 && strerror(errno) != NULL)
        {
            _TIFFErrorEarly(opts, NULL, module, "%s: %s", name,
                            strerror(errno));
        }
        else
        {
            _TIFFErrorEarly(opts, NULL, module, "%s: Cannot open", name);
        }
        return ((TIFF *)0);
    }

    tif = TIFFFdOpenExt((int)fd, name, mode, opts);
    if (!tif)
        close(fd);
    return tif;
}

#ifdef _WIN32
#include <windows.h>
/*
 * Open a TIFF file with a Unicode filename, for read/writing.
 */
TIFF *TIFFOpenW(const wchar_t *name, const char *mode)
{
    return TIFFOpenWExt(name, mode, NULL);
}
TIFF *TIFFOpenWExt(const wchar_t *name, const char *mode, TIFFOpenOptions *opts)
{
    static const char module[] = "TIFFOpenW";
    int m, fd;
    int mbsize;
    char *mbname;
    TIFF *tif;

    m = _TIFFgetMode(opts, NULL, mode, module);
    if (m == -1)
        return ((TIFF *)0);

/* for cygwin and mingw */
#ifdef O_BINARY
    m |= O_BINARY;
#endif

    fd = _wopen(name, m, 0666);
    if (fd < 0)
    {
        _TIFFErrorEarly(opts, NULL, module, "%ls: Cannot open", name);
        return ((TIFF *)0);
    }

    mbname = NULL;
    mbsize = WideCharToMultiByte(CP_ACP, 0, name, -1, NULL, 0, NULL, NULL);
    if (mbsize > 0)
    {
        mbname = _TIFFmalloc(mbsize);
        if (!mbname)
        {
            _TIFFErrorEarly(
                opts, NULL, module,
                "Can't allocate space for filename conversion buffer");
            return ((TIFF *)0);
        }

        WideCharToMultiByte(CP_ACP, 0, name, -1, mbname, mbsize, NULL, NULL);
    }

    tif = TIFFFdOpenExt((int)fd, (mbname != NULL) ? mbname : "<unknown>", mode,
                        opts);

    _TIFFfree(mbname);

    if (!tif)
        close(fd);
    return tif;
}
#endif

void *_TIFFmalloc(tmsize_t s)
{
    if (s == 0)
        return ((void *)NULL);

    return (malloc((size_t)s));
}

void *_TIFFcalloc(tmsize_t nmemb, tmsize_t siz)
{
    if (nmemb == 0 || siz == 0)
        return ((void *)NULL);

    return calloc((size_t)nmemb, (size_t)siz);
}

void _TIFFfree(void *p) { free(p); }

void *_TIFFrealloc(void *p, tmsize_t s) { return (realloc(p, (size_t)s)); }

void _TIFFmemset(void *p, int v, tmsize_t c) { memset(p, v, (size_t)c); }

void _TIFFmemcpy(void *d, const void *s, tmsize_t c)
{
    memcpy(d, s, (size_t)c);
}

int _TIFFmemcmp(const void *p1, const void *p2, tmsize_t c)
{
    return (memcmp(p1, p2, (size_t)c));
}

/**
 * @brief Print a default warning diagnostic to stderr.
 * @ingroup libtiff_private_diagnostics
 *
 * Writes to stderr using stdio. Prints the module prefix when non-NULL, then
 * "Warning, ", then the formatted message and a final period/newline. Stdio
 * failure is not checked and no status is returned. Does not own
 * strings/arguments or terminate the process; output/errno effects follow
 * stdio.
 *
 * @param[in] module Optional borrowed module name printed as a prefix.
 * @param[in] fmt Non-NULL printf-style format string.
 * @param[in,out] ap Borrowed matching arguments consumed by vfprintf().
 */
static void TIFF_ATTRIBUTE((__format__(__printf__, 2, 0)))
    unixWarningHandler(const char *module, const char *fmt, va_list ap)
{
    if (module != NULL)
        fprintf(stderr, "%s: ", module);
    fprintf(stderr, "Warning, ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, ".\n");
}
TIFFErrorHandler _TIFFwarningHandler = unixWarningHandler;

/**
 * @brief Print a default error diagnostic to stderr.
 * @ingroup libtiff_private_diagnostics
 *
 * Writes to stderr using stdio. Prints the module prefix when non-NULL, then
 * the formatted message and a final period/newline. Stdio failure is not
 * checked and no status is returned. Does not own strings/arguments or
 * terminate the process; output/errno effects follow stdio.
 *
 * @param[in] module Optional borrowed module name printed as a prefix.
 * @param[in] fmt Non-NULL printf-style format string.
 * @param[in,out] ap Borrowed matching arguments consumed by vfprintf().
 */
static void TIFF_ATTRIBUTE((__format__(__printf__, 2, 0)))
    unixErrorHandler(const char *module, const char *fmt, va_list ap)
{
    if (module != NULL)
        fprintf(stderr, "%s: ", module);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, ".\n");
}
TIFFErrorHandler _TIFFerrorHandler = unixErrorHandler;

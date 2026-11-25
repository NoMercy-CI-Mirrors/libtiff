/*
 * Copyright (c) 1988-1997 Sam Leffler
 * Copyright (c) 1991-1997 Silicon Graphics, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that (i) the above copyright notices and this permission notice appear in
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
 * TIFF Library Win32-specific Routines.  Adapted from tif_unix.c 4/5/95 by
 * Scott Wagner (wagner@itek.com), Itek Graphix, Rochester, NY USA
 */

#ifdef TIFF_DO_NOT_USE_NON_EXT_ALLOC_FUNCTIONS
#undef TIFF_DO_NOT_USE_NON_EXT_ALLOC_FUNCTIONS
#endif

/**
 * @file tif_win32.c
 * @brief Implement native Windows resource callbacks and pathname opening.
 */

#include "tiffiop.h"
#include <stdlib.h>

#include <windows.h>

/*
  CreateFileA/CreateFileW return type 'HANDLE' while TIFFFdOpen() takes 'int',
  which is formally incompatible and can even seemingly be of different size:
  HANDLE is 64 bit under Win64, while int is still 32 bits there.

  However, only the lower 32 bits of a HANDLE are significant under Win64 as,
  for interoperability reasons, they must have the same values in 32- and
  64-bit programs running on the same system, see

  https://docs.microsoft.com/en-us/windows/win32/winprog64/interprocess-communication

  Because of this, it is safe to define the following trivial functions for
  casting between ints and HANDLEs, which are only really needed to avoid
  compiler warnings (and, perhaps, to make the code slightly more clear).
  Note that using the intermediate cast to "intptr_t" is crucial for warning
  avoidance, as this integer type has the same size as HANDLE in all builds.
*/

/**
 * @brief Convert the native Windows integer handle representation to thandle_t.
 * @ingroup libtiff_private_lifecycle
 *
 * Performs no OS validation, allocation, close, or diagnostic. Does not
 * transfer ownership.
 *
 * @param[in] ifd Integer representation of a Windows HANDLE, not a CRT
 * descriptor.
 * @return The handle value cast through intptr_t.
 */
static inline thandle_t thandle_from_int(int ifd)
{
    return (thandle_t)(intptr_t)ifd;
}

/**
 * @brief Convert a native Windows handle to the backend integer representation.
 * @ingroup libtiff_private_lifecycle
 *
 * Performs no OS validation, allocation, close, or diagnostic. Does not
 * transfer ownership; this is the backend HANDLE convention, not a CRT-
 * descriptor conversion.
 *
 * @param[in] fd Native Windows HANDLE represented as thandle_t.
 * @return The integer representation produced through intptr_t.
 */
static inline int thandle_to_int(thandle_t fd) { return (int)(intptr_t)fd; }

/**
 * @brief Read bytes from the Windows HANDLE with ReadFile().
 * @ingroup libtiff_private_lifecycle
 *
 * Reads in chunks no larger than 0x80000000 bytes and stops at a short
 * read. Zero length returns zero. API errors are exposed through the
 * Windows last-error state; there is no library diagnostic here. The file
 * position and destination bytes may already have changed before failure.
 * Buffer ownership is unchanged.
 *
 * @param[in,out] fd Live native Windows HANDLE encoded as thandle_t.
 * @param[out] buf Writable destination of at least size bytes.
 * @param[in] size Nonnegative byte request representable by tmsize_t and the
 * backend.
 * @return The number of bytes read (less than size at end of file), or zero
 * on any ReadFile() failure even if earlier chunks transferred data.
 */
static tmsize_t _tiffReadProc(thandle_t fd, void *buf, tmsize_t size)
{
    /* tmsize_t is 64bit on 64bit systems, but the WinAPI ReadFile takes
     * 32bit sizes, so we loop through the data in suitable 32bit sized
     * chunks */
    uint8_t *ma;
    uint64_t mb;
    DWORD n;
    DWORD o;
    tmsize_t p;
    ma = (uint8_t *)buf;
    mb = (uint64_t)size;
    p = 0;
    while (mb > 0)
    {
        n = 0x80000000UL;
        if ((uint64_t)n > mb)
            n = (DWORD)mb;
        if (!ReadFile(fd, (LPVOID)ma, n, &o, NULL))
            return (0);
        ma += o;
        mb -= o;
        p += o;
        if (o != n)
            break;
    }
    return (p);
}

/**
 * @brief Write bytes to the Windows HANDLE with WriteFile().
 * @ingroup libtiff_private_lifecycle
 *
 * Writes in chunks no larger than 0x80000000 bytes and stops at a short
 * write. Zero length returns zero. API errors are exposed through the
 * Windows last-error state; there is no library diagnostic here. The file
 * position and contents may already have changed before failure. Buffer
 * ownership is unchanged.
 *
 * @param[in,out] fd Live native Windows HANDLE encoded as thandle_t.
 * @param[in] buf Readable source of at least size bytes.
 * @param[in] size Nonnegative byte request representable by tmsize_t and the
 * backend.
 * @return The number of bytes written (less than size after a short write),
 * or zero on any WriteFile() failure even if earlier chunks transferred data.
 */
static tmsize_t _tiffWriteProc(thandle_t fd, void *buf, tmsize_t size)
{
    /* tmsize_t is 64bit on 64bit systems, but the WinAPI WriteFile takes
     * 32bit sizes, so we loop through the data in suitable 32bit sized
     * chunks */
    uint8_t *ma;
    uint64_t mb;
    DWORD n;
    DWORD o;
    tmsize_t p;
    ma = (uint8_t *)buf;
    mb = (uint64_t)size;
    p = 0;
    while (mb > 0)
    {
        n = 0x80000000UL;
        if ((uint64_t)n > mb)
            n = (DWORD)mb;
        if (!WriteFile(fd, (LPVOID)ma, n, &o, NULL))
            return (0);
        ma += o;
        mb -= o;
        p += o;
        if (o != n)
            break;
    }
    return (p);
}

/**
 * @brief Seek the platform resource and return its reported position.
 * @ingroup libtiff_private_lifecycle
 *
 * Uses SetFilePointer; unsupported origins fall back to FILE_BEGIN. Failure
 * is detected through GetLastError() and replaced with zero. Position
 * preservation on failure is not guaranteed, and no library diagnostic is
 * emitted.
 *
 * @param[in,out] fd Live native Windows HANDLE encoded as thandle_t.
 * @param[in] off Byte offset, reinterpreted as a signed LONGLONG.
 * @param[in] whence SEEK_SET, SEEK_CUR or SEEK_END; any other value is
 * treated as SEEK_SET.
 * @return The Windows position, or zero on SetFilePointer failure; zero is
 * also a valid position.
 */
static uint64_t _tiffSeekProc(thandle_t fd, uint64_t off, int whence)
{
    LARGE_INTEGER offli;
    DWORD dwMoveMethod;
    offli.QuadPart = (LONGLONG)off;
    switch (whence)
    {
        case SEEK_SET:
            dwMoveMethod = FILE_BEGIN;
            break;
        case SEEK_CUR:
            dwMoveMethod = FILE_CURRENT;
            break;
        case SEEK_END:
            dwMoveMethod = FILE_END;
            break;
        default:
            dwMoveMethod = FILE_BEGIN;
            break;
    }
    offli.LowPart =
        SetFilePointer(fd, (LONG)offli.LowPart, &offli.HighPart, dwMoveMethod);
    if ((offli.LowPart == INVALID_SET_FILE_POINTER) &&
        (GetLastError() != NO_ERROR))
        offli.QuadPart = 0;
    return ((uint64_t)offli.QuadPart);
}

/**
 * @brief Release the backend OS resource.
 * @ingroup libtiff_private_lifecycle
 *
 * Calls CloseHandle(). Its result is translated to the callback convention;
 * GetLastError() describes OS failure. No library diagnostic is added. The
 * handle is invalid after successful close.
 *
 * @param[in,out] fd Live native Windows HANDLE encoded as thandle_t.
 * @return Zero on success and -1 on failure.
 */
static int _tiffCloseProc(thandle_t fd) { return (CloseHandle(fd) ? 0 : -1); }

/**
 * @brief Query the backend resource length in bytes.
 * @ingroup libtiff_private_lifecycle
 *
 * Uses GetFileSizeEx() without seeking. GetLastError() follows OS failure.
 * Does not transfer ownership or emit library diagnostics.
 *
 * @param[in] fd Live native Windows HANDLE encoded as thandle_t.
 * @return The byte length, or zero on query failure; an empty file also has
 * length zero.
 */
static uint64_t _tiffSizeProc(thandle_t fd)
{
    LARGE_INTEGER m;
    if (GetFileSizeEx(fd, &m))
        return ((uint64_t)m.QuadPart);
    else
        return (0);
}

/*
 * From "Hermann Josef Hill" <lhill@rhein-zeitung.de>:
 *
 * Windows uses both a handle and a pointer for file mapping,
 * but according to the SDK documentation and Richter's book
 * "Advanced Windows Programming" it is safe to free the handle
 * after obtaining the file mapping pointer
 *
 * This removes a nasty OS dependency and cures a problem
 * with Visual C++ 5.0
 */
/**
 * @brief Create a read-only view of the entire backend resource.
 * @ingroup libtiff_private_lifecycle
 *
 * Queries file size and maps the whole resource. The view must be released
 * through _tiffUnmapProc(), not an allocator. The temporary Windows file-
 * mapping HANDLE is closed after MapViewOfFile(), while the view remains
 * valid. API failure may set *pbase to NULL; earlier failures leave it
 * unchanged. No library diagnostic is emitted.
 *
 * @param[in] fd Live native Windows HANDLE encoded as thandle_t.
 * @param[out] pbase Output view base on success; do not rely on it after
 * failure.
 * @param[out] psize Output view length in bytes on success; unchanged on
 * failure.
 * @return 1 for a mapped view; 0 for a CreateFileMapping() or MapViewOfFile()
 * failure, an empty file or failed size query, or a length not
 * representable as tmsize_t.
 */
static int _tiffMapProc(thandle_t fd, void **pbase, toff_t *psize)
{
    uint64_t size;
    tmsize_t sizem;
    HANDLE hMapFile;

    size = _tiffSizeProc(fd);
    sizem = (tmsize_t)size;
    if (!size || (uint64_t)sizem != size)
        return (0);

    /* By passing in 0 for the maximum file size, it specifies that we
       create a file mapping object for the full file size. */
    hMapFile = CreateFileMapping(fd, NULL, PAGE_READONLY, 0, 0, NULL);
    if (hMapFile == NULL)
        return (0);
    *pbase = MapViewOfFile(hMapFile, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(hMapFile);
    if (*pbase == NULL)
        return (0);
    *psize = size;
    return (1);
}

/**
 * @brief Release a read-only backend mapping.
 * @ingroup libtiff_private_lifecycle
 *
 * Calls UnmapViewOfFile() and discards its result. The view must no longer
 * be used after successful unmapping; failures have no callback status or
 * library diagnostic. Does not close the file HANDLE.
 *
 * @param[in] fd Unused client context.
 * @param[in] base Mapped-view base, or NULL during early failed-open cleanup.
 * @param[in] size Mapped length in bytes; unused by the Windows API.
 */
static void _tiffUnmapProc(thandle_t fd, void *base, toff_t size)
{
    (void)fd;
    (void)size;
    UnmapViewOfFile(base);
}

/*
 * Open a TIFF file descriptor for read/writing.
 * Note that TIFFFdOpen and TIFFOpen recognise the character 'u' in the mode
 * string, which forces the file to be opened unmapped.
 */
TIFF *TIFFFdOpen(int ifd, const char *name, const char *mode)
{
    return TIFFFdOpenExt(ifd, name, mode, NULL);
}

TIFF *TIFFFdOpenExt(int ifd, const char *name, const char *mode,
                    TIFFOpenOptions *opts)
{
    TIFF *tif;
    int fSuppressMap;
    int m;

    fSuppressMap = 0;
    for (m = 0; mode[m] != 0; m++)
    {
        if (mode[m] == 'u')
        {
            fSuppressMap = 1;
            break;
        }
    }

    tif = TIFFClientOpenExt(
        name, mode, thandle_from_int(ifd), _tiffReadProc, _tiffWriteProc,
        _tiffSeekProc, _tiffCloseProc, _tiffSizeProc,
        fSuppressMap ? _tiffDummyMapProc : _tiffMapProc,
        fSuppressMap ? _tiffDummyUnmapProc : _tiffUnmapProc, opts);
    if (tif)
        tif->tif_fd = ifd;
    return (tif);
}

#ifndef _WIN32_WCE

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
    thandle_t fd;
    int m;
    DWORD dwMode;
    TIFF *tif;

    m = _TIFFgetMode(opts, NULL, mode, module);

    switch (m)
    {
        case O_RDONLY:
            dwMode = OPEN_EXISTING;
            break;
        case O_RDWR:
            dwMode = OPEN_EXISTING;
            break;
        case O_RDWR | O_CREAT:
            dwMode = OPEN_ALWAYS;
            break;
        case O_RDWR | O_TRUNC:
            dwMode = CREATE_ALWAYS;
            break;
        case O_RDWR | O_CREAT | O_TRUNC:
            dwMode = CREATE_ALWAYS;
            break;
        default:
            return ((TIFF *)0);
    }

    fd = (thandle_t)CreateFileA(
        name, (m == O_RDONLY) ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE),
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, dwMode,
        (m == O_RDONLY) ? FILE_ATTRIBUTE_READONLY : FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (fd == INVALID_HANDLE_VALUE)
    {
        _TIFFErrorEarly(opts, NULL, module, "%s: Cannot open", name);
        return ((TIFF *)0);
    }

    tif = TIFFFdOpenExt(thandle_to_int(fd), name, mode, opts);
    if (!tif)
        CloseHandle(fd);
    return tif;
}

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
    thandle_t fd;
    int m;
    DWORD dwMode;
    int mbsize;
    char *mbname;
    TIFF *tif;

    m = _TIFFgetMode(opts, NULL, mode, module);

    switch (m)
    {
        case O_RDONLY:
            dwMode = OPEN_EXISTING;
            break;
        case O_RDWR:
            dwMode = OPEN_EXISTING;
            break;
        case O_RDWR | O_CREAT:
            dwMode = OPEN_ALWAYS;
            break;
        case O_RDWR | O_TRUNC:
            dwMode = CREATE_ALWAYS;
            break;
        case O_RDWR | O_CREAT | O_TRUNC:
            dwMode = CREATE_ALWAYS;
            break;
        default:
            return ((TIFF *)0);
    }

    fd = (thandle_t)CreateFileW(
        name, (m == O_RDONLY) ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE),
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, dwMode,
        (m == O_RDONLY) ? FILE_ATTRIBUTE_READONLY : FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (fd == INVALID_HANDLE_VALUE)
    {
        _TIFFErrorEarly(opts, NULL, module, "%ls: Cannot open", name);
        return ((TIFF *)0);
    }

    mbname = NULL;
    mbsize = WideCharToMultiByte(CP_ACP, 0, name, -1, NULL, 0, NULL, NULL);
    if (mbsize > 0)
    {
        mbname = (char *)_TIFFmalloc(mbsize);
        if (!mbname)
        {
            _TIFFErrorEarly(
                opts, NULL, module,
                "Can't allocate space for filename conversion buffer");
            return ((TIFF *)0);
        }

        WideCharToMultiByte(CP_ACP, 0, name, -1, mbname, mbsize, NULL, NULL);
    }

    tif = TIFFFdOpenExt(thandle_to_int(fd),
                        (mbname != NULL) ? mbname : "<unknown>", mode, opts);
    if (!tif)
        CloseHandle(fd);

    _TIFFfree(mbname);

    return tif;
}

#endif /* ndef _WIN32_WCE */

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

#ifndef _WIN32_WCE

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
static void Win32WarningHandler(const char *module, const char *fmt, va_list ap)
{
    if (module != NULL)
        fprintf(stderr, "%s: ", module);
    fprintf(stderr, "Warning, ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, ".\n");
}
TIFFErrorHandler _TIFFwarningHandler = Win32WarningHandler;

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
static void Win32ErrorHandler(const char *module, const char *fmt, va_list ap)
{
    if (module != NULL)
        fprintf(stderr, "%s: ", module);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, ".\n");
}
TIFFErrorHandler _TIFFerrorHandler = Win32ErrorHandler;

#endif /* ndef _WIN32_WCE */

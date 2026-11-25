/*
 * Copyright (c) 1990-1997 Sam Leffler
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

/**
 * @file tif_fax3.h
 * @brief Define fax run callbacks generated transitions and decoder state
 * macros.
 */

#ifndef _FAX3_
#define _FAX3_
/*
 * TIFF Library.
 *
 * CCITT Group 3 (T.4) and Group 4 (T.6) Decompression Support.
 *
 * Decoder support is derived, with permission, from the code
 * in Frank Cringle's viewfax program;
 *      Copyright (C) 1990, 1995  Frank D. Cringle.
 */
#include "tiff.h"

/*
 * To override the default routine used to image decoded
 * spans one can use the pseudo tag TIFFTAG_FAXFILLFUNC.
 * The routine must have the type signature given below;
 * for example:
 *
 * fillruns(unsigned char* buf, uint32_t* runs, uint32_t* erun, uint32_t lastx)
 *
 * where buf is place to set the bits, runs is the array of b&w run
 * lengths (white then black), erun is the last run in the array, and
 * lastx is the width of the row in pixels.  Fill routines can assume
 * the run array has room for at least lastx runs and can overwrite
 * data in the run array as needed (e.g. to append zero runs to bring
 * the count up to a nice multiple).
 */
/**
 * @brief Render decoded fax runs through an installed callback.
 * @ingroup libtiff_private_fax
 *
 * Called synchronously per decoded row. Borrows output/runs only for the
 * call; never retain them or close/redecode the active handle. Default
 * callback writes MSB-first bilevel bytes. Custom callbacks may render
 * elsewhere and can explicitly accept NULL buf. Keep code/context live
 * while installed or in flight. No status channel or ownership transfer;
 * error reporting is callback-defined.
 *
 * @param[out] buf Borrowed decoded row destination; NULL only for a
 * callback explicitly supporting output elsewhere.
 * @param[in,out] runs Borrowed alternating white/black run storage with at
 * least the decoder-required row capacity; callback may modify it.
 * @param[in,out] erun Borrowed end cursor with room for callback-specific
 * sentinel padding.
 * @param[in] lastx Row width in pixels.
 */
typedef void (*TIFFFaxFillFunc)(unsigned char *buf, uint32_t *runs,
                                uint32_t *erun, uint32_t lastx);

/*
 * The default run filler; made external for other decoders.
 */
#if defined(__cplusplus)
extern "C"
{
#endif
    /**
     * @brief Render alternating white/black fax runs into an MSB-first row.
     * @ingroup libtiff_private_fax
     *
     * Requires nonoverlapping valid row/run storage and representable run
     * arithmetic. Appends a zero run for odd count, clips overlong runs in
     * place, clears white bits and sets black bits using aligned bulk stores.
     * Asserts final row width; does not validate every allocation bound. No
     * allocation, I/O, diagnostics, retained pointers or ownership transfer.
     *
     * @param[out] buf Borrowed writable ceil(lastx/8)-byte row; must be
     * non-NULL for the default filler.
     * @param[in,out] runs Borrowed writable white/black alternating run
     * lengths, with room at erun for a possible zero sentinel; at least the
     * decoder-required capacity.
     * @param[in,out] erun End cursor within runs storage; an odd run count
     * appends a zero here.
     * @param[in] lastx Row width in pixels within signed loop/count limits;
     * runs must describe that row.
     */
    extern void _TIFFFax3fillruns(unsigned char *buf, uint32_t *runs,
                                  uint32_t *erun, uint32_t lastx);
#if defined(__cplusplus)
}
#endif

/* finite state machine codes */
#define S_Null 0
#define S_Pass 1
#define S_Horiz 2
#define S_V0 3
#define S_VR 4
#define S_VL 5
#define S_Ext 6
#define S_TermW 7
#define S_TermB 8
#define S_MakeUpW 9
#define S_MakeUpB 10
#define S_MakeUp 11
#define S_EOL 12

/* WARNING: do not change the layout of this structure as the HylaFAX software
 */
/* really depends on it. See http://bugzilla.maptools.org/show_bug.cgi?id=2636
 */
/**
 * @brief Describe one generated fax decoding state transition.
 * @ingroup libtiff_private_fax
 *
 * Entries live in the immutable generated tables TIFFFaxMainTable,
 * TIFFFaxWhiteTable and TIFFFaxBlackTable. The layout is fixed because
 * HylaFAX depends on it (bug 2636).
 */
typedef struct
{
    /** @brief Finite-state operation code S_Null through S_EOL. */
    unsigned char State;
    /** @brief Number of input code bits consumed by this entry. */
    unsigned char Width;
    /**
     * @brief Run length or vertical offset magnitude interpreted by State;
     * fixed ABI layout.
     */
    uint32_t Param;
} TIFFFaxTabEnt;

/**
 * @brief Decode seven-bit two-dimensional (Group3 2D and Group4) mode
 * prefixes.
 * @ingroup libtiff_private_fax
 *
 * Generated immutable library-lifetime table; canonical declaration covers
 * tif_fax3sm.c storage. Do not modify or free it.
 */
extern const TIFFFaxTabEnt TIFFFaxMainTable[];
/**
 * @brief Decode twelve-bit white-run prefixes.
 * @ingroup libtiff_private_fax
 *
 * Generated immutable library-lifetime table; canonical declaration covers
 * tif_fax3sm.c storage. Do not modify or free it.
 */
extern const TIFFFaxTabEnt TIFFFaxWhiteTable[];
/**
 * @brief Decode thirteen-bit black-run prefixes.
 * @ingroup libtiff_private_fax
 *
 * Generated immutable library-lifetime table; canonical declaration covers
 * tif_fax3sm.c storage. Do not modify or free it.
 */
extern const TIFFFaxTabEnt TIFFFaxBlackTable[];

/*
 * The following macros define the majority of the G3/G4 decoder
 * algorithm using the state tables defined elsewhere.  To build
 * a decoder you need some setup code and some glue code. Note
 * that you may also need/want to change the way the NeedBits*
 * macros get input data if, for example, you know the data to be
 * decoded is properly aligned and oriented (doing so before running
 * the decoder can be a big performance win).
 *
 * Consult the decoder in the TIFF library for an idea of what you
 * need to define and setup to make use of these definitions.
 *
 * NB: to enable a debugging version of these macros define FAX3_DEBUG
 *     before including this file.  Trace output goes to stdout.
 */

#ifndef EndOfData
#define EndOfData() (cp >= ep)
#endif
/*
 * Need <=8 or <=16 bits of input data.  Unlike viewfax we
 * cannot use/assume a word-aligned, properly bit swizzled
 * input data set because data may come from an arbitrarily
 * aligned, read-only source such as a memory-mapped file.
 * Note also that the viewfax decoder does not check for
 * running off the end of the input data buffer.  This is
 * possible for G3-encoded data because it prescans the input
 * data to count EOL markers, but can cause problems for G4
 * data.  In any event, we don't prescan and must watch for
 * running out of data since we can't permit the library to
 * scan past the end of the input data buffer.
 *
 * Finally, note that we must handle remaindered data at the end
 * of a strip specially.  The coder asks for a fixed number of
 * bits when scanning for the next code.  This may be more bits
 * than are actually present in the data stream.  If we appear
 * to run out of data but still have some number of valid bits
 * remaining then we makeup the requested amount with zeros and
 * return successfully.  If the returned data is incorrect then
 * we should be called again and get a premature EOF error;
 * otherwise we should get the right answer.
 */
#ifndef NeedBits8
#define NeedBits8(n, eoflab)                                                   \
    do                                                                         \
    {                                                                          \
        if (BitsAvail < (n))                                                   \
        {                                                                      \
            if (EndOfData())                                                   \
            {                                                                  \
                if (BitsAvail == 0) /* no valid bits */                        \
                    goto eoflab;                                               \
                BitsAvail = (n); /* pad with zeros */                          \
            }                                                                  \
            else                                                               \
            {                                                                  \
                BitAcc |= ((uint32_t)bitmap[*cp++]) << BitsAvail;              \
                BitsAvail += 8;                                                \
            }                                                                  \
        }                                                                      \
    } while (0)
#endif
#ifndef NeedBits16
#define NeedBits16(n, eoflab)                                                  \
    do                                                                         \
    {                                                                          \
        if (BitsAvail < (n))                                                   \
        {                                                                      \
            if (EndOfData())                                                   \
            {                                                                  \
                if (BitsAvail == 0) /* no valid bits */                        \
                    goto eoflab;                                               \
                BitsAvail = (n); /* pad with zeros */                          \
            }                                                                  \
            else                                                               \
            {                                                                  \
                BitAcc |= ((uint32_t)bitmap[*cp++]) << BitsAvail;              \
                if ((BitsAvail += 8) < (n))                                    \
                {                                                              \
                    if (EndOfData())                                           \
                    {                                                          \
                        /* NB: we know BitsAvail is non-zero here */           \
                        BitsAvail = (n); /* pad with zeros */                  \
                    }                                                          \
                    else                                                       \
                    {                                                          \
                        BitAcc |= ((uint32_t)bitmap[*cp++]) << BitsAvail;      \
                        BitsAvail += 8;                                        \
                    }                                                          \
                }                                                              \
            }                                                                  \
        }                                                                      \
    } while (0)
#endif
#define GetBits(n) (BitAcc & ((1 << (n)) - 1))
#define ClrBits(n)                                                             \
    do                                                                         \
    {                                                                          \
        BitsAvail -= (n);                                                      \
        BitAcc >>= (n);                                                        \
    } while (0)

#ifdef FAX3_DEBUG
/**
 * @brief Name decoder states for debug trace output.
 * @ingroup libtiff_private_fax
 *
 * Static library-lifetime strings; trace code reads them without ownership
 * transfer.
 */
static const char *StateNames[] = {
    "Null   ", "Pass   ", "Horiz  ", "V0     ", "VR     ", "VL     ", "Ext    ",
    "TermW  ", "TermB  ", "MakeUpW", "MakeUpB", "MakeUp ", "EOL    ",
};
/**
 * @brief Print one accumulator bit to stdout.
 * @ingroup libtiff_private_fax
 *
 * Defined only with FAX3_DEBUG. Writes '1' or '0' for bit t of BitAcc, both
 * locals of the enclosing LOOKUP8()/LOOKUP16() expansion.
 */

#define DEBUG_SHOW putchar(BitAcc & (1 << t) ? '1' : '0')
#define LOOKUP8(wid, tab, eoflab)                                              \
    do                                                                         \
    {                                                                          \
        int t;                                                                 \
        NeedBits8(wid, eoflab);                                                \
        TabEnt = tab + GetBits(wid);                                           \
        printf("%08lX/%d: %s%5d\t", (long)BitAcc, BitsAvail,                   \
               StateNames[TabEnt->State], TabEnt->Param);                      \
        for (t = 0; t < TabEnt->Width; t++)                                    \
            DEBUG_SHOW;                                                        \
        putchar('\n');                                                         \
        fflush(stdout);                                                        \
        ClrBits(TabEnt->Width);                                                \
    } while (0)
#define LOOKUP16(wid, tab, eoflab)                                             \
    do                                                                         \
    {                                                                          \
        int t;                                                                 \
        NeedBits16(wid, eoflab);                                               \
        TabEnt = tab + GetBits(wid);                                           \
        printf("%08lX/%d: %s%5d\t", (long)BitAcc, BitsAvail,                   \
               StateNames[TabEnt->State], TabEnt->Param);                      \
        for (t = 0; t < TabEnt->Width; t++)                                    \
            DEBUG_SHOW;                                                        \
        putchar('\n');                                                         \
        fflush(stdout);                                                        \
        ClrBits(TabEnt->Width);                                                \
    } while (0)

#define SETVALUE(x)                                                            \
    do                                                                         \
    {                                                                          \
        *pa++ = RunLength + (x);                                               \
        printf("SETVALUE: %d\t%d\n", RunLength + (x), a0);                     \
        a0 += x;                                                               \
        RunLength = 0;                                                         \
    } while (0)
#else
#define LOOKUP8(wid, tab, eoflab)                                              \
    do                                                                         \
    {                                                                          \
        NeedBits8(wid, eoflab);                                                \
        TabEnt = tab + GetBits(wid);                                           \
        ClrBits(TabEnt->Width);                                                \
    } while (0)
#define LOOKUP16(wid, tab, eoflab)                                             \
    do                                                                         \
    {                                                                          \
        NeedBits16(wid, eoflab);                                               \
        TabEnt = tab + GetBits(wid);                                           \
        ClrBits(TabEnt->Width);                                                \
    } while (0)

/*
 * Append a run to the run length array for the
 * current row and reset decoding state.
 */
#define SETVALUE(x)                                                            \
    do                                                                         \
    {                                                                          \
        if (pa >= thisrun + sp->nruns)                                         \
        {                                                                      \
            TIFFErrorExtR(tif, module, "Buffer overflow at line %d of %s %u",  \
                          sp->line, isTiled(tif) ? "tile" : "strip",           \
                          isTiled(tif) ? tif->tif_dir.td_curtile               \
                                       : tif->tif_dir.td_curstrip);            \
            return (-1);                                                       \
        }                                                                      \
        *pa++ = (uint32_t)((uint32_t)RunLength + (uint32_t)(x));               \
        a0 += (int)(uint32_t)(x);                                              \
        RunLength = 0;                                                         \
    } while (0)
#endif

/*
 * Synchronize input decoding at the start of each
 * row by scanning for an EOL (if appropriate) and
 * skipping any trash data that might be present
 * after a decoding error.  Note that the decoding
 * done elsewhere that recognizes an EOL only consumes
 * 11 consecutive zero bits.  This means that if EOLcnt
 * is non-zero then we still need to scan for the final flag
 * bit that is part of the EOL code.
 */
#define SYNC_EOL(eoflab, retrywithouteol)                                      \
    do                                                                         \
    {                                                                          \
        if (!(sp->b.mode & FAXMODE_NOEOL)) /* skip EOL, if not present */      \
        {                                                                      \
            if (EOLcnt == 0)                                                   \
            {                                                                  \
                for (;;)                                                       \
                {                                                              \
                    NeedBits16(11, eoflab);                                    \
                    if (GetBits(11) == 0)                                      \
                        break; /* EOL found */                                 \
                    ClrBits(1);                                                \
                }                                                              \
            }                                                                  \
            /* Now move after EOL or detect missing EOL. */                    \
            for (;;)                                                           \
            {                                                                  \
                NeedBits8(8, noEOLFound);                                      \
                if (GetBits(8))                                                \
                    break;                                                     \
                ClrBits(8);                                                    \
            }                                                                  \
            while (GetBits(1) == 0)                                            \
                ClrBits(1);                                                    \
            ClrBits(1); /* EOL bit */                                          \
            EOLcnt = 0; /* reset EOL counter/flag */                           \
            break;      /* existing EOL skipped, leave macro */                \
        noEOLFound:                                                            \
            sp->b.mode |= FAXMODE_NOEOL;                                       \
            tryG3WithoutEOL(a0);                                               \
            goto retrywithouteol;                                              \
        }                                                                      \
    } while (0)

/*
 * Cleanup the array of runs after decoding a row.
 * We adjust final runs to insure the user buffer is not
 * overwritten and/or undecoded area is white filled.
 */
#define CLEANUP_RUNS()                                                         \
    do                                                                         \
    {                                                                          \
        if (RunLength)                                                         \
            SETVALUE(0);                                                       \
        if (a0 != (int)lastx)                                                  \
        {                                                                      \
            badlength(a0, lastx);                                              \
            while (a0 > (int)lastx && pa > thisrun)                            \
                a0 -= (int)*--pa;                                              \
            if (a0 < (int)lastx)                                               \
            {                                                                  \
                if (a0 < 0)                                                    \
                    a0 = 0;                                                    \
                if ((pa - thisrun) & 1)                                        \
                    SETVALUE(0);                                               \
                SETVALUE((uint32_t)((int)lastx - a0));                         \
            }                                                                  \
            else if (a0 > (int)lastx)                                          \
            {                                                                  \
                SETVALUE(lastx);                                               \
                SETVALUE(0);                                                   \
            }                                                                  \
        }                                                                      \
    } while (0)

/*
 * Decode a line of 1D-encoded data.
 *
 * The line expanders are written as macros so that they can be reused
 * but still have direct access to the local variables of the "calling"
 * function.
 *
 * Note that unlike the original version we have to explicitly test for
 * a0 >= lastx after each black/white run is decoded.  This is because
 * the original code depended on the input data being zero-padded to
 * insure the decoder recognized an EOL before running out of data.
 */
#define EXPAND1D(eoflab)                                                       \
    do                                                                         \
    {                                                                          \
        for (;;)                                                               \
        {                                                                      \
            for (;;)                                                           \
            {                                                                  \
                LOOKUP16(12, TIFFFaxWhiteTable, eof1d);                        \
                switch (TabEnt->State)                                         \
                {                                                              \
                    case S_EOL:                                                \
                        EOLcnt = 1;                                            \
                        goto done1d;                                           \
                    case S_TermW:                                              \
                        SETVALUE(TabEnt->Param);                               \
                        goto doneWhite1d;                                      \
                    case S_MakeUpW:                                            \
                    case S_MakeUp:                                             \
                        a0 = (int)((uint32_t)a0 + TabEnt->Param);              \
                        RunLength =                                            \
                            (int)((uint32_t)RunLength + TabEnt->Param);        \
                        break;                                                 \
                    default:                                                   \
                        unexpected("WhiteTable", a0);                          \
                        goto done1d;                                           \
                }                                                              \
            }                                                                  \
        doneWhite1d:                                                           \
            if (a0 >= (int)lastx)                                              \
                goto done1d;                                                   \
            for (;;)                                                           \
            {                                                                  \
                LOOKUP16(13, TIFFFaxBlackTable, eof1d);                        \
                switch (TabEnt->State)                                         \
                {                                                              \
                    case S_EOL:                                                \
                        EOLcnt = 1;                                            \
                        goto done1d;                                           \
                    case S_TermB:                                              \
                        SETVALUE(TabEnt->Param);                               \
                        goto doneBlack1d;                                      \
                    case S_MakeUpB:                                            \
                    case S_MakeUp:                                             \
                        a0 += (int)TabEnt->Param;                              \
                        RunLength += (int)TabEnt->Param;                       \
                        break;                                                 \
                    default:                                                   \
                        unexpected("BlackTable", a0);                          \
                        goto done1d;                                           \
                }                                                              \
            }                                                                  \
        doneBlack1d:                                                           \
            if (a0 >= (int)lastx)                                              \
                goto done1d;                                                   \
            if (*(pa - 1) == 0 && *(pa - 2) == 0)                              \
                pa -= 2;                                                       \
        }                                                                      \
    eof1d:                                                                     \
        prematureEOF(a0);                                                      \
        CLEANUP_RUNS();                                                        \
        goto eoflab;                                                           \
    done1d:                                                                    \
        CLEANUP_RUNS();                                                        \
    } while (0)

/*
 * Update the value of b1 using the array
 * of runs for the reference line.
 */
#define CHECK_b1                                                               \
    do                                                                         \
    {                                                                          \
        if (pa != thisrun)                                                     \
            while (b1 <= a0 && b1 < (int)lastx)                                \
            {                                                                  \
                if (pb + 1 >= sp->refruns + sp->nruns)                         \
                {                                                              \
                    TIFFErrorExtR(tif, module,                                 \
                                  "Buffer overflow at line %d of %s %u",       \
                                  sp->line, isTiled(tif) ? "tile" : "strip",   \
                                  isTiled(tif) ? tif->tif_dir.td_curtile       \
                                               : tif->tif_dir.td_curstrip);    \
                    return (-1);                                               \
                }                                                              \
                b1 += (int)(pb[0] + pb[1]);                                    \
                pb += 2;                                                       \
            }                                                                  \
    } while (0)

/*
 * Expand a row of 2D-encoded data.
 */
#define EXPAND2D(eoflab)                                                       \
    do                                                                         \
    {                                                                          \
        while (a0 < (int)lastx)                                                \
        {                                                                      \
            if (pa >= thisrun + sp->nruns)                                     \
            {                                                                  \
                TIFFErrorExtR(tif, module,                                     \
                              "Buffer overflow at line %d of %s %u", sp->line, \
                              isTiled(tif) ? "tile" : "strip",                 \
                              isTiled(tif) ? tif->tif_dir.td_curtile           \
                                           : tif->tif_dir.td_curstrip);        \
                return (-1);                                                   \
            }                                                                  \
            LOOKUP8(7, TIFFFaxMainTable, eof2d);                               \
            switch (TabEnt->State)                                             \
            {                                                                  \
                case S_Pass:                                                   \
                    CHECK_b1;                                                  \
                    if (pb + 1 >= sp->refruns + sp->nruns)                     \
                    {                                                          \
                        TIFFErrorExtR(                                         \
                            tif, module,                                       \
                            "Buffer overflow at line %d of %s %u", sp->line,   \
                            isTiled(tif) ? "tile" : "strip",                   \
                            isTiled(tif) ? tif->tif_dir.td_curtile             \
                                         : tif->tif_dir.td_curstrip);          \
                        return (-1);                                           \
                    }                                                          \
                    b1 = b1 + (int)*pb++;                                      \
                    RunLength =                                                \
                        (int)((uint32_t)RunLength + (uint32_t)(b1 - a0));      \
                    a0 = b1;                                                   \
                    b1 = b1 + (int)*pb++;                                      \
                    break;                                                     \
                case S_Horiz:                                                  \
                    if ((pa - thisrun) & 1)                                    \
                    {                                                          \
                        for (;;)                                               \
                        { /* black first */                                    \
                            LOOKUP16(13, TIFFFaxBlackTable, eof2d);            \
                            switch (TabEnt->State)                             \
                            {                                                  \
                                case S_TermB:                                  \
                                    SETVALUE(TabEnt->Param);                   \
                                    goto doneWhite2da;                         \
                                case S_MakeUpB:                                \
                                case S_MakeUp:                                 \
                                    a0 = (int)((uint32_t)a0 + TabEnt->Param);  \
                                    RunLength = (int)((uint32_t)RunLength +    \
                                                      TabEnt->Param);          \
                                    break;                                     \
                                default:                                       \
                                    goto badBlack2d;                           \
                            }                                                  \
                        }                                                      \
                    doneWhite2da:;                                             \
                        for (;;)                                               \
                        { /* then white */                                     \
                            LOOKUP16(12, TIFFFaxWhiteTable, eof2d);            \
                            switch (TabEnt->State)                             \
                            {                                                  \
                                case S_TermW:                                  \
                                    SETVALUE(TabEnt->Param);                   \
                                    goto doneBlack2da;                         \
                                case S_MakeUpW:                                \
                                case S_MakeUp:                                 \
                                    a0 = (int)((uint32_t)a0 + TabEnt->Param);  \
                                    RunLength = (int)((uint32_t)RunLength +    \
                                                      TabEnt->Param);          \
                                    break;                                     \
                                default:                                       \
                                    goto badWhite2d;                           \
                            }                                                  \
                        }                                                      \
                    doneBlack2da:;                                             \
                    }                                                          \
                    else                                                       \
                    {                                                          \
                        for (;;)                                               \
                        { /* white first */                                    \
                            LOOKUP16(12, TIFFFaxWhiteTable, eof2d);            \
                            switch (TabEnt->State)                             \
                            {                                                  \
                                case S_TermW:                                  \
                                    SETVALUE(TabEnt->Param);                   \
                                    goto doneWhite2db;                         \
                                case S_MakeUpW:                                \
                                case S_MakeUp:                                 \
                                    a0 = (int)((uint32_t)a0 + TabEnt->Param);  \
                                    RunLength = (int)((uint32_t)RunLength +    \
                                                      TabEnt->Param);          \
                                    break;                                     \
                                default:                                       \
                                    goto badWhite2d;                           \
                            }                                                  \
                        }                                                      \
                    doneWhite2db:;                                             \
                        for (;;)                                               \
                        { /* then black */                                     \
                            LOOKUP16(13, TIFFFaxBlackTable, eof2d);            \
                            switch (TabEnt->State)                             \
                            {                                                  \
                                case S_TermB:                                  \
                                    SETVALUE(TabEnt->Param);                   \
                                    goto doneBlack2db;                         \
                                case S_MakeUpB:                                \
                                case S_MakeUp:                                 \
                                    a0 = (int)((uint32_t)a0 + TabEnt->Param);  \
                                    RunLength = (int)((uint32_t)RunLength +    \
                                                      TabEnt->Param);          \
                                    break;                                     \
                                default:                                       \
                                    goto badBlack2d;                           \
                            }                                                  \
                        }                                                      \
                    doneBlack2db:;                                             \
                    }                                                          \
                    CHECK_b1;                                                  \
                    break;                                                     \
                case S_V0:                                                     \
                    CHECK_b1;                                                  \
                    SETVALUE(b1 - a0);                                         \
                    if (pb >= sp->refruns + sp->nruns)                         \
                    {                                                          \
                        TIFFErrorExtR(                                         \
                            tif, module,                                       \
                            "Buffer overflow at line %d of %s %u", sp->line,   \
                            isTiled(tif) ? "tile" : "strip",                   \
                            isTiled(tif) ? tif->tif_dir.td_curtile             \
                                         : tif->tif_dir.td_curstrip);          \
                        return (-1);                                           \
                    }                                                          \
                    b1 = b1 + (int)*pb++;                                      \
                    break;                                                     \
                case S_VR:                                                     \
                    CHECK_b1;                                                  \
                    SETVALUE((int)((uint32_t)(b1 - a0) + TabEnt->Param));      \
                    if (pb >= sp->refruns + sp->nruns)                         \
                    {                                                          \
                        TIFFErrorExtR(                                         \
                            tif, module,                                       \
                            "Buffer overflow at line %d of %s %u", sp->line,   \
                            isTiled(tif) ? "tile" : "strip",                   \
                            isTiled(tif) ? tif->tif_dir.td_curtile             \
                                         : tif->tif_dir.td_curstrip);          \
                        return (-1);                                           \
                    }                                                          \
                    b1 = b1 + (int)*pb++;                                      \
                    break;                                                     \
                case S_VL:                                                     \
                    CHECK_b1;                                                  \
                    if (b1 < (int)((uint32_t)a0 + TabEnt->Param))              \
                    {                                                          \
                        unexpected("VL", a0);                                  \
                        goto eol2d;                                            \
                    }                                                          \
                    SETVALUE((int)((uint32_t)(b1 - a0) - TabEnt->Param));      \
                    b1 = b1 - (int)*--pb;                                      \
                    break;                                                     \
                case S_Ext:                                                    \
                    *pa++ = (uint32_t)((int)lastx - a0);                       \
                    extension(a0);                                             \
                    goto eol2d;                                                \
                case S_EOL:                                                    \
                    *pa++ = (uint32_t)((int)lastx - a0);                       \
                    NeedBits8(4, eof2d);                                       \
                    if (GetBits(4))                                            \
                        unexpected("EOL", a0);                                 \
                    ClrBits(4);                                                \
                    EOLcnt = 1;                                                \
                    goto eol2d;                                                \
                default:                                                       \
                badMain2d:                                                     \
                    unexpected("MainTable", a0);                               \
                    goto eol2d;                                                \
                badBlack2d:                                                    \
                    unexpected("BlackTable", a0);                              \
                    goto eol2d;                                                \
                badWhite2d:                                                    \
                    unexpected("WhiteTable", a0);                              \
                    goto eol2d;                                                \
                eof2d:                                                         \
                    prematureEOF(a0);                                          \
                    CLEANUP_RUNS();                                            \
                    goto eoflab;                                               \
            }                                                                  \
        }                                                                      \
        if (RunLength)                                                         \
        {                                                                      \
            if (RunLength + a0 < lastx)                                        \
            {                                                                  \
                /* expect a final V0 */                                        \
                NeedBits8(1, eof2d);                                           \
                if (!GetBits(1))                                               \
                    goto badMain2d;                                            \
                ClrBits(1);                                                    \
            }                                                                  \
            SETVALUE(0);                                                       \
        }                                                                      \
    eol2d:                                                                     \
        CLEANUP_RUNS();                                                        \
    } while (0)
#endif /* _FAX3_ */

/**
 * @def S_Null
 * @brief Mark a table entry that matches no code.
 * @ingroup libtiff_private_fax
 *
 * TIFFFaxTabEnt.State value 0, left in generated table entries that mkg3states
 * does not assign. The decoder macros report such an entry as a bad code
 * word through unexpected().
 */

/**
 * @def S_Pass
 * @brief Select pass mode.
 * @ingroup libtiff_private_fax
 *
 * TIFFFaxTabEnt.State value 1, assigned by mkg3states in
 * TIFFFaxMainTable. EXPAND2D moves a0 to b2 without
 * ending a run.
 */

/**
 * @def S_Horiz
 * @brief Select horizontal mode.
 * @ingroup libtiff_private_fax
 *
 * TIFFFaxTabEnt.State value 2, assigned by mkg3states in
 * TIFFFaxMainTable. EXPAND2D then decodes two
 * runs from the white and black tables.
 */

/**
 * @def S_V0
 * @brief Select zero-offset vertical mode.
 * @ingroup libtiff_private_fax
 *
 * TIFFFaxTabEnt.State value 3, assigned by mkg3states in
 * TIFFFaxMainTable. The next change a1 is
 * directly below b1.
 */

/**
 * @def S_VR
 * @brief Select right-offset vertical mode.
 * @ingroup libtiff_private_fax
 *
 * TIFFFaxTabEnt.State value 4, assigned by mkg3states in
 * TIFFFaxMainTable. The next change a1 is
 * TIFFFaxTabEnt.Param pixels to the right of b1.
 */

/**
 * @def S_VL
 * @brief Select left-offset vertical mode.
 * @ingroup libtiff_private_fax
 *
 * TIFFFaxTabEnt.State value 5, assigned by mkg3states in
 * TIFFFaxMainTable. The next change a1 is
 * TIFFFaxTabEnt.Param pixels to the left of b1.
 */

/**
 * @def S_Ext
 * @brief Select unsupported extension mode.
 * @ingroup libtiff_private_fax
 *
 * TIFFFaxTabEnt.State value 6, assigned by mkg3states in
 * TIFFFaxMainTable. Uncompressed-mode
 * extensions are not supported: EXPAND2D reports them with extension() and
 * ends the row.
 */

/**
 * @def S_TermW
 * @brief Finish a white run.
 * @ingroup libtiff_private_fax
 *
 * TIFFFaxTabEnt.State value 7, assigned by mkg3states in
 * TIFFFaxWhiteTable. TIFFFaxTabEnt.Param is the
 * terminating run length (0 to 63).
 */

/**
 * @def S_TermB
 * @brief Finish a black run.
 * @ingroup libtiff_private_fax
 *
 * TIFFFaxTabEnt.State value 8, assigned by mkg3states in
 * TIFFFaxBlackTable. TIFFFaxTabEnt.Param is the
 * terminating run length (0 to 63).
 */

/**
 * @def S_MakeUpW
 * @brief Extend a white run.
 * @ingroup libtiff_private_fax
 *
 * TIFFFaxTabEnt.State value 9, assigned by mkg3states in
 * TIFFFaxWhiteTable. TIFFFaxTabEnt.Param, a
 * multiple of 64, is added to the current run and decoding continues with
 * the same table.
 */

/**
 * @def S_MakeUpB
 * @brief Extend a black run.
 * @ingroup libtiff_private_fax
 *
 * TIFFFaxTabEnt.State value 10, assigned by mkg3states in
 * TIFFFaxBlackTable. TIFFFaxTabEnt.Param, a
 * multiple of 64, is added to the current run and decoding continues with
 * the same table.
 */

/**
 * @def S_MakeUp
 * @brief Extend either run colour.
 * @ingroup libtiff_private_fax
 *
 * TIFFFaxTabEnt.State value 11, assigned by mkg3states in both
 * TIFFFaxWhiteTable and TIFFFaxBlackTable for the extended makeup codes
 * (runs of 1792 to 2560) that both colours share.
 */

/**
 * @def S_EOL
 * @brief Recognise end of line.
 * @ingroup libtiff_private_fax
 *
 * TIFFFaxTabEnt.State value 12, assigned by mkg3states to the EOL code in
 * all three tables. The decoder macros set EOLcnt and end the row.
 */

/**
 * @def EndOfData()
 * @brief Test whether the local raw cursor is exhausted.
 * @ingroup libtiff_private_fax
 *
 * Expands to (cp >= ep) using the decoder locals declared by DECLARE_STATE.
 * Only defined if not already defined, so a user of the decoder macros can
 * supply a different end-of-input test.
 */

/**
 * @def NeedBits8(n, eoflab)
 * @brief Load bits or branch to the caller EOF label.
 * @ingroup libtiff_private_fax
 *
 * If the local BitsAvail is below @p n, loads one byte from the local cp
 * (through the bit-reversal table @c bitmap) into BitAcc above the valid
 * bits and advances cp, so @p n must not exceed BitsAvail + 8. At the end
 * of input (EndOfData()) it jumps to @p eoflab when no valid bits remain;
 * otherwise it sets BitsAvail to @p n, treating the missing bits as zero.
 * Writes BitAcc, BitsAvail and cp. @p n is evaluated up to twice.
 *
 * @param[in] n Number of bits required, at most 8.
 * @param[in] eoflab Caller label jumped to when the input is exhausted and
 * BitsAvail is zero.
 */

/**
 * @def NeedBits16(n, eoflab)
 * @brief Load up to sixteen bits or branch to EOF.
 * @ingroup libtiff_private_fax
 *
 * Like NeedBits8() but loads up to two bytes from the local cp, so @p n
 * may be up to 16. At the end of input it jumps to @p eoflab only when no
 * valid bits remain before the first byte would be read; otherwise it sets
 * BitsAvail to @p n, treating the missing bits as zero. Writes BitAcc,
 * BitsAvail and cp. @p n is evaluated up to three times.
 *
 * @param[in] n Number of bits required, at most 16.
 * @param[in] eoflab Caller label jumped to when the input is exhausted and
 * BitsAvail is zero.
 */

/**
 * @def GetBits(n)
 * @brief Inspect low bits of the local accumulator.
 * @ingroup libtiff_private_fax
 *
 * Expands to the low @p n bits of the local BitAcc without consuming them.
 * A NeedBits8() or NeedBits16() call must first make @p n bits available.
 *
 * @param[in] n Number of bits to inspect, at most 16.
 */

/**
 * @def ClrBits(n)
 * @brief Consume low bits from the local accumulator.
 * @ingroup libtiff_private_fax
 *
 * Subtracts @p n from the local BitsAvail and shifts BitAcc right by @p n.
 * @p n is evaluated twice.
 *
 * @param[in] n Number of bits to consume, at most BitsAvail.
 */

/**
 * @def LOOKUP8(wid, tab, eoflab)
 * @brief Fetch and consume an eight-bit-bounded transition.
 * @ingroup libtiff_private_fax
 *
 * Calls NeedBits8(wid, eoflab), sets the local TabEnt to
 * tab + GetBits(wid), then consumes TabEnt->Width bits with ClrBits(). The
 * table must have 2^@p wid entries. Writes TabEnt, BitAcc, BitsAvail and
 * cp. @p wid is evaluated several times and @p tab once. With FAX3_DEBUG
 * the expansion also prints the state and code bits to stdout.
 *
 * @param[in] wid Number of bits used to index @p tab, at most 8.
 * @param[in] tab Decoding table (TIFFFaxTabEnt array) to index.
 * @param[in] eoflab Caller label jumped to by NeedBits8() at the end of
 * input.
 */

/**
 * @def LOOKUP16(wid, tab, eoflab)
 * @brief Fetch and consume a sixteen-bit-bounded transition.
 * @ingroup libtiff_private_fax
 *
 * Same as LOOKUP8() but uses NeedBits16(), so @p wid may be up to 16. The
 * decoder uses it with 12 bits for TIFFFaxWhiteTable and 13 bits for
 * TIFFFaxBlackTable. Writes TabEnt, BitAcc, BitsAvail and cp.
 *
 * @param[in] wid Number of bits used to index @p tab, at most 16.
 * @param[in] tab Decoding table (TIFFFaxTabEnt array) to index.
 * @param[in] eoflab Caller label jumped to by NeedBits16() at the end of
 * input.
 */

/**
 * @def SETVALUE(x)
 * @brief Append a checked decoded run and reset its local length.
 * @ingroup libtiff_private_fax
 *
 * Stores RunLength + @p x at the local pa and advances pa, adds @p x to
 * a0 and sets RunLength to 0. If pa has reached thisrun + sp->nruns it
 * instead reports "Buffer overflow" with TIFFErrorExtR() and executes
 * return (-1) from the enclosing function, without UNCACHE_STATE(). The
 * FAX3_DEBUG variant omits this check and prints the run to stdout.
 * @p x is evaluated twice (three times with FAX3_DEBUG).
 *
 * @param[in] x Pixel count added to the pending RunLength.
 */

/**
 * @def SYNC_EOL(eoflab, retrywithouteol)
 * @brief Synchronise EOL or retry configured no-EOL recovery.
 * @ingroup libtiff_private_fax
 *
 * Does nothing if FAXMODE_NOEOL is set in sp->b.mode. Otherwise, when the
 * local EOLcnt is 0, discards bits until 11 zero bits are found; then
 * skips the remaining zero bits and the terminating 1 bit of the EOL code
 * and resets EOLcnt to 0. Writes BitAcc, BitsAvail, cp and EOLcnt.
 *
 * If the input ends while scanning for the final 1 bit, it sets
 * FAXMODE_NOEOL in sp->b.mode, which persists in the codec state and
 * affects all later rows and strips, warns through tryG3WithoutEOL(), and
 * jumps to @p retrywithouteol. Defines the label noEOLFound, so it can be used
 * only once per function.
 *
 * @param[in] eoflab Caller label jumped to when the input ends while
 * searching for the 11 zero bits.
 * @param[in] retrywithouteol Caller label jumped to after switching to
 * FAXMODE_NOEOL; the decoders place it before CACHE_STATE(), so the bit
 * state and input cursor are reloaded from sp and the raw buffer.
 */

/**
 * @def CLEANUP_RUNS()
 * @brief Repair the current row run total to the requested width.
 * @ingroup libtiff_private_fax
 *
 * Appends any pending RunLength with SETVALUE(0). If a0 then differs from
 * lastx, it warns through badlength() (incrementing sp->eolReachedCount),
 * drops trailing runs from pa until a0 is at most lastx, and pads the row:
 * a white run of lastx - a0 (after a zero-length run if needed to keep the
 * colour parity), or, if a0 still exceeds lastx with no runs left, a run of
 * lastx followed by a zero-length run. Writes pa, a0 and RunLength and the
 * run array at thisrun. Through SETVALUE() it can report "Buffer overflow"
 * and return (-1) from the enclosing function.
 */

/**
 * @def EXPAND1D(eoflab)
 * @brief Expand a one-dimensional Huffman row into runs.
 * @ingroup libtiff_private_fax
 *
 * Decodes alternating white and black runs with LOOKUP16() until a0 reaches
 * lastx, an EOL code (which sets EOLcnt to 1) or an invalid code (reported
 * by unexpected(), which increments sp->unexpectedReachedCount), and then
 * runs CLEANUP_RUNS(). Appends runs at pa with SETVALUE() and updates a0,
 * RunLength, TabEnt and the bit state. Does not use the reference runs.
 *
 * At the end of input it warns through prematureEOF() (incrementing
 * sp->eofReachedCount), runs CLEANUP_RUNS() and jumps to @p eoflab, so that
 * label must not run CLEANUP_RUNS() again. Through SETVALUE() it can report
 * "Buffer overflow" and return (-1) from the enclosing function. Defines
 * internal labels (eof1d, done1d, doneWhite1d, doneBlack1d), so it can be
 * used only once per function.
 *
 * @param[in] eoflab Caller label jumped to after a premature end of input.
 */

/**
 * @def CHECK_b1
 * @brief Advance the reference change cursor with capacity checks.
 * @ingroup libtiff_private_fax
 *
 * Unless pa is still at thisrun, advances the reference-line cursor pb two
 * runs at a time, adding both to b1, while b1 is at most a0 and below lastx.
 * Writes b1 and pb. If pb + 1 reaches sp->refruns + sp->nruns it reports
 * "Buffer overflow" with TIFFErrorExtR() and executes return (-1) from the
 * enclosing function.
 */

/**
 * @def EXPAND2D(eoflab)
 * @brief Expand a two-dimensional row relative to reference runs.
 * @ingroup libtiff_private_fax
 *
 * Decodes mode codes with LOOKUP8() and TIFFFaxMainTable while a0 is below
 * lastx: pass, horizontal (two runs from the white and black tables) and
 * vertical modes, using b1 and pb on the reference runs (advanced with
 * CHECK_b1). Appends runs at pa with SETVALUE() and writes a0, RunLength,
 * b1, pb, TabEnt, EOLcnt and the bit state. A pending run at the end of
 * the row is appended, after checking for a final V0 bit if the row would
 * otherwise be short. The row is finished with CLEANUP_RUNS().
 *
 * An invalid code is reported by unexpected() (incrementing
 * sp->unexpectedReachedCount) and an uncompressed-mode extension by
 * extension(); both end the row. An EOL code sets EOLcnt to 1 and ends the
 * row. At the end of input it warns through prematureEOF() (incrementing
 * sp->eofReachedCount), runs CLEANUP_RUNS() and jumps to @p eoflab. If the
 * current or reference run array would overflow it reports "Buffer
 * overflow" with TIFFErrorExtR() and executes return (-1) from the
 * enclosing function. Defines internal labels (eof2d, eol2d and others), so
 * it can be used only once per function.
 *
 * @param[in] eoflab Caller label jumped to after a premature end of input.
 */

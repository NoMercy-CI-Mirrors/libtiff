/*
 * Copyright (c) 1988-1997 Sam Leffler
 * Copyright (c) 1991-1997 Silicon Graphics, Inc.
 * Copyright (c) 2022 Even Rouault
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
 * @file tif_lzw.c
 * @brief Implement TIFF LZW encoding standard decoding and legacy decoding.
 */

#include "tiffiop.h"
#ifdef LZW_SUPPORT
/*
 * TIFF Library.
 * Rev 5.0 Lempel-Ziv & Welch Compression Support
 *
 * This code is derived from the compress program whose code is
 * derived from software contributed to Berkeley by James A. Woods,
 * derived from original work by Spencer Thomas and Joseph Orost.
 *
 * The original Berkeley copyright notice appears below in its entirety.
 */
#include "tif_predict.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

/* Select the plausible largest natural integer type for the architecture */
#define SIZEOF_WORDTYPE SIZEOF_SIZE_T
/**
 * @brief Select the native size_t word for buffered LZW bits.
 * @ingroup libtiff_private_lzw
 */
typedef size_t WordType;

/*
 * NB: The 5.0 spec describes a different algorithm than Aldus
 *     implements.  Specifically, Aldus does code length transitions
 *     one code earlier than should be done (for real LZW).
 *     Earlier versions of this library implemented the correct
 *     LZW algorithm, but emitted codes in a bit order opposite
 *     to the TIFF spec.  Thus, to maintain compatibility w/ Aldus
 *     we interpret MSB-LSB ordered codes to be images written w/
 *     old versions of this library, but otherwise adhere to the
 *     Aldus "off by one" algorithm.
 *
 * Future revisions to the TIFF spec are expected to "clarify this issue".
 */
#define LZW_COMPAT /* include backwards compatibility code */

#define MAXCODE(n) ((1 << (n)) - 1)
/*
 * The TIFF spec specifies that encoded bit
 * strings range from 9 to 12 bits.
 */
#define BITS_MIN 9  /* start with 9 bits */
#define BITS_MAX 12 /* max of 12 bit strings */
/* predefined codes */
#define CODE_CLEAR 256 /* code to clear string table */
#define CODE_EOI 257   /* end-of-information code */
#define CODE_FIRST 258 /* first free code entry */
#define CODE_MAX MAXCODE(BITS_MAX)
#define HSIZE 9001 /* 91% occupancy */
#define HSHIFT (13 - 8)
#ifdef LZW_COMPAT
/* NB: +1024 is for compatibility with old files */
#define CSIZE (MAXCODE(BITS_MAX) + 1024L)
#else
#define CSIZE (MAXCODE(BITS_MAX) + 1L)
#endif

/*
 * State block for each open TIFF file using LZW
 * compression/decompression.  Note that the predictor
 * state block must be first in this data structure.
 */
/**
 * @brief Prefix LZW state with Predictor hooks and shared bit-code state.
 * @ingroup libtiff_private_lzw
 *
 * Lives at the start of the handle-owned LZWCodecState (tif_data), which
 * LZWCleanup() frees.
 */
typedef struct
{
    /** @brief Predictor prefix; must be the first member. */
    TIFFPredictorState predict;

    /** @brief Current code width in bits. */
    unsigned short nbits;
    /** @brief Largest code under the current width convention. */
    unsigned short maxcode;
    /** @brief Next available dictionary code. */
    unsigned short free_ent;
    /** @brief Buffered input/output code bits. */
    WordType nextdata;
    /** @brief Number of valid buffered code bits. */
    long nextbits;

    /** @brief Open mode saved at codec initialisation. */
    int rw_mode;
} LZWBaseState;

#define lzw_nbits base.nbits
#define lzw_maxcode base.maxcode
#define lzw_free_ent base.free_ent
#define lzw_nextdata base.nextdata
#define lzw_nextbits base.nextbits

/*
 * Encoding-specific state.
 */
/**
 * @brief Store an encoder dictionary code in sixteen bits.
 * @ingroup libtiff_private_lzw
 */
typedef uint16_t hcode_t; /* codes fit in 16 bits */
/**
 * @brief Pair an encoder hash key with its dictionary code.
 * @ingroup libtiff_private_lzw
 *
 * Element of the HSIZE-entry enc_hashtab array owned by LZWCodecState;
 * cl_hash() marks every entry unused.
 */
typedef struct
{
    /** @brief Encoder lookup key; minus one means unused. */
    long hash;
    /** @brief Encoder dictionary code for the key. */
    hcode_t code;
} hash_t;

/*
 * Decoding-specific state.
 */
/**
 * @brief Link one decoder string to its preceding dictionary string.
 * @ingroup libtiff_private_lzw
 *
 * Element of the CSIZE-entry dec_codetab array owned by LZWCodecState;
 * next links point into the same array.
 */
typedef struct code_ent
{
    /**
     * @brief Borrowed link within the decoder table to the preceding string
     * entry.
     */
    struct code_ent *next;
    /** @brief Decoded string length including this entry. */
    unsigned short length;

    /**
     * @brief First byte of the decoded string; immediately precedes value for
     * fast paths.
     */
    unsigned char firstchar;
    /** @brief Final byte represented by this entry. */
    unsigned char value;
    /** @brief Whether the string consists of one repeated byte. */
    bool repeated;
} code_t;

/**
 * @brief Signature shared by the LZW decode entry points (recorded in
 * dec_decode).
 * @ingroup libtiff_private_lzw
 *
 * Matches LZWDecode() and LZWDecodeCompat(). No pointer of this type is ever
 * called: decoding is dispatched through tif_decoderow, tif_decodestrip and
 * tif_decodetile.
 *
 * @param[in,out] tif LZW handle being decoded.
 * @param[out] buf Decoded output of size bytes.
 * @param[in] size Requested decoded byte count.
 * @param[in] sample Sample plane, ignored by LZW.
 * @return One on filling output, zero on decoding failure.
 */
typedef int (*decodeFunc)(TIFF *tif, uint8_t *buf, tmsize_t size,
                          uint16_t sample);

/**
 * @brief Own LZW dictionaries hash storage and transfer restart state.
 * @ingroup libtiff_private_lzw
 *
 * Allocated as tif_data by TIFFInitLZW() (or LZWSetupDecode() if missing).
 * dec_codetab and enc_hashtab are allocated by decode/encode setup, and
 * LZWCleanup() frees them together with the state.
 */
typedef struct
{
    /** @brief Shared LZW/Predictor prefix, first in the codec allocation. */
    LZWBaseState base;

    /** @brief Right-adjusted mask for current decode width. */
    long dec_nbitsmask;
    /** @brief Bytes already returned from an interrupted decoded string. */
    tmsize_t dec_restart;
    /** @brief Number of cached raw-input bits remaining. */
    uint64_t dec_bitsleft;
    /** @brief Raw byte count saved after the preceding decoder call. */
    tmsize_t old_tif_rawcc;
    /**
     * @brief Decoder last recorded by predecode; NULL until the first
     * predecode. Used only as a first-strile detection flag, never called, and
     * may differ from the installed tif_decode* methods.
     */
    decodeFunc dec_decode;
    /** @brief Borrowed current/restart entry within dec_codetab. */
    code_t *dec_codep;
    /** @brief Borrowed preceding recognised entry. */
    code_t *dec_oldcodep;
    /** @brief Next available decoder table entry cursor. */
    code_t *dec_free_entp;
    /** @brief Current width-dependent dictionary limit cursor. */
    code_t *dec_maxcodep;
    /** @brief Owned decoder entry allocation, released by cleanup. */
    code_t *dec_codetab;
    /** @brief Latched same-strile failure cleared by predecode. */
    int read_error;

    /** @brief Pending previous encoder string code. */
    int enc_oldcode;
    /** @brief Input-byte threshold for the next ratio test. */
    tmsize_t enc_checkpoint;
#define CHECK_GAP 10000
    /** @brief Cached compression ratio in fixed-point form. */
    tmsize_t enc_ratio;
    /** @brief Input bytes counted for ratio estimation. */
    tmsize_t enc_incount;
    /** @brief Output code bits counted for ratio estimation. */
    tmsize_t enc_outcount;
    /** @brief Borrowed raw-buffer limit leaving room for final codes. */
    uint8_t *enc_rawlimit;
    /** @brief Owned HSIZE-entry encoding table, released by cleanup. */
    hash_t *enc_hashtab;
} LZWCodecState;

#define LZWState(tif) ((LZWBaseState *)(tif)->tif_data)
#define LZWDecoderState(tif) ((LZWCodecState *)LZWState(tif))
#define LZWEncoderState(tif) ((LZWCodecState *)LZWState(tif))

static int LZWDecode(TIFF *tif, uint8_t *op0, tmsize_t occ0, uint16_t s);
#ifdef LZW_COMPAT
static int LZWDecodeCompat(TIFF *tif, uint8_t *op0, tmsize_t occ0, uint16_t s);
#endif

/*
 * LZW Decoder.
 */

/**
 * @brief Accept LZW tag fixup without changes.
 * @ingroup libtiff_private_lzw
 *
 * No allocation, I/O, diagnostics, mutation or ownership effects.
 *
 * @param[in] tif Unused handle.
 * @return One unconditionally.
 */
static int LZWFixupTags(TIFF *tif)
{
    (void)tif;
    return (1);
}

/**
 * @brief Allocate and initialise the LZW decode dictionary.
 * @ingroup libtiff_private_lzw
 *
 * Allocates handle-owned codec state when absent, installs predictor hooks
 * on that path, and lazily allocates CSIZE entries. Preloads literal
 * entries and clears reserved entries. Allocation failure diagnoses and can
 * retain outer state for cleanup; no pixel I/O. Predictor initialisation
 * status is not propagated.
 *
 * @param[in,out] tif Non-NULL live LZW handle; tif_data may still be NULL,
 * in which case the state is allocated here. Serialise access.
 * @return One when dictionary storage is ready, zero on allocation failure.
 */
static int LZWSetupDecode(TIFF *tif)
{
    static const char module[] = "LZWSetupDecode";
    LZWCodecState *sp = LZWDecoderState(tif);
    int code;

    if (sp == NULL)
    {
        /*
         * Allocate state block so tag methods have storage to record
         * values.
         */
        tif->tif_data = (uint8_t *)_TIFFmallocExt(tif, sizeof(LZWCodecState));
        if (tif->tif_data == NULL)
        {
            TIFFErrorExtR(tif, module, "No space for LZW state block");
            return (0);
        }

        sp = LZWDecoderState(tif);
        sp->dec_codetab = NULL;
        sp->dec_decode = NULL;

        /*
         * Setup predictor setup.
         */
        (void)TIFFPredictorInit(tif);
    }

    if (sp->dec_codetab == NULL)
    {
        sp->dec_codetab = (code_t *)_TIFFmallocExt(tif, CSIZE * sizeof(code_t));
        if (sp->dec_codetab == NULL)
        {
            TIFFErrorExtR(tif, module, "No space for LZW code table");
            return (0);
        }
        /*
         * Pre-load the table.
         */
        code = 255;
        do
        {
            sp->dec_codetab[code].firstchar = (unsigned char)code;
            sp->dec_codetab[code].value = (unsigned char)code;
            sp->dec_codetab[code].repeated = true;
            sp->dec_codetab[code].length = 1;
            sp->dec_codetab[code].next = NULL;
        } while (code--);
        /*
         * Zero-out the unused entries  */
        /* Silence false positive */
        /* coverity[overrun-buffer-arg] */
        memset(&sp->dec_codetab[CODE_CLEAR], 0,
               (CODE_FIRST - CODE_CLEAR) * sizeof(code_t));
    }
    return (1);
}

/*
 * Setup state for decoding a strip.
 */
/**
 * @brief Reset LZW bit and dictionary state for a strile.
 * @ingroup libtiff_private_lzw
 *
 * Requires live outer state; lazily attempts setup if needed and rejects a
 * missing dictionary. Tests the first two raw bytes for historical reversed
 * code order on every call and, with LZW_COMPAT (always defined), sets the
 * matching lzw_maxcode convention. Only on the first predecode after codec
 * initialisation (dec_decode still NULL) does it act on the result: for
 * old-style data it warns, installs LZWDecodeCompat() as the row/strip/tile
 * decoder and re-runs setup so that predictor wrappers rebind. The decoder
 * choice is not revisited for later striles: a later strile with the other code
 * order keeps the decoder chosen first (and the warning is not repeated).
 * Without LZW_COMPAT, old-style data fails on every strile but is diagnosed
 * only the first time. The setup statuses are ignored. Clears restart/error
 * state and resets code pointers/bit cache. Does not allocate output or read
 * client bytes directly; setup can allocate/diagnose.
 *
 * @param[in,out] tif Non-NULL live LZW handle with valid state, raw-buffer
 * cursors and matching geometry; serialise access.
 * @param[in] s Unused sample plane; predictor wrappers manage channel
 * stride.
 * @return One after reset, zero for missing dictionary or unavailable
 * compatibility support.
 */
static int LZWPreDecode(TIFF *tif, uint16_t s)
{
    static const char module[] = "LZWPreDecode";
    LZWCodecState *sp = LZWDecoderState(tif);

    (void)s;
    assert(sp != NULL);
    if (sp->dec_codetab == NULL)
    {
        tif->tif_setupdecode(tif);
        if (sp->dec_codetab == NULL)
            return (0);
    }

    /*
     * Check for old bit-reversed codes.
     */
    if (tif->tif_rawcc >= 2 && tif->tif_rawdata[0] == 0 &&
        (tif->tif_rawdata[1] & 0x1))
    {
#ifdef LZW_COMPAT
        if (!sp->dec_decode)
        {
            TIFFWarningExtR(tif, module, "Old-style LZW codes, convert file");
            /*
             * Override default decoding methods with
             * ones that deal with the old coding.
             * Otherwise the predictor versions set
             * above will call the compatibility routines
             * through the dec_decode method.
             */
            tif->tif_decoderow = LZWDecodeCompat;
            tif->tif_decodestrip = LZWDecodeCompat;
            tif->tif_decodetile = LZWDecodeCompat;
            /*
             * If doing horizontal differencing, must
             * re-setup the predictor logic since we
             * switched the basic decoder methods...
             */
            (*tif->tif_setupdecode)(tif);
            sp->dec_decode = LZWDecodeCompat;
        }
        sp->lzw_maxcode = MAXCODE(BITS_MIN);
#else  /* !LZW_COMPAT */
        if (!sp->dec_decode)
        {
            TIFFErrorExtR(tif, module, "Old-style LZW codes not supported");
            sp->dec_decode = LZWDecode;
        }
        return (0);
#endif /* !LZW_COMPAT */
    }
    else
    {
        sp->lzw_maxcode = MAXCODE(BITS_MIN) - 1;
        sp->dec_decode = LZWDecode;
    }
    sp->lzw_nbits = BITS_MIN;
    sp->lzw_nextbits = 0;
    sp->lzw_nextdata = 0;

    sp->dec_restart = 0;
    sp->dec_nbitsmask = MAXCODE(BITS_MIN);
    sp->dec_bitsleft = 0;
    sp->old_tif_rawcc = 0;
    sp->dec_free_entp = sp->dec_codetab - 1; // + CODE_FIRST;
    /*
     * Zero entries that are not yet filled in.  We do
     * this to guard against bogus input data that causes
     * us to index into undefined entries.  If you can
     * come up with a way to safely bounds-check input codes
     * while decoding then you can remove this operation.
     */
    sp->dec_oldcodep = &sp->dec_codetab[0];
    sp->dec_maxcodep = &sp->dec_codetab[sp->dec_nbitsmask - 1];
    sp->read_error = 0;
    return (1);
}

/*
 * Decode a "hunk of data".
 */

/* Get the next 32 or 64-bit from the input data */
#if WORDS_BIGENDIAN
#define GetNextData(nextdata, bp) memcpy(&nextdata, bp, sizeof(nextdata))
#elif SIZEOF_WORDTYPE == 8
#if defined(_M_X64)
#define GetNextData(nextdata, bp) nextdata = _byteswap_uint64(*(uint64_t *)(bp))
#elif defined(__GNUC__)
#define GetNextData(nextdata, bp)                                              \
    memcpy(&nextdata, bp, sizeof(nextdata));                                   \
    nextdata = __builtin_bswap64(nextdata)
#else
#define GetNextData(nextdata, bp)                                              \
    nextdata = (((uint64_t)bp[0]) << 56) | (((uint64_t)bp[1]) << 48) |         \
               (((uint64_t)bp[2]) << 40) | (((uint64_t)bp[3]) << 32) |         \
               (((uint64_t)bp[4]) << 24) | (((uint64_t)bp[5]) << 16) |         \
               (((uint64_t)bp[6]) << 8) | (((uint64_t)bp[7]))
#endif
#elif SIZEOF_WORDTYPE == 4
#if defined(_M_X86)
#define GetNextData(nextdata, bp)                                              \
    nextdata = _byteswap_ulong(*(unsigned long *)(bp))
#elif defined(__GNUC__)
#define GetNextData(nextdata, bp)                                              \
    memcpy(&nextdata, bp, sizeof(nextdata));                                   \
    nextdata = __builtin_bswap32(nextdata)
#else
#define GetNextData(nextdata, bp)                                              \
    nextdata = (((uint32_t)bp[0]) << 24) | (((uint32_t)bp[1]) << 16) |         \
               (((uint32_t)bp[2]) << 8) | (((uint32_t)bp[3]))
#endif
#else
#error "Unhandled SIZEOF_WORDTYPE"
#endif

#define GetNextCodeLZW()                                                       \
    do                                                                         \
    {                                                                          \
        nextbits -= nbits;                                                     \
        if (nextbits < 0)                                                      \
        {                                                                      \
            if (dec_bitsleft >= 8 * SIZEOF_WORDTYPE)                           \
            {                                                                  \
                unsigned codetmp = (unsigned)(nextdata << (-nextbits));        \
                GetNextData(nextdata, bp);                                     \
                bp += SIZEOF_WORDTYPE;                                         \
                nextbits += 8 * SIZEOF_WORDTYPE;                               \
                dec_bitsleft -= 8 * SIZEOF_WORDTYPE;                           \
                code = (WordType)((codetmp | (nextdata >> nextbits)) &         \
                                  (WordType)nbitsmask);                        \
                break;                                                         \
            }                                                                  \
            else                                                               \
            {                                                                  \
                if (dec_bitsleft < 8)                                          \
                {                                                              \
                    goto no_eoi;                                               \
                }                                                              \
                nextdata = (nextdata << 8) | *(bp)++;                          \
                nextbits += 8;                                                 \
                dec_bitsleft -= 8;                                             \
                if (nextbits < 0)                                              \
                {                                                              \
                    if (dec_bitsleft < 8)                                      \
                    {                                                          \
                        goto no_eoi;                                           \
                    }                                                          \
                    nextdata = (nextdata << 8) | *(bp)++;                      \
                    nextbits += 8;                                             \
                    dec_bitsleft -= 8;                                         \
                }                                                              \
            }                                                                  \
        }                                                                      \
        code = (WordType)((nextdata >> nextbits) & (WordType)nbitsmask);       \
    } while (0)

/**
 * @brief Expand standard TIFF LZW codes into caller output.
 * @ingroup libtiff_private_lzw
 *
 * Requires a nonnegative writable output extent and coherent predecode
 * dictionary/bit state. Supports interrupted-string restart across calls,
 * clear/end codes and TIFF early code-width changes. Updates dictionary,
 * bit cache and raw cursors on normal exits. Detected corrupt/truncated
 * input clears the unfilled tail, latches read_error, diagnoses and returns
 * zero; some error exits do not commit local cursor state. Further calls in
 * the same strile clear requested output and fail until predecode resets
 * the latch. No allocation/client I/O or ownership transfer.
 *
 * @param[in,out] tif Non-NULL live LZW handle with valid state, raw-buffer
 * cursors and matching geometry; serialise access.
 * @param[out] op0 Borrowed writable output of occ0 bytes, disjoint from raw
 * input and codec tables.
 * @param[in] occ0 Nonnegative requested decoded bytes; use a positive
 * extent during an interrupted-string restart.
 * @param[in] s Unused sample plane; predictor wrappers manage channel
 * stride.
 * @return One when requested output is filled, zero on detected decoding
 * error.
 */
static int LZWDecode(TIFF *tif, uint8_t *op0, tmsize_t occ0, uint16_t s)
{
    static const char module[] = "LZWDecode";
    LZWCodecState *sp = LZWDecoderState(tif);
    uint8_t *op = (uint8_t *)op0;
    tmsize_t occ = occ0;
    uint8_t *bp;
    long nbits, nextbits, nbitsmask;
    WordType nextdata;
    code_t *free_entp, *maxcodep, *oldcodep;

    (void)s;
    assert(sp != NULL);
    assert(sp->dec_codetab != NULL);

    if (sp->read_error)
    {
        memset(op, 0, (size_t)occ);
        TIFFErrorExtR(tif, module,
                      "LZWDecode: Scanline %" PRIu32 " cannot be read due to "
                      "previous error",
                      tif->tif_dir.td_row);
        return 0;
    }

    /*
     * Restart interrupted output operation.
     */
    if (sp->dec_restart)
    {
        tmsize_t residue;

        code_t *codep = sp->dec_codep;
        residue = codep->length - sp->dec_restart;
        if (residue > occ)
        {
            /*
             * Residue from previous decode is sufficient
             * to satisfy decode request.  Skip to the
             * start of the decoded string, place decoded
             * values in the output buffer, and return.
             */
            sp->dec_restart += occ;
            do
            {
                codep = codep->next;
            } while (--residue > occ && codep);
            if (codep)
            {
                uint8_t *tp = op + occ;
                do
                {
                    *--tp = codep->value;
                    codep = codep->next;
                } while (--occ && codep);
            }
            return (1);
        }
        /*
         * Residue satisfies only part of the decode request.
         */
        op += residue;
        occ -= residue;
        uint8_t *tp = op;
        do
        {
            *--tp = codep->value;
            codep = codep->next;
        } while (--residue && codep);
        sp->dec_restart = 0;
    }

    bp = (uint8_t *)tif->tif_rawcp;
    sp->dec_bitsleft +=
        (((uint64_t)tif->tif_rawcc - (uint64_t)sp->old_tif_rawcc) << 3);
    uint64_t dec_bitsleft = sp->dec_bitsleft;
    nbits = sp->lzw_nbits;
    nextdata = sp->lzw_nextdata;
    nextbits = sp->lzw_nextbits;
    nbitsmask = sp->dec_nbitsmask;
    oldcodep = sp->dec_oldcodep;
    free_entp = sp->dec_free_entp;
    maxcodep = sp->dec_maxcodep;
    code_t *const dec_codetab = sp->dec_codetab;
    code_t *codep;

    if (occ == 0)
    {
        goto after_loop;
    }

begin:
{
    WordType code;
    GetNextCodeLZW();
    codep = dec_codetab + (unsigned long)code;
    if (code >= CODE_FIRST)
        goto code_above_or_equal_to_258;
    if (code < 256)
        goto code_below_256;
    if (code == CODE_EOI)
        goto after_loop;
    goto code_clear;

code_below_256:
{
    if (codep > free_entp)
        goto error_code;
    free_entp->next = oldcodep;
    free_entp->firstchar = oldcodep->firstchar;
    free_entp->length = (uint16_t)(oldcodep->length + 1);
    free_entp->value = (uint8_t)code;
    free_entp->repeated =
        (bool)(oldcodep->repeated & (oldcodep->value == code));
    if (++free_entp > maxcodep)
    {
        if (++nbits > BITS_MAX) /* should not happen for a conformant encoder */
            nbits = BITS_MAX;
        nbitsmask = MAXCODE(nbits);
        maxcodep = dec_codetab + nbitsmask - 1;
        if (free_entp >= &dec_codetab[CSIZE])
        {
            /* At that point, the next valid states are either EOI or a
             */
            /* CODE_CLEAR. If a regular code is read, at the next */
            /* attempt at registering a new entry, we will error out */
            /* due to setting free_entp before any valid code */
            free_entp = dec_codetab - 1;
        }
    }
    oldcodep = codep;
    *op++ = (uint8_t)code;
    occ--;
    if (occ == 0)
        goto after_loop;
    goto begin;
}

code_above_or_equal_to_258:
{
    /*
     * Add the new entry to the code table.
     */

    if (codep >= free_entp)
    {
        if (codep != free_entp)
            goto error_code;
        free_entp->value = oldcodep->firstchar;
    }
    else
    {
        free_entp->value = codep->firstchar;
    }
    free_entp->repeated =
        (bool)(oldcodep->repeated & (oldcodep->value == free_entp->value));
    free_entp->next = oldcodep;

    free_entp->firstchar = oldcodep->firstchar;
    free_entp->length = (uint16_t)(oldcodep->length + 1);
    if (++free_entp > maxcodep)
    {
        if (++nbits > BITS_MAX) /* should not happen for a conformant encoder */
            nbits = BITS_MAX;
        nbitsmask = MAXCODE(nbits);
        maxcodep = dec_codetab + nbitsmask - 1;
        if (free_entp >= &dec_codetab[CSIZE])
        {
            /* At that point, the next valid states are either EOI or a
             */
            /* CODE_CLEAR. If a regular code is read, at the next */
            /* attempt at registering a new entry, we will error out */
            /* due to setting free_entp before any valid code */
            free_entp = dec_codetab - 1;
        }
    }
    oldcodep = codep;

    /*
     * Code maps to a string, copy string
     * value to output (written in reverse).
     */
    /* tiny bit faster on x86_64 to store in unsigned short than int */
    unsigned short len = codep->length;

    if (len < 3) /* equivalent to len == 2 given all other conditions */
    {
        if (occ <= 2)
        {
            if (occ == 2)
            {
                memcpy(op, &(codep->firstchar), 2);
                op += 2;
                occ -= 2;
                goto after_loop;
            }
            goto too_short_buffer;
        }

        memcpy(op, &(codep->firstchar), 2);
        op += 2;
        occ -= 2;
        goto begin; /* we can save the comparison occ > 0 */
    }

    if (len == 3)
    {
        if (occ <= 3)
        {
            if (occ == 3)
            {
                op[0] = codep->firstchar;
                op[1] = codep->next->value;
                op[2] = codep->value;
                op += 3;
                occ -= 3;
                goto after_loop;
            }
            goto too_short_buffer;
        }

        op[0] = codep->firstchar;
        op[1] = codep->next->value;
        op[2] = codep->value;
        op += 3;
        occ -= 3;
        goto begin; /* we can save the comparison occ > 0 */
    }

    if (len > occ)
    {
        goto too_short_buffer;
    }

    if (codep->repeated)
    {
        memset(op, codep->value, len);
        op += len;
        occ -= len;
        if (occ == 0)
            goto after_loop;
        goto begin;
    }

    uint8_t *tp = op + len;

    assert(len >= 4);

    *--tp = codep->value;
    codep = codep->next;
    *--tp = codep->value;
    codep = codep->next;
    *--tp = codep->value;
    codep = codep->next;
    *--tp = codep->value;
    if (tp > op)
    {
        do
        {
            codep = codep->next;
            *--tp = codep->value;
        } while (tp > op);
    }

    assert(occ >= len);
    op += len;
    occ -= len;
    if (occ == 0)
        goto after_loop;
    goto begin;
}

code_clear:
{
    free_entp = dec_codetab + CODE_FIRST;
    nbits = BITS_MIN;
    nbitsmask = MAXCODE(BITS_MIN);
    maxcodep = dec_codetab + (unsigned long)nbitsmask - 1;
    do
    {
        GetNextCodeLZW();
    } while (code == CODE_CLEAR); /* consecutive CODE_CLEAR codes */
    if (code == CODE_EOI)
        goto after_loop;
    if (code > CODE_EOI)
    {
        goto error_code;
    }
    *op++ = (uint8_t)code;
    occ--;
    oldcodep = dec_codetab + code;
    if (occ == 0)
        goto after_loop;
    goto begin;
}
}

too_short_buffer:
{
    /*
     * String is too long for decode buffer,
     * locate portion that will fit, copy to
     * the decode buffer, and setup restart
     * logic for the next decoding call.
     */
    sp->dec_codep = codep;
    do
    {
        codep = codep->next;
    } while (codep->length > occ);

    sp->dec_restart = occ;
    uint8_t *tp = op + occ;
    do
    {
        *--tp = codep->value;
        codep = codep->next;
    } while (--occ);
}

after_loop:
    tif->tif_rawcc -= (tmsize_t)((uint8_t *)bp - tif->tif_rawcp);
    tif->tif_rawcp = (uint8_t *)bp;
    sp->old_tif_rawcc = tif->tif_rawcc;
    sp->dec_bitsleft = dec_bitsleft;
    sp->lzw_nbits = (unsigned short)nbits;
    sp->lzw_nextdata = nextdata;
    sp->lzw_nextbits = nextbits;
    sp->dec_nbitsmask = nbitsmask;
    sp->dec_oldcodep = oldcodep;
    sp->dec_free_entp = free_entp;
    sp->dec_maxcodep = maxcodep;

    if (occ > 0)
    {
        memset(op, 0, (size_t)occ);
        sp->read_error = 1;
        TIFFErrorExtR(tif, module,
                      "Not enough data at scanline %" PRIu32 " (short %" PRIu64
                      " bytes)",
                      tif->tif_dir.td_row, (uint64_t)occ);
        return (0);
    }
    return (1);

no_eoi:
    memset(op, 0, (size_t)occ);
    sp->read_error = 1;
    TIFFErrorExtR(tif, module,
                  "LZWDecode: Strip %" PRIu32 " not terminated with EOI code",
                  tif->tif_dir.td_curstrip);
    return 0;
error_code:
    memset(op, 0, (size_t)occ);
    sp->read_error = 1;
    TIFFErrorExtR(tif, tif->tif_name, "Using code not yet in table");
    return 0;
}

#ifdef LZW_COMPAT

/*
 * This check shouldn't be necessary because each
 * strip is suppose to be terminated with CODE_EOI.
 */
#define NextCode(_tif, _sp, _bp, _code, _get, dec_bitsleft)                    \
    {                                                                          \
        if (dec_bitsleft < (uint64_t)nbits)                                    \
        {                                                                      \
            TIFFWarningExtR(_tif, module,                                      \
                            "LZWDecode: Strip %" PRIu32                        \
                            " not terminated with EOI code",                   \
                            _tif->tif_dir.td_curstrip);                        \
            _code = CODE_EOI;                                                  \
        }                                                                      \
        else                                                                   \
        {                                                                      \
            _get(_sp, _bp, _code);                                             \
            dec_bitsleft -= (uint64_t)nbits;                                   \
        }                                                                      \
    }

/*
 * Decode a "hunk of data" for old images.
 */
#define GetNextCodeCompat(sp, bp, code)                                        \
    {                                                                          \
        nextdata |= (unsigned long)*(bp)++ << nextbits;                        \
        nextbits += 8;                                                         \
        if (nextbits < nbits)                                                  \
        {                                                                      \
            nextdata |= (unsigned long)*(bp)++ << nextbits;                    \
            nextbits += 8;                                                     \
        }                                                                      \
        code = (hcode_t)(nextdata & (unsigned long)nbitsmask);                 \
        nextdata >>= nbits;                                                    \
        nextbits -= nbits;                                                     \
    }

/**
 * @brief Expand historical reversed-order LZW codes into caller output.
 * @ingroup libtiff_private_lzw
 *
 * Requires compatibility predecode selection, nonnegative output extent and
 * valid dictionary/raw state. Maintains restart, bit and dictionary
 * progress across calls. Corrupt dictionary codes and zero-length strings
 * clear the entire requested output (including bytes already decoded),
 * latch read_error, diagnose and return zero without committing local
 * cursor state. Running out of input bits is treated as an end code with
 * only a warning; if output is then short, the unfilled tail is cleared,
 * read_error latched and zero returned. While read_error is latched,
 * further calls clear the requested output and fail until predecode resets
 * it. No allocation/client I/O or ownership transfer.
 *
 * @param[in,out] tif Non-NULL live LZW handle with valid state, raw-buffer
 * cursors and matching geometry; serialise access.
 * @param[out] op0 Borrowed writable output of occ0 bytes, disjoint from raw
 * input and codec tables.
 * @param[in] occ0 Nonnegative requested decoded bytes; use a positive
 * extent during an interrupted-string restart.
 * @param[in] s Unused sample plane; predictor wrappers manage channel
 * stride.
 * @return One when requested output is filled, zero on detected decoding
 * error.
 */
static int LZWDecodeCompat(TIFF *tif, uint8_t *op0, tmsize_t occ0, uint16_t s)
{
    static const char module[] = "LZWDecodeCompat";
    LZWCodecState *sp = LZWDecoderState(tif);
    uint8_t *op = (uint8_t *)op0;
    tmsize_t occ = occ0;
    uint8_t *tp;
    uint8_t *bp;
    int code, nbits;
    int len;
    long nextbits, nbitsmask;
    WordType nextdata;
    code_t *codep, *free_entp, *maxcodep, *oldcodep;

    (void)s;
    assert(sp != NULL);

    if (sp->read_error)
    {
        memset(op, 0, (size_t)occ);
        TIFFErrorExtR(tif, module,
                      "LZWDecodeCompat: Scanline %" PRIu32
                      " cannot be read due to previous error",
                      tif->tif_dir.td_row);
        return 0;
    }

    /*
     * Restart interrupted output operation.
     */
    if (sp->dec_restart)
    {
        tmsize_t residue;

        codep = sp->dec_codep;
        residue = codep->length - sp->dec_restart;
        if (residue > occ)
        {
            /*
             * Residue from previous decode is sufficient
             * to satisfy decode request.  Skip to the
             * start of the decoded string, place decoded
             * values in the output buffer, and return.
             */
            sp->dec_restart += occ;
            do
            {
                codep = codep->next;
            } while (--residue > occ);
            tp = op + occ;
            do
            {
                *--tp = codep->value;
                codep = codep->next;
            } while (--occ);
            return (1);
        }
        /*
         * Residue satisfies only part of the decode request.
         */
        op += residue;
        occ -= residue;
        tp = op;
        do
        {
            *--tp = codep->value;
            codep = codep->next;
        } while (--residue);
        sp->dec_restart = 0;
    }

    bp = (uint8_t *)tif->tif_rawcp;

    sp->dec_bitsleft +=
        (((uint64_t)tif->tif_rawcc - (uint64_t)sp->old_tif_rawcc) << 3);
    uint64_t dec_bitsleft = sp->dec_bitsleft;

    nbits = sp->lzw_nbits;
    nextdata = sp->lzw_nextdata;
    nextbits = sp->lzw_nextbits;
    nbitsmask = sp->dec_nbitsmask;
    oldcodep = sp->dec_oldcodep;
    free_entp = sp->dec_free_entp;
    maxcodep = sp->dec_maxcodep;

    while (occ > 0)
    {
        NextCode(tif, sp, bp, code, GetNextCodeCompat, dec_bitsleft);
        if (code == CODE_EOI)
            break;
        if (code == CODE_CLEAR)
        {
            do
            {
                free_entp = sp->dec_codetab + CODE_FIRST;
                _TIFFmemset(free_entp, 0,
                            (CSIZE - CODE_FIRST) * sizeof(code_t));
                nbits = BITS_MIN;
                nbitsmask = MAXCODE(BITS_MIN);
                maxcodep = sp->dec_codetab + nbitsmask;
                NextCode(tif, sp, bp, code, GetNextCodeCompat, dec_bitsleft);
            } while (code == CODE_CLEAR); /* consecutive CODE_CLEAR codes */
            if (code == CODE_EOI)
                break;
            if (code > CODE_CLEAR)
            {
                memset(op0, 0, (size_t)occ0);
                sp->read_error = 1;
                TIFFErrorExtR(
                    tif, tif->tif_name,
                    "LZWDecode: Corrupted LZW table at scanline %" PRIu32,
                    tif->tif_dir.td_row);
                return (0);
            }
            *op++ = (uint8_t)code;
            occ--;
            oldcodep = sp->dec_codetab + code;
            continue;
        }
        codep = sp->dec_codetab + code;

        /*
         * Add the new entry to the code table.
         */
        if (free_entp < &sp->dec_codetab[0] ||
            free_entp >= &sp->dec_codetab[CSIZE])
        {
            memset(op0, 0, (size_t)occ0);
            sp->read_error = 1;
            TIFFErrorExtR(tif, module,
                          "Corrupted LZW table at scanline %" PRIu32,
                          tif->tif_dir.td_row);
            return (0);
        }

        free_entp->next = oldcodep;
        if (free_entp->next < &sp->dec_codetab[0] ||
            free_entp->next >= &sp->dec_codetab[CSIZE])
        {
            memset(op0, 0, (size_t)occ0);
            sp->read_error = 1;
            TIFFErrorExtR(tif, module,
                          "Corrupted LZW table at scanline %" PRIu32,
                          tif->tif_dir.td_row);
            return (0);
        }
        free_entp->firstchar = free_entp->next->firstchar;
        free_entp->length = (uint16_t)(free_entp->next->length + 1);
        free_entp->value =
            (codep < free_entp) ? codep->firstchar : free_entp->firstchar;
        if (++free_entp > maxcodep)
        {
            if (++nbits > BITS_MAX) /* should not happen */
                nbits = BITS_MAX;
            nbitsmask = MAXCODE(nbits);
            maxcodep = sp->dec_codetab + nbitsmask;
        }
        oldcodep = codep;
        if (code >= 256)
        {
            /*
             * Code maps to a string, copy string
             * value to output (written in reverse).
             */
            if (codep->length == 0)
            {
                memset(op0, 0, (size_t)occ0);
                sp->read_error = 1;
                TIFFErrorExtR(
                    tif, module,
                    "Wrong length of decoded "
                    "string: data probably corrupted at scanline %" PRIu32,
                    tif->tif_dir.td_row);
                return (0);
            }
            if (codep->length > occ)
            {
                /*
                 * String is too long for decode buffer,
                 * locate portion that will fit, copy to
                 * the decode buffer, and setup restart
                 * logic for the next decoding call.
                 */
                sp->dec_codep = codep;
                do
                {
                    codep = codep->next;
                } while (codep->length > occ);
                sp->dec_restart = occ;
                tp = op + occ;
                do
                {
                    *--tp = codep->value;
                    codep = codep->next;
                } while (--occ);
                break;
            }
            len = codep->length;
            tp = op + len;
            do
            {
                *--tp = codep->value;
                codep = codep->next;
            } while (codep && tp > op);
            assert(occ >= len);
            op += len;
            occ -= len;
        }
        else
        {
            *op++ = (uint8_t)code;
            occ--;
        }
    }

    tif->tif_rawcc -= (tmsize_t)((uint8_t *)bp - tif->tif_rawcp);
    tif->tif_rawcp = (uint8_t *)bp;

    sp->old_tif_rawcc = tif->tif_rawcc;
    sp->dec_bitsleft = dec_bitsleft;

    sp->lzw_nbits = (unsigned short)nbits;
    sp->lzw_nextdata = nextdata;
    sp->lzw_nextbits = nextbits;
    sp->dec_nbitsmask = nbitsmask;
    sp->dec_oldcodep = oldcodep;
    sp->dec_free_entp = free_entp;
    sp->dec_maxcodep = maxcodep;

    if (occ > 0)
    {
        memset(op, 0, (size_t)occ);
        sp->read_error = 1;
        TIFFErrorExtR(tif, module,
                      "Not enough data at scanline %" PRIu32 " (short %" PRIu64
                      " bytes)",
                      tif->tif_dir.td_row, (uint64_t)occ);
        return (0);
    }
    return (1);
}
#endif /* LZW_COMPAT */

#ifndef LZW_READ_ONLY

static void cl_hash(LZWCodecState *);

/*
 * LZW Encoding.
 */

/**
 * @brief Allocate the LZW encoder hash table.
 * @ingroup libtiff_private_lzw
 *
 * Requires existing codec state and no previous unreleased encoder table;
 * assigns a new handle-owned HSIZE allocation. Diagnoses allocation
 * failure; no pixel I/O. Cleanup releases the table.
 *
 * @param[in,out] tif Non-NULL live LZW handle with valid state, raw-buffer
 * cursors and matching geometry; serialise access.
 * @return One after allocation, zero on failure.
 */
static int LZWSetupEncode(TIFF *tif)
{
    static const char module[] = "LZWSetupEncode";
    LZWCodecState *sp = LZWEncoderState(tif);

    assert(sp != NULL);
    sp->enc_hashtab = (hash_t *)_TIFFmallocExt(tif, HSIZE * sizeof(hash_t));
    if (sp->enc_hashtab == NULL)
    {
        TIFFErrorExtR(tif, module, "No space for LZW hash table");
        return (0);
    }
    return (1);
}

/*
 * Reset encoding state at the start of a strip.
 */
/**
 * @brief Reset the LZW encoder dictionary bit cache and counters.
 * @ingroup libtiff_private_lzw
 *
 * Requires valid outer state, allocated hash table and raw output capacity
 * of at least five bytes plus usable payload room. If table is NULL it
 * invokes setup but does not check its return before clearing the table;
 * caller must ensure setup succeeds. Resets code widths, checkpoint,
 * pending code and output limit. Does not emit payload directly; setup can
 * allocate/diagnose.
 *
 * @param[in,out] tif Non-NULL live LZW handle with valid state, raw-buffer
 * cursors and matching geometry; serialise access.
 * @param[in] s Unused sample plane; predictor wrappers manage channel
 * stride.
 * @return One unconditionally after reset; not independent validation of
 * setup success.
 */
static int LZWPreEncode(TIFF *tif, uint16_t s)
{
    LZWCodecState *sp = LZWEncoderState(tif);

    (void)s;
    assert(sp != NULL);

    if (sp->enc_hashtab == NULL)
    {
        tif->tif_setupencode(tif);
    }

    sp->lzw_nbits = BITS_MIN;
    sp->lzw_maxcode = MAXCODE(BITS_MIN);
    sp->lzw_free_ent = CODE_FIRST;
    sp->lzw_nextbits = 0;
    sp->lzw_nextdata = 0;
    sp->enc_checkpoint = CHECK_GAP;
    sp->enc_ratio = 0;
    sp->enc_incount = 0;
    sp->enc_outcount = 0;
    /*
     * The 4 here insures there is space for 2 max-sized
     * codes in LZWEncode and LZWPostDecode.
     */
    sp->enc_rawlimit = tif->tif_rawdata + tif->tif_rawdatasize - 1 - 4;
    cl_hash(sp);                   /* clear hash table */
    sp->enc_oldcode = (hcode_t)-1; /* generates CODE_CLEAR in LZWEncode */
    return (1);
}

#define CALCRATIO(sp, rat)                                                     \
    {                                                                          \
        if (incount > 0x007fffff)                                              \
        { /* NB: shift will overflow */                                        \
            rat = outcount >> 8;                                               \
            rat = (rat == 0 ? 0x7fffffff : incount / rat);                     \
        }                                                                      \
        else                                                                   \
            rat = (incount << 8) / outcount;                                   \
    }

/* Explicit 0xff masking to make icc -check=conversions happy */
#define PutNextCode(op, c)                                                     \
    {                                                                          \
        nextdata = (nextdata << nbits) | c;                                    \
        nextbits += nbits;                                                     \
        *op++ = (unsigned char)((nextdata >> (nextbits - 8)) & 0xff);          \
        nextbits -= 8;                                                         \
        if (nextbits >= 8)                                                     \
        {                                                                      \
            *op++ = (unsigned char)((nextdata >> (nextbits - 8)) & 0xff);      \
            nextbits -= 8;                                                     \
        }                                                                      \
        outcount += nbits;                                                     \
    }

/*
 * Encode a chunk of pixels.
 *
 * Uses an open addressing double hashing (no chaining) on the
 * prefix code/next character combination.  We do a variant of
 * Knuth's algorithm D (vol. 3, sec. 6.4) along with G. Knott's
 * relatively-prime secondary probe.  Here, the modular division
 * first probe is gives way to a faster exclusive-or manipulation.
 * Also do block compression with an adaptive reset, whereby the
 * code table is cleared when the compression ratio decreases,
 * but after the table fills.  The variable-length output codes
 * are re-sized at this point, and a CODE_CLEAR is generated
 * for the decoder.
 */
/**
 * @brief Compress borrowed bytes into buffered TIFF LZW codes.
 * @ingroup libtiff_private_lzw
 *
 * Requires successful encoder setup/preencode, nonnegative input length and
 * usable raw capacity/limit. Updates code/hash state, emits clear codes and
 * adjusts code width; ratio checkpoints can reset the dictionary. Flushes
 * when needed, invoking client I/O and diagnostics. Input is not changed.
 * Early flush failure can leave output bytes, dictionary and local/cached
 * progress inconsistent for retry; no rollback or ownership transfer.
 *
 * @param[in,out] tif Non-NULL live LZW handle with valid state, raw-buffer
 * cursors and matching geometry; serialise access.
 * @param[in] bp Borrowed readable cc-byte input, not retained.
 * @param[in] cc Nonnegative input byte count.
 * @param[in] s Unused sample plane; predictor wrappers manage channel
 * stride.
 * @return One after encoding, zero for missing state or flush failure.
 */
static int LZWEncode(TIFF *tif, uint8_t *bp, tmsize_t cc, uint16_t s)
{
    LZWCodecState *sp = LZWEncoderState(tif);
    long fcode;
    hash_t *hp;
    int h, c;
    hcode_t ent;
    long disp;
    tmsize_t incount, outcount, checkpoint;
    WordType nextdata;
    long nextbits;
    int free_ent, maxcode, nbits;
    uint8_t *op;
    uint8_t *limit;

    (void)s;
    if (sp == NULL)
        return (0);

    assert(sp->enc_hashtab != NULL);

    /*
     * Load local state.
     */
    incount = sp->enc_incount;
    outcount = sp->enc_outcount;
    checkpoint = sp->enc_checkpoint;
    nextdata = sp->lzw_nextdata;
    nextbits = sp->lzw_nextbits;
    free_ent = sp->lzw_free_ent;
    maxcode = sp->lzw_maxcode;
    nbits = sp->lzw_nbits;
    op = tif->tif_rawcp;
    limit = sp->enc_rawlimit;
    ent = (hcode_t)sp->enc_oldcode;

    if (ent == (hcode_t)-1 && cc > 0)
    {
        /*
         * NB: This is safe because it can only happen
         *     at the start of a strip where we know there
         *     is space in the data buffer.
         */
        PutNextCode(op, CODE_CLEAR);
        ent = *bp++;
        cc--;
        incount++;
    }
    while (cc > 0)
    {
        c = *bp++;
        cc--;
        incount++;
        fcode = ((long)c << BITS_MAX) + ent;
        h = (c << HSHIFT) ^ ent; /* xor hashing */
        hp = &sp->enc_hashtab[h];
        if (hp->hash == fcode)
        {
            ent = hp->code;
            continue;
        }
        if (hp->hash >= 0)
        {
            /*
             * Primary hash failed, check secondary hash.
             */
            disp = HSIZE - h;
            if (h == 0)
                disp = 1;
            do
            {
                /*
                 * Avoid pointer arithmetic because of
                 * wraparound problems with segments.
                 */
                if ((h -= (int)disp) < 0)
                    h += HSIZE;
                hp = &sp->enc_hashtab[h];
                if (hp->hash == fcode)
                {
                    ent = hp->code;
                    goto hit;
                }
            } while (hp->hash >= 0);
        }
        /*
         * New entry, emit code and add to table.
         */
        /*
         * Verify there is space in the buffer for the code
         * and any potential Clear code that might be emitted
         * below.  The value of limit is setup so that there
         * are at least 4 bytes free--room for 2 codes.
         */
        if (op > limit)
        {
            tif->tif_rawcc = (tmsize_t)(op - tif->tif_rawdata);
            if (!TIFFFlushData1(tif))
                return 0;
            op = tif->tif_rawdata;
        }
        PutNextCode(op, ent);
        ent = (hcode_t)c;
        hp->code = (hcode_t)(free_ent++);
        hp->hash = fcode;
        if (free_ent == CODE_MAX - 1)
        {
            /* table is full, emit clear code and reset */
            cl_hash(sp);
            sp->enc_ratio = 0;
            incount = 0;
            outcount = 0;
            free_ent = CODE_FIRST;
            PutNextCode(op, CODE_CLEAR);
            nbits = BITS_MIN;
            maxcode = MAXCODE(BITS_MIN);
        }
        else
        {
            /*
             * If the next entry is going to be too big for
             * the code size, then increase it, if possible.
             */
            if (free_ent > maxcode)
            {
                nbits++;
                assert(nbits <= BITS_MAX);
                maxcode = (int)MAXCODE(nbits);
            }
            else if (incount >= checkpoint)
            {
                tmsize_t rat;
                /*
                 * Check compression ratio and, if things seem
                 * to be slipping, clear the hash table and
                 * reset state.  The compression ratio is a
                 * 24+8-bit fractional number.
                 */
                checkpoint = incount + CHECK_GAP;
                CALCRATIO(sp, rat);
                if (rat <= sp->enc_ratio)
                {
                    cl_hash(sp);
                    sp->enc_ratio = 0;
                    incount = 0;
                    outcount = 0;
                    free_ent = CODE_FIRST;
                    PutNextCode(op, CODE_CLEAR);
                    nbits = BITS_MIN;
                    maxcode = MAXCODE(BITS_MIN);
                }
                else
                    sp->enc_ratio = rat;
            }
        }
    hit:;
    }

    /*
     * Restore global state.
     */
    sp->enc_incount = incount;
    sp->enc_outcount = outcount;
    sp->enc_checkpoint = checkpoint;
    sp->enc_oldcode = ent;
    sp->lzw_nextdata = nextdata;
    sp->lzw_nextbits = nextbits;
    sp->lzw_free_ent = (unsigned short)free_ent;
    sp->lzw_maxcode = (unsigned short)maxcode;
    sp->lzw_nbits = (unsigned short)nbits;
    tif->tif_rawcp = op;
    return (1);
}

/*
 * Finish off an encoded strip by flushing the last
 * string and tacking on an End Of Information code.
 */
/**
 * @brief Emit the final LZW string end code and padding bits.
 * @ingroup libtiff_private_lzw
 *
 * Requires valid encoder state and reserved raw capacity. Can flush first,
 * emit clear/width changes, append EOI and pad the last byte, then set
 * rawcc. Does not persist the IFD or guarantee final buffered bytes were
 * written. Raw/file state can change before failed flush; no ownership
 * transfer.
 *
 * @param[in,out] tif Non-NULL live LZW handle with valid state, raw-buffer
 * cursors and matching geometry; serialise access.
 * @return One after final codes buffered, zero on flush failure.
 */
static int LZWPostEncode(TIFF *tif)
{
    LZWCodecState *sp = LZWEncoderState(tif);
    uint8_t *op = tif->tif_rawcp;
    long nextbits = sp->lzw_nextbits;
    WordType nextdata = sp->lzw_nextdata;
    tmsize_t outcount = sp->enc_outcount;
    int nbits = sp->lzw_nbits;

    if (op > sp->enc_rawlimit)
    {
        tif->tif_rawcc = (tmsize_t)(op - tif->tif_rawdata);
        if (!TIFFFlushData1(tif))
            return 0;
        op = tif->tif_rawdata;
    }
    if (sp->enc_oldcode != (hcode_t)-1)
    {
        int free_ent = sp->lzw_free_ent;

        PutNextCode(op, (WordType)sp->enc_oldcode);
        sp->enc_oldcode = (hcode_t)-1;
        free_ent++;

        if (free_ent == CODE_MAX - 1)
        {
            /* table is full, emit clear code and reset */
            outcount = 0;
            PutNextCode(op, CODE_CLEAR);
            nbits = BITS_MIN;
        }
        else
        {
            /*
             * If the next entry is going to be too big for
             * the code size, then increase it, if possible.
             */
            if (free_ent > sp->lzw_maxcode)
            {
                nbits++;
                assert(nbits <= BITS_MAX);
            }
        }
    }
    PutNextCode(op, CODE_EOI);
    /* Explicit 0xff masking to make icc -check=conversions happy */
    if (nextbits > 0)
        *op++ = (unsigned char)((nextdata << (8 - nextbits)) & 0xff);
    tif->tif_rawcc = (tmsize_t)(op - tif->tif_rawdata);
    (void)outcount;
    return (1);
}

/*
 * Reset encoding hash table.
 */
/**
 * @brief Mark every LZW encoder hash slot unused.
 * @ingroup libtiff_private_lzw
 *
 * Requires a live HSIZE-entry table. Sets hash fields to minus one; code
 * fields need not be cleared. No allocation, I/O, diagnostics, ownership
 * changes or error status.
 *
 * @param[in,out] sp Non-NULL codec state owning an allocated HSIZE-entry
 * encoder table.
 */
static void cl_hash(LZWCodecState *sp)
{
    hash_t *hp = &sp->enc_hashtab[HSIZE - 1];
    long i = HSIZE - 8;

    do
    {
        i -= 8;
        hp[-7].hash = -1;
        hp[-6].hash = -1;
        hp[-5].hash = -1;
        hp[-4].hash = -1;
        hp[-3].hash = -1;
        hp[-2].hash = -1;
        hp[-1].hash = -1;
        hp[0].hash = -1;
        hp -= 8;
    } while (i >= 0);
    for (i += 8; i > 0; i--, hp--)
        hp->hash = -1;
}

#endif

/**
 * @brief Release LZW tables and codec state and reset methods.
 * @ingroup libtiff_private_lzw
 *
 * Requires live initialised LZW state. Restores predictor chains, frees
 * dictionary and hash table if present, frees outer state and sets tif_data
 * NULL, then installs default compression methods. Invalidates all
 * state/table views. Does not flush payload or close the borrowed handle;
 * no separate status result.
 *
 * @param[in,out] tif Non-NULL live LZW handle with valid state, raw-buffer
 * cursors and matching geometry; serialise access.
 */
static void LZWCleanup(TIFF *tif)
{
    (void)TIFFPredictorCleanup(tif);

    assert(tif->tif_data != NULL);

    if (LZWDecoderState(tif)->dec_codetab)
        _TIFFfreeExt(tif, LZWDecoderState(tif)->dec_codetab);

    if (LZWEncoderState(tif)->enc_hashtab)
        _TIFFfreeExt(tif, LZWEncoderState(tif)->enc_hashtab);

    _TIFFfreeExt(tif, tif->tif_data);
    tif->tif_data = NULL;

    _TIFFSetDefaultCompressionState(tif);
}

/**
 * @brief Report the empirical LZW expansion heuristic.
 * @ingroup libtiff_private_lzw
 *
 * Ignores the handle; no allocation, I/O, diagnostics, state or ownership
 * effects. Not a universal compression bound or data-validation guarantee.
 *
 * @param[in] tif Unused handle.
 * @return One thousand four hundred.
 */
static uint64_t LZWGetMaxCompressionRatio(TIFF *tif)
{
    (void)tif;

    /* See README_for_libtiff_developpers.md for raw data used to estimate
     * the maximum compression rate. */

    /* 1024x1024: 562 */
    /* 4096x4096: 1243 */
    /* 16383x16383: 1353 */
    /* 65536x65536: 1362 */

    return 1400;
}

int TIFFInitLZW(TIFF *tif, int scheme)
{
    static const char module[] = "TIFFInitLZW";
    (void)scheme;
    assert(scheme == COMPRESSION_LZW);
    /*
     * Allocate state block so tag methods have storage to record values.
     */
    tif->tif_data = (uint8_t *)_TIFFmallocExt(tif, sizeof(LZWCodecState));
    if (tif->tif_data == NULL)
        goto bad;
    LZWDecoderState(tif)->dec_codetab = NULL;
    LZWDecoderState(tif)->dec_decode = NULL;
    LZWEncoderState(tif)->enc_hashtab = NULL;
    LZWState(tif)->rw_mode = tif->tif_mode;

    /*
     * Install codec methods.
     */
    tif->tif_fixuptags = LZWFixupTags;
    tif->tif_setupdecode = LZWSetupDecode;
    tif->tif_predecode = LZWPreDecode;
    tif->tif_decoderow = LZWDecode;
    tif->tif_decodestrip = LZWDecode;
    tif->tif_decodetile = LZWDecode;
#ifndef LZW_READ_ONLY
    tif->tif_setupencode = LZWSetupEncode;
    tif->tif_preencode = LZWPreEncode;
    tif->tif_postencode = LZWPostEncode;
    tif->tif_encoderow = LZWEncode;
    tif->tif_encodestrip = LZWEncode;
    tif->tif_encodetile = LZWEncode;
#endif
    tif->tif_getmaxcompressionratio = LZWGetMaxCompressionRatio;
    tif->tif_cleanup = LZWCleanup;
    /*
     * Setup predictor setup.
     */
    (void)TIFFPredictorInit(tif);
    return (1);
bad:
    TIFFErrorExtR(tif, module, "No space for LZW state block");
    return (0);
}

/*
 * Copyright (c) 1985, 1986 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * James A. Woods, derived from original work by Spencer Thomas
 * and Joseph Orost.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */
#endif /* LZW_SUPPORT */

/**
 * @def SIZEOF_WORDTYPE
 * @brief Size in bytes of WordType, the decoder bit-buffer word.
 * @ingroup libtiff_private_lzw
 *
 * Equals SIZEOF_SIZE_T; selects the GetNextData() variant and how many
 * bytes GetNextCodeLZW() loads at once (4 or 8).
 */

/**
 * @def LZW_COMPAT
 * @brief Include decoding of historical reversed-bit-order LZW data.
 * @ingroup libtiff_private_lzw
 *
 * Defined unconditionally. Enables LZWDecodeCompat(), its detection in
 * LZWPreDecode() and 1024 extra decode-table entries in CSIZE.
 */

/**
 * @def MAXCODE(n)
 * @brief Compute the inclusive maximum code for a valid bit width.
 * @ingroup libtiff_private_lzw
 *
 * Expands to ((1 << n) - 1); pure, with n evaluated once.
 *
 * @param[in] n Code width in bits.
 */

/**
 * @def BITS_MIN
 * @brief Initial and post-clear code width: 9 bits.
 * @ingroup libtiff_private_lzw
 */

/**
 * @def BITS_MAX
 * @brief Maximum code width allowed by TIFF LZW: 12 bits.
 * @ingroup libtiff_private_lzw
 */

/**
 * @def CODE_CLEAR
 * @brief Reserved code 256 that resets the string table.
 * @ingroup libtiff_private_lzw
 */

/**
 * @def CODE_EOI
 * @brief Reserved code 257 that ends the strip or tile.
 * @ingroup libtiff_private_lzw
 */

/**
 * @def CODE_FIRST
 * @brief First dictionary code assigned to a new string: 258.
 * @ingroup libtiff_private_lzw
 */

/**
 * @def CODE_MAX
 * @brief Largest 12-bit code: MAXCODE(BITS_MAX), i.e. 4095.
 * @ingroup libtiff_private_lzw
 */

/**
 * @def HSIZE
 * @brief Number of entries in the encoder hash table: 9001.
 * @ingroup libtiff_private_lzw
 *
 * Upstream notes this as 91% occupancy. Indexed with HSHIFT-mixed keys
 * and probed with a secondary displacement.
 */

/**
 * @def HSHIFT
 * @brief Left shift applied to the input byte when hashing encoder strings.
 * @ingroup libtiff_private_lzw
 *
 * Value 5 (13 - 8), so (byte << HSHIFT) ^ code stays within 13 bits.
 */

/**
 * @def CSIZE
 * @brief Number of entries allocated for the decoder string table.
 * @ingroup libtiff_private_lzw
 *
 * MAXCODE(BITS_MAX) + 1024 with LZW_COMPAT (headroom for old files),
 * otherwise MAXCODE(BITS_MAX) + 1.
 */

/**
 * @def lzw_nbits
 * @brief Member alias for base.nbits, the current code width.
 * @ingroup libtiff_private_lzw
 */

/**
 * @def lzw_maxcode
 * @brief Member alias for base.maxcode, the largest code at this width.
 * @ingroup libtiff_private_lzw
 */

/**
 * @def lzw_free_ent
 * @brief Member alias for base.free_ent, the next free dictionary code.
 * @ingroup libtiff_private_lzw
 */

/**
 * @def lzw_nextdata
 * @brief Member alias for base.nextdata, the buffered code bits.
 * @ingroup libtiff_private_lzw
 */

/**
 * @def lzw_nextbits
 * @brief Member alias for base.nextbits, the number of buffered bits.
 * @ingroup libtiff_private_lzw
 */

/**
 * @def CHECK_GAP
 * @brief Encoder input bytes between compression-ratio checks: 10000.
 * @ingroup libtiff_private_lzw
 *
 * If the ratio has not improved since the previous check, the encoder emits
 * CODE_CLEAR and resets the table.
 */

/**
 * @def LZWState(tif)
 * @brief Cast tif->tif_data to the shared LZWBaseState prefix.
 * @ingroup libtiff_private_lzw
 *
 * Pure cast; tif is evaluated once. Requires the LZW codec to be
 * initialised (tif_data non-NULL).
 *
 * @param[in] tif Handle whose codec state is returned.
 */

/**
 * @def LZWDecoderState(tif)
 * @brief Cast tif->tif_data to the full LZWCodecState for decoding.
 * @ingroup libtiff_private_lzw
 *
 * Pure cast via LZWState(); tif is evaluated once. Requires the LZW codec to
 * be initialised.
 *
 * @param[in] tif Handle whose codec state is returned.
 */

/**
 * @def LZWEncoderState(tif)
 * @brief Cast tif->tif_data to the full LZWCodecState for encoding.
 * @ingroup libtiff_private_lzw
 *
 * Pure cast via LZWState(); tif is evaluated once. Requires the LZW codec to
 * be initialised.
 *
 * @param[in] tif Handle whose codec state is returned.
 */

/**
 * @def GetNextData(nextdata, bp)
 * @brief Load SIZEOF_WORDTYPE bytes at bp into nextdata, first byte most
 * significant.
 * @ingroup libtiff_private_lzw
 *
 * Statement macro used by GetNextCodeLZW() to refill the bit cache with a
 * whole word. Assigns nextdata and reads SIZEOF_WORDTYPE bytes from bp
 * without a bounds check; does not advance bp. The portable variants
 * evaluate bp once per byte, and the memcpy/bswap variants expand to two
 * statements, so bp must be free of side effects and the macro must be used
 * inside braces. The _M_X64/_M_X86 variants dereference bp through a
 * wider pointer type, i.e. an unaligned load.
 *
 * @param[in,out] nextdata Unsigned WordType lvalue that receives the word.
 * @param[in] bp Byte pointer to at least SIZEOF_WORDTYPE readable bytes.
 */

/**
 * @def GetNextCodeLZW()
 * @brief Extract the next nbits-wide MSB-first code from the decoder bit
 * cache into code, refilling from bp as needed.
 * @ingroup libtiff_private_lzw
 *
 * Operates entirely on locals of LZWDecode(): reads nbits and nbitsmask,
 * reads and updates nextbits, nextdata, bp and dec_bitsleft, and assigns
 * code. When the cache runs short it loads a whole word with GetNextData()
 * if dec_bitsleft holds at least one word, otherwise one or two single bytes.
 * dec_bitsleft is reduced by the bits loaded, not by nbits.
 *
 * Hidden control flow: if fewer than 8 bits remain in dec_bitsleft when a
 * byte is needed, it executes `goto no_eoi`, so the enclosing function must
 * define a no_eoi label (LZWDecode() zeroes the output, sets read_error,
 * reports a missing EOI with TIFFErrorExtR() and returns 0 there). Its
 * internal `break` only leaves the macro's own do/while.
 */

/**
 * @def NextCode(_tif, _sp, _bp, _code, _get, dec_bitsleft)
 * @brief Fetch the next compatibility-mode code with _get, or yield CODE_EOI
 * with a warning when fewer than nbits input bits remain.
 * @ingroup libtiff_private_lzw
 *
 * Defined only with LZW_COMPAT and used by LZWDecodeCompat(). Also reads the
 * caller locals nbits and module. If dec_bitsleft < nbits it emits
 * TIFFWarningExtR() ("not terminated with EOI code") naming
 * _tif->tif_dir.td_curstrip, sets _code to CODE_EOI and consumes no input;
 * this is not treated as an error. Otherwise it expands _get(_sp, _bp, _code)
 * and subtracts nbits from dec_bitsleft. _tif is evaluated twice on the
 * warning path. No hidden return or goto.
 *
 * @param[in] _tif TIFF handle used for the warning.
 * @param[in] _sp Codec state passed through to _get.
 * @param[in,out] _bp Input byte-pointer lvalue, advanced by _get.
 * @param[in,out] _code Lvalue that receives the code.
 * @param[in] _get Code-fetch macro name, e.g. GetNextCodeCompat.
 * @param[in,out] dec_bitsleft Lvalue holding the remaining input bit count.
 */

/**
 * @def GetNextCodeCompat(sp, bp, code)
 * @brief Extract the next nbits-wide LSB-first code for old reversed-order
 * LZW data.
 * @ingroup libtiff_private_lzw
 *
 * ORs one byte, or two if nextbits is still below nbits, from *bp into the
 * caller locals nextdata/nextbits, advancing bp each time; then assigns
 * code = nextdata & nbitsmask and shifts nbits out of nextdata. Reads the
 * caller locals nbits and nbitsmask. Performs no bounds check: NextCode()
 * ensures dec_bitsleft >= nbits first. The sp operand is not used.
 *
 * @param[in] sp Unused; present for the NextCode() _get signature.
 * @param[in,out] bp Input byte-pointer lvalue, advanced by 1 or 2.
 * @param[in,out] code Lvalue that receives the code.
 */

/**
 * @def CALCRATIO(sp, rat)
 * @brief Compute the encoder compression ratio incount/outcount as a 24.8
 * fixed-point value into rat.
 * @ingroup libtiff_private_lzw
 *
 * Reads the LZWEncode() locals incount (input bytes) and outcount (output
 * bits); the sp operand is not used. If incount > 0x007fffff it divides
 * incount by outcount >> 8 to avoid overflowing the shift, giving 0x7fffffff
 * when that is 0; otherwise it computes (incount << 8) / outcount, which
 * requires outcount != 0. rat is assigned up to twice. No hidden control
 * flow or diagnostics.
 *
 * @param[in] sp Unused.
 * @param[in,out] rat Lvalue that receives the ratio.
 */

/**
 * @def PutNextCode(op, c)
 * @brief Append the nbits-wide code c MSB-first to the encoder output at op.
 * @ingroup libtiff_private_lzw
 *
 * Shifts c into the LZWEncode()/LZWPostEncode() locals nextdata and nextbits,
 * stores one or two complete bytes at op (advancing it each time) and adds
 * nbits to outcount. Writes without a bounds check: callers compare op with
 * enc_rawlimit (which reserves 4 bytes, room for two codes) and flush first.
 * c is evaluated once; op is read and incremented up to twice. No hidden
 * control flow or diagnostics.
 *
 * @param[in,out] op Output byte-pointer lvalue, advanced by 1 or 2.
 * @param[in] c Code value, below 1 << nbits.
 */

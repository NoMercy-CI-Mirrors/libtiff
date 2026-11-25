/**********************************************************************
 *
 * Name:     tif_hash_set.c
 * Purpose:  Hash set functions.
 * Author:   Even Rouault, <even dot rouault at spatialys.com>
 *
 **********************************************************************
 * Copyright (c) 2008-2009, Even Rouault <even dot rouault at spatialys.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 ****************************************************************************/

/**
 * @file tif_hash_set.c
 * @brief Implement callback-driven buckets, recycled nodes, and element
 * disposal.
 */

#include "tif_config.h"

#include "tif_hash_set.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/** @brief Name a private live/recycled hash-list node.
 * @ingroup libtiff_private_hash
 */
typedef struct _TIFFList TIFFList;

/**
 * @brief Link a live hash element or a recycled node.
 * @ingroup libtiff_private_hash
 *
 * The set owns nodes. Payload ownership follows the optional disposer;
 * recycled nodes can contain stale payload values that must not be used.
 */
struct _TIFFList
{
    /** @brief Live payload pointer, or stale/unused recycled value. */
    void *pData;
    /** @brief Next live-bucket or recycling-list node. */
    struct _TIFFList *psNext;
};

/**
 * @brief Own bucket/node storage and record callback-defined element policy.
 * @ingroup libtiff_private_hash
 *
 * No TIFF handle or accounting allocator is used. Access and callback lifetime
 * need external coordination; no internal lock or iteration mutation guard
 * exists.
 */
struct _TIFFHashSet
{
    /** @brief Borrowed hash function, with a default installed at creation. */
    TIFFHashSetHashFunc fnHashFunc;
    /**
     * @brief Borrowed equality function, with a default installed at creation.
     */
    TIFFHashSetEqualFunc fnEqualFunc;
    /**
     * @brief Optional consuming disposer, including for failed incoming
     * insertions.
     */
    TIFFHashSetFreeEltFunc fnFreeEltFunc;
    /** @brief Owned array of bucket-head pointers. */
    TIFFList **tabList;
    /** @brief Live-element count in normal externally visible state. */
    int nSize;
    /**
     * @brief Index into anPrimes; callers must keep growth/shrink selection in
     * range.
     */
    int nIndiceAllocatedSize;
    /** @brief Logical bucket count used for hashing and traversal. */
    int nAllocatedSize;
    /**
     * @brief Owned spare-node list, whose payload fields are not live elements.
     */
    TIFFList *psRecyclingList;
    /**
     * @brief Number of nodes in the spare-node list, capped at 128 on return.
     */
    int nRecyclingListSize;
    /** @brief Deferred rehash state used by optional deferred removal. */
    bool bRehash;
#ifdef HASH_DEBUG
    /** @brief Optional HASH_DEBUG collision counter. */
    int nCollisions;
#endif
};

/**
 * @brief Provide the fixed sequence of supported bucket capacities.
 * @ingroup libtiff_private_hash
 *
 * Growth indexes this array without an explicit end-of-table guard.
 */
static const int anPrimes[] = {
    53,        97,        193,       389,       769,       1543,     3079,
    6151,      12289,     24593,     49157,     98317,     196613,   393241,
    786433,    1572869,   3145739,   6291469,   12582917,  25165843, 50331653,
    100663319, 201326611, 402653189, 805306457, 1610612741};

/************************************************************************/
/*                       TIFFHashSetHashPointer()                       */
/************************************************************************/

/**
 * @brief Hash a pointer value without dereferencing it.
 * @ingroup libtiff_private_hash
 *
 * Does not allocate, mutate, dispose, or emit diagnostics. Collisions are
 * resolved by equality; the result is not a persistent identifier or
 * cryptographic hash.
 *
 * @param[in] elt Arbitrary pointer, including NULL.
 * @return The uintptr_t pointer representation converted to unsigned long;
 * high bits can be truncated when that type is narrower.
 */
static unsigned long TIFFHashSetHashPointer(const void *elt)
{
    return (unsigned long)(uintptr_t)((void *)(elt));
}

/************************************************************************/
/*                      TIFFHashSetEqualPointer()                       */
/************************************************************************/

/**
 * @brief Compare pointer values without dereferencing them.
 * @ingroup libtiff_private_hash
 *
 * Does not allocate, mutate, dispose, transfer ownership, or emit
 * diagnostics.
 *
 * @param[in] elt1 First pointer, possibly NULL.
 * @param[in] elt2 Second pointer, possibly NULL.
 * @return true if the pointers compare equal; false otherwise.
 */
static bool TIFFHashSetEqualPointer(const void *elt1, const void *elt2)
{
    return elt1 == elt2;
}

/************************************************************************/
/*                           TIFFHashSetNew()                           */
/************************************************************************/

TIFFHashSet *TIFFHashSetNew(TIFFHashSetHashFunc fnHashFunc,
                            TIFFHashSetEqualFunc fnEqualFunc,
                            TIFFHashSetFreeEltFunc fnFreeEltFunc)
{
    TIFFHashSet *set = (TIFFHashSet *)malloc(sizeof(TIFFHashSet));
    if (set == NULL)
        return NULL;
    set->fnHashFunc = fnHashFunc ? fnHashFunc : TIFFHashSetHashPointer;
    set->fnEqualFunc = fnEqualFunc ? fnEqualFunc : TIFFHashSetEqualPointer;
    set->fnFreeEltFunc = fnFreeEltFunc;
    set->nSize = 0;
    set->tabList = (TIFFList **)(calloc(53, sizeof(TIFFList *)));
    if (set->tabList == NULL)
    {
        free(set);
        return NULL;
    }
    set->nIndiceAllocatedSize = 0;
    set->nAllocatedSize = 53;
    set->psRecyclingList = NULL;
    set->nRecyclingListSize = 0;
    set->bRehash = false;
#ifdef HASH_DEBUG
    set->nCollisions = 0;
#endif
    return set;
}

/************************************************************************/
/*                          TIFFHashSetSize()                           */
/************************************************************************/

int TIFFHashSetSize(const TIFFHashSet *set)
{
    assert(set != NULL);
    return set->nSize;
}

/************************************************************************/
/*                      TIFFHashSetGetNewListElt()                      */
/************************************************************************/

/**
 * @brief Acquire a detached node from the cache or libc allocator.
 * @ingroup libtiff_private_hash
 *
 * Pops and decrements the recycling list when available, clearing its stale
 * payload pointer; a freshly allocated node is uninitialised. The caller
 * must initialise payload/link fields before linking. Does not invoke the
 * disposer or emit diagnostics.
 *
 * @param[in,out] set Non-NULL live set with a consistent recycling
 * list/count.
 * @return A detached node, or NULL if fresh-node allocation fails.
 */
static TIFFList *TIFFHashSetGetNewListElt(TIFFHashSet *set)
{
    if (set->psRecyclingList)
    {
        TIFFList *psRet = set->psRecyclingList;
        psRet->pData = NULL;
        set->nRecyclingListSize--;
        set->psRecyclingList = psRet->psNext;
        return psRet;
    }

    return (TIFFList *)malloc(sizeof(TIFFList));
}

/************************************************************************/
/*                      TIFFHashSetReturnListElt()                      */
/************************************************************************/

/**
 * @brief Recycle a detached node or free it when the cache is full.
 * @ingroup libtiff_private_hash
 *
 * Pushes the node onto the recycling list while fewer than 128 nodes are
 * cached, otherwise calls free(). Does not dispose or dereference its
 * payload; the caller must have completed element disposal/ownership
 * handling first. Returns no status and emits no diagnostic.
 *
 * @param[in,out] set Non-NULL live set.
 * @param[in] psList Detached owned node; must not be used by the caller
 * afterward.
 */
static void TIFFHashSetReturnListElt(TIFFHashSet *set, TIFFList *psList)
{
    if (set->nRecyclingListSize < 128)
    {
        psList->psNext = set->psRecyclingList;
        set->psRecyclingList = psList;
        set->nRecyclingListSize++;
    }
    else
    {
        free(psList);
    }
}

/************************************************************************/
/*                      TIFFHashSetClearInternal()                      */
/************************************************************************/

/**
 * @brief Unlink live buckets and dispose their elements as configured.
 * @ingroup libtiff_private_hash
 *
 * Calls the disposer when installed, empties each bucket, and clears
 * bRehash. Does not reset nSize or capacity metadata; Clear/Destroy callers
 * finish that bookkeeping. Does not release a pre-existing recycling list or
 * the bucket array. No status or direct diagnostic is provided.
 *
 * @param[in,out] set Non-NULL live set.
 * @param[in] bFinalize true to free live nodes; false to recycle/free them
 * through the cache.
 */
static void TIFFHashSetClearInternal(TIFFHashSet *set, bool bFinalize)
{
    assert(set != NULL);
    for (int i = 0; i < set->nAllocatedSize; i++)
    {
        TIFFList *cur = set->tabList[i];
        while (cur)
        {
            if (set->fnFreeEltFunc)
                set->fnFreeEltFunc(cur->pData);
            TIFFList *psNext = cur->psNext;
            if (bFinalize)
                free(cur);
            else
                TIFFHashSetReturnListElt(set, cur);
            cur = psNext;
        }
        set->tabList[i] = NULL;
    }
    set->bRehash = false;
}

/************************************************************************/
/*                          TIFFListDestroy()                           */
/************************************************************************/

/**
 * @brief Free a node chain without disposing its payload fields.
 * @ingroup libtiff_private_hash
 *
 * Walks psNext and frees nodes only. Appropriate for recycled nodes
 * containing stale payload values. Does not free payload objects, invoke
 * callbacks, or emit diagnostics.
 *
 * @param[in] psList Owned list head, or NULL; chain nodes become invalid.
 */
static void TIFFListDestroy(TIFFList *psList)
{
    TIFFList *psCurrent = psList;

    while (psCurrent)
    {
        TIFFList *const psNext = psCurrent->psNext;
        free(psCurrent);
        psCurrent = psNext;
    }
}

/************************************************************************/
/*                         TIFFHashSetDestroy()                         */
/************************************************************************/

void TIFFHashSetDestroy(TIFFHashSet *set)
{
    if (set)
    {
        TIFFHashSetClearInternal(set, true);
        free(set->tabList);
        TIFFListDestroy(set->psRecyclingList);
        free(set);
    }
}

#ifdef notused
/************************************************************************/
/*                          TIFFHashSetClear()                          */
/************************************************************************/

void TIFFHashSetClear(TIFFHashSet *set)
{
    TIFFHashSetClearInternal(set, false);
    set->nIndiceAllocatedSize = 0;
    set->nAllocatedSize = 53;
#ifdef HASH_DEBUG
    set->nCollisions = 0;
#endif
    set->nSize = 0;
}

/************************************************************************/
/*                         TIFFHashSetForeach()                         */
/************************************************************************/

void TIFFHashSetForeach(TIFFHashSet *set, TIFFHashSetIterEltFunc fnIterFunc,
                        void *user_data)
{
    assert(set != NULL);
    if (!fnIterFunc)
        return;

    for (int i = 0; i < set->nAllocatedSize; i++)
    {
        TIFFList *cur = set->tabList[i];
        while (cur)
        {
            if (!fnIterFunc(cur->pData, user_data))
                return;

            cur = cur->psNext;
        }
    }
}
#endif

/************************************************************************/
/*                         TIFFHashSetRehash()                          */
/************************************************************************/

/**
 * @brief Replace the bucket array using the selected prime-capacity index.
 * @ingroup libtiff_private_hash
 *
 * Allocation failure leaves the current array/nodes/elements unchanged; the
 * caller owns any capacity-index rollback. Success invokes hash callbacks,
 * relinks existing nodes, frees the old bucket array, updates capacity, and
 * clears bRehash. Payload objects and list nodes do not move, but bucket
 * views and traversal order change. HASH_DEBUG builds also update/log
 * collision statistics. No direct allocation-failure diagnostic is emitted.
 *
 * @param[in,out] set Non-NULL set with nIndiceAllocatedSize within anPrimes
 * bounds.
 * @return true after relinking into the new bucket array; false if its
 * calloc() fails.
 */
static bool TIFFHashSetRehash(TIFFHashSet *set)
{
    int nNewAllocatedSize = anPrimes[set->nIndiceAllocatedSize];
    TIFFList **newTabList =
        (TIFFList **)(calloc((size_t)nNewAllocatedSize, sizeof(TIFFList *)));
    if (newTabList == NULL)
        return false;
#ifdef HASH_DEBUG
    TIFFDebug("TIFFHASH",
              "hashSet=%p, nSize=%d, nCollisions=%d, "
              "fCollisionRate=%.02f",
              set, set->nSize, set->nCollisions,
              set->nCollisions * 100.0 / set->nSize);
    set->nCollisions = 0;
#endif
    for (int i = 0; i < set->nAllocatedSize; i++)
    {
        TIFFList *cur = set->tabList[i];
        while (cur)
        {
            const unsigned long nNewHashVal =
                set->fnHashFunc(cur->pData) % (unsigned long)nNewAllocatedSize;
#ifdef HASH_DEBUG
            if (newTabList[nNewHashVal])
                set->nCollisions++;
#endif
            TIFFList *psNext = cur->psNext;
            cur->psNext = newTabList[nNewHashVal];
            newTabList[nNewHashVal] = cur;
            cur = psNext;
        }
    }
    free(set->tabList);
    set->tabList = newTabList;
    set->nAllocatedSize = nNewAllocatedSize;
    set->bRehash = false;
    return true;
}

/************************************************************************/
/*                         TIFFHashSetFindPtr()                         */
/************************************************************************/

/**
 * @brief Find the mutable payload slot for a matching key.
 * @ingroup libtiff_private_hash
 *
 * Invokes callbacks without allocation or container mutation. The slot
 * remains part of its node across rehash, but removal/recycling/destruction
 * invalidates or repurposes it. Does not extend payload lifetime, take
 * ownership, or emit diagnostics.
 *
 * @param[in] set Non-NULL live set.
 * @param[in] elt Borrowed key supported by the hash/equality callbacks.
 * @return A borrowed address of the matching node's payload slot, or NULL if
 * absent.
 */
static void **TIFFHashSetFindPtr(TIFFHashSet *set, const void *elt)
{
    const unsigned long nHashVal =
        set->fnHashFunc(elt) % (unsigned long)set->nAllocatedSize;
    TIFFList *cur = set->tabList[nHashVal];
    while (cur)
    {
        if (set->fnEqualFunc(cur->pData, elt))
            return &cur->pData;
        cur = cur->psNext;
    }
    return NULL;
}

/************************************************************************/
/*                         TIFFHashSetInsert()                          */
/************************************************************************/

bool TIFFHashSetInsert(TIFFHashSet *set, void *elt)
{
    assert(set != NULL);
    void **pElt = TIFFHashSetFindPtr(set, elt);
    if (pElt)
    {
        if (set->fnFreeEltFunc)
            set->fnFreeEltFunc(*pElt);

        *pElt = elt;
        return true;
    }

    if (set->nSize >= 2 * set->nAllocatedSize / 3 ||
        (set->bRehash && set->nIndiceAllocatedSize > 0 &&
         set->nSize <= set->nAllocatedSize / 2))
    {
        set->nIndiceAllocatedSize++;
        if (!TIFFHashSetRehash(set))
        {
            set->nIndiceAllocatedSize--;
            if (set->fnFreeEltFunc)
                set->fnFreeEltFunc(elt);
            return false;
        }
    }

    const unsigned long nHashVal =
        set->fnHashFunc(elt) % (unsigned long)set->nAllocatedSize;
#ifdef HASH_DEBUG
    if (set->tabList[nHashVal])
        set->nCollisions++;
#endif

    TIFFList *new_elt = TIFFHashSetGetNewListElt(set);
    if (new_elt == NULL)
    {
        if (set->fnFreeEltFunc)
            set->fnFreeEltFunc(elt);
        return false;
    }
    new_elt->pData = elt;
    new_elt->psNext = set->tabList[nHashVal];
    set->tabList[nHashVal] = new_elt;
    set->nSize++;

    return true;
}

/************************************************************************/
/*                         TIFFHashSetLookup()                          */
/************************************************************************/

void *TIFFHashSetLookup(TIFFHashSet *set, const void *elt)
{
    assert(set != NULL);
    void **pElt = TIFFHashSetFindPtr(set, elt);
    if (pElt)
        return *pElt;

    return NULL;
}

/************************************************************************/
/*                     TIFFHashSetRemoveInternal()                      */
/************************************************************************/

/**
 * @brief Remove a matching element with immediate or deferred shrink handling.
 * @ingroup libtiff_private_hash
 *
 * Selects shrink handling before looking up the key. Immediate rehash
 * failure restores the capacity index and returns before removing anything.
 * Deferred handling can change index/rehash state even when the key is
 * absent. A found node is unlinked, its payload is disposed if configured,
 * its node is returned to the cache, and nSize decreases. No direct
 * diagnostic is emitted.
 *
 * @param[in,out] set Non-NULL live set.
 * @param[in] elt Borrowed removal key; may become invalid if it is the
 * disposed element.
 * @param[in] bDeferRehash true to mark a possible shrink for later rather
 * than allocating now.
 * @return true when removed; false if absent or immediate shrink allocation
 * fails.
 */
static bool TIFFHashSetRemoveInternal(TIFFHashSet *set, const void *elt,
                                      bool bDeferRehash)
{
    assert(set != NULL);
    if (set->nIndiceAllocatedSize > 0 && set->nSize <= set->nAllocatedSize / 2)
    {
        set->nIndiceAllocatedSize--;
        if (bDeferRehash)
            set->bRehash = true;
        else
        {
            if (!TIFFHashSetRehash(set))
            {
                set->nIndiceAllocatedSize++;
                return false;
            }
        }
    }

    int nHashVal =
        (int)(set->fnHashFunc(elt) % (unsigned long)set->nAllocatedSize);
    TIFFList *cur = set->tabList[nHashVal];
    TIFFList *prev = NULL;
    while (cur)
    {
        if (set->fnEqualFunc(cur->pData, elt))
        {
            if (prev)
                prev->psNext = cur->psNext;
            else
                set->tabList[nHashVal] = cur->psNext;

            if (set->fnFreeEltFunc)
                set->fnFreeEltFunc(cur->pData);

            TIFFHashSetReturnListElt(set, cur);
#ifdef HASH_DEBUG
            if (set->tabList[nHashVal])
                set->nCollisions--;
#endif
            set->nSize--;
            return true;
        }
        prev = cur;
        cur = cur->psNext;
    }
    return false;
}

/************************************************************************/
/*                         TIFFHashSetRemove()                          */
/************************************************************************/

bool TIFFHashSetRemove(TIFFHashSet *set, const void *elt)
{
    return TIFFHashSetRemoveInternal(set, elt, false);
}

#ifdef notused
/************************************************************************/
/*                    TIFFHashSetRemoveDeferRehash()                    */
/************************************************************************/

bool TIFFHashSetRemoveDeferRehash(TIFFHashSet *set, const void *elt)
{
    return TIFFHashSetRemoveInternal(set, elt, true);
}
#endif

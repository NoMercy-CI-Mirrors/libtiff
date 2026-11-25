/**********************************************************************
 * $Id$
 *
 * Name:     tif_hash_set.h
 * Project:  TIFF - Common Portability Library
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
 * @file tif_hash_set.h
 * @brief Declare the private callback-driven hash-set interface.
 *
 * Elements are unique according to the comparison callback. Hash/equality
 * keys must remain stable while stored, and equal elements must hash equally.
 * Operations are not synchronised; callbacks must not structurally mutate
 * or destroy the invoking set. See @ref shared_services for ownership rules.
 */

#ifndef TIFF_HASH_SET_H_INCLUDED
#define TIFF_HASH_SET_H_INCLUDED

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Types */

    /**
     * @brief Refer to a private hash set with callback-defined element
     * ownership.
     * @ingroup libtiff_private_hash
     *
     * Owns its buckets and nodes. Element disposal depends on fnFreeEltFunc.
     * Allocate with TIFFHashSetNew() and release with TIFFHashSetDestroy().
     */
    typedef struct _TIFFHashSet TIFFHashSet;

    /**
     * @brief Compute an element hash without changing its key.
     * @ingroup libtiff_private_hash
     *
     * The callback and any referenced external state must outlive the set.
     * It must support stored elements and lookup-key representations
     * consistently and must not mutate or destroy the set. Hash bits need
     * not be a pointer address or stable across processes.
     *
     * @param[in] elt Stored element or lookup key, including NULL only if
     * supported by the callback.
     * @return An unsigned hash; equal elements must produce equal hashes.
     */
    typedef unsigned long (*TIFFHashSetHashFunc)(const void *elt);

    /**
     * @brief Test whether a stored element matches another element or key.
     * @ingroup libtiff_private_hash
     *
     * Matching elements must have equal hashes. Both representations must be
     * accepted by the callback, with stable key fields while stored.
     * Callback code/state outlives the set; it must not structurally mutate
     * or destroy the invoking set.
     *
     * @param[in] elt1 Stored element.
     * @param[in] elt2 Incoming element or lookup/removal key.
     * @return true for equal keys; false otherwise.
     */
    typedef bool (*TIFFHashSetEqualFunc)(const void *elt1, const void *elt2);

    /**
     * @brief Dispose of an element according to the set's ownership policy.
     * @ingroup libtiff_private_hash
     *
     * Invoked on replacement, removal, clear, destruction, and failed
     * insertion of an incoming element. A failed insertion therefore does
     * not return ownership to a caller that configured a consuming disposer.
     * The callback must accept every inserted representation and must not
     * mutate or destroy the invoking set. The set adds no status/error
     * handling around disposal.
     *
     * @param[in,out] elt Element being consumed; may become invalid after
     * disposal.
     */
    typedef void (*TIFFHashSetFreeEltFunc)(void *elt);

    /* Functions */

    /**
     * @brief Allocate an empty hash set with optional ownership callbacks.
     * @ingroup libtiff_private_hash
     *
     * Uses libc malloc/calloc, independently of TIFF handles and open-option
     * allocation limits. Failure releases partial container allocations and
     * emits no library diagnostic. Hash/equality callbacks must agree and
     * remain valid until destruction; elements' hash/equality keys must
     * remain stable while stored. A configured disposer consumes elements on
     * removal/replacement/destruction and also on failed insertion. No
     * external element is owned by creation itself.
     *
     * @param[in] fnHashFunc Borrowed hash callback, or NULL for pointer
     * hashing.
     * @param[in] fnEqualFunc Borrowed equality callback, or NULL for pointer
     * equality.
     * @param[in] fnFreeEltFunc Optional element disposer; NULL leaves
     * elements caller-owned.
     * @return An owned empty set for TIFFHashSetDestroy(), or NULL if
     * set/bucket allocation fails.
     */
    TIFFHashSet *TIFFHashSetNew(TIFFHashSetHashFunc fnHashFunc,
                                TIFFHashSetEqualFunc fnEqualFunc,
                                TIFFHashSetFreeEltFunc fnFreeEltFunc);

    /**
     * @brief Release a hash set and dispose its live elements if configured.
     * @ingroup libtiff_private_hash
     *
     * NULL is accepted. Calls the disposer for each live element when
     * installed, frees buckets/live/recycled nodes, then frees the set. The
     * set and all container views become invalid. Without a disposer,
     * element allocations remain caller-owned. No status or direct
     * diagnostic is provided; disposer effects follow its contract.
     *
     * @param[in,out] set Owned set to destroy, or NULL.
     */
    void TIFFHashSetDestroy(TIFFHashSet *set);

    /**
     * @brief Return the current live-element count.
     * @ingroup libtiff_private_hash
     *
     * Reads state without allocation, callback invocation, ownership
     * transfer, or diagnostics. Debug builds assert that set is non-NULL.
     *
     * @param[in] set Non-NULL live set.
     * @return The live-element count, not the bucket capacity; no distinct
     * error result.
     */
    int TIFFHashSetSize(const TIFFHashSet *set);

#ifdef notused
    /**
     * @brief Remove every element while retaining a reusable set.
     * @ingroup libtiff_private_hash
     *
     * Conditional notused API, not built by default. Disposes live elements
     * when configured, empties buckets, resets count/capacity index and
     * logical bucket count to 53, and recycles up to the node-cache limit.
     * It does not physically shrink the existing bucket allocation here.
     * Payloads without a disposer remain caller-owned. No status or direct
     * diagnostic is provided.
     *
     * @param[in,out] set Non-NULL live set.
     */
    void TIFFHashSetClear(TIFFHashSet *set);

    /**
     * @brief Visit an element during optional hash-set iteration.
     * @ingroup libtiff_private_hash
     *
     * Available only when the notused conditional API is enabled. Do not
     * structurally modify/destroy the set. Neither pointer is retained by
     * iteration; ownership does not transfer.
     *
     * @param[in,out] elt Borrowed stored element; do not alter its
     * hash/equality key.
     * @param[in,out] user_data Borrowed caller context, possibly NULL.
     * @return Nonzero to continue; zero to stop the traversal.
     */
    typedef int (*TIFFHashSetIterEltFunc)(void *elt, void *user_data);

    /**
     * @brief Visit elements in unspecified hash traversal order.
     * @ingroup libtiff_private_hash
     *
     * Conditional notused API, not built by default. Stops when a visitor
     * returns zero or traversal ends; provides no status result and no
     * ordering guarantee. Does not allocate, retain pointers, transfer
     * ownership, or directly emit diagnostics. Visitor effects follow its
     * contract; hash/equality keys must remain unchanged.
     *
     * @param[in] set Non-NULL live set, not modified by the traversal itself;
     * structural mutation during
     * callbacks is forbidden.
     * @param[in] fnIterFunc Borrowed visitor, or NULL for a no-op.
     * @param[in,out] user_data Borrowed visitor context, possibly NULL.
     */
    void TIFFHashSetForeach(TIFFHashSet *set, TIFFHashSetIterEltFunc fnIterFunc,
                            void *user_data);
#endif

    /**
     * @brief Insert an element or replace an equal element.
     * @ingroup libtiff_private_hash
     *
     * For an equal key, disposes the previous stored element before
     * replacing its pointer; the count is unchanged. With a consuming
     * disposer, reinserting the same owned pointer as its own replacement
     * leaves a dangling pointer and is not supported. Without a disposer,
     * the caller remains responsible for replaced element storage.
     *
     * A new key can allocate/rehash buckets and allocate or recycle a node.
     * A failed insertion leaves existing membership/payloads intact, but a
     * successful rehash before a node-allocation failure may change bucket
     * layout/order. Without a disposer, a failed incoming element remains
     * caller-owned. Emits no direct diagnostic. Growth assumes an in-range
     * index into the fixed prime-capacity table; it is not an unbounded-
     * capacity contract.
     *
     * @param[in,out] set Non-NULL live set with compatible callbacks.
     * @param[in,out] elt Incoming element; consumed by the disposer on
     * failure when installed.
     * @return true when inserted/replaced; false on a required bucket/node
     * allocation failure. On false, elt has already been passed to the
     * disposer when installed.
     */
    bool TIFFHashSetInsert(TIFFHashSet *set, void *elt);

    /**
     * @brief Find a borrowed stored element matching a key.
     * @ingroup libtiff_private_hash
     *
     * Invokes hash/equality callbacks without allocation or container
     * mutation. Does not transfer element ownership. Rehashing does not move
     * payload objects, but replacement/removal/clear/destruction may dispose
     * the returned object. Do not alter stored key fields or assume the view
     * extends lifetime. Emits no direct diagnostic.
     *
     * @param[in] set Non-NULL live set.
     * @param[in] elt Borrowed lookup key accepted by the hash/equality
     * callbacks.
     * @return The matching stored pointer, or NULL if absent; a stored NULL
     * pointer cannot be distinguished from absence.
     */
    void *TIFFHashSetLookup(TIFFHashSet *set, const void *elt);

    /**
     * @brief Remove a matching element, disposing it if configured.
     * @ingroup libtiff_private_hash
     *
     * May allocate a smaller bucket table before looking for the key, even
     * if the key is absent. Rehash failure returns false before removal and
     * preserves membership. Successful removal unlinks the node, calls the
     * disposer if installed, recycles/frees the node, and decrements the
     * count. Without a disposer, payload storage stays caller-owned. No
     * direct diagnostic or ownership of an unmatched key is taken.
     *
     * @param[in,out] set Non-NULL live set.
     * @param[in] elt Borrowed removal key; may alias a stored object that
     * disposal invalidates.
     * @return true when removed; false when absent or when a preliminary
     * shrink rehash allocation fails. These failure cases are not
     * distinguished.
     */
    bool TIFFHashSetRemove(TIFFHashSet *set, const void *elt);

#ifdef notused
    /**
     * @brief Remove an element while deferring a possible shrink rehash.
     * @ingroup libtiff_private_hash
     *
     * Conditional notused API, not built by default. Uses
     * TIFFHashSetRemoveInternal() with deferred rehashing, so it does not
     * allocate a shrink table on this path. Can update deferred
     * capacity/rehash state even for an absent key. Disposal, node
     * recycling, and element ownership follow TIFFHashSetRemove(); a later
     * operation handles rehashing according to its threshold logic, not a
     * guaranteed immediate shrink.
     *
     * @param[in,out] set Non-NULL live set.
     * @param[in] elt Borrowed key; removal may dispose a matching stored
     * object.
     * @return true when removed; false when absent.
     */
    bool TIFFHashSetRemoveDeferRehash(TIFFHashSet *set, const void *elt);
#endif

#ifdef __cplusplus
}
#endif

#endif /* TIFF_HASH_SET_H_INCLUDED */

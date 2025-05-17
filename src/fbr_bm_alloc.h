/* See LICENSE file for copyright and license details. */

#ifndef _FBR_BM_ALLOC_H
#define _FBR_BM_ALLOC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <ck_bitmap.h>

#include <fbr_errno.h>

#include "fbr_debug.h"
#include "fbr_platform.h"

struct fbr_bm_alloc_meta {
	ck_bitmap_t *bm;
};

struct fbr_bm_alloc_iterator {
	ck_bitmap_iterator_t bm_iter;
};

/* I'm not forking ck to implement one silly function */
CK_CC_INLINE static bool ck_bitmap_next_unset(const struct ck_bitmap *bitmap,
					      struct ck_bitmap_iterator *i,
					      unsigned int *bit)
{
	unsigned int cache = i->cache;
	unsigned int n_block = i->n_block;
	unsigned int n_limit = i->n_limit;

	if (cache == UINT_MAX) {
		if (n_block >= n_limit)
			return false;

		for (n_block++; n_block < n_limit; n_block++) {
			cache = ck_pr_load_uint(&bitmap->map[n_block]);
			if (cache != UINT_MAX)
				goto not_max;
		}

		i->cache = UINT_MAX;
		i->n_block = n_block;
		return false;
	}

not_max:
	*bit = (unsigned int)CK_BITMAP_BLOCK * n_block +
	       (unsigned int)ck_cc_ctz(~cache);
	i->cache = cache | (cache + 1);
	i->n_block = n_block;
	return true;
}

#define FBR_BM_ALLOC_CAP(meta_ptr) ((meta_ptr)->bm->n_bits)

inline static size_t fbr_bm_alloc_size_base(uint32_t entries)
{
	size_t base_size = ck_bitmap_size(entries);
	return FBR_SIZE_ROUND_CACHELINE(base_size);
}

inline static size_t fbr_bm_alloc_size_entries(uint32_t entries,
					       size_t entry_size)
{
	size_t entries_size = entries * entry_size;
	return FBR_SIZE_ROUND_MIN_ALIGNMENT(entries_size);
}

inline static size_t fbr_bm_alloc_size(uint32_t entries, size_t entry_size)
{
	return fbr_bm_alloc_size_base(entries) +
	       fbr_bm_alloc_size_entries(entries, entry_size);
}

inline static void *fbr_bm_alloc_init(struct fbr_bm_alloc_meta *meta,
				      size_t entry_size, unsigned int entries,
				      void *buffer, size_t buffer_size)
{
	size_t base_size;
	size_t entries_size;
	void *arr_ptr;

	fbr_assert(meta != NULL);
	fbr_assert(buffer != NULL);
	fbr_assert(fbr_aligned(buffer, FBR_ALIGNMENT_MIN));

	base_size = fbr_bm_alloc_size_base(entries);
	entries_size = fbr_bm_alloc_size_entries(entries, entry_size);
	if (buffer_size < base_size + entries_size) {
		fbr_panic(FBR_ENOMEM);
	}

	/* Bitmap will be first */
	meta->bm = buffer;
	ck_bitmap_init(meta->bm, entries, 0);

	/* Array will be just after bitmap. This will be aligned properly */
	arr_ptr = (void *)((uintptr_t)buffer + base_size);
	fbr_assert(fbr_aligned(arr_ptr, FBR_ALIGNMENT_MIN));

	return arr_ptr;
}

inline static fbr_errno_t fbr_bm_malloc(struct fbr_bm_alloc_meta *meta,
					unsigned int *index)
{
	ck_bitmap_iterator_t iter;
	unsigned int free_idx;

	fbr_assert(meta != NULL);
	fbr_assert(index != NULL);

	ck_bitmap_iterator_init(&iter, meta->bm);
	while (ck_bitmap_next_unset(meta->bm, &iter, &free_idx) &&
	       free_idx < FBR_BM_ALLOC_CAP(meta)) {
		bool old = ck_bitmap_bts(meta->bm, free_idx);
		/* Bit's old value was 0. Successfully grabbed index */
		if (!old) {
			*index = free_idx;
			return FBR_EOK;
		}
	}
	return FBR_ENOMEM;
}

inline static void fbr_bm_free(struct fbr_bm_alloc_meta *meta,
			       unsigned int index)
{
	fbr_assert(meta != NULL);
	fbr_assert(index < meta->bm->n_bits);
	fbr_assert(ck_bitmap_test(meta->bm, index));

	ck_bitmap_reset(meta->bm, index);
}

inline static void fbr_bm_iterator_init(struct fbr_bm_alloc_meta *meta,
					struct fbr_bm_alloc_iterator *iter)
{
	ck_bitmap_iterator_init(&iter->bm_iter, meta->bm);
}

inline static bool fbr_bm_iterator_next(struct fbr_bm_alloc_meta *meta,
					struct fbr_bm_alloc_iterator *iter,
					unsigned int *idx)
{
	bool res = ck_bitmap_next(meta->bm, &iter->bm_iter, idx);
	if (*idx >= FBR_BM_ALLOC_CAP(meta)) {
		return false;
	}
	return res;
}

#endif /* _FBR_BM_ALLOC_H */

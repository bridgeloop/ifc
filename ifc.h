/*
	ISC License
	
	Copyright (c) 2022, aiden (aiden@cmp.bz)
	
	Permission to use, copy, modify, and/or distribute this software for any
	purpose with or without fee is hereby granted, provided that the above
	copyright notice and this permission notice appear in all copies.
	
	THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
	WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
	MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
	ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
	WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
	ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
	OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#ifndef IFC_H
#define IFC_H

#include <pthread.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>

struct ifc_head {
	const unsigned int n;
	const unsigned short int area_sz;
	const unsigned short int padding_sz;
};
struct ifc_tid {
	_Atomic unsigned char occupied;
	pthread_t tid;
};

#define _ifc_iter(ifc, area) *area = NULL; (area = ifc_reap(ifc, area)) != NULL;)
#define ifc_iter(type) for (type _ifc_iter

#define IFC_OFFSET(ifc, x) ((void *)((unsigned char *)ifc + x))
#define IFC_HEAD(ifc) ((struct ifc_head *)IFC_OFFSET(ifc, 0))
#define IFC_TID(ifc) ((struct ifc_tid *)IFC_OFFSET(ifc, sizeof(struct ifc_head)))
#define IFC_AREAS(ifc) ((unsigned char *)IFC_OFFSET(ifc, sizeof(struct ifc_head) + (sizeof(struct ifc_tid) * IFC_HEAD(ifc)->n) + IFC_HEAD(ifc)->padding_sz))
#define IFC_AREA(ifc, idx) ((void *)(IFC_AREAS(ifc) + (IFC_HEAD(ifc)->area_sz * (idx))))

struct ifc;

static void ifc_free(struct ifc *ifc) {
	free(ifc);
	return;
}

static struct ifc *ifc_alloc(unsigned int n, unsigned short int sz) {
	if (n == 0) {
		return NULL;
	}
	unsigned long int sz_before_padding =
		sizeof(struct ifc_head) +
		(sizeof(struct ifc_tid) * n); // tid
	unsigned short int
		cl_sz,
		area_sz,
		padding_sz;
	if (sz == 0) {
		cl_sz = sizeof(void *);
		area_sz = 0;
		padding_sz = 0;
	} else {
		#if defined(_SC_LEVEL1_DCACHE_LINESIZE)
		long int s = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
		#elif defined(__APPLE__)
		int sysctlbyname(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen);
		int s;
		{
			size_t sz;
			if (sysctlbyname("hw.cachelinesize", &(s), &(sz), NULL, 0) != 0) {
				s = 0;
			}
			assert(sz == sizeof(int));
		}
		#else
		long int s = 0;
		#endif
		if (s <= 0 || s > (unsigned short int)~0) {
			return NULL;
		}
		cl_sz = (unsigned short int)s;
		unsigned short int diff = (cl_sz - (sz % cl_sz)) % cl_sz;
		if ((unsigned short int)~0 - sz < diff) {
			return NULL;
		}
		area_sz = sz + diff;
		padding_sz = (cl_sz - (sz_before_padding % cl_sz)) % cl_sz;
	}
	void *ifc;
	if (
		posix_memalign(
			&(ifc),
			cl_sz,
			(
				sz_before_padding +
				padding_sz +
				(area_sz * n)
			)
		) != 0
	) {
		return NULL;
	}
	struct ifc_head *head = IFC_HEAD(ifc);
	*(unsigned int *)&(head->n) = n;
	*(unsigned short int *)&(head->area_sz) = area_sz;
	*(unsigned short int *)&(head->padding_sz) = padding_sz;
	struct ifc_tid *tid = IFC_TID(ifc);
	for (unsigned int idx = 0; idx < n; ++idx) {
		tid[idx].occupied = 0;
	}
	// IFC_AREAS are purposefully uninitialised
	return (struct ifc *)ifc;
}

static void *ifc_area(struct ifc *ifc) {
	pthread_t self = pthread_self();

	struct ifc_head *head = IFC_HEAD(ifc);
	unsigned int
		n = head->n,
		likely_unoccupied = head->n;
	struct ifc_tid *tid = IFC_TID(ifc);
	for (unsigned int idx = 0; idx < n; ++idx) {
		if (!tid[idx].occupied) {
			likely_unoccupied = idx;
			continue;
		}
		if (pthread_equal(tid[idx].tid, self)) {
			return IFC_AREA(ifc, idx);
		}
	}

	for (unsigned int idx = likely_unoccupied; idx < n; ++idx) {
		if (!__atomic_test_and_set(&(tid[idx].occupied), __ATOMIC_RELAXED)) {
			tid[idx].tid = self;
			return IFC_AREA(ifc, idx);
		}
	}
	for (unsigned int idx = 0; idx < likely_unoccupied; ++idx) {
		if (!__atomic_test_and_set(&(tid[idx].occupied), __ATOMIC_RELAXED)) {
			tid[idx].tid = self;
			return IFC_AREA(ifc, idx);
		}
	}

	return NULL;
}

static void ifc_release(struct ifc *ifc, void *area) {
	struct ifc_head *head = IFC_HEAD(ifc);
	struct ifc_tid *tid = IFC_TID(ifc);
	size_t idx = (size_t)((unsigned char *)area - IFC_AREAS(ifc)) / head->area_sz;
	assert(pthread_equal(tid[idx].tid, pthread_self()));
	__atomic_clear(&(tid[idx].occupied), __ATOMIC_RELEASE);
	return;
}

static inline void *ifc_reap(struct ifc *ifc, void *area) {
	struct ifc_head *head = IFC_HEAD(ifc);
	if (area == NULL) {
		if (head->area_sz == 0) {
			return NULL;
		}
		return IFC_AREA(ifc, 0);
	}
	area = (void *)((size_t)area + head->area_sz);
	if (area == IFC_AREA(ifc, head->n + 1)) {
		return NULL;
	}
	return area;
}

#endif

#ifndef IFC_H
#define IFC_H

#include <pthread.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>

struct ifc_head {
	const size_t n;
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

static struct ifc *ifc_alloc(size_t n, unsigned short int sz) {
	if (n == 0) {
		return NULL;
	}
	size_t sz_before_padding =
		sizeof(struct ifc_head) +
		(sizeof(struct ifc_tid) * n); // tid
	assert(sysconf(_SC_LEVEL1_DCACHE_LINESIZE) <= (unsigned short int)~0);
	unsigned short int
		cl_sz,
		area_sz,
		padding_sz;
	if (sz == 0) {
		cl_sz = sizeof(void *);
		area_sz = 0;
		padding_sz = 0;
	} else {
		cl_sz = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
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
	*(size_t *)&(head->n) = n;
	*(unsigned short int *)&(head->area_sz) = area_sz;
	*(unsigned short int *)&(head->padding_sz) = padding_sz;
	struct ifc_tid *tid = IFC_TID(ifc);
	for (size_t idx = 0; idx < n; ++idx) {
		tid[idx].occupied = 0;
	}
	// IFC_AREAS are purposefully uninitialised
	return (struct ifc *)ifc;
}

static void *ifc_area(struct ifc *ifc) {
	pthread_t self = pthread_self();

	struct ifc_head *head = IFC_HEAD(ifc);
	size_t n = head->n;
	struct ifc_tid *tid = IFC_TID(ifc);
	size_t likely_unoccupied = n;
	for (size_t idx = 0; idx < n; ++idx) {
		if (!__atomic_load_n(&(tid[idx].occupied), __ATOMIC_RELAXED)) {
			likely_unoccupied = idx;
			continue;
		}
		if (pthread_equal(tid[idx].tid, self)) {
			return IFC_AREA(ifc, idx);
		}
	}

	#pragma GCC unroll 2
	for (int it = 0; it < 2; ++it) {
		for (size_t idx = likely_unoccupied; idx < n; ++idx) {
			if (!__atomic_test_and_set(&(tid[idx].occupied), __ATOMIC_RELAXED)) {
				tid[idx].tid = self;
				return IFC_AREA(ifc, idx);
			}
		}
		n = likely_unoccupied;
		likely_unoccupied = 0;
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
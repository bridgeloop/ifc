#ifndef IFC_H
#define IFC_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define IFC_OFFSET(ifc, x) ((void *)((unsigned char *)ifc + x))
#define IFC_OCCUPIED(ifc) ((_Atomic size_t *)IFC_OFFSET(ifc, 0))
#define IFC_N(ifc) (*(size_t *)IFC_OFFSET(ifc, sizeof(size_t)))
#define IFC_RAND(ifc) ((unsigned int *)IFC_OFFSET(ifc, (sizeof(size_t) * 2)))
#define IFC_CLSZ(ifc) (*(unsigned short int *)IFC_OFFSET(ifc, (sizeof(size_t) * 2) + sizeof(unsigned int)))
#define IFC_PADSZ(ifc) (*(unsigned short int *)IFC_OFFSET(ifc, (sizeof(size_t) * 2) + sizeof(unsigned int) + sizeof(unsigned short int)))
#define IFC_TID(ifc) ((pthread_t *)IFC_OFFSET(ifc, (sizeof(size_t) * 2) + sizeof(unsigned int) + (sizeof(unsigned short int) * 2)))
#define IFC_CNT(ifc, n) ((unsigned char *)IFC_OFFSET(ifc, (sizeof(size_t) * 2) + sizeof(unsigned int) + (sizeof(unsigned short int)) * 2 + (sizeof(pthread_t) * n) + IFC_PADSZ(ifc)))
#define IFC_IDX(ifc, n, idx) ((_Atomic size_t *)(IFC_CNT(ifc, n) + (IFC_CLSZ(ifc) * idx)))

struct ifc;

static struct ifc *ifc_alloc(size_t n) {
	size_t sz_before_padding =
		sizeof(_Atomic size_t) + // occupied
		sizeof(size_t) + // n
		sizeof(unsigned int) + // rand seed
		sizeof(unsigned short int) + // cacheline size
		sizeof(unsigned short int) + // padding size
		(sizeof(pthread_t) * n); // tid
	unsigned short int cl_sz = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
	if (cl_sz < sizeof(size_t)) {
		cl_sz = sizeof(size_t);
	}
	unsigned short int padding_sz = (cl_sz - (sz_before_padding % cl_sz)) % cl_sz;
	void *ifc;
	posix_memalign(
		&(ifc),
		cl_sz,
		(
			sz_before_padding +
			padding_sz + // padding
			(cl_sz * n) // ifc
		)
	);
	if (ifc == NULL) {
		return NULL;
	}
	*IFC_OCCUPIED(ifc) = 0;
	IFC_N(ifc) = n;
	// IFC_RAND is purposefully uninitialised
	IFC_CLSZ(ifc) = cl_sz;
	IFC_PADSZ(ifc) = padding_sz;
	// IFC_CNT is purposefully uninitialised
	return (struct ifc *)ifc;
}

static void ifc_free(struct ifc *ifc) {
	free(ifc);
	return;
}

static size_t ifc_id(struct ifc *ifc) {
	pthread_t self = pthread_self();

	_Atomic size_t *occupied = IFC_OCCUPIED(ifc);
	pthread_t *tid = IFC_TID(ifc);
	size_t exp = *occupied;
	for (size_t idx = 0; idx < exp; ++idx) {
		if (pthread_equal(tid[idx], self)) {
			return idx;
		}
	}

	size_t n = IFC_N(ifc);
	do {
		if (exp == n) {
			return rand_r(IFC_RAND(ifc)) % n;
		}
	} while (!atomic_compare_exchange_weak(occupied, &(exp), exp + 1));

	memcpy(&(tid[exp]), &(self), sizeof(pthread_t));
	return exp;
}

#ifndef NDEBUG
#include <stdio.h>
#endif

inline static void ifc_inc(struct ifc *ifc, size_t id) {
	size_t n = IFC_N(ifc);
	#ifndef NDEBUG
	if (id >= *IFC_OCCUPIED(ifc)) {
		fputs("ifc_inc: id is out-of-bounds\n", stderr);
		abort();
	}
	#endif
	*IFC_IDX(ifc, n, id) += 1;
	return;
}

static size_t ifc_sum(struct ifc *ifc) {
	size_t occupied = *IFC_OCCUPIED(ifc);
	size_t n = IFC_N(ifc);

	size_t output = 0;
	for (size_t idx = 0; idx < occupied; ++idx) {
		output += *IFC_IDX(ifc, n, idx);
	}

	return output;
}

#endif
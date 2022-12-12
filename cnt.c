#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define CNT_OFFSET(cnt, x) ((void *)((unsigned char *)cnt + x))
#define CNT_OCCUPIED(cnt) ((_Atomic size_t *)CNT_OFFSET(cnt, 0))
#define CNT_N(cnt) (*(size_t *)CNT_OFFSET(cnt, sizeof(size_t)))
#define CNT_RAND(cnt) ((unsigned int *)CNT_OFFSET(cnt, (sizeof(size_t) * 2)))
#define CNT_CLSZ(cnt) (*(unsigned short int *)CNT_OFFSET(cnt, (sizeof(size_t) * 2) + sizeof(unsigned int)))
#define CNT_PADSZ(cnt) (*(unsigned short int *)CNT_OFFSET(cnt, (sizeof(size_t) * 2) + sizeof(unsigned int) + sizeof(unsigned short int)))
#define CNT_TID(cnt) ((pthread_t *)CNT_OFFSET(cnt, (sizeof(size_t) * 2) + sizeof(unsigned int) + (sizeof(unsigned short int) * 2)))
#define CNT_CNT(cnt, n) ((unsigned char *)CNT_OFFSET(cnt, (sizeof(size_t) * 2) + sizeof(unsigned int) + (sizeof(unsigned short int)) * 2 + (sizeof(pthread_t) * n) + CNT_PADSZ(cnt)))
#define CNT_IDX(cnt, n, idx) ((_Atomic size_t *)(CNT_CNT(cnt, n) + (CNT_CLSZ(cnt) * idx)))

struct cnt;

struct cnt *cnt_alloc(size_t n) {
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
	void *cnt;
	posix_memalign(
		&(cnt),
		cl_sz,
		(
			sz_before_padding +
			padding_sz + // padding
			(cl_sz * n) // cnt
		)
	);
	if (cnt == NULL) {
		return NULL;
	}
	*CNT_OCCUPIED(cnt) = 0;
	CNT_N(cnt) = n;
	// CNT_RAND is purposefully uninitialised
	CNT_CLSZ(cnt) = cl_sz;
	CNT_PADSZ(cnt) = padding_sz;
	for (size_t it = 0; it < n; ++it) {
		*CNT_IDX(cnt, n, it) = 0;
	}
	return (struct cnt *)cnt;
}

void cnt_free(struct cnt *cnt) {
	free(cnt);
	return;
}

size_t cnt_id(struct cnt *cnt) {
	pthread_t self = pthread_self();

	_Atomic size_t *occupied = CNT_OCCUPIED(cnt);
	pthread_t *tid = CNT_TID(cnt);
	size_t exp = *occupied;
	for (size_t idx = 0; idx < exp; ++idx) {
		if (pthread_equal(tid[idx], self)) {
			return idx;
		}
	}

	size_t n = CNT_N(cnt);
	do {
		if (exp == n) {
			return rand_r(CNT_RAND(cnt)) % n;
		}
	} while (!atomic_compare_exchange_weak(occupied, &(exp), exp + 1));

	memcpy(&(tid[exp]), &(self), sizeof(pthread_t));
	return exp;
}

#ifndef NDEBUG
#include <stdio.h>
#endif

inline void cnt_inc(struct cnt *cnt, size_t id) {
	#ifndef NDEBUG
	size_t n = CNT_N(cnt);
	if (id >= n) {
		fputs("cnt_inc: id is out-of-bounds\n", stderr);
		abort();
	}
	#endif
	*CNT_IDX(cnt, n, id) += 1;
	return;
}

size_t cnt_sum(struct cnt *cnt) {
	size_t occupied = *CNT_OCCUPIED(cnt);
	size_t n = CNT_N(cnt);

	size_t output = 0;
	for (size_t idx = 0; idx < occupied; ++idx) {
		output += *CNT_IDX(cnt, n, idx);
	}

	return output;
}

#define N_THREADS 16

#include <stdio.h>
double rc(void) {
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &(now));
    return now.tv_sec + (now.tv_nsec * 1e-9);
}
void clk(void) {
	static double c = 0;
	if (c == 0) {
		c = rc();
	} else {
		printf("%lfs\n", rc() - c);
		c = 0;
	}
	return;
}

void *thread_main(void *_cnt) {
	struct cnt *cnt = _cnt;
	pthread_t shit = cnt_id(cnt);
	for (size_t it = 0; it < 0x10000; ++it) {
		cnt_inc(cnt, shit);
	}
	return NULL;
}

char spawn(struct cnt **cnt) {
	*cnt = cnt_alloc(16);
	if (cnt == NULL) {
		return 0;
	}
	pthread_t threads[N_THREADS];
	for (size_t it = 0; it < N_THREADS; ++it) {
		if (pthread_create(&(threads[it]), NULL, &(thread_main), *cnt) != 0) {
			for (size_t it2 = 0; it2 < it; ++it2) {
				pthread_cancel(threads[it2]);
				pthread_join(threads[it2], NULL);
			}
			cnt_free(*cnt);
			return 0;
		}
	}
	for (size_t it = 0; it < N_THREADS; ++it) {
		pthread_join(threads[it], NULL);
	}
	return 1;
}

int main(void) {
	struct cnt *cnt;
	clk();
	char s = spawn(&(cnt));
	if (!s) {
		exit(1);
	}
	clk();
	printf("%zu\n", cnt_sum(cnt));
	cnt_free(cnt);
	return 0;
}
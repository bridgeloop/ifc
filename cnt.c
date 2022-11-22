#define _GNU_SOURCE

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>

#define CNT_OFFSET(cnt, x) ((void *)((unsigned char *)cnt + x))
#define CNT_OCCUPIED(cnt) ((_Atomic size_t *)CNT_OFFSET(cnt, 0))
#define CNT_TID(cnt) ((pthread_t *)CNT_OFFSET(cnt, 8))
#define CNT_CLSZ(cnt, n) (*(unsigned short int *)CNT_OFFSET(cnt, 8 + (sizeof(pthread_t) * n)))
#define CNT_PADSZ(cnt, n) (*(unsigned short int *)CNT_OFFSET(cnt, 10 + (sizeof(pthread_t) * n)))
#define CNT_CNT(cnt, n) ((unsigned char *)CNT_OFFSET(cnt, 12 + (sizeof(pthread_t) * n) + CNT_PADSZ(cnt, n)))
#define CNT_IDX(cnt, n, idx) ((_Atomic size_t *)(CNT_CNT(cnt, n) + (CNT_CLSZ(cnt, n) * idx)))

void *cnt_alloc(size_t n) {
	size_t sz_before_padding =
		sizeof(size_t) + // occupied
		(sizeof(pthread_t) * n) + // tid
		sizeof(unsigned short int) + // cacheline size
		sizeof(unsigned short int); // padding size
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
	*&(CNT_CLSZ(cnt, n)) = cl_sz;
	*&(CNT_PADSZ(cnt, n)) = padding_sz;
	for (size_t it = 0; it < n; ++it) {
		*CNT_IDX(cnt, n, it) = 0;
	}
	return cnt;
}

void cnt_free(void *cnt) {
	free(cnt);
	return;
}

void cnt_inc(void *cnt, size_t n, pthread_t self) {
	_Atomic size_t *occupied = CNT_OCCUPIED(cnt);
	pthread_t *tid = CNT_TID(cnt);

	size_t exp = *occupied;
	for (size_t idx = 0; idx < exp; ++idx) {
		if (tid[idx] == self) {
			*CNT_IDX(cnt, n, idx) += 1;
			return;
		}
	}

	do {
		if (exp == n) {
			size_t idx = rand_r(NULL) % n;
			*CNT_IDX(cnt, n, idx) += 1;
			return;
		}
	} while (!atomic_compare_exchange_weak(occupied, &(exp), exp + 1));

	tid[exp] = self;
	*CNT_IDX(cnt, n, exp) = 1;

	return;
}

size_t cnt_sum(void *cnt, size_t n) {
	size_t occupied = *CNT_OCCUPIED(cnt);

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

void *thread_main(void *cnt) {
	pthread_t shit = pthread_self();
	for (size_t it = 0; it < 0x10000; ++it) {
		cnt_inc(cnt, N_THREADS, shit);
	}
	return NULL;
}

char spawn(void **cnt) {
	*cnt = cnt_alloc(N_THREADS);
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
	void *cnt;
	clk();
	char s = spawn(&(cnt));
	if (!s) {
		exit(1);
	}
	clk();
	printf("%zu\n", cnt_sum(cnt, N_THREADS));
	cnt_free(cnt);
	return 0;
}
#include "ifc.h"

#define N_THREADS 16

#include <stdio.h>
double rc(void) {
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &(now));
    return now.tv_sec + (now.tv_nsec * 1e-9);
}
void clk(double *c) {
	if (*c == 0) {
		*c = rc();
	} else {
		printf("%lfs\n", rc() - *c);
		*c = 0;
	}
	return;
}

void *thread_main(void *_ifc) {
	struct ifc *ifc = _ifc;
	size_t *area = ifc_area(ifc);
	*area = 0;
	for (size_t it = 0; it < 0x10000; ++it) {
		++(*area);
	}
	return NULL;
}

char spawn(struct ifc **ifc) {
	*ifc = ifc_alloc(N_THREADS, sizeof(size_t));
	if (ifc == NULL) {
		return 0;
	}
	pthread_t threads[N_THREADS];
	for (size_t it = 0; it < N_THREADS; ++it) {
		if (pthread_create(&(threads[it]), NULL, &(thread_main), *ifc) != 0) {
			for (size_t it2 = 0; it2 < it; ++it2) {
				pthread_cancel(threads[it2]);
				pthread_join(threads[it2], NULL);
			}
			ifc_free(*ifc);
			return 0;
		}
	}
	for (size_t it = 0; it < N_THREADS; ++it) {
		pthread_join(threads[it], NULL);
	}
	return 1;
}

int main(void) {
	struct ifc *ifc;
	double c = 0;
	clk(&(c));
	char s = spawn(&(ifc));
	if (!s) {
		exit(1);
	}
	clk(&(c));
	c = 0;
	clk(&(c));
	size_t sum = 0;
	ifc_iter(size_t)(ifc, area) {
		sum += *area;
	}
	clk(&(c));
	printf("%zu\n", sum);
	ifc_free(ifc);
	return 0;
}
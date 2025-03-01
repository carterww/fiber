#ifndef _FIBER_TEST_BUSY_WAIT_H
#define _FIBER_TEST_BUSY_WAIT_H

#include <stdlib.h>
#include <sys/time.h>

static void mssleep_busy_wait(unsigned long ms)
{
	struct timeval t;
	unsigned long breakat, now;

	gettimeofday(&t, NULL);
	if (sizeof(unsigned long) >= 8) {
		breakat = (t.tv_sec * 1000) + (t.tv_usec / 1000) + ms;
		do {
			gettimeofday(&t, NULL);
			now = (t.tv_sec * 1000) + (t.tv_usec / 1000);
		} while (breakat > now);
	} else {
		breakat = t.tv_sec + (t.tv_usec / 1000000) +
			  (ms <= 1000 ? 1 : ms / 1000);
		do {
			gettimeofday(&t, NULL);
			now = t.tv_sec + (t.tv_usec / 1000000);
		} while (breakat > now);
	}
}

#endif /* _FIBER_TEST_BUSY_WAIT_H */

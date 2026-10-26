#include <internal/types.h>

struct timespec {
	long tv_sec;
	long tv_nsec;
};

int nanosleep(const struct timespec *duration, struct timespec *rem);
unsigned int sleep(unsigned sec);

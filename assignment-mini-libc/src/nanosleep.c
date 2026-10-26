// SPDX-License-Identifier: BSD-3-Clause

#include <errno.h>
#include <internal/syscall.h>
#include <internal/io.h>
#include <time.h>

int nanosleep(const struct timespec *duration, struct timespec *rem)
{
	int ret = syscall(35, duration, rem);

	if (ret < 0) {
		errno = -ret;
		return -1;
	}
	return 0;
}

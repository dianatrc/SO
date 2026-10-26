// SPDX-License-Identifier: BSD-3-Clause

#include <errno.h>
#include <internal/syscall.h>
#include <stddef.h>
#include <internal/io.h>
#include <time.h>

unsigned int sleep(unsigned int sec)
{
	struct timespec seconds;

	seconds.tv_sec = sec;
	seconds.tv_nsec = 0;
	int ret = nanosleep(&seconds, NULL);

	if (ret < 0)
		return seconds.tv_sec;
	return 0;
}

// SPDX-License-Identifier: BSD-3-Clause

#include <errno.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

int puts(const char *str)
{
	size_t i = 0;
	const char *s = str;

	while (*s) {
		i++;
		s++;
	}
	int ret = write(1, str, i);

	if (ret < 0) {
		errno = -ret;
		return -1;
	}
	ret = write(1, "\n", 1);

	if (ret < 0) {
		errno = -ret;
		return -1;
	}
	return 0;
}

// SPDX-License-Identifier: BSD-3-Clause

#include <sys/mman.h>
#include <errno.h>
#include <internal/syscall.h>

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
	/* TODO: Implement mmap(). */
	long ret = syscall(9, (long)addr, (long)length,
		(long)prot, (long)flags, (long)fd, (long)offset);
	if (ret < 0) {
		errno = -ret;
		return MAP_FAILED;
	}
	return (void*)ret;
}

void *mremap(void *old_address, size_t old_size, size_t new_size, int flags)
{
	/* TODO: Implement mremap(). */
	long ret = syscall(25, (long)old_address, (long)old_size,
		(long)new_size, (long)flags, (long)NULL);
	if (ret < 0) {
		errno = -ret;
		return MAP_FAILED;
	}
	return (void*)ret;
}

int munmap(void *addr, size_t length)
{
	/* TODO: Implement munmap(). */
	long ret = syscall(11, (long)addr, (long)length);
	if (ret < 0) {
		errno = -ret;
		return -1;
	}
	return 0;
}
